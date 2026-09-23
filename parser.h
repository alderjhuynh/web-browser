#pragma once
#include "dom.h"
#include <stddef.h>

Node *parse_html(const char *src, size_t len, char **out_style, size_t *out_style_len);
