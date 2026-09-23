#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum { NODE_DOCUMENT, NODE_ELEMENT, NODE_TEXT } NodeType;

typedef struct Attr {
    char *name;
    char *value;
} Attr;

typedef struct StyleProp {
    char *name;
    char *value;
} StyleProp;

typedef struct Node {
    NodeType type;
    char *tag;
    char *text;
    Attr *attrs;
    size_t attr_count, attr_cap;
    StyleProp *style;
    size_t style_count, style_cap;
    struct Node **children;
    size_t child_count, child_cap;
    struct Node *parent;
} Node;

Node *node_new(NodeType type, const char *tag);
Node *node_new_document(void);
Node *node_new_element(const char *tag);
Node *node_new_text(const char *txt);
void node_free(Node *n);
void node_add_child(Node *parent, Node *child);
void node_set_attr(Node *n, const char *name, const char *value);
const char *node_get_attr(const Node *n, const char *name);
bool node_has_class(const Node *n, const char *cls);
void node_style_set(Node *n, const char *prop, const char *val);
const char *node_style_get(const Node *n, const char *prop);

typedef void (*WalkCb)(Node *node, int depth, void *ctx);
void dom_walk(Node *root, WalkCb cb, void *ctx);
void dom_walk_iterative(Node *root, WalkCb cb, void *ctx);
Node *dom_find_tag(Node *root, const char *tag);
Node *dom_find_by_id(Node *root, const char *id);

bool selector_matches(Node *node, const char *selector);
void query_collect(Node *n, const char *sel, Node ***out, size_t *count, size_t *cap);

void dump_cb(Node *n, int depth, void *ctx);

bool is_void_tag(const char *tag);
bool is_skip_tag(const char *tag);
bool is_block_tag(const char *tag);
