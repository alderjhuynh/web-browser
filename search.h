#pragma once
#include <stddef.h>
#include <stdbool.h>

char *search_build_url(const char *query);

char *search_url_encode(const char *s);

const char *search_auth_header_for_url(const char *url);

char *search_render_json(const char *url, const char *body, size_t len, size_t *out_len);
