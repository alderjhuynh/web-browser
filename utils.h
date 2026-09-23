#pragma once
#include <stddef.h>
#include <stdbool.h>

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
void *xcalloc(size_t n, size_t s);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *str_lower_dup(const char *s);
void str_trim_inplace(char *s);
bool streqi(const char *a, const char *b);

char *decode_entities(const char *src);
