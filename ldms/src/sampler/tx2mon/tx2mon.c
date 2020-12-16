/*
 * tx2mon.c -	LDMS sampler for basic Marvell TX2 chip telemetry.
 *
 *		Sampler to provide LDMS data available via the tx2mon
 *		CLI utility (https://github.com/jchandra-cavm/tx2mon).
 *		This data exists in structure that is mapped all
 *		the way into sysfs from the memory of the M3 management
 *		processor present on each TX2 die.
 *		This sampler requires the tx2mon kernel module to be loaded.
 *		This module creates sysfs entries that can be opened and
 *		mmapped, then overlaid with a matching structure definition.
 *		Management processor updates the underlying structure at >= 10Hz.
 *		The structure contains a great deal of useful telemetry, including:
 *		 - clock speeds
 *		 - per-core temperatures
 *		 - power data
 *		 - throttling statistics
 */

/*
 *  Copyright [2020] Hewlett Packard Enterprise Development LP
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License as published 
 * by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for 
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with 
 * this program; if not, write to:
 * 
 *   Free Software Foundation, Inc.
 *   51 Franklin Street, Fifth Floor
 *   Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include "assert.h"
#include <unistd.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
#include "tx2mon.h"
#include <pthread.h>
#include "stdbool.h"
#include <stdint.h>
#include <termios.h>
#include <term.h>
#include <string.h>

#define SAMP "tx2mon"

static ldmsd_msg_log_f msglog;
static ldms_set_t set[MAX_CPUS_PER_SOC];
static ldms_set_t sc;

static base_data_t base;
static ldms_schema_t schema;
static ldmsd_msg_log_f msglog;
static struct tx2mon_sampler tx2mon_s = {0};

static struct tx2mon_sampler *tx2mon = &tx2mon_s;

#define MCP_STR_WRAP(NAME) #NAME
#define MCP_LISTWRAP(NAME) MCP_ ## NAME

#define MC_OPER_REGION 0x1

static int pidopts = 0;
static int pidarray = 0;
static char *pids = "self";

/* 
 * - Define metric list found in /usr/include/tx2mon/mc_oper_region.h. 
 * - Create list with the following arguments: name, metric name, type, position in the metric list.
 * - Add schema meta data with ldms_schema_meta_array_add() and ldms_schema_add().
 * - Add schema metric data with ldms_schema_metric_array_add() and ldms_schema_add().
 * - Set the total length of the arrays to the number of cores found in /sys/bus/platform/devices/tx2mon/socinfo.
 * - If "noarray = true/1/t", set the length of the arrays to the number of values that will be included. 
 *	For this sampler, only the maximum and minimum values will be included so the length will be set to 2.
 * */
#define MCP_LIST(WRAP) \
	WRAP("cmd_status", cmd_status, LDMS_V_U32, pos_cmd_status) \
	WRAP("counter", counter, LDMS_V_U32, pos_counter) \
	WRAP("temp_abs_max", temp_abs_max, LDMS_V_F32, pos_temp_abs_max) \
	WRAP("temp_soft_thresh", temp_soft_thresh, LDMS_V_F32, pos_temp_soft_thresh) \
	WRAP("temp_hard_thresh", temp_hard_thresh, LDMS_V_U32, pos_temp_hard_thresh) \
	WRAP("freq_cpu", freq_cpu[0], LDMS_V_U32_ARRAY, pos_freq_cpu) \
	WRAP("tmon_cpu", tmon_cpu[0], LDMS_V_F32_ARRAY, pos_tmon_cpu) \
	WRAP("tmon_soc_avg", tmon_soc_avg, LDMS_V_F32, pos_tmon_soc_avg) \
	WRAP("freq_mem_net", freq_mem_net, LDMS_V_U32, pos_freq_mem_net) \
	WRAP("freq_socs", freq_socs, LDMS_V_U32, pos_freq_socs) \
	WRAP("freq_socn", freq_socn, LDMS_V_U32, pos_freq_socn) \
	WRAP("freq_max", freq_max, LDMS_V_U32, pos_freq_max) \
	WRAP("freq_min", freq_min, LDMS_V_U32, pos_freq_min) \
	WRAP("pwr_core", pwr_core, LDMS_V_F32, pos_pwr_core) \
	WRAP("pwr_sram", pwr_sram, LDMS_V_F32, pos_pwr_sram) \
	WRAP("pwr_mem", pwr_mem, LDMS_V_F32, pos_pwr_mem) \
	WRAP("pwr_soc", pwr_soc, LDMS_V_F32, pos_pwr_soc) \
	WRAP("v_core", v_core, LDMS_V_F32, pos_v_core) \
	WRAP("v_sram", v_sram, LDMS_V_F32, pos_v_sram) \
	WRAP("v_mem", v_mem, LDMS_V_F32, pos_v_mem ) \
	WRAP("v_soc", v_soc, LDMS_V_F32, pos_v_soc) \
	WRAP("active_evt", active_evt, LDMS_V_U32, pos_active_evt) \
	WRAP("temp_evt_cnt", temp_evt_cnt, LDMS_V_U32, pos_temp_evt_cnt) \
	WRAP("pwr_evt_cnt", pwr_evt_cnt, LDMS_V_U32, pos_pwr_evt_cnt) \
	WRAP("ext_evt_cnt", ext_evt_cnt, LDMS_V_U32, pos_ext_evt_cnt) \
	WRAP("temp_throttle_ms", temp_throttle_ms, LDMS_V_U32, pos_temp_throttle_ms) \
	WRAP("pwr_throttle_ms", pwr_throttle_ms, LDMS_V_U32, pos_pwr_throttle_ms) \
	WRAP("ext_throttle_ms", ext_throttle_ms , LDMS_V_U32, pos_ext_throttle_ms) 
	

#define DECLPOS(n, m, t, p) static int p = -1;

MCP_LIST(DECLPOS);
#define META(n, m, t, p) \
	switch (t) {\
	case LDMS_V_U32_ARRAY: \
		if (!pidarray)\
			rc = ldms_schema_meta_array_add(schema, n, t, tx2mon->n_core);\
		else \
			rc = ldms_schema_meta_array_add(schema, n, t, 2);\
		break;\
	case LDMS_V_F32_ARRAY: \
		if (!pidarray)\
			rc = ldms_schema_meta_array_add(schema, n, t, tx2mon->n_core);\
		else \
			rc = ldms_schema_meta_array_add(schema, n, t, 2);\
		break;\
	default:\
		rc = ldms_schema_meta_add(schema, n, t); \
		break;\
	}\
	if (rc < 0) { \
		rc = ENOMEM; \
		return rc; \
	} \
	p = rc;\

#define METRIC(n, m, t, p) \
	switch (t) {\
	case LDMS_V_U32_ARRAY: \
		if (!pidarray)\
			rc = ldms_schema_metric_array_add(schema, n, t, tx2mon->n_core);\
		else\
			rc = ldms_schema_metric_array_add(schema, n, t, 2);\
		break;\
	case LDMS_V_F32_ARRAY: \
		if (!pidarray)\
		rc = ldms_schema_metric_array_add(schema, n, t, tx2mon->n_core);\
		else\
			rc = ldms_schema_metric_array_add(schema, n, t, 2);\
		break;\
	default:\
		rc = ldms_schema_metric_add(schema, n, t); \
		break;\
	}\
	if (rc < 0) { \
		rc = ENOMEM; \
		return rc; \
	} \
	p = rc;\


/*
 * Plug-in data structure and access method.
 */
static struct ldmsd_sampler tx2mon_plugin = {
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
	int i;
	for (i = 0; i < MAX_CPUS_PER_SOC; i++)
		set[i] = NULL;
	return &tx2mon_plugin.base;
}

static const char *tx2mon_opts[] = {
	"metric-conf",
	"soc-number",
	"source-list",
	"node-name-map",
	"debug",
	"schema",
	"instance",
	"producer",
	"component_id",
	"uid",
	"gid",
	"perm",
	NULL
};

static bool get_bool(const char *val, char *name)
{
	if (!val)
		return false;

	switch (val[0]) {
	case '1':
	case 't':
	case 'T':
	case 'y':
	case 'Y':
		return true;
	case '0':
	case 'f':
	case 'F':
	case 'n':
	case 'N':
		return false;
	default:
		msglog(LDMSD_LERROR, "%s: bad bool value %s for %s\n",
			val, name);
		return false;
	}
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc = -1;
	char *noarray;
	int i;
	for (i = 0; i < tx2mon->n_cpu; i++){
	if (set[i]) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
		}
	}
	
	msglog(LDMSD_LDEBUG, SAMP ": config started. \n");

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		goto err;
	}
	
	noarray = av_value(avl, "noarray");
	pidopts = MC_OPER_REGION;

	if (noarray && get_bool(noarray, "noarray"))
		pidarray = 1;

	if (!pidopts) {
		msglog(LDMSD_LERROR, SAMP ": configured with nothing to do.\n");
		goto err;
	}
	
	rc = create_metric_set(base);
	
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create metric set.\n");
		goto err;
	}
	

	msglog(LDMSD_LDEBUG, SAMP ": config done. \n");
	
	return 0;

err:
	rc = EINVAL;
	base_del(base);
	msglog(LDMSD_LDEBUG, SAMP ": config fail.\n");
	return rc;

}

static const char *usage(struct ldmsd_plugin *self)
{
	return 
	"config name=" SAMP " [port-number=<num>]\n"
	"	[producer=<name>] [instance=<name>] [component_id=<uint64_t>] [schema=<name_base>] [noarray=<bool>] \n"
	"	[uid=<user-id>] [gid=<group-id>] [perm=<mode_t permission bits>]\n"
	"    producer	  A unique name for the host providing the timing data (default $HOSTNAME)\n"
	"    instance	  A unique name for the timing metric set (default $HOSTNAME/" SAMP ")\n"
	"    component_id A unique number for the component being monitoring, Defaults to zero.\n"
	"    schema	  The base name of the port metrics schema, Defaults to " SAMP ".\n"
	"    noarray	  Includs only the minimum and maximum values of each array in the metric set. \n" 
	"		  If false, all array values are included. Default is FALSE.\n"
	"    uid	  The user-id of the set's owner\n"
	"    gid	  The group id of the set's owner\n"
	"    perm	  The set's access permissions\n"
	;
}

static void term(struct ldmsd_plugin *self)
{
	int i;
	if (base)
		base_del(base);
	for (i = 0; i < tx2mon->n_cpu; i++){
	if (set[i])
		ldms_set_delete(set[i]);
	set[i] = NULL;
	}
}

static int sample(struct ldmsd_sampler *self)
{
	int rc = 0;
	int i;
	int mcprc = -1;
	for (i = 0; i < tx2mon->n_cpu; i++)
		if (!set[i]) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}
	if (pidopts & MC_OPER_REGION) 
		mcprc = parse_mc_oper_region();
		
	for (i = 0; i < tx2mon->n_cpu; i ++)
	{
		base_sample_begin(base);
		if (!mcprc) {
			rc = tx2mon_set_metrics(i);
		if (rc) {
			msglog(LDMSD_LERROR, SAMP ": failed to create metric set.\n");
			rc = EINVAL;
			return rc;
			}

		}
	
		base_sample_end(base);
	}
	return rc;
}

/* 
 * - Define all metrics in the structure list "MCP_LIST". This definition will be used to sample the metric set
 * - Call tx2mon_array_conv. 
 * - tx2mon_array_conv converts each metric and array to the necessary types (float, uint16 and uint32) and sets 
 *	the metrics with ldms_metric_set_<type>() and ldms_metric_array_set_<type>(). 
 * - If "noarray = true/1/t/T", the two arrays in "MCP_LIST" (freq_cpu and tmon_cpu) will contain only the maximum and minimum
 * - values found in the array after they have been converted with my_to_c_u16() and my_to_c_u32().
 * - Fail with rc = EINVAL if a metric type does not exist
 * */

static int  tx2mon_set_metrics (int i)
{

	struct mc_oper_region *s;
	int rc = 0;
	s = &tx2mon->cpu[i].mcp;
	
#define MCSAMPLE(n, m, t, p) \
	rc = tx2mon_array_conv(&s->m, p, tx2mon->n_core, i, t);\
	if (rc){ \
		rc = EINVAL; \
		msglog(LDMSD_LERROR, SAMP ": sample " n " not correctly defined.\n"); \
		}
	
	MCP_LIST(MCSAMPLE);
	
	return rc;

}

static float my_to_c_u32(uint32_t t)
{
	return to_c(t);

}
static float my_to_c_u16(uint16_t t)
{
	return to_c(t);
}

static int tx2mon_array_conv(void *s, int p, int idx, int i, uint32_t t)
{
	int rc = 0;
	int c = 0;
	if (t == LDMS_V_F32_ARRAY){ 
		uint16_t *s16 = (uint16_t*)s;
		if (pidarray){
			uint16_t *min_max16 = (uint16_t*)s;
			min_max16[0] = s16[0];
			min_max16[1] = s16[0];
			for (c = 0; c < idx; c++){
				if (my_to_c_u16(s16[c]) < my_to_c_u16(min_max16[0]))
					min_max16[0] = s16[c];
				if (my_to_c_u16(s16[c]) > my_to_c_u16(min_max16[1]))
					min_max16[1] =s16[c];
				}
			for (c = 0; c < 2; c++)
				ldms_metric_array_set_float(set[i], p, c, my_to_c_u16(min_max16[c]));
			}
		
		else if (!pidarray) {
			for (c = 0; c < idx; c++)
				ldms_metric_array_set_float(set[i], p, c, my_to_c_u16(s16[c]));
		}
	}
	
	if (t == LDMS_V_U32_ARRAY){
		uint32_t *s32 = (uint32_t*)s;
		if (pidarray) {
			uint32_t *min_max32 = (uint32_t*)s;
			min_max32[0] = s32[0];
			min_max32[1] = s32[0];
			for (c = 0; c < idx; c++){
				if (s32[c] < min_max32[0])
					min_max32[0] = s32[c];
				if (s32[c] > min_max32[1])
					min_max32[1] =s32[c];
				}
			for (c = 0; c < 2; c++)
				ldms_metric_array_set_u32(set[i], p, c, min_max32[c]);
			}

		else if (!pidarray){
			for (c = 0; c < idx; c++)
				ldms_metric_array_set_u32(set[i], p, c, s32[c]);
		}
	}
	
	if(t == LDMS_V_F32){
		uint32_t *f32 = (uint32_t*)s;
		if (p >= 16)
			ldms_metric_set_float(set[i], p, (*f32/1000.0));
		else
			ldms_metric_set_float(set[i], p, my_to_c_u32(*f32));
		
		}

	if (t == LDMS_V_U32)
		ldms_metric_set_u32(set[i], p, *(uint32_t*)s);
	
		
	return rc;
}

/*
 *     get_set() - Obsolete call, no longer used.
 *		       Return safe value, just in case.
 */

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
		return sc;
}

/*
 * Create the schema and metric set.
 * 
 *  - Read & parse TX2MON_SOCINFO_PATH to learn about the system config, and
 *    test that the kernel module is loaded etc. This file is plain ascii.
 *  - Open & mmap TX2MON_NODE?_PATH files. These are binary data structures
 *    defined in mc_oper_region.h (that we were magically pointed to during
 *    configure.)
 *  - Establish the capabilities reported, based on the version number of
 *    the data structure.
 *  - Set up metrics, update data and sample.
 *
 *    Return 0 iff all good, else report useful error message, clean up,
 *    and return appropriate errno.
 */
static int create_metric_set(base_data_t base)
{
	int rc, ret, i;
	int mcprc = -1;
	size_t instance_len = strlen(base->instance_name) + 12;

	char buf[instance_len];
	char cpu_instance_index[12];

	if (pidopts & MC_OPER_REGION) {
		ret = parse_socinfo();
		if (ret < 0){
			msglog(LDMSD_LERROR, SAMP ": Check that you loaded tx2mon module. \n");
			exit (1);
		}
		mcprc = parse_mc_oper_region();
		if (mcprc != 0) {
			msglog(LDMSD_LERROR, SAMP ": unable to read the node file for the sample (%s)\n",
				pids, strerror(mcprc));
			return mcprc;
		}
	
	schema = base_schema_new(base);
	if (!schema) {
		rc = ENOMEM;
		goto err;
	}
	if (pidopts & MC_OPER_REGION) {
		MCP_LIST(METRIC);
	}
	for (i = 0; i < 2; i++){
		snprintf(cpu_instance_index, instance_len, ".%d", i);
		
		strncpy(buf, base->instance_name, instance_len);
		strncat(buf, cpu_instance_index, 12); 
		
		set[i] = ldms_set_new(buf, schema);
		
		if (!set[i]) {
			rc = errno;
			msglog(LDMSD_LERROR, SAMP ": ldms_set_new failed %d for %s\n",
				errno, base->instance_name);
			goto err;
		}
		
		ldms_set_producer_name_set(set[i], base->producer_name);
		ldms_metric_set_u64(set[i], BASE_COMPONENT_ID, base->component_id);
		ldms_metric_set_u64(set[i], BASE_JOB_ID, 0);
		ldms_metric_set_u64(set[i], BASE_APP_ID, 0);
		base_auth_set(&base->auth, set[i]);
		
		rc = ldms_set_publish(set[i]);
		if (rc) {
			ldms_set_delete(base->set);
			base->set = NULL;
			errno = rc;
			base->log(LDMSD_LERROR,"base_set_new: ldms_set_publish failed for %s\n",
				base->instance_name);
			return EINVAL;
		}
		ldmsd_set_register(set[i], base->pi_name);
		
		base->set = set[i];
		base_sample_begin(base);

		if (pidopts & MC_OPER_REGION) {
			rc = tx2mon_set_metrics(i);
			if (rc) {
				msglog(LDMSD_LERROR, SAMP ": failed to create metric set.\n");
				rc = EINVAL;
				return rc;
			}
		}
		base_sample_end(base);
		base->set = NULL;
	}
	
}

	return 0;
err:
	if(schema)
		ldms_schema_delete(schema);
	return rc;
}

static int parse_socinfo(void){
	FILE *socinfo;
	char *path;

	path = realpath(TX2MON_SOCINFO_PATH, NULL);
	if (path == NULL) {
		msglog(LDMSD_LERROR, SAMP ": cannot resolve path for '%s'.\n",
				TX2MON_SOCINFO_PATH);
		return EBADF;
	}

	socinfo = fopen(path, "r");
	if (!socinfo) {
		msglog(LDMSD_LERROR, SAMP ": cannot open '%s', %s.\n",
				path, strerror(errno));
		free(path);
		return errno;
	}

	/*
	Parse socinfo file, it contains three integers with a single
	space between, any problem => fail.
	*/
	if (fscanf(socinfo, "%d %d %d", &tx2mon->n_cpu,
			&tx2mon->n_core, &tx2mon->n_thread) != 3) {
		msglog(LDMSD_LERROR, SAMP ": cannot parse '%s'.\n", path);
		fclose(socinfo);
		free(path);
		return EBADF;
	}

	fclose(socinfo);
	free(path);

	msglog(LDMSD_LINFO, SAMP ": n_cpu: %d, n_core: %d, n_thread: %d.\n",
			tx2mon->n_cpu, tx2mon->n_core, tx2mon->n_thread);

	if (TX2MON_MAX_CPU < tx2mon->n_cpu) {
		msglog(LDMSD_LWARNING, SAMP ": sampler built for max %d CPUs, system reporting %d CPUs, limiting reporting to %d.\n",
				TX2MON_MAX_CPU, tx2mon->n_cpu, TX2MON_MAX_CPU);
		tx2mon->n_cpu = TX2MON_MAX_CPU;
	}

	if (MAX_CPUS_PER_SOC < tx2mon->n_core) {
		msglog(LDMSD_LWARNING, SAMP ": sampler built for max %d cores, system reporting %d cores, limiting reporting to %d.\n",
				MAX_CPUS_PER_SOC, tx2mon->n_core, MAX_CPUS_PER_SOC);
		tx2mon->n_core = MAX_CPUS_PER_SOC;
	}


	tx2mon->cap = CAP_BASIC;
	return 0;

}


static int read_cpu_info(struct cpu_info *s)
{
	assert(s!=NULL);
	int rv;
	struct mc_oper_region *op = &s->mcp;
	rv = lseek(s->fd, 0, SEEK_SET);
	if (rv < 0)
	       return rv;
	rv = read(s->fd, op, sizeof(*op));
	if (rv < sizeof(*op))
		return rv;
	if (CMD_STATUS_READY(op->cmd_status) == 0)
		return 0;
	if (CMD_VERSION(op->cmd_status) > 0)
		s->throttling_available =  1;
	else
		s->throttling_available =  0;
	return 1;
}

#ifdef debug

static inline double cpu_temp(struct cpu_info *d, int c)
{
	return to_c(d->mcp.tmon_cpu[c]);
}

static inline unsigned int cpu_freq(struct cpu_info *d, int c)
{
	return d->mcp.freq_cpu[c];
}

static inline double to_v(int mv)
{
	return mv/1000.0;
}

static inline double to_w(int mw)
{
	return mw/1000.0;
}

/* Used for debugging
 *  * Prints out data in table format similar to tx2mon program */
static void term_init_save(void)
{
	static struct termios nts;

	if (!isatty(1)) {
		term_seq.cl = "";
		term_seq.nl = "\n";
		return;
	}
	ts_saved = malloc(sizeof(*ts_saved));
	if (tcgetattr(0, ts_saved) < 0)
		goto fail;

	nts = *ts_saved;
	nts.c_lflag &= ~(ICANON | ECHO);
	nts.c_cc[VMIN] = 1;
	nts.c_cc[VTIME] = 0;
	if (tcsetattr (0, TCSANOW, &nts) < 0)
		goto fail;

	term_seq.nl = "\r\n";
	return;
fail:
	if (ts_saved) {
		free(ts_saved);
		ts_saved = NULL;
	}
	msglog(LDMSD_LERROR, SAMP ": Failed to set up  terminal %i", errno);
}


/* Used for debugging:
 * Dump out the information stored in the nodes of each core*/
static void dump_cpu_info(struct cpu_info *s)
{
	struct mc_oper_region *op = &s->mcp;
	struct term_seq *t = &term_seq;
	int i, c, n;
	char buf[64];
	
	printf("Node: %d  Snapshot: %u%s", s->node, op->counter, t->nl);
	printf("Freq (Min/Max): %u/%u MHz     Temp Thresh (Soft/Max): %6.2f/%6.2f C%s",
		op->freq_min, op->freq_max, to_c(op->temp_soft_thresh),
		to_c(op->temp_abs_max), t->nl);
	printf("%s", t->nl);
	n = tx2mon->n_core < CORES_PER_ROW ? tx2mon->n_core : CORES_PER_ROW;
	for (i = 0; i < n; i++)
		printf("|Core  Temp   Freq ");
	printf("|%s", t->nl);
	for (i = 0; i < n; i++)
		printf("+------------------");
	printf("+%s", t->nl);
	for (c = 0;  c < tx2mon->n_core; ) {
		for (i = 0; i < CORES_PER_ROW && c < tx2mon->n_core; i++, c++)
			printf("|%3d: %6.2f %5d ", c,
					cpu_temp(s, c), cpu_freq(s, c));
		printf("|%s", t->nl);
	}
	printf("%s", t->nl);
	printf("SOC Center Temp: %6.2f C%s\n", to_c(op->tmon_soc_avg), t->nl);
	printf("Voltage    Core: %6.2f V, SRAM: %5.2f V,  Mem: %5.2f V, SOC: %5.2f V%s",
		to_v(op->v_core), to_v(op->v_sram), to_v(op->v_mem),
		to_v(op->v_soc), t->nl);
	printf("Power	   Core: %6.2f W, SRAM: %5.2f W,  Mem: %5.2f W, SOC: %5.2f W%s",
		to_w(op->pwr_core), to_w(op->pwr_sram), to_w(op->pwr_mem),
		to_w(op->pwr_soc), t->nl);
	printf("Frequency    Memnet: %4d MHz", op->freq_mem_net);
	if (display_extra)
		printf(", SOCS: %4d MHz, SOCN: %4d MHz", op->freq_socs, op->freq_socn);
	printf("%s%s", t->nl, t->nl);
	if (!display_throttling)
		return;

	if (s->throttling_available) {
		printf("%s", t->nl);
		printf("Throttling Active Events: %s%s",
			 get_throttling_cause(op->active_evt, ",", buf, sizeof(buf)), t->nl);
		printf("Throttle Events     Temp: %6d,	  Power: %6d,	 External: %6d%s",
				op->temp_evt_cnt, op->pwr_evt_cnt, op->ext_evt_cnt, t->nl);
		printf("Throttle Durations  Temp: %6d ms, Power: %6d ms, External: %6d ms%s",
				op->temp_throttle_ms, op->pwr_throttle_ms,
				op->ext_throttle_ms, t->nl);
	} else {
		printf("Throttling events not supported.%s", t->nl);
	}
	printf("%s", t->nl);
}

/* Used for debugging:
 * Prints out the throttling "active events"*/
static char *get_throttling_cause(unsigned int active_event, const char *sep, char *buf, int bufsz)
{
	const char *causes[] = { "Temperature", "Power", "External", "Unk3", "Unk4", "Unk5"};
	const int ncauses = sizeof(causes)/sizeof(causes[0]);
	int i, sz, events;
	char *rbuf;

	rbuf = buf;
	if (active_event == 0) {
		snprintf(buf, bufsz, "None");
		return rbuf;
	}

	for (i = 0, events = 0; i < ncauses && bufsz > 0; i++) {
		if ((active_event & (1 << i)) == 0)
			continue;
		sz = snprintf(buf, bufsz, "%s%s", events ? sep : "", causes[i]);
		bufsz -= sz;
		buf += sz;
		++events;
	}
	return rbuf;
}
#endif

/* - Loop through each cpu struct determined in /sys/bus/platform/devices/tx2mon/socinfo
 * - Check that the node file path for each cpu struct exist. 
 * - Read cpu information in /sys/bus/platform/devices/tx2mon/node<i>_raw
 * - Close file path 
 * - Define "debug" to output all structs and their metric values in table format
 *   similar to tx2mon program (term_init_save() does this).
 */

static int parse_mc_oper_region()
{
	int ret, ret1;
	int i;
	char filename[sizeof(TX2MON_NODE_PATH) + 2];
	ret = ret1 = 1;
	assert(tx2mon != NULL);
	for(i = 0; i < tx2mon->n_cpu; i++) {
		
		//Get node path name(s)
		snprintf(filename, sizeof(filename), TX2MON_NODE_PATH, i);
		//set number of nodes for each cpu found
		tx2mon->cpu[i].node = i;
		tx2mon->cpu[i].fd = open(filename, O_RDONLY);
		if (tx2mon->cpu[i].fd < 0){
			msglog(LDMSD_LERROR, SAMP ": Error reading node%i entry.\n", i);
			exit (1);
		}
		ret = read_cpu_info(&tx2mon->cpu[i]);
		if (ret < 0){
			printf("Unexpected read error!\n");
			return EINVAL;
		}
#ifdef debug
		if (ret > 0) {
			tx2mon->samples++;
			term_init_save();
			dump_cpu_info(&tx2mon->cpu[i]);
		}
#endif
		close(tx2mon->cpu[i].fd);
		
	}
	return 0;
}

