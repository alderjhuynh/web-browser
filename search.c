#define _POSIX_C_SOURCE 200809L
#include "search.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static char *g_rc_keys[16], *g_rc_vals[16];
static int g_rc_n = 0;
static bool g_rc_loaded = false;

static void load_rc(void){
    if(g_rc_loaded) return;
    g_rc_loaded = true;
    const char *home = getenv("HOME");
    if(!home) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/.browserrc", home);
    FILE *f = fopen(path, "r");
    if(!f) return;
    char line[1024];
    while(g_rc_n < 16 && fgets(line, sizeof(line), f)){
        char *p = line;
        while(*p && isspace((unsigned char)*p)) p++;
        if(*p == '#' || !*p) continue;
        char *eq = strchr(p, '=');
        if(!eq) continue;
        *eq = 0;
        char *k = p, *v = eq + 1;
        str_trim_inplace(k); str_trim_inplace(v);
        if(!*k || !*v) continue;
        g_rc_keys[g_rc_n] = xstrdup(k);
        g_rc_vals[g_rc_n] = xstrdup(v);
        g_rc_n++;
    }
    fclose(f);
}

static const char *cfg(const char *env_name, const char *rc_key){
    const char *e = getenv(env_name);
    if(e && *e) return e;
    load_rc();
    for(int i = 0; i < g_rc_n; i++)
        if(strcasecmp(g_rc_keys[i], rc_key) == 0) return g_rc_vals[i];
    return NULL;
}

char *search_url_encode(const char *s){
    size_t len = strlen(s);
    char *out = (char*)xmalloc(len * 3 + 1);
    size_t oi = 0;
    for(size_t i = 0; i < len; i++){
        unsigned char c = (unsigned char)s[i];
        if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[oi++] = (char)c;
        else if(c == ' ') out[oi++] = '+';
        else { out[oi++] = '%'; out[oi++] = "0123456789ABCDEF"[c >> 4]; out[oi++] = "0123456789ABCDEF"[c & 15]; }
    }
    out[oi] = 0;
    return out;
}

static const char *brave_base(void){
    const char *b = getenv("BRAVE_API_BASE");
    return (b && *b) ? b : "https://api.search.brave.com/res/v1/web/search";
}

static char *fill_template(const char *tmpl, const char *enc){
    const char *p = strstr(tmpl, "%s");
    if(!p) {
        size_t n = strlen(tmpl) + strlen(enc) + 8;
        char *u = (char*)xmalloc(n);
        snprintf(u, n, "%s%sq=%s", tmpl, strchr(tmpl, '?') ? "&" : "?", enc);
        return u;
    }
    size_t n = strlen(tmpl) + strlen(enc) + 1;
    char *u = (char*)xmalloc(n);
    size_t head = (size_t)(p - tmpl);
    memcpy(u, tmpl, head);
    memcpy(u + head, enc, strlen(enc));
    strcpy(u + head + strlen(enc), p + 2);
    return u;
}

char *search_build_url(const char *query){
    const char *q = query;
    const char *force = NULL;
    if(strncasecmp(q, "!ddg ", 5) == 0){ force = "ddg"; q += 5; }
    else if(strncasecmp(q, "!brave ", 7) == 0){ force = "brave"; q += 7; }
    while(*q && isspace((unsigned char)*q)) q++;

    char *enc = search_url_encode(q);
    char *u = NULL;
    const char *key = cfg("BRAVE_API_KEY", "brave_api_key");
    const char *tmpl = cfg("BROWSER_SEARCH_URL", "search_url");

    if(force && strcmp(force, "ddg") == 0){
    } else if(key || (force && strcmp(force, "brave") == 0)){
        size_t n = strlen(brave_base()) + strlen(enc) + 32;
        u = (char*)xmalloc(n);
        snprintf(u, n, "%s?q=%s&count=20", brave_base(), enc);
    } else if(tmpl){
        u = fill_template(tmpl, enc);
    }
    if(!u){
        size_t n = strlen(enc) + 64;
        u = (char*)xmalloc(n);
        snprintf(u, n, "https://html.duckduckgo.com/html/?q=%s", enc);
    }
    free(enc);
    return u;
}

const char *search_auth_header_for_url(const char *url){
    static char hdr[600];
    if(!url) return NULL;
    const char *base = brave_base();
    if(strncmp(url, base, strlen(base)) != 0) return NULL;
    const char *key = cfg("BRAVE_API_KEY", "brave_api_key");
    if(!key) return NULL;
    snprintf(hdr, sizeof(hdr), "X-Subscription-Token: %s", key);
    return hdr;
}

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal {
    JType t;
    char *s;
    struct JVal **kids;
    char **keys;
    size_t n, cap;
} JVal;

static void jfree(JVal *v){
    if(!v) return;
    for(size_t i = 0; i < v->n; i++){ jfree(v->kids[i]); if(v->keys) free(v->keys[i]); }
    free(v->kids); free(v->keys); free(v->s); free(v);
}
static JVal *jnew(JType t){ JVal *v = (JVal*)xcalloc(1, sizeof(JVal)); v->t = t; return v; }
static void jpush(JVal *p, char *key, JVal *c){
    if(p->n >= p->cap){
        p->cap = p->cap ? p->cap * 2 : 8;
        p->kids = (JVal**)xrealloc(p->kids, p->cap * sizeof(JVal*));
        p->keys = (char**)xrealloc(p->keys, p->cap * sizeof(char*));
    }
    p->kids[p->n] = c; p->keys[p->n] = key; p->n++;
}
static const JVal *jget(const JVal *o, const char *key){
    if(!o || o->t != J_OBJ) return NULL;
    for(size_t i = 0; i < o->n; i++) if(o->keys[i] && strcmp(o->keys[i], key) == 0) return o->kids[i];
    return NULL;
}
static const char *jstr(const JVal *v){ return (v && v->t == J_STR && v->s) ? v->s : NULL; }

typedef struct { const char *p, *end; int depth; } JP;
static void jws(JP *j){ while(j->p < j->end && isspace((unsigned char)*j->p)) j->p++; }

static int hexv(char c){ if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='f') return c-'a'+10; if(c>='A'&&c<='F') return c-'A'+10; return -1; }
static bool jhex4(const char *p, const char *end, unsigned *out){
    if(end - p < 4) return false;
    unsigned v = 0;
    for(int i = 0; i < 4; i++){ int h = hexv(p[i]); if(h < 0) return false; v = v * 16 + (unsigned)h; }
    *out = v; return true;
}
static size_t put_utf8(char *o, unsigned cp){
    if(cp < 0x80){ o[0] = (char)cp; return 1; }
    if(cp < 0x800){ o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if(cp < 0x10000){ o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

static char *jparse_string(JP *j){
    if(j->p >= j->end || *j->p != '"') return NULL;
    j->p++;
    size_t cap = (size_t)(j->end - j->p) + 1;
    char *out = (char*)xmalloc(cap);
    size_t oi = 0;
    while(j->p < j->end && *j->p != '"'){
        if(*j->p == '\\' && j->p + 1 < j->end){
            j->p++;
            switch(*j->p){
                case 'n': out[oi++] = '\n'; break;
                case 't': out[oi++] = '\t'; break;
                case 'r': out[oi++] = '\r'; break;
                case 'b': out[oi++] = '\b'; break;
                case 'f': out[oi++] = '\f'; break;
                case 'u': {
                    unsigned cp;
                    if(!jhex4(j->p + 1, j->end, &cp)){ free(out); return NULL; }
                    j->p += 4;
                    if(cp >= 0xD800 && cp < 0xDC00 && j->p + 6 < j->end && j->p[1] == '\\' && j->p[2] == 'u'){
                        unsigned lo;
                        if(jhex4(j->p + 3, j->end, &lo) && lo >= 0xDC00 && lo < 0xE000){
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            j->p += 6;
                        }
                    }
                    if(cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
                    oi += put_utf8(out + oi, cp);
                    break;
                }
                default: out[oi++] = *j->p; break;
            }
            j->p++;
        } else out[oi++] = *j->p++;
    }
    if(j->p >= j->end){ free(out); return NULL; }
    j->p++;
    out[oi] = 0;
    return out;
}

static JVal *jparse_value(JP *j){
    if(++j->depth > 64) return NULL;
    JVal *res = NULL;
    jws(j);
    if(j->p >= j->end) { j->depth--; return NULL; }
    char c = *j->p;
    if(c == '"'){
        char *s = jparse_string(j);
        if(s){ res = jnew(J_STR); res->s = s; }
    } else if(c == '{' || c == '['){
        bool obj = (c == '{');
        char close = obj ? '}' : ']';
        res = jnew(obj ? J_OBJ : J_ARR);
        j->p++; jws(j);
        if(j->p < j->end && *j->p == close){ j->p++; }
        else for(;;){
            char *key = NULL;
            jws(j);
            if(obj){
                key = jparse_string(j);
                if(!key){ jfree(res); res = NULL; break; }
                jws(j);
                if(j->p >= j->end || *j->p != ':'){ free(key); jfree(res); res = NULL; break; }
                j->p++;
            }
            JVal *kid = jparse_value(j);
            if(!kid){ free(key); jfree(res); res = NULL; break; }
            jpush(res, key, kid);
            jws(j);
            if(j->p < j->end && *j->p == ','){ j->p++; continue; }
            if(j->p < j->end && *j->p == close){ j->p++; break; }
            jfree(res); res = NULL; break;
        }
    } else if(!strncmp(j->p, "true", 4) || !strncmp(j->p, "null", 4)){
        res = jnew(c == 't' ? J_BOOL : J_NULL); j->p += 4;
    } else if(!strncmp(j->p, "false", 5)){
        res = jnew(J_BOOL); j->p += 5;
    } else if(c == '-' || isdigit((unsigned char)c)){
        res = jnew(J_NUM);
        while(j->p < j->end && (isdigit((unsigned char)*j->p) || strchr("+-.eE", *j->p))) j->p++;
    }
    j->depth--;
    return res;
}

typedef struct { char *d; size_t n, cap; } Buf;
static void bput(Buf *b, const char *s, size_t len){
    if(b->n + len + 1 > b->cap){ b->cap = (b->n + len + 1) * 2 + 1024; b->d = (char*)xrealloc(b->d, b->cap); }
    memcpy(b->d + b->n, s, len); b->n += len; b->d[b->n] = 0;
}
static void bstr(Buf *b, const char *s){ bput(b, s, strlen(s)); }
static void bescape(Buf *b, const char *s){
    for(; *s; s++){
        switch(*s){
            case '&': bstr(b, "&amp;"); break;
            case '<': bstr(b, "&lt;"); break;
            case '>': bstr(b, "&gt;"); break;
            case '"': bstr(b, "&quot;"); break;
            default: bput(b, s, 1);
        }
    }
}
static void bplain(Buf *b, const char *s){
    char *tmp = xstrdup(s);
    size_t oi = 0;
    bool in_tag = false;
    for(size_t i = 0; tmp[i]; i++){
        if(tmp[i] == '<') in_tag = true;
        else if(tmp[i] == '>' && in_tag) in_tag = false;
        else if(!in_tag) tmp[oi++] = tmp[i];
    }
    tmp[oi] = 0;
    char *dec = decode_entities(tmp);
    bescape(b, dec ? dec : tmp);
    free(dec); free(tmp);
}

static bool safe_http_url(const char *u){
    return u && (strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0);
}

char *search_render_json(const char *url, const char *body, size_t len, size_t *out_len){
    if(!url || !(strstr(url, "format=json") || strncmp(url, brave_base(), strlen(brave_base())) == 0)) return NULL;
    const char *p = body;
    while(len && isspace((unsigned char)*p)){ p++; len--; }
    if(!len || *p != '{') return NULL;

    JP j = { p, p + len, 0 };
    JVal *root = jparse_value(&j);
    if(!root || root->t != J_OBJ){ jfree(root); return NULL; }

    const JVal *results = NULL;
    const JVal *web = jget(root, "web");
    if(web) results = jget(web, "results");
    if(!results) results = jget(root, "results");
    if(!results || results->t != J_ARR){ jfree(root); return NULL; }

    const JVal *qv = jget(jget(root, "query"), "original");
    const char *qtxt = jstr(qv);
    if(!qtxt) qtxt = jstr(jget(root, "query"));

    Buf b = {0};
    bstr(&b, "<!DOCTYPE html><html><head><title>Search results</title><style>"
             "body{font-family:sans-serif;margin:24px;color:#202124;background:#fff}"
             "h1{font-size:20px;color:#5f6368;margin-bottom:16px}"
             "h3{font-size:18px;margin-top:20px;margin-bottom:2px}"
             "a{color:#1a0dab}"
             ".u{font-size:12px;color:#188038;margin:0}"
             ".d{font-size:14px;color:#4d5156;margin-top:2px;margin-bottom:0}"
             "</style></head><body><h1>");
    if(qtxt){ bstr(&b, "Results for &ldquo;"); bplain(&b, qtxt); bstr(&b, "&rdquo;"); }
    else bstr(&b, "Search results");
    bstr(&b, "</h1>");

    size_t shown = 0;
    for(size_t i = 0; i < results->n; i++){
        const JVal *r = results->kids[i];
        const char *href = jstr(jget(r, "url"));
        const char *title = jstr(jget(r, "title"));
        if(!safe_http_url(href) || !title) continue;
        const char *desc = jstr(jget(r, "description"));
        if(!desc) desc = jstr(jget(r, "content"));
        bstr(&b, "<h3><a href=\""); bescape(&b, href); bstr(&b, "\">"); bplain(&b, title); bstr(&b, "</a></h3>");
        bstr(&b, "<p class=\"u\">"); bescape(&b, href); bstr(&b, "</p>");
        if(desc && *desc){ bstr(&b, "<p class=\"d\">"); bplain(&b, desc); bstr(&b, "</p>"); }
        shown++;
    }
    if(!shown) bstr(&b, "<p>No results.</p>");
    bstr(&b, "</body></html>");
    jfree(root);
    if(out_len) *out_len = b.n;
    return b.d;
}
