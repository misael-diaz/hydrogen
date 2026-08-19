/*

hydrogen

Copyright (C) 2026 Misael Díaz-Maldonado

This file is released under the GNU General Public License version 2 only
as published by the Free Software Foundation.

*/

#include "http.hpp"

#define HTTP_URI_HERO "/hero"
#define HTTP_PATH_HERO (DIRBUILD "/public/hero/hero.png")

__httpd_extern
__httpd_internal
int HeroHead(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondHeadFile(DataResponse, HTTP_PATH_HERO);
}

__httpd_extern
__httpd_internal
int HeroGet(
        struct HttpResponse * const DataResponse,
        struct HttpRequest const * const DataRequest __attribute__((unused))
) {
        return HttpRespondGetFile(DataResponse, HTTP_PATH_HERO);
}

// NOTE: not going to include stddef.h just for NULL, we can use zero instead
struct HttpModule heroModule = {
	.name = HTTP_URI_HERO,
	.Head = HeroHead,
	.Get = HeroGet,
	.Put = 0,
	.Post = 0,
	.Delete = 0
};
