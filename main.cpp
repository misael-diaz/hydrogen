/*

hydrogen

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
#include <dlfcn.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "http.hpp"

#define HTTP_LISTEN_PORT 8080
#define HTTP_SERVER_SCHEME "http"

#define HTTP_PATH_MODULES (DIRBUILD "/modules")

// NOTE: `__httpd_extern` is used to disable function name mangling when compiling with a C++ compiler

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
	HTTP_METHOD_LAST_SHF = HTTP_METHOD_UNKNOWN_SHF,
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
	struct DataModule *modules;
	char *origin;
	char *host;
	char *port;
	int sockfd;
	int modno;
};

__httpd_extern
struct HttpRequest {
	enum HttpMethod method;
	char const *URI;
	size_t size_header;
	size_t size_content;
	size_t size_total;
	char header[HTTP_HEADER_SIZE];
	char *content;
};

__httpd_extern
struct HttpResponse {
        size_t size_header;
        size_t size_content;
        size_t size_total;
        int64_t requires_cors;
        char header[HTTP_HEADER_SIZE];
        char *content;
};

__httpd_extern
__httpd_internal
int HttpRespondNotImpl(
	struct HttpResponse * const DataResponse,
	struct HttpRequest const * const DataRequest __attribute__((unused))
) {
	char CRLF[] = "\r\n";
	char stat[] = "501";
	size_t const len = sizeof(CRLF) - 1;
	ssize_t const sbytes = sizeof(CRLF);
	size_t const len_stat = (sizeof(stat) - 1);
	ssize_t avail = 0;
	int rc = 0;

	// FIXME: I know this is a quick hack that probably needs to go with a proper implementation, maybe starting to construct the response beforehand is starting to show its downside
	char *p = strstr(DataResponse->header, "200");
	if (!p) {
		fprintf(stderr, "HttpRespondNotImpl: %s\n", "missing initial response pus");
		goto error_handler;
	}

	memcpy(p, stat, len_stat);

	DataResponse->size_header = strlen(DataResponse->header);
	avail = (HTTP_HEADER_SIZE - DataResponse->size_header);
	if ((avail > 0) && (avail <= sbytes)) {
		fprintf(stderr, "HttpRespondHeadFile: %s\n", "surprising error avail header size");
		goto error_handler;
	}

	memcpy(DataResponse->header + DataResponse->size_header, CRLF, len);

	DataResponse->size_header += len;
	DataResponse->size_total = DataResponse->size_header;
	return HTTP_SUCCESS_RC;
error_handler:
	rc = HTTP_FAILURE_RC;
	return rc;
}

__httpd_extern
__httpd_visible
int HttpRespondHeadFile(
	struct HttpResponse * const DataResponse,
	char const * const filename __attribute__((unused))
) {
	char CRLF[] = "\r\n";
	size_t const len = sizeof(CRLF) - 1;
	ssize_t const sbytes = sizeof(CRLF);
	ssize_t avail = 0;
	int rc = 0;

	DataResponse->size_header = strlen(DataResponse->header);
	avail = (HTTP_HEADER_SIZE - DataResponse->size_header);
	if ((avail > 0) && (avail <= sbytes)) {
		fprintf(stderr, "HttpRespondHeadFile: %s\n", "surprising error avail header size");
		goto error_handler;
	}

	memcpy(DataResponse->header + DataResponse->size_header, CRLF, len);

	DataResponse->size_header += len;
	DataResponse->size_total = DataResponse->size_header;
	return HTTP_SUCCESS_RC;
error_handler:
	rc = HTTP_FAILURE_RC;
	return rc;
}

__httpd_extern
__httpd_visible
int HttpRespondGetFile(
	struct HttpResponse * const DataResponse,
	char const * const filename
) {
	int rc = 0;
	int fd = -1;
	ssize_t ret = -1;
	ssize_t avail = 0;
	size_t len_CORS = 0;
	size_t len_contentType = 0;
	size_t len_contentLength = 0;
	size_t bytes_CORS = 0;
	size_t bytes_written = 0;
	size_t bytes_contentType = 0;
	size_t bytes_contentData = 0;
	ssize_t sbytes_CORS = 0;
	ssize_t sbytes_contentType = 0;
	size_t bytes_file = 0;
	size_t size_mmap = 0;
	size_t pagesize = 0;
	size_t pagemask = 0;
	void *map = NULL;
	char *data = NULL;
	char access_control_allow_origin[] = "Access-Control-Allow-Origin: *\r\n";
	char content_length[HTTP_CONTENT_LENGTH_SIZE];
	char content_type[256];
	char imagepng[] = "Content-Type: image/png\r\n";
	char texthtml[] = "Content-Type: text/html\r\n";
	struct stat st = {};

	bytes_contentType = 0;
	memset(content_type, 0, sizeof(content_type));
	if (strstr(filename, ".ico")) {
		sbytes_contentType = sizeof(imagepng);
		bytes_contentType = sizeof(imagepng);
		len_contentType = bytes_contentType - 1;
		memcpy(content_type, imagepng, sizeof(imagepng));
	}
	else if (strstr(filename, ".png")) {
		sbytes_contentType = sizeof(imagepng);
		bytes_contentType = sizeof(imagepng);
		len_contentType = bytes_contentType - 1;
		memcpy(content_type, imagepng, sizeof(imagepng));
	}
	else if (strstr(filename, ".html")) {
		sbytes_contentType = sizeof(texthtml);
		bytes_contentType = sizeof(texthtml);
		len_contentType = bytes_contentType - 1;
		memcpy(content_type, texthtml, sizeof(texthtml));
	}
	else {
		fprintf(stderr, "HttpRespondGetFile: %s\n", "error unsupported file type");
		goto error_handler;
	}

	memset(content_length, 0, sizeof(content_length));

	errno = 0;
	ret = sysconf(_SC_PAGESIZE);
	if (-1 == ret) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", strerror(errno));
		}
	}
	pagesize = (typeof(pagesize)) ret;
	pagemask = (pagesize - 1);

	errno = 0;
	// NOTE: this is one of the things that I would like to do early on, this is related to having a list of files generated by the http-server on startup; the size can be stored along with the type of the content (as for example image/png or text/html)
	fd = open(filename, O_PATH | O_CLOEXEC, O_RDONLY);
	if (-1 == fd) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	errno = 0;
	// NOTE: here we are being optimistic in that nobody is going to modify the file right after we get its size in bytes for the response
	rc = fstat(fd, &st);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	// NOTE: we are checking the available space as if we were going to include the null byte even if we won't and the response header is zero initialized and so we can use strlen() to get its size reliably
	DataResponse->size_header = strlen(DataResponse->header);
	avail = (HTTP_HEADER_SIZE - DataResponse->size_header);
	if ((avail > 0) && (avail <= sbytes_contentType)) {
		fprintf(stderr, "HttpRespondGetFile: %s\n", "surprising error avail header size");
		goto error_handler;
	}

	memcpy(DataResponse->header + DataResponse->size_header, content_type, len_contentType);

	DataResponse->size_header += len_contentType;

	if (DataResponse->requires_cors) {
		avail = (HTTP_HEADER_SIZE - DataResponse->size_header);
		sbytes_CORS = bytes_CORS = sizeof(access_control_allow_origin);
		len_CORS = (bytes_CORS - 1);
		if ((avail > 0) && (avail <= sbytes_CORS)) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", "error avail header size for CORS");
			goto error_handler;
		}

		memcpy(DataResponse->header + DataResponse->size_header, access_control_allow_origin, len_CORS);

		DataResponse->size_header += len_CORS;
	}

	// NOTE: this is the end of the reponse header and this is why we append CRLF
	bytes_contentData = bytes_file = st.st_size;
	bytes_written = snprintf(
		content_length,
		HTTP_CONTENT_LENGTH_SIZE,
		"Content-Length: %lu\r\n"
		"\r\n",
		bytes_file
	);

	// NOTE: we must check bytes written by snprintf() to know if there was truncation
	if (HTTP_CONTENT_LENGTH_SIZE <= bytes_written) {
		fprintf(stderr, "HttpRespondGetFile: %s\n", "error truncation");
		goto error_handler;
	}

	len_contentLength = strlen(content_length);

	// NOTE: again this is the end of the response header and so we can now set the offset to the content (that is the data of the response which correspond to the bytes that belong to the file we are currently dealing with)
	memcpy(DataResponse->header + DataResponse->size_header, content_length, len_contentLength);
	DataResponse->size_header += len_contentLength;

	// NOTE: we need another file descriptor without O_PATH to mmap the contents (see `man open` and `man mmap`)
	close(fd);
	fd = -1;

	errno = 0;
	fd = open(filename, O_CLOEXEC, O_RDONLY);
	if (-1 == fd) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	size_mmap = ((bytes_file + pagemask) & (~pagemask));

	errno = 0;
	map = mmap(NULL, size_mmap, PROT_READ, MAP_PRIVATE, fd, 0);
	if (MAP_FAILED == map) {
		if (errno) {
			fprintf(stderr, "HttpRespondGetFile: %s\n", strerror(errno));
			goto error_handler;
		}
	}

	// NOTE: the memory mapping should have cleared the trailing bytes to zero and so if we try to access the data at the boundary we should get the null byte (zero)
	data = (typeof(data)) map;
	if (data[bytes_file]) {
		fprintf(stderr, "HttpRespondGetFile: %s\n", "error: file size mismatch detected");
		goto error_handler;
	}

	bytes_file = bytes_contentData;
	DataResponse->content = data;
	DataResponse->size_content = bytes_contentData;
	DataResponse->size_total = DataResponse->size_header + DataResponse->size_content;

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

__httpd_extern
__httpd_internal
int HttpSysRead(
	int const sockfd,
	char * const buffer,
	size_t const size_buffer,
	size_t * const bytes_proc
) {
	*bytes_proc = 0;
	int sw = 0;
	float const microsleep_time_float = 5e3;
	useconds_t const microsleep_time = microsleep_time_float;
	size_t tries = 0;
	size_t const max_tries = 32;
	size_t const sleep_after_tries = (max_tries >> 1);
	ssize_t ret = 0;
	ssize_t sbytes_remaining = 0;
	size_t bytes_remaining = 0;
	size_t bytes_read = 0;
	size_t bytes_total = 0;
	size_t const chunk = 16;
	size_t bytes = (chunk > size_buffer)? size_buffer : chunk;
	char *p = buffer;
	do {
		ret = read(sockfd, p, bytes);
		if (0 > ret) {
			// NOTE: we have a non-blocking socket and on linux we need to handle tcp/ip errors as EAGAIN, for more info see `man accept`
			if ((ENETUNREACH != errno) && (EOPNOTSUPP != errno) && (EHOSTUNREACH != errno) && (ENONET != errno) && (EHOSTDOWN != errno) && (ENOPROTOOPT != errno) && (EPROTO != errno) && (ENETDOWN != errno) && (EAGAIN != errno) && (EINTR != errno)) {
				*bytes_proc = bytes_total;
				goto error_handler;
			}
			else {
				if (max_tries > tries) {
					fprintf(stderr, "HttpHeaderRead: ignoring errno: %d error: %s\n", errno, strerror(errno));
					if (sleep_after_tries <= tries) {
						fprintf(stderr, "HttpHeaderRead: %s\n", "sleeping");
						usleep(microsleep_time);
					}
					++tries;
				}
				else {
					fprintf(stderr, "HttpHeaderRead: %s\n", "error maximum number of tries reached quitting");
					*bytes_proc = bytes_total;
					goto error_handler;
				}
			}
			sw = 1;
		}
		else {
			tries = 0;
			bytes_read = ret;
			bytes_total += bytes_read;
			sbytes_remaining = (size_buffer - bytes_total);
			if (0 > sbytes_remaining) {
				fprintf(stderr, "HttpSysRead: %s\n", "error impl");
				*bytes_proc = bytes_total;
				goto error_handler;
			}
			bytes_remaining = sbytes_remaining;
			bytes = (bytes_remaining > chunk)? chunk : bytes_remaining;
			p += bytes_read;
			if (!bytes_read) {
				sw = 0;
			}
			else if (chunk != bytes_read) {
				sw = 0;
			}
			else {
				sw = 1;
			}
		}
	} while (sw);

	*bytes_proc = bytes_total;
	return HTTP_SUCCESS_RC;
error_handler:
	fprintf(stderr, "%s\n", strerror(errno));
	return HTTP_FAILURE_RC;
}

// NOTE: consider microsleeps on consecutive EAGAIN errors if we encounter them later on (as it is done for HttpSysRead)
__httpd_extern
__httpd_internal
int HttpSysWrite(
	int const sockfd,
	char const * const buffer,
	size_t const size_buffer
) {
	int sw = 0;
	ssize_t ret = 0;
	ssize_t sbytes_remaining = 0;
	size_t bytes_written = 0;
	size_t bytes_remaining = 0;
	size_t bytes_total = 0;
	size_t const chunk = 16;
	size_t bytes_write = (size_buffer < chunk)? size_buffer : chunk;
	char *p = (typeof(p)) buffer;
	do {
		ret = write(sockfd, p, bytes_write);
		if (0 > ret) {
			// NOTE: we have a non-blocking socket and on linux we need to handle tcp/ip errors as EAGAIN, for more info see `man accept`
			if (
				(ECONNRESET != errno) &&
				(ENETUNREACH != errno) &&
				(EOPNOTSUPP != errno) &&
				(EHOSTUNREACH != errno) &&
				(ENONET != errno) &&
				(EHOSTDOWN != errno) &&
				(ENOPROTOOPT != errno) &&
				(EPROTO != errno) &&
				(ENETDOWN != errno) &&
				(EAGAIN != errno) &&
				(EINTR != errno)
			   ) {
				if (EPIPE == errno) {
					fprintf(stderr, "HttpSysWrite: warning: %s\n", "got error EPIPE: Broken Pipe");
				}
				goto error_handler;
			}
			else {
				fprintf(stderr, "HttpSysWrite: ignoring errno: %d error: %s\n", errno, strerror(errno));
			}
			sw = 1;
		}
		else {
			bytes_written = ret;
			bytes_total += bytes_written;
			sbytes_remaining = (size_buffer - bytes_total);
			if (0 > sbytes_remaining) {
				fprintf(stderr, "HttpSysWrite: %s\n", "error impl");
				goto error_handler;

			}
			bytes_remaining = sbytes_remaining;
			bytes_write = (chunk > bytes_remaining)? bytes_remaining : chunk;
			p += bytes_written;
			if (!bytes_written) {
				if (size_buffer != bytes_total) {
					fprintf(
						stderr,
						"HttpSysWrite: %s\n",
						"no bytes written but there is data "
						"pending in the buffer"
					);
					goto error_handler;
				}
				else {
					sw = 0;
				}
			}
			else {
				if (size_buffer == bytes_total) {
					sw = 0;
				}
				else {
					sw = 1;
				}
			}
		}
	} while (sw);

	return HTTP_SUCCESS_RC;
error_handler:
	if (errno) {
		fprintf(stderr, "HttpSysWrite: errno: %d error: %s\n", errno, strerror(errno));
	}
	else {
		fprintf(stderr, "HttpSysWrite: %s\n", "error_handler");
	}
	return HTTP_FAILURE_RC;
}

// TODO: this is fine for HTTP GET Request Headers but not adequate for POST because we are just reading bytes until there's no more or until we run out of space; bear in mind that POST requests include data that should be put elsewhere not as part of the header
__httpd_extern
__httpd_internal
int HttpHeaderRead(
	char * const head,
	int const sockfd
) {
	size_t bytes_proc = 0;
	int rc = HttpSysRead(sockfd, head, HTTP_HEADER_SIZE, &bytes_proc);
#if DEVBUILD
	fprintf(stdout, "%s\n", "request header:");
	fprintf(stdout, "%s", head);
	fprintf(stdout, "bytes: %ld\n", bytes_proc);
#endif
	return rc;
}

__httpd_extern
__httpd_internal
int HttpResponseWrite(
	int const sockfd,
	struct HttpResponse const * const DataResponse
) {
	int rc = HttpSysWrite(sockfd, DataResponse->header, DataResponse->size_header);
	if (HTTP_FAILURE_RC == rc) {
		return rc;
	}
	if (DataResponse->size_content) {
		rc = HttpSysWrite(sockfd, DataResponse->content, DataResponse->size_content);
	}
	return rc;
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

// TODO: probably you want to keep a global list of files, so instead of having the child process find the file the server could do that during startup, generate the list, and grant access to the children via some suitable data structure (not global access per se). For development this is great, server responds with updated files.

__httpd_extern
__httpd_internal
int HttpRouter(
	struct HttpResponse * const DataResponse,
	struct HttpRequest const * const DataRequest,
	struct ClientData const * const client
) {
	int rc = 0;
	char const * const URI = DataRequest->URI;
	enum HttpMethod const method = DataRequest->method;
	for (int i = 0; i != client->modno; ++i) {
		struct DataModule *module = &client->modules[i];
		if (!module->handle) {
			fprintf(
				stderr,
				"HttpRouter: missing handle of module %s\n",
				module->name
			);
			goto error_handler;
		}
		else if (!module->data) {
			fprintf(
				stderr,
				"HttpRouter: missing symbols of module %s\n",
				module->name
			);
			goto error_handler;
		}
		struct HttpModule *mod = module->data;
		if (strstr(URI, mod->name)) {
			if ((HTTP_METHOD_HEAD == method) && (mod->Head)) {
				fprintf(stdout, "HttpRouter: %s\n", "head method");
				rc = mod->Head(DataResponse, DataRequest);
				if (HTTP_FAILURE_RC == rc) {
					goto error_handler;
				}
			}
			else if ((HTTP_METHOD_GET == method) && (mod->Get)) {
				fprintf(stdout, "HttpRouter: %s\n", "get method");
				rc = mod->Get(DataResponse, DataRequest);
				if (HTTP_FAILURE_RC == rc) {
					goto error_handler;
				}
			}
			else {
				fprintf(stdout, "HttpRouter: %s\n", "not-implemented method");
				rc = HttpRespondNotImpl(DataResponse, DataRequest);
				if (HTTP_FAILURE_RC == rc) {
					goto error_handler;
				}
			}
			break;
		}
	}

	return HTTP_SUCCESS_RC;
error_handler:
	return HTTP_FAILURE_RC;
}

// TODO:
// [x] if Origin is in the request Header then the server must respond with `Access-Control-Allow-Origin: *` if that makes sense, otherwise what it is appropriate for the resource. However for images (which is just content) we can safely add that to the response header. Recommend reading (again):
//
// https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Access-Control-Allow-Origin
//
// Suggestion: check for Origin early on and enable access control if it's the favicon image for starters.
//
// [x] check early about the request header size, we bail out if we cannot process it
// [x] check if closing the socket on errors would make the client hang (note that we are not responding just closing the connection)
// [ ] RFC9112 https://www.rfc-editor.org/info/rfc9112/#name-message-body reject requests with both Content-Length and Transfer-Enconding and close the connection.
// [ ] A server MAY reject a request that contains a message body but not a Content-Length by responding with 411 (Length Required). https://www.rfc-editor.org/info/rfc9112/#section-6.3-5
// [ ] Host field is required in HTTP Requests to HTTP/1.1 servers; complain by responding with a status 400 "(Bad Request)" if the Host field is missing from the request header. https://www.rfc-editor.org/info/rfc9112/#section-3.2-4
// [ ] complain about whitespace in request-lines with a status 400 "(Bad Request)"; https://www.rfc-editor.org/info/rfc9112/#section-3.2-4
// [ ] we can complain about too long URIs and respond with a status 414 "(URI Too Long)"; https://www.rfc-editor.org/info/rfc9112/#section-3-4
// [x] support HTTP GET and HEAD methods is required, see linked MDN resource for more info (talks about that even though it's about HTTP 501 Status): https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status/501
// [ ] To avoid the TCP reset problem, servers typically close a connection in stages. First, the server performs a half-close by closing only the write side of the read/write connection. The server then continues to read from the connection until it receives a corresponding close by the client, or until the server is reasonably certain that its own TCP stack has received the client's acknowledgement of the packet(s) containing the server's last response. Finally, the server fully closes the connection. https://www.rfc-editor.org/info/rfc9112/#section-9.6-10 . This is something that I will need to think about in the future, right now we read and write from the same socket that we got via accept().
// [x] include Connection: close on the resonse because we have yet to implement persistent connections; https://www.rfc-editor.org/info/rfc9112/#name-persistence
__httpd_extern
__httpd_internal
int HttpRespond(void *data) {
	struct ClientData *client = (typeof(client)) data;
	int fd = client->sockfd;
	enum HttpMethod method = HTTP_METHOD_UNKNOWN;

	char const *URI = NULL;
	char head[HTTP_HEADER_SIZE];

	char CRLF[] = "\r\n";

	// TODO: we probably want to remove this to make up space for the stack
	char initial_response[HTTP_HEADER_SIZE] = (
		"HTTP/1.1 200 \r\n"
		"Connection: close\r\n"
	);

	char default_response[] = (
		"HTTP/1.1 200 \r\n"
		"Connection: close\r\n"
		"\r\n"
	);

	char error_response[] = (
		"HTTP/1.1 500 \r\n"
		"Connection: close\r\n"
		"\r\n"
	);

	struct HttpRequest DataRequest = {};
	struct HttpResponse DataResponse = {};
	memcpy(DataResponse.header, initial_response, sizeof(initial_response));

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

	strncat(DataResponse.header, timestamp, bytes_time);
	strncat(DataResponse.header, CRLF, sizeof(CRLF) - 1);

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

	// NOTE: probably not necessary because child is not the owner of the listening socket
	rc = sigaction(SIGURG, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	errno = 0;
	sa.sa_handler = SIG_IGN;
	// NOTE: if we don't ignore SIGPIPE we will get a SIGPIPE when the user agent or client closes the reading end of the socket
	rc = sigaction(SIGPIPE, &sa, NULL);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		goto error_handler;
	}

	// TODO: read into HttpRequest struct instead
	memset(head, 0, sizeof(head));
	rc = HttpHeaderRead(head, fd);
	if (HTTP_FAILURE_RC == rc) {
		fprintf(stderr, "%s\n", "HttpHeaderRead: read header failed");
		goto fatal_error_handler;
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

	// TODO: normally we would provide this data structure and set the field values there, not here by copying but this is a good first step
	DataRequest.method = method;
	DataRequest.URI = URI;
	memcpy(DataRequest.header, head, sizeof(head));

	if (strcasestr(head, client->origin)) {
		DataResponse.requires_cors = 1;
	}

	rc = HttpRouter(&DataResponse, &DataRequest, client);
	if (HTTP_FAILURE_RC == rc) {
		goto fatal_error_handler;
	}

	errno = 0;
	if (!DataResponse.size_total) {
		// NOTE: we are effectively excluding the terminating null byte from the response and note that this only works for pure text responses and this check will be revised in the future because it's insufficient
		rc = HttpSysWrite(fd, default_response, strlen(default_response));
		if (HTTP_FAILURE_RC == rc) {
			goto fatal_error_handler;
		}
	} else {
		rc = HttpResponseWrite(fd, &DataResponse);
		if (HTTP_FAILURE_RC == rc) {
			goto fatal_error_handler;
		}
	}

	close(fd);
	return HTTP_SUCCESS_RC;
error_handler:
	HttpSysWrite(fd, error_response, strlen(error_response));
	close(fd);
	return HTTP_FAILURE_RC;
fatal_error_handler:
	// NOTE: we are not able to write more data to the socket probably so it does not make sense to try to write to it but in the future we may want to log fatal errors specially and so it's a good idea to handle them specially
	fprintf(stderr, "HttpRespond: closing socket %d\n", fd);
	// NOTE: doing demystifies the return status that the parent process is going to report (our rc = -1 as an unsigned octet is just 255)
	fprintf(stderr, "HttpRespond: returning status (unsigned octet) %u\n", (rc & 255));
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
						sleep(1);
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
	int signum = 0;
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
			else if ((signum = WIFSIGNALED(wstatus))) {
				fprintf(
					stdout,
					"pid: %d signum: %d signal: %s\n",
					pid,
					signum,
					strsignal(signum)
				);
			}
		}

	} while (running && !request);

	return HTTP_SUCCESS_RC;
error_handler:
	return HTTP_FAILURE_RC;
}

__httpd_extern
__httpd_internal
int HttpIsModule(struct dirent const * const dent) {
	char const *name = dent->d_name;
	char *module = (typeof(module)) name;
	// NOTE: had to resort to type cast gymnastics so that GCC would accept this
	char *match = strstr(module, ".so");
	int rc = (!match)? 0 : 1;
	return rc;
}

int main() {
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

	// TODO: add code to free `modlist` elements and the list itself on errors
	struct dirent **modlist = NULL;
	rc = scandir(HTTP_PATH_MODULES, &modlist, HttpIsModule, alphasort);
	if (-1 == rc) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	int const modno = rc;
	if (!modno) {
		fprintf(stderr, "%s\n", "no modules found");
		_exit(1);
	}

	if (modno > 1) {
		for (int i = 1; i != modno; ++i) {
			if (!strcmp(modlist[i - 1]->d_name, modlist[i]->d_name)) {
				fprintf(stderr, "error duplicate module: %s\n", modlist[i - 1]->d_name);
				_exit(1);
			}
		}
	}

	fprintf(stdout, "found %d modules\n", modno);
	fprintf(stdout, "%s\n", "modules:");
	for (int i = 0; i != modno; ++i) {
		fprintf(stdout, "%s\n", modlist[i]->d_name);
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
	size_t const pagemask = (pagesize - 1);

	struct DataModule module = {};
	struct DataModule *modulp = &module;
	size_t const size_modules = (modno * sizeof(*modulp));
	size_t const size_modmap = (
		((size_modules + (pagemask << 1)) & (~pagemask))
	);
	size_t const offset_modname = size_modules;
	size_t const size_modname = pagemask;
	size_t const offset_modfile = size_modules + size_modname;
	size_t const size_modfile = pagemask;
	ssize_t const ssize_modname = pagemask;
	ssize_t const ssize_modfile = pagemask;

	errno = 0;
	void *vmodules = mmap(
			NULL,
			size_modmap,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			-1,
			0
	);
	if (MAP_FAILED == vmodules) {
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		freeaddrinfo(ai);
		_exit(1);
	}

	errno = 0;
	size_t const size_stack = (((HTTP_HEADER_SIZE) + (pagesize << 2)) << 1);
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

	char origin[256] = "Origin: " HTTP_SERVER_SCHEME "://";
	char *host = inet_ntoa(sin->sin_addr);
	char port[16];
	memset(port, 0, sizeof(port));

	size_t bytes_written = snprintf(port, sizeof(port), ":%d", HTTP_LISTEN_PORT);
	if (sizeof(port) <= bytes_written) {
		fprintf(stderr, "%s\n", "server: would overflow buffer port");
		freeaddrinfo(ai);
		_exit(1);
	}

	if (sizeof(origin) < (1 + strlen(origin) + strlen(host) + strlen(port))) {
		fprintf(stderr, "%s\n", "server: would overflow buffer origin");
		freeaddrinfo(ai);
		_exit(1);
	}

	strncat(origin, host, strlen(host));
	strncat(origin, port, strlen(port));

	void *handleHttpRespondHeadFile = dlsym(RTLD_DEFAULT, "HttpRespondHeadFile");
	if (!handleHttpRespondHeadFile) {
		fprintf(stderr, "%s\n", "error failed to load global symbol HttpRespondHeadFile");
		fprintf(stderr, "error: %s\n", dlerror());
		_exit(1);
	}

	void *handleHttpRespondGetFile = dlsym(RTLD_DEFAULT, "HttpRespondGetFile");
	if (!handleHttpRespondGetFile) {
		fprintf(stderr, "%s\n", "error failed to load global symbol HttpRespondGetFile");
		fprintf(stderr, "error: %s\n", dlerror());
		_exit(1);
	}

	// TODO: handle duplicate modules, do not load duplicates, just complain about it and refuse to start the server for security.
	struct DataModule *modules = (typeof(modules)) vmodules;
	for (int i = 0; i != modno; ++i) {
		struct DataModule *module = &modules[i];
		char *modname = (((char*) vmodules) + offset_modname);
		char *modfile = (((char*) vmodules) + offset_modfile);
		char *ext = strstr(modlist[i]->d_name, ".so");
		if (!ext) {
			fprintf(stderr, "error module missing .so file extension: %s\n", modlist[i]->d_name);
			_exit(1);
		}

		char *fullname = modlist[i]->d_name;
		// NOTE: on linux PATH_MAX defines the maximum number of bytes for files so we are safe to use this to define the shortened version of the module; moreover, d_name is 256 bytes so we are certain that this won't overflow the buffer
		char name[PATH_MAX];

		size_t const len = (ext - fullname);
		memset(name, 0, sizeof(name));
		strncat(name, modlist[i]->d_name, len);
		name[len] = 0;

		ssize_t const bytes_modname = snprintf(modname, size_modname, "%sModule", name);
		if (bytes_modname >= ssize_modname) {
			fprintf(stderr, "error truncated module: %s\n", modlist[i]->d_name);
			_exit(1);
		}

		ssize_t const bytes_modfile = snprintf(modfile, size_modfile, "%s/modules/%s", DIRBUILD, modlist[i]->d_name);
		if (bytes_modfile >= ssize_modfile) {
			fprintf(stderr, "error truncated module filename: %s\n", modlist[i]->d_name);
			_exit(1);
		}

		void *handle = dlopen(modfile, RTLD_NOW | RTLD_GLOBAL);
		if (!handle) {
			fprintf(stderr, "error failed to open module: %s\n", modfile);
			fprintf(stderr, "error: %s\n", dlerror());
			_exit(1);
		}

		void *data = dlsym(handle, modname);
		if (!data) {
			fprintf(stderr, "error symbol %s not found in module %s\n", modname, modfile);
			_exit(1);
		}
		module->name = name;
		module->handle = handle;
		module->data = (typeof(module->data)) data;
	}

	running = 1;
	while (running) {

		if (request) {
			struct sockaddr_in client = {};
			socklen_t len = sizeof(struct sockaddr_in);
			rc = accept4(fd, (struct sockaddr*) &client, &len, O_NONBLOCK | O_CLOEXEC);
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
				client.modules = modules;
				client.origin = origin;
				client.host = host;
				client.port = port;
				client.sockfd = sockfd;
				client.modno = modno;
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

	for (int i = 0; i != modno; ++i) {
		free(modlist[i]);
		modlist[i] = NULL;
	}

	for (int i = 0; i != modno; ++i) {
		struct DataModule *module = &modules[i];
		rc = dlclose(module->handle);
		if (rc) {
			// NOTE: the right thing is to report this but we should not panic and quit just keep trying to unload the other modules
			fprintf(stderr, "failed to close module: %s\n", module->name);
			fprintf(stderr, "error: %s\n", dlerror());
		}
		module->handle = NULL;
		// NOTE: nullify the module symbol address is the right way to clear it
		module->data = NULL;
	}

	// NOTE: memory maps will be recovered by the linux kernel automatically so we do not need to bother to unmap them
	free(modlist);
	freeaddrinfo(ai);
	modlist = NULL;
	ai = NULL;
	_exit(0);
	return 0;
}
