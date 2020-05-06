#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include "mad.h"
#include "umad.h"
#include "opensm/osm_node.h"
#include "complib/cl_nodenamemap.h"
#include "ibnetdisc.h"
#include <pthread.h>

#define MAX_PAIRS 4096 //The maximum number of LID and port pairs
#define DEFAULT_NUMBER_OF_SAMPLERS 90 //This value will need to be changed in product to a user defined number
#define MAXBUF 2048
#define BUFSZ 80

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

struct data input_port_pair[MAX_PAIRS];
struct data output_port_pair[MAX_PAIRS];

struct requested_port_assignment {
	int component_id; //to be changed to a string later for 
                          //better identification
	int base_lid;
	int portnum;
};

struct requested_port_assignment request_list[MAX_PAIRS];

int numports,request_list_length,samplers;
static nn_map_t *node_name_map=NULL;

struct ibnd_config config = {0};
//char c[64]="mlx4_0";
char c[64];
char *ca;

int array_index=0;
int ports_per_sampler;

FILE *user_metric_file,*config_file;
char *metric_file;
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


int print_request_list(){
  int i;

  for (i = 0; i < request_list_length; i++){
    printf("%d: %d %d.%d\n", i,
	  request_list[i].component_id,
	  request_list[i].base_lid,
	  request_list[i].portnum);
  }
}

void read_port_deleg_config_file(char *config_filename) {

	char lbuf[MAXBUF];
	int lid, port;
	int i;
	int rc; 
	
	char* token;
	char* ptr;

	config_file=fopen(config_filename,"r");
	if (!config_file) {
		printf("Cannot open the configuration file <%s>\n",config_filename);
		return;
	}

	for (i = 0; i < MAX_PAIRS; i++){
	  request_list[i].component_id = -1;
	  request_list[i].base_lid = -1;
	  request_list[i].portnum = -1;
	}

	request_list_length = 0;
	while (fgets(lbuf,MAXBUF,config_file) != NULL) {
	  token = strtok_r(lbuf, " ", &ptr);
	  if (!token){
	    continue;
	  }

	  if (strcmp(token, "samplers") == 0){
	    token = strtok_r(NULL, " ", &ptr);
	    if (!token){
	      printf("Bad format\n");
	      exit(-1);
	    } else {
	      samplers=atoi(token);
	    }
	  } else {
	    int compid = atoi(token);
	    token = strtok_r(NULL, " ", &ptr);
	    while(token){
	      rc=sscanf(token,"%d:%d",&lid,&port);
	      if (rc == 2){
		request_list[request_list_length].component_id=compid;
		request_list[request_list_length].base_lid=lid;
		request_list[request_list_length].portnum=port;
		request_list_length++;
	      } else {
		printf("Bad format in config file\n");
	      }
	      token = strtok_r(NULL, " ", &ptr);
	    }
	  }
	}

	fclose(config_file);

	print_request_list();

	return;

}	

void dump_ports_into_array(ibnd_node_t * node, void *user_data)
{
	int i;
	uint8_t *info = NULL;
	ibnd_port_t *port=NULL;
	char *nodename=NULL;
	char *rem_nodename=NULL;


	for (i=node->numports, port=node->ports[i]; i>0; port=node->ports[--i]) {

		input_port_pair[array_index].port_data.base_lid=0;
		input_port_pair[array_index].port_data.portnum=0;
		input_port_pair[array_index].port_data.remoteport_baselid=0;
		input_port_pair[array_index].port_data.remoteport_portnum=0;


		if (port->node->type == IB_NODE_SWITCH) {
			if (port->node->ports[0])
				info= (uint8_t *)&port->node->ports[0]->info;
		}
		else
			info=(uint8_t *)&port->info;
		nodename= remap_node_name(node_name_map, port->node->guid, port->node->nodedesc);

		//      fprintf(stdout,"array index value is: %4d, ",array_index);
	
		if (port->node->type == IB_NODE_SWITCH) {
               
//			printf("port->node->type is: %d, switch port LID# %d\n",port->node->type,port->base_lid);
 
			input_port_pair[array_index].port_data.base_lid=port->base_lid;
			input_port_pair[array_index].port_data.portnum=port->portnum;
                
			if (port->remoteport) {
				rem_nodename=remap_node_name(node_name_map,port->remoteport->node->guid,port->remoteport->node->nodedesc);
				input_port_pair[array_index].port_data.remoteport_baselid=port->remoteport->base_lid;
				input_port_pair[array_index].port_data.remoteport_portnum=port->remoteport->portnum;
               
				free(rem_nodename);
			}				
                
			free(nodename);
			array_index++;
               	} 
	}	
                
	numports=array_index;
	//printf("array_index=%d \n",array_index);
	//parse_ports();
                
	return;
                
}

void read_user_input_file(char *filename) {

	int i,rc;

	printf("The filename is: %s\n",filename);
	user_metric_file=fopen(filename,"r");
	if (!user_metric_file) {
		printf("Cannot open file <%s>\n",filename);
		return;
	}

	 while (fgets(lbuf,128,user_metric_file) !=NULL) {
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
					
	fclose(user_metric_file);

	return;
}


void set_remotes(){

}

void sort_delegation_responsibilities(int ports_per_sampler) {

	int i,j,input_index,output_index;
	int sampler_id,sampler_pair;

	input_index=0;
	output_index=0;
	
	//load the output samples with the user defined port pairs
	for (sampler_id=1;sampler_id<=samplers;sampler_id++) {
		for (sampler_pair=1;sampler_pair<=ports_per_sampler;sampler_pair++) {
			//printf ("sampler id is : %d,sampler pair %d \n",sampler_id,sampler_pair);
			//loop through all user inputs to find matching identifiers
			for (input_index=0;input_index<request_list_length;input_index++) {
				//printf ("The request list component id is: %d, the sampler id is: %d \n",request_list[input_index].component_id,sampler_id);
				if (request_list[input_index].component_id==sampler_id) {
					output_port_pair[output_index].port_data.base_lid=request_list[input_index].base_lid;
					output_port_pair[output_index].port_data.portnum=request_list[input_index].portnum;
					output_port_pair[output_index].port_data.remoteport_baselid=0;
					output_port_pair[output_index].port_data.remoteport_portnum=0;
					goto jump_ahead;
				}
			} // input request loop
		jump_ahead:		request_list[input_index].component_id=0;  // remove  the first request
					request_list[input_index].base_lid=0;      // from the list
					request_list[input_index].portnum=0;       // of inputs as it has now been added
					output_index++;	
		}		
	} // end load the output samples with user defined port pairs	


	// match port sampler allocations for output and fill in data details for the user defined port samplers
	for (j=0;j<numports;j++) {
		for (i=0;i<numports;i++) {
			if (output_port_pair[j].port_data.base_lid!=0) {
			   if (output_port_pair[j].port_data.base_lid==input_port_pair[i].port_data.base_lid) {
				if (output_port_pair[j].port_data.portnum==input_port_pair[i].port_data.portnum) {
						output_port_pair[j].port_data.remoteport_baselid=input_port_pair[i].port_data.remoteport_baselid;
						output_port_pair[j].port_data.remoteport_portnum=input_port_pair[i].port_data.remoteport_portnum;
						input_port_pair[i].port_data.base_lid=0; //remove the entry from the input array
						input_port_pair[i].port_data.portnum=0;
				}
			    }
			}
		}
	}		

	output_index=0; //preset the output index to 0
	while (output_port_pair[output_index].port_data.base_lid!=0) {
	//find the first open position in the output array that needs a LID and port
		output_index++;
	}

	// finish filling out the LID and Port Output Array with any remaining unallocated
	for (i=0;i<numports;i++) { //parse the input 
		if (input_port_pair[i].port_data.base_lid!=0) {
			output_port_pair[output_index].port_data.base_lid=input_port_pair[i].port_data.base_lid;
			output_port_pair[output_index].port_data.portnum=input_port_pair[i].port_data.portnum;
			output_port_pair[output_index].port_data.remoteport_baselid=input_port_pair[i].port_data.remoteport_baselid;
                        output_port_pair[output_index].port_data.remoteport_portnum=input_port_pair[i].port_data.remoteport_portnum;
			input_port_pair[i].port_data.base_lid=0;
			while ((output_port_pair[output_index].port_data.base_lid!=0)&&(output_index<numports)) {
        		//find the first open position in the output array that needs a LID and port
                		output_index++;
			}
		}	
	}

 	for (i=0;i<numports;i++) {
		printf ("LID: %d, Port: %d, Remote LID: %d, Remote Port: %d\n",output_port_pair[i].port_data.base_lid,output_port_pair[i].port_data.portnum,output_port_pair[i].port_data.remoteport_baselid,output_port_pair[i].port_data.remoteport_portnum);
	}


	return;
}

void *fabric_gatherer() {
        
        ibnd_fabric_t *fabric=NULL;
        ca=strdup(c);

        pthread_rwlock_wrlock(&rw_lock);

        fabric = ibnd_discover_fabric(ca,0,NULL,&config);

        ibnd_iter_nodes(fabric,dump_ports_into_array,NULL);

	ports_per_sampler=numports/samplers;

	//	sort_delegation_responsibilities(ports_per_sampler);

        pthread_rwlock_unlock(&rw_lock);

        return; 
}


void *port_disperser(void *socket_descriptor) {

        char buffer[256];
        uint64_t comp_id;
	int accept_sockfd = *(int *) socket_descriptor;
	int first_sample,last_sample,hca_quan,rc,i;
	socklen_t accept_socksz;

        pthread_rwlock_rdlock(&rw_lock);

        ports_per_sampler=numports/samplers;
	printf("numports=%d, ports per sampler=%d \n",numports,ports_per_sampler);

	memset(buffer, '0', sizeof(buffer));
	rc = read(accept_sockfd, &buffer, sizeof(buffer));
	if ( rc < 0 ) {
	  error("Error reading component id buffer value from client. Exiting");
	  exit(-1);
	}
	buffer[rc] = '\0';
	printf("read <%s> for buffer rc = %d\n", buffer, rc);

	comp_id = atoi(buffer);
	printf("read <%d> for component_id samplers is <%d>\n", comp_id, samplers);

	/*
	if (comp_id > samplers) {
		comp_id = samplers;
	}

	if (comp_id < 1) {
		comp_id = 1;
	} 
	*/

       printf ("The client component id is: %d \n", comp_id);

       /*
	first_sample=ports_per_sampler*(comp_id-1);
	printf ("The first sample given is: %d \n",first_sample);

	if ( comp_id < samplers ) {
		last_sample=((comp_id)*ports_per_sampler)-1;
	}
	else {
		last_sample=(comp_id*ports_per_sampler)+(numports % comp_id)-1;
	}

	printf ("The last sample given is %d \n",last_sample);

	//	rc = write(accept_sockfd, (void *) &metrics_to_check,sizeof(metrics_to_check));

	hca_quan=last_sample-first_sample+1;
	//	rc = write(accept_sockfd, (void *) &hca_quan,sizeof(hca_quan));

	//write out the type of HCAs whether mlx4, mlx5, etc
	//	rc = write(accept_sockfd, (void *) &c,sizeof(c));
	*/


	char sendbuf[MAXBUF];
	int j = 0;
	memset(sendbuf, '0', sizeof(sendbuf));
	sendbuf[0] = '\0';

	for (i = 0; i < request_list_length; i++){
	  if (request_list[i].component_id == comp_id){
	    if ((request_list[i].base_lid == -1) || (request_list[i].portnum == -1)){
	      //skip this one
	      continue;
	    }

	    char buf[BUFSZ];
	    char format[BUFSZ];
	    if (j == 0){
	      snprintf(format, BUFSZ, "%s", "%d.%d:%d.%d");
	    } else {
	      snprintf(format, BUFSZ, "%s", ",%d.%d:%d.%d");
	    }
	    snprintf(buf, BUFSZ, format,
		     request_list[i].base_lid,
		     request_list[i].portnum,
		     0,
		     0);
	    //	    printf("Should be adding %d as %d <%s>\n", i, j, buf);
	    strcat(sendbuf, buf);
	    j++;
	  }
	}

	if (j == 0){
	  printf("WARNING: No matching items for component id <%d>. Sending empty buf\n");
	}

	printf("Sending <%s> to component id <%d>\n", sendbuf, comp_id);
	rc = write(accept_sockfd, (void *) sendbuf, strlen(sendbuf)+1);
	//FIXME: figure out when to free this.
	//	free(sendbuf);

	  /*
		printf ("%d\n",i);
		//input_port_pair[i].port_data.base_lid=i;
		printf("The base lid given is: %d\n",output_port_pair[i].port_data.base_lid);
		rc = write(accept_sockfd, (void *) &output_port_pair[i].port_data.base_lid,sizeof(output_port_pair[i].port_data.base_lid));
		if (rc<0) error("Error writing to socket");
		//input_port_pair[i].port_data.portnum=i;
		rc = write(accept_sockfd, (void *) &output_port_pair[i].port_data.portnum,sizeof(output_port_pair[i].port_data.portnum));
		if (rc<0) error("Error writing to socket");
		//input_port_pair[i].port_data.remoteport_baselid=i;
		rc = write(accept_sockfd, (void *) &output_port_pair[i].port_data.remoteport_baselid,sizeof(output_port_pair[i].port_data.remoteport_baselid));
		if (rc<0) error("Error writing to socket");
		//input_port_pair[i].port_data.remoteport_portnum=i;
		rc = write(accept_sockfd, (void *) &output_port_pair[i].port_data.remoteport_portnum,sizeof(output_port_pair[i].port_data.remoteport_portnum));
		if (rc<0) error("Error writing to socket");
		//input_port_pair[i].port_data.ext=i;
		output_port_pair[i].port_data.ext=port_extended(output_port_pair[i].port_data.base_lid,output_port_pair[i].port_data.portnum);
		//input_port_pair[i].port_data.ext=false;
		rc = write(accept_sockfd, (void *) &output_port_pair[i].port_data.ext,sizeof(output_port_pair[i].port_data.ext));
		if (rc<0) error("Error writing to socket");
		//input_port_pair[i].access_delay=i;
		rc = write(accept_sockfd, (void *) &output_port_pair[i].access_delay,sizeof(output_port_pair[i].access_delay));
		if (rc<0) error("Error writing to socket");
	}	
	  */
	pthread_rwlock_unlock(&rw_lock);
                        
	return;
}



 

void main(int argc, char *argv[] ) {

	int sockfd,accept_sockfd,rc,i,ports_per_sampler;
	char buffer[256];
	struct sockaddr_in serv_addr,cli_addr;
	socklen_t accept_socksz;
	uint32_t comp_id;
	char *config_filename;

	samplers=DEFAULT_NUMBER_OF_SAMPLERS;

	if (argc==1) {
		//metrics_to_check=4194303;
		metrics_to_check=16777215;
		strcpy(c,"mlx4_0");
	} else {
		if (argc==2) {
			metric_file=argv[1];
			read_user_input_file(metric_file);
			strcpy(c,"mlx4_0");
		} else {
			if (argc==3) {
				metric_file=argv[1];
				read_user_input_file(metric_file);
				strcpy(c,argv[2]);
				} else {
					metric_file=argv[1];
                                	read_user_input_file(metric_file);
                                	strcpy(c,argv[2]);	
					config_filename=argv[3];
					read_port_deleg_config_file(config_filename);
				}
			}
	}
	
	pthread_create(&gatherer_thread,NULL,fabric_gatherer,NULL);
        pthread_join(gatherer_thread,NULL);

//	ports_per_sampler=numports/samplers;

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
