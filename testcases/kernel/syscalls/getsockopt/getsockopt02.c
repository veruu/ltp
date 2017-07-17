/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This program is free software;  you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Test description: Test retrieving of peer credentials (SO_PEERCRED)
 *
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include "tst_safe_pthread.h"
#include "tst_test.h"

static int socket_fd, thread_socket_fd, accepted;
static struct sockaddr_un sun;

#define SOCKNAME	"testsocket"

static void setup(void)
{
	sun.sun_family = AF_UNIX;
	(void)strcpy(sun.sun_path, SOCKNAME);
	socket_fd = SAFE_SOCKET(sun.sun_family, SOCK_STREAM, 0);
	SAFE_BIND(socket_fd, (struct sockaddr *)&sun, sizeof(sun));
	SAFE_LISTEN(socket_fd, SOMAXCONN);
}

void *thread_func(void *args __attribute__((unused)))
{
	thread_socket_fd = SAFE_SOCKET(sun.sun_family, SOCK_STREAM, 0);
	SAFE_CONNECT(thread_socket_fd, (struct sockaddr *)&sun, sizeof(sun));
	return NULL;
}

static void test_function(void)
{
	pthread_t thread_id;
	struct ucred cred;
	socklen_t cred_len = sizeof(cred);

	SAFE_PTHREAD_CREATE(&thread_id, NULL, thread_func, NULL);
	accepted = accept(socket_fd, NULL, NULL);
	if (accepted < 0) {
		tst_res(TFAIL | TERRNO, "Error with accepting connection");
		return;
	}
	if (getsockopt(accepted, SOL_SOCKET,
				SO_PEERCRED, &cred, &cred_len) < 0) {
		tst_res(TFAIL | TERRNO, "Error while getting socket options");
		return;
	}

	SAFE_PTHREAD_JOIN(thread_id, NULL);
	if (accepted >= 0) {
		(void)shutdown(accepted, SHUT_RDWR);
		SAFE_CLOSE(accepted);
	}
	if (thread_socket_fd >= 0)
		SAFE_CLOSE(thread_socket_fd);

	if (getpid() != cred.pid) {
		tst_res(TFAIL, "Received wrong PID %d, expected %d",
				cred.pid, getpid());
	} else
		tst_res(TPASS, "Test passed");
}

static void cleanup(void)
{
	if (accepted >= 0) {
		(void)shutdown(accepted, SHUT_RDWR);
		SAFE_CLOSE(accepted);
	}
	if (thread_socket_fd >= 0)
		SAFE_CLOSE(thread_socket_fd);
	if (socket_fd >= 0)
		SAFE_CLOSE(socket_fd);
}

static struct tst_test test = {
	.tid = "getsockopt02",
	.test_all = test_function,
	.setup = setup,
	.cleanup = cleanup,
	.needs_tmpdir = 1,
};
