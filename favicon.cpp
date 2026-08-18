/*

sonic-httpd

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#include "http.hpp"

#define HTTP_URI_FAVICON "/favicon"
#define HTTP_PATH_FAVICON (DIRBUILD "/public/favicons/favicon.png")

__httpd_extern
__httpd_internal
int FaviconGet(
        struct HttpRequest const * const DataRequest __attribute__((unused)),
        struct HttpResponse * const DataResponse
) {
        return HttpRespondGetFile(DataResponse, HTTP_PATH_FAVICON);
}

// NOTE: not going to include stddef.h just for NULL, we can use zero instead
struct HttpModule faviconModule = {
	.name = HTTP_URI_FAVICON,
	.Head = 0,
	.Get = FaviconGet,
	.Put = 0,
	.Post = 0,
	.Delete = 0
};
