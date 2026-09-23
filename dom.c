#define _POSIX_C_SOURCE 200809L
#include "dom.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

Node *node_new(NodeType type, const char *tag) {
    Node *n = (Node*)xcalloc(1, sizeof(Node));
    n->type = type;
    n->tag = tag ? str_lower_dup(tag) : xstrdup("#text");
    return n;
}
Node *node_new_document(void) { return node_new(NODE_DOCUMENT, "document"); }
Node *node_new_element(const char *tag) { return node_new(NODE_ELEMENT, tag); }
Node *node_new_text(const char *txt) {
    Node *n = node_new(NODE_TEXT, "#text");
    n->text = xstrdup(txt);
    return n;
}
void node_free(Node *n) {
    if (!n) return;
    for (size_t i=0;i<n->child_count;i++) node_free(n->children[i]);
    free(n->children);
    for (size_t i=0;i<n->attr_count;i++) { free(n->attrs[i].name); free(n->attrs[i].value); }
    free(n->attrs);
    for (size_t i=0;i<n->style_count;i++) { free(n->style[i].name); free(n->style[i].value); }
    free(n->style);
    free(n->tag);
    free(n->text);
    free(n);
}
void node_add_child(Node *parent, Node *child) {
    if (!parent || !child) return;
    if (parent->child_count == parent->child_cap) {
        size_t nc = parent->child_cap ? parent->child_cap*2 : 4;
        parent->children = (Node**)xrealloc(parent->children, nc*sizeof(Node*));
        parent->child_cap = nc;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}
void node_set_attr(Node *n, const char *name, const char *value) {
    char *lname = str_lower_dup(name);
    for (size_t i=0;i<n->attr_count;i++) {
        if (strcmp(n->attrs[i].name, lname)==0) {
            free(n->attrs[i].value);
            n->attrs[i].value = xstrdup(value ? value : "");
            free(lname);
            return;
        }
    }
    if (n->attr_count == n->attr_cap) {
        size_t nc = n->attr_cap ? n->attr_cap*2 : 4;
        n->attrs = (Attr*)xrealloc(n->attrs, nc*sizeof(Attr));
        n->attr_cap = nc;
    }
    n->attrs[n->attr_count].name = lname;
    n->attrs[n->attr_count].value = xstrdup(value ? value : "");
    n->attr_count++;
}
const char *node_get_attr(const Node *n, const char *name) {
    if (!n || !name) return NULL;
    for (size_t i=0;i<n->attr_count;i++) {
        if (strcasecmp(n->attrs[i].name, name)==0) return n->attrs[i].value;
    }
    return NULL;
}
bool node_has_class(const Node *n, const char *cls) {
    const char *c = node_get_attr(n, "class");
    if (!c) return false;
    const char *p = c;
    size_t clen = strlen(cls);
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);
        if (len==clen && strncmp(start, cls, len)==0) return true;
    }
    return false;
}
void node_style_set(Node *n, const char *prop, const char *val) {
    char *lp = str_lower_dup(prop);
    for (size_t i=0;i<n->style_count;i++) {
        if (strcmp(n->style[i].name, lp)==0) {
            free(n->style[i].value);
            n->style[i].value = xstrdup(val);
            free(lp);
            return;
        }
    }
    if (n->style_count == n->style_cap) {
        size_t nc = n->style_cap ? n->style_cap*2 : 4;
        n->style = (StyleProp*)xrealloc(n->style, nc*sizeof(StyleProp));
        n->style_cap = nc;
    }
    n->style[n->style_count].name = lp;
    n->style[n->style_count].value = xstrdup(val);
    n->style_count++;
}
const char *node_style_get(const Node *n, const char *prop) {
    if (!n || !prop) return NULL;
    for (size_t i=0;i<n->style_count;i++) if (strcasecmp(n->style[i].name, prop)==0) return n->style[i].value;
    return NULL;
}

static void dom_walk_recursive(Node *n, int depth, WalkCb cb, void *ctx) {
    if (!n) return;
    cb(n, depth, ctx);
    for (size_t i=0;i<n->child_count;i++) dom_walk_recursive(n->children[i], depth+1, cb, ctx);
}
void dom_walk(Node *root, WalkCb cb, void *ctx) {
    dom_walk_recursive(root, 0, cb, ctx);
}
void dom_walk_iterative(Node *root, WalkCb cb, void *ctx) {
    if (!root) return;
    typedef struct { Node *n; int depth; size_t child_idx; } Frame;
    size_t cap = 64;
    Frame *stack = (Frame*)xmalloc(cap*sizeof(Frame));
    size_t sp = 0;
    stack[sp++] = (Frame){root, 0, 0};
    cb(root, 0, ctx);
    while (sp) {
        Frame *top = &stack[sp-1];
        if (top->child_idx < top->n->child_count) {
            Node *child = top->n->children[top->child_idx++];
            cb(child, top->depth+1, ctx);
            if (sp >= cap) { cap*=2; stack=(Frame*)xrealloc(stack, cap*sizeof(Frame)); }
            stack[sp++] = (Frame){child, top->depth+1, 0};
        } else {
            sp--;
        }
    }
    free(stack);
}
static Node *dom_find_tag_recursive(Node *n, const char *tag) {
    if (!n) return NULL;
    if (n->type==NODE_ELEMENT && streqi(n->tag, tag)) return n;
    for (size_t i=0;i<n->child_count;i++) {
        Node *r = dom_find_tag_recursive(n->children[i], tag);
        if (r) return r;
    }
    return NULL;
}
Node *dom_find_tag(Node *root, const char *tag) {
    return dom_find_tag_recursive(root, tag);
}
Node *dom_find_by_id(Node *root, const char *id) {
    if (!root || !id) return NULL;
    if (root->type==NODE_ELEMENT) {
        const char *v = node_get_attr(root, "id");
        if (v && strcmp(v, id)==0) return root;
    }
    for (size_t i=0;i<root->child_count;i++) {
        Node *r = dom_find_by_id(root->children[i], id);
        if (r) return r;
    }
    return NULL;
}

typedef struct {
    char *tag;
    char *id;
    char **classes;
    size_t class_count;
} SimpleSel;

static void simple_sel_free(SimpleSel *s) {
    free(s->tag); free(s->id);
    for (size_t i=0;i<s->class_count;i++) free(s->classes[i]);
    free(s->classes);
    memset(s,0,sizeof(*s));
}
static bool parse_simple_token(const char *token, SimpleSel *out) {
    memset(out,0,sizeof(*out));
    size_t i=0, n=strlen(token);
    size_t tag_end=0;
    while (tag_end<n && token[tag_end]!='#' && token[tag_end]!='.') tag_end++;
    if (tag_end>0) {
        out->tag = xstrndup(token, tag_end);
        for (char *c=out->tag; *c; c++) *c=(char)tolower((unsigned char)*c);
        if (strcmp(out->tag, "*")==0) { free(out->tag); out->tag=NULL; }
    } else {
        out->tag = NULL;
    }
    i = tag_end;
    while (i<n) {
        char delim = token[i];
        i++;
        size_t start=i;
        while (i<n && token[i]!='#' && token[i]!='.') i++;
        size_t len = i-start;
        if (len==0) continue;
        char *val = xstrndup(token+start, len);
        if (delim=='#') {
            free(out->id);
            out->id = val;
        } else if (delim=='.') {
            out->classes = (char**)xrealloc(out->classes, (out->class_count+1)*sizeof(char*));
            out->classes[out->class_count++] = val;
        } else {
            free(val);
        }
    }
    return true;
}
static bool node_matches_simple(const Node *node, const SimpleSel *sel) {
    if (node->type != NODE_ELEMENT) return false;
    if (sel->tag && !streqi(node->tag, sel->tag)) return false;
    if (sel->id) {
        const char *id = node_get_attr(node, "id");
        if (!id || strcmp(id, sel->id)!=0) return false;
    }
    for (size_t i=0;i<sel->class_count;i++) {
        if (!node_has_class(node, sel->classes[i])) return false;
    }
    return true;
}
bool selector_matches(Node *node, const char *selector) {
    char *copy = xstrdup(selector);
    char *src = copy;
    bool in_space=true;
    char *write=copy;
    while (*src && isspace((unsigned char)*src)) src++;
    while (*src) {
        if (isspace((unsigned char)*src)) {
            if (!in_space) { *write++=' '; in_space=true; }
        } else { *write++=*src; in_space=false; }
        src++;
    }
    if (write>copy && *(write-1)==' ') write--;
    *write='\0';
    char *tokens[64];
    int tcnt=0;
    char *tok = copy;
    while (*tok) {
        char *end = strchr(tok, ' ');
        if (end) *end='\0';
        char *colon = strchr(tok, ':');
        if (colon) *colon='\0';
        if (*tok && tcnt<64) tokens[tcnt++]=tok;
        if (!end) break;
        tok = end+1;
    }
    if (tcnt==0) { free(copy); return false; }
    Node *cur = node;
    int ti = tcnt-1;
    SimpleSel sel;
    parse_simple_token(tokens[ti], &sel);
    bool ok = node_matches_simple(cur, &sel);
    simple_sel_free(&sel);
    if (!ok) { free(copy); return false; }
    ti--;
    cur = cur->parent;
    while (ti>=0) {
        bool found = false;
        while (cur) {
            parse_simple_token(tokens[ti], &sel);
            bool m = node_matches_simple(cur, &sel);
            simple_sel_free(&sel);
            if (m) { found=true; cur = cur->parent; break; }
            cur = cur->parent;
        }
        if (!found) { free(copy); return false; }
        ti--;
    }
    free(copy);
    return true;
}
void query_collect(Node *n, const char *sel, Node ***out, size_t *count, size_t *cap) {
    if (!n) return;
    if (selector_matches(n, sel)) {
        if (*count >= *cap) {
            *cap = *cap ? *cap*2 : 8;
            *out = (Node**)xrealloc(*out, *cap*sizeof(Node*));
        }
        (*out)[(*count)++] = n;
    }
    for (size_t i=0;i<n->child_count;i++) query_collect(n->children[i], sel, out, count, cap);
}
void dump_cb(Node *n, int depth, void *ctx) {
    FILE *f = (FILE*)ctx;
    for (int i=0;i<depth;i++) fprintf(f, "  ");
    if (n->type==NODE_DOCUMENT) fprintf(f, "#document\n");
    else if (n->type==NODE_TEXT) {
        char *t2 = xstrdup(n->text ? n->text : "");
        for (char *c=t2; *c; c++) if (isspace((unsigned char)*c)) *c=' ';
        char *wout = (char*)xmalloc(strlen(t2)+1);
        size_t woi=0;
        bool last_space=false;
        for (char *r=t2; *r; r++) {
            if (*r==' ') { if (!last_space) wout[woi++]=' '; last_space=true; }
            else { wout[woi++]=*r; last_space=false; }
        }
        wout[woi]='\0';
        char *t=wout;
        while (*t==' ') t++;
        size_t tl=strlen(t);
        while (tl && t[tl-1]==' ') tl--;
        t[tl]='\0';
        if (t[0]) fprintf(f, "\"%s\"\n", t);
        else fprintf(f, "\"\" (ws)\n");
        free(t2); free(wout);
    } else {
        fprintf(f, "<%s", n->tag);
        for (size_t i=0;i<n->attr_count;i++) fprintf(f, " %s=\"%s\"", n->attrs[i].name, n->attrs[i].value);
        if (n->style_count) {
            fprintf(f, " [style:");
            for (size_t i=0;i<n->style_count;i++) fprintf(f, " %s:%s;", n->style[i].name, n->style[i].value);
            fprintf(f, "]");
        }
        fprintf(f, ">\n");
    }
}
bool is_void_tag(const char *tag) {
    static const char *voids[] = {"area","base","br","col","embed","hr","img","input","link","meta","param","source","track","wbr",NULL};
    for (int i=0; voids[i]; i++) if (streqi(tag, voids[i])) return true;
    return false;
}
bool is_skip_tag(const char *tag) {
    static const char *skips[] = {"noscript","template","svg",NULL};
    for (int i=0; skips[i]; i++) if (streqi(tag, skips[i])) return true;
    return false;
}
bool is_block_tag(const char *tag) {
    static const char *blocks[] = {"html","body","div","p","h1","h2","h3","h4","h5","h6","ul","ol","li","table","tr","td","th","thead","tbody","header","footer","section","article","nav","main","aside","blockquote","pre","form","fieldset","figure","figcaption","dl","dt","dd",NULL};
    for (int i=0; blocks[i]; i++) if (streqi(tag, blocks[i])) return true;
    return false;
}
