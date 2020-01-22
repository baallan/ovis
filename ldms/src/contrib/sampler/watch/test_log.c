#include "ldms_watch.h"
#include "string.h"
#include "stdlib.h" // getenv
#include "errno.h"
static void warn_bad_env() {
	char *lwok = getenv("LDMS_WATCH");
	if (!lwok)
		printf("Warning: LDMS_WATCH unset. Results may be unexpected.\n");
}

static ldms_watch_p gw = NULL; // simple globally used watch for demo.

static void fail(const char *func, int err)
{
	printf("FAIL of %s: %d: %s.\nquitting.\n",
		func, err, strerror(err));
	free(gw);
	exit(err);
}

static void repeat_line(int i)
{
	char buf[20];
	sprintf(buf,"iteration: %d", i);
	ldms_watch_log_add_line(gw, buf);
}

// int main() {
int test_log()
{
	int err;
	warn_bad_env();
	ldms_watch_tidy(NULL);
	gw = ldms_watch_init("lammps","test_log");
	if ((err = ldms_watch_log_init(gw)) != ESUCCESS)
		fail("ldms_watch_log_init", err);
	ldms_watch_log_add_line(gw,"test line from application");
	int i;
	for (i = 0; i < 500; i++) {
		repeat_line(i);
	}
	ldms_watch_final(gw);
	gw = NULL;
	return 0;
}
