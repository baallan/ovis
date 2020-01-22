#include "ldms_watch.h"
#include "string.h"
#include "stdlib.h" // getenv
#include "errno.h"
#include "unistd.h"

static void warn_bad_env() {
	char *lwok = getenv("LDMS_WATCH");
	if (!lwok)
		printf("Warning: LDMS_WATCH unset. Results may be unexpected.\n");
}

static ldms_watch_p gw = NULL; 
// simple globally used (this file) watch for demo. Should be class member
// or passed around in c/c++ code. As presently defined, gw is not a thread-safe
// object.

static void fail(const char *func, int err)
{
	printf("FAIL of %s: %d: %s.\nquitting.\n",
		func, err, strerror(err));
	free(gw);
	exit(err);
}

static void repeat_line(int i)
{
	static ldms_watch_point_p wp;
	if (!wp) {
		wp = ldms_watch_progress_add(gw, __PRETTY_FUNCTION__);
	}
	ldms_watch_progress_update(wp);
}

// int main() {
int test_progress()
{
	int err;
	warn_bad_env();
	ldms_watch_tidy(NULL);
	gw = ldms_watch_init("lammps","test_log");
	if ((err = ldms_watch_progress_init(gw)) != ESUCCESS)
		fail("ldms_watch_progress_init", err);
	ldms_watch_progress_schedule_write(gw, 2, 0);
	int i;
	for (i = 0; i < 6; i++) {
		repeat_line(i);
		sleep(1);
	}
	ldms_watch_final(gw);
	gw = NULL;
	return 0;
}
