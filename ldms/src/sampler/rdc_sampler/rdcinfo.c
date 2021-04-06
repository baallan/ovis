/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2020-2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2020 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2021, Advanced Micro Devices, Inc. All rights reserved.
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

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include "rdcinfo.h"
#include "dstring.h"
#include "ldmsd_plugattr.h"
#include "coll/fnv_hash.h"

static rdc_field_t default_field_ids[] = {
	RDC_FI_GPU_CLOCK,
	RDC_FI_MEM_CLOCK,
	RDC_FI_MEMORY_TEMP,
	RDC_FI_GPU_TEMP,
	RDC_FI_POWER_USAGE,
	RDC_FI_PCIE_TX,
	RDC_FI_PCIE_RX,
	RDC_FI_GPU_UTIL,
	RDC_FI_GPU_MEMORY_USAGE,
	RDC_FI_GPU_MEMORY_TOTAL,
	RDC_FI_ECC_CORRECT_TOTAL,
	RDC_FI_ECC_UNCORRECT_TOTAL,
	RDC_EVNT_XGMI_0_THRPUT,
	RDC_EVNT_XGMI_1_THRPUT
};

static const uint32_t num_fields_default = sizeof(default_field_ids)/sizeof(default_field_ids[0]);

/* FUNCTIONS */

#if SCHEMA_HAVE_UNITS
/* get unit string of field id, for fields listed in default_field_ids only. */
static char* field_id_unit(rdc_field_t field_id)
{
	switch(field_id) {
	case RDC_FI_GPU_CLOCK:
	case RDC_FI_MEM_CLOCK:
		return "Hz";
	case RDC_FI_MEMORY_TEMP:
	case RDC_FI_GPU_TEMP:
		return "millideg";
	case RDC_FI_POWER_USAGE:
		return "microwatt";
	case RDC_FI_PCIE_TX:
	case RDC_FI_PCIE_RX:
		return "Bps";
	case RDC_FI_GPU_UTIL:
		return "%";
	case RDC_FI_GPU_MEMORY_USAGE:
	case RDC_FI_GPU_MEMORY_TOTAL:
		return "bytes";
	case RDC_FI_ECC_CORRECT_TOTAL:
	case RDC_FI_ECC_UNCORRECT_TOTAL:
		return "";
	default:
		return "";
	}

	return "";
}
#endif

#define MAX_DEVICE_NAME 24
#define MAX_STR_NAME 128

#ifndef MAIN
static
int rdcinfo_update_schema(rdcinfo_inst_t inst, ldms_schema_t schema)
{
	char name[RDC_MAX_STR_LENGTH + 20]; /* room for name + gpu: + %PRIu32 + nul */
	int rc; /* rc == -errno from schema_metric_add */
	const char *fname;

	/* For each GPU and each metric */
	uint32_t gindex, findex;
	switch (inst->shape) {
	case SS_WIDE:
		inst->num_sets = 1;
		for (gindex = 0; gindex < inst->group_info.count; gindex++) {
			for (findex = 0; findex < inst->field_info.count; findex++) {
				snprintf(name, sizeof(name), "gpu%d:%s",
					inst->group_info.entity_ids[gindex],
					field_id_string(inst->field_info.field_ids[findex]));
				rc = ldms_schema_metric_add(schema, name, LDMS_V_U64
			#if SCHEMA_HAVE_UNITS
					, field_id_unit(inst->field_info.field_ids[findex])
			#endif
					);
				if (rc < 0)
					return -rc; /* rc == -errno */
			}
		}
		inst->metric_offset = inst->first_index;
		break;
	case SS_DEVICE:
		inst->num_sets = inst->group_info.count;
		rc = ldms_schema_meta_array_add(schema, "device", LDMS_V_CHAR_ARRAY,
						MAX_DEVICE_NAME);
		if (rc < 0)
			return -rc;
		for (findex = 0; findex < inst->field_info.count; findex++) {
			fname = field_id_string(inst->field_info.field_ids[findex]);
			rc = ldms_schema_metric_add(schema, fname, LDMS_V_U64
		#if SCHEMA_HAVE_UNITS
				, field_id_unit(inst->field_info.field_ids[findex])
		#endif
				);
			if (rc < 0)
				return -rc;
		}
		inst->metric_offset = inst->first_index + 1; /* only update device name once */
		break;
	/* case SS_VECTOR:  need to know if group_info.entity_ids are always
 	* 0 - group_info.count-1 before supporting vector set. */
	default:
		INST_LOG(inst, LDMSD_LERROR, "not yet supported shape %d in rdcinfo_update_schema\n");
		return -ENOTSUP;
	}
	return 0;
}

/* requires input from monotonic so now >= then always. result in microsec */
static uint64_t difftimespec_us( struct timespec *then, struct timespec *now)
{
	uint64_t tus = then->tv_sec*1000000 + then->tv_nsec/1000;
	uint64_t nus = now->tv_sec*1000000 + now->tv_nsec/1000;
	return nus - tus;
}

int rdcinfo_sample(rdcinfo_inst_t inst)
{
	int i = 0;
	rdc_status_t result;

	/* do nothing until the rdc library has run about 2 cycles */
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	uint64_t tdiff = difftimespec_us(&inst->rdc_start, &now);
	if ( tdiff < 2 * inst->update_freq) {
		return 0;
	}

	uint32_t gindex, findex;
	switch (inst->shape) {
	case SS_WIDE:
		inst->base->set = inst->devset[0];
		base_sample_begin(inst->base);
		for (gindex = 0; gindex < inst->group_info.count; gindex++) {
			for (findex = 0; findex < inst->field_info.count; findex++, i++) {
				rdc_field_value value;
				result = rdc_field_get_latest_value(inst->rdc_handle,
						inst->group_info.entity_ids[gindex],
						inst->field_info.field_ids[findex],
						&value);
				if (result != RDC_ST_OK) {
					INST_LOG(inst, LDMSD_LWARNING,
						"%s:Failed to get (gpu %d: field: %d): %s\n",
						inst->group_info.entity_ids[gindex],
						inst->field_info.field_ids[findex],
						rdc_status_string(result));
					continue;
				}
				ldms_metric_set_u64(inst->devset[0],
					inst->metric_offset + i, value.value.l_int);
			}
		}
		base_sample_end(inst->base);
		inst->base->set = NULL;
		break;
	case SS_DEVICE:
		for (gindex = 0; gindex < inst->group_info.count; gindex++) {
			i = 0;
			inst->base->set = inst->devset[gindex];
			base_sample_begin(inst->base);
			if (!inst->meta_done) {
				char tmp[MAX_DEVICE_NAME];
				snprintf(tmp, sizeof(tmp), "gpu%d",
					inst->group_info.entity_ids[gindex]);
				ldms_metric_array_set_str(inst->devset[gindex],
					inst->first_index, tmp);
			}
			for (findex = 0; findex < inst->field_info.count; findex++, i++) {
				rdc_field_value value;
				result = rdc_field_get_latest_value(inst->rdc_handle,
						inst->group_info.entity_ids[gindex],
						inst->field_info.field_ids[findex],
						&value);
				if (result != RDC_ST_OK) {
					INST_LOG(inst, LDMSD_LWARNING,
						"%s:Failed to get (gpu %d: field: %d): %s\n",
						inst->group_info.entity_ids[gindex],
						inst->field_info.field_ids[findex],
						rdc_status_string(result));
					continue;
				}
				ldms_metric_set_u64(inst->devset[gindex],
					inst->metric_offset + i, value.value.l_int);
			}
			base_sample_end(inst->base);
			inst->base->set = NULL;
		}
		inst->meta_done = 1;
		break;
	default:
		INST_LOG(inst, LDMSD_LERROR, "Unsupported shape=%" PRIu32 ".\n",
			inst->shape);
		return EINVAL;
	}

	return 0;
}

static
char *_help =
"rdc_sb config synopsis:\n"
"    config name=INST [COMMON_OPTIONS] [metrics=METRICS] \n"
"\n"
"Option descriptions:\n"
"    metrics   The comma-separated list of metrics to monitor.\n"
"              See rdc_field_t in <rdc/rdc.h> for possible values.\n"
"    shape=SHAPE  Number indicating set layout to use (default: 0):\n"
"                 0: one wide set, with metrics prefixed by \"gpu%d:\"\n"
"                 1: one set per gpu.\n"
"                 2: one set of vectors indexed by gpu (reserved; not yet supported).\n"
"\n"
"The rdc_sampler collects AMD GPU data according to the given 'metrics' option.\n"
"For example metrics=RDC_FI_GPU_CLOCK,RDC_FI_GPU_TEMP,RDC_FI_POWER_USAGE,RDC_FI_GPU_MEMORY_USAGE\n"
"The default metrics list is:\n"
;

const char *rdc_usage()
{
	dstring_t ds;
	dstr_init2(&ds, 2048);
	dstrcat(&ds, _help, DSTRING_ALL);
	int i = 0;
	for ( ; i < num_fields_default; i++) {
		dstrcat(&ds, "    ", 4);
		dstrcat(&ds, field_id_string(default_field_ids[i]), DSTRING_ALL);
		dstrcat(&ds, "\n", 1);
	}
	char *r = dstr_extract(&ds);
	return r;
}
#endif

static uint32_t rdcinfo_hash(rdcinfo_inst_t inst)
{
	if (!inst)
		return 0;
	dstring_t ds;
	dstr_init2(&ds, 2048);
	uint32_t i = 0;
	for ( ; i < inst->num_fields; i++) {
		dstrcat(&ds, ",", 1);
		dstrcat(&ds, field_id_string(inst->field_ids[i]), DSTRING_ALL);
	}
	i = fnv_hash_a1_32(dstrval(&ds), ds.length, 0);
	dstr_free(&ds);
	return i;
}

rdcinfo_inst_t rdcinfo_new(ldmsd_msg_log_f log)
{
	rdcinfo_inst_t x = calloc(1, sizeof(*x));
	if (!x) {
		log(LDMSD_LERROR, SAMP " : out of memory in rdcinfo_new\n");
		return NULL;
	}
	pthread_mutex_init(&x->lock, NULL);
	x->msglog = log;
	return x;
}

void rdcinfo_delete(rdcinfo_inst_t inst)
{
	if (!inst)
		return;
	if (inst->base) {
		INST_LOG(inst, LDMSD_LERROR, "rdcinfo_delete called before rdcinfo_reset.\n");
		return;
	}
	pthread_mutex_destroy(&inst->lock);
	free(inst);
}

void rdcinfo_reset(rdcinfo_inst_t inst)
{
	if (!inst)
		return;
#ifndef MAIN
	if (inst->shape == SS_DEVICE) {
		uint32_t i;
		for (i = 0; i < inst->num_sets; i++) {
			ldms_set_t set = inst->devset[i];
			if (set) {
				const char *tmp = ldms_set_instance_name_get(set);
				ldmsd_set_deregister(tmp, inst->base->pi_name);
				ldms_set_unpublish(set);
				ldms_set_delete(set);
			}
		}
		if (inst->base) {
			/* suppress unregister of wrong inst name and delete.*/
			free(inst->base->instance_name);
			inst->base->instance_name = NULL;
			base_del(inst->base);
		}
	} else {
		ldms_set_t set = inst->devset[0];
		base_del(inst->base);
		if (set) {
			ldms_set_unpublish(set);
			ldms_set_delete(set);
		}
	}
	inst->base = NULL;
#endif
	free(inst->schema_name);
	inst->schema_name = NULL;

	if (inst->rdc_handle) {
		INST_LOG(inst, LDMSD_LINFO, "shutdown rdc\n");
		/* The undo of rdcinfo_init */
		rdc_status_t result;
		result = rdc_field_unwatch(inst->rdc_handle, inst->group_id, inst->field_group_id);
		if (result)
			INST_LOG(inst, LDMSD_LWARNING, "rdc_field_unwatch failed.\n");
		result = rdc_group_field_destroy(inst->rdc_handle, inst->field_group_id);
		if (result)
			INST_LOG(inst, LDMSD_LWARNING, "rdc_group_field_destroy failed.\n");
		result = rdc_group_gpu_destroy(inst->rdc_handle, inst->group_id);
		if (result)
			INST_LOG(inst, LDMSD_LWARNING, "rdc_group_gpu_destroy failed.\n");
		rdc_stop_embedded(inst->rdc_handle);
		rdc_shutdown();
		inst->rdc_handle = NULL;
	}
	inst->group_id = 0;
	inst->field_group_id = 0;
	memset(&inst->group_info, 0, sizeof(inst->group_info));
	memset(&inst->field_info, 0, sizeof(inst->field_info));
	memset(&inst->schema_name_base, 0, sizeof(inst->schema_name_base));
	memset(&inst->rdc_start, 0 , sizeof(inst->rdc_start));
	inst->meta_done =
		inst->first_index =
		inst->metric_offset =
		inst->update_freq =
		inst->max_keep_age =
		inst->max_keep_samples = 0;
	int i;
	for (i = 0; i < inst->num_fields; i++) {
		inst->field_ids[i] = 0;
	}
	inst->num_fields = 0;
	inst->shape = SS_WIDE;
}

static int rdcinfo_hardware_init(rdcinfo_inst_t inst)
{
	int result;
	/* rdc library initialization */
	result = rdc_init(0);
	result = rdc_start_embedded(RDC_OPERATION_MODE_AUTO, &(inst->rdc_handle));
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to start rdc in embedded mode: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Create the group for all GPUs */
	result = rdc_group_gpu_create(inst->rdc_handle, RDC_GROUP_DEFAULT,
			"rdc_ldms_group", &(inst->group_id));
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create the group: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Create the field group */
	result = rdc_group_field_create(inst->rdc_handle, inst->num_fields ,
			&inst->field_ids[0], "rdc_ldms_field_group", &(inst->field_group_id));
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to create the field group: %s.\n",
			rdc_status_string(result));
		return result;
	}

	/* Get the group info and field info */
	result = rdc_group_gpu_get_info(inst->rdc_handle, inst->group_id, &(inst->group_info));
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR, "Failed to get gpu group info: %s.\n",
			rdc_status_string(result));
		return result;
	}
	result = rdc_group_field_get_info(inst->rdc_handle, inst->field_group_id, &(inst->field_info));
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR,  "Failed to get field group info: %s.\n",
			rdc_status_string(result));
		return result;
	}

	result = rdc_field_watch(inst->rdc_handle,
			inst->group_id,
			inst->field_group_id,
			inst->update_freq,
			inst->max_keep_age,
			inst->max_keep_samples);
	if (result != RDC_ST_OK) {
		INST_LOG(inst, LDMSD_LERROR,  "Failed to watch the field group: %s.\n",
			rdc_status_string(result));
		return result;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &inst->rdc_start);
	return 0;
}

static char suffix[32];
static const char *compute_schema_suffix(rdcinfo_inst_t inst)
{

	uint32_t hash = rdcinfo_hash(inst);

	char base[20];
	switch (inst->shape) {
	case SS_WIDE:
		snprintf(base, sizeof(base), "_%d", inst->group_info.count);
		break;
	case SS_DEVICE:
		strcpy(base, "_device");
		break;
	case SS_VECTOR:
		strcpy(base, "_vector");
		break;
	}
	snprintf(suffix, sizeof(suffix), "%s_%"PRIx32, base, hash);
	return suffix;
}

/* set *val to value derived from attribute in avl, or to def_val if
 * not found in avl.
 */
static
int rdcinfo_config_find_int_value(rdcinfo_inst_t inst, struct attr_value_list *avl,
	const char *attribute, uint32_t def_val, uint32_t *val)
{
	if (!inst)
		return EINVAL;
	if ( !avl || !attribute || !val) {
		INST_LOG(inst, LDMSD_LERROR, "rdcinfo_config_find_int_value miscalled\n");
		return EINVAL;
	}
	const char *s = av_value(avl, attribute);
	if (!s) {
		*val = def_val;
		goto out;
	}
	if (!strlen(s)) {
		INST_LOG(inst, LDMSD_LERROR, "needs %s=something\n", attribute);
		return EINVAL;
	}
	char *ep;
	unsigned long u = strtoul(s, &ep, 10);
	if (*ep != '\0' || ep == s || u == ULONG_MAX || u > UINT32_MAX) {
		INST_LOG(inst, LDMSD_LERROR, "rdcinfo_config_find_int_value %s got bad value %s\n",
			attribute, s);
		return EINVAL;
	}
	*val = u;
out:
	INST_LOG(inst, LDMSD_LDEBUG, "rdcinfo_config_find_int_value %s %" PRIu32 "\n",
		attribute, *val);
	return 0;
}

static const char *rdc_opts[] = {
        "schema",
        "instance",
        "producer",
        "component_id",
        "uid",
        "gid",
        "perm",
        "job_set",
        "metrics",
	"shape",
	"update_freq",
	"max_keep_age",
	"max_keep_samples",
        NULL
};


/* populate inst value from avl, and set up gpu reporting accordingly. */
int rdcinfo_config(rdcinfo_inst_t inst, struct attr_value_list *avl)
{
	if (!inst || !avl)
		return EINVAL;
	int rc;

	if (inst->rdc_handle) {
		INST_LOG(inst, LDMSD_LERROR, "rdc already configured.\n");
		return EALREADY;
	}

	rc = ldmsd_plugattr_config_check(rdc_opts, NULL, avl, NULL, NULL, SAMP);
	if (rc)
		return EINVAL;

	char *sbase = av_value(avl, "schema");
	if (!sbase) {
		strcpy(inst->schema_name_base, SAMP);
	} else {
		if (!strlen(sbase)) {
			INST_LOG(inst, LDMSD_LERROR, "empty schema= given. Try again\n");
			return EINVAL;
		}
		if (strlen(sbase) >= MAX_SCHEMA_BASE) {
		INST_LOG(inst, LDMSD_LERROR, " schema name > %d long: %s\n",
			MAX_SCHEMA_BASE, sbase);
			rc = EINVAL;
			return rc;
		}
		strcpy(inst->schema_name_base, sbase);
	}

	char *mt = NULL;
	const char *metrics = av_value(avl, "metrics");
	uint32_t i;
	if (!metrics) {
		/* default to all metrics in default_field_ids */
		for (i = 0; i < num_fields_default; i++)
			inst->field_ids[i] = default_field_ids[i];
		inst->num_fields = num_fields_default;
	} else {
		char *tkn, *ptr;
		INST_LOG(inst, LDMSD_LDEBUG, "metrics=%s.\n", metrics);
		mt = strdup(metrics);
		if (!mt) {
			INST_LOG(inst, LDMSD_LERROR, "out of memory parsing metrics=\n");
			return ENOMEM;
		}
		uint32_t num_fields_max = sizeof(inst->field_ids)/sizeof(inst->field_ids[0]);
		tkn = strtok_r(mt, ",", &ptr);
		while (tkn) {
			rdc_field_t cur_field_id = get_field_id_from_name(tkn);
			if (cur_field_id != RDC_FI_INVALID) {
				if (inst->num_fields >= num_fields_max) {
					INST_LOG(inst, LDMSD_LERROR, "exceeded the max fields allowed %d"
						". Check metrics= parameter.\n", num_fields_max);
					rc = -1;
					goto out_metrics;
				}
				INST_LOG(inst, LDMSD_LDEBUG, "field %d:%s.\n", cur_field_id, tkn);
				inst->field_ids[inst->num_fields++] = cur_field_id;
			} else {
				INST_LOG(inst, LDMSD_LERROR, "Unsupported field %d: %s in metrics=.\n",
					inst->num_fields, tkn);
				rc = ENOTSUP;
				goto out_metrics;
			}
			tkn = strtok_r(NULL, ",", &ptr);
		}
		free(mt);
		mt = NULL;
	}

	uint32_t shape;
	rc = rdcinfo_config_find_int_value(inst, avl, "shape", SS_WIDE, &shape);
	if (rc)
		return rc;
	else
		switch (shape) {
		case SS_DEVICE:
		case SS_WIDE:
			inst->shape = shape;
			break;
		case SS_VECTOR:
			INST_LOG(inst, LDMSD_LERROR, "Not yet supported vector shape. Use 0 or 1.\n");
			return EINVAL;
		default:
			INST_LOG(inst, LDMSD_LERROR, "Unsupported shape=%" PRIu32 ".\n",
				shape);
			return EINVAL;
		}

	rc = rdcinfo_config_find_int_value(inst, avl, "update_freq", 1000000, &inst->update_freq);
	if (rc)
		return rc;

	rc = rdcinfo_config_find_int_value(inst, avl, "max_keep_age", 60, &inst->max_keep_age);
	if (rc)
		return rc;

	rc = rdcinfo_config_find_int_value(inst, avl, "max_keep_samples", 10, &inst->max_keep_samples);
	if (rc)
		return rc;

	rc = rdcinfo_hardware_init(inst);
	if (rc)
		goto out_metrics;

	const char *schema_suffix = compute_schema_suffix(inst);
	char schema_name[MAX_STR_NAME] = { '\0' };
	snprintf(schema_name, MAX_STR_NAME, "%s%s", inst->schema_name_base, schema_suffix);
	inst->schema_name = strdup(schema_name);
	if (!inst->schema_name) {
		rc = ENOMEM;
		goto out_metrics;
	}
#ifndef MAIN
	inst->base = base_config(avl, SAMP, inst->schema_name, inst->msglog);
	if (!inst->base)
		goto out_metrics;
	/* override the schema name default behavior */
	char *tmp = inst->base->schema_name;
	inst->base->schema_name = inst->schema_name;
	inst->schema_name = NULL;
	free(tmp);

	ldms_schema_t schema = base_schema_new(inst->base);
	if (!schema) {
		rc = errno;
		goto out_metrics;
	}
	inst->first_index = ldms_schema_metric_count_get(schema);
	rc = rdcinfo_update_schema(inst, schema);
	if (rc)
		goto out_metrics;

	tmp = NULL;
	for (i = 0; i < inst->num_sets; i++) {
		if (inst->shape == SS_DEVICE) {
			/* temporarily override default instance name behavior */
			tmp = inst->base->instance_name;
			size_t len = strlen(tmp);
			inst->base->instance_name = malloc( len + 20);
			if (!inst->base->instance_name) {
				rc = ENOMEM;
				goto loop_err;
			}
			snprintf(inst->base->instance_name, len+20, "%s/gpu%d", tmp,
					inst->group_info.entity_ids[i]);
		}
		ldms_set_t set = base_set_new(inst->base);
		if (!set) {
			INST_LOG(inst, LDMSD_LERROR, "failed to make %d-th set for %s\n",
				i, schema_name);
			rc = errno;
			goto loop_err;
		}
		inst->devset[i] = set;
		inst->base->set = NULL;
		if (inst->shape == SS_DEVICE) {
			free(inst->base->instance_name);
			inst->base->instance_name = tmp;
		}
		continue;
	loop_err:
		if (tmp) {
			free(inst->base->instance_name);
			inst->base->instance_name = tmp;
			tmp = NULL;
		}
		goto out_metrics;
	}
#endif

	return 0;

out_metrics:
	free(mt);
	rdcinfo_reset(inst);
	return rc;
}

#ifdef MAIN
/* if MAIN is defined, most only the functions and segments needed to compute
 * the schema name are included in compile.
 */

static void rdc_get_schema_name(int argc, char **argv)
{
	int rc = 0;
	dstring_t ds;
	dstr_init2(&ds, 2048);
	int i;
	for (i = 1; i < argc; i++) {
		dstrcat(&ds, argv[i], DSTRING_ALL);
		dstrcat(&ds, " ", 1);
	}
	char *buf = dstr_extract(&ds);
	int size = 1;
	char *t = buf;
	while (t[0] != '\0') {
		if (isspace(t[0])) size++;
		t++;
	}
	struct attr_value_list *avl = av_new(size);
	struct attr_value_list *kwl = av_new(size);
	rc = tokenize(buf, kwl, avl);
	if (rc) {
		fprintf(stderr, SAMP " failed to parse arguments. %s\n", buf);
		rc = EINVAL;
		goto out;
	}
	rdcinfo_inst_t d = rdcinfo_new(ldmsd_log);
	if (!d) {
		fprintf(stderr, "could not create schema from options\n");
		rc = EINVAL;
		goto out;
	}
	rc = rdcinfo_config(d, avl);
	if (!rc)
		printf("%s\n", d->schema_name);
	else
		fprintf(stderr, "counld not init rdcinfo.\n");
	rdcinfo_reset(d);
	rdcinfo_delete(d);
out:
	av_free(kwl);
	av_free(avl);
	free(buf);
	exit(rc);
}

int main(int argc, char **argv)
{
	rdc_get_schema_name(argc, argv);
	return 0;
}

void ldmsd_log(enum ldmsd_loglevel level, const char *fmt, ...) { }
#if 0
rdc_status_t rdc_field_unwatch(rdc_handle_t p_rdc_handle, rdc_gpu_group_t group_id, rdc_field_grp_t field_group_id) { return 1; }
rdc_status_t rdc_group_field_destroy(rdc_handle_t p_rdc_handle, rdc_field_grp_t rdc_field_group_id) { return 1; }
rdc_status_t rdc_group_gpu_destroy(rdc_handle_t p_rdc_handle, rdc_gpu_group_t p_rdc_group_id) { return 1; }
rdc_status_t rdc_stop_embedded(rdc_handle_t p_rdc_handle) { return 1; }
rdc_status_t rdc_shutdown() { return 1;}
rdc_field_t get_field_id_from_name(const char* name) { return 0; }
rdc_status_t rdc_init(uint64_t init_flags) { return 1; }
rdc_status_t rdc_start_embedded(rdc_operation_mode_t op_mode, rdc_handle_t* p_rdc_handle ) { return 1; }
rdc_status_t rdc_group_gpu_create(rdc_handle_t p_rdc_handle, rdc_group_type_t type, const char* group_name, rdc_gpu_group_t* p_rdc_group_id) { return 1; }
rdc_status_t rdc_group_field_create(rdc_handle_t p_rdc_handle, uint32_t num_field_ids, rdc_field_t* field_ids, const char* field_group_name, rdc_field_grp_t* rdc_field_group_id) { return 1; }
rdc_status_t rdc_group_gpu_get_info(rdc_handle_t p_rdc_handle, rdc_gpu_group_t p_rdc_group_id, rdc_group_info_t* p_rdc_group_info) { return 1; }
rdc_status_t rdc_group_field_get_info(rdc_handle_t p_rdc_handle, rdc_field_grp_t rdc_field_group_id, rdc_field_group_info_t* field_group_info) { return 1; }
rdc_status_t rdc_field_watch(rdc_handle_t p_rdc_handle, rdc_gpu_group_t group_id, rdc_field_grp_t field_group_id, uint64_t update_freq, double max_keep_age, uint32_t max_keep_samples) { return 1; }
const char* rdc_status_string(rdc_status_t status) { return ""; }
#endif
