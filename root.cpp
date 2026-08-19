/*

hydrogen

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#include "http.hpp"

#define HTTP_URI_ROOT "/"
#define HTTP_PATH_ROOT (DIRBUILD "/http/index.html")

__httpd_extern
__httpd_internal
int RootHead(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondHeadFile(DataResponse, HTTP_PATH_ROOT);
}

__httpd_extern
__httpd_internal
int RootGet(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondGetFile(DataResponse, HTTP_PATH_ROOT);
}

// NOTE: not going to include stddef.h just for NULL, we can use zero instead
struct HttpModule rootModule = {
	.name = HTTP_URI_ROOT,
	.Head = RootHead,
	.Get = RootGet,
	.Put = 0,
	.Post = 0,
	.Delete = 0
};
