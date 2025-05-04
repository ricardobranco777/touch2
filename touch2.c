/*
 * Change last-inode-change times on files
*
 * DETAILS:
 *   First we set the system time to the desired ctime, then we call chmod(2)
 *   to force an update of the inode's ctime. Later, we restore the system time
 */

#define USAGE	"%s [-a|-m] [-r file|-t timestamp] files...\n\
Options:\n\
	-a	Use the file's last-access time\n\
	-m	Use the file's last-modification time\n\
	-r file	Use this file's time instead of current time\n\
	-t [[[YYYY:]MM:]DD:]hh:mm:ss[.uuuuuu]\n\
		Use this timestamp instead of current time"

#define ERROR_MUTUALLY_EXCLUSIVE1 \
	"The -a, -m & -t options are mutually exclusive"

#define ERROR_MUTUALLY_EXCLUSIVE2 \
	"The -r & -t options are mutually exclusive"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <err.h>

#ifdef __linux__
extern char *__progname;
#define getprogname()   (__progname)
#endif

/* Use the file's atime instead of ctime as reference */
static int use_atime = 0;
/* Use the file's mtime... */
static int use_mtime = 0;

static void
change_ctime(const char *file, struct timeval ctime)
{
	sigset_t newsigmask, oldsigmask;
	struct timeval now;
	struct stat inode;

	/* Get file's inode information */
	while (stat(file, &inode) < 0)
		if (errno != EINTR)
			err(1, "%s", file);

	if (!timerisset(&ctime)) {
		if (use_atime) {		/* Use the file's own atime */
			ctime.tv_sec = inode.st_atime;
			ctime.tv_usec = inode.st_atim.tv_nsec / 1000;
		} else if (use_mtime) {		/* Use the file's own mtime */
			ctime.tv_sec = inode.st_mtime;
			ctime.tv_usec = inode.st_mtim.tv_nsec / 1000;
		}
	}

	/* Save current time */
	if (gettimeofday(&now, NULL) < 0)
		err(1, "gettimeofday");

	/* Block ALL signals */
	if (sigfillset(&newsigmask) < 0)
		err(1, "sigfillset");
	if (sigprocmask(SIG_SETMASK, &newsigmask, &oldsigmask) < 0)
		err(1, "sigprocmask");

/* ----- BEGIN CRITICAL SECTION ----- */

	/* If there's no time, it will be the current time */
	/* Otherwise set system time to ctime */
	if (timerisset(&ctime) && settimeofday(&ctime, NULL) < 0)
		err(1, "settimeofday");

	/* Touch inode */
	while (chmod(file, inode.st_mode) < 0) 
		if (errno != EINTR) {
			warn("%s", file);
			break;
		}

	/* Restore system time */
	if (timerisset(&ctime) && settimeofday(&now, NULL) < 0)
		err(1, "settimeofday");

/* ----- END CRITICAL SECTION ----- */

	/* Unblock signals */
	if (sigprocmask(SIG_SETMASK, &oldsigmask, NULL) < 0)
		err(1, "sigprocmask");
}

static int
nstrchr(const char *s, char c)
{
	int n = 0;

	if (s == NULL)
		return 0;

	while (*s != '\0')
		if (*s++ == c)
			n++;

	return n;
}

/* converts from "[[[YYYY:]MM:]DD:]hh:mm:ss[.uuuuuu]" to struct timeval */
static void
str2timeval(const char *s, struct timeval *tvp)
{
#define DELIM ':'
#define DOT '.'
	struct tm *tp;
	struct tm tm;
	time_t now;
	int n;

	if (s == NULL || tvp == NULL)
		return;

	now = time(NULL);
	tp = localtime(&now);

	memset(&tm, 0, sizeof(tm));
	/* Default is current date & time */
	memcpy(&tm, tp, sizeof(tm));

	tvp->tv_sec = tvp->tv_usec = 0;

	n = nstrchr(s, DELIM);

	if (n > 4) {
		tm.tm_year = atoi(s) - 1900;
		s = strchr(s, DELIM) + 1;
	}
	if (n > 3) {
		tm.tm_mon = atoi(s) - 1;
		s = strchr(s, DELIM) + 1;
	}
	if (n > 2) {
		tm.tm_mday = atoi(s);
		s = strchr(s, DELIM) + 1;
	}
	if (n > 1) {
		tm.tm_hour = atoi(s);
		s = strchr(s, DELIM) + 1;
	}
	if (n > 0) {
		tm.tm_min = atoi(s);
		s = strchr(s, DELIM) + 1;
	}
	tm.tm_sec = atoi(s);

	tvp->tv_sec = mktime(&tm);

	if ((s = strchr(s, DOT)) != NULL)
		if (*++s != '\0')
		tvp->tv_usec = atol(s);

	return;
}

int
main(int argc, char *argv[])
{
	struct timeval new_ctime = { 0, 0 };
	char *rfile = NULL; /* Reference file */
	struct stat inode;
	int i, ch;

	while ((ch = getopt(argc, argv, "amr:t:")) != -1) {
		switch (ch) {
		case 'a':
			if (use_mtime || timerisset(&new_ctime))
				errx(1, "%s", ERROR_MUTUALLY_EXCLUSIVE1);
			use_atime = 1;
			break;
		case 'm':
			if (use_atime || timerisset(&new_ctime))
				errx(1, "%s", ERROR_MUTUALLY_EXCLUSIVE1);
			use_mtime = 1;
			break;
		case 'r':
			if (timerisset(&new_ctime))
				errx(1, "%s", ERROR_MUTUALLY_EXCLUSIVE2);
			rfile = optarg;
			break;
		case 't':
			if (use_atime || use_mtime)
				errx(1, "%s", ERROR_MUTUALLY_EXCLUSIVE1);
			if (rfile != NULL)
				errx(1, USAGE, getprogname());
			str2timeval(optarg, &new_ctime);
			break;
		default:
			errx(1, USAGE, getprogname());
		}
	}

	if (optind >= argc)
		errx(1, USAGE, getprogname());

	if (rfile != NULL) {
		while (stat(rfile, &inode) < 0)
			if (errno != EINTR)
				err(1, "%s", rfile);

		if (use_atime) {
			new_ctime.tv_sec = inode.st_atime;
			new_ctime.tv_usec = inode.st_atim.tv_nsec / 1000;
		} else if (use_mtime) {
			new_ctime.tv_sec = inode.st_mtime;
			new_ctime.tv_usec = inode.st_mtim.tv_nsec / 1000;
		} else {
			new_ctime.tv_sec = inode.st_ctime;
			new_ctime.tv_usec = inode.st_ctim.tv_nsec / 1000;
		}
	}

	for (i = optind; i < argc; i++)
		change_ctime(argv[i], new_ctime);

	return (0);
}
