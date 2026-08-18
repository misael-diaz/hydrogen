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

__httpd_extern
struct HttpRequest;

__httpd_extern
struct HttpResponse;

typedef int (*HttpMethodFn)(
	struct HttpRequest const * const request,
	struct HttpResponse * const response
);

__httpd_extern
struct DataModule {
	char const *name;
	void *handle;
	void *data;
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
int HttpRespondGetFile(
        struct HttpResponse * const DataResponse,
        char const * const filename
);

#endif
