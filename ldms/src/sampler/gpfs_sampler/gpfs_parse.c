#ifndef gpfs_parse_h
#define gpfs_parse_h

/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2021 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS). Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * Copyright (c) 2821 Open Grid Computing, Inc. All rights reserved.
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
 * \file procnet.c
 * \brief /proc/net/dev data provider with set per device
 */

#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"
#include "gpfs.h"
#include <pthread.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))
#endif

/*struct Gpfs
{
        char *node,*name,*prod_name,*job_id,*comp_id,*timestamp,*cluster,
                *filesystem,*disks,*bytes_read,*bytes_written,*opens,
                *closes,*reads,*writes,*read_dir,*inode_updates;
        ldms_set_t set;
} Gpfs;
*/
struct nlist {
        struct nlist *next;
        char *name;
        char *def;
};

#define HASHSIZE 101
#define FLAGSIZE 12
static struct nlist *hashtable[HASHSIZE];
static char* start_flags[FLAGSIZE];
static char* def_flags[FLAGSIZE];

#define SAMP "gpfs"
static int metric_offset;
static base_data_t base;


/*LDMS return function*/


/*Dictionary Functions. Creates, sets, hashes, and looks up definitions.*/
unsigned hash(char *s)
{
        unsigned hashval;
        for (hashval = 0; *s != '\0'; s++)
                hashval = *s +31 * hashval;
        return hashval % HASHSIZE;
}

struct nlist *lookup(char *s)
{
        struct nlist *np;
        for (np = hashtable[hash(s)]; np != NULL; np = np->next)
                if (strcmp(s, np->name) == 0)
                        return np;
        return NULL;
}

char *gpfs_strdup(const char *);

struct nlist *dict_put(char *name, char *def)
{
        struct nlist *np;
        unsigned hashval;
        if ((np = lookup(name)) == NULL) {
                np = (struct nlist *) malloc(sizeof(*np));
                if (np == NULL || (np->name = strdup(name)) == NULL)
                        return NULL;
                hashval = hash(name);
                np->next = hashtable[hashval];
                hashtable[hashval] = np;
        } else
                free((void *)np->def);
        if ((np->def = strdup(def)) == NULL)
                return NULL;
        return np;
}

char *gpfs_strdup(const char *s)
{
        char *r = NULL;
        if(s != NULL)
        {
                const size_t size = strlen(s)+1;
                if((r = malloc(size)) != NULL)
                        memcpy(r, s, size);
                }
        return r;
}


struct Gpfs gpfs_set(char *buffer, struct Gpfs gpfs) {
        char* end_flags[] = {"_ "};
        int i = 0;
        int ARRAY_SIZE=100;
        char** args = (char**)malloc(ARRAY_SIZE*sizeof(char*));
	

	char* token = strtok(buffer, " \t");
	for (int k = 0; token != NULL; k++) {
		args[k] = strdup(token);
                token = strtok(NULL, " \t");
                switch(k){
                	  case 5  : {
				gpfs.name = args[k];
				break;
			} case 9  :  {
				gpfs.cluster = args[k];
				break;
			} case 11 : {
				gpfs.filesystem = args[k];
				break;
			} case 13 : {
				gpfs.disks = args[k];
				break;
			} case 15 : {
                                gpfs.timestamp = args[k];
                                break;
			} case 18 : {
				gpfs.bytes_read = args[k];
				break;
			} case 21 : {
				gpfs.bytes_written = args[k];
				break;
		        } case 23 : {
				gpfs.opens = args[k];
				break;
			} case 25 : {
				gpfs.closes = args[k];
				break;
			} case 27 : {
				gpfs.reads = args[k];
				break;
			} case 29 : {
				gpfs.writes = args[k];
				break;
			} case 31 : {
				gpfs.read_dir = args[k];
				break;
			} case 34 :  {
				gpfs.inode_updates = args[k];
				break;
			}
                }
	}	

        return gpfs;
}


int main (void) {

        char buffer[500];
        struct Gpfs gpfs;
	char temp[500];
        while(fgets(buffer, 500, stdin) != NULL)
        {
		strcat(temp," ");
		strcat(temp,buffer);
        }
	gpfs = gpfs_set(temp,gpfs);
        printf("%s\n",gpfs.name);

        return 0;

}
#endif