There are two samplers here.

Both samplers use the same code to control what user-selectable metrics are 
included in a metric set.

The differences are:

app_sampler.c:

Subscribes to a spank plugin generated stream identifying processes
spawned directly by slurm.
The sampler produces set instances named as $producer/$job_id/$task_rank.
Within a job, task_rank may get reused across multiple processes if there
are multiple srun commands.

linux_proc_sampler.c (with a bit of code from forkstat in get_stat_field.c):

Subscribes to a netlink-notifier daemon generated stream identifying 
all the processes the kernel announces that are not filtered out by
matching criteria in the notifier configuration.

For non-slurm processes, the sampler produces set instances named as
	 $producer/$job_id/$start_time/$task_pid
where job_id will be 0 for any process not enclosed in a slurm environment.
Task_rank may get recycled but the combination $start_time/$task_rank will not.

For slurm processes, the sampler produces set instances named as 
	 $producer/$job_id/$start_time/rank/$task_rank

Notes!:

There may be a race between the netlink-notifier and the spank notifier if both
are present to inform ldmsd of interesting pids. These can be publishing to separate streams,
in which case there is no race at the sampler, or to the same stream.

If this sampler sees messages from both notifiers on the same stream,
it will switch to the slurm set instance naming when the slurm message 
is seen after the non-slurm message. If the non-slurm message is seen 
after the slurm message, the slurm-derived set will be retained.  In 
either case, message-unique data will be merged to the surviving set.

It is redundant but safe to run both samplers at the same time
subscribing to the same stream.
