#pragma once
#include <stddef.h>
#include "dom.h"

char *fetch_url(const char *url, size_t *out_len);
char *read_file(const char *path, size_t *out_len);

char *load_linked_stylesheets(Node *root, const char *base_path_or_url, size_t *out_len);
