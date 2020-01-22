#include "ldms_watch.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


enum ldms_watch_log_full {
	lwlf_limit,
	lwlf_truncate,
	lwlf_ring,
};

struct ldms_watch {
	// lib
	char dir[LDMS_WATCH_MAXDIRNAME+1];
	short tlinger;
	short syserr;
	char final_name[LDMS_WATCH_MAXFILENAME+1];
	// log 
	FILE *log;
	char log_name[LDMS_WATCH_MAXFILENAME+1];
	enum ldms_watch_log_full logfull;
	uint32_t maxlog;
	uint32_t maxline;
	uint32_t line_no; //< app log line
	uint32_t row_cur; //< line in buffer to use next
	char *line_buffer; //< log data space.
	size_t line_buffer_size; //< log space bytes.
	// table 
	FILE *tables;
	char table_name[LDMS_WATCH_MAXFILENAME+1];
	uint32_t tabmax;
	// progress
	FILE *progress;
	char progress_name[LDMS_WATCH_MAXFILENAME+1];
	uint32_t progmax;
	uint32_t progint;
	int32_t progoff;
};

static void set_defaults(ldms_watch_p w)
{
	strcpy(w->dir, "/dev/shm/ldms_watch");
	w->tlinger = 120;
	w->syserr = 0;
	w->maxlog = 200;
	w->maxline = 215;
	w->line_no = 0;
	w->logfull = lwlf_limit;
	w->tabmax = 4096;
	w->progmax = 1024;
	w->progint = 1;
	w->progoff = 0;
}


//%dir%/%app%.%pid%.%start_time%.%tlinger.%run_name%.%suffix%
//256,64,20,18,10,64,10; 
static void set_file_names( ldms_watch_p w, const char *app, const char *run)
{
	pid_t pid = getpid();
	struct timeval atv;
	(void)gettimeofday(&atv, NULL);
	unsigned sec = atv.tv_sec;
	unsigned usec = atv.tv_usec;

	sprintf(w->final_name,"%s/%s.%ld.%u.%06u.%u.%s.final",
			w->dir, app, (long)pid, sec, usec, w->tlinger, run);
	sprintf(w->log_name,"%s/%s.%ld.%u.%06u.%u.%s.log",
			w->dir, app, (long)pid, sec, usec, w->tlinger, run);
	sprintf(w->table_name,"%s/%s.%ld.%u.%06u.%u.%s.table",
			w->dir, app, (long)pid, sec, usec, w->tlinger, run);
	sprintf(w->progress_name,"%s/%s.%ld.%u.%06u.%u.%s.progress",
			w->dir, app, (long)pid, sec, usec, w->tlinger, run);
}

static int parse_env(ldms_watch_p w)
{
	return 0; // fixme
#if 0
	char *lw = getenv("LDMS_WATCH");
	for (pair in split(lw,",")) do
		k,v = split(pair,":")
		if (!strcmp(a,"tlinger")) {
			w->tlinger = parse_int(v);
		if (!strcmp(a,"dir")) {
			check maxdirname
			w->dir
		if (!strcmp(a,"syserr"));
	enum ldms_watch_log_full logfull;
	uint32_t maxlog;
	uint32_t maxline;
	uint32_t tabmax;
	uint32_t progmax;
	uint32_t progint;
	int32_t progoff;
#endif
}

ldms_watch_p ldms_watch_init(const char *app, const char *run_name)
{
	char *lw = getenv("LDMS_WATCH");
	if (!lw) {
		errno = ENOTSUP;
		return NULL;
	}

	if (!app || strlen(app) > LDMS_WATCH_MAXSTRNAME || 
		!run_name || strlen(run_name) > LDMS_WATCH_MAXSTRNAME) {
		errno = EINVAL;
		return NULL;
	}

	ldms_watch_p w = calloc(sizeof(*w), 1);
	if (!w) {
		errno = ENOMEM;
		return NULL;
	}
	set_defaults(w);
	if (parse_env(w)) {
		errno = EINVAL;
		free(w);
		return NULL;
	}
	set_file_names(w, app, run_name);
	return w;
}

#if 0
void ldms_watch_continuation(ldms_watch_p watch, const char *previous_job); 
#endif

void ldms_watch_final(ldms_watch_p w)
{
	if (!w)
		return;
	if (w->log) {
		FILE *f = w->log;
		w->log = NULL;
		fclose(f);
		free(w->line_buffer);
		w->line_buffer = NULL;
		w->line_buffer_size = 0;
		w->row_cur = 0;
	}
	if (w->tables) {
		// fixme cleanup
	}
	if (w->progress) {
		// fixme cleanup file, points
	}
	FILE *final = fopen(w->final_name, "w");
	fclose(final);
	free(w);
}

// static const char *known_suffixes[] = { "log", "table", "progress", "final", NULL };
void ldms_watch_tidy(FILE *dryrun)
{
	// get dir from env.
	// glob it for *.final
	// get create time off final file stat, parse tlinger out of name,
	// compute ok = create+tlinger; if now > ok, then delete files with
	// same progid as final and known suffixes.
	//
	// glob for other known suffixes
	// parse out tlinger, start,  and pid
	// if pid missing in proc table or proc entry of pid has start newer
	// than start in filename, check if last_modified+tlinger < now,
	// delete file.
}

int ldms_watch_log_init(ldms_watch_p w)
{
	if (!w)
		return 0;
	// should eventually be shm_open and mmap
	w->log = fopen(w->log_name, "w");
	if (!w->log) {
		if (w->syserr) {
			// do system logging of problem
		}
		return errno;
	}
	w->line_buffer_size = w->maxlog * (w->maxline + 1);
	w->line_buffer = calloc( w->line_buffer_size, 1);
	w->row_cur = 0;
	w->line_no = 0;
	return 0;
}

int ldms_watch_log_add_line(ldms_watch_p w, const char *line)
{
	if (!w || !line || !w->log || line[0] == '\0')
		return 0;
	w->line_no++;
	if (w->row_cur >= w->maxlog) {
		switch (w->logfull) {
		case lwlf_limit:
			return 0;
		case lwlf_truncate:
			// need sema around this
			w->row_cur = 0;
			printf("truncatng at %d; resetting %zu\n", w->line_no, w->line_buffer_size);
			truncate(w->log_name, 0);
			memset(w->line_buffer, 0, w->line_buffer_size);
			// end lock
			break;
		case lwlf_ring:
			w->row_cur = 0;
			break;
		default:
			return EINVAL;
		}
	}
	char clean[w->maxline+1];
	int i = 0;
	// replace disallowed newlines
	for ( ; i < w->maxline; i++) {
		if (line[i] != '\n')
			clean[i] = line[i];
		else
			clean[i] = ' ';
		if (clean[i] == '\0')
			break;
	}
	// ensure nul terminator
	clean[i] = '\0';

	char *buffer = w->line_buffer + w->row_cur * (w->maxline+1);
	struct timeval atv;
	(void)gettimeofday(&atv, NULL);
	unsigned sec = atv.tv_sec;
	unsigned usec = atv.tv_usec;
	// need sema around this if shm
	int cnt = snprintf(buffer,  w->maxline + 1, "%u:%u.%06u:%s\n",
		w->line_no, sec, usec, clean);
	if (cnt >= 0)
		memset(buffer + cnt, '\0', w->maxline + 1 - cnt);
	// end sema
	w->row_cur++;
	// hacky demo code; cleaner with shm
	// for now it's a plain file and we write the whole thing.
	// need flock thing here if not shm
	truncate(w->log_name, 0);
	rewind(w->log);
	// user inspects the file with ldms_watch_cat
#if 1
	fwrite(w->line_buffer, w->line_buffer_size, 1, w->log);
#else
	// if alas we must suppress nuls for less and more pagers., then do this
	for (i = 0; i < w->row_cur; i++) {
		buffer = w->line_buffer + i * (w->maxline+1);
		if (buffer[0] != '\0')
			fprintf(w->log,"%s", buffer);
	}
#endif
	return 0;
}

int ldms_watch_progress_init(ldms_watch_p watch)
{
	return 0;
	// fixme
}
ldms_watch_point_p ldms_watch_progress_add(ldms_watch_p watch, const char *point_name)
{
	return NULL;
	// fixme
}
void ldms_watch_progress_update(ldms_watch_point_p point)
{
	// fixme
}
int ldms_watch_progress_write(ldms_watch_p watch)
{
	return 0;
	// fixme
}
int ldms_watch_progress_schedule_write(ldms_watch_p watch, unsigned interval_s, unsigned offset_us)
{
	return 0;
	// fixme
}

int ldms_watch_table_init(ldms_watch_p watch)
{
	return 0;
	// fixme
}
void ldms_watch_table_set(ldms_watch_p watch, const char *key, const char *value)
{
	// fixme
}
int ldms_watch_table_group_set(ldms_watch_p watch, const char *group, const char *key, const char *value)
{
	return 0;
	// fixme
}
void ldms_watch_table_write(ldms_watch_p watch)
{
	// fixme
}
ldms_watch_log_p ldms_watch_log_open(const char *filename)
{
	return NULL;
	// fixme
}
void ldms_watch_log_close(ldms_watch_log_p log)
{
	// fixme
}
ldms_watch_tables_p ldms_watch_tables_open(const char *filename)
{
	return NULL;
	// fixme
}
void ldms_watch_tables_close(ldms_watch_tables_p tables)
{
	// fixme
}
ldms_watch_progress_p ldms_watch_progress_open(const char *filename)
{
	return NULL;
	// fixme
}
void ldms_watch_progress_close(ldms_watch_progress_p progress)
{
	// fixme
}
int ldms_watch_log_read(ldms_watch_log_p log)
{
	return 0;
	// fixme
}
int ldms_watch_tables_read(ldms_watch_tables_p tables)
{
	return 0;
	// fixme
}
int ldms_watch_progress_read(ldms_watch_progress_p progress)
{
	return 0;
	// fixme
}
