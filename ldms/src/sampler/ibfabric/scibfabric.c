/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2012-2016 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2017 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
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

/**
 * \file scibfabric.c
 * \brief Switch Infiniband metric sampler.
 *
 * For the given LIDs and port numbers, this sampler will check the port
 * capability whether they support extended performance metric counters. For the
 * supported port, this sampler will query the counters and do nothing. For the
 * ports that do not support extended metric counters, the sampler will query
 * and then reset the counters to prevent the counters to stay at MAX value.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <wordexp.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <infiniband/mad.h>
#include <infiniband/umad.h>
/**
 * IB Extension capability flag (obtained from
 * /usr/include/infiniband/iba/ib_types.h)
 */
#define IB_PM_EXT_WIDTH_SUPPORTED       2
/**
 * Another IB Extension capability flag (obtained from
 * /usr/include/infiniband/iba/ib_types.h)
 */
#define IB_PM_EXT_WIDTH_NOIETF_SUP      4

#define SCIB_PATH "/sys/class/infiniband/*/ports/*"

#define SCIB_PATH_SCANFMT "/sys/class/infiniband/%[^/]/ports/%d"

/**
 * The first counter that we're intested in IB_PC_*.
 *
 * We ignore IB_PC_PORT_SELECT_F and IB_PC_COUNTER_SELECT_F.
 */
#define SCIB_PC_FIRST IB_PC_ERR_SYM_F

/**
 * The dummy last counter.
 */
#define SCIB_PC_LAST IB_PC_LAST_F

/**
 * The first counter that we're interested in IB_PC_EXT*.
 *
 * We ignore  IB_PC_EXT_PORT_SELECT_F and IB_PC_EXT_COUNTER_SELECT_F.
 */
#define SCIB_PC_EXT_FIRST IB_PC_EXT_XMT_BYTES_F

/**
 * The dummy last counter.
 */
#define SCIB_PC_EXT_LAST IB_PC_EXT_LAST_F

#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

const char *all_metric_names[] = {
	/* These exist only in IB_PC_* */
	"symbol_error",
	"link_error_recovery",
	"link_downed",
	"port_rcv_errors",
	"port_rcv_remote_physical_errors",
	"port_rcv_switch_relay_errors",
	"port_xmit_discards",
	"port_xmit_constraint_errors",
	"port_rcv_constraint_errors",
	"COUNTER_SELECT2_F",
	"local_link_integrity_errors",
	"excessive_buffer_overrun_errors",
	"VL15_dropped",
	/* These four mutually exist in both IB_PC_* and IB_PC_EXT_* */
	"port_xmit_data",
	"port_rcv_data",
	"port_xmit_packets",
	"port_rcv_packets",
	/* this little guy exists only in IB_PC_* */
	"port_xmit_wait",

	/* these exists only in IB_PC_EXT_* */
	"port_unicast_xmit_packets",
	"port_unicast_rcv_packets",
	"port_multicast_xmit_packets",
	"port_multicast_rcv_packets",
};

/**
 * IB_PC_* to scib index map.
 */
const int scib_idx[] = {
	/* ignore these two */
	[IB_PC_PORT_SELECT_F]         =  -1,
	[IB_PC_COUNTER_SELECT_F]      =  -1,

	[IB_PC_ERR_SYM_F]             =  0,
	[IB_PC_LINK_RECOVERS_F]       =  1,
	[IB_PC_LINK_DOWNED_F]         =  2,
	[IB_PC_ERR_RCV_F]             =  3,
	[IB_PC_ERR_PHYSRCV_F]         =  4,
	[IB_PC_ERR_SWITCH_REL_F]      =  5,
	[IB_PC_XMT_DISCARDS_F]        =  6,
	[IB_PC_ERR_XMTCONSTR_F]       =  7,
	[IB_PC_ERR_RCVCONSTR_F]       =  8,
	[IB_PC_COUNTER_SELECT2_F]     =  9,
	[IB_PC_ERR_LOCALINTEG_F]      =  10,
	[IB_PC_ERR_EXCESS_OVR_F]      =  11,
	[IB_PC_VL15_DROPPED_F]        =  12,

	/* these four overlaps with IB_PC_EXT_* */
	[IB_PC_XMT_BYTES_F]           =  13,
	[IB_PC_RCV_BYTES_F]           =  14,
	[IB_PC_XMT_PKTS_F]            =  15,
	[IB_PC_RCV_PKTS_F]            =  16,

	[IB_PC_XMT_WAIT_F]            =  17,

	/* ignore these two */
	[IB_PC_EXT_PORT_SELECT_F]     =  -1,
	[IB_PC_EXT_COUNTER_SELECT_F]  =  -1,

	/* these four overlaps with IB_PC_* */
	[IB_PC_EXT_XMT_BYTES_F]       =  13,
	[IB_PC_EXT_RCV_BYTES_F]       =  14,
	[IB_PC_EXT_XMT_PKTS_F]        =  15,
	[IB_PC_EXT_RCV_PKTS_F]        =  16,

	/* these four exist only in IB_PC_EXT* */
	[IB_PC_EXT_XMT_UPKTS_F]       =  18,
	[IB_PC_EXT_RCV_UPKTS_F]       =  19,
	[IB_PC_EXT_XMT_MPKTS_F]       =  20,
	[IB_PC_EXT_RCV_MPKTS_F]       =  21,
};

/**
 * Infiniband port representation and context.
 */
struct scib_port {
	char *ca; /**< CA name */
	int portno; /**< port number (remote) */
	int lidno; /**< lid no (remote) */
	float mfloat;
	uint64_t comp_id; /**< comp_id */
	ib_portid_t portid; /**< IB port id */

	/**
	 * Source port for MAD send.
	 *
	 * Actually, one source port is enough for one IB network. However, our
	 * current implementation trivially open one srcport per target port
	 * (::portid) to avoid managing the mapping between IB networks and
	 * source ports.
	 */
	struct ibmad_port *srcport;
	int ext; /**< Extended metric indicator */
	LIST_ENTRY(scib_port) entry; /**< List entry */

	/**
	 * Metric handles for raw metric counters of the port.
	 */
	int handle[ARRAY_SIZE(all_metric_names)];
	/**
	 * Metric handles for rate metrics of the port.
	 */
	int rate[ARRAY_SIZE(all_metric_names)];

	/**
	 * Metric handle for the meta lidport;
	 */
	int mfloathandle;
	int qtime_index;
	int ptime_index;
};

LIST_HEAD(scib_port_list, scib_port);
struct scib_port_list scib_port_list = {0};
uint8_t rcvbuf[BUFSIZ] = {0};
uint8_t rcvbufext[BUFSIZ] = {0};

#define SAMP "scibfabric"
#define SERVERBUFSZ 2048
static ldms_set_t set = NULL;
static ldms_schema_t schema = NULL;
static char *default_schema_name = SAMP;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static uint64_t compid;
static char ca_mlx4[] = "mlx4_0";
LJI_GLOBALS;

struct timeval tv[2];
struct timeval *tv_now = &tv[0];
struct timeval *tv_prev = &tv[1];

static int ib_query_time_index = -1;
static int ib_data_process_time_index = -1;

/**
 * \param setname The set name (e.g. nid00001/scibfabric)
 */
static int create_metric_set(const char *instance_name, char *schema_name)
{
	int rc, i;
	char metric_name[128];
	struct scib_port *port;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Double create set: %s\n",
				instance_name);
		return EEXIST;
	}

	schema = ldms_schema_new(schema_name);
	if (!schema)
		return ENOMEM;

	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		ldms_schema_delete(schema);
		schema = NULL;
		return rc;
	}

	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		ldms_schema_delete(schema);
		schema = NULL;
		return rc;
	}
	
	rc = ldms_schema_metric_add(schema, "ib_query_time", LDMS_V_D64);
	if (rc < 0) {
		rc = ENOMEM;
		ldms_schema_delete(schema);
		schema = NULL;
		return rc;
	}
	ib_query_time_index = rc;

	rc = ldms_schema_metric_add(schema, "ib_data_process_time", LDMS_V_D64);
	if (rc < 0) {
		rc = ENOMEM;
		ldms_schema_delete(schema);
		schema = NULL;
		return rc;
	}
	ib_data_process_time_index = rc;



	LIST_FOREACH(port, &scib_port_list, entry) {
		snprintf(metric_name, 128, "%d.%d#remote", 
			 port->lidno,
			 port->portno);
		rc = ldms_schema_meta_add(schema, metric_name, LDMS_V_F32);
		if (rc < 0) {
			rc = ENOMEM;
			ldms_schema_delete(schema);
			schema = NULL;
			return rc;
		}
		port->mfloathandle = rc;

		for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
			/* counters */
			snprintf(metric_name, 128, "%d.%d#%s",
				 port->lidno,
				 port->portno,
				 all_metric_names[i]
				 );
			port->handle[i] = ldms_schema_metric_add(schema, metric_name,
							  LDMS_V_U64);

			/* rates */
			snprintf(metric_name, 128, "%d.%d#%s.rate",
				 port->lidno,
				 port->portno,
				 all_metric_names[i]);
			port->rate[i] = ldms_schema_metric_add(schema, metric_name,
							LDMS_V_F32);
		}

		snprintf(metric_name, 128, "%d.%d#%s",
			 port->lidno,
			 port->portno,
			 "port_query_time");
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_D64);
		if (rc < 0) {
			rc = ENOMEM;
			ldms_schema_delete(schema);
			schema = NULL;
			return rc;
		}
		port->qtime_index = rc;

		snprintf(metric_name, 128, "%d.%d#%s",
			 port->lidno,
			 port->portno,
			 "port_data_process_time");
		rc = ldms_schema_metric_add(schema, metric_name, LDMS_V_D64);
		if (rc < 0) {
			rc = ENOMEM;
			ldms_schema_delete(schema);
			schema = NULL;
			return rc;
		}
		port->ptime_index = rc;

	}
	/* create set and metrics */
	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": ldms_set_new failed, "
				"errno: %d, %s\n", rc, strerror(errno));
		ldms_schema_delete(schema);
		schema = NULL;
		return errno;
	}
	union ldms_value v;
	v.v_u64 = compid;
	ldms_metric_set(set, 0, &v);

	LJI_SAMPLE(set,1);


	LIST_FOREACH(port, &scib_port_list, entry) {
		v.v_f = port->mfloat;
		ldms_metric_set(set, port->mfloathandle, &v);
	}

	return 0;
}

/**
 *
 * UPDATE 3/23: this will no longer be called
 *
 * Populate all ports (in /sys/class/infiniband) and put into the given \c list.
 *
 * Port population only create port handle and fill in basic port information
 * (CA and port number).
 *
 * \return 0 on success.
 * \return Error code on error.
 */
int populate_ports_wild(struct scib_port_list *list)
{
	wordexp_t p;
	struct scib_port *port;
	int rc = wordexp(SCIB_PATH, &p, 0);
	char ca[64];
	int port_no;
	int i;

	return EINVAL;


	if (rc) {
		if (rc == WRDE_NOSPACE)
			return ENOMEM;
		else
			return ENOENT;
	}
	for (i = 0; i < p.we_wordc; i++) {
		port = calloc(1, sizeof(*port));
		if (!port) {
			rc = ENOMEM;
			goto err;
		}
		LIST_INSERT_HEAD(list, port, entry);
		rc = sscanf(p.we_wordv[i], SCIB_PATH_SCANFMT, ca, &port_no);
		if (rc != 2) {
			rc = EINVAL;
			goto err;
		}
		port->ca = strdup(ca);
		port->portno = port_no;
	}
	wordfree(&p);
	return 0;
err:
	while ((port = LIST_FIRST(list))) {
		LIST_REMOVE(port, entry);
		if (port->ca)
			free(port->ca);
		free(port);
	}
	wordfree(&p);
	return rc;
}

/**
 * Populate ports by the given string \c ports.
 *
 * Port population only create port handle and fill in basic port information
 * (CA and port number).
 *
 * \return 0 if success.
 * \return Error code if error.
 */
int populate_ports(struct scib_port_list *list, char *ports)
{

	int rc, port_no, lid_no;
	float mfloat;
	struct scib_port *port;
	char* val;
	char* ptr;

	if (strcmp(ports, "*") == 0){
		msglog(LDMSD_LERROR, SAMP ":wild ports funcationality has been removed\n");
		rc = EINVAL;
		goto err;
		//		return populate_ports_wild(list);
	}

	if (ports){
		val = strtok_r(ports, ",", &ptr);
		while (val) {
			rc = sscanf(val, "%d.%d:%f", &lid_no, &port_no, &mfloat);
			if (rc != 3) {
				rc = EINVAL;  /* invalid format */
				msglog(LDMSD_LERROR, SAMP ": invalid format for ports\n");
				goto err;
			}
			port = calloc(1, sizeof(*port));
			if (!port) {
				rc = ENOMEM;
				goto err;
			}
			LIST_INSERT_HEAD(list, port, entry);
			port->ca = strdup(ca_mlx4);
			if (!port->ca) {
				rc = ENOMEM;
				goto err;
			}
			port->portno = port_no;
			port->lidno = lid_no;
			port->mfloat = mfloat;
			val = strtok_r(NULL, ",", &ptr);
		}
	}

	return 0;

err:
	while ((port = LIST_FIRST(list))) {
		LIST_REMOVE(port, entry);
		if (port->ca)
			free(port->ca);
		free(port);
	}
	return rc;
}

/**
 * Open a given IB \c port (using \c port->ca and \c port->portno) and check its
 * capability.
 *
 * \return 0 if success.
 * \return Error number if error.
 */
int open_port(struct scib_port *port)
{
	int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
	void *p;
	uint16_t cap;

	/* open source port for sending MAD messages */
	port->srcport = mad_rpc_open_port(port->ca, 1,
			mgmt_classes, 3);

	if (!port->srcport) {
		msglog(LDMSD_LERROR, SAMP ": ERROR: Cannot open CA:%s port:%d,"
				" ERRNO: %d\n", port->ca, port->portno,
				errno);
		return errno;
	}

	ib_portid_set(&port->portid, port->lidno, 0, 0);

	/* check port capability */
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			CLASS_PORT_INFO, port->srcport);
	if (!p) {
		msglog(LDMSD_LERROR, SAMP ": pma_query_via ca: %s port: %d"
				"  %d\n", port->ca, port->portno, errno);
		return errno;
	}
	memcpy(&cap, rcvbuf + 2, sizeof(cap));
	port->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!port->ext) {
		msglog(LDMSD_LERROR, SAMP ": WARNING: Extended query not "
				"supported for %s:%d, the sampler will reset "
				"counters every query\n", port->ca, port->portno);
	}

	return 0;
}

/**
 * Close the \c port.
 *
 * This function only close IB port. It does not destroy the port handle. The
 * port handle can be reused in open_port() again.
 */
void close_port(struct scib_port *port)
{
	if (port->srcport)
		mad_rpc_close_port(port->srcport);
	port->srcport = 0;
}

/**
 * Open all port in the \c list at once.
 *
 * \return 0 if no error.
 * \return Error code when encounter an error from a port opening.
 */
int open_ports(struct scib_port_list *list)
{
	struct scib_port *port;
	int rc;

	LIST_FOREACH(port, list, entry) {
		rc = open_port(port);
		if (rc)
			return rc;
	}

	return 0;
}


static int gather_ports_info(char* servername, int serverport, int compid, char** ports){

	struct sockaddr_in serv_addr;
	struct hostent *server;
	int sockfd;
	char buf1[SERVERBUFSZ];
	char buf2[SERVERBUFSZ];
	char *llist;
	int numrcv, rc;


	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0){
		msglog(LDMSD_LERROR, SAMP ": Error opening socket");
		return EINVAL;
	}

	server = gethostbyname((void *)servername);
	if (server == NULL) {
		msglog(LDMSD_LERROR, SAMP ": Error getting server host");
		close(sockfd);
		sockfd = -1;
		return EINVAL;
	}

	// Initialize the server address with 0s
	memset((char *) &serv_addr,'\0',sizeof(serv_addr));

	serv_addr.sin_port=htons(serverport);
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
	      server->h_length);

	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0){
		msglog(LDMSD_LERROR, SAMP ": Error connecting to server");
		close(sockfd);
		sockfd = -1;
		return EINVAL;
	}

	memset(buf1,'\0',sizeof(buf1));
	memset(buf2,'\0',sizeof(buf2));
	snprintf(buf1,SERVERBUFSZ,"%d",compid);
	rc = write(sockfd, &buf1, sizeof(buf1));
	if (rc < 0){
		msglog(LDMSD_LERROR, SAMP ": Error writing to socket");
		close(sockfd);
		sockfd = -1;
		return EINVAL;
	}

	numrcv = read(sockfd, buf2, sizeof(buf2));
	//NOTE: add something about time to wait???
	buf2[numrcv] = '\0';
	llist = strdup(buf2);
	*ports = llist;

	close(sockfd);
	sockfd = -1;
	
	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return
"config name=scibfabric producer=<prod_name> instance=<inst_name> ports=<ports> [component_id=<compid> schema=<sname> with_jobid=<bool>]\n"
"config name=scibfabric producer=<prod_name> instance=<inst_name> servername=<servername> serverport=<port> [component_id=<compid> schema=<sname> with_jobid=<bool>]\n"
"    <prod_name>     The producer name\n"
"    <inst_name>     The instance name\n"
"    <ports>         A comma-separated list of lid.port (e.g. 14.3). OR \n"
"    <servername>    Hostname of the server that will hand us the lid.port list \n"
"    <serverport>    Port of the servername \n"
"    <compid>     Optional unique number identifier. Defaults to zero.\n"
LJI_DESC
"    <sname>      Optional schema name. Defaults to '" SAMP "'\n"
;
}

/**
 * \brief Configuration
 *
 * config name=scibfabric component_id=NUM set=NAME ports=PORTS
 * NUM is a regular decimal
 * NAME is a set name
 * PORTS is a comma-separate list of the form LID1.PORT1,LID2.PORT2,...
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{

	int rc = 0;
	char *setstr;
	char *sname;
	char *ports;
	char *value;

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = (uint64_t)(atoi(value));
	else
		compid = 0;

	LJI_CONFIG(value,avl);

	setstr = av_value(avl, "instance");
	if (!setstr) {
		msglog(LDMSD_LERROR, SAMP ": missing 'instance'\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname) {
		sname = default_schema_name;
	}
	if (strlen(sname) == 0){
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	if (0){
		ports = av_value(avl, "ports");
		if (!ports){
			msglog(LDMSD_LERROR, SAMP ": No longer supporting wild ports\n");
			return EINVAL;
		}
	} else {
		char* servername = NULL;
		char* temps = NULL;
		int serverport;

		servername = av_value(avl, "servername");
		if (!servername){
			msglog(LDMSD_LERROR, SAMP ": server name invalid.\n");
			return EINVAL;
		}

		temps = av_value(avl, "serverport");
		if (!temps){
			msglog(LDMSD_LERROR, SAMP ": server port invalid.\n");
			return EINVAL;
		}
		serverport = atoi(temps);

		rc = gather_ports_info(servername, serverport, compid, &ports);
		//free(servername);
		//free(temps);
		if (rc != 0){
			msglog(LDMSD_LERROR, SAMP ": failed to get ports info. Exiting\n");
			return EINVAL;
		}
	}

	rc = populate_ports(&scib_port_list, ports);
	if (rc) {
		msglog(LDMSD_LINFO, SAMP ": Failed to find ports matching %s.\n",ports);
		return rc;
	}

	rc = open_ports(&scib_port_list);
	if (rc) {
		msglog(LDMSD_LINFO, SAMP ": Failed to open ports.\n");
		return rc;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}
	rc = create_metric_set(setstr, sname);
	if (rc)
		return rc;
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

/**
 * Utility function for updating a single metric in a port.
 */
inline void update_metric(struct scib_port *port, int idx, uint64_t new_v,
			float dt)
{
	(void)dt; //not doing rates.
	if (!port->ext) {
		msglog(LDMSD_LINFO, SAMP ": !port->ext found in update_metric\n");
		uint64_t old_v = ldms_metric_get_u64(set, port->handle[idx]);
		new_v += old_v;
	}
	ldms_metric_set_u64(set, port->handle[idx], new_v);
	// ldms_metric_set_float(set, port->rate[idx], (new_v - old_v) / dt);
}

/**
 * Port query (utility function).
 */
int query_port(struct scib_port *port, float dt, double *dtquery, double *dtprocess)
{
	void *p;
	void *pext;
	int rc;
	uint64_t v;
	int i, j;
	if (!port->srcport) {
		rc = open_port(port);
		if (rc)
			return rc;
	}

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	double qdtx = 0;
	struct timeval qtv_diff, qtv_now, qtv_prev;
	gettimeofday(&qtv_prev, 0);
	p = pma_query_via(rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS, port->srcport);
	if (!p) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Error querying %s.%d, errno: %d\n",
				port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}

	/* for ext: update the shared part and the ext-only part */
	pext = pma_query_via(rcvbufext, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS_EXT, port->srcport);
	if (!pext) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Error extended querying %s.%d, "
				"errno: %d\n", port->ca, port->portno, rc);
		close_port(port);
		return rc;
	}
	gettimeofday(&qtv_now, 0);
	timersub(&qtv_now, &qtv_prev, &qtv_diff);
	qdtx += qtv_diff.tv_sec + qtv_diff.tv_usec / 1e6;
	*dtquery += qdtx;

	struct timeval stv_diff, stv_now, stv_prev;
	double dtx = 0;
	/* 1st part: the data that only exist in the non-ext */
	gettimeofday(&stv_prev, 0);
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(port, j, v, dt);
	}
	v = 0;
	mad_decode_field(rcvbuf, IB_PC_XMT_WAIT_F, &v);
	j = scib_idx[IB_PC_XMT_WAIT_F];
	update_metric(port, j, v, dt);

	/* 2nd part: the shared and the ext part */
	if (!port->ext) {
		msglog(LDMSD_LINFO, SAMP ": !port->ext found where not expected\n");
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(rcvbuf, i, &v);
			j = scib_idx[i];
			update_metric(port, j, v, dt);
		}
		/* and reset the counters */
		msglog(LDMSD_LINFO, SAMP ": !port->ext caused performance_reset_via\n");
		performance_reset_via(rcvbuf, &port->portid, port->portno,
				0xFFFF, 0, IB_GSI_PORT_COUNTERS, port->srcport);
		return 0;
	}

	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(rcvbufext, i, &v);
		j = scib_idx[i];
		update_metric(port, j, v, dt);
	}


	gettimeofday(&stv_now, 0);
	timersub(&stv_now, &stv_prev, &stv_diff);
	dtx += stv_diff.tv_sec + stv_diff.tv_usec / 1.0e6;
	*dtprocess += dtx;

	ldms_metric_set_double(set, port->qtime_index, qdtx);
	ldms_metric_set_double(set, port->ptime_index, dtx);
	return 0;
}

static int sample(struct ldmsd_sampler *self)
{
	struct timeval *tmp;
	struct timeval tv_diff;
	double dt;
	union ldms_value v;
	struct scib_port *port;
	double dtquery = 0;
	double dtprocess = 0;

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	gettimeofday(tv_now, 0);
	timersub(tv_now, tv_prev, &tv_diff);
	dt = tv_diff.tv_sec + tv_diff.tv_usec / 1e6;

	ldms_transaction_begin(set);
	LJI_SAMPLE(set,1);
	// double dtx;
	LIST_FOREACH(port, &scib_port_list, entry) {
		/* query errors are handled in query_port() function */
		query_port(port, dt, &dtquery, &dtprocess);
		// msglog(LDMSD_LDEBUG , SAMP ":%d.%d dtprocess %f\n", port->lidno, port->portno, dtprocess);
	}


	v.v_d = dtquery;
	ldms_metric_set(set, ib_query_time_index, &v);

	v.v_d = dtprocess;
	ldms_metric_set(set, ib_data_process_time_index, &v);

	ldms_transaction_end(set);

	
	tmp = tv_now;
	tv_now = tv_prev;
	tv_prev = tmp;
	return 0;
}

static void term(struct ldmsd_plugin *self){

	struct scib_port *port;
	while ((port = LIST_FIRST(&scib_port_list))) {
		LIST_REMOVE(port, entry);
		close_port(port);
		free(port->ca);
		free(port);
	}
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler scibfabric_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &scibfabric_plugin.base;
}
