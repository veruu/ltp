/******************************************************************************/
/*									      */
/* Copyright (c) International Business Machines  Corp., 2001		      */
/* Copyright (c) 2001 Manoj Iyer <manjo@austin.ibm.com>                       */
/* Copyright (c) 2003 Robbie Williamson <robbiew@us.ibm.com>                  */
/* Copyright (c) 2004 Paul Larson <plars@linuxtestproject.org>                */
/* Copyright (c) 2007 <rsalveti@linux.vnet.ibm.com>                           */
/* Copyright (c) 2007 Suzuki K P <suzuki@in.ibm.com>                          */
/* Copyright (c) 2011 Cyril Hrubis <chrubis@suse.cz>                          */
/* Copyright (c) 2017 Red Hat, Inc.                                           */
/*									      */
/* This program is free software;  you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation; either version 2 of the License, or          */
/* (at your option) any later version.					      */
/*									      */
/* This program is distributed in the hope that it will be useful,	      */
/* but WITHOUT ANY WARRANTY;  without even the implied warranty of	      */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See	              */
/* the GNU General Public License for more details.			      */
/*									      */
/* You should have received a copy of the GNU General Public License	      */
/* along with this program;  if not, see <http://www.gnu.org/licenses/>.      */
/*									      */
/******************************************************************************/
/******************************************************************************/
/* Description:	Test the LINUX memory manager. The program is aimed at        */
/*		stressing the memory manager by simultanious map/unmap/read   */
/*		by light weight processes, the test is scheduled to run for   */
/*		a mininum of 24 hours.					      */
/*									      */
/*		Create two light weight processes X and Y.                    */
/*		X - maps, writes  and unmap a file in a loop.	              */
/*		Y - read from this mapped region in a loop.		      */
/*	        read must be a success between map and unmap of the region.   */
/*									      */
/******************************************************************************/
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <stdlib.h>
#include "tst_safe_pthread.h"
#include "tst_test.h"

#define DISTANT_MMAP_SIZE (64 * 1024 * 1024)

static long file_size = 1024;
static long num_iter = 1000;
static float exec_time = 24;

static char *opt_verbose_print;
static char *opt_file_size;
static char *opt_num_iter;
static char *opt_exec_time;

static char *volatile map_address;
static jmp_buf jmpbuf;
static sig_atomic_t volatile active_map;
static sig_atomic_t volatile test_end;
static void *distant_area;
static pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;

static void sig_handler(int signal, siginfo_t * info, void *ut)
{
	switch (signal) {
	case SIGSEGV:
		if (active_map) {
			tst_res(TINFO, "[%lu] Unexpected page fault at %p",
				 pthread_self(), info->si_addr);
			test_end = signal;
			break;
		}
		longjmp(jmpbuf, 1);
		break;
	default:
		test_end = signal;
		break;
	}
}

static struct tst_option mmap1_options[] = {
	{"l:", &opt_num_iter, "Number of mmap/write/unmap loops, default: 1000"},
	{"s:", &opt_file_size, "Size of the file to be mapped, default: 1024 bytes"},
	{"x:", &opt_exec_time, "Test execution time, default: 24 hours"},
	{"v", &opt_verbose_print, "Verbose output, default: quiet"},
	{NULL, NULL, NULL}
};

static void mmap1_setup(void)
{
	int i;
	int siglist[] = {SIGSEGV, SIGALRM, -1};
	struct sigaction sigptr;

	if (tst_parse_long(opt_file_size, &file_size, 1, LONG_MAX))
		tst_brk(TBROK, "Invalid file size: %s", opt_file_size);
	if (tst_parse_long(opt_num_iter, &num_iter, 1, LONG_MAX))
		tst_brk(TBROK, "Invalid number of interations: %s",
				opt_num_iter);
	if (tst_parse_float(opt_exec_time, &exec_time, 0.0005, INT_MAX))
		tst_brk(TBROK, "Invalid execution time: %s", opt_exec_time);

	if (opt_verbose_print)
		tst_res(TINFO, "Input parameters are: File size: %ld; "
			 "Scheduled to run: %lf hours; "
			 "Number of mmap/write/read: %ld",
			 file_size, exec_time, num_iter);

	tst_set_timeout(exec_time * 3600 + 300);

	sigptr.sa_sigaction = sig_handler;
	sigptr.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sigptr.sa_mask);

	for (i = 0; siglist[i] != -1; i++) {
		if (sigaction(siglist[i], &sigptr, NULL) == -1) {
			tst_brk(TBROK | TERRNO, "could not set handler for %s",
					tst_strsig(siglist[i]));
		}
	}

	/* We don't want other mmap calls to map into same area as is
	 * used for test (mmap_address). The test expects read to return
	 * test pattern or read must fail with SIGSEGV. Find an area
	 * that we can use, which is unlikely to be chosen for other
	 * mmap calls. */
	distant_area = SAFE_MMAP(NULL, DISTANT_MMAP_SIZE,
			PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE,
			-1, 0);
	SAFE_MUNMAP(distant_area, (size_t)DISTANT_MMAP_SIZE);
	distant_area += DISTANT_MMAP_SIZE / 2;
}

static int mkfile(int size)
{
	char template[] = "ashfileXXXXXX";
	int fd, i;

	if ((fd = mkstemp(template)) == -1)
		tst_brk(TBROK | TERRNO, "mkstemp() failed");
	SAFE_UNLINK(template);

	for (i = 0; i < size; i++) {
		SAFE_WRITE(1, fd, "a", 1);
	}
	SAFE_WRITE(1, fd, "\0", 1);
	SAFE_FSYNC(fd);

	return fd;
}

void *map_write_unmap(void *ptr)
{
	long *args = ptr;
	long i, j;

	tst_res(TINFO, "[%lu] - map, change contents, unmap files %ld times",
		 pthread_self(), args[2]);

	if (opt_verbose_print)
		tst_res(TINFO, "map_write_unmap() arguments are: "
			 "fd - arg[0]: %ld; "
			 "size of file - arg[1]: %ld; "
			 "num of map/write/unmap - arg[2]: %ld",
			 args[0], args[1], args[2]);

	for (i = 0; i < args[2]; i++) {
		pthread_mutex_lock(&thread_lock);
		map_address = SAFE_MMAP(distant_area, (size_t)args[1],
				PROT_WRITE | PROT_READ, MAP_SHARED,
				(int)args[0], 0);
		active_map = 1;
		pthread_mutex_unlock(&thread_lock);

		if (opt_verbose_print)
			tst_res(TINFO, "map address = %p", map_address);

		j = 0;
		while (j < args[1]) {
			if (!pthread_mutex_trylock(&thread_lock)) {
				map_address[j] = 'a';
				j++;
				pthread_mutex_unlock(&thread_lock);
			}
			if (random() % 2)
				sched_yield();
		}

		if (opt_verbose_print)
			tst_res(TINFO, "[%ld] times done: of total [%ld] "
					"iterations, map_write_unmap(), "
					"contents of memory: %s",
					i, args[2], map_address);

		pthread_mutex_lock(&thread_lock);
		active_map = 0;
		SAFE_MUNMAP(map_address, (size_t) args[1]);
		pthread_mutex_unlock(&thread_lock);
	}

	pthread_exit(NULL);
}

void *read_mem(void *ptr)
{
	long *args = ptr;
	long i, j;

	tst_res(TINFO, "[%lu] - read contents of memory %p %ld times",
		 pthread_self(), map_address, args[2]);

	if (opt_verbose_print)
		tst_res(TINFO, "read_mem() arguments are: "
			 "number of reads to be performed - arg[2]: %ld; "
			 "read from address %p", args[2], map_address);

	for (i = 0; i < args[2]; i++) {
		if (opt_verbose_print)
			tst_res(TINFO, "read_mem() in while loop %ld times "
				 "to go %ld times", i, args[2]);

		if (setjmp(jmpbuf) == 1) {
			pthread_mutex_unlock(&thread_lock);
			if (opt_verbose_print)
				tst_res(TINFO, "page fault occurred due to "
					 "a read after an unmap");
		} else {
			if (opt_verbose_print) {
				pthread_mutex_lock(&thread_lock);
				tst_res(TINFO, "read_mem(): contents of "
						"memory: %s", map_address);
				pthread_mutex_unlock(&thread_lock);
			}

			for (j = 0; j < args[1]; j++) {
				pthread_mutex_lock(&thread_lock);
				if (map_address[j] != 'a') {
					pthread_mutex_unlock(&thread_lock);
					pthread_exit((void *)-1);
				}
				pthread_mutex_unlock(&thread_lock);
				if (random() % 2)
					sched_yield();
			}
		}
	}
	pthread_exit(NULL);
}

static void test_mmap1(void)
{
	int i, fd;
	void *status;
	pthread_t thid[2];
	long chld_args[3];

	test_end = 0;
	alarm(exec_time * 3600);

	for (;;) {
		if ((fd = mkfile(file_size)) == -1)
			tst_brk(TBROK, "main(): mkfile(): "
					"Failed to create temp file");
		if (opt_verbose_print)
			tst_res(TINFO, "Tmp file created");

		chld_args[0] = fd;
		chld_args[1] = file_size;
		chld_args[2] = num_iter;

		SAFE_PTHREAD_CREATE(&thid[0], NULL, map_write_unmap, chld_args);
		tst_res(TINFO, "created writing thread[%lu]", thid[0]);
		SAFE_PTHREAD_CREATE(&thid[1], NULL, read_mem, chld_args);
		tst_res(TINFO, "created reading thread[%lu]", thid[1]);

		for (i = 0; i < 2; i++) {
			SAFE_PTHREAD_JOIN(thid[i], &status);
			if (status)
				tst_res(TFAIL, "thread [%lu] - process exited "
					 "with %ld", thid[i], (long)status);
		}
		SAFE_CLOSE(fd);

		switch (test_end) {
		case 0:
			continue;
		case SIGALRM:
			tst_res(TPASS, "Test ended, success");
			return;
		default:
			tst_res(TFAIL, "Test failed with unexpected signal %s",
					tst_strsig(test_end));
			return;
		}
	}
}

static struct tst_test test = {
	.test_all = test_mmap1,
	.setup = mmap1_setup,
	.options = mmap1_options,
	.needs_tmpdir = 1,
};
