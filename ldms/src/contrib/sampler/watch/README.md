To read about this API sketch point a web browser at 

https://baallan.github.io/ovis/

The example programs are:
test\_log.c
test\_progress.c

The test can be run with:

```
  make test
  mkdir -p /dev/shm/ldms_watch
  export LDMS_WATCH=""
  ./test
```

Notes:

This is NOT a full library, it is a stubbed library and documentation
to support design discussions.

The functions all use an admin/user-environment configurable
maximum file size. If later in the code too many lines
or progress points are added such that the file size will 
be exceeded, the excess adds are handled per policy options
and (for tables and progress data) an error counter in the 
output becomes non-zero. The current policy options for the 
log file are stop logging, truncate the log and continue logging,
and treat the log as a ring buffer.

It is trivial to adjust these schemes to force error-handling on
the application writer if desired and to accept max=0 as a flag 
that the app writer wants no limitations. The LDMS sampler may, 
however, impose limitations such that app data is lost. 

The current implementation is a straw-man. The ldms sampler side
is not implemented, but is trivial. The design has accounted for
reliable and safe data file cleanup to keep memory use bounded.
See dox/html/index.html (and then 'modules') for the interface
and test\_log.c and test\_progress.c for usage example.

The progress and table functionality is not implemented.

Interactive user inspection of log files:

For efficiency reasons, the log files contain ^@ characters.
Use ldms\_watch\_cat /dev/shm/ldms\_watch/lammps.\* | more
or equivalent. (ldms\_watch\_cat just filters out the ^@).

Thread-safety:

The base assumption regarding threading is that use of a watch
object and any of its children will be done in a single thread
by the application; this is easily revisited. The optional 'scheduled'
progress output will require either locks or use of atomics within
the library.

Multiple applications:

The base assumption is that there may be thousands of processes using the library at the same time to create data. 
Memory use is bounded as a linear function of the number of log lines or table entries or progress points allowed.
CPU use is very small and driven by application logic.

Multiple data collectors:
There is nothing to prevent multiple data collectors from accessing the output files simultaneously.
