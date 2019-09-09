/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2018 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <strings.h>
#include <string.h>
#include <pwd.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <pthread.h>
#include <slurm/spank.h>
#include <json/json_util.h>
#include "ldms.h"
#include <assert.h>
#include "../ldmsd/ldmsd_stream.h"

static char *stream;
#define SLURM_NOTIFY_TIMEOUT 5
static time_t io_timeout = SLURM_NOTIFY_TIMEOUT;

static void msglog(const char *format, ...)
{
	char buff[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buff, sizeof(buff), format, ap);
	va_end(ap);
	slurm_info("%s", buff);
}

/*
 * From the spank.h header file
 *
 *   slurmd
 *        `-> slurmd_init()
 *        |
 *        `-> job_prolog()
 *        |
 *        | `-> slurmstepd
 *        |      `-> init ()
 *        |       -> process spank options
 *        |       -> init_post_opt ()
 *        |      + drop privileges (initgroups(), seteuid(), chdir())
 *        |      `-> user_init ()
 *        |      + for each task
 *        |      |       + fork ()
 *        |      |       |
 *        |      |       + reclaim privileges
 *        |      |       `-> task_init_privileged ()
 *        |      |       |
 *        |      |       + become_user ()
 *        |      |       `-> task_init ()
 *        |      |       |
 *        |      |       + execve ()
 *        |      |
 *        |      + reclaim privileges
 *        |      + for each task
 *        |      |     `-> task_post_fork ()
 *        |      |
 *        |      + for each task
 *        |      |       + wait ()
 *        |      |          `-> task_exit ()
 *        |      `-> exit ()
 *        |
 *        `---> job_epilog()
 *        |
 *        `-> slurmd_exit()
 *
 *   In srun only the init(), init_post_opt() and local_user_init(), and exit()
 *    callbacks are used.
 *
 *   In sbatch/salloc only the init(), init_post_opt(), and exit() callbacks
 *    are used.
 *
 *   In slurmd proper, only the slurmd_init(), slurmd_exit(), and
 *    job_prolog/epilog callbacks are used.

 */
/*
 * This is a SLURM SPANK plugin sends 'events' to LDMSD plugins.
 *
 * Events are JSon formatted objects sent over the LDMS transport
 * to a set of plugins configured to receive them.
 *
 * Events have the following syntax:
 *
 *   {
 *      "event"     : <event-type>,
 *      "timestamp" : <timestamp>
 *      "context"   : <spank-context-name>
 *      "data"      : { <event-specific data> }
 *   }
 *
 * Init Event ("init") - Start of Job
 *
 *   "data" : {
 *        "id"     : <integer>		// S_JOB_ID
 *        "name"   : <string>		// getenv("SLURM_JOB_NAME")
 *        "uid"    : <integer>		// S_JOB_UID
 *        "gid"    : <integer>		// S_JOB_GID
 *        "ncpus"  : <integer>		// S_JOB_NCPUS
 *        "nnodes" : <integer>		// S_JOB_NNODES
 *        "local_tasks" : <integer>	// S_JOB_LOCAL_TASK_COUNT
 *        "total_tasks" : <integer>	// S_JOB_TOTAL_TASK_COUNT
 *   }
 *
 * Task Init ("task_init") - Start of each process (task) for the job on the node
 *
 *   "data" : {
 *        "id"          : <integer>	// S_JOB_ID
 *        "task_id"     : <integer>	// S_TASK_ID
 *        "global_id"   : <integer>	// S_TASK_GLOBAL_ID
 *        "task_pid"    : <integer>	// S_TASK_PID
 *   }
 *
 * Task Exit ("task_exit") - End of each process (task) for the job
 *
 *   "data" : {
 *        "id"          : <integer>	// S_JOB_ID
 *        "task_id"     : <integer>	// S_TASK_ID
 *        "global_id"   : <integer>	// S_TASK_GLOBAL_ID
 *        "task_pid"    : <integer>	// S_TASK_PID
 *        "task_exit_status" : <integer>// S_TASK_EXIT_STATUS
 *   }
 *
 * Exit Event("exit") - called after all tasks have exited
 *
 *   "data" : {
 *        "id"              : <integer>	// S_JOB_ID
 * 	  "job_exit_status" : <integer>	// S_TASK_EXIT_STATUS
 *   }
 */

#include <json/json_util.h>

SPANK_PLUGIN(slurm_notifier, 1)

static spank_err_t _get_item_u16(spank_t s, int id, uint16_t *pv)
{
	spank_err_t err = spank_get_item(s, id, pv);
	if (err) {
		*pv = 0;
		slurm_debug2("Spank returned %d accessing item %d", err, id);
	}
	return 0;
}

static spank_err_t _get_item_u32(spank_t s, int id, uint32_t *pv)
{
	spank_err_t err = spank_get_item(s, id, pv);
	if (err) {
		*pv = 0;
		slurm_debug2("Spank returned %d accessing item %d", err, id);
	}
	return 0;
}

static jbuf_t _append_item_u16(spank_t s, jbuf_t jb, const char *name, spank_item_t id, char term)
{
	uint16_t v;
	spank_err_t err = _get_item_u16(s, id, &v);
	if (err) {
		jbuf_free(jb);
		return NULL;
	}
	return jbuf_append_attr(jb, name, "%hd%c", v, term);
}

static jbuf_t _append_item_u32(spank_t s, jbuf_t jb, const char *name, spank_item_t id, char term)
{
	uint32_t v;
	spank_err_t err = _get_item_u32(s, id, &v);
	if (err) {
		jbuf_free(jb);
		return NULL;
	}
	return jbuf_append_attr(jb, name, "%d%c", v, term);
}

struct client {
	char xprt[16];
	char host[64];
	char port[16];
	char auth[16];
	ldms_t ldms;
	pthread_cond_t wait_cond;
	pthread_mutex_t wait_lock;
	int state;
	LIST_ENTRY(client) entry;
	LIST_ENTRY(client) delete;
};

#define IDLE		0
#define CONNECTING	1
#define CONNECTED	2
#define ACKED		3
#define DISCONNECTED	4

static void event_cb(ldms_t x, ldms_xprt_event_t e, void *cb_arg)
{
	struct client *client = cb_arg;
	const char *event;
	if (!client->ldms)
		return;
	pthread_mutex_lock(&client->wait_lock);
	switch (e->type) {
	case LDMS_XPRT_EVENT_CONNECTED:
		client->state = CONNECTED;
		event = "connected";
		break;
	case LDMS_XPRT_EVENT_RECV:
		event = "recv";
		client->state = ACKED;
		break;
	case LDMS_XPRT_EVENT_REJECTED:
		client->state = DISCONNECTED;
		event = "rejected";
		break;
	case LDMS_XPRT_EVENT_DISCONNECTED:
		client->state = DISCONNECTED;
		event = "disconnected";
		break;
	case LDMS_XPRT_EVENT_ERROR:
		client->state = DISCONNECTED;
		event = "error";
		break;
	default:
		slurm_debug2("%s[%d]: Received invalid event type",
			   __func__, __LINE__);
	}
	pthread_mutex_unlock(&client->wait_lock);
	pthread_cond_signal(&client->wait_cond);
	slurm_debug2("%s[%d]: Event %s received for "
		   "client xprt=%s host=%s port=%s auth=%s\n",
		   __func__, __LINE__, event,
		   client->xprt, client->host, client->port, client->auth);
}

static char *get_arg_value(const char *arg)
{
	char *s = strstr(arg, "=");
	if (s) {
		s++;
		return s;
	}
	return NULL;
}

/*
 * The client spec syntax is:
 * xprt:host:port:auth
 *
 * All entries except port are optional. Missing entries have defaults
 * as follows:
 *
 * xprt - 'sock'
 * host - 'localhost'
 * port - '411'
 * auth - 'munge'
 *
 * Therefore:
 *
 * :::: is sock:localhost:10001:munge
 */
LIST_HEAD(client_list, client);
void add_client(struct client_list *cl, const char *spec)
{
	const char *r;
	int i;
	struct client *client;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;
	/* Transport */
	r = spec;
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->xprt); i++)
		client->xprt[i] = *r++;
	if (i == 0)
		strcpy(client->xprt, "sock");
	if (*r == ':')
		r++;
	/* Host */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->host); i++)
		client->host[i] = *r++;
	if (i == 0)
		strcpy(client->host, "localhost");
	if (*r == ':')
		r++;
	/* Port */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->port); i++)
		client->port[i] = *r++;
	if (i == 0)
		strcpy(client->port, "411");
	if (*r == ':')
		r++;
	/* Auth */
	for (i = 0; *r != '\0' && *r != ':' && i < sizeof(client->auth); i++)
		client->auth[i] = *r++;
	if (i == 0)
		strcpy(client->auth, "munge");

	client->state = IDLE;
	pthread_mutex_init(&client->wait_lock, NULL);
	pthread_cond_init(&client->wait_cond, NULL);
	LIST_INSERT_HEAD(cl, client, entry);
	slurm_debug2("%s[%d] client xprt=%s host=%s port=%s auth=%s\n", __func__, __LINE__,
		   client->xprt, client->host, client->port, client->auth);
	return;
 err:
	if (client)
		free(client);
	slurm_debug2("%s - Memory allocation failure.\n", __func__);
}

void setup_clients(int argc, char *argv[], struct client_list *cl)
{
	const char *timeout = NULL;
	int rc;

	for (rc = 0; rc < argc; rc++) {
		if (0 == strncasecmp(argv[rc], "client", 6)) {
			add_client(cl, get_arg_value(argv[rc]));
		}
		if (0 == strncasecmp(argv[rc], "stream", 6)) {
			stream = get_arg_value(argv[rc]);
			continue;
		}
		if (0 == strncasecmp(argv[rc], "timeout", 7)) {
			timeout = get_arg_value(argv[rc]);
			continue;
		}
	}
	if (!stream)
		stream = "slurm";
	if (!timeout)
		io_timeout = SLURM_NOTIFY_TIMEOUT;
	else
		io_timeout = strtoul(timeout, NULL, 0);
	slurm_debug2("%s[%d]: timeout %s io_timeout %ld", __func__, __LINE__, timeout, io_timeout);
}

int purge(struct client_list *client_list, struct client_list *delete_list)
{
	struct client *client;
	while (!LIST_EMPTY(delete_list)) {
		client = LIST_FIRST(delete_list);
		LIST_REMOVE(client, delete);
		LIST_REMOVE(client, entry);
	}
	if (LIST_EMPTY(client_list))
		return ENOTCONN;
	return 0;
}

static int send_event(int argc, char *argv[], jbuf_t jb)
{
	struct client_list client_list;
	struct client_list delete_list;
	struct client *client;
	struct timespec wait_ts;
	int rc = ENOTCONN;

	LIST_INIT(&client_list);
	LIST_INIT(&delete_list);

	setup_clients(argc, argv, &client_list);

	LIST_FOREACH(client, &client_list, entry) {
		client->ldms =
			ldms_xprt_new_with_auth(client->xprt,
						msglog, client->auth, NULL);
		if (!client->ldms) {
			slurm_debug2("%s[%d]: ERROR %d creating the '%s' transport\n",
				     __func__, __LINE__,
				     errno, client->xprt);
			continue;
		}
		client->state = IDLE;
	}
	if (LIST_EMPTY(&client_list))
		return ENOTCONN;

	/* Attempt to connect to each client */
	LIST_FOREACH(client, &client_list, entry) {
		client->state = CONNECTING;
		assert(client->ldms);
		rc = ldms_xprt_connect_by_name(client->ldms, client->host,
					       client->port, event_cb, client);
		if (rc) {
			slurm_debug2("%s[%d]: Synchronous ERROR %d connecting to %s:%s\n",
				     __func__, __LINE__,
				     rc, client->host, client->port);
			LIST_INSERT_HEAD(&delete_list, client, delete);
		}
	}
	rc = purge(&client_list, &delete_list);
	if (rc)
		return rc;
	/*
	 * Wait for the connections to complete and purge clients who
	 * failed to connect
	 */
	LIST_INIT(&delete_list);
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, &client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == CONNECTING)
			pthread_cond_timedwait(&client->wait_cond, &client->wait_lock, &wait_ts);
		if (client->state != CONNECTED) {
			slurm_debug2("%s[%d]: ERROR state=%d connecting to %s:%s\n",
				   __func__, __LINE__,
				   client->state, client->host, client->port);
			LIST_INSERT_HEAD(&delete_list, client, delete);
		}
		pthread_mutex_unlock(&client->wait_lock);
	}
	/*
	 * Purge clients who failed to connect or timed-out
	 */
	rc = purge(&client_list, &delete_list);
	if (rc)
		return rc;

	/*
	 * Publish event to connected clents
	 */
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_INIT(&delete_list);
	LIST_FOREACH(client, &client_list, entry) {
		slurm_debug2("%s:%d publishing to %s:%s\n", __func__, __LINE__,
			   client->host, client->port);
		rc = ldmsd_stream_publish(client->ldms, stream,
					  LDMSD_STREAM_JSON, jb->buf, jb->cursor);
		if (rc) {
			slurm_debug2("%s:%d ERROR %d publishing to %s:%s\n", __func__, __LINE__, rc,
				   client->host, client->port);
			LIST_INSERT_HEAD(&delete_list, client, delete);
			continue;
		}
	}
	rc = purge(&client_list, &delete_list);
	if (rc)
		return rc;
	/*
	 * Wait for the event to be acknowledged by the client before
	 * disconnecting
	*/
	wait_ts.tv_sec = time(NULL) + io_timeout;
	wait_ts.tv_nsec = 0;
	LIST_FOREACH(client, &client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state == CONNECTED)
			pthread_cond_timedwait(&client->wait_cond, &client->wait_lock, &wait_ts);
		if (client->state == ACKED)
			slurm_debug2("%s:%d ACKED %s:%s\n", __func__, __LINE__,
				   client->host, client->port);
		else
			slurm_debug2("%s:%d ACK TIMEOUT state=%d %s:%s\n", __func__, __LINE__,
				   client->state, client->host, client->port);
		pthread_mutex_unlock(&client->wait_lock);
	}
	/*
	 * Disconnect client
	 */
	LIST_FOREACH(client, &client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state != DISCONNECTED) {
			slurm_debug2("%s[%d] CLOSING client xprt=%s host=%s "
				   "port=%s auth=%s\n", __func__, __LINE__,
				   client->xprt, client->host, client->port, client->auth);
			ldms_xprt_close(client->ldms);
		}
		pthread_mutex_unlock(&client->wait_lock);
	}
	/*
	 * Wait for close complete
	 */
	LIST_FOREACH(client, &client_list, entry) {
		pthread_mutex_lock(&client->wait_lock);
		if (client->state != DISCONNECTED) {
			slurm_debug2("%s[%d] CLOSE WAIT for client %s:%s\n",
				   __func__, __LINE__, client->host, client->port);
			pthread_cond_timedwait(&client->wait_cond, &client->wait_lock, &wait_ts);
		}
		pthread_mutex_unlock(&client->wait_lock);
	}
	return 0;
}

jbuf_t make_init_data(spank_t sh, const char *event, const char *context)
{
	char subscriber_data[PATH_MAX];
	char name[80];
	jbuf_t jb;
	spank_err_t err;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	subscriber_data[0] = '\0';
	err = spank_getenv(sh, "SUBSCRIBER_DATA", subscriber_data, sizeof(subscriber_data));
	if (err)
		strcpy(subscriber_data, "{}");
	slurm_debug2("%s:%d SUBSCRIBER_DATA '%s'.\n", __func__, __LINE__, subscriber_data);
	if (json_verify_string(subscriber_data)) {
		slurm_debug2("%s:%d subscriber_data '%s' is not valid JSON and is being ignored.\n",
			   __func__, __LINE__, subscriber_data);
		strcpy(subscriber_data, "{}");
	}
	jb = jbuf_append_attr(jb, "subscriber_data", "\"%s\",", subscriber_data); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;

	name[0] = '\0';
	err = spank_getenv(sh, "SLURM_JOB_NAME", name, sizeof(name));
	if (err)
		name[0] = '\0';
	jb = jbuf_append_attr(jb, "job_name", "\"%s\",", name); if (!jb) goto out_1;

	name[0] = '\0';
	err = spank_getenv(sh, "SLURM_JOB_USER", name, sizeof(name));
	if (err)
		name[0] = '\0';
	jb = jbuf_append_attr(jb, "job_user", "\"%s\",", name); if (!jb) goto out_1;

	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "uid", S_JOB_UID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "gid", S_JOB_GID, ','); if (!jb) goto out_1;
	jb = _append_item_u16(sh, jb, "ncpus", S_JOB_NCPUS, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nnodes", S_JOB_NNODES, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "local_tasks", S_JOB_LOCAL_TASK_COUNT, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "total_tasks", S_JOB_TOTAL_TASK_COUNT, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_exit_data(spank_t sh, const char *event, const char *context)
{
	jbuf_t jb;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_task_init_data(spank_t sh, const char *event, const char *context)
{
	jbuf_t jb;
	pid_t pid = -1;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "schema", "\"slurm_job_data\","); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_id", S_TASK_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_global_id", S_TASK_GLOBAL_ID, ','); if (!jb) goto out_1;
	_get_item_u32(sh, S_TASK_PID, (uint32_t*)&pid);
	if (pid == 0 || pid == -1) {
		pid = getpid();
	}
	jb = jbuf_append_attr(jb, "task_pid", "%d,", pid); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "uid", S_JOB_UID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "gid", S_JOB_GID, ','); if (!jb) goto out_1;
	jb = _append_item_u16(sh, jb, "ncpus", S_JOB_NCPUS, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nnodes", S_JOB_NNODES, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "local_tasks", S_JOB_LOCAL_TASK_COUNT, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "total_tasks", S_JOB_TOTAL_TASK_COUNT, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

jbuf_t make_task_exit_data(spank_t sh, const char *event, const char *context)
{
	jbuf_t jb;
	jb = jbuf_new(); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "{"); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "event", "\"%s\",", event); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "timestamp", "%d,", time(NULL)); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "context", "\"%s\",", context); if (!jb) goto out_1;
	jb = jbuf_append_attr(jb, "data", "{"); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "job_id", S_JOB_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_id", S_TASK_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_global_id", S_TASK_GLOBAL_ID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_pid", S_TASK_PID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "nodeid", S_JOB_NODEID, ','); if (!jb) goto out_1;
	jb = _append_item_u32(sh, jb, "task_exit_status", S_TASK_EXIT_STATUS, ' '); if (!jb) goto out_1;
	jb = jbuf_append_str(jb, "}}");
 out_1:
	return jb;
}

static int nnodes(spank_t sh)
{
	uint32_t nnodes;
	spank_err_t err = spank_get_item(sh, S_JOB_NNODES, &nnodes);
	if (err)
		return 0;
	return nnodes;
}

int slurm_spank_init(spank_t sh, int argc, char *argv[])
{
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_init_data(sh, "init", context_str);
	if (jb) {
		slurm_debug2("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	return ESPANK_SUCCESS;
}

/*
int
slurm_spank_task_init_privileged(spank_t sh, int argc, char *argv[])
*/
int
slurm_spank_task_post_fork(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_task_init_data(sh, "task_init_priv", context_str);
	if (jb) {
		slurm_debug2("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	return ESPANK_SUCCESS;
}

/**
 * local
 *
 *     In local context, the plugin is loaded by srun. (i.e. the
 *     "local" part of a parallel job).
 *
 * remote
 *
 *     In remote context, the plugin is loaded by
 *     slurmstepd. (i.e. the "remote" part of a parallel job).
 *
 * allocator
 *
 *     In allocator context, the plugin is loaded in one of the job
 *     allocation utilities sbatch or salloc.
 *
 * slurmd
 *
 *     In slurmd context, the plugin is loaded in the slurmd daemon
 *     itself. Note: Plugins loaded in slurmd context persist for the
 *     entire time slurmd is running, so if configuration is changed or
 *     plugins are updated, slurmd must be restarted for the changes to
 *     take effect.
 *
 * job_script
 *
 *     In the job_script context, plugins are loaded in the
 *     context of the job prolog or epilog. Note: Plugins are loaded
 *     in job_script context on each run on the job prolog or epilog,
 *     in a separate address space from plugins in slurmd
 *     context. This means there is no state shared between this
 *     context and other contexts, or even between one call to
 *     slurm_spank_job_prolog or slurm_spank_job_epilog and subsequent
 *     calls.
 */
/*
 * Called by SLURM just after job exit.
 */
int
slurm_spank_task_exit(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_task_exit_data(sh, "task_exit", context_str);
	if (jb) {
		slurm_debug2("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	return ESPANK_SUCCESS;
}

int slurm_spank_exit(spank_t sh, int argc, char *argv[])
{
	/* Runs as root */
	spank_context_t context = spank_context();
	const char *context_str;
	jbuf_t jb;

	if (0 == nnodes(sh))
		/* Ignore events before node assignment */
		return ESPANK_SUCCESS;

	switch (context) {
	case S_CTX_REMOTE:
		context_str = "remote";
		break;
	case S_CTX_LOCAL:
	default:
		return ESPANK_SUCCESS;
	}

	jb = make_exit_data(sh, "exit", context_str);
	if (jb) {
		slurm_debug2("%s:%d %s", __func__, __LINE__, jb->buf);
		send_event(argc, argv, jb);
		jbuf_free(jb);
	}
	return ESPANK_SUCCESS;
}
