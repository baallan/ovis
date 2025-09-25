#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldmsd_plug_api.h"

#define FAKEDATA 1

#define PLUGIN_NAME "threadsampler"
#define MSR_IA32_APERF 0xE7
#define MSR_IA32_MPERF 0xE8

#if FAKEDATA
uint64_t fake;
#endif

struct threadsampler {
    ldms_set_t set;
    ldms_schema_t schema;
    int cpu_count;
    char inst_name[256];
    uint64_t *aperf_vals;
    uint64_t *mperf_vals;
};


static uint64_t read_msr(int cpu, off_t msr_offset)
{
#if FAKEDATA
	return (uint64_t)msr_offset*1000000 + fake;
#else
    char msr_path[32];
    snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", cpu);

    int fd = open(msr_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening MSR for CPU %d: %s\n", cpu, strerror(errno));
        return 0;
    }

    uint64_t value = 0;
    ssize_t ret = pread(fd, &value, sizeof(value), msr_offset);
    if (ret != sizeof(value)) {
        fprintf(stderr, "ERROR reading MSR 0x%llx for CPU %d: %s\n",
                (unsigned long long)msr_offset, cpu, strerror(errno));
        close(fd);
        return 0;
    }

    close(fd);
    return value;
#endif
}


static int threadsampler_constructor(ldmsd_plug_handle_t handle)
{

    printf(">>> threadsampler_constructor() called!\n");
    fflush(stdout);
    int rc = 0;


    struct threadsampler *ts = calloc(1, sizeof(*ts));
    if (!ts)
        return -ENOMEM;

    ldmsd_plug_ctxt_set(handle, ts);
    ts->cpu_count = sysconf(_SC_NPROCESSORS_ONLN);


    if (ts->cpu_count <= 0 || ts->cpu_count > 1024) {
        fprintf(stderr, "Invalid cpu_count detected: %d\n", ts->cpu_count);
        rc = -ENODEV;
        goto err;
    }

    ts->aperf_vals = calloc(ts->cpu_count, sizeof(uint64_t));
    ts->mperf_vals = calloc(ts->cpu_count, sizeof(uint64_t));

    printf("ALLOC: aperf_vals @ %p\n", ts->aperf_vals);
    printf("ALLOC: mperf_vals @ %p\n", ts->mperf_vals);
    if (!ts->aperf_vals || !ts->mperf_vals) {
        fprintf(stderr, "Failed to allocate APERF/MPERF arrays\n");
        rc = -ENOMEM;
        goto err;
    }


    ts->schema = ldms_schema_new(PLUGIN_NAME);
    if (!ts->schema) {
        fprintf(stderr, "Failed to create schema\n");
        rc = -ENOMEM;
        goto err;
    }


    for (int i = 0; i < ts->cpu_count; i++) {
        char metric_name[64];
        snprintf(metric_name, sizeof(metric_name), "cpu%d_ratio", i);
        metric_name[sizeof(metric_name)-1] = '\0';
        printf("DEBUG: Added metric[%d] = '%s'\n", i, metric_name);
        int ret = ldms_schema_metric_add(ts->schema, metric_name, LDMS_V_D64);
        if (ret < 0) {
            fprintf(stderr, "Failed to add metric '%s', rc=%d at index %d\n",
                    metric_name, ret, i);
            rc = ret;
            goto err;
        }
    }


    char hostname[64];
    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    printf("DEBUG: hostname length = %zu, value = '%s'\n",
           strlen(hostname), hostname);

    snprintf(ts->inst_name, sizeof(ts->inst_name), "%s/%s", PLUGIN_NAME, hostname);
    ts->inst_name[sizeof(ts->inst_name) - 1] = '\0';
    ts->set = ldms_set_new(ts->inst_name, ts->schema);
    if (!ts->set) {
        fprintf(stderr, "Failed to create set: %s\n", ts->inst_name);
        rc = -ENOMEM;
        goto err;
    }

    printf("DEBUG: inst_name length = %zu, value = '%s'\n",
       strlen(ts->inst_name), ts->inst_name);


    ldms_set_producer_name_set(ts->set, hostname);


    ldms_set_publish(ts->set);


    int rcrc = ldmsd_set_register(ts->set, PLUGIN_NAME);

    if (rcrc) {
        fprintf(stderr, "threadsampler: ldmsd_set_register failed rc=%d\n", rcrc);
    }
    int base = ldms_metric_by_name(ts->set, "cpu0_ratio");
    printf("Base index of cpu0_ratio is %d\n", base);


    printf("threadsampler: initialized with %d CPUs\n", ts->cpu_count);
    printf("threadsampler: set card (metric count) = %d\n", ldms_set_card_get(ts->set));
    fflush(stdout);

    return 0;

err:

    if (ts) {
        if (ts->set) {
            ldms_set_unpublish(ts->set);
            ldms_set_delete(ts->set);
        }
        if (ts->schema) {
            ldms_schema_delete(ts->schema);
        }
        free(ts->aperf_vals);
        free(ts->mperf_vals);
        free(ts);
        ldmsd_plug_ctxt_set(handle, NULL);
    }
    return rc;
}


static int threadsampler_config(ldmsd_plug_handle_t handle,
                                struct attr_value_list *kwl,
                                struct attr_value_list *avl)
{
    printf(">>> threadsampler_config() called!\n");
#if FAKEDATA
    fake = 1;
#endif
    fflush(stdout);
    struct threadsampler *ts = ldmsd_plug_ctxt_get(handle);
    if (!ts) {
        fprintf(stderr, "threadsampler_config: plugin not constructed yet!\n");
        return EINVAL;
    }

    const char *interval_str = av_value(avl, "interval");
    if (interval_str) {
        printf("threadsampler: interval set to %s microseconds\n", interval_str);

    }

    return 0;
}


static int threadsampler_sample(ldmsd_plug_handle_t handle)
{
    printf(">>> threadsampler_sample() called!\n");
    fflush(stdout);
    struct threadsampler *ts = ldmsd_plug_ctxt_get(handle);
    if (!ts || !ts->set)
        return EINVAL;

#if FAKEDATA
    fake += 1;
#endif

    int metric_count = ldms_set_card_get(ts->set);
    static int warned = 0;

    if (!warned && metric_count != ts->cpu_count) {
        fprintf(stderr, "WARN: metric_count(%d) != cpu_count(%d)\n",
                metric_count, ts->cpu_count);
        warned = 1;
    }

    ldms_transaction_begin(ts->set);

    for (int cpu = 0; cpu < ts->cpu_count; cpu++) {
        if (cpu >= metric_count) {
            fprintf(stderr, "ERROR: cpu index %d >= metric_count %d (bailing)\n",
                    cpu, metric_count);
            break;
        }

        uint64_t aperf = read_msr(cpu, MSR_IA32_APERF);
        uint64_t mperf = read_msr(cpu, MSR_IA32_MPERF);

        ts->aperf_vals[cpu] = aperf;
        ts->mperf_vals[cpu] = mperf;

        double ratio = (mperf > 0) ? ((double)aperf / (double)mperf) : 0.0;
        ldms_metric_set_double(ts->set, cpu, ratio);
    }

    ldms_transaction_end(ts->set);
    if (!ts || !ts->set || !ts->aperf_vals || !ts->mperf_vals) {
        fprintf(stderr, "ERROR: sample() called before initialization complete!\n");
        return EINVAL;
    }


    return 0;
}


static void threadsampler_destructor(ldmsd_plug_handle_t handle)
{
    printf(">>> threadsampler_destructor() called!\n");
    fflush(stdout);

    struct threadsampler *ts = ldmsd_plug_ctxt_get(handle);
    if (!ts)
        return;


    printf("FREE: aperf_vals @ %p\n", ts->aperf_vals);
    printf("FREE: mperf_vals @ %p\n", ts->mperf_vals);


    if (ts->set) {
        ldmsd_set_deregister(ts->inst_name, PLUGIN_NAME);

        ldms_set_unpublish(ts->set);


        ldms_set_delete(ts->set);
    }


    if (ts->schema)
        ldms_schema_delete(ts->schema);

    free(ts->aperf_vals);
    free(ts->mperf_vals);
    free(ts);

    ldmsd_plug_ctxt_set(handle, NULL);

    printf(">>> threadsampler_destructor() finished!\n");
    fflush(stdout);
}

static const char *threadsampler_usage(ldmsd_plug_handle_t handle)
{
    (void)handle;
    return "config name=threadsampler [interval=<usec>]\n";
}


struct ldmsd_sampler ldmsd_plugin_interface = {
    .base.type        = LDMSD_PLUGIN_SAMPLER,
    .base.flags       = LDMSD_PLUGIN_MULTI_INSTANCE,
    .base.constructor = threadsampler_constructor,
    .base.destructor  = threadsampler_destructor,
    .base.config      = threadsampler_config,
    .base.usage       = threadsampler_usage,
    .sample           = threadsampler_sample,
};
