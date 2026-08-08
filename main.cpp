/*

sonic-httpd

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#include <linux/limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT 8080

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
	fprintf(stdout, "host: %s port: %d\n", inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));

	errno = 0;
	int const fd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == fd) {
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

	struct sockaddr_in client = {};
	socklen_t len = sizeof(struct sockaddr_in);
	rc = accept(fd, (struct sockaddr*) &client, &len);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}
	int sockfd = rc;
	fprintf(stdout, "client: %s port: %d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

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
	void *head = mmap(NULL, size_mmap, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (MAP_FAILED == head) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	int sw = 0;
	ssize_t bytes_read = 0;
	ssize_t bytes_total = 0;
	size_t const chunk = 16;
	char *p = (typeof(p)) head;
	do {
		ret = read(sockfd, p, chunk);
		if (-1 == ret) {
			if (EINTR != errno) {
				fprintf(stderr, "%s\n", strerror(errno));
				freeaddrinfo(ai);
				_exit(1);
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

	fprintf(stdout, "%s\n", "request header:");
	fprintf(stdout, "%s", (char*) head);
	fprintf(stdout, "bytes: %ld\n", bytes_total);

	char response[] = (
		"HTTP/1.1 200\r\n"
		"\r\n"
	);

	errno = 0;
	ret = write(sockfd, response, sizeof(response));
	if (-1 == ret) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
	}

	ai = NULL;
	_exit(0);
	return 0;
}
