#ifndef gpfs_HEADER_H
#define gpfs_HEADER_H

struct Gpfs
{
        char *node,*name,*prod_name,*job_id,*comp_id,*timestamp,*cluster,
                *filesystem,*disks,*bytes_read,*bytes_written,*opens,
                *closes,*reads,*writes,*read_dir,*inode_updates;
        ldms_set_t set;
} Gpfs;

void update_GPFS(struct Gpfs *gpfs);

#endif
