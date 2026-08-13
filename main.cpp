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
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HTTP_SUCCESS_RC 0
#define HTTP_FAILURE_RC -1
#define HTTP_LISTEN_PORT 8080
#define HTTP_HEADER_SIZE (PATH_MAX)
#define HTTP_CONTENT_LENGTH_SIZE 256
#define HTTP_FAVICON_PATH "./public/favicons/favicon.png"

// NOTE: this is how we disable function name mangling when compiling with a C++ compiler
#ifndef __cplusplus
#define __httpd_extern
#else
#define __httpd_extern extern "C"
#endif

#define __httpd_internal __attribute__((visibility("hidden")))

// NOTE: do not attemp to hotload if adding a new HTTP Method
__httpd_extern
enum _HttpMethodShifts {
	HTTP_METHOD_HEAD_SHF = 0,
	HTTP_METHOD_GET_SHF,
	HTTP_METHOD_PUT_SHF,
	HTTP_METHOD_POST_SHF,
	HTTP_METHOD_DELETE_SHF,
	// NOTE: Do **Not** add http methods after UNKNOWN so that we never miss the count
	HTTP_METHOD_UNKNOWN_SHF,
	HTTP_METHOD_LAST_SHF = HTTP_METHOD_DELETE_SHF,
	HTTP_METHOD_COUNT = HTTP_METHOD_LAST_SHF,
	HTTP_METHOD_NUM = HTTP_METHOD_COUNT,
	HTTP_METHOD_MAX = HTTP_METHOD_COUNT
};

__httpd_extern
enum HttpMethod {
	HTTP_METHOD_HEAD = (1 << HTTP_METHOD_HEAD_SHF),
	HTTP_METHOD_GET = (1 << HTTP_METHOD_GET_SHF),
	HTTP_METHOD_PUT = (1 << HTTP_METHOD_PUT_SHF),
	HTTP_METHOD_POST = (1 << HTTP_METHOD_POST_SHF),
	HTTP_METHOD_DELETE = (1 << HTTP_METHOD_DELETE_SHF),
	HTTP_METHOD_UNKNOWN = (1 << HTTP_METHOD_UNKNOWN_SHF)
};

__httpd_extern
struct ClientData {
	int sockfd;
	char _pad[4];
};

#ifndef __cplusplus
_Static_assert(8 == sizeof(struct ClientData));
#else
static_assert(8 == sizeof(struct ClientData));
#endif

__httpd_extern
__httpd_internal
int HttpHeaderRead(
	char * const head,
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

// NOTE: this is a very optimistic way of handling this and hence it needs improvement
__httpd_extern
__httpd_internal
int HttpHeaderFindMethod(
	enum HttpMethod * const method,
	char const * const head,
	char const ** const URI
) {
	int rc = HTTP_SUCCESS_RC;
	char const * str = NULL;
	char const HEAD[] = "HEAD";
	char const GET[] = "GET";
	char const PUT[] = "PUT";
	char const POST[] = "POST";
	char const DELETE[] = "DELETE";
	if ((str = strstr(head, HEAD))) {
		rc = HTTP_SUCCESS_RC;
		str += sizeof(HEAD);
		*URI = str;
		*method = HTTP_METHOD_HEAD;
	}
	else if ((str = strstr(head, GET))) {
		rc = HTTP_SUCCESS_RC;
		str += sizeof(GET);
		*URI = str;
		*method = HTTP_METHOD_GET;
	}
	else if ((str = strstr(head, PUT))) {
		rc = HTTP_SUCCESS_RC;
		str += sizeof(PUT);
		*URI = str;
		*method = HTTP_METHOD_PUT;
	}
	else if ((str = strstr(head, POST))) {
		rc = HTTP_SUCCESS_RC;
		str += sizeof(POST);
		*URI = str;
		*method = HTTP_METHOD_POST;
	}
	else if ((str = strstr(head, DELETE))) {
		rc = HTTP_SUCCESS_RC;
		str += sizeof(DELETE);
		*URI = str;
		*method = HTTP_METHOD_DELETE;
	}
	else {
		*URI = NULL;
		rc = HTTP_FAILURE_RC;
		*method = HTTP_METHOD_UNKNOWN;
	}
	return rc;
}

// NOTE: experimental code, this is so that we can respond with a favicon but this is not how I intend to handle responses
// NOTE: this assumes that there are no more response header fields to include after this call
// TODO: provide the response buffer size so that we know if there's enough space for the header
// TODO: the data structure for the http response is starting to arise, we need an offset for the data and a size and a size for the header-section of the response
// TODO: probably you want to keep a global list of files, so instead of having the child process find the file the server could do that during startup, generate the list, and grant access to the children via some suitable data structure (not global access per se)
__httpd_extern
__httpd_internal
int HttpRespondGetFavicon(
	char * const response,
	size_t * const bytes_response
) {
	int rc = 0;
	int fd = -1;
	ssize_t ret = -1;
	size_t len = 0;
	size_t len_written = 0;
	size_t bytes_written = 0;
	size_t bytes_favicon = 0;
	size_t len_response = 0;
	size_t avail_response = 0;
	size_t size_response = 0;
	size_t size_mmap = 0;
	size_t pagesize = 0;
	size_t pagemask = 0;
	void *map = NULL;
	char *img = NULL;
	char *content = NULL;
	char content_length[HTTP_CONTENT_LENGTH_SIZE];
	struct stat st = {};

	memset(content_length, 0, sizeof(content_length));

	errno = 0;
	ret = sysconf(_SC_PAGESIZE);
	if (-1 == ret) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFavicon: %s\n", strerror(errno));
		}
	}
	pagesize = (typeof(pagesize)) ret;
	pagemask = (pagesize - 1);

	errno = 0;
	fd = open(HTTP_FAVICON_PATH, O_PATH | O_CLOEXEC, O_RDONLY);
	if (-1 == fd) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFavicon: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	errno = 0;
	// NOTE: here we are being optimistic in that nobody is going to modify the favicon right after we get its size in bytes for the response
	rc = fstat(fd, &st);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFavicon: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	bytes_favicon = st.st_size;
	bytes_written = snprintf(
		content_length,
		HTTP_CONTENT_LENGTH_SIZE,
		"Content-Type: image/png\r\n"
		"Content-Length: %lu\r\n"
		"\r\n",
		bytes_favicon
	);

	// NOTE: we need another file descriptor without O_PATH to mmap the contents (see `man open` and `man mmap`)
	close(fd);
	fd = -1;

	errno = 0;
	fd = open(HTTP_FAVICON_PATH, O_CLOEXEC, O_RDONLY);
	if (-1 == fd) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFavicon: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	if (HTTP_CONTENT_LENGTH_SIZE <= bytes_written) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error truncation");
		goto error_handler;
	}

	len_written = strlen(content_length);
	if (len_written != bytes_written) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error size of content-length buffer");
		goto error_handler;
	}

	len_response = strlen(response);
	size_response = 1 + len_response;
	if (HTTP_HEADER_SIZE < size_response) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error header size");
		goto error_handler;
	}

	avail_response = (HTTP_HEADER_SIZE - size_response);

	if (avail_response < bytes_written) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error avail header size");
		goto error_handler;
	}

	memcpy(response + len_response, content_length, bytes_written);
	// NOTE: we include the null byte because we intend to keep it at the end of the response for the caller because it expects it and we use `len_response` to know where the image data begins
	size_response += bytes_written;
	len_response = size_response - 1;

	// NOTE: as above here we add the content and so we must be sure to have enough room for the content
	size_mmap = ((bytes_favicon + pagemask) & (~pagemask));

	errno = 0;
	map = mmap(NULL, size_mmap, PROT_READ, MAP_PRIVATE, fd, 0);
	if (MAP_FAILED == map) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFavicon: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	// NOTE: the memory mapping should have cleared the trailing bytes to zero and so if we try to access the data at the boundary we should get the null byte (zero)
	img = (typeof(img)) map;
	if (img[bytes_favicon]) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error: image size mismatch detected");
		goto error_handler;
	}

	if (HTTP_HEADER_SIZE < size_response) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error content size");
		goto error_handler;
	}

	// NOTE: size response includes the null byte that the caller expects at the end
	avail_response = (HTTP_HEADER_SIZE - size_response);

	if (avail_response < bytes_favicon) {
		fprintf(stderr, "HttpRespondGetFavicon: %s\n", "error avail content size");
		goto error_handler;
	}

	// NOTE: we intend to overwrite the null character that strncat appended and so we expect no changes to `len_response` between calling `strncat` and `memcpy` and we append a null byte for the caller because it expects it
	content = response + len_response;
	memcpy(content, img, bytes_favicon);
	len = bytes_favicon;
	content[len] = 0;

	len = len_response + bytes_favicon;
	*bytes_response = len;
	rc = HTTP_SUCCESS_RC;
	return rc;
error_handler:
	if (0 < fd) {
		close(fd);
		fd = -1;
	}
	rc = HTTP_FAILURE_RC;
	return rc;
}

// TODO:
// [ ] check if closing the socket on errors would make the client hang (note that we are not responding just closing the connection)
// [ ] RFC9112 https://www.rfc-editor.org/info/rfc9112/#name-message-body reject requests with both Content-Length and Transfer-Enconding and close the connection.
// [ ] A server MAY reject a request that contains a message body but not a Content-Length by responding with 411 (Length Required). https://www.rfc-editor.org/info/rfc9112/#section-6.3-5
// [ ] Host field is required in HTTP Requests to HTTP/1.1 servers; complain by responding with a status 400 "(Bad Request)" if the Host field is missing from the request header. https://www.rfc-editor.org/info/rfc9112/#section-3.2-4
// [ ] complain about whitespace in request-lines with a status 400 "(Bad Request)"; https://www.rfc-editor.org/info/rfc9112/#section-3.2-4
// [ ] we can complain about too long URIs and respond with a status 414 "(URI Too Long)"; https://www.rfc-editor.org/info/rfc9112/#section-3-4
// [ ] support HTTP GET and HEAD methods is required, see linked MDN resource for more info (talks about that even though it's about HTTP 501 Status): https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status/501
// [ ] To avoid the TCP reset problem, servers typically close a connection in stages. First, the server performs a half-close by closing only the write side of the read/write connection. The server then continues to read from the connection until it receives a corresponding close by the client, or until the server is reasonably certain that its own TCP stack has received the client's acknowledgement of the packet(s) containing the server's last response. Finally, the server fully closes the connection. https://www.rfc-editor.org/info/rfc9112/#section-9.6-10 . This is something that I will need to think about in the future, right now we read and write from the same socket that we got via accept().
// [x] include Connection: close on the resonse because we have yet to implement persistent connections; https://www.rfc-editor.org/info/rfc9112/#name-persistence
__httpd_extern
__httpd_internal
int HttpRespond(void *data) {
	struct ClientData *client = (typeof(client)) data;
	int fd = client->sockfd;
	enum HttpMethod method = HTTP_METHOD_UNKNOWN;
	size_t bytes_response = 0;

	char const *URI = NULL;
	char head[HTTP_HEADER_SIZE];

	char CRLF[] = "\r\n";

	// TODO: we probably want to clear define this later, so we should only allocate
	char response[HTTP_HEADER_SIZE] = (
		"HTTP/1.1 200 \r\n"
		"Connection: close\r\n"
	);

	errno = 0;
	// NOTE: sets the timezone to GMT for the response according to RFC9110
	int rc = setenv("TZ", "UTC+0:00:00", 1);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		close(fd);
		return HTTP_FAILURE_RC;
	}

	errno = 0;
	rc = time(NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		close(fd);
		return HTTP_FAILURE_RC;
	}
	time_t t = rc;

	errno = 0;
	struct tm tm = {};
	struct tm *tp = localtime_r(&t, &tm);
	if (!tp) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		close(fd);
		return HTTP_FAILURE_RC;
	}

	char format_timestamp[] = "Date: %a, %d %b %Y %I %H:%M:%S GMT";
	char timestamp[256];

	// NOTE: returns bytes but excludes the terminating null byte and in this case we know that if this function returns zero bytes that we cannot use the timestamp; at this point in development we simply close the connection
	size_t bytes_time = strftime(timestamp, sizeof(timestamp), format_timestamp, &tm);
	if (!bytes_time) {
		close(fd);
		return HTTP_FAILURE_RC;
	}

	strncat(response, timestamp, bytes_time);
	strncat(response, CRLF, sizeof(CRLF) - 1);

	ssize_t bytes_written = 0;

	// NOTE: the child process inherits the signal table from the parent so we need to set SIGINT to its default action (does not affect the parent process (i.e. the http-server)
	struct sigaction sa = {};
	sa.sa_handler = SIG_DFL;

	errno = 0;
	rc = sigemptyset(&sa.sa_mask);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}
	sa.sa_flags = 0;

	errno = 0;
	rc = sigaction(SIGINT, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	errno = 0;
	// NOTE: probably not necessary because child is not the owner of the listening socket
	rc = sigaction(SIGIO, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	rc = sigaction(SIGURG, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	errno = 0;
	// NOTE: probably not necessary because child is not the owner of the listening socket
	rc = sigaction(SIGURG, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	memset(head, 0, sizeof(head));
	rc = HttpHeaderRead(head, fd);
	// FIXME: check againt HTTP_FAILURE_RC instead
	if (-1 == rc) {
		fprintf(stderr, "%s\n", "HttpHeaderRead: read header failed");
		goto error_handler;
	}

	rc = HttpHeaderFindMethod(&method, head, &URI);
	if (HTTP_FAILURE_RC == rc) {
		fprintf(stderr, "%s\n", "HttpHeaderFindMethod: unexpected failure");
		goto error_handler;
	}
	else if (HTTP_METHOD_UNKNOWN == method) {
		fprintf(stderr, "%s\n", "HttpHeaderFindMethod: error unknown method detected");
		goto error_handler;
	}
	else if (!URI) {
		fprintf(stderr, "%s\n", "HttpHeaderFindMethod: uninitialized URI");
		goto error_handler;
	}

	// TODO: refactor this into the router function
	bytes_response = 0;
	if (HTTP_METHOD_GET == method) {
		if (strstr(URI, "favicon")) {
			rc = HttpRespondGetFavicon(response, &bytes_response);
			if (HTTP_FAILURE_RC == rc) {
				goto error_handler;
			}
		}
		else {
			// NOTE: generates the original default response
			char content_length[] = (
				"Content-Length: 0\r\n"
				"\r\n"
			);
			strncat(response, content_length, sizeof(content_length) - 1);
		}
	}

	// NOTE: we are effectively excluding the terminating null byte from the response and note that this only works for pure text responses and this check will be revised in the future because it's insufficient
	errno = 0;
	if (!bytes_response) {
		bytes_written = write(fd, response, strlen(response));
	} else {
		bytes_written = write(fd, response, bytes_response);
	}
	if (-1 == bytes_written) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	close(fd);
	return HTTP_SUCCESS_RC;
error_handler:
	close(fd);
	return HTTP_FAILURE_RC;
}

static int request = 0;
static int running = 0;

__httpd_extern
__httpd_internal
void HttpSignalHandler(int signum) 
{
	if (SIGINT == signum) {
#if DEVBUILD
		fprintf(
			stdout,
			"\n\n%s\n\n",
			"HttpSignalHandler: "
			"received SIGINT terminating execution normally"
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
			"HttpSignalHandler: "
			"received SIGIO due to request on listening socket"
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
int HttpResponseScheduler(
		void *top_stack,
		void *data
) {
	int rc = 0;
	int sw = 0;
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
				goto error_handler;
			}
			else {
				fprintf(
					stderr,
					"%s\n",
					"HttpResponseScheduler: "
					"WARNING: "
					"too many child processes trying again"
				);
				errno = 0;
				rc = waitpid(-1, NULL, WNOHANG);
				if (-1 == rc) {
					if ((EINTR != errno) && (ECHILD != errno)) {
						fprintf(stderr, "%s\n", strerror(errno));
						goto error_handler;
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

	return HTTP_SUCCESS_RC;
error_handler:
	return HTTP_FAILURE_RC;
}

__httpd_extern
__httpd_internal
int HttpDowntimeProcessReaper(void) {
	int rc = 0;
	do {
		errno = 0;
		int wstatus = 0;
		rc = waitpid(-1, &wstatus, WNOHANG);
		if (-1 == rc) {
			if (ECHILD != errno) {
				fprintf(stderr, "%s\n", strerror(errno));
				goto error_handler;
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

	return HTTP_SUCCESS_RC;
error_handler:
	return HTTP_FAILURE_RC;
}

int main () {
	errno = 0;
	char hostname[PATH_MAX];
	// NOTE: it should not be surprising that we get 127.0.1.1 if we supply the hostname this way getaddrinfo() because that's the expected result; you may want to try res_nquery() see `man resolver`
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
	sin->sin_port = htons(HTTP_LISTEN_PORT);
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

	errno = 0;
	size_t const size_stack = (((HTTP_HEADER_SIZE) + (pagesize << 1)) << 1);
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
			// TODO: tcp/ip error handling pending see "Error handling" section of `man accept` for more info
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

				struct ClientData client = {};
				client.sockfd = sockfd;
				rc = HttpResponseScheduler(top_stack, &client);
				if (HTTP_FAILURE_RC == rc) {
					freeaddrinfo(ai);
					_exit(1);
				}
			}
		}
		else {
			rc = HttpDowntimeProcessReaper();
			if (HTTP_FAILURE_RC == rc) {
				freeaddrinfo(ai);
				_exit(1);
			}
		}
	}

	freeaddrinfo(ai);
	ai = NULL;
	_exit(0);
	return 0;
}
