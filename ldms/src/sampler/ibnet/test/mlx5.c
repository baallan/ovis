/* compile with gcc -o mad_query new_mad_query.c -O3 -g -I/usr/include/infiniband -libmad -libumad -losmcomp -libnetdisc -pthread ; # on mlx5 systems */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <umad.h>
#include <mad.h>
#include <iba/ib_types.h>
struct ibmad_port *srcport;

int mgmt_classes[3]  =  { IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS };
ib_portid_t portid  =  { 0 };
int mask  =  0xffff;
uint64_t ext_mask  =  0xffffffffffffffffULL;
uint16_t cap_mask;
int ibd_ca_port  =  1;
char *ibd_ca  =  NULL;
static char c[64] = "mlx5_0";
static uint8_t rcv_buf[1024];
void *p;
uint64_t val64;
int port = 1; /*allow the sampler to change this later */
int dec_val;
int espeed;


struct mad_port {
	char *ca_name;       /* Name of the device      */
	int portnum;	 /* Physical port number    */
	uint base_lid;       /* LMC of LID	      */
	uint sm_lid;	 /* SM LID		  */
	uint sm_sl;	  /* SM service level	*/
	uint state;	  /* Logical port state      */
	uint phys_state;     /* Physical port state     */
	uint rate;	   /* Port link bit rate      */
	uint64_t capmask;    /* Port capabilities       */
	uint64_t gid_prefix; /* Gid prefix of this port */
	uint64_t port_guid;  /* GUID of this port       */
};

umad_ca_t hca;
umad_ca_t hcas[UMAD_MAX_DEVICES],hfis[UMAD_MAX_DEVICES];

char nodedesc[IB_SMP_DATA_SIZE];
int istate;
uint8_t info[IB_SMP_DATA_SIZE];

int reset_counters() 
{
	uint8_t pc[1024];
	int mask = 0xffff;
	int timeout =1;

	memset(pc,0,sizeof(pc));
	//if (!performance_reset_via(pc, portid, port, mask, timeout,IB_GSI_PORT_COUNTERS_EXT, srcport))
        //                printf("perf ext resei\n");


	return 0;

}

int find_connected_hcas()
{

	int i,hfi_quant,hca_quant,error;
	char names[UMAD_MAX_DEVICES][UMAD_CA_NAME_LEN];

	if (umad_init()<0)
		printf("can't initialize the UMAD library \n");
	
	if ((hca_quant=umad_get_cas_names(names,UMAD_MAX_DEVICES))<0)
		printf("can't list IB device names\n");

	for (i=0;i<hca_quant;i++) {
		//printf("%s\n",names[i]);
		error=umad_get_ca(names[i],&hcas[i]);
		if (error!=0) {
			break;
		}
		//printf ("The LID number is: %d \n",hcas[i].ports[1]->base_lid);
	}
	
	hfi_quant=0;

	for (i=0;i<hca_quant;i++) {
		if (strncmp(hcas[i].ca_name,"hfi",3) == 0) {
			hfis[hfi_quant]=hcas[i];
			hfi_quant++;
		}	

	}

	for (i=0;i<hfi_quant;i++) {
		printf ("hfi : %s with the number of ports available is: %d \n",hfis[i].ca_name,hfis[i].numports);
	}

	return 0;
}

// the low and high for a contiguous group of counter ids */
struct counter_range {
/* to manage config, add a name and enabled flag to each counter range */
//	bool enabled;
//	const char *subset;
	int lo;
	int hi;
};
	
#define NGROUPS 14
struct counter_range r[] = {
	{ IB_PC_PORT_SELECT_F, IB_PC_XMT_WAIT_F },
	{ IB_PC_ERR_SYM_F,  IB_PC_ERR_RCVCONSTR_F},
	{ IB_PC_ERR_LOCALINTEG_F, IB_PC_VL15_DROPPED_F},
        { IB_PC_XMT_WAIT_F, IB_PC_XMT_WAIT_F},
	{ IB_PC_EXT_XMT_BYTES_F, IB_PC_EXT_RCV_MPKTS_F },
	{ IB_PC_XMT_INACT_DISC_F, IB_PC_XMT_SW_HOL_DISC_F },
	{ IB_PC_RCV_LOCAL_PHY_ERR_F, IB_PC_RCV_VL_MAP_ERR_F },
	{ IB_PC_PORT_VL_XMIT_WAIT1_F, IB_PC_PORT_VL_XMIT_WAIT15_F },
	{ IB_PC_SL_RCV_FECN0_F, IB_PC_SL_RCV_FECN15_F },
	{ IB_PC_SL_RCV_BECN0_F, IB_PC_SL_RCV_BECN15_F },
	{ IB_PC_PORT_VL_XMIT_WAIT0_F, IB_PC_PORT_VL_XMIT_WAIT15_F},
	{ IB_PESC_PORT_SELECT_F, IB_PESC_FEC_UNCORR_BLOCK_CTR_LANE11_F},
	{ IB_PC_VL_XMIT_TIME_CONG0_F, IB_PC_VL_XMIT_TIME_CONG14_F},
	{ IB_PORT_LINK_SPEED_EXT_ACTIVE_F, IB_PORT_LINK_SPEED_EXT_ENABLED_F}
};


int get_group[NGROUPS] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

int main(int argc,char **argv)
{

	portid.drpath.cnt = 0;
	memset(portid.drpath.p, 0, 63);
	portid.drpath.drslid = 0;
	portid.drpath.drdlid = 0;
	portid.grh_present = 0;
	memset(portid.gid, 0, 16);
	portid.qp = 1;
	portid.qkey = 0;
	portid.sl = 0;
	portid.pkey_idx = 0;

	find_connected_hcas();
	if (argc < 3) {
		printf ("There are missing LID and port assignments.  I am defaulting to the local port \n");
		portid.lid=hcas[0].ports[1]->base_lid;
	}

	if (argc == 3) {
	/* portid.lid = 217; */
		portid.lid = atoi(argv[1]);
		port = atoi(argv[2]);
		if (portid.lid <= 0) {
			printf("need positive LID input\n");
			exit(1);
		}
		if (port <= 0) {
			printf("need positive port input\n");
			exit(1);
		}	
	}

	cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport) {
		printf("Failed to open %s port %d", ibd_ca, ibd_ca_port);
		return 1;
	}

	memset(rcv_buf, 0, sizeof(rcv_buf));
	if (!pma_query_via(rcv_buf, &portid, port, 0, IB_GSI_PORT_COUNTERS_EXT, srcport)) {

		printf ("perfextquery error\n");
		return 1;
	}
	
	int g; // group number
	for (g = 0; g < NGROUPS; g++) {
		if (get_group[g]) {
			for (dec_val = r[g].lo; dec_val <= r[g].hi; dec_val++) {
				mad_decode_field(rcv_buf, dec_val, &val64);
				printf("%s: %llu \n", mad_field_name(dec_val), val64);
			}
		}
	}

	istate = mad_get_field(info, 0, IB_PORT_STATE_F);
	if (istate==IB_LINK_DOWN) {
		printf ("LINK IS DOWNED  \n");
	}else {
		printf ("LINK IS UP \n");
		} 

	mad_rpc_close_port(srcport);

	return 0;
}
