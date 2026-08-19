#ifndef __HTTP_H
#define __HTTP_H

/*

sonic-httpd

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#ifndef __cplusplus
#define __httpd_extern
#else
#define __httpd_extern extern "C"
#endif

#define __httpd_internal __attribute__((visibility("hidden")))
#define __httpd_visible  __attribute__((visibility("default")))

#define HTTP_SUCCESS_RC 0
#define HTTP_FAILURE_RC -1
#define HTTP_CONTENT_LENGTH_SIZE 256
#define HTTP_HEADER_SIZE (PATH_MAX)

__httpd_extern
struct HttpRequest;

__httpd_extern
struct HttpResponse;

typedef int (*HttpMethodFn)(
	struct HttpResponse * const response,
	struct HttpRequest const * const request
);

__httpd_extern
struct HttpModule;

__httpd_extern
struct DataModule {
	char const *name;
	void *handle;
	struct HttpModule *data;
};

__httpd_extern
struct HttpModule {
	char const *name;
	HttpMethodFn Head;
	HttpMethodFn Get;
	HttpMethodFn Put;
	HttpMethodFn Post;
	HttpMethodFn Delete;
};

__httpd_extern
__httpd_visible
int HttpRespondGetFile(
        struct HttpResponse * const DataResponse,
        char const * const filename
);

__httpd_extern
__httpd_visible
int HttpRespondHeadFile(
        struct HttpResponse * const DataResponse,
        char const * const filename
);

#endif
