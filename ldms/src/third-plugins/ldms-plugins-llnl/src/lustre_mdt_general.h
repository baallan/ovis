/* -*- c-basic-offset: 8 -*- */
/* Copyright 2019 Lawrence Livermore National Security, LLC and other
 * ldms-plugins-llnl project developers.  See the top-level COPYRIGHT file
 * for details.
 *
 * SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
 */
#ifndef __LUSTRE_MDT_GENERAL_H
#define __LUSTRE_MDT_GENERAL_H

#include <ldms/ldms.h>
#include <ldms/ldmsd.h>

int mdt_general_schema_is_initialized();
int mdt_general_schema_init();
void mdt_general_schema_fini();
ldms_set_t mdt_general_create(const char *producer_name, const char *fs_name,
                              const char *mdt_name);
char *mdt_general_osd_path_find(const char *search_path, const char *mdt_name);
void mdt_general_sample(const char *mdt_name, const char *stats_path,
                        const char *osd_path, ldms_set_t general_metric_set);
void mdt_general_destroy(ldms_set_t set);

#endif /* __LUSTRE_MDT_GENERAL_H */