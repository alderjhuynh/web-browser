#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "utils.h"
#include "dom.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>

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

Node *parse_html(const char *src, size_t len, char **out_style, size_t *out_style_len) {
    Parser p;
    parser_init(&p, src, len);
    while (p.pos < p.len) {

        if (p.script_depth > 0) {
            if (p.pos + 9 <= p.len && strncasecmp(p.src + p.pos, "</script", 8) == 0) {
                size_t j = p.pos + 8;
                while (j < p.len && p.src[j] != '>') j++;
                if (j < p.len) j++;
                p.pos = j;
                p.script_depth--;
                continue;
            }
            p.pos++;
            continue;
        }

        if (p.in_style) {
            if (p.pos + 8 <= p.len && strncasecmp(p.src + p.pos, "</style", 7) == 0) {
                size_t j = p.pos + 7;
                while (j < p.len && p.src[j] != '>') j++;
                if (j < p.len) j++;
                p.pos = j;
                p.in_style = false;
                continue;
            }
            if (p.src[p.pos] != '<') {
                size_t start = p.pos;
                while (p.pos < p.len && p.src[p.pos] != '<') p.pos++;
                size_t tlen = p.pos - start;
                if (tlen) {
                    char *raw = xstrndup(p.src+start, tlen);
                    char *decoded = decode_entities(raw);
                    free(raw);
                    parser_append_style(&p, decoded, strlen(decoded));
                    free(decoded);
                }
                continue;
            }

            parser_append_style(&p, "<", 1);
            p.pos++;
            continue;
        }
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
