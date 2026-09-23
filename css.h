#pragma once
#include <stddef.h>
#include "dom.h"

typedef struct {
    char **selectors;
    size_t sel_count;
    StyleProp *decls;
    size_t decl_count;
} CSSRule;

void css_rule_free(CSSRule *r);
char *css_strip_comments(const char *src);
CSSRule *parse_css(const char *css_text, size_t *out_count);
