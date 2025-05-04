/* SPDX-License-Identifier: MIT */

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
	-n	Dry run. Do not change anything\n\
	-r file	Use this file's time instead of current time\n\
	-t TS	Use this timestamp instead of current time\n\
	-T FMT	strftime(3) format to parse -t option"

#define ERROR_MUTUALLY_EXCLUSIVE1 \
	"The -a, -m & -t options are mutually exclusive"

#define ERROR_MUTUALLY_EXCLUSIVE2 \
	"The -r & -t options are mutually exclusive"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
static int use_atime;
/* Use the file's mtime... */
static int use_mtime;

static int dry_run;

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
		if (use_atime)
			ctime.tv_sec = inode.st_atime;
		else if (use_mtime)
			ctime.tv_sec = inode.st_mtime;
	}

	if (dry_run) {
		char buf[64];
		struct tm *tm = localtime(&ctime.tv_sec);
		strftime(buf, sizeof(buf), "%F %T", tm);
		printf("Would change ctime of %s to %s\n", file, buf);
		return;
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

static void
str2timeval(const char *str, const char *fmt, struct timeval *tvp)
{
	struct tm *tp;
	struct tm tm;
	time_t now;

	now = time(NULL);
	tp = localtime(&now);
	tm = *tp;

	if (strptime(str, fmt, &tm) == NULL)
		errx(1, "invalid time format: %s", str);

	if ((tvp->tv_sec = mktime(&tm)) == -1)
		err(1, "mktime");
	tvp->tv_usec = 0;
}

int
main(int argc, char *argv[])
{
	struct timeval new_ctime = { 0, 0 };
	const char *timefmt= "%Y-%m-%d %H:%M:%S";
	char *timestamp = NULL;
	char *rfile = NULL;
	struct stat inode;
	int i, ch;

	while ((ch = getopt(argc, argv, "amnr:t:T:")) != -1) {
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
		case 'n':
			dry_run = 1;
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
			timestamp = optarg;
			break;
		case 'T':
			timefmt= optarg;
			break;
		default:
			errx(1, USAGE, getprogname());
		}
	}

	if (optind >= argc)
		errx(1, USAGE, getprogname());

	if (timestamp != NULL)
		str2timeval(timestamp, timefmt, &new_ctime);
	else if (rfile != NULL) {
		while (stat(rfile, &inode) < 0)
			if (errno != EINTR)
				err(1, "%s", rfile);
		if (use_atime)
			new_ctime.tv_sec = inode.st_atime;
		else if (use_mtime)
			new_ctime.tv_sec = inode.st_mtime;
		else
			new_ctime.tv_sec = inode.st_ctime;
	}

	for (i = optind; i < argc; i++)
		change_ctime(argv[i], new_ctime);

	return (0);
}
