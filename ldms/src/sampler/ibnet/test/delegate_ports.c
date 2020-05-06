#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include "infiniband/mad.h"
#include "infiniband/umad.h"
#include "opensm/osm_node.h"
#include "complib/cl_nodenamemap.h"
#include "ibnetdisc.h"
#include <pthread.h>

#define MAX_PAIRS 4096 //The maximum number of LID and port pairs
#define NUMBER_OF_SAMPLERS 40 //This value will need to be changed in product to a user defined number

pthread_t gatherer_thread,dispersal_thread;
pthread_rwlock_t rw_lock;

typedef enum {
	false,
	true } bool;

struct port_info {
        int base_lid;
        int portnum;
        int remoteport_baselid;
        int remoteport_portnum;
	bool ext;
};

struct data {
        struct port_info port_data;
        float access_delay;
};

struct data port_pair[MAX_PAIRS];

int numports;
static nn_map_t *node_name_map=NULL;

struct ibnd_config config = {0};
char c[64]="mlx4_0";
char *ca;

int array_index=0;

FILE *mf;
char *fname;
char lbuf[128];
char lexeme[128];
unsigned int curr_metric=1;
unsigned int metrics_to_check=0;

enum {        
    /* These exist only in IB_PC_* */        
    Remote_LID,
    Remote_port,
    symbol_error=0,        
    link_error_recovery,        
    link_downed,        
    port_rcv_errors,        
    port_rcv_remote_physical_errors,        
    port_rcv_switch_relay_errors,        
    port_xmit_discards,        
    port_xmit_constraint_errors,        
    port_rcv_constraint_errors,        
    COUNTER_SELECT2_F,        
    local_link_integrity_errors,        
    excessive_buffer_overrun_errors,        
    VL15_dropped,        
    /* These four mutually exist in both IB_PC_* and IB_PC_EXT_* */        
    port_xmit_data,        
    port_rcv_data,        
    port_xmit_packets,        
    port_rcv_packets,        
    /* this little guy exists only in IB_PC_* */        
    port_xmit_wait,        
    /* these exists only in IB_PC_EXT_* */        
    port_unicast_xmit_packets,        
    port_unicast_rcv_packets,        
    port_multicast_xmit_packets,        
    port_multicast_rcv_packets
};

char* metric_names_values [] = {        
    "Remote_LID",
    "Remote_port",
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
    "port_multicast_rcv_packets"
};

void error(char *msg)
{
        perror(msg);
        exit(1);
}

bool port_extended (int LID, int PORT) {

	ib_portid_t portid;	
	struct ibmad_port *srcport;
	char rcvbuf[64]={0};
	char c[64]="mlx4_0";
	int mgmt_classes[3]={IB_SMI_CLASS,IB_SA_CLASS,IB_PERFORMANCE_CLASS};
	uint16_t cap,is_ext,check_mask=6;
	
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
	portid.lid=LID;

        srcport=mad_rpc_open_port(ca,1,mgmt_classes,3);

	pma_query_via(rcvbuf,&portid,PORT,0,CLASS_PORT_INFO,srcport);
	memcpy(&cap,rcvbuf+2,sizeof(cap));
	is_ext=cap&check_mask;	

	mad_rpc_close_port(srcport);

	if (is_ext) {
		 return true;
	}
	else {
		return false;
	}
}	

void dump_ports_into_array(ibnd_node_t * node, void *user_data)
{
	int i;
	uint8_t *info = NULL;
	ibnd_port_t *port=NULL;
	char *nodename=NULL;
	char *rem_nodename=NULL;


	for (i=node->numports, port=node->ports[i]; i>0; port=node->ports[--i]) {

		port_pair[array_index].port_data.base_lid=0;
		port_pair[array_index].port_data.portnum=0;
		port_pair[array_index].port_data.remoteport_baselid=0;
		port_pair[array_index].port_data.remoteport_portnum=0;


		if (port->node->type = IB_NODE_SWITCH) {
			if (port->node->ports[0])
				info= (uint8_t *)&port->node->ports[0]->info;
		}
		else
			info=(uint8_t *)&port->info;
			nodename= remap_node_name(node_name_map, port->node->guid, port->node->nodedesc);

			//      fprintf(stdout,"array index value is: %4d, ",array_index);
                
			port_pair[array_index].port_data.base_lid=port->base_lid;
			port_pair[array_index].port_data.portnum=port->portnum;
                
			if (port->remoteport) {
				rem_nodename=remap_node_name(node_name_map,port->remoteport->node->guid,port->remoteport->node->nodedesc);
				port_pair[array_index].port_data.remoteport_baselid=port->remoteport->base_lid;
				port_pair[array_index].port_data.remoteport_portnum=port->remoteport->portnum;
               
				free(rem_nodename);
			}				
                
			free(nodename);
			array_index++;
                
	}	
                
	numports=array_index;
	//printf("array_index=%d \n",array_index);
	//parse_ports();
                
	return;
                
}

void read_user_input_file(char *filename) {

	int i,rc;

	printf("The filename is: %s\n",filename);
	mf=fopen(filename,"r");
	if (!mf) {
		printf("Cannot open file <%s>\n",filename);
		return;
	}

	 while (fgets(lbuf,128,mf) !=NULL) {
		rc=sscanf(lbuf,"%s",lexeme);	
		if (strlen(lbuf)==1) {goto skip;}
		if (strcmp(lexeme,"#")==0) {goto skip;}
		else {
	//		for (i=symbol_error;i<=port_multicast_rcv_packets;i++) {
			for (i=Remote_LID;i<=port_multicast_rcv_packets+2;i++) {
				if (strcmp(lexeme,metric_names_values[i])==0) {
					printf("%s\n",lexeme);
					//printf("The current metric is: %d \n",curr_metric);
					metrics_to_check=metrics_to_check | curr_metric;
					goto skip;					
				}
				curr_metric=curr_metric << 1;
			}
		}
	skip: curr_metric=1;	
	}  
	printf ("I have to check metrics %d \n",metrics_to_check);
					
	fclose(mf);

	return;
}

void *fabric_gatherer() {
        
        ibnd_fabric_t *fabric=NULL;
        ca=strdup(c);

        pthread_rwlock_wrlock(&rw_lock);

        fabric = ibnd_discover_fabric(ca,0,NULL,&config);

        ibnd_iter_nodes(fabric,dump_ports_into_array,NULL);

        pthread_rwlock_unlock(&rw_lock);

        return; 
}

void *port_disperser(void *socket_descriptor) {

        char buffer[256];
        uint64_t comp_id;
	int accept_sockfd = *(int *) socket_descriptor;
	int ports_per_sampler,first_sample,last_sample,hca_quan,rc,i;
	socklen_t accept_socksz;

        pthread_rwlock_rdlock(&rw_lock);

        ports_per_sampler=numports/NUMBER_OF_SAMPLERS;
	printf("numports=%d, ports per sampler=%d \n",numports,ports_per_sampler);

	rc = read(accept_sockfd,&comp_id,sizeof(comp_id));
       if ( rc < 0 ) error("Error reading component id value from client");

	if (comp_id > NUMBER_OF_SAMPLERS) {
		comp_id = NUMBER_OF_SAMPLERS;
	}

	if (comp_id < 1) {
		comp_id = 1;
	} 

       printf ("The client component id is: %d \n",comp_id);

	first_sample=ports_per_sampler*(comp_id-1);
	printf ("The fist sample given is: %d \n",first_sample);

	if ( comp_id < NUMBER_OF_SAMPLERS ) {
		last_sample=((comp_id)*ports_per_sampler)-1;
	}
	else {
		last_sample=(comp_id*ports_per_sampler)+(numports % comp_id)-1;
	}

	printf ("The last sample given is %d \n",last_sample);

	rc = write(accept_sockfd, (void *) &metrics_to_check,sizeof(metrics_to_check));

	hca_quan=last_sample-first_sample+1;
	rc = write(accept_sockfd, (void *) &hca_quan,sizeof(hca_quan));

	for (i=first_sample;i<=last_sample;i++) {

		printf ("%d\n",i);
		//port_pair[i].port_data.base_lid=i;
		rc = write(accept_sockfd, (void *) &port_pair[i].port_data.base_lid,sizeof(port_pair[i].port_data.base_lid));
		if (rc<0) error("Error writing to socket");
		//port_pair[i].port_data.portnum=i;
		rc = write(accept_sockfd, (void *) &port_pair[i].port_data.portnum,sizeof(port_pair[i].port_data.portnum));
		if (rc<0) error("Error writing to socket");
		//port_pair[i].port_data.remoteport_baselid=i;
		rc = write(accept_sockfd, (void *) &port_pair[i].port_data.remoteport_baselid,sizeof(port_pair[i].port_data.remoteport_baselid));
		if (rc<0) error("Error writing to socket");
		//port_pair[i].port_data.remoteport_portnum=i;
		rc = write(accept_sockfd, (void *) &port_pair[i].port_data.remoteport_portnum,sizeof(port_pair[i].port_data.remoteport_portnum));
		if (rc<0) error("Error writing to socket");
		//port_pair[i].port_data.ext=i;
		port_pair[i].port_data.ext=port_extended(port_pair[i].port_data.base_lid,port_pair[i].port_data.portnum);
		//port_pair[i].port_data.ext=false;
		rc = write(accept_sockfd, (void *) &port_pair[i].port_data.ext,sizeof(port_pair[i].port_data.ext));
		if (rc<0) error("Error writing to socket");
		//port_pair[i].access_delay=i;
		rc = write(accept_sockfd, (void *) &port_pair[i].access_delay,sizeof(port_pair[i].access_delay));
		if (rc<0) error("Error writing to socket");
	}	
	pthread_rwlock_unlock(&rw_lock);
                        
	return;
}



 

void main(int argc, char *argv[] ) {

	int sockfd,accept_sockfd,rc,i,ports_per_sampler;
	char buffer[256];
	struct sockaddr_in serv_addr,cli_addr;
	socklen_t accept_socksz;
	uint32_t comp_id;

	if (argc==1) {
		//metrics_to_check=4194303;
		metrics_to_check=16777215;
	} else {
		fname=argv[1];
		read_user_input_file(fname);
	}
	
	pthread_create(&gatherer_thread,NULL,fabric_gatherer,NULL);
        pthread_join(gatherer_thread,NULL);


	ports_per_sampler=numports/NUMBER_OF_SAMPLERS;

	sockfd=socket(AF_INET,SOCK_STREAM,0);
	if (sockfd<0) error("Error opening the delivery socket.");

	memset((char *) &serv_addr,'\0',sizeof(serv_addr));
	serv_addr.sin_addr.s_addr=INADDR_ANY;
        serv_addr.sin_port=htons(atoi("9876")); //port 9876
        serv_addr.sin_family=AF_INET;

	if (bind(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr))<0) error ("Error on socket binding.");
        listen(sockfd,5); //5 is max?

	accept_socksz=sizeof(cli_addr);
	while (accept_sockfd = accept(sockfd, (struct sockaddr *) &cli_addr,&accept_socksz)) {

		pthread_create(&dispersal_thread,NULL,port_disperser,(void *) &accept_sockfd);
	
	}	

	
	return;
}
