#pragma once
#include "dom.h"
#include "css.h"

void apply_styles(Node *root, CSSRule *rules, size_t n);
int node_font_size(Node *n);
int resolve_font_size(const char *s, int parent);
char *node_text_content(Node *n);
void font_cb(Node *n, int depth, void *ctx);

char *style_resolve_value(Node *node, const char *raw);

void style_resolve_all(Node *root);
