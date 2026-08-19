/*

hydrogen

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#include "http.hpp"

#define HTTP_URI_FAVICON "/favicon"
#define HTTP_PATH_FAVICON (DIRBUILD "/public/favicons/favicon.png")

__httpd_extern
__httpd_internal
int FaviconHead(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondHeadFile(DataResponse, HTTP_PATH_FAVICON);
}

__httpd_extern
__httpd_internal
int FaviconGet(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondGetFile(DataResponse, HTTP_PATH_FAVICON);
}

// NOTE: not going to include stddef.h just for NULL, we can use zero instead
struct HttpModule faviconModule = {
	.name = HTTP_URI_FAVICON,
	.Head = FaviconHead,
	.Get = FaviconGet,
	.Put = 0,
	.Post = 0,
	.Delete = 0
};
