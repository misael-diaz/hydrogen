/*

sonic-httpd

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT 8080

#ifndef __cplusplus
#define __httpd_extern
#else
#define __httpd_extern extern "C"
#endif

#define __httpd_internal __attribute__ ((visibility("hidden")))

static int request = 0;
static int running = 0;

__httpd_extern
__httpd_internal
void HttpSignalHandler(int signum) {
	if (SIGINT == signum) {
#if DEVBUILD
		fprintf(
			stdout,
			"\n\n%s\n\n",
			"HttpSignalHandler: received SIGINT terminating execution normally"
		);
#endif
		running = 0;
		return;
	}
	else if (SIGIO == signum) {
#if DEVBUILD
		fprintf(
			stdout,
			"\n\n%s\n\n",
			"HttpSignalHandler: received SIGIO due to request on listening socket"
		);
#endif
		request = 1;
		return;
	}
	else if (SIGURG == signum) {
#if DEVBUILD
		fprintf(
			stdout,
			"\n\n%s\n\n",
			"HttpSignalHandler: received SIGURG: WARNING: unhandled: PANIC"
		);
#endif
		running = 0;
		return;
	}
}

__httpd_extern
__httpd_internal
int HttpHeaderRead(
		void * const head,
		int const sockfd
) {
	int sw = 0;
	ssize_t ret = 0;
	ssize_t bytes_read = 0;
	ssize_t bytes_total = 0;
	size_t const chunk = 16;
	char *p = (typeof(p)) head;
	do {
		ret = read(sockfd, p, chunk);
		if (-1 == ret) {
			if (EINTR != errno) {
				goto error_handler;
			}
			sw = 1;
		}
		else {
			bytes_read = ret;
			bytes_total += bytes_read;
			p += bytes_read;
			if (!bytes_read) {
				sw = 0;
			}
			else if (chunk != bytes_read)
				sw = 0;
			else {
				sw = 1;
			}
		}
	} while (sw);

#if DEVBUILD
	fprintf(stdout, "%s\n", "request header:");
	fprintf(stdout, "%s", (char*) head);
	fprintf(stdout, "bytes: %ld\n", bytes_total);
#endif
	return HTTP_SUCCESS_RC;
error_handler:
	fprintf(stderr, "%s\n", strerror(errno));
	return HTTP_FAILURE_RC;
}

__httpd_extern
__httpd_internal
int HttpRespond(void *data) {

	// NOTE: the child process inherits the signal table from the parent so we need to set SIGINT to its default action (does not affect the parent process (i.e. the http-server)
	struct sigaction sa = {};
	sa.sa_handler = SIG_DFL;

	errno = 0;
	int rc = sigemptyset(&sa.sa_mask);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	sa.sa_flags = 0;

	errno = 0;
	rc = sigaction(SIGINT, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	errno = 0;
	// NOTE: probably not necessary because child is not the owner of the listening socket
	rc = sigaction(SIGIO, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	rc = sigaction(SIGURG, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	errno = 0;
	// NOTE: probably not necessary because child is not the owner of the listening socket
	rc = sigaction(SIGURG, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	char response[] = (
		"HTTP/1.1 200\r\n"
		"\r\n"
	);

	errno = 0;
	int *sockfd = (typeof(sockfd)) data;
	int fd = *sockfd;
	ssize_t bytes_written = write(fd, response, sizeof(response) - 1);
	if (-1 == bytes_written) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	close(fd);
	return 0;
}

int main () {
	errno = 0;
	char hostname[PATH_MAX];
	int rc = gethostname(hostname, sizeof(hostname));
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	struct sigaction sa = {};
	sa.sa_handler = HttpSignalHandler;
	errno = 0;
	rc = sigemptyset(&sa.sa_mask);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	sa.sa_flags = SA_RESTART;

	errno = 0;
	rc = sigaction(SIGINT, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	rc = sigaction(SIGIO, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	errno = 0;
	// NOTE: getaddrinfo does not set `errno` unless there's an issue at the system level and it does not simply set the error code `rc` to -1 as other utilities (see man getaddrinfo() for more details)
	char const *node = hostname;
	char const *service = NULL;
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_addrlen = sizeof(struct sockaddr_in);
	struct addrinfo *ai = NULL;
	rc = getaddrinfo(
		node,
		service,
		&hints,
		&ai
	);
	if (rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	struct sockaddr_in *sin = (typeof(sin)) ai->ai_addr;
	sin->sin_port = htons(PORT);
	fprintf(
		stdout,
		"host: %s port: %d\n",
		inet_ntoa(sin->sin_addr),
		ntohs(sin->sin_port)
	);

	errno = 0;
	int const fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (-1 == fd) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	rc = fcntl(fd, F_SETOWN, getpid());
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	rc = fcntl(fd, F_GETFL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	int flags = rc;
	flags |= O_ASYNC;
	rc = fcntl(fd, F_SETFL, flags);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	rc = bind(fd, (struct sockaddr*) sin, sizeof(*sin));
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	int const backlog = 32;
	rc = listen(fd, backlog);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	ssize_t ret = sysconf(_SC_PAGESIZE);
	if (-1 == ret) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	size_t const pagesize = ret;
	size_t size_mmap = pagesize;
	void *head = mmap(
		NULL,
		size_mmap,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE,
		-1,
		0
	);
	if (MAP_FAILED == head) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	size_t size_stack = (pagesize << 1);
	void *stack = mmap(NULL,
			size_stack,
			PROT_READ | PROT_WRITE,
			MAP_STACK | MAP_ANONYMOUS | MAP_PRIVATE,
			-1,
			0
	);
	if (MAP_FAILED == stack) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	rc = mprotect(stack, pagesize, PROT_NONE);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	char *top_stack = ((char*) stack) + size_stack;

	running = 1;
	while (running) {

		if (request) {
			struct sockaddr_in client = {};
			socklen_t len = sizeof(struct sockaddr_in);
			rc = accept(fd, (struct sockaddr*) &client, &len);
			if (-1 == rc) {
				if ((EAGAIN != errno) && (EWOULDBLOCK != errno)) {
					fprintf(stderr, "%s\n", strerror(errno));
					freeaddrinfo(ai);
					_exit(1);
				}
				else {
					// NOTE: rc = EAGAIN or EWOULDBLOCK means no pending requests and so this is the right place to clear this one
					request = 0;
				}
			}
			else {
				int sockfd = rc;
				fprintf(
						stdout,
						"client: %s port: %d\n",
						inet_ntoa(client.sin_addr),
						ntohs(client.sin_port)
				       );

				// TODO: refactor header reading into a function
				// TODO: forward the task of reading the header to the child process
				HttpHeaderRead(head, sockfd);
				int sw = 0;
				void *data = &sockfd;

				// TODO: refactor task forwarding into a function
				do {
					errno = 0;
					pid_t pid = clone(
							HttpRespond,
							top_stack,
							CLONE_PTRACE | CLONE_FILES | SIGCHLD,
							data
							);
					if (-1 == pid) {
						if (EAGAIN != errno) {
							fprintf(stderr, "%s\n", strerror(errno));
							freeaddrinfo(ai);
							_exit(1);
						}
						else {
							fprintf(stdout, "%s\n", "WARNING: too many child processes trying again");
							errno = 0;
							rc = waitpid(-1, NULL, WNOHANG);
							if (-1 == rc) {
								if ((EINTR != errno) && (ECHILD != errno)) {
									fprintf(stderr, "%s\n", strerror(errno));
									freeaddrinfo(ai);
									_exit(1);
								}
								else {
									// we were interrupted or there are now no child processes so we should try to again
									sw = 1;
								}
							}
							else {
								sw = 1;
							}
						}
					}
					else {
						sw = 0;
					}
				} while (running && sw);
			}
		}
		else {
			// TODO: refactor child reaping into a function
			do {
				errno = 0;
				int wstatus = 0;
				rc = waitpid(-1, &wstatus, WNOHANG);
				if (-1 == rc) {
					if (ECHILD != errno) {
						fprintf(stderr, "%s\n", strerror(errno));
						freeaddrinfo(ai);
						_exit(1);
					}
					sleep(1);
				}
				else if (0 < rc) {
					pid_t pid = rc;
					if (WIFEXITED(wstatus)) {
						fprintf(
								stdout,
								"pid: %d status: %d\n",
								pid,
								WEXITSTATUS(wstatus)
						       );
					}
					else if (WIFSIGNALED(wstatus)) {
						fprintf(
								stdout,
								"pid: %d signal: %d\n",
								pid,
								WTERMSIG(wstatus)
						       );
					}
				}

			} while (running && !request);
		}
	}

	freeaddrinfo(ai);
	ai = NULL;
	_exit(0);
	return 0;
}
