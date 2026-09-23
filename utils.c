#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "oom malloc %zu\n", n); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "oom realloc %zu\n", n); exit(1); }
    return q;
}
void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) { fprintf(stderr, "oom calloc\n"); exit(1); }
    return p;
}
char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n+1);
    memcpy(p, s, n+1);
    return p;
}
char *xstrndup(const char *s, size_t n) {
    char *p = (char*)xmalloc(n+1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
char *str_lower_dup(const char *s) {
    char *p = xstrdup(s);
    for (char *c = p; *c; c++) *c = (char)tolower((unsigned char)*c);
    return p;
}
void str_trim_inplace(char *s) {
    char *a = s;
    while (*a && isspace((unsigned char)*a)) a++;
    if (a != s) memmove(s, a, strlen(a)+1);
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}
bool streqi(const char *a, const char *b) {
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

char *decode_entities(const char *src) {
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
