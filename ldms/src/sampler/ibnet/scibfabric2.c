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
 * \file scibfabric2.c
 * \brief Switch Infiniband metric sampler.
 *
 * For the given LIDs and port numbers, this sampler will check the port
 * capability whether they support extended performance metric counters. For the
 * supported port, this sampler will query the counters and do nothing. For the
 * ports that do not support extended metric counters, the sampler will query
 * and then reset the counters to prevent the counters to stay at MAX value.
 *
 * TODO:
 * Add ib node name map support to report and configure via human names
 * instead of lid numbers.
 * node name map format is two column:
 * long-hex-label "human label"
 * where we may need to munge human label (replace blank with _, etc).
 * On past sandia systems, the label has been items like "host1 qib0.1"
 *
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdbool.h>
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

/* 6.10+pad */
#define LDPSZ 20
/**
 * Infiniband port representation and context.
 */
struct scib_port {
	/* char *ca; CA name common to a set of ports. */
	int portno; /**< port number (remote) */
	uint16_t lidno; /**< lid no (remote) */
	char ldp[20]; /**< lid.port as a string. Not floating point compatible
			as the maximum format is LLLLL.PPPPPPPPPP and many values
			even small P are not represented in fp bit patterns. */
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
	 * Metric handle for the meta lidport;
	 */
	int ldphandle;
	int qtime_index;
	double qtime; /* mad call time in seconds */
	uint8_t *rcvbuf; 
	uint8_t *rcvbufext;
};

#define SAMP "scibfabric2"
#define SERVERBUFSZ 2048
static char *default_schema_name = SAMP;
static ldmsd_msg_log_f msglog;
// static ldms_set_t set = NULL;
// static ldms_schema_t schema = NULL;


struct scibfabric2_instance {
	/* globals, when we have multiple instances. */
	struct ldmsd_plugin *plugin;

	/* per instance, when we have multiple instances. */
	// char *producer_name; unused
	uint64_t compid;
	char *ca_name;
	int32_t ca_port;
	ldms_schema_t schema;
	ldms_set_t set;
	unsigned porttime; /* gtod can affect performance, so optional */
	int ib_query_time_index;
	int ib_data_process_time_index;
	LIST_HEAD(scib_port_list, scib_port) scib_port_list;

} only_instance;

/* reset instance data to empty values. */
static void init_instance(struct scibfabric2_instance *i, struct ldmsd_plugin *p)
{
	memset(i, 0, sizeof(*i));
	i->plugin = p;
	i->ib_query_time_index = -1;
	i->ib_data_process_time_index = -1;
}

/**
 * \param setname The set name (e.g. nid00001/scibfabric2)
 */
static int create_metric_set(const char *instance_name, char *schema_name, struct scibfabric2_instance *self, uint64_t compid)
{
	int rc, i;
#define MNMAX 128
	char metric_name[MNMAX];
	struct scib_port *port;

	if (self->set) {
		msglog(LDMSD_LERROR, SAMP ": Double create set: %s\n",
				instance_name);
		return EEXIST;
	}

	self->schema = ldms_schema_new(schema_name);
	if (!self->schema)
		return ENOMEM;

	rc = ldms_schema_meta_add(self->schema, LDMSD_COMPID, LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto noschema;
	}

	if (self->porttime) {
		rc = ldms_schema_metric_add(self->schema, "ib_query_time", LDMS_V_D64);
		if (rc < 0) {
			rc = ENOMEM;
			goto noschema;
		}
		self->ib_query_time_index = rc;

		rc = ldms_schema_metric_add(self->schema, "ib_data_process_time", LDMS_V_D64);
		if (rc < 0) {
			rc = ENOMEM;
			goto noschema;
		}
		self->ib_data_process_time_index = rc;
	}
	/*fixme: if we have ibnames we could use %s.%d name/port instead of %d.%d lid/port */

	LIST_FOREACH(port, &(self->scib_port_list), entry) {
		snprintf(metric_name, MNMAX, "%" PRIu16 ".%d#remote", 
			 port->lidno,
			 port->portno);
		rc = ldms_schema_meta_array_add(self->schema, metric_name,
			LDMS_V_CHAR_ARRAY, LDPSZ);
		if (rc < 0) {
			rc = ENOMEM;
			goto noschema;
		}
		port->ldphandle = rc;

		for (i = 0; i < ARRAY_SIZE(all_metric_names); i++) {
			/* counters */
			snprintf(metric_name, MNMAX, "%" PRIu16 ".%d#%s",
				 port->lidno,
				 port->portno,
				 all_metric_names[i]
				 );
			port->handle[i] = ldms_schema_metric_add(self->schema, metric_name,
							  LDMS_V_U64);
		}

		if (self->porttime > 1) {
			snprintf(metric_name, MNMAX, "%d.%d#%s",
				 port->lidno,
				 port->portno,
				 "port_query_time");
			rc = ldms_schema_metric_add(self->schema, metric_name, LDMS_V_D64);
			if (rc < 0) {
				rc = ENOMEM;
				goto noschema;
			}
			port->qtime_index = rc;
		}

	}
	/* create set and metrics */
	self->set = ldms_set_new(instance_name, self->schema);
	if (!self->set) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": ldms_set_new failed, "
				"errno: %d, %s\n", rc, strerror(errno));
		goto noschema;
	}
	union ldms_value v;
	v.v_u64 = compid;
	ldms_metric_set(self->set, 0, &v);

	LIST_FOREACH(port, &(self->scib_port_list), entry) {
		ldms_metric_array_set_str(self->set, port->ldphandle, port->ldp);
	}

	return 0;

noschema:
	ldms_schema_delete(self->schema);
	self->schema = NULL;
	return rc;
#undef MNMAX
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
int populate_ports(struct scibfabric2_instance *self, char *ports)
{
	int rc, port_no;
       	uint16_t lid_no;
	uint16_t rlid; int32_t rport;
	struct scib_port *port;
	char* val;
	char* ptr;

	if (strcmp(ports, "*") == 0){
		msglog(LDMSD_LERROR, SAMP ":wild ports functionality not supported\n");
		rc = EINVAL;
		goto err;
	}

	if (ports) {
		if (!self->ca_name) {
			msglog(LDMSD_LERROR, SAMP ": null ca_name unexpected.\n");
			rc = EINVAL;
			goto err;
		}
		val = strtok_r(ports, ",", &ptr);
		while (val) {
			rc = sscanf(val, "%" SCNu16 ".%d:%" SCNu16 ".%d",
				&lid_no, &port_no, &rlid, &rport);
			if (rc != 4) {
				rc = EINVAL;  /* invalid format */
				msglog(LDMSD_LERROR, SAMP ": invalid format for ports\n");
				goto err;
			}
			port = calloc(1, sizeof(*port));
			if (!port) {
				rc = ENOMEM;
				goto err;
			}
			port->rcvbuf = calloc(1, umad_size() + IB_MAD_SIZE);
			port->rcvbufext = calloc(1, umad_size() + IB_MAD_SIZE);
			LIST_INSERT_HEAD(&(self->scib_port_list), port, entry);
			if (!port->rcvbuf) {
				rc = ENOMEM;
				goto err;
			}
			if (!port->rcvbufext) {
				rc = ENOMEM;
				goto err;
			}
			port->portno = port_no;
			port->lidno = lid_no;
			sprintf(port->ldp,"%" PRIu16 ".%d",lid_no, port_no);
			val = strtok_r(NULL, ",", &ptr);
		}
	}

	return 0;

err:
	while ((port = LIST_FIRST(&(self->scib_port_list)))) {
		LIST_REMOVE(port, entry);
		if (port && port->rcvbuf)
			free(port->rcvbuf);
		if (port && port->rcvbufext)
			free(port->rcvbufext);
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
int open_port(char * portca, struct scib_port *port)
{
	int mgmt_classes[3] = {IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS};
	void *p;
	uint16_t cap;

	/* open source port for sending MAD messages */
	port->srcport = mad_rpc_open_port(portca, 1,
			mgmt_classes, 3);

	if (!port->srcport) {
		msglog(LDMSD_LERROR, SAMP ": ERROR: Cannot open CA:%s port:%d,"
				" ERRNO: %d\n", portca, port->portno,
				errno);
		return errno;
	}

	ib_portid_set(&port->portid, port->lidno, 0, 0);

	/* check port capability */
	p = pma_query_via(port->rcvbuf, &port->portid, port->portno, 0,
			CLASS_PORT_INFO, port->srcport);
	if (!p) {
		msglog(LDMSD_LERROR, SAMP ": pma_query_via ca: %s port: %d"
				"  %d\n", portca, port->portno, errno);
		return errno;
	}
	memcpy(&cap, port->rcvbuf + 2, sizeof(cap));
	port->ext = cap & (IB_PM_EXT_WIDTH_SUPPORTED
			| IB_PM_EXT_WIDTH_NOIETF_SUP);

	if (!port->ext) {
		msglog(LDMSD_LERROR, SAMP ": Extended query not "
			"supported for %s:%d, the sampler will reset "
			"counters every query\n", portca, port->portno);
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
int open_ports(struct scibfabric2_instance *self)
{
	struct scib_port *port;
	int rc;

	LIST_FOREACH(port, &(self->scib_port_list), entry) {
		rc = open_port(self->ca_name, port);
		if (rc)
			return rc;
	}

	return 0;
}

static int read_ports_file(const char *pfname, uint64_t compid,
	const char *producer, char* *ports)
{
	return EINVAL;
}

static int gather_ports_info(const char* servername, int serverport, uint64_t compid, char** ports){

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
	snprintf(buf1,SERVERBUFSZ,"%" PRIu64, compid);
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
"config name=scibfabric2 producer=<prod_name> instance=<inst_prefix> portfile=<ports> [component_id=<compid> schema=<sname>] ca_name=<ca> ca_port=<port> timing=<pt>\n"
"config name=scibfabric2 producer=<prod_name> instance=<inst_prefix> servername=<servername> serverport=<port> [component_id=<compid> schema=<sname>"
#ifdef NODENAMEMAP
"nodenamemap=<ibmap>"
#endif
"]\n"
"    <prod_name>     The producer name\n"
"    <inst_name>     The instance name prefix\n"
"    <ca_name>       The local CA used to access the IB network for data collection.\n"
"    <ca_port>       The local port on ca_name used for data collection.\n"
"    <portfile>      File of lines containing '<prod_name> [lid.port_list]*'\n"
"    <pt>            Record and report port timing (2) aggregate timing (1) or nothing (0).\n"
"    <servername>    Hostname of the server that will hand us the lid.port list \n"
"    <serverport>    Port of the servername \n"
"    <compid>        Optional unique number identifier. Defaults to zero.\n"
"    <sname>         Optional schema name. Defaults to '" SAMP "'\n"
#ifdef NODENAMEMAP
"    <ibmap>         Optional map (as ibnetdiscover --node-name-map=<ibmap>).\n"
#endif
"\n"
"port_list may be a number or a range such as [1,3,5,7-12] or *.\n"
"If nodenamemap is provided, the lid used in portfile may be a quoted (\") string instead of a number.\n"
;
}

/**
 * \brief Configuration
 *
 */
static 
int new_instance(struct ldmsd_plugin *pi, struct scibfabric2_instance *self,  struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = 0;
	char *setstr;
	char *sname;
	char *ports;
	char *value;
	char *pt;
	uint64_t compid;

	if (self->set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	pt = av_value(avl, "timing");
	if (pt) {
		switch(pt[0]) {
		case '1':
			self->porttime = 1;
			break;
		case '2':
			self->porttime = 2;
			break;
		case '0':
			break;
		default:
			msglog(LDMSD_LERROR, SAMP ": timing option %s unsupported.\n",pt);
			return EINVAL;
		}
	}
	char *producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing 'producer'\n");
		return ENOENT;
	}

	value = av_value(avl, LDMSD_COMPID);
	char *endp = NULL;
	if (value) {
		compid = strtoull(value, &endp, 0);
		if (endp == value || errno) {
			msglog(LDMSD_LERROR,"Fail parsing %s '%s'\n",
				LDMSD_COMPID, value);
			return EINVAL;
		}
	} else {
		compid = 0;
	}

	setstr = av_value(avl, "instance");
	if (!setstr) {
		msglog(LDMSD_LERROR, SAMP ": missing 'instance'\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname) {
		sname = default_schema_name;
	}
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	char *cname = av_value(avl, "ca_name");
	char *cport = av_value(avl, "ca_port");
	if (!cname || !cport) {
		msglog(LDMSD_LERROR, SAMP ": ca_name or ca_port not provided.\n");
		return EINVAL;
	}
	char *pfname = av_value(avl, "portfile");
	char *srvname = av_value(avl, "servername");
	char *srvport = av_value(avl, "serverport");
	if (! pfname || !(srvname && srvport)) {
		msglog(LDMSD_LERROR, SAMP 
			": neither portfile nor portlist server provided.\n");
		return EINVAL;
	}
#ifdef NODENAMEMAP
	char *nnmap = av_value(avl, "nodenamemap");
	// then use it to report name instead of or in addition to lid?
	// need to convert lid to guid then map to string using file.
	// string name needs to be short single word, not an essay.
#endif
	ports = av_value(avl, "ports");
	if (ports) {
		msglog(LDMSD_LERROR, SAMP ": 'ports' is not a supported option. use portfile or server options\n");
			return EINVAL;
	}
	
	if (srvname && srvport) {

		int serverport = atoi(srvport);

		rc = gather_ports_info(srvname, serverport, compid, &ports);
		if (rc != 0) {
			msglog(LDMSD_LERROR, SAMP 
				": failed to get ports info from %s:%s\n",
				srvname, srvport);
			return EINVAL;
		}
	} else {
		rc = read_ports_file(pfname, compid, producer_name, &ports);
		if (rc != 0) {
			msglog(LDMSD_LERROR, SAMP 
				": failed to get ports info from %s\n",
				pfname);
			return EINVAL;
		}
	}
	/* at this point we must free ports eventually. */

	rc = populate_ports(self, ports);
	if (rc) {
		msglog(LDMSD_LINFO, SAMP
			": Failed to find ports matching %s.\n",ports);
		return rc;
	}

	rc = open_ports(self);
	if (rc) {
		msglog(LDMSD_LINFO, SAMP ": Failed to open ports.\n");
		return rc;
	}
	if (self->porttime > 1) {
		msglog(LDMSD_LINFO, SAMP ": Logging individual port query times.\n");
	}
	if (self->porttime ) {
		msglog(LDMSD_LINFO, SAMP ": Logging aggregate port query times.\n");
	}

	rc = create_metric_set(setstr, sname, self, compid);
	if (rc)
		return rc;
	ldms_set_producer_name_set(self->set, producer_name);
	return 0;
}

static int config(struct ldmsd_plugin *pi, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	struct scibfabric2_instance *self;
#ifdef MULTISAMPLER
	struct scibfabric2_instance *self = malloc(sizeof(*self));
#else
	self = &only_instance;
#endif
	init_instance(self, pi);
	return new_instance(pi, self, kwl, avl);
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	msglog(LDMSD_LERROR, SAMP ": get_set not supported by this plugin.\n");
	return NULL;
}

/**
 * Utility function for updating a single metric in a port.
 * In the case of no extended counters, caller will reset the counter after
 * reading it and we add the value to our previously stored old value.
 */
inline void update_metric(ldms_set_t set, struct scib_port *port, int idx, uint64_t new_v)
			
{
	if (!port->ext) {
		uint64_t old_v = ldms_metric_get_u64(set, port->handle[idx]);
		new_v += old_v;
	}
	ldms_metric_set_u64(set, port->handle[idx], new_v);
}

/**
 * Port query (utility function).
 */
int query_port(char *portca, struct scib_port *port, unsigned porttime)
{
	void *p;
	void *pext;
	int rc;
	if (!port) return EINVAL;

	struct timeval qtv_diff, qtv_now, qtv_prev;
	if (porttime > 1) {
		port->qtime = 0;
		gettimeofday(&qtv_prev, 0);
	}
	p = pma_query_via(port->rcvbuf, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS, port->srcport);
	if (!p) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Error querying %s.%d, errno: %d\n",
				portca, port->portno, rc);
		close_port(port);
		return rc;
	}

	/* for ext: update the shared part and the ext-only part */
	pext = pma_query_via(port->rcvbufext, &port->portid, port->portno, 0,
			IB_GSI_PORT_COUNTERS_EXT, port->srcport);
	if (!pext) {
		rc = errno;
		msglog(LDMSD_LERROR, SAMP ": Error extended querying %s.%d, "
				"errno: %d\n", portca, port->portno, rc);
		close_port(port);
		return rc;
	}
	if (porttime > 1) {
		gettimeofday(&qtv_now, 0);
		timersub(&qtv_now, &qtv_prev, &qtv_diff);
		port->qtime = qtv_diff.tv_sec + qtv_diff.tv_usec / 1e6;
	}
	return 0;
}

int decode_port(ldms_set_t set, struct scib_port *port, unsigned porttime) {
	/* 1st part: the data that only exist in the non-ext */
	int i,j;
	uint64_t v;
	for (i = SCIB_PC_FIRST; i < IB_PC_XMT_BYTES_F; i++) {
		v = 0;
		mad_decode_field(port->rcvbuf, i, &v);
		j = scib_idx[i];
		update_metric(set, port, j, v);
	}
	v = 0;
	mad_decode_field(port->rcvbuf, IB_PC_XMT_WAIT_F, &v);
	j = scib_idx[IB_PC_XMT_WAIT_F];
	update_metric(set, port, j, v);

	/* 2nd part: the shared and the ext part */
	if (!port->ext) {
		/*
		 * In the case of no extended counters, we reset the counter after
		 * reading it and add the value to our previously stored old value.
		 * This effectively promotes all the metrics to 64 bit wide and rolling,
		 * except that events between read and reset are not counted. This also
		 * destroys the counting plans of any other counter users.
		 * The read/reset pair occurs in the caller.
		 */
		msglog(LDMSD_LINFO, SAMP ": !port->ext found where not expected\n");
		/* non-ext: update only the shared part */
		for (i = IB_PC_XMT_BYTES_F; i < IB_PC_XMT_WAIT_F; i++) {
			mad_decode_field(port->rcvbuf, i, &v);
			j = scib_idx[i];
			update_metric(set, port, j, v);
		}
		/* and reset the counters */
		msglog(LDMSD_LINFO, SAMP ": !port->ext caused performance_reset_via\n");
		performance_reset_via(port->rcvbuf, &port->portid, port->portno,
				0xFFFF, 0, IB_GSI_PORT_COUNTERS, port->srcport);
		return 0;
	}

	for (i = SCIB_PC_EXT_FIRST; i < SCIB_PC_EXT_LAST; i++) {
		v = 0;
		mad_decode_field(port->rcvbufext, i, &v);
		j = scib_idx[i];
		update_metric(set, port, j, v);
	}

	if (porttime > 1) {
		ldms_metric_set_double(set, port->qtime_index, port->qtime);
	}
	return 0;
}

static int sample(struct ldmsd_sampler *obj)
{
	struct scibfabric2_instance *self;
#ifdef MULTISAMPLER
	self = obj;
#else
	self = &only_instance;
#endif
	union ldms_value v;
	struct scib_port *port;
	double dtquery = 0;
	double dtprocess = 0;
	struct timeval tv[3];
	struct timeval tv_diff;


	if (!self->set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	ldms_transaction_begin(self->set);
	// time overall. Need to test case where node is down, too.
	// test case where switch is down?
	if (self->porttime) {
		gettimeofday(&tv[0], 0);
	}
	LIST_FOREACH(port, &(self->scib_port_list), entry) {
		if (!port->srcport) {
			int rc;
			rc = open_port(self->ca_name, port);
			if (rc) {
				continue;
			}
		}
		/* query errors skip updating data silently */
		query_port(self->ca_name, port, self->porttime);
	}
	if (self->porttime) {
		gettimeofday(&tv[1], 0);
	}
	LIST_FOREACH(port, &(self->scib_port_list), entry) {
		decode_port(self->set, port, self->porttime);
	}
	if (self->porttime) {
		gettimeofday(&tv[2], 0);

		timersub(&tv[1], &tv[0], &tv_diff);
		dtquery = tv_diff.tv_sec + tv_diff.tv_usec *1e-6;

		timersub(&tv[2], &tv[1], &tv_diff);
		dtprocess = tv_diff.tv_sec + tv_diff.tv_usec *1e-6;

		v.v_d = dtquery;
		ldms_metric_set(self->set, self->ib_query_time_index, &v);

		v.v_d = dtprocess;
		ldms_metric_set(self->set, self->ib_data_process_time_index, &v);
	}

	ldms_transaction_end(self->set);

	return 0;
}

/* clear data in self and free if free_inst >0 */
static void term_mi(struct scibfabric2_instance *self, bool free_inst) {

	struct scib_port *port;
	while ((port = LIST_FIRST(&(self->scib_port_list)))) {
		LIST_REMOVE(port, entry);
		close_port(port);
		if (port->rcvbuf)
			free(port->rcvbuf);
		if (port->rcvbufext)
			free(port->rcvbufext);
		free(port);
	}
	if (self->schema)
		ldms_schema_delete(self->schema);
	self->schema = NULL;
	if (self->set)
		ldms_set_delete(self->set);
	self->set = NULL;
	// fixme: what else to free from self?
	if (free_inst) {
		// fixme: what else to free?
	}
}

static void term(struct ldmsd_plugin *self){
	struct scibfabric2_instance *inst;
#ifdef MULTISAMPLER
	inst =  NULL;
	/* there will be an instance pointer supplied to term
	 * or we will iterate a list of instances, depending on api.
	 */
#else
	inst = &only_instance;
#endif
	term_mi(inst, 0);
}

static struct ldmsd_sampler scibfabric2_plugin = {
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
	return &scibfabric2_plugin.base;
}
