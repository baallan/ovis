/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2019 Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory. Written by
 * Christopher J. Morrone <morrone2@llnl.gov>
 * All rights reserved.
 *
 * This work was performed under the auspices of the U.S. Department of
 * Energy by Lawrence Livermore National Laboratory under
 * Contract DE-AC52-07NA27344.
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
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <coll/rbt.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include "ldms.h"
#include "ldmsd.h"
#include "ldms_jobid.h"
#include "lustre_client.h"
#include "lustre_client_general.h"

#define _GNU_SOURCE

#define LLITE_PATH "/proc/fs/lustre/llite"
#define OSD_SEARCH_PATH "/proc/fs/lustre"

ldmsd_msg_log_f log_fn;
static char producer_name[LDMS_PRODUCER_NAME_MAX];
static uint64_t component_id;
static time_t last_refresh;
static time_t refresh_interval = 30;
#ifdef ENABLE_JOBID
bool with_jobid = true; /* for LJI */
#endif

/* red-black tree root for llites */
static struct rbt llite_tree;

struct llite_data {
        char *fs_name;
        char *name;
        char *path;
        char *stats_path;
        ldms_set_t general_metric_set; /* a pointer */
        struct rbn llite_tree_node;
};

static int string_comparator(void *a, const void *b)
{
        return strcmp((char *)a, (char *)b);
}

static struct llite_data *llite_create(const char *llite_name, const char *basedir)
{
        struct llite_data *llite;
        char path_tmp[PATH_MAX]; /* TODO: move large stack allocation to heap */
        char *token;
        char *state;
        ldms_set_t general_set;

        log_fn(LDMSD_LDEBUG, SAMP" llite_create() %s from %s\n",
               llite_name, basedir);
        llite = calloc(1, sizeof(*llite));
        if (llite == NULL)
                goto out1;
        llite->name = strdup(llite_name);
        if (llite->name == NULL)
                goto out2;
        snprintf(path_tmp, PATH_MAX, "%s/%s", basedir, llite_name);
        llite->path = strdup(path_tmp);
        if (llite->path == NULL)
                goto out3;
        snprintf(path_tmp, PATH_MAX, "%s/stats", llite->path);
        llite->stats_path = strdup(path_tmp);
        if (llite->stats_path == NULL)
                goto out4;
        llite->fs_name = strdup(llite_name);
        if (llite->fs_name == NULL)
                goto out5;
        if (strtok_r(llite->fs_name, "-", &state) == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to parse filesystem name from \"%s\"\n",
                       llite->fs_name);
                goto out6;
        }
        llite->general_metric_set = llite_general_create(producer_name, producer_name,
                                                         llite->fs_name, llite->name,
                                                         component_id);
        if (llite->general_metric_set == NULL)
                goto out6;
        rbn_init(&llite->llite_tree_node, llite->name);

        return llite;
out6:
        free(llite->fs_name);
out5:
        free(llite->stats_path);
out4:
        free(llite->path);
out3:
        free(llite->name);
out2:
        free(llite);
out1:
        return NULL;
}

static void llite_destroy(struct llite_data *llite)
{
        log_fn(LDMSD_LDEBUG, SAMP" llite_destroy() %s\n", llite->name);
        llite_general_destroy(llite->general_metric_set);
        free(llite->fs_name);
        free(llite->stats_path);
        free(llite->path);
        free(llite->name);
        free(llite);
}

static void llites_destroy()
{
        struct rbn *rbn;
        struct llite_data *llite;

        while (!rbt_empty(&llite_tree)) {
                rbn = rbt_min(&llite_tree);
                llite = container_of(rbn, struct llite_data,
                                   llite_tree_node);
                rbt_del(&llite_tree, rbn);
                llite_destroy(llite);
        }
}

/* List subdirectories in LLITE_PATH to get list of
   LLITE names.  Create llite_data structures for any LLITES that we
   have not seen, and delete any that we no longer see. */
static void llites_refresh()
{
        struct dirent *dirent;
        DIR *dir;
        struct rbt new_llite_tree;

        log_fn(LDMSD_LDEBUG, SAMP" rescanning llite directory\n");
        rbt_init(&new_llite_tree, string_comparator);

        /* Make sure we have llite_data objects in the new_llite_tree for
           each currently existing directory.  We can find the objects
           cached in the global llite_tree (in which case we move them
           from llite_tree to new_llite_tree), or they can be newly allocated
           here. */

        dir = opendir(LLITE_PATH);
        if (dir == NULL) {
                log_fn(LDMSD_LWARNING, SAMP" unable to open llite dir %s\n",
                       LLITE_PATH);
                return;
        }
        while ((dirent = readdir(dir)) != NULL) {
                struct rbn *rbn;
                struct llite_data *llite;

                if (dirent->d_type != DT_DIR ||
                    strcmp(dirent->d_name, ".") == 0 ||
                    strcmp(dirent->d_name, "..") == 0)
                        continue;
                rbn = rbt_find(&llite_tree, dirent->d_name);
                if (rbn) {
                        llite = container_of(rbn, struct llite_data,
                                           llite_tree_node);
                        rbt_del(&llite_tree, &llite->llite_tree_node);
                } else {
                        llite = llite_create(dirent->d_name, LLITE_PATH);
                }
                if (llite == NULL)
                        continue;
                rbt_ins(&new_llite_tree, &llite->llite_tree_node);
        }
        closedir(dir);

        /* destroy any llites remaining in the global llite_tree since we
           did not see their associated directories this time around */
        /* llites_destroy();*/
        /* NOTE: ldms_set_delete() doesn't really work until some time post v4.2.
           So instead of llites_destroy(), we'll just keep old data around forever
           in this version of the code.  When ldms_set delete() works we can
           delete the following while() loop and uncomment the llites_destroy()
           call above. */
        while (!rbt_empty(&llite_tree)) {
                struct rbn *rbn;
                rbn = rbt_min(&llite_tree);
                rbt_del(&llite_tree, rbn);
                rbt_ins(&new_llite_tree, rbn);
        }

        /* copy the new_llite_tree into place over the global llite_tree */
        memcpy(&llite_tree, &new_llite_tree, sizeof(struct rbt));

        return;
}

static int llites_sample()
{
        struct rbn *rbn;

        /* walk tree of known LLITEs */
        RBT_FOREACH(rbn, &llite_tree) {
                struct llite_data *llite;
                llite = container_of(rbn, struct llite_data, llite_tree_node);
                llite_general_sample(llite->name, llite->stats_path,
                                   llite->general_metric_set);
        }
}

static int sample(struct ldmsd_sampler *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" sample() called\n");
        if (llite_general_schema_is_initialized() < 0) {
                if (llite_general_schema_init() < 0) {
                        log_fn(LDMSD_LERROR, SAMP" general schema create failed\n");
                        return ENOMEM;
                }
        }

        if ((time(NULL)-last_refresh) >= refresh_interval) {
                last_refresh = time(NULL);
                llites_refresh();
        }

        llites_sample();

        return 0;
}

static void term(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" term() called\n");
        llites_destroy();
        llite_general_schema_fini();
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return NULL;
}

static const char *usage(struct ldmsd_plugin *self)
{
        log_fn(LDMSD_LDEBUG, SAMP" usage() called\n");
	return  "config name=" SAMP " [producer=<prod_name>] [instance=<inst_name>] [component_id=<compid>] [schema=<sname>] [rescan_sec=<rsec>] [with_jobid=<jid>]\n"
		"    <prod_name>  The producer name (default: the hostname)\n"
		"    <inst_name>  The instance name (ignored, dynamically generated)\n"
		"    <compid>     Optional unique number identifier (default: zero)\n"
		"    <sname>      Optional schema name (ignored, static schema name)\n"
                "    <rsec>       Interval in seconds between rescanning for lustre client mounts (default: 30s)\n"
		LJI_DESC;
	return  "config name=" SAMP;
}

static int config(struct ldmsd_plugin *self,
                  struct attr_value_list *kwl, struct attr_value_list *avl)
{
        char *value;

        log_fn(LDMSD_LDEBUG, SAMP" config() called\n");

        value = av_value(avl, "producer");
        if (value != NULL) {
                snprintf(producer_name, sizeof(producer_name),
                         "%s", value);
        }

	value = av_value(avl, "component_id");
	if (value != NULL) {
		component_id = (uint64_t)atoi(value);
        }

	value = av_value(avl, "instance");
	if (value != NULL) {
		log_fn(LDMSD_LWARNING, SAMP ": ignoring option \"instance=%s\"\n",
                       value);
	}

	value = av_value(avl, "schema");
	if (value != NULL) {
		log_fn(LDMSD_LWARNING, SAMP ": ignoring option \"schema=%s\"\n",
                       value);
	}

	value = av_value(avl, "rescan_sec");
	if (value != NULL) {
		refresh_interval = (time_t)atoi(value);
        }

        LJI_CONFIG(value, avl);

        return 0;
}

static struct ldmsd_sampler llite_plugin = {
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
        log_fn = pf;
        log_fn(LDMSD_LDEBUG, SAMP" get_plugin() called\n");
        rbt_init(&llite_tree, string_comparator);
        /* set the default producer name */
        gethostname(producer_name, sizeof(producer_name));

        return &llite_plugin.base;
}
