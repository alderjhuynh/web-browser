#define _POSIX_C_SOURCE 200809L
#include "css.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void css_rule_free(CSSRule *r) {
    for (size_t i=0;i<r->sel_count;i++) free(r->selectors[i]);
    free(r->selectors);
    for (size_t i=0;i<r->decl_count;i++) { free(r->decls[i].name); free(r->decls[i].value); }
    free(r->decls);
}

char *css_strip_comments(const char *src) {
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

CSSRule *parse_css(const char *css_text, size_t *out_count) {
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
                str_trim_inplace(tok);
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
                str_trim_inplace(filtered_tok);

                bool is_root = (streqi(tok, ":root"));
                bool is_pseudo=false;
                if(!is_root){
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
                if(is_root){
                    if (scnt>=scap) { scap=scap?scap*2:4; sels=(char**)xrealloc(sels, scap*sizeof(char*)); }
                    sels[scnt++]=xstrdup("html");
                } else if (!is_pseudo && filtered_tok[0]) {
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
                        str_trim_inplace(prop); str_trim_inplace(val);
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
