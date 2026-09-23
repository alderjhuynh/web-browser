#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>

#if defined(__has_include)
#  if __has_include(<curl/curl.h>)
#    define HAVE_CURL 1
#    include <curl/curl.h>
#  endif
#endif

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "oom malloc %zu\n", n); exit(1); }
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "oom realloc %zu\n", n); exit(1); }
    return q;
}
static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) { fprintf(stderr, "oom calloc\n"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n+1);
    memcpy(p, s, n+1);
    return p;
}
static char *xstrndup(const char *s, size_t n) {
    char *p = (char*)xmalloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
static char *str_lower_dup(const char *s) {
    char *p = xstrdup(s);
    for (char *c = p; *c; c++) *c = (char)tolower((unsigned char)*c);
    return p;
}
static void str_trim_inplace(char *s) {
    char *a = s;
    while (*a && isspace((unsigned char)*a)) a++;
    if (a != s) memmove(s, a, strlen(a)+1);
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}
static bool streqi(const char *a, const char *b) {
    return strcasecmp(a,b)==0;
}

typedef struct { const char *name; const char *value; } Entity;
static const Entity ENTITIES[] = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
    {"nbsp", " "}, {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"}, {"hellip", "\xE2\x80\xA6"},
    {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"}, {"laquo", "\xC2\xAB"},
    {"raquo", "\xC2\xBB"}, {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"},
    {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"}, {"trade", "\xE2\x84\xA2"},
    {NULL, NULL}
};

static char *decode_entities(const char *src) {
    size_t len = strlen(src);
    char *out = (char*)xmalloc(len*4+1);
    size_t oi = 0;
    for (size_t i=0; i<len; ) {
        if (src[i] != '&') {
            out[oi++] = src[i++];
            continue;
        }
        size_t semi = i+1;
        while (semi < len && semi - i < 16 && src[semi] != ';' && src[semi] != '&' && !isspace((unsigned char)src[semi]) && src[semi] != '<')
            semi++;
        bool has_semi = (semi < len && src[semi]==';');
        size_t name_len = has_semi ? semi - (i+1) : 0;
        if (name_len==0) { out[oi++] = src[i++]; continue; }
        char name[32];
        if (name_len >= sizeof(name)) { out[oi++] = src[i++]; continue; }
        memcpy(name, src+i+1, name_len);
        name[name_len]='\0';

        if (name[0]=='#') {
            long cp = 0;
            if (name[1]=='x' || name[1]=='X') cp = strtol(name+2, NULL, 16);
            else cp = strtol(name+1, NULL, 10);
            if (cp>0 && cp<0x10FFFF) {

                if (cp < 0x80) out[oi++] = (char)cp;
                else if (cp < 0x800) { out[oi++] = (char)(0xC0 | (cp>>6)); out[oi++] = (char)(0x80 | (cp&0x3F)); }
                else if (cp < 0x10000) { out[oi++] = (char)(0xE0 | (cp>>12)); out[oi++] = (char)(0x80 | ((cp>>6)&0x3F)); out[oi++] = (char)(0x80 | (cp&0x3F)); }
                else { out[oi++] = (char)(0xF0 | (cp>>18)); out[oi++] = (char)(0x80 | ((cp>>12)&0x3F)); out[oi++] = (char)(0x80 | ((cp>>6)&0x3F)); out[oi++] = (char)(0x80 | (cp&0x3F)); }
                i = semi+1;
                continue;
            }
        } else {

            for (char *c=name; *c; c++) *c=(char)tolower((unsigned char)*c);
            for (int e=0; ENTITIES[e].name; e++) {
                if (strcmp(name, ENTITIES[e].name)==0) {
                    size_t vl = strlen(ENTITIES[e].value);
                    memcpy(out+oi, ENTITIES[e].value, vl);
                    oi += vl;
                    i = semi+1;
                    goto next;
                }
            }
        }

        out[oi++] = src[i++];
        next: ;
    }
    out[oi]='\0';

    char *shrunk = xstrdup(out);
    free(out);
    return shrunk;
}

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

static Node *node_new(NodeType type, const char *tag) {
    Node *n = (Node*)xcalloc(1, sizeof(Node));
    n->type = type;
    n->tag = tag ? str_lower_dup(tag) : xstrdup("#text");
    return n;
}
static Node *node_new_document(void) { return node_new(NODE_DOCUMENT, "document"); }
static Node *node_new_element(const char *tag) { return node_new(NODE_ELEMENT, tag); }
static Node *node_new_text(const char *txt) {
    Node *n = node_new(NODE_TEXT, "#text");
    n->text = xstrdup(txt);
    return n;
}

static void node_free(Node *n) {
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

static void node_add_child(Node *parent, Node *child) {
    if (!parent || !child) return;
    if (parent->child_count == parent->child_cap) {
        size_t nc = parent->child_cap ? parent->child_cap*2 : 4;
        parent->children = (Node**)xrealloc(parent->children, nc*sizeof(Node*));
        parent->child_cap = nc;
    }
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

static void node_set_attr(Node *n, const char *name, const char *value) {
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
static const char *node_get_attr(const Node *n, const char *name) {
    if (!n || !name) return NULL;

    for (size_t i=0;i<n->attr_count;i++) {
        if (strcasecmp(n->attrs[i].name, name)==0) return n->attrs[i].value;
    }
    return NULL;
}
static bool node_has_class(const Node *n, const char *cls) {
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

static void node_style_set(Node *n, const char *prop, const char *val) {
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
static const char *node_style_get(const Node *n, const char *prop) {
    if (!n || !prop) return NULL;
    for (size_t i=0;i<n->style_count;i++) if (strcasecmp(n->style[i].name, prop)==0) return n->style[i].value;
    return NULL;
}

typedef void (*WalkCb)(Node *node, int depth, void *ctx);

static void dom_walk_recursive(Node *n, int depth, WalkCb cb, void *ctx) {
    if (!n) return;
    cb(n, depth, ctx);
    for (size_t i=0;i<n->child_count;i++) dom_walk_recursive(n->children[i], depth+1, cb, ctx);
}
static void dom_walk(Node *root, WalkCb cb, void *ctx) {
    dom_walk_recursive(root, 0, cb, ctx);
}

static void dom_walk_iterative(Node *root, WalkCb cb, void *ctx) {
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
static Node *dom_find_tag(Node *root, const char *tag) {
    return dom_find_tag_recursive(root, tag);
}
static Node *dom_find_by_id(Node *root, const char *id) __attribute__((unused));
static Node *dom_find_by_id(Node *root, const char *id) {
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

static bool selector_matches(Node *node, const char *selector) {

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

static void query_collect(Node *n, const char *sel, Node ***out, size_t *count, size_t *cap) {
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

static void dump_cb(Node *n, int depth, void *ctx) {
    FILE *f = (FILE*)ctx;
    for (int i=0;i<depth;i++) fprintf(f, "  ");
    if (n->type==NODE_DOCUMENT) fprintf(f, "#document\n");
    else if (n->type==NODE_TEXT) {

        char *tmp = xstrdup(n->text ? n->text : "");

        for (char *c=tmp; *c; c++) if (isspace((unsigned char)*c)) *c=' ';

        char *w=tmp, *r=tmp;
        bool last_space=false;
        char *out = (char*)xmalloc(strlen(tmp)+1);
        size_t oi=0;
        while (*r) {
            if (*r==' ') { if (!last_space) out[oi++]=' '; last_space=true; }
            else { out[oi++]=*r; last_space=false; }
            r++;
        }
        out[oi]='\0';
        char *t=out;
        while (*t==' ') t++;
        size_t tl=strlen(t);
        while (tl && t[tl-1]==' ') tl--;
        t[tl]='\0';
        if (t[0]) fprintf(f, "\"%s\"\n", t);
        else fprintf(f, "\"\" (ws)\n");
        free(tmp); free(out);
        (void)w;
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

static bool is_void_tag(const char *tag) {
    static const char *voids[] = {"area","base","br","col","embed","hr","img","input","link","meta","param","source","track","wbr",NULL};
    for (int i=0; voids[i]; i++) if (streqi(tag, voids[i])) return true;
    return false;
}
static bool is_skip_tag(const char *tag) {
    static const char *skips[] = {"head","noscript","template","svg",NULL};
    for (int i=0; skips[i]; i++) if (streqi(tag, skips[i])) return true;
    return false;
}
static bool is_block_tag(const char *tag) {
    static const char *blocks[] = {"html","body","div","p","h1","h2","h3","h4","h5","h6","ul","ol","li","table","tr","td","th","thead","tbody","header","footer","section","article","nav","main","aside","blockquote","pre","form","fieldset","figure","figcaption","dl","dt","dd",NULL};
    for (int i=0; blocks[i]; i++) if (streqi(tag, blocks[i])) return true;
    return false;
}

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    Node *root;
    Node *current;
    Node **stack;
    size_t stack_len, stack_cap;
    int script_depth;
    bool in_style;
    char *style_buf;
    size_t style_len, style_cap;

    char **skip_stack;
    size_t skip_len, skip_cap;
} Parser;

static void parser_init(Parser *p, const char *src, size_t len) {
    memset(p,0,sizeof(*p));
    p->src = src;
    p->len = len;
    p->root = node_new_document();
    p->current = p->root;
    p->stack_cap = 32;
    p->stack = (Node**)xmalloc(p->stack_cap*sizeof(Node*));
    p->stack[0]=p->root;
    p->stack_len=1;
    p->style_cap = 1024;
    p->style_buf = (char*)xmalloc(p->style_cap);
    p->style_len=0;
    p->skip_cap=8;
    p->skip_stack=(char**)xmalloc(p->skip_cap*sizeof(char*));
}
static void parser_free(Parser *p) {

    free(p->stack);
    free(p->style_buf);
    for (size_t i=0;i<p->skip_len;i++) free(p->skip_stack[i]);
    free(p->skip_stack);
}
static inline char parser_peek(Parser *p, size_t off) __attribute__((unused));
static inline char parser_peek(Parser *p, size_t off) {
    size_t idx = p->pos+off;
    return idx < p->len ? p->src[idx] : '\0';
}
static inline bool parser_starts_with(Parser *p, const char *s) __attribute__((unused));
static inline bool parser_starts_with(Parser *p, const char *s) {
    size_t sl = strlen(s);
    if (p->pos+sl > p->len) return false;
    return strncasecmp(p->src+p->pos, s, sl)==0;
}
static void parser_push(Parser *p, Node *n) {
    if (p->stack_len >= p->stack_cap) {
        p->stack_cap*=2;
        p->stack=(Node**)xrealloc(p->stack, p->stack_cap*sizeof(Node*));
    }
    p->stack[p->stack_len++]=n;
    p->current=n;
}
static void parser_pop_to(Parser *p, Node *target) {

    for (size_t i=p->stack_len; i>0; i--) {
        if (p->stack[i-1]==target) {
            p->stack_len = i-1;
            p->current = p->stack_len ? p->stack[p->stack_len-1] : p->root;
            return;
        }
    }
}
static Node *parser_find_in_stack(Parser *p, const char *tag) {
    for (size_t i=p->stack_len; i>0; i--) {
        Node *n = p->stack[i-1];
        if (n->type==NODE_ELEMENT && streqi(n->tag, tag)) return n;
    }
    return NULL;
}
static void parser_append_style(Parser *p, const char *txt, size_t len) {
    if (p->style_len+len+1 >= p->style_cap) {
        while (p->style_len+len+1 >= p->style_cap) p->style_cap*=2;
        p->style_buf=(char*)xrealloc(p->style_buf, p->style_cap);
    }
    memcpy(p->style_buf+p->style_len, txt, len);
    p->style_len += len;
    p->style_buf[p->style_len]='\0';
}
static void parser_skip_push(Parser *p, const char *tag) {
    if (p->skip_len>=p->skip_cap) { p->skip_cap*=2; p->skip_stack=(char**)xrealloc(p->skip_stack, p->skip_cap*sizeof(char*));}
    p->skip_stack[p->skip_len++] = str_lower_dup(tag);
}
static bool parser_in_skip(Parser *p) { return p->skip_len>0; }
static void parser_skip_pop(Parser *p, const char *tag) {
    if (p->skip_len==0) return;

    char *top = p->skip_stack[p->skip_len-1];
    if (streqi(top, tag)) {
        free(top);
        p->skip_len--;
    }
}

static char *parse_tag_name(const char *s, size_t *pos, size_t len) {
    size_t i=*pos;
    while (i<len && isspace((unsigned char)s[i])) i++;
    size_t start=i;
    while (i<len && (isalnum((unsigned char)s[i]) || s[i]=='-' || s[i]==':' || s[i]=='_')) i++;
    if (i==start) return NULL;
    char *name = xstrndup(s+start, i-start);
    *pos=i;
    return name;
}

static void parse_attributes(Parser *p, size_t *pos, Node *elem) {
    const char *s = p->src;
    size_t len = p->len;
    size_t i=*pos;
    while (i<len) {
        while (i<len && isspace((unsigned char)s[i])) i++;
        if (i>=len || s[i]=='>' || s[i]=='/' ) break;

        size_t name_start=i;
        while (i<len && s[i]!='=' && !isspace((unsigned char)s[i]) && s[i]!='>' && s[i]!='/') i++;
        if (i==name_start) { i++; continue; }
        char *name = xstrndup(s+name_start, i-name_start);
        while (i<len && isspace((unsigned char)s[i])) i++;
        char *value = NULL;
        if (i<len && s[i]=='=') {
            i++;
            while (i<len && isspace((unsigned char)s[i])) i++;
            if (i<len && (s[i]=='"' || s[i]=='\'')) {
                char q=s[i++];
                size_t vstart=i;
                while (i<len && s[i]!=q) i++;
                value = xstrndup(s+vstart, i-vstart);
                if (i<len && s[i]==q) i++;
            } else {
                size_t vstart=i;
                while (i<len && !isspace((unsigned char)s[i]) && s[i]!='>' && s[i]!='/') i++;
                value = xstrndup(s+vstart, i-vstart);
            }
            char *dec = decode_entities(value);
            free(value);
            value = dec;
        } else {
            value = xstrdup("");
        }
        node_set_attr(elem, name, value);
        free(name);
        free(value);
    }
    *pos=i;
}

static Node *parse_html(const char *src, size_t len, char **out_style, size_t *out_style_len) {
    Parser p;
    parser_init(&p, src, len);
    while (p.pos < p.len) {
        if (p.src[p.pos] != '<') {

            size_t start = p.pos;
            while (p.pos < p.len && p.src[p.pos] != '<') p.pos++;
            size_t tlen = p.pos - start;
            if (tlen==0) continue;
            char *raw = xstrndup(p.src+start, tlen);
            char *decoded = decode_entities(raw);
            free(raw);

            if (p.in_style) {
                parser_append_style(&p, decoded, strlen(decoded));
                free(decoded);
                continue;
            }
            if (p.script_depth>0 || parser_in_skip(&p)) { free(decoded); continue; }

            if (decoded[0]=='\0') { free(decoded); continue; }
            Node *tn = node_new_text(decoded);
            free(decoded);
            node_add_child(p.current, tn);
            continue;
        }

        if (p.pos+4 <= p.len && strncasecmp(p.src+p.pos, "<!--", 4)==0) {
            size_t end = p.pos+4;
            while (end+2 < p.len && !(p.src[end]=='-' && p.src[end+1]=='-' && p.src[end+2]=='>')) end++;
            if (end+2 < p.len) p.pos = end+3;
            else p.pos = p.len;
            continue;
        }
        if (p.pos+9 <= p.len && strncasecmp(p.src+p.pos, "<!DOCTYPE", 9)==0) {
            size_t end = p.pos;
            while (end < p.len && p.src[end]!='>') end++;
            p.pos = end < p.len ? end+1 : p.len;
            continue;
        }
        if (p.pos+9 <= p.len && strncasecmp(p.src+p.pos, "<![CDATA[", 9)==0) {
            size_t end = p.pos+9;
            while (end+2 < p.len && !(p.src[end]==']' && p.src[end+1]==']' && p.src[end+2]=='>')) end++;

            size_t cstart = p.pos+9;
            size_t clen = end - cstart;
            if (clen && !p.in_style && p.script_depth==0 && !parser_in_skip(&p)) {
                char *raw = xstrndup(p.src+cstart, clen);
                Node *tn = node_new_text(raw);
                free(raw);
                node_add_child(p.current, tn);
            }
            p.pos = end+2 < p.len ? end+3 : p.len;
            continue;
        }
        if (p.pos+2 <= p.len && p.src[p.pos+1]=='?' ) {
            size_t end = p.pos+2;
            while (end+1 < p.len && !(p.src[end]=='?' && p.src[end+1]=='>')) end++;
            p.pos = end+1 < p.len ? end+2 : p.len;
            continue;
        }
        if (p.pos+1 < p.len && p.src[p.pos+1]=='/') {
            size_t i = p.pos+2;
            char *tag = parse_tag_name(p.src, &i, p.len);
            if (!tag) { p.pos++; continue; }

            while (i<p.len && p.src[i]!='>') i++;
            if (i<p.len) i++;
            p.pos = i;
            for (char *c=tag; *c; c++) *c=(char)tolower((unsigned char)*c);
            if (strcmp(tag, "style")==0) {
                p.in_style=false;
                free(tag);
                continue;
            }
            if (strcmp(tag, "script")==0) {
                if (p.script_depth>0) p.script_depth--;
                free(tag);
                continue;
            }
            if (p.script_depth>0) { free(tag); continue; }
            if (parser_in_skip(&p)) {
                parser_skip_pop(&p, tag);
                free(tag);
                continue;
            }

            Node *found = parser_find_in_stack(&p, tag);
            if (found) {

                parser_pop_to(&p, found);

            }
            free(tag);
            continue;
        }

        size_t i = p.pos+1;

        if (i<p.len && p.src[i]=='!') {
            size_t end=i;
            while (end<p.len && p.src[end]!='>') end++;
            p.pos = end<p.len? end+1 : p.len;
            continue;
        }
        char *tag = parse_tag_name(p.src, &i, p.len);
        if (!tag) { p.pos++; continue; }
        for (char *c=tag; *c; c++) *c=(char)tolower((unsigned char)*c);
        Node *elem = node_new_element(tag);
        parse_attributes(&p, &i, elem);

        bool self_close=false;
        while (i<p.len && isspace((unsigned char)p.src[i])) i++;
        if (i<p.len && p.src[i]=='/') { self_close=true; i++; }
        if (i<p.len && p.src[i]=='>') i++;
        else {

            while (i<p.len && p.src[i]!='>') i++;
            if (i<p.len) i++;
        }
        p.pos = i;

        if (strcmp(tag, "style")==0 && p.script_depth==0) {

            p.in_style=true;

            node_free(elem);
            free(tag);
            continue;
        }
        if (strcmp(tag, "script")==0) {
            p.script_depth++;
            node_free(elem);
            free(tag);
            continue;
        }
        if (p.script_depth>0) { node_free(elem); free(tag); continue; }
        if (parser_in_skip(&p) || is_skip_tag(tag)) {
            if (!is_void_tag(tag) && !self_close) {

                parser_skip_push(&p, tag);
            }
            node_free(elem);
            free(tag);
            continue;
        }

        bool is_void = is_void_tag(tag);
        node_add_child(p.current, elem);
        if (!is_void && !self_close) {
            parser_push(&p, elem);
        } else {

        }
        free(tag);
    }
    char *style_out = NULL;
    size_t slen = p.style_len;
    if (slen) {
        style_out = (char*)xmalloc(slen+1);
        memcpy(style_out, p.style_buf, slen);
        style_out[slen]='\0';
    }
    if (out_style) *out_style = style_out; else free(style_out);
    if (out_style_len) *out_style_len = slen;
    Node *root = p.root;
    parser_free(&p);
    return root;
}

typedef struct {
    char **selectors;
    size_t sel_count;
    StyleProp *decls;
    size_t decl_count;
} CSSRule;

static void css_rule_free(CSSRule *r) {
    for (size_t i=0;i<r->sel_count;i++) free(r->selectors[i]);
    free(r->selectors);
    for (size_t i=0;i<r->decl_count;i++) { free(r->decls[i].name); free(r->decls[i].value); }
    free(r->decls);
}

static char *css_strip_comments(const char *src) {
    size_t len=strlen(src);
    char *out=(char*)xmalloc(len+1);
    size_t oi=0;
    bool in_str=false;
    char str_q=0;
    for (size_t i=0;i<len; ) {
        if (!in_str && i+1<len && src[i]=='/' && src[i+1]=='*') {
            i+=2;
            while (i+1<len && !(src[i]=='*' && src[i+1]=='/')) i++;
            if (i+1<len) i+=2;
            else i=len;
            continue;
        }
        if (!in_str && (src[i]=='"' || src[i]=='\'')) { in_str=true; str_q=src[i]; out[oi++]=src[i++]; continue; }
        if (in_str && src[i]==str_q) {
            bool escaped = (i>0 && src[i-1]=='\\');
            if (!escaped) in_str=false;
            out[oi++]=src[i++];
            continue;
        }
        out[oi++]=src[i++];
    }
    out[oi]='\0';
    return out;
}

static void trim(char *s){ str_trim_inplace(s); }

static CSSRule *parse_css(const char *css_text, size_t *out_count) {
    if (!css_text || !*css_text) { *out_count=0; return NULL; }
    char *clean = css_strip_comments(css_text);

    size_t len=strlen(clean);
    char *filtered=(char*)xmalloc(len+1);
    size_t fi=0;
    for (size_t i=0;i<len; ) {
        if (clean[i]=='@') {

            size_t j=i;
            while (j<len && clean[j]!=';' && clean[j]!='{' ) j++;
            if (j<len && clean[j]=='{') {

                int depth=0;
                size_t k=j;
                for (; k<len; k++) {
                    if (clean[k]=='{') depth++;
                    else if (clean[k]=='}') { depth--; if (depth==0) { k++; break; } }
                }
                i = k;
            } else if (j<len && clean[j]==';') i=j+1;
            else i=len;
            continue;
        }
        filtered[fi++]=clean[i++];
    }
    filtered[fi]='\0';
    free(clean);
    clean=filtered;

    size_t cap=16;
    CSSRule *rules=(CSSRule*)xcalloc(cap, sizeof(CSSRule));
    size_t rcount=0;

    size_t i=0;
    len=strlen(clean);
    while (i<len) {

        while (i<len && isspace((unsigned char)clean[i])) i++;
        if (i>=len) break;

        size_t sel_start=i;
        size_t brace = sel_start;
        bool in_str=false;
        char q=0;
        while (brace<len) {
            if (!in_str && (clean[brace]=='"' || clean[brace]=='\'')) { in_str=true; q=clean[brace]; }
            else if (in_str && clean[brace]==q) { in_str=false; }
            else if (!in_str && clean[brace]=='{') break;

            brace++;
        }
        if (brace>=len || clean[brace]!='{') break;
        size_t sel_end=brace;

        size_t body_start=brace+1;
        size_t body_end=body_start;
        int depth=1;
        in_str=false; q=0;
        while (body_end<len && depth>0) {
            char c=clean[body_end];
            if (!in_str && (c=='"' || c=='\'')) { in_str=true; q=c; }
            else if (in_str && c==q) {

                if (body_end>0 && clean[body_end-1]!='\\') in_str=false;
            } else if (!in_str) {
                if (c=='{') depth++;
                else if (c=='}') depth--;
            }
            if (depth>0) body_end++;
            else break;
        }
        if (depth!=0) break;

        char *sel_part = xstrndup(clean+sel_start, sel_end-sel_start);
        char *body = xstrndup(clean+body_start, body_end-body_start);

        char *s = sel_part;

        char **sels=NULL;
        size_t scap=0, scnt=0;
        char *token_start=s;
        bool in_bracket=false, in_s=false;
        char sq=0;
        for (char *c=s; ; c++) {
            char ch = *c;
            bool is_end = (ch=='\0');
            bool is_comma = (ch==',' && !in_bracket && !in_s);
            if (ch=='"' || ch=='\'') {
                if (!in_s) { in_s=true; sq=ch; }
                else if (ch==sq) { in_s=false; }
            }
            if (ch=='[') in_bracket=true;
            if (ch==']') in_bracket=false;
            if (is_comma || is_end) {
                size_t tlen = (size_t)(c - token_start);
                char *tok = xstrndup(token_start, tlen);
                trim(tok);

                char *filtered_tok = (char*)xmalloc(strlen(tok)+1);
                size_t fi2=0;
                bool in_attr=false;
                for (size_t k=0;k<strlen(tok); ) {
                    if (tok[k]=='[') in_attr=true;
                    if (tok[k]==']') in_attr=false;
                    if (!in_attr && tok[k]==':') {

                        k++;
                        if (k<strlen(tok) && tok[k]==':') k++;
                        while (k<strlen(tok) && (isalnum((unsigned char)tok[k]) || tok[k]=='-' || tok[k]=='(' || tok[k]==')')) {
                            if (tok[k]=='(') {

                                k++;
                                while (k<strlen(tok) && tok[k]!=')') k++;
                                if (k<strlen(tok)) k++;
                                break;
                            } else k++;
                        }
                        continue;
                    } else {
                        filtered_tok[fi2++]=tok[k++];
                    }
                }
                filtered_tok[fi2]='\0';
                trim(filtered_tok);

                    bool is_pseudo=false;
                    {
                        bool in_a=false; bool in_q=false; char qq=0;
                        for (char *cc=tok; *cc; cc++) {
                            if (*cc=='"'||*cc=='\'') { if (!in_q) { in_q=true; qq=*cc; } else if (*cc==qq) in_q=false; }
                            if (!in_q) {
                                if (*cc=='[') in_a=true;
                                else if (*cc==']') in_a=false;
                                else if (!in_a && *cc==':') { is_pseudo=true; break; }
                            }
                        }
                    }
                    if (!is_pseudo && filtered_tok[0]) {
                        if (scnt>=scap) { scap=scap?scap*2:4; sels=(char**)xrealloc(sels, scap*sizeof(char*)); }
                        sels[scnt++]=xstrdup(filtered_tok);
                }
                free(filtered_tok);
                free(tok);
                if (is_end) break;
                token_start = c+1;
            }
            if (is_end) break;
        }

        StyleProp *decls=NULL;
        size_t dcap=0, dcnt=0;
        char *b=body;
        size_t blen=strlen(b);
        size_t dstart=0;
        in_s=false; sq=0;
        int paren=0;
        for (size_t k=0;k<=blen; k++) {
            char ch = b[k];
            if (ch=='"' || ch=='\'') {
                if (!in_s) { in_s=true; sq=ch; }
                else if (ch==sq && (k==0 || b[k-1]!='\\')) in_s=false;
            }
            if (!in_s) {
                if (ch=='(') paren++;
                else if (ch==')' && paren>0) paren--;
            }
            if ((ch==';' && !in_s && paren==0) || ch=='\0') {
                size_t dlen = k - dstart;
                if (dlen>0) {
                    char *decl = xstrndup(b+dstart, dlen);
                    char *colon = strchr(decl, ':');
                    if (colon) {
                        *colon='\0';
                        char *prop = decl;
                        char *val = colon+1;
                        trim(prop); trim(val);

                        for (char *c2=prop; *c2; c2++) *c2=(char)tolower((unsigned char)*c2);
                        if (prop[0] && val[0]) {
                            if (dcnt>=dcap) { dcap=dcap?dcap*2:4; decls=(StyleProp*)xrealloc(decls, dcap*sizeof(StyleProp));}
                            decls[dcnt].name = xstrdup(prop);
                            decls[dcnt].value = xstrdup(val);
                            dcnt++;
                        }
                    }
                    free(decl);
                }
                dstart = k+1;
            }
        }
        free(sel_part); free(body);
        if (scnt && dcnt) {
            if (rcount>=cap) { cap*=2; rules=(CSSRule*)xrealloc(rules, cap*sizeof(CSSRule)); }
            rules[rcount].selectors = sels;
            rules[rcount].sel_count = scnt;
            rules[rcount].decls = decls;
            rules[rcount].decl_count = dcnt;
            rcount++;
        } else {
            for (size_t k=0;k<scnt;k++) free(sels[k]);
            free(sels);
            for (size_t k=0;k<dcnt;k++) { free(decls[k].name); free(decls[k].value); }
            free(decls);
        }
        i = body_end+1;
    }
    free(clean);
    *out_count = rcount;
    return rules;
}

typedef struct { const char *name; const char *hex; } NamedColor;
static const NamedColor NAMED_COLORS[] = {
    {"black","#000000"}, {"white","#ffffff"}, {"red","#ff0000"},
    {"green","#008000"}, {"blue","#0000ff"}, {"yellow","#ffff00"},
    {"orange","#ffa500"}, {"purple","#800080"}, {"pink","#ffc0cb"},
    {"gray","#808080"}, {"grey","#808080"}, {"silver","#c0c0c0"},
    {"navy","#000080"}, {"teal","#008080"}, {"aqua","#00ffff"},
    {"cyan","#00ffff"}, {"lime","#00ff00"}, {"maroon","#800000"},
    {"olive","#808000"}, {"fuchsia","#ff00ff"}, {"magenta","#ff00ff"},
    {"brown","#a52a2a"}, {"coral","#ff7f50"}, {"crimson","#dc143c"},
    {"darkblue","#00008b"}, {"darkcyan","#008b8b"}, {"darkgray","#a9a9a9"},
    {"darkgrey","#a9a9a9"}, {"darkgreen","#006400"}, {"darkred","#8b0000"},
    {"dimgray","#696969"}, {"dimgrey","#696969"},
    {"dodgerblue","#1e90ff"}, {"firebrick","#b22222"},
    {"forestgreen","#228b22"}, {"gainsboro","#dcdcdc"}, {"gold","#ffd700"},
    {"goldenrod","#daa520"}, {"hotpink","#ff69b4"}, {"indianred","#cd5c5c"},
    {"indigo","#4b0082"}, {"ivory","#fffff0"}, {"khaki","#f0e68c"},
    {"lavender","#e6e6fa"}, {"lawngreen","#7cfc00"}, {"lemonchiffon","#fffacd"},
    {"lightblue","#add8e6"}, {"lightcoral","#f08080"}, {"lightcyan","#e0ffff"},
    {"lightgray","#d3d3d3"}, {"lightgrey","#d3d3d3"},
    {"lightgreen","#90ee90"}, {"lightpink","#ffb6c1"}, {"lightyellow","#ffffe0"},
    {"limegreen","#32cd32"}, {"linen","#faf0e6"},
    {"mediumblue","#0000cd"}, {"mediumpurple","#9370db"},
    {"mediumseagreen","#3cb371"}, {"midnightblue","#191970"},
    {"mintcream","#f5fffa"}, {"mistyrose","#ffe4e1"}, {"moccasin","#ffe4b5"},
    {"oldlace","#fdf5e6"}, {"orchid","#da70d6"},
    {"palegoldenrod","#eee8aa"}, {"palegreen","#98fb98"},
    {"paleturquoise","#afeeee"}, {"papayawhip","#ffefd5"},
    {"peachpuff","#ffdab9"}, {"peru","#cd853f"}, {"plum","#dda0dd"},
    {"powderblue","#b0e0e6"}, {"rosybrown","#bc8f8f"}, {"royalblue","#4169e1"},
    {"saddlebrown","#8b4513"}, {"salmon","#fa8072"}, {"sandybrown","#f4a460"},
    {"seagreen","#2e8b57"}, {"seashell","#fff5ee"}, {"sienna","#a0522d"},
    {"skyblue","#87ceeb"}, {"slateblue","#6a5acd"}, {"slategray","#708090"},
    {"slategrey","#708090"}, {"snow","#fffafa"}, {"springgreen","#00ff7f"},
    {"steelblue","#4682b4"}, {"tan","#d2b48c"}, {"thistle","#d8bfd8"},
    {"tomato","#ff6347"}, {"turquoise","#40e0d0"}, {"violet","#ee82ee"},
    {"wheat","#f5deb3"}, {"whitesmoke","#f5f5f5"}, {"yellowgreen","#9acd32"},
    {NULL,NULL}
};

static char *normalize_color(const char *color) {
    if (!color) return xstrdup("");
    char *tmp = xstrdup(color);
    trim(tmp);
    char *lower = str_lower_dup(tmp);
    free(tmp);
    tmp = lower;
    for (int i=0; NAMED_COLORS[i].name; i++) {
        if (strcmp(tmp, NAMED_COLORS[i].name)==0) {
            char *r = xstrdup(NAMED_COLORS[i].hex);
            free(tmp);
            return r;
        }
    }
    if (strcmp(tmp, "transparent")==0 || strcmp(tmp, "currentcolor")==0 || strcmp(tmp, "inherit")==0) { free(tmp); return xstrdup(""); }
    if (tmp[0]=='#') {
        size_t hlen = strlen(tmp+1);
        char *res=NULL;
        if (hlen==3) {
            res=(char*)xmalloc(8);
            res[0]='#'; res[1]=tmp[1]; res[2]=tmp[1]; res[3]=tmp[2]; res[4]=tmp[2]; res[5]=tmp[3]; res[6]=tmp[3]; res[7]='\0';
            for (int i=1;i<7;i++) res[i]=(char)tolower((unsigned char)res[i]);
        } else if (hlen==6) {
            res=xstrdup(tmp);
            for (char *c=res; *c; c++) *c=(char)tolower((unsigned char)*c);
        } else if (hlen==8) {
            res=(char*)xmalloc(8);
            res[0]='#'; memcpy(res+1, tmp+1, 6); res[7]='\0';
            for (char *c=res; *c; c++) *c=(char)tolower((unsigned char)*c);
        } else {
            free(tmp); return xstrdup("");
        }
        free(tmp);
        return res;
    }

    if (strncmp(tmp, "rgb",3)==0) {

        char *p=tmp;
        int nums[4]={0}; int n=0;
        bool is_percent=false;
        while (*p && n<4) {
            if (isdigit((unsigned char)*p) || *p=='.') {
                char *end;
                double v = strtod(p, &end);
                bool pct = (end && *end=='%');
                if (pct) { v = v*255/100.0; is_percent=true; }
                nums[n++] = (int)v;
                p = end + (pct?1:0);
            } else p++;
        }
        (void)is_percent;
        if (n>=3) {
            if (nums[0]<0) nums[0]=0; if (nums[0]>255) nums[0]=255;
            if (nums[1]<0) nums[1]=0; if (nums[1]>255) nums[1]=255;
            if (nums[2]<0) nums[2]=0; if (nums[2]>255) nums[2]=255;
            char *res=(char*)xmalloc(8);
            snprintf(res,8,"#%02x%02x%02x", nums[0], nums[1], nums[2]);
            free(tmp);
            return res;
        }
    }

    free(tmp);
    return xstrdup("");
}

static void specificity_of_selector(const char *sel, int *a, int *b, int *c) {
    int ids=0, classes=0, tags=0;
    for (size_t i=0; sel[i]; ) {
        if (sel[i]=='[') { classes++; while (sel[i] && sel[i]!=']') i++; if (sel[i]==']') i++; continue; }
        if (sel[i]=='#') { ids++; i++; while (sel[i] && (isalnum((unsigned char)sel[i]) || sel[i]=='-' || sel[i]=='_')) i++; continue; }
        if (sel[i]=='.') { classes++; i++; while (sel[i] && (isalnum((unsigned char)sel[i]) || sel[i]=='-' || sel[i]=='_')) i++; continue; }
        if (isalpha((unsigned char)sel[i])) { tags++; while (sel[i] && (isalnum((unsigned char)sel[i]) || sel[i]=='-' || sel[i]=='_')) i++; continue; }
        i++;
    }
    *a=ids; *b=classes; *c=tags;
}

static int heading_size(const char *tag) {
    if (streqi(tag,"h1")) return 32;
    if (streqi(tag,"h2")) return 24;
    if (streqi(tag,"h3")) return 20;
    if (streqi(tag,"h4")) return 16;
    if (streqi(tag,"h5")) return 14;
    if (streqi(tag,"h6")) return 12;
    return 0;
}

static void ua_stylesheet(Node *n) {
    const char *tag = n->tag;
    int hs = heading_size(tag);
    if (hs) {
        char buf[16]; snprintf(buf,sizeof(buf),"%dpx", hs);
        node_style_set(n, "font-size", buf);
        node_style_set(n, "font-weight", "bold");
        node_style_set(n, "margin-top", "0.67em");
        node_style_set(n, "margin-bottom", "0.67em");
    }
    if (streqi(tag,"b") || streqi(tag,"strong")) node_style_set(n,"font-weight","bold");
    if (streqi(tag,"i") || streqi(tag,"em")) node_style_set(n,"font-style","italic");
    if (streqi(tag,"p")) { node_style_set(n,"margin-top","1em"); node_style_set(n,"margin-bottom","1em"); }
    if (streqi(tag,"body")) { node_style_set(n,"color","#000000"); node_style_set(n,"background-color","#ffffff"); node_style_set(n,"margin","8px"); }
    if (streqi(tag,"a")) { node_style_set(n,"color","#0000ee"); node_style_set(n,"text-decoration","underline"); }
    if (streqi(tag,"ul") || streqi(tag,"ol")) { node_style_set(n,"margin-top","1em"); node_style_set(n,"margin-bottom","1em"); node_style_set(n,"padding-left","40px"); }
    if (streqi(tag,"li")) { node_style_set(n,"margin-top","0"); node_style_set(n,"margin-bottom","0"); }
    if (streqi(tag,"blockquote")) { node_style_set(n,"margin-left","40px"); node_style_set(n,"margin-top","1em"); node_style_set(n,"margin-bottom","1em"); }
    if (streqi(tag,"pre")) { node_style_set(n,"font-family","Courier"); node_style_set(n,"margin-top","1em"); node_style_set(n,"margin-bottom","1em"); }
    if (streqi(tag,"small")) node_style_set(n,"font-size","0.83em");
    if (streqi(tag,"sub") || streqi(tag,"sup")) node_style_set(n,"font-size","0.75em");
    if (!node_style_get(n,"font-size")) node_style_set(n,"font-size","16px");
}

static void apply_styles_recursive(Node *node, CSSRule *rules, size_t rule_count) {

    if (node->type==NODE_ELEMENT || node->type==NODE_DOCUMENT) {

        ua_stylesheet(node);
        if (node->parent) {

            const char *inheritable[] = {"color","font-size","font-family","font-weight","font-style","text-align","line-height","text-decoration","visibility","list-style-type",NULL};
            bool ua_owns_size = (heading_size(node->tag)>0 || streqi(node->tag,"small") || streqi(node->tag,"sub") || streqi(node->tag,"sup"));
            bool ua_owns_weight = (streqi(node->tag,"b")||streqi(node->tag,"strong")|| heading_size(node->tag)>0);
            bool ua_owns_style = (streqi(node->tag,"i")||streqi(node->tag,"em"));
            for (int k=0; inheritable[k]; k++) {
                const char *prop = inheritable[k];
                if (strcmp(prop,"font-size")==0 && ua_owns_size) continue;
                if (strcmp(prop,"font-weight")==0 && ua_owns_weight) continue;
                if (strcmp(prop,"font-style")==0 && ua_owns_style) continue;
                const char *pv = node_style_get(node->parent, prop);
                if (pv) {

                    node_style_set(node, prop, pv);
                }
            }
        }

        typedef struct { char *prop; int a,b,c; int idx; } Winner;
        Winner *winners=NULL;
        size_t wcap=0, wcnt=0;
        for (size_t ri=0; ri<rule_count; ri++) {
            CSSRule *r = &rules[ri];
            for (size_t si=0; si<r->sel_count; si++) {
                const char *sel = r->selectors[si];
                if (!selector_matches(node, sel)) continue;
                int a,b,c;
                specificity_of_selector(sel,&a,&b,&c);
                for (size_t di=0; di<r->decl_count; di++) {
                    const char *prop = r->decls[di].name;

                    Winner *w=NULL;
                    for (size_t k=0;k<wcnt;k++) if (strcmp(winners[k].prop, prop)==0) { w=&winners[k]; break; }
                    bool win = false;
                    if (!w) win=true;
                    else {
                        if (a > w->a) win=true;
                        else if (a==w->a && b > w->b) win=true;
                        else if (a==w->a && b==w->b && c > w->c) win=true;
                        else if (a==w->a && b==w->b && c==w->c && (int)ri > w->idx) win=true;
                    }
                    if (win) {
                        if (!w) {
                            if (wcnt>=wcap) { wcap=wcap? wcap*2:8; winners=(Winner*)xrealloc(winners, wcap*sizeof(Winner)); }
                            winners[wcnt].prop = xstrdup(prop);
                            winners[wcnt].a=a; winners[wcnt].b=b; winners[wcnt].c=c; winners[wcnt].idx=(int)ri;
                            wcnt++;
                            w = &winners[wcnt-1];
                        } else { w->a=a; w->b=b; w->c=c; w->idx=(int)ri; }
                        node_style_set(node, prop, r->decls[di].value);
                    }
                }
            }
        }
        for (size_t k=0;k<wcnt;k++) free(winners[k].prop);
        free(winners);

        const char *inline_style = node_get_attr(node, "style");
        if (inline_style) {

            char *copy = xstrdup(inline_style);
            size_t len=strlen(copy);
            size_t start=0;
            bool in_s=false; char sq=0; int paren=0;
            for (size_t k=0;k<=len; k++) {
                char ch = copy[k];
                if (ch=='"'||ch=='\'') {
                    if (!in_s) { in_s=true; sq=ch; }
                    else if (ch==sq) in_s=false;
                }
                if (!in_s) {
                    if (ch=='(') paren++;
                    else if (ch==')' && paren>0) paren--;
                }
                if ((ch==';' && !in_s && paren==0) || ch=='\0') {
                    size_t dlen = k - start;
                    if (dlen>0) {
                        char *decl = xstrndup(copy+start, dlen);
                        char *colon=strchr(decl, ':');
                        if (colon) {
                            *colon='\0';
                            char *prop=decl, *val=colon+1;
                            trim(prop); trim(val);
                            for (char *c2=prop; *c2; c2++) *c2=(char)tolower((unsigned char)*c2);
                            if (prop[0] && val[0]) node_style_set(node, prop, val);
                        }
                        free(decl);
                    }
                    start=k+1;
                }
            }
            free(copy);
        }
    }
    for (size_t i=0;i<node->child_count;i++) apply_styles_recursive(node->children[i], rules, rule_count);
}
static void apply_styles(Node *root, CSSRule *rules, size_t n) {
    apply_styles_recursive(root, rules, n);
}

#ifdef HAVE_CURL
struct Mem { char *data; size_t size; };
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size*nmemb;
    struct Mem *mem = (struct Mem*)userp;
    char *ptr = (char*)xrealloc(mem->data, mem->size + realsize + 1);
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size]=0;
    return realsize;
}
static char *fetch_url(const char *url, size_t *out_len) {
    CURL *curl = curl_easy_init();
    if (!curl) { fprintf(stderr,"curl init failed\n"); return NULL; }
    struct Mem chunk = {0};
    chunk.data = (char*)xmalloc(1);
    chunk.size=0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (C Browser)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr,"fetch %s failed: %s\n", url, curl_easy_strerror(res));
        free(chunk.data);
        curl_easy_cleanup(curl);
        return NULL;
    }
    long code=0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (code>=400) fprintf(stderr,"http %ld for %s\n", code, url);
    curl_easy_cleanup(curl);
    if (out_len) *out_len = chunk.size;
    return chunk.data;
}
#else
static char *fetch_url(const char *url, size_t *out_len) {
    (void)url; (void)out_len;
    fprintf(stderr,"fetch: libcurl not available (compile with -lcurl)\n");
    return NULL;
}
#endif

static char *read_file(const char *path, size_t *out_len) {
    FILE *f=fopen(path,"rb");
    if (!f) { perror(path); return NULL; }
    fseek(f,0,SEEK_END);
    long sz=ftell(f);
    fseek(f,0,SEEK_SET);
    if (sz<0) sz=0;
    char *buf=(char*)xmalloc((size_t)sz+1);
    size_t n=fread(buf,1,(size_t)sz,f);
    buf[n]='\0';
    fclose(f);
    if (out_len) *out_len=n;
    return buf;
}

static int resolve_font_size(const char *s, int parent) {
    if (!s) return parent;
    char *tmp=xstrdup(s);
    trim(tmp);
    for (char *c=tmp; *c; c++) *c=(char)tolower((unsigned char)*c);
    int res=parent;
    char *end=NULL;
    double v=strtod(tmp,&end);
    if (end==tmp) {

        if (strcmp(tmp,"small")==0) res=13;
        else if (strcmp(tmp,"medium")==0) res=16;
        else if (strcmp(tmp,"large")==0) res=18;
        else if (strcmp(tmp,"x-large")==0) res=24;
        else if (strcmp(tmp,"xx-large")==0) res=32;
        else if (strcmp(tmp,"xxx-large")==0) res=48;
        else if (strcmp(tmp,"smaller")==0) res=(int)(parent*0.85);
        else if (strcmp(tmp,"larger")==0) res=(int)(parent*1.15);
        else res=parent;
    } else {
        if (strstr(tmp,"px")) res=(int)v;
        else if (strstr(tmp,"pt")) res=(int)(v*4/3);
        else if (strstr(tmp,"em")) res=(int)(v*parent);
        else if (strstr(tmp,"rem")) res=(int)(v*16);
        else if (strstr(tmp,"%")) res=(int)(parent*v/100);
        else res=(int)v;
    }
    if (res<1) res=1;
    free(tmp);
    return res;
}
static int node_font_size(Node *n) {
    int parent=16;
    if (n->parent) parent=node_font_size(n->parent);
    const char *fs=node_style_get(n,"font-size");
    if (!fs) fs="16px";
    return resolve_font_size(fs, parent);
}

static void collect_text(Node *n, char **buf, size_t *len, size_t *cap) {
    if (n->type==NODE_TEXT) {
        const char *t=n->text ? n->text : "";

        size_t tl=strlen(t);
        if (*len+tl+2 >= *cap) { *cap = (*cap? *cap*2:256); while (*len+tl+2 >= *cap) *cap*=2; *buf=(char*)xrealloc(*buf, *cap); }

        if (*len>0 && (*buf)[*len-1]!=' ' && t[0]!=' ') { (*buf)[(*len)++]=' '; }
        memcpy(*buf+*len, t, tl);
        *len+=tl;
        (*buf)[*len]='\0';
        return;
    }
    for (size_t i=0;i<n->child_count;i++) collect_text(n->children[i], buf, len, cap);
}
static char *node_text_content(Node *n) {
    char *buf=(char*)xmalloc(1); buf[0]='\0';
    size_t len=0, cap=1;
    collect_text(n,&buf,&len,&cap);

    char *out=(char*)xmalloc(len*2+1);
    size_t oi=0;
    bool last_space=true;
    for (size_t i=0;i<len; i++) {
        char c=buf[i];
        if (isspace((unsigned char)c)) { if (!last_space) out[oi++]=' '; last_space=true; }
        else { out[oi++]=c; last_space=false; }
    }
    out[oi]='\0';

    char *s=out;
    while (*s==' ') s++;
    size_t l=strlen(s);
    while (l && s[l-1]==' ') l--;
    s[l]='\0';
    char *res=xstrdup(s);
    free(buf); free(out);
    return res;
}

static void font_cb(Node *n, int depth, void *ctx) {
    (void)ctx;
    if (n->type==NODE_ELEMENT) {
        int fs = node_font_size(n);
        const char *color = node_style_get(n,"color");
        char *norm = normalize_color(color ? color : "");
        for (int i=0;i<depth;i++) printf("  ");
        printf("<%s> font-size=%dpx color=%s (%s) block=%s\n", n->tag, fs, color ? color : "(none)", norm, is_block_tag(n->tag)?"yes":"no");
        free(norm);
    }
}

#if defined(__has_include)
#  if __has_include(<SDL.h>) && __has_include(<SDL_ttf.h>)
#    define HAVE_SDL 1
#    include <SDL.h>
#    include <SDL_ttf.h>
#  elif __has_include(<SDL2/SDL.h>) && __has_include(<SDL2/SDL_ttf.h>)
#    define HAVE_SDL 1
#    include <SDL2/SDL.h>
#    include <SDL2/SDL_ttf.h>
#  endif
#endif

#ifdef HAVE_SDL

#define GUI_TOOLBAR_H 44
#define GUI_STATUS_H  22
#define GUI_ADDR_PAD  8
#define GUI_BTN_W     36
#define GUI_BTN_GAP   4
#define GUI_CONTENT_PAD 8

typedef enum { GCMD_TEXT, GCMD_RECT, GCMD_HR } GuiCmdType;
typedef struct {
    GuiCmdType type;
    int x, y, w, h;
    char *text;
    int  font_size;
    bool bold, italic, mono, underline;
    SDL_Color color;
    SDL_Color bg;
    char *href;
    SDL_Texture *tex;
    int tex_w, tex_h;
} GuiCmd;

#define FONT_CACHE 64
typedef struct { int size; bool bold, italic, mono; TTF_Font *font; } FontCacheEnt;
static FontCacheEnt g_font_cache[FONT_CACHE];
static int g_font_cache_n = 0;
static char g_font_regular_path[512] = {0};
static char g_font_mono_path[512] = {0};
static void gui_find_font_paths(void) {
    if (g_font_regular_path[0]) return;
    const char *candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/opt/homebrew/share/fonts/DejaVuSans.ttf",
        NULL
    };
    for (int i=0;candidates[i];i++) { FILE *f=fopen(candidates[i],"rb"); if(f){ fclose(f); strncpy(g_font_regular_path,candidates[i],sizeof(g_font_regular_path)-1); break; } }
    if (!g_font_regular_path[0]) strncpy(g_font_regular_path,"/System/Library/Fonts/Supplemental/Arial.ttf",sizeof(g_font_regular_path)-1);
    const char *mono_cands[] = {
        "/System/Library/Fonts/Courier.dfont",
        "/System/Library/Fonts/Courier.ttc",
        "/System/Library/Fonts/Courier New.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/System/Library/Fonts/SFNSMono.ttf",
        NULL
    };
    for(int i=0;mono_cands[i];i++){ FILE*f=fopen(mono_cands[i],"rb"); if(f){ fclose(f); strncpy(g_font_mono_path,mono_cands[i],sizeof(g_font_mono_path)-1); break; } }
    if(!g_font_mono_path[0]) strncpy(g_font_mono_path,g_font_regular_path,sizeof(g_font_mono_path)-1);
}
static TTF_Font *gui_get_font(int sz, bool bold, bool italic, bool mono){
    if(sz<8) sz=8; if(sz>72) sz=72;
    for(int i=0;i<g_font_cache_n;i++) if(g_font_cache[i].size==sz && g_font_cache[i].bold==bold && g_font_cache[i].italic==italic && g_font_cache[i].mono==mono) return g_font_cache[i].font;
    if(g_font_cache_n>=FONT_CACHE) return g_font_cache[0].font;
    gui_find_font_paths();
    const char *path = mono? g_font_mono_path : g_font_regular_path;
    TTF_Font *f = TTF_OpenFont(path, sz);
    if(!f){

        if(mono) f = TTF_OpenFont(g_font_regular_path, sz);
        if(!f) f = TTF_OpenFont("/System/Library/Fonts/Supplemental/Arial.ttf", sz);
        if(!f) return NULL;
    }
    int style = TTF_STYLE_NORMAL;
    if(bold) style|=TTF_STYLE_BOLD;
    if(italic) style|=TTF_STYLE_ITALIC;
    if(mono) {}
    TTF_SetFontStyle(f, style);
    g_font_cache[g_font_cache_n++] = (FontCacheEnt){sz,bold,italic,mono,f};
    return f;
}
static void gui_free_fonts(void){
    for(int i=0;i<g_font_cache_n;i++) TTF_CloseFont(g_font_cache[i].font);
    g_font_cache_n=0;
}

static SDL_Color hex_to_sdl(const char *hex){
    SDL_Color c={0,0,0,255};
    if(!hex||!*hex) return c;
    char *norm = normalize_color(hex);
    if(!norm||!*norm||norm[0]!='#'){ free(norm); return c; }
    if(strlen(norm)>=7){
        char r[3]={norm[1],norm[2],0}, g_[3]={norm[3],norm[4],0}, b[3]={norm[5],norm[6],0};
        c.r=(Uint8)strtol(r,NULL,16); c.g=(Uint8)strtol(g_,NULL,16); c.b=(Uint8)strtol(b,NULL,16);
    }
    free(norm);
    return c;
}
static bool is_light(SDL_Color c) __attribute__((unused));
static bool is_light(SDL_Color c){ return (0.299*c.r+0.587*c.g+0.114*c.b)>160; }

typedef struct BrowserState BrowserState;

typedef struct {
    int win_w, win_h;
    int content_x, content_y, content_w, content_h;
    int cur_x, cur_y;
    int left_pad;
    int indent;
    int cur_line_ascent, cur_line_descent, cur_line_h;
    GuiCmd *cmds; size_t ncmd, cap;
    GuiCmd *line; size_t nline, line_cap;
    struct { SDL_Rect r; char *href; } *links; size_t nlink, link_cap;
    const char *cur_href;
    BrowserState *bstate;
} LayoutCtx;

static void lctx_init(LayoutCtx *c,int win_w,int win_h){
    memset(c,0,sizeof(*c));
    c->win_w=win_w; c->win_h=win_h;
    c->content_x = GUI_CONTENT_PAD;
    c->content_y = GUI_TOOLBAR_H + GUI_CONTENT_PAD;
    c->content_w = win_w - 2*GUI_CONTENT_PAD - 12;
    c->content_h = win_h - GUI_TOOLBAR_H - GUI_STATUS_H - 2*GUI_CONTENT_PAD;
    c->cur_x = c->content_x + c->indent;
    c->cur_y = c->content_y;
    c->cur_line_h = 0; c->cur_line_ascent=0; c->cur_line_descent=0;
    c->left_pad = GUI_CONTENT_PAD;
}
static void lctx_free(LayoutCtx *c){
    for(size_t i=0;i<c->ncmd;i++){ free(c->cmds[i].text); free(c->cmds[i].href); if(c->cmds[i].tex) SDL_DestroyTexture(c->cmds[i].tex); }
    free(c->cmds);
    for(size_t i=0;i<c->nline;i++){ free(c->line[i].text); free(c->line[i].href); if(c->line[i].tex) SDL_DestroyTexture(c->line[i].tex); }
    free(c->line);
    for(size_t i=0;i<c->nlink;i++) free(c->links[i].href);
    free(c->links);
    memset(c,0,sizeof(*c));
}
static void lctx_flush(LayoutCtx *c){
    if(c->nline==0) return;
    int line_h = 0, ascent=0, descent=0;
    for(size_t i=0;i<c->nline;i++){
        TTF_Font *f = gui_get_font(c->line[i].font_size, c->line[i].bold, c->line[i].italic, c->line[i].mono);
        int fh = f? TTF_FontHeight(f) : c->line[i].font_size;
        int asc = f? TTF_FontAscent(f) : (int)(fh*0.8);
        int des = fh - asc;
        if(fh>line_h) line_h=fh;
        if(asc>ascent) ascent=asc;
        if(des>descent) descent=des;
    }
    if(line_h==0) line_h=16;

    int x = c->content_x + c->indent;
    int baseline = c->cur_y + ascent;
    for(size_t i=0;i<c->nline;i++){
        TTF_Font *f = gui_get_font(c->line[i].font_size, c->line[i].bold, c->line[i].italic, c->line[i].mono);
        int asc = f? TTF_FontAscent(f) : (int)(c->line[i].font_size*0.8);
        int y = baseline - asc;
        c->line[i].x = x;
        c->line[i].y = y;

        if(c->line[i].href && c->line[i].href[0]){
            if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
            c->links[c->nlink].r = (SDL_Rect){c->line[i].x,c->line[i].y,c->line[i].w,c->line[i].h};
            c->links[c->nlink].href = xstrdup(c->line[i].href);
            c->nlink++;
        }
        if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
        c->cmds[c->ncmd++] = c->line[i];
        int space_w=4;
        TTF_Font *sf=f; if(sf){ TTF_SizeUTF8(sf," ",&space_w,NULL); } else space_w = c->line[i].font_size/3;
        if(i+1 < c->nline) x += c->line[i].w + space_w;
        else x += c->line[i].w;
    }

    c->nline=0;
    c->cur_x = c->content_x + c->indent;
    c->cur_y += line_h + 4;
    c->cur_line_h=0; c->cur_line_ascent=0; c->cur_line_descent=0;
}

static void lctx_emit_word(LayoutCtx *c, const char *word, Node *parent_elem){
    const char *color_s = parent_elem? node_style_get(parent_elem,"color"): NULL;
    const char *bg_s = parent_elem? node_style_get(parent_elem,"background-color"): NULL;
    if(!bg_s) bg_s = parent_elem? node_style_get(parent_elem,"background"): NULL;
    int fs = parent_elem? node_font_size(parent_elem):16;
    bool bold=false, italic=false, mono=false, underline=false;
    if(parent_elem){
        const char *fw = node_style_get(parent_elem,"font-weight");
        if(fw && (strcasecmp(fw,"bold")==0 || atoi(fw)>=600)) bold=true;
        if(streqi(parent_elem->tag,"b")||streqi(parent_elem->tag,"strong")|| streqi(parent_elem->tag,"h1")||streqi(parent_elem->tag,"h2")||streqi(parent_elem->tag,"h3")||streqi(parent_elem->tag,"h4")) bold=true;
        const char *fst = node_style_get(parent_elem,"font-style");
        if(fst && streqi(fst,"italic")) italic=true;
        if(streqi(parent_elem->tag,"i")||streqi(parent_elem->tag,"em")) italic=true;
        const char *ff = node_style_get(parent_elem,"font-family");
        if(ff && (strstr(ff,"Courier")||strstr(ff,"Mono")||strstr(ff,"mono"))) mono=true;
        if(streqi(parent_elem->tag,"pre")||streqi(parent_elem->tag,"code")) mono=true;
        const char *td = node_style_get(parent_elem,"text-decoration");
        if(td && strstr(td,"underline")) underline=true;
        if(streqi(parent_elem->tag,"a")) underline=true;
    }
    SDL_Color col = hex_to_sdl(color_s?color_s:"#000000");
    if(parent_elem && streqi(parent_elem->tag,"a") && (!color_s || !*color_s)) col = (SDL_Color){0,0,238,255};

    char *href_dup = c->cur_href? xstrdup(c->cur_href): NULL;

    TTF_Font *font = gui_get_font(fs,bold,italic,mono);
    int w=0,h=0;
    if(font){ TTF_SizeUTF8(font, word, &w, &h); }
    else { w=(int)strlen(word)* (fs*6/10); h=fs; }

    int space_w=0; { TTF_Font *sf=font; if(sf) TTF_SizeUTF8(sf," ",&space_w,NULL); else space_w=fs/3; }
    (void)space_w;

    int line_x = c->content_x + c->indent;
    int cur_line_used = 0;
    for(size_t i=0;i<c->nline;i++) cur_line_used += c->line[i].w + space_w;
    if(c->nline>0 && line_x + cur_line_used + w > c->content_x + c->content_w){
        lctx_flush(c);
        line_x = c->content_x + c->indent;
        cur_line_used=0;
    }

    GuiCmd cmd={0};
    cmd.type=GCMD_TEXT; cmd.text=xstrdup(word); cmd.font_size=fs; cmd.bold=bold; cmd.italic=italic; cmd.mono=mono; cmd.underline=underline; cmd.color=col; cmd.href=href_dup; cmd.w=w; cmd.h=h;

    (void)bg_s;
    if(c->nline==0){

    } else {

    }
    if(c->nline>=c->line_cap){ c->line_cap=c->line_cap?c->line_cap*2:16; c->line=(GuiCmd*)xrealloc(c->line,c->line_cap*sizeof(GuiCmd)); }
    c->line[c->nline++] = cmd;

}

static void lctx_emit_hr(LayoutCtx *c){
    lctx_flush(c);
    GuiCmd cmd={0}; cmd.type=GCMD_HR; cmd.x=c->content_x; cmd.y=c->cur_y; cmd.w=c->content_w; cmd.h=1; cmd.color=(SDL_Color){160,160,160,255};
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=cmd;
    c->cur_y+=12;
}
static void lctx_emit_img(LayoutCtx *c, Node *elem){
    lctx_flush(c);
    const char *alt = node_get_attr(elem,"alt");
    if(!alt) alt=node_get_attr(elem,"src");
    if(!alt) alt="[image]";
    char buf[256]; snprintf(buf,sizeof(buf), "[IMG: %s]", alt);
    const char *color_s = node_style_get(elem,"color");
    SDL_Color col = hex_to_sdl(color_s?color_s:"#666666");
    int fs=14;
    TTF_Font *f = gui_get_font(fs,false,false,false);
    int w=0,h=0; if(f) TTF_SizeUTF8(f,buf,&w,&h); else {w=(int)strlen(buf)*8; h=14;}
    int bw = w+16, bh = h+10;
    GuiCmd rect={0}; rect.type=GCMD_RECT; rect.x=c->content_x + c->indent; rect.y=c->cur_y; rect.w=bw; rect.h=bh; rect.bg=(SDL_Color){220,220,220,255}; rect.color=(SDL_Color){180,180,180,255};
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=rect;
    GuiCmd txt={0}; txt.type=GCMD_TEXT; txt.text=xstrdup(buf); txt.font_size=fs; txt.color=col; txt.x=rect.x+8; txt.y=rect.y+5; txt.w=w; txt.h=h;
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=txt;
    c->cur_y+=bh+6;
}
static void lctx_emit_bullet(LayoutCtx *c){

    int bx = c->content_x + c->indent - 14;
    if(bx < c->content_x) bx = c->content_x + c->indent;
    GuiCmd b={0}; b.type=GCMD_RECT; b.x=bx; b.y=c->cur_y+6; b.w=6; b.h=6; b.bg=(SDL_Color){0,0,0,255};
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=b;
}

static int parse_px(const char *s,int parent_fs) __attribute__((unused));
static int parse_px(const char *s,int parent_fs){
    if(!s) return 0;
    char *e=NULL; double v=strtod(s,&e);
    if(!e||e==s) return 0;
    if(strstr(s,"em")) return (int)(v*parent_fs);
    if(strstr(s,"px")) return (int)v;
    if(strstr(s,"%")) return (int)(v*parent_fs/100);
    return (int)v;
}

static bool str_is_auto(const char *s){
    if(!s) return false;
    char *t=xstrdup(s);
    trim(t);
    bool r = (strcasecmp(t,"auto")==0);
    free(t);
    return r;
}
static int resolve_len(const char *s, int containing, int font_size, bool *is_auto){
    if(is_auto) *is_auto=false;
    if(!s) return 0;
    char *t=xstrdup(s);
    trim(t);

    char *low=str_lower_dup(t);
    free(t);
    t=low;
    if(strcmp(t,"auto")==0){
        if(is_auto) *is_auto=true;
        free(t);
        return 0;
    }
    if(t[0]=='\0'){ free(t); return 0; }
    char *end=NULL;
    double v=strtod(t,&end);
    int res=0;
    if(end==t){ free(t); return 0; }

    while(end && *end && isspace((unsigned char)*end)) end++;
    if(end && *end=='%'){
        res = (int)(v * containing / 100.0);
    } else if(strstr(t,"rem")){
        res = (int)(v * 16);
    } else if(strstr(t,"em")){
        res = (int)(v * font_size);
    } else if(strstr(t,"pt")){
        res = (int)(v * 4.0/3.0);
    } else {

        res = (int)v;
    }
    free(t);
    return res;
}

static int split_shorthand(const char *s, char *out[4]){
    if(!s) return 0;
    char *copy=xstrdup(s);
    int n=0;
    char *p=copy;
    while(*p && n<4){
        while(*p && isspace((unsigned char)*p)) p++;
        if(!*p) break;
        char *start=p;

        while(*p && !isspace((unsigned char)*p)) p++;
        char save=*p;
        *p='\0';
        out[n++]=xstrdup(start);
        if(save=='\0') break;
        p++;
    }
    free(copy);
    return n;
}
static int map_shorthand_side(int side, int n){

    if(n==1) return 0;
    if(n==2) return (side==0||side==2)?0:1;
    if(n==3) { if(side==0) return 0; if(side==1||side==3) return 1; return 2; }
    if(n==4) return side;
    return 0;
}
static int resolve_shorthand_side(const char *sh, int side, int containing, int font_size, bool *is_auto, int def){
    if(is_auto) *is_auto=false;
    if(!sh) return def;
    char *toks[4]={0};
    int n=split_shorthand(sh,toks);
    if(n==0) return def;
    int idx=map_shorthand_side(side,n);
    const char *tok = toks[idx];
    int v=0;
    bool a=false;
    if(str_is_auto(tok)) a=true;
    else v = resolve_len(tok, containing, font_size, &a);
    for(int i=0;i<n;i++) free(toks[i]);
    if(is_auto) *is_auto=a;
    if(a) return 0;
    return v;
}
static const char *side_name(int s){
    static const char *nms[]={"top","right","bottom","left"};
    return nms[s&3];
}
static int get_margin_side(Node *n, int side, int containing, int font_size, bool *is_auto){
    char key[32]; snprintf(key,sizeof(key),"margin-%s",side_name(side));
    const char *v=node_style_get(n,key);
    if(v){
        bool a=false;
        if(str_is_auto(v)){ if(is_auto) *is_auto=true; return 0; }
        int px=resolve_len(v,containing,font_size,&a);
        if(is_auto) *is_auto=a;
        if(a) return 0;
        return px;
    }
    const char *sh=node_style_get(n,"margin");
    if(sh){
        bool a=false;
        int px=resolve_shorthand_side(sh,side,containing,font_size,&a,0);
        if(is_auto) *is_auto=a;
        return px;
    }
    if(is_auto) *is_auto=false;
    return 0;
}
static int get_padding_side(Node *n, int side, int containing, int font_size){
    char key[32]; snprintf(key,sizeof(key),"padding-%s",side_name(side));
    const char *v=node_style_get(n,key);
    if(v){
        bool a=false;
        int px=resolve_len(v,containing,font_size,&a);
        if(a) return 0;
        return px<0?0:px;
    }
    const char *sh=node_style_get(n,"padding");
    if(sh){
        bool a=false;
        int px=resolve_shorthand_side(sh,side,containing,font_size,&a,0);
        if(a) px=0;
        return px<0?0:px;
    }
    return 0;
}
static int get_border_side(Node *n, int side, int containing, int font_size){
    char key[32]; snprintf(key,sizeof(key),"border-%s-width",side_name(side));
    const char *v=node_style_get(n,key);
    if(v){
        bool a=false;
        int px=resolve_len(v,containing,font_size,&a);
        return a?0:(px<0?0:px);
    }
    const char *sh=node_style_get(n,"border-width");
    if(sh){
        bool a=false;
        int px=resolve_shorthand_side(sh,side,containing,font_size,&a,0);
        return a?0:(px<0?0:px);
    }

    const char *b=node_style_get(n,"border");
    if(b){
        char *toks[4]={0}; int nn=split_shorthand(b,toks);
        for(int i=0;i<nn;i++){
            if(!str_is_auto(toks[i])){

                char *end=NULL; double vv=strtod(toks[i],&end);
                if(end!=toks[i]){
                    int px=resolve_len(toks[i],containing,font_size,NULL);
                    for(int k=0;k<nn;k++) free(toks[k]);
                    return px<0?0:px;
                }
                (void)vv;
            }
        }
        for(int k=0;k<nn;k++) free(toks[k]);
    }

    char key2[32]; snprintf(key2,sizeof(key2),"border-%s",side_name(side));
    const char *bv=node_style_get(n,key2);
    if(bv){
        char *toks[4]={0}; int nn=split_shorthand(bv,toks);
        for(int i=0;i<nn;i++){
            char *end=NULL; double vv=strtod(toks[i],&end);
            if(end!=toks[i]){ int px=resolve_len(toks[i],containing,font_size,NULL); for(int k=0;k<nn;k++) free(toks[k]); return px<0?0:px; (void)vv; }
        }
        for(int k=0;k<nn;k++) free(toks[k]);
    }
    return 0;
}
typedef struct {
    int width; bool width_auto;
    int ml,mr,mt,mb; bool ml_auto,mr_auto;
    int pl,pr,pt,pb;
    int bl,br,bt,bb;
    int content_w;
    int outer_w;
    bool border_box;
} BoxModel;

static void compute_box_model(Node *n, int containing_w, BoxModel *out){
    memset(out,0,sizeof(*out));
    int fs=node_font_size(n);

    const char *bs=node_style_get(n,"box-sizing");
    out->border_box = (bs && strcasecmp(bs,"border-box")==0);

    out->pl=get_padding_side(n,3,containing_w,fs);
    out->pr=get_padding_side(n,1,containing_w,fs);
    out->pt=get_padding_side(n,0,containing_w,fs);
    out->pb=get_padding_side(n,2,containing_w,fs);
    out->bl=get_border_side(n,3,containing_w,fs);
    out->br=get_border_side(n,1,containing_w,fs);
    out->bt=get_border_side(n,0,containing_w,fs);
    out->bb=get_border_side(n,2,containing_w,fs);

    out->ml=get_margin_side(n,3,containing_w,fs,&out->ml_auto);
    out->mr=get_margin_side(n,1,containing_w,fs,&out->mr_auto);
    out->mt=get_margin_side(n,0,containing_w,fs,NULL);
    out->mb=get_margin_side(n,2,containing_w,fs,NULL);

    const char *ws=node_style_get(n,"width");
    if(!ws || str_is_auto(ws)){
        out->width_auto=true;
        out->width=0;
    } else {
        bool a=false;
        int w=resolve_len(ws,containing_w,fs,&a);
        out->width_auto=a;
        out->width=a?0:w;
        if(out->width<0) out->width=0;
    }

    int pad_lr = out->pl + out->pr;
    int border_lr = out->bl + out->br;
    int mlv = out->ml_auto?0:out->ml;
    int mrv = out->mr_auto?0:out->mr;
    if(out->width_auto){

        out->ml_auto=false; out->mr_auto=false;
        if(out->border_box){

            int outer = containing_w - mlv - mrv;
            if(outer<0) outer=0;
            out->outer_w = outer;
            out->content_w = outer - pad_lr - border_lr;
            if(out->content_w<0) out->content_w=0;
            out->width = outer;
        } else {
            int cw = containing_w - mlv - mrv - pad_lr - border_lr;
            if(cw<0) cw=0;
            out->content_w = cw;
            out->width = cw;
            out->outer_w = cw + pad_lr + border_lr;
        }
    } else {

        if(out->border_box){
            out->outer_w = out->width;
            out->content_w = out->width - pad_lr - border_lr;
            if(out->content_w<0) out->content_w=0;
        } else {
            out->content_w = out->width;
            out->outer_w = out->width + pad_lr + border_lr;
        }

        if(out->ml_auto && out->mr_auto){
            int remaining = containing_w - out->outer_w;
            if(remaining<0){ out->ml=0; out->mr=0; out->ml_auto=false; out->mr_auto=false; }
            else { out->ml = remaining/2; out->mr = remaining - out->ml; out->ml_auto=false; out->mr_auto=false; }
        } else if(out->ml_auto){
            int remaining = containing_w - out->outer_w - mrv;
            if(remaining<0) remaining=0;
            out->ml = remaining; out->ml_auto=false;
        } else if(out->mr_auto){
            int remaining = containing_w - out->outer_w - mlv;
            if(remaining<0) remaining=0;
            out->mr = remaining; out->mr_auto=false;
        } else {

        }
    }
}

static void layout_node_recursive(Node *n, LayoutCtx *c);

static void layout_children(Node *parent, LayoutCtx *c){
    for(size_t i=0;i<parent->child_count;i++) layout_node_recursive(parent->children[i],c);
}

static void layout_node_recursive(Node *n, LayoutCtx *c){
    if(!n) return;
    if(n->type==NODE_TEXT){

        const char *txt = n->text? n->text:"";

        Node *pe = n->parent;

        bool is_pre = pe && streqi(pe->tag,"pre");
        if(is_pre){

            char *copy = xstrdup(txt);
            char *line = strtok(copy,"\n");
            while(line){

                if(line[0]){

                    lctx_emit_word(c, line, pe);
                }
                lctx_flush(c);
                line = strtok(NULL,"\n");
            }
            free(copy);
            return;
        }

        const char *p=txt;
        while(*p){
            while(*p && isspace((unsigned char)*p)) p++;
            if(!*p) break;
            const char *wstart=p;
            while(*p && !isspace((unsigned char)*p)) p++;
            size_t wl = p - wstart;
            char *word = xstrndup(wstart,wl);
            lctx_emit_word(c, word, pe);
            free(word);
        }
        return;
    }
    if(n->type==NODE_DOCUMENT){
        layout_children(n,c);
        lctx_flush(c);
        return;
    }

    const char *tag = n->tag;
    bool is_block = is_block_tag(tag) || streqi(tag,"hr") || streqi(tag,"br") || streqi(tag,"img") || streqi(tag,"li");

    int saved_indent = c->indent;
    const char *saved_href = c->cur_href;
    const char *href = node_get_attr(n,"href");
    if(href && streqi(tag,"a")) c->cur_href = href;

    bool has_bg=false; SDL_Color bgcol={0};
    const char *bg = node_style_get(n,"background-color");
    if(!bg) bg=node_style_get(n,"background");
    if(bg && *bg){ char *norm=normalize_color(bg); if(norm && *norm){ bgcol=hex_to_sdl(norm); has_bg=true; } free(norm); }

    if(streqi(tag,"br")){
        lctx_flush(c);
        c->cur_y+=4;

        c->cur_x = c->content_x + c->indent;
        c->cur_href = saved_href;
        return;
    }
    if(streqi(tag,"hr")){
        lctx_emit_hr(c);
        c->cur_href=saved_href; return;
    }
    if(streqi(tag,"img")){
        lctx_emit_img(c,n);
        c->cur_href=saved_href; return;
    }
    if(is_block){
        lctx_flush(c);

        int containing_w = c->content_w - c->indent;
        if(containing_w<0) containing_w=0;
        BoxModel box;
        compute_box_model(n, containing_w, &box);

        const char *has_mt = node_style_get(n,"margin-top");
        const char *has_mb = node_style_get(n,"margin-bottom");
        const char *has_m  = node_style_get(n,"margin");
        (void)has_m;
        if(!has_mt && !has_m){
            if(streqi(tag,"h1")) box.mt = box.mt ? box.mt : 12;
            else if(streqi(tag,"h2")) box.mt = box.mt ? box.mt : 10;
            else if(streqi(tag,"p"))  box.mt = box.mt ? box.mt : 8;
            else if(streqi(tag,"h3")||streqi(tag,"ul")||streqi(tag,"ol")||streqi(tag,"blockquote")||streqi(tag,"div")||streqi(tag,"li")){
                if(box.mt==0) box.mt=6;
            }
        }
        if(!has_mb && !has_m){
            if(streqi(tag,"h1")) box.mb = box.mb ? box.mb : 12;
            else if(streqi(tag,"h2")) box.mb = box.mb ? box.mb : 10;
            else if(streqi(tag,"p"))  box.mb = box.mb ? box.mb : 8;
            else if(streqi(tag,"h3")||streqi(tag,"ul")||streqi(tag,"ol")||streqi(tag,"blockquote")||streqi(tag,"div")||streqi(tag,"li")){
                if(box.mb==0) box.mb=6;
            }
        }

        int saved_cx = c->content_x;
        int saved_cw = c->content_w;
        int saved_indent = c->indent;
        int eff_cx = c->content_x + c->indent;

        int outer_x = eff_cx + box.ml;
        int content_x = outer_x + box.bl + box.pl;
        int content_w = box.content_w;

        c->cur_y += box.mt;
        int block_outer_y = c->cur_y;

        int prev_indent = saved_indent;
        c->content_x = content_x;
        c->content_w = content_w;
        c->indent = 0;
        c->cur_x = c->content_x + c->indent;

        c->cur_y += box.bt + box.pt;

        if(streqi(tag,"ul")||streqi(tag,"ol")||streqi(tag,"blockquote")) c->indent+=32;
        if(streqi(tag,"li")){
            lctx_emit_bullet(c);
            c->indent+=16;
        }

        layout_children(n,c);
        lctx_flush(c);

        c->cur_y += box.pb + box.bb;
        int block_outer_h = c->cur_y - block_outer_y;

        if(has_bg){
            GuiCmd bgcmd={0}; bgcmd.type=GCMD_RECT;
            bgcmd.x = outer_x;
            bgcmd.w = box.outer_w;
            bgcmd.y = block_outer_y;
            bgcmd.h = block_outer_h;
            bgcmd.bg = bgcol;

            if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd));}
            memmove(c->cmds+1, c->cmds, c->ncmd*sizeof(GuiCmd));
            c->cmds[0]=bgcmd;
            c->ncmd++;
        }

        c->cur_y += box.mb;

        c->content_x = saved_cx;
        c->content_w = saved_cw;
        c->indent = prev_indent;
        c->cur_x = c->content_x + c->indent;
    } else {

        layout_children(n,c);
    }
    c->cur_href = saved_href;
    c->indent = saved_indent;
}

static void gui_build_layout(Node *root, LayoutCtx *out, int win_w,int win_h){
    lctx_init(out,win_w,win_h);
    layout_node_recursive(root,out);
    lctx_flush(out);
}

struct BrowserState{
    SDL_Window *win;
    SDL_Renderer *ren;
    Node *root;
    CSSRule *rules; size_t nrule;
    char *style_text;
    LayoutCtx lctx;
    int scroll_y;
    int total_h;
    char *current_url;
    char *addr_text;
    size_t addr_len, addr_cap, addr_cursor;
    bool addr_focused;
    bool addr_select_all;
    char status_msg[256];
    char hover_href[512];

    char **back_stack; size_t back_n, back_cap;
    char **fwd_stack; size_t fwd_n, fwd_cap;
    int win_w, win_h;
    bool needs_relayout;
    bool running;
};

static void browser_set_status(BrowserState *b,const char *fmt,...){
    va_list ap; va_start(ap,fmt); vsnprintf(b->status_msg,sizeof(b->status_msg),fmt,ap); va_end(ap);
}
static void history_push_back(BrowserState *b,const char *url){
    if(!url||!*url) return;
    if(b->back_n>=b->back_cap){ b->back_cap=b->back_cap?b->back_cap*2:8; b->back_stack=(char**)xrealloc(b->back_stack,b->back_cap*sizeof(char*));}
    b->back_stack[b->back_n++]=xstrdup(url);
}
static void history_push_fwd(BrowserState *b,const char *url){
    if(b->fwd_n>=b->fwd_cap){ b->fwd_cap=b->fwd_cap?b->fwd_cap*2:8; b->fwd_stack=(char**)xrealloc(b->fwd_stack,b->fwd_cap*sizeof(char*));}
    b->fwd_stack[b->fwd_n++]=xstrdup(url);
}
static char *history_pop_back(BrowserState *b){ if(!b->back_n) return NULL; return b->back_stack[--b->back_n]; }
static char *history_pop_fwd(BrowserState *b){ if(!b->fwd_n) return NULL; return b->fwd_stack[--b->fwd_n]; }
static void history_clear_fwd(BrowserState *b){ for(size_t i=0;i<b->fwd_n;i++) free(b->fwd_stack[i]); b->fwd_n=0; }

static char *url_encode(const char *s){
    size_t len=strlen(s);
    char *out=(char*)xmalloc(len*3+1);
    size_t oi=0;
    for(size_t i=0;i<len;i++){
        unsigned char c=(unsigned char)s[i];
        if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') out[oi++]=c;
        else if(c==' ') { out[oi++]='%'; out[oi++]='2'; out[oi++]='0'; }
        else { out[oi++]='%'; out[oi++]= "0123456789ABCDEF"[c>>4]; out[oi++]= "0123456789ABCDEF"[c&15]; }
    }
    out[oi]=0;
    return out;
}
static bool looks_like_url(const char *s){
    if(!s||!*s) return false;
    if(strncmp(s,"http://",7)==0||strncmp(s,"https://",8)==0) return true;
    if(strstr(s,"://")) return true;

    if(strchr(s,' ')!=NULL) return false;
    if(strchr(s,'.') && !strchr(s,' ')) return true;
    return false;
}
static char *make_search_url(const char *query){
    char *enc=url_encode(query);
    char *u=(char*)xmalloc(strlen(enc)+64);
    sprintf(u,"https://html.duckduckgo.com/html/?q=%s",enc);
    free(enc); return u;
}
static void addr_set_text(BrowserState *b,const char *s){
    size_t n=strlen(s);
    if(n+1>b->addr_cap){ b->addr_cap=n+64; b->addr_text=(char*)xrealloc(b->addr_text,b->addr_cap); }
    memcpy(b->addr_text,s,n+1); b->addr_len=n; b->addr_cursor=n;
}

static void browser_free_doc(BrowserState *b){
    if(b->root) node_free(b->root); b->root=NULL;
    for(size_t i=0;i<b->nrule;i++) css_rule_free(&b->rules[i]);
    free(b->rules); b->rules=NULL; b->nrule=0;
    free(b->style_text); b->style_text=NULL;
    lctx_free(&b->lctx);
    b->scroll_y=0; b->total_h=0;
}

static void browser_load_html(BrowserState *b, char *html,size_t hlen, const char *url_for_addr){
    browser_free_doc(b);
    char *style=NULL; size_t style_len=0;
    b->root = parse_html(html,hlen,&style,&style_len);
    b->style_text=style;
    b->rules = parse_css(style? style:"",&b->nrule);
    apply_styles(b->root,b->rules,b->nrule);
    gui_build_layout(b->root,&b->lctx,b->win_w,b->win_h);

    int max_y=0;
    for(size_t i=0;i<b->lctx.ncmd;i++) if(b->lctx.cmds[i].type==GCMD_TEXT||b->lctx.cmds[i].type==GCMD_RECT) {
        int bottom = b->lctx.cmds[i].y + b->lctx.cmds[i].h;
        if(bottom>max_y) max_y=bottom;
    }
    if(max_y < b->lctx.content_h) max_y = b->lctx.content_h;

    if(b->lctx.cur_y>max_y) max_y=b->lctx.cur_y;
    b->total_h = max_y - b->lctx.content_y + 20;
    b->scroll_y=0;
    if(url_for_addr){
        free(b->current_url); b->current_url=xstrdup(url_for_addr);
        addr_set_text(b, url_for_addr);
    }
    browser_set_status(b,"Loaded %zu bytes, %zu rules, %zu items", hlen, b->nrule, b->lctx.ncmd);
}

static void browser_navigate(BrowserState *b, const char *input, bool push_hist){
    if(!input||!*input){ browser_set_status(b,"Empty address"); return; }
    char *trimmed=xstrdup(input); str_trim_inplace(trimmed);
    if(!*trimmed){ free(trimmed); return; }

    if(push_hist && b->current_url) { history_push_back(b,b->current_url); history_clear_fwd(b); }
    char *fetch_url_str=NULL;
    char *html=NULL; size_t hlen=0; char *to_free=NULL;
    bool is_file_path=false;

    if(strncmp(trimmed,"about:",6)==0){
        if(strcmp(trimmed,"about:blank")==0){
            const char *blank="<html><body style='margin:20px;font-family:sans-serif'><h1>New Tab</h1><p>Type a URL or search query in the address bar.</p><p>Examples: <b>https://example.com</b> or <b>hello world</b></p></body></html>";
            browser_load_html(b,(char*)blank,strlen(blank),trimmed);
            free(trimmed); return;
        }

    }
    if(strncmp(trimmed,"http://",7)==0||strncmp(trimmed,"https://",8)==0){
        fetch_url_str=xstrdup(trimmed);
    } else {

        FILE *f=fopen(trimmed,"rb");
        if(f){ fclose(f); is_file_path=true; }
        else if(looks_like_url(trimmed) && !strchr(trimmed,' ')){

            fetch_url_str=(char*)xmalloc(strlen(trimmed)+8+1); sprintf(fetch_url_str,"https://%s",trimmed);
        } else {
            fetch_url_str=make_search_url(trimmed);
        }
    }
    if(is_file_path){
        html=read_file(trimmed,&hlen);
        if(!html){
            char errbuf[1024]; snprintf(errbuf,sizeof(errbuf),"<html><body><h1>File not found</h1><p>%s</p></body></html>",trimmed);
            html=xstrdup(errbuf); hlen=strlen(html); to_free=html;
        } else to_free=html;
        browser_load_html(b,html,hlen,trimmed);
        if(to_free) free(to_free);
        free(trimmed); free(fetch_url_str);
        return;
    }
    if(fetch_url_str){
        browser_set_status(b,"Fetching %s ...",fetch_url_str);

        html=fetch_url(fetch_url_str,&hlen);
        if(!html){
            char errbuf[2048];
            snprintf(errbuf,sizeof(errbuf),"<html><body style='margin:20px'><h1>Failed to load</h1><p>URL: %s</p><p>Check internet or try a local file.</p><p><a href='about:blank'>Go to new tab</a></p></body></html>",fetch_url_str);
            html=xstrdup(errbuf); hlen=strlen(html); to_free=html;
        } else to_free=html;
        browser_load_html(b,html,hlen,fetch_url_str);
        if(to_free) free(to_free);
        free(fetch_url_str);
        free(trimmed);
        return;
    }
    free(trimmed);
}

static void browser_relayout(BrowserState *b){
    if(!b->root) return;
    lctx_free(&b->lctx);
    gui_build_layout(b->root,&b->lctx,b->win_w,b->win_h);
    int max_y=0;
    for(size_t i=0;i<b->lctx.ncmd;i++){ int bottom=b->lctx.cmds[i].y + b->lctx.cmds[i].h; if(bottom>max_y) max_y=bottom; }
    if(b->lctx.cur_y>max_y) max_y=b->lctx.cur_y;
    b->total_h = max_y - b->lctx.content_y + 20;
    if(b->scroll_y > b->total_h - b->lctx.content_h) b->scroll_y = b->total_h - b->lctx.content_h;
    if(b->scroll_y<0) b->scroll_y=0;
}

static SDL_Rect addr_rect(BrowserState *b){
    int ax = 128, ay = 6, aw = b->win_w - 128 - 60 - 8, ah = GUI_TOOLBAR_H-12;
    if(aw<80) aw=80;
    return (SDL_Rect){ax,ay,aw,ah};
}
static bool point_in_rect(int x,int y, SDL_Rect r){ return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h; }

static void render_browser(BrowserState *b){
    SDL_Renderer *ren=b->ren;

    SDL_SetRenderDrawColor(ren,255,255,255,255);
    SDL_RenderClear(ren);

    SDL_Color body_bg={255,255,255,255};
    if(b->root){
        Node *body=dom_find_tag(b->root,"body");
        if(body){
            const char *bg=node_style_get(body,"background-color");
            if(!bg) bg=node_style_get(body,"background");
            if(bg) body_bg=hex_to_sdl(bg);
        }
    }

    SDL_Rect toolbar={0,0,b->win_w,GUI_TOOLBAR_H};
    SDL_SetRenderDrawColor(ren, 225,228,232,255);
    SDL_RenderFillRect(ren,&toolbar);
    SDL_SetRenderDrawColor(ren,200,200,200,255);
    SDL_RenderDrawLine(ren,0,GUI_TOOLBAR_H, b->win_w, GUI_TOOLBAR_H);

    struct { SDL_Rect r; const char *label; } btns[4];
    btns[0].r=(SDL_Rect){8,6,GUI_BTN_W, GUI_TOOLBAR_H-12}; btns[0].label="<";
    btns[1].r=(SDL_Rect){8+GUI_BTN_W+GUI_BTN_GAP,6,GUI_BTN_W, GUI_TOOLBAR_H-12}; btns[1].label=">";
    btns[2].r=(SDL_Rect){8+2*(GUI_BTN_W+GUI_BTN_GAP),6,GUI_BTN_W, GUI_TOOLBAR_H-12}; btns[2].label="\xE2\x86\xBB";

    SDL_Rect ar = addr_rect(b);
    btns[3].r=(SDL_Rect){ar.x+ar.w+8,6,52, GUI_TOOLBAR_H-12}; btns[3].label="Go";
    for(int i=0;i<4;i++){
        SDL_SetRenderDrawColor(ren, 250,250,250,255);
        SDL_RenderFillRect(ren,&btns[i].r);
        SDL_SetRenderDrawColor(ren,180,180,180,255);
        SDL_RenderDrawRect(ren,&btns[i].r);
        TTF_Font *bf = gui_get_font(14,true,false,false);
        if(bf){
            SDL_Color col={50,50,50,255};
            if(i==0 && b->back_n==0) col=(SDL_Color){150,150,150,255};
            if(i==1 && b->fwd_n==0) col=(SDL_Color){150,150,150,255};
            SDL_Surface *s=TTF_RenderUTF8_Blended(bf,btns[i].label,col);
            if(s){ SDL_Texture *t=SDL_CreateTextureFromSurface(ren,s); SDL_Rect tr={btns[i].r.x+(btns[i].r.w - s->w)/2, btns[i].r.y+(btns[i].r.h - s->h)/2, s->w,s->h}; SDL_RenderCopy(ren,t,NULL,&tr); SDL_DestroyTexture(t); SDL_FreeSurface(s); }
        }
    }

    SDL_SetRenderDrawColor(ren,255,255,255,255);
    SDL_RenderFillRect(ren,&ar);
    if(b->addr_focused) SDL_SetRenderDrawColor(ren,66,133,244,255); else SDL_SetRenderDrawColor(ren,180,180,180,255);
    SDL_RenderDrawRect(ren,&ar);

    if(b->addr_text){
        TTF_Font *af = gui_get_font(13,false,false,false);
        if(af){
            SDL_Color tc={30,30,30,255};

            int maxw = ar.w - 10;

            int tw=0, th=0; TTF_SizeUTF8(af,b->addr_text,&tw,&th);
            int start_x = ar.x+5;
            int offset=0;
            if(tw>maxw){

                char *pre = xstrndup(b->addr_text,b->addr_cursor);
                int prew=0; TTF_SizeUTF8(af,pre,&prew,NULL); free(pre);
                if(prew > maxw-10) offset = prew - (maxw-10);
            }

            SDL_Rect clip=ar; clip.x+=2; clip.w-=4; clip.y+=2; clip.h-=4;
            SDL_RenderSetClipRect(ren,&clip);
            SDL_Surface *s=TTF_RenderUTF8_Blended(af,b->addr_text,tc);
            if(s){
                SDL_Texture *t=SDL_CreateTextureFromSurface(ren,s);
                SDL_Rect dst={start_x - offset, ar.y + (ar.h - s->h)/2, s->w, s->h};
                SDL_RenderCopy(ren,t,NULL,&dst);

                if(b->addr_focused){
                    Uint32 now=SDL_GetTicks();
                    if((now/500)%2==0){
                        char *pre = xstrndup(b->addr_text,b->addr_cursor);
                        int prew=0; TTF_SizeUTF8(af,pre,&prew,NULL); free(pre);
                        int cx = start_x - offset + prew;
                        SDL_SetRenderDrawColor(ren,30,30,30,255);
                        SDL_RenderDrawLine(ren,cx, ar.y+6, cx, ar.y+ar.h-6);
                    }
                }
                SDL_DestroyTexture(t); SDL_FreeSurface(s);
            }
            SDL_RenderSetClipRect(ren,NULL);
        }
    }

    SDL_Rect content_bg={0,GUI_TOOLBAR_H, b->win_w, b->win_h-GUI_TOOLBAR_H-GUI_STATUS_H};
    SDL_SetRenderDrawColor(ren, body_bg.r, body_bg.g, body_bg.b, 255);
    SDL_RenderFillRect(ren,&content_bg);

    SDL_Rect content_clip={b->lctx.content_x-2, b->lctx.content_y-2, b->lctx.content_w+4, b->lctx.content_h+4};
    SDL_RenderSetClipRect(ren,&content_clip);

    for(size_t i=0;i<b->lctx.ncmd;i++){
        GuiCmd *cmd=&b->lctx.cmds[i];
        if(cmd->type==GCMD_RECT){
            SDL_Rect r={cmd->x, cmd->y - b->scroll_y, cmd->w, cmd->h};

            if(r.y + r.h < b->lctx.content_y || r.y > b->lctx.content_y + b->lctx.content_h) continue;
            SDL_SetRenderDrawColor(ren, cmd->bg.r, cmd->bg.g, cmd->bg.b, 255);
            SDL_RenderFillRect(ren,&r);

            if(cmd->color.a){ SDL_SetRenderDrawColor(ren, cmd->color.r, cmd->color.g, cmd->color.b, 255); SDL_RenderDrawRect(ren,&r); }
        } else if(cmd->type==GCMD_HR){
            SDL_Rect r={cmd->x, cmd->y - b->scroll_y, cmd->w, 1};
            if(r.y < b->lctx.content_y || r.y > b->lctx.content_y + b->lctx.content_h) continue;
            SDL_SetRenderDrawColor(ren,160,160,160,255);
            SDL_RenderDrawLine(ren, r.x, r.y, r.x+r.w, r.y);
        }
    }

    for(size_t i=0;i<b->lctx.ncmd;i++){
        GuiCmd *cmd=&b->lctx.cmds[i];
        if(cmd->type!=GCMD_TEXT) continue;
        int draw_y = cmd->y - b->scroll_y;
        if(draw_y + cmd->h < b->lctx.content_y || draw_y > b->lctx.content_y + b->lctx.content_h) continue;
        if(!cmd->tex){
            TTF_Font *f=gui_get_font(cmd->font_size, cmd->bold, cmd->italic, cmd->mono);
            if(!f) continue;
            SDL_Surface *s=TTF_RenderUTF8_Blended(f, cmd->text, cmd->color);
            if(!s) continue;
            cmd->tex=SDL_CreateTextureFromSurface(ren,s);
            cmd->tex_w=s->w; cmd->tex_h=s->h;
            cmd->w=s->w; cmd->h=s->h;
            SDL_FreeSurface(s);
        }
        if(cmd->tex){
            SDL_Rect dst={cmd->x, draw_y, cmd->tex_w, cmd->tex_h};
            SDL_RenderCopy(ren,cmd->tex,NULL,&dst);
            if(cmd->underline){
                SDL_SetRenderDrawColor(ren, cmd->color.r, cmd->color.g, cmd->color.b, 255);
                SDL_RenderDrawLine(ren, dst.x, dst.y+dst.h-1, dst.x+dst.w, dst.y+dst.h-1);
            }
        }
    }
    SDL_RenderSetClipRect(ren,NULL);

    if(b->total_h > b->lctx.content_h){
        int bar_h = content_bg.h;
        float ratio = (float)b->lctx.content_h / (float)b->total_h;
        int thumb_h = (int)(bar_h * ratio);
        if(thumb_h<24) thumb_h=24;
        if(thumb_h>bar_h) thumb_h=bar_h;
        int max_scroll = b->total_h - b->lctx.content_h;
        int thumb_y = content_bg.y + (max_scroll? (int)((float)b->scroll_y / max_scroll * (bar_h - thumb_h)) : 0);
        SDL_Rect track={b->win_w-10, content_bg.y, 10, bar_h};
        SDL_SetRenderDrawColor(ren,240,240,240,255); SDL_RenderFillRect(ren,&track);
        SDL_Rect thumb={b->win_w-10, thumb_y, 10, thumb_h};
        SDL_SetRenderDrawColor(ren,180,180,180,255); SDL_RenderFillRect(ren,&thumb);
        SDL_SetRenderDrawColor(ren,160,160,160,255); SDL_RenderDrawRect(ren,&thumb);
    }

    SDL_Rect status={0,b->win_h-GUI_STATUS_H,b->win_w,GUI_STATUS_H};
    SDL_SetRenderDrawColor(ren,240,240,240,255); SDL_RenderFillRect(ren,&status);
    SDL_SetRenderDrawColor(ren,200,200,200,255); SDL_RenderDrawLine(ren,0,b->win_h-GUI_STATUS_H,b->win_w,b->win_h-GUI_STATUS_H);
    TTF_Font *sf = gui_get_font(11,false,false,false);
    if(sf){
        const char *msg = b->hover_href[0]? b->hover_href : b->status_msg;
        SDL_Color sc={80,80,80,255};
        SDL_Surface *s=TTF_RenderUTF8_Blended(sf, msg?msg:"", sc);
        if(s){ SDL_Texture *t=SDL_CreateTextureFromSurface(ren,s); SDL_Rect dst={6, b->win_h-GUI_STATUS_H + (GUI_STATUS_H - s->h)/2, s->w, s->h}; if(dst.w> b->win_w-12) dst.w=b->win_w-12; SDL_RenderCopy(ren,t,NULL,&dst); SDL_DestroyTexture(t); SDL_FreeSurface(s); }

        char right[128]; snprintf(right,sizeof(right), "%zu items | %d%%", b->lctx.ncmd, b->total_h? (int)(100*b->scroll_y/(b->total_h - b->lctx.content_h + 1)):0);

    }

    SDL_RenderPresent(ren);
}

static void handle_text_input(BrowserState *b, const char *txt){
    if(!b->addr_focused) return;
    size_t n=strlen(txt);
    if(b->addr_len + n + 1 > b->addr_cap){ b->addr_cap = b->addr_len+n+64; b->addr_text=(char*)xrealloc(b->addr_text,b->addr_cap); }

    memmove(b->addr_text + b->addr_cursor + n, b->addr_text + b->addr_cursor, b->addr_len - b->addr_cursor + 1);
    memcpy(b->addr_text + b->addr_cursor, txt, n);
    b->addr_len+=n; b->addr_cursor+=n;
}
static void handle_key(BrowserState *b, SDL_Keysym key, bool *consumed){
    *consumed=false;
    if(b->addr_focused){
        *consumed=true;
        if(key.sym==SDLK_RETURN || key.sym==SDLK_KP_ENTER){
            browser_navigate(b,b->addr_text,true);
            b->addr_focused=false; SDL_StopTextInput();
        } else if(key.sym==SDLK_ESCAPE){
            b->addr_focused=false; SDL_StopTextInput();
        } else if(key.sym==SDLK_BACKSPACE){
            if(b->addr_cursor>0){

                memmove(b->addr_text + b->addr_cursor -1, b->addr_text + b->addr_cursor, b->addr_len - b->addr_cursor +1);
                b->addr_len--; b->addr_cursor--;
            }
        } else if(key.sym==SDLK_DELETE){
            if(b->addr_cursor < b->addr_len){
                memmove(b->addr_text + b->addr_cursor, b->addr_text + b->addr_cursor +1, b->addr_len - b->addr_cursor);
                b->addr_len--;
            }
        } else if(key.sym==SDLK_LEFT){
            if(b->addr_cursor>0) b->addr_cursor--;
            if(key.mod & KMOD_CTRL) { while(b->addr_cursor>0 && b->addr_text[b->addr_cursor-1]==' ') b->addr_cursor--; while(b->addr_cursor>0 && b->addr_text[b->addr_cursor-1]!=' ') b->addr_cursor--; }
        } else if(key.sym==SDLK_RIGHT){
            if(b->addr_cursor < b->addr_len) b->addr_cursor++;
        } else if(key.sym==SDLK_HOME){ b->addr_cursor=0; }
        else if(key.sym==SDLK_END){ b->addr_cursor=b->addr_len; }
        else if((key.mod & KMOD_CTRL) && key.sym==SDLK_a){ b->addr_select_all=true; }
        else if((key.mod & KMOD_CTRL) && key.sym==SDLK_c){ SDL_SetClipboardText(b->addr_text); }
        else if((key.mod & KMOD_CTRL) && key.sym==SDLK_v){
            char *clip=SDL_GetClipboardText(); if(clip){ handle_text_input(b,clip); SDL_free(clip); }
        } else if(key.sym==SDLK_TAB){
            b->addr_focused=false; SDL_StopTextInput();
        } else {
            *consumed=false;
        }
        return;
    }

    if(key.sym==SDLK_F5 || (key.sym==SDLK_r && (key.mod & KMOD_CTRL))){
        if(b->current_url) browser_navigate(b,b->current_url,false);
        *consumed=true;
    } else if(key.sym==SDLK_F6 || (key.mod & KMOD_CTRL && key.sym==SDLK_l)){
        b->addr_focused=true; SDL_StartTextInput(); b->addr_cursor=b->addr_len;
        *consumed=true;
    } else if(key.sym==SDLK_BACKSPACE || (key.mod & KMOD_ALT && key.sym==SDLK_LEFT)){
        char *prev=history_pop_back(b);
        if(prev){ if(b->current_url) history_push_fwd(b,b->current_url); browser_navigate(b,prev,false); free(b->current_url); b->current_url=prev; addr_set_text(b,prev); } else free(prev);
        *consumed=true;
    }
}

static int run_gui(const char *initial_url){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ fprintf(stderr,"SDL_Init: %s\n", SDL_GetError()); return 1; }
    if(TTF_Init()!=0){ fprintf(stderr,"TTF_Init: %s\n", TTF_GetError()); SDL_Quit(); return 1; }
    BrowserState b={0};
    b.win_w=1024; b.win_h=720;
    b.win = SDL_CreateWindow("C Web Browser", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, b.win_w,b.win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if(!b.win){ fprintf(stderr,"CreateWindow: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return 1; }
    b.ren = SDL_CreateRenderer(b.win,-1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!b.ren){ b.ren = SDL_CreateRenderer(b.win,-1, SDL_RENDERER_SOFTWARE); if(!b.ren){ fprintf(stderr,"Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(b.win); TTF_Quit(); SDL_Quit(); return 1; } }
    b.addr_cap=1024; b.addr_text=(char*)xmalloc(b.addr_cap); b.addr_text[0]=0; b.addr_len=0; b.addr_cursor=0;
    b.needs_relayout=false; b.running=true;
    b.current_url=xstrdup(initial_url?initial_url:"about:blank");
    addr_set_text(&b,b.current_url);
    browser_set_status(&b,"Ready");
    gui_find_font_paths();

    browser_navigate(&b, initial_url?initial_url:"about:blank", false);

    Uint32 last_ticks=SDL_GetTicks();
    while(b.running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){ b.running=false; }
            else if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_RESIZED){
                b.win_w=e.window.data1; b.win_h=e.window.data2;
                browser_relayout(&b);
            }
            else if(e.type==SDL_MOUSEBUTTONDOWN){
                int mx=e.button.x, my=e.button.y;
                SDL_Rect ar=addr_rect(&b);
                SDL_Rect back_r={8,6,GUI_BTN_W,GUI_TOOLBAR_H-12}, fwd_r={8+GUI_BTN_W+GUI_BTN_GAP,6,GUI_BTN_W,GUI_TOOLBAR_H-12}, reload_r={8+2*(GUI_BTN_W+GUI_BTN_GAP),6,GUI_BTN_W,GUI_TOOLBAR_H-12}, go_r={ar.x+ar.w+8,6,52,GUI_TOOLBAR_H-12};
                if(e.button.button==SDL_BUTTON_LEFT){
                    if(point_in_rect(mx,my,back_r)){
                        char *prev=history_pop_back(&b);
                        if(prev){ if(b.current_url) history_push_fwd(&b,b.current_url); browser_navigate(&b,prev,false); free(prev); }
                    } else if(point_in_rect(mx,my,fwd_r)){
                        char *nxt=history_pop_fwd(&b);
                        if(nxt){ if(b.current_url) history_push_back(&b,b.current_url); browser_navigate(&b,nxt,false); free(nxt); }
                    } else if(point_in_rect(mx,my,reload_r)){
                        if(b.current_url) browser_navigate(&b,b.current_url,false);
                    } else if(point_in_rect(mx,my,ar)){
                        b.addr_focused=true; SDL_StartTextInput();

                        TTF_Font *af=gui_get_font(13,false,false,false);
                        if(af){
                            int rel = mx - (ar.x+5);

                            int best=0, bestdist=10000;
                            for(size_t i=0;i<=b.addr_len;i++){
                                char *pre=xstrndup(b.addr_text,i); int w=0; TTF_SizeUTF8(af,pre,&w,NULL); free(pre);
                                int d=abs(w - rel); if(d<bestdist){bestdist=d; best=(int)i;}
                            }
                            b.addr_cursor=best;
                        }
                    } else if(point_in_rect(mx,my,go_r)){
                        browser_navigate(&b,b.addr_text,true);
                        b.addr_focused=false; SDL_StopTextInput();
                    } else if(my > GUI_TOOLBAR_H && my < b.win_h - GUI_STATUS_H){

                        if(b.addr_focused){ b.addr_focused=false; SDL_StopTextInput(); }
                        int content_my = my + b.scroll_y;
                        bool hit=false;
                        for(size_t i=0;i<b.lctx.nlink;i++){
                            SDL_Rect r=b.lctx.links[i].r;

                            if(mx >= r.x && mx < r.x+r.w && content_my >= r.y && content_my < r.y+r.h){
                                browser_navigate(&b,b.lctx.links[i].href,true);
                                hit=true; break;
                            }
                        }
                        if(!hit){

                            GuiCmd *closest=NULL; int bestd=10000;
                            for(size_t i=0;i<b.lctx.ncmd;i++){ GuiCmd *cm=&b.lctx.cmds[i]; if(cm->type!=GCMD_TEXT) continue; SDL_Rect r={cm->x, cm->y, cm->w, cm->h}; if(mx>=r.x && mx<r.x+r.w && content_my>=r.y && content_my<r.y+r.h){ closest=cm; break; } int cx=cm->x+cm->w/2, cy=cm->y+cm->h/2; int d=abs(cx-mx)+abs(cy-content_my); if(d<bestd){bestd=d; closest=cm;}}
                            if(closest){
                                browser_set_status(&b,"<%s> %dpx %s%s%s %s", closest->text?closest->text:"", closest->font_size, closest->bold?"bold ":"", closest->italic?"italic ":"", closest->mono?"mono ":"", closest->href?closest->href:"");
                            }
                        }
                    } else {
                        if(b.addr_focused){ b.addr_focused=false; SDL_StopTextInput(); }
                    }
                }
            }
            else if(e.type==SDL_MOUSEWHEEL){
                int dy = e.wheel.y;

                b.scroll_y -= dy * 40;
                if(b.scroll_y<0) b.scroll_y=0;
                int max_scroll = b.total_h - b.lctx.content_h;
                if(max_scroll<0) max_scroll=0;
                if(b.scroll_y>max_scroll) b.scroll_y=max_scroll;
            }
            else if(e.type==SDL_MOUSEMOTION){
                int mx=e.motion.x, my=e.motion.y;
                b.hover_href[0]=0;
                if(my > GUI_TOOLBAR_H && my < b.win_h - GUI_STATUS_H){
                    int content_my = my + b.scroll_y;
                    for(size_t i=0;i<b.lctx.nlink;i++){
                        SDL_Rect r=b.lctx.links[i].r;
                        if(mx >= r.x && mx < r.x+r.w && content_my >= r.y && content_my < r.y+r.h){
                            strncpy(b.hover_href,b.lctx.links[i].href,sizeof(b.hover_href)-1);
                            break;
                        }
                    }
                }
            }
            else if(e.type==SDL_TEXTINPUT){
                handle_text_input(&b, e.text.text);
            }
            else if(e.type==SDL_KEYDOWN){
                bool cons=false;
                handle_key(&b, e.key.keysym, &cons);
                if(!cons){
                    if(e.key.keysym.sym==SDLK_UP){ b.scroll_y-=40; if(b.scroll_y<0) b.scroll_y=0; }
                    else if(e.key.keysym.sym==SDLK_DOWN){ b.scroll_y+=40; int maxs=b.total_h - b.lctx.content_h; if(maxs<0) maxs=0; if(b.scroll_y>maxs) b.scroll_y=maxs; }
                    else if(e.key.keysym.sym==SDLK_PAGEUP){ b.scroll_y-= b.lctx.content_h -40; if(b.scroll_y<0) b.scroll_y=0; }
                    else if(e.key.keysym.sym==SDLK_PAGEDOWN){ b.scroll_y+= b.lctx.content_h -40; int maxs=b.total_h - b.lctx.content_h; if(maxs<0) maxs=0; if(b.scroll_y>maxs) b.scroll_y=maxs; }
                    else if(e.key.keysym.sym==SDLK_HOME){ b.scroll_y=0; }
                    else if(e.key.keysym.sym==SDLK_END){ int maxs=b.total_h - b.lctx.content_h; if(maxs<0) maxs=0; b.scroll_y=maxs; }
                }
            }
        }
        Uint32 now=SDL_GetTicks();
        if(now - last_ticks >= 16){
            render_browser(&b);
            last_ticks=now;
        } else {

            render_browser(&b);
            SDL_Delay(5);
        }
    }
    browser_free_doc(&b);
    free(b.current_url); free(b.addr_text);
    for(size_t i=0;i<b.back_n;i++) free(b.back_stack[i]); free(b.back_stack);
    for(size_t i=0;i<b.fwd_n;i++) free(b.fwd_stack[i]); free(b.fwd_stack);
    gui_free_fonts();
    SDL_DestroyRenderer(b.ren); SDL_DestroyWindow(b.win);
    TTF_Quit(); SDL_Quit();
    return 0;
}

#else
static int run_gui(const char *u){ (void)u; fprintf(stderr,"GUI not available: SDL2/SDL_ttf not found at compile time.\nInstall sdl2 + sdl2_ttf via 'brew install sdl2 sdl2_ttf' and rebuild with 'make gui'\n"); return 1; }
#endif

static const char *TEST_HTML =
"<!DOCTYPE html>\n"
"<html><head><style>\n"
"  body { color: #333; font-family: Helvetica, Arial; margin: 8px; }\n"
"  h1 { color: red; font-size: 32px; }\n"
"  .note { background: yellow; padding: 4px; }\n"
"  #main p { line-height: 1.4; }\n"
"  /* comment should be stripped */\n"
"  p.note::before { content: \">>\"; }\n"
"</style></head>\n"
"<body id=\"main\">\n"
"  <h1>Title &amp; Intro</h1>\n"
"  <!-- comment between -->\n"
"  <p class=\"note\" style=\"color: blue;\">Hello <b>bold</b> &lt;world&gt; and <i>italic</i> text.</p>\n"
"  <p>Second paragraph with <a href=\"#\">link</a> and<br>line break.</p>\n"
"  <ul><li>item 1</li><li>item 2</li></ul>\n"
"  <div><p>Nested <span>inline <b>deep</b></span> text</p></div>\n"
"  <img src=\"x.png\" alt=test>\n"
"  <hr>\n"
"  <script>var x = \"<div>fake</div>\";</script>\n"
"  <svg><circle r=10></svg><!-- svg skipped -->\n"
"</body></html>\n";

static void print_usage(const char *prog) {
    fprintf(stderr,"Usage: %s [url|file] [options]\n", prog);
    fprintf(stderr,"  url starting with http:// or https:// will be fetched via libcurl\n");
    fprintf(stderr,"  file path will be read from disk\n");
    fprintf(stderr,"  no argument: parse built-in TEST_HTML demo (CLI)\n");
    fprintf(stderr,"Options:\n");
    fprintf(stderr,"  --gui [url]        Launch SDL2 windowed browser (address bar + computed rendering)\n");
    fprintf(stderr,"  --cli              Force CLI mode (default when no --gui)\n");
    fprintf(stderr,"  --dump / --no-dump  Show/hide DOM dump in CLI mode\n");
    fprintf(stderr,"  --query selector   Run selector query in CLI mode\n");
    fprintf(stderr,"Window controls (GUI):\n");
    fprintf(stderr,"  Type URL/search in top bar, Enter to go; Back/Forward/Reload buttons; wheel/page keys to scroll; click links; hover shows URL; Ctrl+L focuses bar; F5 reloads\n");
}

int main(int argc, char **argv) {
    const char *url_or_file=NULL;
    const char *query=NULL;
    bool dump=true;
    bool force_gui=false;
    bool force_cli=false;
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) { print_usage(argv[0]); return 0; }
        else if (strcmp(argv[i],"--gui")==0) { force_gui=true; if(i+1<argc && argv[i+1][0]!='-') url_or_file=argv[++i]; }
        else if (strcmp(argv[i],"--cli")==0) force_cli=true;
        else if (strcmp(argv[i],"--query")==0 && i+1<argc) query=argv[++i];
        else if (strcmp(argv[i],"--no-dump")==0) dump=false;
        else if (!url_or_file) url_or_file=argv[i];
        else { fprintf(stderr,"unknown arg %s\n", argv[i]); print_usage(argv[0]); return 1; }
    }
    if(force_gui){
        const char *start = url_or_file ? url_or_file : "about:blank";
        return run_gui(start);
    }
    if(!force_cli){

    }

    char *html=NULL;
    size_t hlen=0;
    char *to_free=NULL;
    if (!url_or_file) {
        html=(char*)TEST_HTML;
        hlen=strlen(html);
        printf("=== C Browser (improved DOM parser) ===\n");
        printf("Parsing built-in TEST_HTML (%zu bytes) ...\n\n", hlen);
    } else if (strncmp(url_or_file,"http://",7)==0 || strncmp(url_or_file,"https://",8)==0) {
        printf("Fetching %s ...\n", url_or_file);
        html=fetch_url(url_or_file, &hlen);
        if (!html) { fprintf(stderr,"fetch failed, falling back to TEST_HTML\n"); html=(char*)TEST_HTML; hlen=strlen(html); }
        else to_free=html;
    } else {
        html=read_file(url_or_file, &hlen);
        if (!html) { fprintf(stderr,"read failed, using TEST_HTML\n"); html=(char*)TEST_HTML; hlen=strlen(html); }
        else to_free=html;
    }

    char *style_text=NULL;
    size_t style_len=0;
    Node *root = parse_html(html, hlen, &style_text, &style_len);
    if (!root) { fprintf(stderr,"parse failed\n"); return 1; }

    size_t rule_count=0;
    CSSRule *rules = parse_css(style_text ? style_text : "", &rule_count);
    printf("Collected %zu bytes of <style> CSS, parsed %zu rules\n", style_len, rule_count);
    apply_styles(root, rules, rule_count);

    if (dump) {
        printf("\n--- DOM tree (with computed styles) ---\n");
        dom_walk_iterative(root, dump_cb, stdout);

        printf("\n--- Text content via dom_walk collection ---\n");
        char *tc = node_text_content(root);
        printf("\"%s\"\n", tc);
        free(tc);

        printf("\n--- Computed font sizes ---\n");
        dom_walk(root, font_cb, NULL);

        printf("\n--- Query selector demo (improved descendant matching) ---\n");
        const char *tests[] = {"h1", "p.note", "#main p", "body #main", "ul li", "div p", "b", "*", NULL};
        for (int i=0; tests[i]; i++) {
            Node **out=NULL; size_t cnt=0, cap=0;
            query_collect(root, tests[i], &out, &cnt, &cap);
            printf(" selector \"%s\" -> %zu matches", tests[i], cnt);
            if (cnt) {
                printf(": ");
                for (size_t k=0;k<cnt && k<3; k++) {
                    if (out[k]->type==NODE_TEXT) printf("\"%s\" ", out[k]->text);
                    else printf("<%s> ", out[k]->tag);
                }
                if (cnt>3) printf("... ");
            }
            printf("\n");
            free(out);
        }
        if (query) {
            Node **out=NULL; size_t cnt=0, cap=0;
            query_collect(root, query, &out, &cnt, &cap);
            printf("\n--- Custom query \"%s\" -> %zu matches ---\n", query, cnt);
            for (size_t i=0;i<cnt;i++) {
                Node *n=out[i];
                char *tc2=node_text_content(n);
                printf("  [%zu] <%s> text=\"%s\"\n", i, n->tag, tc2);
                free(tc2);
            }
            free(out);
        }

        Node *body = dom_find_tag(root, "body");
        if (body) {
            const char *bg = node_style_get(body, "background-color");
            if (!bg) bg = node_style_get(body, "background");
            char *norm = normalize_color(bg ? bg : "");
            printf("\nBody background: %s -> %s\n", bg?bg:"(none)", norm);
            free(norm);
        }

        printf("\n--- Box-model layout (containing=996px assumed GUI) ---\n");
        Node **all=NULL; size_t all_cnt=0, all_cap=0;
        query_collect(root,"*", &all, &all_cnt, &all_cap);
        int gui_containing = 996;
        for(size_t i=0;i<all_cnt;i++){
            Node *n=all[i];
            if(n->type!=NODE_ELEMENT) continue;
            if(!is_block_tag(n->tag) && !streqi(n->tag,"div")) continue;

            BoxModel bm;
            compute_box_model(n, gui_containing, &bm);
            const char *ws=node_style_get(n,"width");
            const char *ms=node_style_get(n,"margin");
            const char *mls=node_style_get(n,"margin-left");
            const char *mrs=node_style_get(n,"margin-right");

            if(!(ws||ms||mls||mrs|| streqi(n->tag,"body") || streqi(n->tag,"div"))) continue;
            const char *id=node_get_attr(n,"id");
            printf(" <%s%s%s> containing=%d width=%s (%d%s) ml=%d%s mr=%d%s pl=%d pr=%d bl=%d br=%d => content_w=%d outer_w=%d box-sizing=%s\n",
                n->tag, id? " id=":"", id?id:"",
                gui_containing,
                ws?ws:"(auto)", bm.width, bm.width_auto?" auto":"",
                bm.ml, bm.ml_auto?" auto":"", bm.mr, bm.mr_auto?" auto":"",
                bm.pl, bm.pr, bm.bl, bm.br,
                bm.content_w, bm.outer_w, bm.border_box?"border-box":"content-box");

            int eff_cx = 8;
            int outer_x = eff_cx + bm.ml;
            printf("    -> outer_x=%d content_x=%d (center offset ml=%d, remaining=%d)\n", outer_x, outer_x+bm.bl+bm.pl, bm.ml, gui_containing - bm.outer_w - (bm.ml_auto?0:bm.ml) - (bm.mr_auto?0:bm.mr));
        }

        Node *a_node = dom_find_by_id(root,"a");
        if(a_node){
            BoxModel abm; compute_box_model(a_node, gui_containing, &abm);
            Node *b_node = dom_find_by_id(root,"b");
            if(b_node){
                BoxModel bbm; compute_box_model(b_node, abm.content_w, &bbm);
                printf("   nesting: #a content_w=%d => #b width 50%% => %d (expected %d) ml=%d mr=%d centered\n", abm.content_w, bbm.width, abm.content_w/2, bbm.ml, bbm.mr);
            }
        }
        free(all);
#ifdef HAVE_SDL

        {
            LayoutCtx tl;
            gui_build_layout(root, &tl, 1024, 720);
            printf("\n--- Layout smoke test (1024x720) ---\n");
            printf(" cmds=%zu links=%zu total_h=%d cur_y=%d\n", tl.ncmd, tl.nlink, tl.cur_y - tl.content_y, tl.cur_y);

            for(size_t k=0;k<tl.ncmd && k<8;k++){
                if(tl.cmds[k].type==GCMD_TEXT && tl.cmds[k].text){
                    printf("  cmd[%zu] TEXT \"%s\" x=%d y=%d w=%d bg %s\n", k, tl.cmds[k].text, tl.cmds[k].x, tl.cmds[k].y, tl.cmds[k].w, tl.cmds[k].href?tl.cmds[k].href:"-");
                } else if(tl.cmds[k].type==GCMD_RECT){
                    printf("  cmd[%zu] RECT x=%d y=%d w=%d h=%d bg %02x%02x%02x\n", k, tl.cmds[k].x, tl.cmds[k].y, tl.cmds[k].w, tl.cmds[k].h, tl.cmds[k].bg.r, tl.cmds[k].bg.g, tl.cmds[k].bg.b);
                }
            }
            lctx_free(&tl);
        }
#endif
    }

    for (size_t i=0;i<rule_count;i++) css_rule_free(&rules[i]);
    free(rules);
    free(style_text);
    if (to_free) free(to_free);

    node_free(root);
    return 0;
}
