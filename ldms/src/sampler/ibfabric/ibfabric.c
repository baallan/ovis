/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2011 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2011 Sandia Corporation. All rights reserved.
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
 * \file ibfabric.c
 * \brief simplest example of a data provider.
 * Also handy for overhead measurements when configured without jobid.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h> // needed for strtoull processing of comp_id
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"
#include "infiniband/mad.h"
#include "infiniband/umad.h"

#define SAMP "ibfabric"
#define MAX_PAIRS 4096 //The maximum number of LID and port pairs
#define NUMBER_OF_SAMPLERS 40 //This value will need to be changed 
			      //in production to a user defined number
#define max_metrics 36

//sampler data vectors used in laying out the metrics 
#define remote_lid_metric 1
#define remote_port_metric 2
#define regular_metrics 3

LJI_GLOBALS;

static char *default_schema_name = SAMP;
static uint64_t compid;
pthread_t gatherer_thread,dispersal_thread;
pthread_rwlock_t rw_lock;

static unsigned int metrics_to_check;

typedef enum {
	fals,
	tru } boolean;

//port delegator information variables
struct port_info {
        int base_lid;
        int portnum;
        int remoteport_baselid;
        int remoteport_portnum;
	boolean ext;
};

struct data {
        struct port_info port_data;
        float access_delay;
};

struct data port_pair[MAX_PAIRS]; //port delegation array

static int numports; //number of ports given by the port delegator`
static int metric_quantity; // number of tokenized metrics

int array_index=0;

//LDMS variable definitions 
static const char metric_name[128];
static uint64_t counter = 0;
static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
static char *producer_name;
static ldms_schema_t schema;

//Infiniband MAD variable definitions 
static char rcvbuf[64]={0};
static void *p;
static ib_portid_t portid;
static struct ibmad_port *srcport;
//static char c[64]="mlx4_0";
static char c[64];
static char *ca;
static int mgmt_classes[3]={IB_SMI_CLASS,IB_SA_CLASS,IB_PERFORMANCE_CLASS};

// brought over from sysclassib.c   //
// definitions of ib parameters     //
// these are displayed in samples   //
// and are set in create metric set //                                                                                    
const char *all_metric_names[] = {                                                  
    /* These exist only in IB_PC_* */                                               
    "Remote_LID",
    "Remote_port",
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

unsigned long long time_in,time_out,elapsed_time;


static int gather_port_info(uint64_t component_id,char *elected) {

	int sockfd,portno,LID,rc;	
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int i; //index variable

	sockfd = socket(AF_INET, SOCK_STREAM, 0); // TCP/IP socket
	if (sockfd < 0) error("Error opening socket");

	server = gethostbyname((void *)elected);
	if (server == NULL) error ("Error hostname doesn't exist");

	// Initialize the server address with 0s
	memset((char *) &serv_addr,'\0',sizeof(serv_addr)); 

	serv_addr.sin_port=htons(atoi("9876")); // port # 9876
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, 
		server->h_length);

	if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
		error("ERROR connecting");

	rc= write(sockfd, &compid,sizeof(compid));
	if (rc < 0) error ("Error writing to socket");

	// The port delevator is providing a list of metrics that 
	// the user wants to check. The metrics are provided as 
	// token bits set on a 32-bit word called metrics_to_check  
	// A set bit (1) is a metric to check, an unset bit (0) means 
	// skip the metric
	
	rc=read(sockfd,(void *) &metrics_to_check, sizeof metrics_to_check);
	if (rc < 0) 
	error ("Error reading the types of metrics to check from the delegator.");
	
	rc=read(sockfd,(void *) &numports, sizeof numports); 
				//THe number of ports that the delegator wants this 
                                // sampler to check
	if (rc < 0) error ("Error reading HCA quantity ");
	
	rc=read(sockfd,(void *) &c, sizeof c); 
				// get the type of HCA from the delegator 
                                // sampler to check
	if (rc < 0) error ("Error reading HCA quantity ");

	// Lets get the data for each HCA that we want to check 
	for (i=1; i<=numports; i++) {
		rc = read(sockfd,(void *) &port_pair[i].port_data.base_lid, 
			sizeof port_pair[i].port_data.base_lid);
		if (rc < 0) goto err;
		rc = read(sockfd,(void *) &port_pair[i].port_data.portnum, 
			sizeof port_pair[i].port_data.portnum);
		if (rc < 0) goto err;
		rc = read(sockfd,(void *) &port_pair[i].port_data.remoteport_baselid, 
			sizeof port_pair[i].port_data.remoteport_baselid);
		if (rc < 0) goto err;
		rc = read(sockfd,(void *) &port_pair[i].port_data.remoteport_portnum, 
			sizeof port_pair[i].port_data.remoteport_portnum);
		if (rc < 0) goto err;
		rc = read(sockfd,(void *) &port_pair[i].port_data.ext, 
			sizeof port_pair[i].port_data.ext);
		if (rc < 0) goto err;
		rc = read(sockfd,(void *) &port_pair[i].access_delay, 
			sizeof port_pair[i].access_delay);
		if (rc < 0) goto err;
	}

	rc=0;
	return rc;
err:
	return rc;
}

static int create_metric_set(const char *instance_name, char* schema_name)
{
	int rc;  // return code
	int i,j; //looping variables
	int lji_entry; //position for LJI SAMPLE
	char metric_name[128];
	unsigned int test,curr_metric;

	schema = ldms_schema_new(schema_name);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}

	//component id is added as a metric
	rc = ldms_schema_meta_add(schema, "component_id", LDMS_V_U64);
	if (rc < 0) {
		rc = ENOMEM;
		goto err;
	}

	// Determine how many ports I am checking 
	// and what the LID and port numbers are
	//gather_port_info(compid,"localhost");
	gather_port_info(compid,"shaun-admin");
	
	// metric names for each LID and port sample //
	// This sample contains each metric that can be gathered from the port//
	for (j=1;j<numports+1;j++) {	
		metric_quantity=0; 
		// Clear the metric quantity so we get a reading of just how many 
                // token bits were set by the metric file parser in the delegator

		curr_metric=1;     
		// right most bit set as 1 on the token checker all other bits are 0
		for (i=0;i<=max_metrics;i++) {
			// We are checking the metric tokens by a 
			// logical and with a metric checker bit
			if ((metrics_to_check & curr_metric)>0) {
				sprintf(metric_name,"LID_%d_port_%d_%s",
					port_pair[j].port_data.base_lid,
					port_pair[j].port_data.portnum,all_metric_names[i]); 
				rc = ldms_schema_metric_add(schema,metric_name, LDMS_V_U64);
				metric_quantity++;
				if (rc < 0) {
					rc = ENOMEM;
					goto err;
				}
			}
			curr_metric=curr_metric<<1; // roll left the metric tester
		}
	}

	rc = LJI_ADD_JOBID(schema);
	if (rc < 0) {
		goto err;
	}

	set = ldms_set_new(instance_name, schema);
	if (!set) {
		rc = errno;
		goto err;
	}

	//This is the framework that matches the metric names
	//add metric values 
	union ldms_value v;
	v.v_u64 = compid;
	// We are adding the component id value to the metric as a 64 bit unsigned integer
	// the middle entry corresponds to the data line number
	ldms_metric_set(set, 0, &v);
	v.v_u64 = 0;

	for (j=1;j<numports+1;j++) {	
		for (i=1;i<metric_quantity+1;i++) {	
			//Now, add the corresponding entries for the infiniband entries 
			// from above.  metric_quantity provides us with the # of tokenized
			// metrics from the delegators parse of the user entry file
			ldms_metric_set(set, i, &v);
		}
	}
	// Now add SLURM JOB information as the last metric
	//lji_entry=numports*24+1;
	//LJI_SAMPLE(set,lji_entry);
	return 0;

 err:
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	return rc;
}

/**
 * check for invalid flags, with particular emphasis on warning 
 * the user about old definitions that could throw off the sampler.
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};
	char* misplaced[]={"policy"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, 
			"meminfo: config argument %s has been deprecated.\n",deprecated[i]);
			return EINVAL;
		}
	}
	for (i = 0; i < (sizeof(misplaced)/sizeof(misplaced[0])); i++){
		value = av_value(avl, misplaced[i]);
		if (value){
			msglog(LDMSD_LERROR, 
			"meminfo: config argument %s is misplaced.\n",misplaced[i]);
			return EINVAL;
		}
	}

	return 0;
}


static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP " producer=<prod_name> instance=<inst_name> [component_id=<compid> schema=<sname> with_jobid=<jid>]\n"
		"    <prod_name>  The producer name\n"
		"    <inst_name>  The instance name\n"
		"    <compid>     Optional unique number identifier. Defaults to zero.\n"
		LJI_DESC
		"    <sname>      Optional schema name. Defaults to '" SAMP "'\n";
}

/**
 * \brief Configuration
 *
 * config name=clock component_id=<comp_id> set=<setname> with_jobid=<bool>
 *     comp_id     The component id value.
 *     setname     The set name.
 *     bool        lookup jobid or report 0.
 */
static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	char *value;
	char *sname;
	void * arg = NULL;
	int rc;

	rc = config_check(kwl, avl, arg);
	if (rc != 0){
		return rc;
	}

	producer_name = av_value(avl, "producer");
	if (!producer_name) {
		msglog(LDMSD_LERROR, SAMP ": missing producer.\n");
		return ENOENT;
	}

	value = av_value(avl, "component_id");
	if (value)
		compid = strtoull(value, NULL, 0);

	LJI_CONFIG(value,avl);

	value = av_value(avl, "instance");
	if (!value) {
		msglog(LDMSD_LERROR, SAMP ": missing instance.\n");
		return ENOENT;
	}

	sname = av_value(avl, "schema");
	if (!sname)
		sname = default_schema_name;
	if (strlen(sname) == 0) {
		msglog(LDMSD_LERROR, SAMP ": schema name invalid.\n");
		return EINVAL;
	}

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = create_metric_set(value, sname);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		return rc;
	}
	ldms_set_producer_name_set(set, producer_name);
	return 0;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int rc;
	int i,j,metric_line;
	int next_ldms_entry;
	int64_t mad_results[50];

	unsigned int curr_metric=1;

	union ldms_value v;

	struct timeval tv;

/**** Moved LDMS transaction begin ***/

	rc = ldms_transaction_begin(set);
	if (rc)
	return rc;

	gettimeofday(&tv, NULL);
	time_in=tv.tv_sec*1000000+tv.tv_usec;

	enum {        
    		/* These exist only in IB_PC_* */        
    		symbol_error=0,        
    		link_error_recovery=1,        
    		link_downed=2,        
    		port_rcv_errors=3,        
    		port_rcv_remote_physical_errors=4,        
    		port_rcv_switch_relay_errors=5,        
    		port_xmit_discards=6,        
   		port_xmit_constraint_errors=7,        
    		port_rcv_constraint_errors=8,        
    		COUNTER_SELECT2_F=9,        
    		local_link_integrity_errors=10,        
    		excessive_buffer_overrun_errors=11,        
    		VL15_dropped=12,        
    		port_xmit_data=13,        
    		port_rcv_data=14,        
    		port_xmit_packets=15,        
    		port_rcv_packets=16,        
    		port_xmit_wait=17,        
    		port_xmit_ext_data=20,        
    		port_rcv_ext_data=21,        
    		port_xmit_ext_packets=22,        
    		port_rcv_ext_packets=23,        
    		port_unicast_xmit_packets=24,        
    		port_unicast_rcv_packets=25,        
    		port_multicast_xmit_packets=26,        
    		port_multicast_rcv_packets=27
	};

	if (!set) {
		msglog(LDMSD_LERROR, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	//initialize the MAD HCA port query

	portid.drpath.cnt=0;
	memset(portid.drpath.p,0,63);
	portid.drpath.drslid=0;
	portid.drpath.drdlid=0;
	portid.grh_present=0;
	memset(portid.gid,0,16);
	portid.qp=1;
	portid.qkey=0;
	portid.sl=0;
	portid.pkey_idx=0;

	ca=strdup(c);

	srcport=mad_rpc_open_port(ca,1,mgmt_classes,3);

	for (j=1;j<numports+1;j++) {

		for (metric_line=0;metric_line<=21;metric_line++) {
			mad_results[metric_line]=0;
		}

		portid.lid=port_pair[j].port_data.base_lid;

	if (!port_pair[j].port_data.ext) {
		p=pma_query_via(rcvbuf,&portid,port_pair[j].port_data.portnum,0,
                                IB_GSI_PORT_COUNTERS,srcport);

/****************************** Load up our acquisition array with new data **********************/
/* From testing, it appears that the array must be initiailized with 0s for mad_results that have*/
/* NULL values.                                                                                  */

		metric_line=0;
		for (i=IB_PC_ERR_SYM_F;i<=IB_PC_XMT_WAIT_F;i++) {
				mad_decode_field(rcvbuf,i,&mad_results[metric_line]);
				metric_line++; //due to ib definition with 123 start for i
		}
	}
/*************************************************************************************************/
/*                                   IB MAD for extra metrics                                    */
/*         Again, libmad mad.h in /usr/include/infiniband  contains the mad decode values        */
/*************************************************************************************************/                 


		if (port_pair[j].port_data.ext) { // are we using extended metrics?
						  // this parameter was passed over from the port 
						  // delegator

			p=pma_query_via(rcvbuf,&portid,port_pair[j].port_data.portnum,0,
                                IB_GSI_PORT_COUNTERS_EXT,srcport);
			metric_line=13; // overwrite the originial bytes and packets values with 
					// extended metric values

			for (i=IB_PC_EXT_XMT_BYTES_F;i<=IB_PC_EXT_RCV_PKTS_F;i++) {
				mad_decode_field(rcvbuf,i,&mad_results[metric_line]);
				metric_line++;
			}

			metric_line=18; //reset the metric lines to the extended metrics
	
			for (i=IB_PC_EXT_XMT_UPKTS_F;i<IB_PC_EXT_LAST_F;i++) {
					mad_decode_field(rcvbuf,i,&mad_results[metric_line]);

					//if (i==IB_PC_EXT_RCV_MPKTS_F) {
						// bug fix because decode results for 
						// IB_PC_EXT_RCV_MPKTS_F are corrupted
						// look into mad_decode with OFA expert
						// to find out why.
					//	mad_results[21]=mad_results[16]-mad_results[19];
					//}

					metric_line++;
			}
			if ((mad_results[18] > mad_results[15]) || (mad_results[20] > mad_results[15])) {
				// If this case happens, we have a firmware read anomaly and we will report back 0 
				// for both values.  This mirrors perfquery's solution.
				mad_results[18]=0;
				mad_results[20]=0;
			}

			if ((mad_results[19] > mad_results[16]) || (mad_results[21] > mad_results[16])) {
				// If this case happens, we have a firmware read anomaly and we will report back 0 
				// for both values.  This mirrors perfquery's solution.
				mad_results[19]=0;
				mad_results[21]=0;
			}
		}

//		rc = ldms_transaction_begin(set);
//		if (rc)
//			return rc;

/****************** If wanted, the remote LID and port are output as metrics *********************/

		curr_metric=1; //reset our User metric checker 
			       // so that we can parse through 
			       // the metric tokens remote LID 
			       // connected to the HCA that we are checking

		if ((metrics_to_check & curr_metric)>0) {
			v.v_u64=port_pair[j].port_data.remoteport_baselid;
			// metric 1 of each HCA output
			ldms_metric_set(set,
				(j-1)*metric_quantity+remote_lid_metric, &v);
		}
		// remote port connected to the HCA that we are checking
		curr_metric=curr_metric<<1; // rotate the checker bit left
		if ((metrics_to_check & curr_metric)>0) {
			v.v_u64=port_pair[j].port_data.remoteport_portnum;
			// metric 2 of each HCA output
			ldms_metric_set(set,
				(j-1)*metric_quantity+remote_port_metric, &v);
		}
		
		curr_metric=curr_metric<<1;  // rotate the checker bit left

/****************** Gather regular HCA metrics for the user *******************/
	
		next_ldms_entry=0;	

		for (i=symbol_error;i<=port_rcv_data;i++) { 
							// we already checked 2 metrics, 
			if ((metrics_to_check & curr_metric)>0) {  
							// checking to see if the rest of 
							// the metrics were  picked by the user
				//mad_decode_field(rcvbuf,i,&v.v_u64);
				v.v_u64=mad_results[i];
				ldms_metric_set(set,
					(j-1)*metric_quantity+next_ldms_entry+regular_metrics, &v);
				//start at metric 3 of each HCA output and lay out the metric values
				next_ldms_entry++;
			} // end of the if statement
			curr_metric=curr_metric<<1; // rotate the token checker bit left
		} //end of the standard metric for-loop
			for (i=port_xmit_packets;i<=port_rcv_packets;i++) { 
				if ((metrics_to_check & curr_metric)>0) {  
					v.v_u64=mad_results[i];
					ldms_metric_set(set,
						(j-1)*metric_quantity+next_ldms_entry+regular_metrics, &v);
					//start at metric 3 of each HCA output and lay out the metric values
					next_ldms_entry++;
				}
				curr_metric=curr_metric<<1; // rotate the token checker bit left
			}

		if ((metrics_to_check & curr_metric)>0) {
			v.v_u64=mad_results[port_xmit_wait];
			ldms_metric_set(set,
				(j-1)*metric_quantity+next_ldms_entry+regular_metrics, &v);
			//start at metric 3 of each HCA output and lay out the metric values
			next_ldms_entry++;
		}
		if (port_pair[j].port_data.ext) {
			curr_metric=curr_metric<<1;
			//for (i=port_unicast_xmit_packets;i<(max_metrics-remote_port_metric);i++) { 
			for (i=18;i<=21;i++) { // the extended metrics start at 18 and run thru 21 in our 
					       // storage array
				if ((metrics_to_check & curr_metric)>0) {  // We are still checking our 
                             						   // lex token passed from the 
                             						   // port delegator
					v.v_u64=mad_results[i];
					ldms_metric_set(set,
						(j-1)*metric_quantity+next_ldms_entry+regular_metrics, &v);
					//start at metric 3 of each HCA output and lay out the metric values
					next_ldms_entry++;
				}
				curr_metric=curr_metric<<1; // rotate the token checker bit left
			}
		}
		if (!port_pair[j].port_data.ext) {
			performance_reset_via(rcvbuf,&portid,port_pair[j].port_data.portnum,0xFFFF,
                                0,IB_GSI_PORT_COUNTERS,srcport);
		}
	} // end of the for loop for the HCA data
	
	mad_rpc_close_port(srcport);

	//LJI_SAMPLE(set,(numports*22)+1);

	gettimeofday(&tv, NULL);	
	time_out=tv.tv_sec*1000000+tv.tv_usec;

	elapsed_time=time_out-time_in;
	v.v_u64=elapsed_time;

	ldms_metric_set(set,metric_quantity*numports+1,&v);

	rc = ldms_transaction_end(set);
	if (rc)
		return rc;
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (schema)
		ldms_schema_delete(schema);
	schema = NULL;
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler ibfabric_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &ibfabric_plugin.base;
}
