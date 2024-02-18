/*
 * touch2 v2.0
 *	Change last-inode-change times on files
 *
 * (c) 2002, 2005, 2006, 2017 by Ricardo Branco
 *
 * MIT License
 *
 * OVERVIEW:
 *   You use touch(1) to change the last-access & last-modification times of the files (or directories) you read or modify. The problem is that doing this will change the last-inode-change time to the current time. Now you may use touch2 right after using touch(1) to erase all evidence.
 *   It must be run as root or with CAP_SYS_TIME capabilities.
 *
 * DETAILS:
 *   First, we set the system time to the desired ctime, then we call chmod(2) to force an update of the inode's ctime. Later, we restore the system time as if nothing happened.
 *
 * BUGS / LIMITATIONS:
 *  + *BSD systems restrict settimeofday(2) when running in secure mode.
 *  + Nanosecond resolution (where supported) it's impossible to get right.
 */

#define _FILE_OFFSET_BITS 64
#define __USE_XOPEN2K8

static char usage[] =
	"Usage: ./touch2 [-a|-m] [-r file|-t timestamp] files...\n"
	"  Options:\n"
	"  -h	   Print this help and exit\n"
	"  -a	   Use the file's last-access time\n"
	"  -m	   Use the file's last-modification time\n"
	"  -r file Use this file's time instead of current time\n"
	"  -t [[[YYYY:]MM:]DD:]hh:mm:ss[.uuuuuu]\n"
	"	   Use this timestamp instead of current time\n";

#define ERROR_MUTUALLY_EXCLUSIVE1 \
	"ERROR: The -a, -m & -t options are mutually exclusive!\n"

#define ERROR_MUTUALLY_EXCLUSIVE2 \
	"ERROR: The -r & -t options are mutually exclusive!\n"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#ifndef timerisset
#define timerisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#ifndef _POSIX_SOURCE
#define ST_ATIME_NSEC	  st_atimespec.tv_nsec
#define ST_MTIME_NSEC	  st_mtimespec.tv_nsec
#define ST_CTIME_NSEC	  st_ctimespec.tv_nsec
#else
#define ST_ATIME_NSEC	  st_atimensec
#define ST_MTIME_NSEC	  st_mtimensec
#define ST_CTIME_NSEC	  st_ctimensec
#endif
#elif defined(__linux__) && defined(_STATBUF_ST_NSEC)
#define ST_ATIME_NSEC	  st_atim.tv_nsec
#define ST_MTIME_NSEC	  st_mtim.tv_nsec
#define ST_CTIME_NSEC	  st_ctim.tv_nsec
#elif defined(sun) && (defined(__SVR4) || defined(__svr4__))
#if defined(_XOPEN_SOURCE) || defined(_POSIX_C_SOURCE)
#define ST_ATIME_NSEC	  st_atim.__tv_nsec
#define ST_MTIME_NSEC	  st_mtim.__tv_nsec
#define ST_CTIME_NSEC	  st_ctim.__tv_nsec
#else
#define ST_ATIME_NSEC	  st_atim.tv_nsec
#define ST_MTIME_NSEC	  st_mtim.tv_nsec
#define ST_CTIME_NSEC	  st_ctim.tv_nsec
#endif
#endif

/* Use the file's atime instead of ctime as reference */
static int use_atime = 0;
/* Use the file's mtime... */
static int use_mtime = 0;

/*
 * Returns 0 on success, -1 on (system call) error
 */
static int change_ctime(const char *file, struct timeval ctime)
{
#ifdef SIG_SETMASK
	sigset_t newsigmask, oldsigmask;
#endif
	struct timeval now;
	struct stat inode;
	int status = 0;

	if (file == NULL)
	return -1;

	/* Get file's inode information */
	while (stat(file, &inode) < 0) {
		if (errno != EINTR) {
			perror("stat()");
			return -1;
		}
	}

	if (!timerisset(&ctime)) {
		if (use_atime) {		/* Use the file's own atime */
			ctime.tv_sec = inode.st_atime;
#ifdef ST_ATIME_NSEC
			ctime.tv_usec = inode.ST_ATIME_NSEC / 1000;
#endif
		}
		else
		if (use_mtime) {		/* Use the file's own mtime */
			ctime.tv_sec = inode.st_mtime;
#ifdef ST_MTIME_NSEC
			ctime.tv_usec = inode.ST_MTIME_NSEC / 1000;
#endif
		}
	}

	/* Save current time */
	if (gettimeofday(&now, NULL) < 0) {
		perror("gettimeofday()");
		return -1;
	}

#ifdef SIG_SETMASK
	/* Block ALL signals */
	if (sigfillset(&newsigmask) < 0) {
		perror("sigfillset()");
		return -1;
	}
	if (sigprocmask(SIG_SETMASK, &newsigmask, &oldsigmask) < 0) {
		perror("sigprocmask()");
		return -1;
	}
#endif

/* ----- BEGIN CRITICAL SECTION ----- */

	/* If there's no time, it will be the current time */
	if (timerisset(&ctime)) {
		/* Set system time to ctime */
		if (settimeofday(&ctime, NULL) < 0) {
			perror("settimeofday(ctime)");
			status--;
			goto end;   /* Restore signal mask */
		}
	}

	/* Touch inode */
	while (chmod(file, inode.st_mode) < 0) {
		if (errno != EINTR) {
			perror("chmod()");
			status--;
		}
	}

	if (timerisset(&ctime)) {
		/* Restore system time */
		if (settimeofday(&now, NULL) < 0) {
			perror("settimeofday(now)");
			status--;
		}
	}

/* ----- END CRITICAL SECTION ----- */

end:

#ifdef SIG_SETMASK
	/* Unblock signals */
	if (sigprocmask(SIG_SETMASK, &oldsigmask, NULL) < 0) {
		perror("sigprocmask()");
		return -1;
	}
#endif

	return ((status != 0) ? -1 : 0);
}

static int nstrchr(const char *s, char c)
{
	int n = 0;

	if (!s) return 0;

	while (*s)
		if (*s++ == c) n++;

	return n;
}

/* converts from "[[[YYYY:]MM:]DD:]hh:mm:ss[.uuuuuu]" to struct timeval */
static void str2timeval(const char *s, struct timeval *tvp)
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

static void exit_usage(int status)
{
	FILE *fp;

	if (status)
		fp = stderr;
	else
		fp = stdout;

	fprintf(fp, "%s\n", usage);

	exit(status);
}

int main(int argc, char *argv[])
{
	struct timeval new_ctime = { 0, 0 };
	char *rfile = NULL; /* Reference file */
	struct stat inode;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'a':   /* use atime */
				if (use_mtime || timerisset(&new_ctime)) {
					fprintf(stderr, "%s: %s\n", argv[0], ERROR_MUTUALLY_EXCLUSIVE1);
					exit_usage(1);
				}
				use_atime = 1;
				break;
			case 'm':   /* use mtime */
				if (use_atime || timerisset(&new_ctime)) {
					fprintf(stderr, "%s: %s\n", argv[0], ERROR_MUTUALLY_EXCLUSIVE1);
					exit_usage(1);
				}
				use_mtime = 1;
				break;
			case 'r':   /* use rfile's ctime */
				if (timerisset(&new_ctime)) {
					fprintf(stderr, "%s: %s\n", argv[0], ERROR_MUTUALLY_EXCLUSIVE2);
					exit_usage(1);
				}
				if ((rfile = argv[++i]) == NULL)
					exit_usage(1);
				break;
			case 't':   /* use timestamp */
				if (use_atime || use_mtime) {
					fprintf(stderr, "%s: %s\n", argv[0], ERROR_MUTUALLY_EXCLUSIVE1);
					exit_usage(1);
				}
				if (rfile != NULL) {
					fprintf(stderr, "%s: %s\n", argv[0], ERROR_MUTUALLY_EXCLUSIVE2);
					exit_usage(1);
				}
				if (argv[++i] == NULL)
					exit_usage(1);
				str2timeval(argv[i], &new_ctime);
				break;
			case 'h':   /* help */
				exit_usage(0);
				break;
			default:
				exit_usage(1);
			}
		}
		else {
			break;
		}
	}

	if (i >= argc) {
		exit_usage(1);
	}

	if (rfile != NULL) {
		while (stat(rfile, &inode) < 0) {
			if (errno != EINTR) {
				perror(rfile);
				exit(1);
			}
		}

		if (use_atime) {
			new_ctime.tv_sec = inode.st_atime;
#ifdef ST_ATIME_NSEC
			new_ctime.tv_usec = inode.ST_ATIME_NSEC / 1000;
#endif
		}
		else if (use_mtime) {
			new_ctime.tv_sec = inode.st_mtime;
#ifdef ST_MTIME_NSEC
			new_ctime.tv_usec = inode.ST_MTIME_NSEC / 1000;
#endif
		}
		else {
			new_ctime.tv_sec = inode.st_ctime;
#ifdef ST_CTIME_NSEC
			new_ctime.tv_usec = inode.ST_CTIME_NSEC / 1000;
#endif
		}

	}

	for (; i < argc; i++) {
		if (change_ctime(argv[i], new_ctime) < 0) {
			fprintf(stderr, "%s: There was an error processing \"%s\"\n",
				argv[0], argv[i]);
		}
	}

	exit(0);
}

