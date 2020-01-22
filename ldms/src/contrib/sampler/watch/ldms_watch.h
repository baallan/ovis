/**
 * \mainpage
 *
 * This interface provides a set of simple functions for application writers
 * to define log files, progress data files, or tabulated configuration data files
 * for consumption by LDMSD samplers, other agents, or humans.
 *
 * See the individual module sections for details, starting with Library Description.
 *
 * STRAWMAN: this is a straw proposal, not a finished library.
 * The log file creation and deletion process is the most difficult
 * and has been implemented as a proof of resource management feasibility.
 *
 * Examples of expected usage are functions test_log in file test_log.c 
 * and test_progress in test_progress.c, which can be found in the file list.
 *
 */
#include <stdio.h>

#include <errno.h>
#ifndef ESUCCESS
#define ESUCCESS 0
#endif


/** \addtogroup library-description
\brief STRAWMAN: The utility of this library is to populate and query 
a directory readable by humans or monitoring agents
that want to collect from a standard location application information 
about configuration, progress events or log lines. Strict resource 
usage controls are also provided.

It creates output files with defined formats such as:
filename                           | purpose
-----------------------------------|------------------
/dev/shm/jobmon/$progid.log        | free text lines
/dev/shm/jobmon/$progid.tables     | structured variable information
/dev/shm/jobmon/$progid.progress   | key=value integer progress counters

where progid is constructed so that numerous independent simultaneous processes
can have their data sampled correctly.

Here $progid is an application string derived from a unique combination of developer, user, and system provided information.

*/
/** @{*/
typedef struct ldms_watch *ldms_watch_p;
typedef struct ldms_watch_point *ldms_watch_point_p;
typedef struct ldms_watch_progress *ldms_watch_progress_p;
typedef struct ldms_watch_tables *ldms_watch_log_p;
typedef struct ldms_watch_log *ldms_watch_tables_p;

#define LDMS_WATCH_MAXDIRNAME 255
#define LDMS_WATCH_MAXSTRNAME 64
#define LDMS_WATCH_MAXFILENAME 511

/** @}*/

/** \addtogroup admin-variables
 * \brief STRAWMAN: environment variables system administrators or
 *users can set to control output file management.

\verbatim
environment LDMS_WATCH
\endverbatim
All watch output functions are gated by the presence (or absence to disable them) of environment variable LDMS_WATCH.

If LDMS_WATCH exists, but is empty, all defaults will be used.
If it is absent, all watch library functions become null operations for the returned pointer.
If LDMS_WATCH has a value, the value will be parsed as a comma separated list of key:value pairs.
The options and defaults are as follows:

option     |default
-----------|------------------------------------------------
dir        |/dev/shm/ldms_watch
tlinger    |120
maxlog     |200
maxline    |215
logfull    |limit
tabmax     |4096
progmax    |1024
syserr     |0
progint    |1
progoff    |0
\#progid    |\%app\%.\%pid\%.\%start_time\%.\%tlinger.\%run_name\%

If any option key is repeated within the string, the last value seen for that key is the value used. Thus the user's application launch environment can append adjustments to the system-define default LDMS_WATCH.

\verbatim
environment LDMS_WATCH option progid 
\endverbatim
is not tunable at this time and will be ignored. It is a format string described here to document our file naming convention:

\%field\%         | meaning
-------------|------------------------------------------------
app         | will be replaced with the value of the app argument passed to ldms_watch_init after any "." in it are replaced with "_".
pid         | is replaced with the process id of the ldms_watch_init caller.
start_time  | is replaced with the time (sec.us) of the call to ldms_watch_init.
tlinger     | is the value of tlinger in the LDMS_WATCH variable.
run_name    | is replaced with run_name from the ldms_watch_init arguments.

\verbatim
environment LDMS_WATCH option tlinger 
\endverbatim
is the advisory number of seconds after which a file without a corresponding live pid and create time can be deleted by any third party. The value should be at least twice the interval (in seconds) of the sampler collecting ldms_watch data. The default is (2^N -1 -40) for N=8.

\verbatim
environment LDMS_WATCH option maxlog 
\endverbatim
provides the upper line count on log files.  Log lines are prefixed by a sequence number and timestamp. Sequence numbers may be used to detect lost lines. Timestamps may be used to correlate system time with other data sources.  

\verbatim
environment LDMS_WATCH option maxline 
\endverbatim
provides the upper line length in log files; longer input strings will be truncated.

\verbatim
environment LDMS_WATCH option logfull 
\endverbatim
determines the behavior when maxlog is reached on log files:

value    | log behavior
---------|-------------------
limit    | ignores further log lines submitted.
truncate | truncates the log file and continues recording.
ring     | treats the file as a ring buffer, overwriting previous lines.
The wisdom of these choices depends on the application ldms_watch usage and
the storage plugin handling the data.

\verbatim
Environment LDMS_WATCH_JOB_CONTINUES
\endverbatim
If set for use by ldms_watch_init, LDMS_WATCH_JOB_CONTINUES should contain a job identication string.
 */
/** @{*/
 
/** @}*/
#define LDMS_WATCH_ENV 0x7

/** \addtogroup setup
 * \brief STRAWMAN: initialization of the watch context for application data capture. 
 * 
 * Initialization takes all the parameters of interest to the system administrator
 * from key:value pairs in the environment variable LDMS_WATCH.
 * The user launching the binary is free to adjust the value of LDMS_WATCH
 * set by the system, as is the binary, but neither is recommended as
 * doing so may cause data loss.
 */
/** @{*/
/*
ldms_watch_init creates the directory structure $dir if it is not present.
Init initializes the progid prefix string used in constructing output file names.
@param app string to identify the application generically, e.g. all lammps users would pass "lammps" for app.

@param run_name string to differentiate this execution from all others expected on the same node. For example if the same job will execute 12 cases and each case is an MPI run with 16 processes, construct the run_name as perhaps "case_$K_rank_$MPI_Comm_rank".

@return context object for communicating to the watcher.
*/
ldms_watch_p ldms_watch_init(const char *app, const char *run_name);

/**
 * \brief Log that the prior job in simulation time had the jobid previous_job.
 * This provides a canonical way to associate two jobs time-limited by
 * the resource manager are related in simulation time.
 * Not every job is a continuation, so calling this is not required.
 *
 * @param watch the context from ldms_watch_init.
 * @param previous_job the job identifier (as a string) of prior run
 * that this run continues. If NULL, watch will be the same as if
 * this function was not called. IF value is LDMS_WATCH_ENV, then
 * environment variable LDMS_WATCH_JOB_CONTINUES will be consulted
 * for the string to use.
 */
void ldms_watch_continuation(ldms_watch_p watch, const char *previous_job); 

/** \brief Close up all open watch files from this watcher and leave a signal in the file system that the application run is done. 
 * Any further calls on watch have undefined results.
 * If this function is not called (e.g. due to crash) the file system will 
 * still have enough information for cleanup to proceed by other means safely.
 * Normally, a resource manager will also wipe the /dev/shm file system
 * after a job is done.
 * This call also frees all progress points and any other internal data structures
 * supporting the log, progress, and tables files. Using these objects
 * after this call yields undefined behavior.
 */

void ldms_watch_final(ldms_watch_p watch);
/**
 * \brief Remove obsolete files.
 * Examine the file system indicated in the LDMS_WATCH environment variable
 * and remove all obsolete files.
 * Obsolete files are those whose producing process is gone that are also
 * older than tlinger.
 *
 * If there is no LDMS_WATCH, does nothing.
 *
 * @param dryrun if NOT NULL, actions that would be taken are printed
 * to dryrun, else silently removes all obsolete files.
 *
 * Files which do not use the naming conventions of the library are ignored.
 * Recommended usage is to call this before ldms_watch_init succeeds
 * in applications which are prone to being run many times in the same
 * job.
 */
void ldms_watch_tidy(FILE *dryrun);

 
/** @}*/

/** \addtogroup log
 * \brief STRAWMAN: manage the .log file of the watch.
 * Log files have a fixed line size
 * as defined in the LDMS_WATCH environment variable options.
 * This enables strict resource management and fast log
 * collection by samplers.
 */
/** @{*/


/** \brief Create a log file for arbitrary messages, typically used for
 * to the run input file lines.
 *
 * The log will be sized and watched to the extent defined by the options in 
 * the LDMS_WATCH environment variable. 
 * @param watch the watch context from ldms_watch_init.
 * @return an errno value.
 */
int ldms_watch_log_init(ldms_watch_p watch);

/** \brief Add a complete line to the log.
 *
 * Line format is up to the library, but it will end with
 * "%s\n" and $s will apply to line.
 * At present, the line format is "$line:$epochtime:$line\n".
 * @param watch the watch context from ldms_watch_init.
 * @param line a null-terminated string NOT containing any linefeed characters.
 * @return errno value.
 */
int ldms_watch_log_add_line(ldms_watch_p watch, const char *line);

 
/** @}*/

/** \addtogroup table
 * \brief  STRAWMAN: Manage a standard tabular format for key application data. 
 *
 * Developers willing and able
 * to describe the run as a set of parameter objects can better
 * support standard analysis pipelines by using a table.
 *
 * The ldms_watch_table_set and ldms_watch_table_group_set
 * are used to add values to the table(s).
 *
 * The application author determines when table file output is
 * appropriate and calls ldms_watch_table_write.
 *
 * Because the application may call the table set functions at any time,
 * the data file produced is potentially dynamic in number and content
 * of tables.
 *
 * At present removal of a key or group once defined is unsupported.
 *
 * Functions for parsing of the formatted output files are also provided,
 * so that client sampler code is independent of the formatting used.
 *
 * At present everything tabulated is a string (untyped values). This
 * is easily extended to supporting typed data if desired.
 */
/* @{*/

/** Create a tables file. 
 *
 * @param watch the watch context from ldms_watch_init.
 * @return an errno value.
 */
int ldms_watch_table_init(ldms_watch_p watch);

/** \brief set a key/value pair in the unnamed table.
 *
 * Creates a new table entry if the value has not been seen before.
 * @param watch the watch context from ldms_watch_init.
 * @param key the name of the value in the table.
 * @param value the value.
 * @return an errno.
 */
void ldms_watch_table_set(ldms_watch_p watch, const char *key, const char *value);

/** \brief add or set a value in a named sub-table
 *  
 * The same key can be used independently in distinct subtables.
 * @param watch the watch context from ldms_watch_init.
 * @param group the sub-table name
 * @param key the name of the value in the table.
 * @param value the value.
 * @return an errno.
 */
int ldms_watch_table_group_set(ldms_watch_p watch, const char *group, const char *key, const char *value);

// c++ extension: add list to a group
// int ldms_watch_table_add_group_kvlist(ldms_watch_p watch, const char *group, map<string, string>);

/** \brief Atomically write all table values to the table file.
 *
 * Writing out table files incrementally as values are updated may pose
 * race conditions and consistency problems. The app developer most likely
 * knows best when a file update is appropriate. In any case, a normal
 * exit (at ldms_watch_final) will always write the table file.
 * @param watch the watch context from ldms_watch_init.
 * @return an errno.
 */
void ldms_watch_table_write(ldms_watch_p watch);

// c extensions 
// void ldms_watch_table_add_kvlist(char **argv, int argc);
// c++ extensions 
// void ldms_watch_table_add_kvlist(map<string, string>& strmap);


/** @}*/

/** \addtogroup progress
 \brief STRAWMAN: Manage rolling unsigned progress counters.

 The application can define a set of progress counters for anyone to 
 examine interactively or collect as a data blob. 
 
 The ldms_watch_progress_update increments the counter at a progress point.

 The application has control over how often the entire set is written out.
 The output format is a key=value format where the key is the point_name give to
 ldms_watch_progress_add. The exact file format is not yet defined, but is
 likely to be .ini or TOML.

 The application author can choose between scheduled periodic file
 output by the watch library (ldms sampler style interval and offset)
 and manual application driven output. The ldms_watch_progress_write
 and ldms_watch_progress_schedule_write functions manage this choice.

 Because the application may call ldms_watch_progress_add at any time,
 the data file produced is potentially dynamic in number and value of keys.

 At present removal of a point once added is not supported.

 Functions for parsing of the formatted output files are also provided,
 so that client sampler code is independent of the formatting used.
 */
/** @{*/

/** \brief Create a progress file.
 * @param watch the watch context from ldms_watch_init.
 */
int ldms_watch_progress_init(ldms_watch_p watch);

/** \brief Name a progress point counter.
 *
 * @param watch the watch context from ldms_watch_init.
 * @param point_name name of the counter to appear in the output data.
 * There is no limit on the length of the point name, but certain characters
 * will be replaced if found: {:=,;[whitespace]} with characters compatible
 * with potential downstream data processing tools {@/.-_}. 
 */
ldms_watch_point_p ldms_watch_progress_add(ldms_watch_p watch, const char *point_name);

/** \brief increment the counter for the given point. 
 * @param point result from a previous call to ldms_watch_progress_add; normally
 * progress points are stored in object or static variables.
 * All progress point objects will be destroyed by ldms_watch_final; any stored
 * references should be ignored after final is called.
 * Incrementing a NULL point is ignored.
 */
void ldms_watch_progress_update(ldms_watch_point_p point);

/** \brief Atomically Write all counters to the progress file.
 * @param watch the watch context from ldms_watch_init.
 * @return an errno.
 */
int ldms_watch_progress_write(ldms_watch_p watch);

/** \brief schedule automatic writing of counters in an independent thread (optional).
 *
 * The app developer who does not want to make ldms_watch_progress_write calls explicitly
 * can schedule how often they prefer to report progress point counters.
 * The app developer can use the interval value of 0 to indicate they want to use the
 * system provided progint and progoff from the LDMS_WATCH environment option values.
 * @param watch the watch context from ldms_watch_init.
 * @param interval_s the number of seconds between progress report writes. 
 * @param offset_us the number of microseconds offset from the interval mark at wichc to write the report.
 * @return an errno.
 */
int ldms_watch_progress_schedule_write(ldms_watch_p watch, unsigned interval_s, unsigned offset_us);

/** @}*/
 
/** \addtogroup monitoring-functions
 \brief STRAWMAN: Read files created by this library.

 Monitoring tools can use these functions to correctly read potentially
 active files.

 This interface has not yet been fleshed out with accessor functions
 on the data pointers returned.
 */
/** @{*/

/** \brief Open an existing log file.
  */
ldms_watch_log_p ldms_watch_log_open(const char *filename);
void ldms_watch_log_close(ldms_watch_log_p log);

/** \brief Open an existing log file.
  */
ldms_watch_tables_p ldms_watch_tables_open(const char *filename);
void ldms_watch_tables_close(ldms_watch_tables_p tables);

/** \brief Open an existing log file.
  */
ldms_watch_progress_p ldms_watch_progress_open(const char *filename);
void ldms_watch_progress_close(ldms_watch_progress_p progress);

/** \brief Open an existing log file.
  */
int ldms_watch_log_read(ldms_watch_log_p log);

/** \brief Read an existing tables file.
  */
int ldms_watch_tables_read(ldms_watch_tables_p tables);

/** \brief Read an existing progress file.
  */
int ldms_watch_progress_read(ldms_watch_progress_p progress);
/** @}*/
