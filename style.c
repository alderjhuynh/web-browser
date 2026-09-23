#define _POSIX_C_SOURCE 200809L
#include "style.h"
#include "utils.h"
#include "color.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

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
                            str_trim_inplace(prop); str_trim_inplace(val);
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
void apply_styles(Node *root, CSSRule *rules, size_t n) {
    apply_styles_recursive(root, rules, n);
    style_resolve_all(root);
}

static const char *var_lookup(Node *node, const char *name){
    for(Node *cur=node; cur; cur=cur->parent){
        const char *v=node_style_get(cur, name);
        if(v) return v;
    }
    return NULL;
}

static char *expand_vars_recursive(Node *node, const char *raw, int depth){
    if(!raw) return xstrdup("");
    if(depth>10) return xstrdup(raw);
    const char *p=strstr(raw,"var(");
    if(!p) return xstrdup(raw);

    size_t start_pos = (size_t)(p - raw);
    int paren=0;
    size_t i=start_pos;
    size_t var_start=i;
    size_t var_end=0;
    for(; raw[i]; i++){
        if(raw[i]=='(') paren++;
        else if(raw[i]==')'){ paren--; if(paren==0){ var_end=i; break; } }
    }
    if(var_end==0) return xstrdup(raw);
    size_t inner_start = start_pos + 4;
    size_t inner_len = var_end - inner_start;
    char *inner = xstrndup(raw+inner_start, inner_len);

    char *comma=NULL;
    int d=0; bool in_s=false; char sq=0;
    for(size_t k=0;k<inner_len;k++){
        char c=inner[k];
        if(!in_s && (c=='"'||c=='\'')){ in_s=true; sq=c; }
        else if(in_s && c==sq) in_s=false;
        else if(!in_s){
            if(c=='(') d++;
            else if(c==')' && d>0) d--;
            else if(c==',' && d==0){ comma=inner+k; break; }
        }
    }
    char *var_name=NULL;
    char *fallback=NULL;
    if(comma){
        *comma='\0';
        var_name=xstrdup(inner);
        fallback=xstrdup(comma+1);
        str_trim_inplace(var_name);
        str_trim_inplace(fallback);
    } else {
        var_name=xstrdup(inner);
        str_trim_inplace(var_name);
    }
    free(inner);
    const char *found=NULL;
    if(var_name && var_name[0]=='-' && var_name[1]=='-'){
        found=var_lookup(node,var_name);
    }
    char *replacement=NULL;
    if(found){
        replacement=expand_vars_recursive(node,found,depth+1);
    } else if(fallback){
        replacement=expand_vars_recursive(node,fallback,depth+1);
    } else {
        replacement=xstrdup("");
    }
    free(var_name);
    if(fallback) free(fallback);

    size_t before_len=start_pos;
    size_t after_len=strlen(raw+var_end+1);
    size_t repl_len=strlen(replacement);
    char *tmp=(char*)xmalloc(before_len+repl_len+after_len+1);
    memcpy(tmp,raw,before_len);
    memcpy(tmp+before_len,replacement,repl_len);
    memcpy(tmp+before_len+repl_len, raw+var_end+1, after_len+1);
    free(replacement);

    char *out=expand_vars_recursive(node,tmp,depth+1);
    free(tmp);
    return out;
}

static char *extract_first_color(const char *s){
    if(!s) return NULL;

    for(const char *p=s; *p; p++){
        if(*p=='#'){
            size_t len=0;
            const char *q=p+1;
            while(q[len] && ((q[len]>='0'&&q[len]<='9')||(q[len]>='a'&&q[len]<='f')||(q[len]>='A'&&q[len]<='F')) && len<8) len++;
            if(len==3||len==4||len==6||len==8){
                char *hex=(char*)xmalloc(len+2);
                hex[0]='#'; memcpy(hex+1,p+1,len); hex[len+1]='\0';
                return hex;
            }
        }
        if(strncasecmp(p,"rgb",3)==0 || strncasecmp(p,"hsl",3)==0){
            const char *start=p;
            const char *l=strchr(p,'(');
            if(!l){ continue; }
            const char *r=strchr(l,')');
            if(!r) continue;
            size_t n=(size_t)(r - start +1);
            char *col=xstrndup(start,n);
            return col;
        }
        if(strncasecmp(p,"rgba",4)==0){

        }
    }

    if(strstr(s,"transparent")) return xstrdup("transparent");
    return NULL;
}

static char *handle_color_mix(const char *v, Node *node){
    (void)node;

    const char *p=strchr(v,'(');
    const char *q=strrchr(v,')');
    if(!p||!q||q<=p) return extract_first_color(v);
    size_t inner_len=(size_t)(q-p-1);
    char *inner=xstrndup(p+1,inner_len);
    char *col=extract_first_color(inner);
    free(inner);
    if(col) return col;
    return xstrdup("");
}

static int css_len_to_px(const char *tok, int parent_fs, int win_w){
    if(!tok) return 0;
    char *t=xstrdup(tok); str_trim_inplace(t);
    for(char *c=t;*c;c++) *c=(char)tolower((unsigned char)*c);
    char *end=NULL; double num=strtod(t,&end);
    if(end==t){ free(t); return 0; }
    int res=0;
    if(strstr(t,"rem")) res=(int)(num*16);
    else if(strstr(t,"em")) res=(int)(num*parent_fs);
    else if(strstr(t,"vw")) res=(int)(num*win_w/100.0);
    else if(strstr(t,"vh")) res=(int)(num*720/100.0);
    else if(strstr(t,"px")) res=(int)num;
    else if(strstr(t,"pt")) res=(int)(num*4/3);
    else if(strstr(t,"%")) res=(int)(num*parent_fs/100.0);
    else res=(int)num;
    free(t);
    return res;
}

static char *handle_clamp(const char *v, Node *node){
    const char *p=strstr(v,"clamp");
    if(!p) return xstrdup(v);
    p=strchr(p,'(');
    if(!p) return xstrdup(v);
    const char *q=p;
    int depth=0;
    size_t end=0;
    for(size_t i=0; p[i]; i++){
        if(p[i]=='(') depth++;
        else if(p[i]==')'){ depth--; if(depth==0){ end=i; break; } }
    }
    if(end==0) return xstrdup(v);
    char *inner=xstrndup(p+1,end-1);

    char *args[3]={0}; int argc=0;
    size_t start=0; int d=0; bool inq=false; char qq=0;
    size_t ilen=strlen(inner);
    for(size_t i=0;i<=ilen;i++){
        char c=inner[i];
        if(c=='"'||c=='\''){ if(!inq){inq=true;qq=c;} else if(c==qq) inq=false; }
        if(!inq){
            if(c=='(') d++;
            else if(c==')' && d>0) d--;
        }
        if((c==',' && !inq && d==0) || c=='\0'){
            size_t len=i-start;
            char *tok=xstrndup(inner+start,len);
            str_trim_inplace(tok);
            if(argc<3) args[argc++]=tok; else free(tok);
            start=i+1;
        }
    }
    free(inner);
    if(argc!=3){
        for(int i=0;i<argc;i++) free(args[i]);
        return xstrdup(v);
    }
    int parent=16;
    if(node) parent=node_font_size(node->parent? node->parent: node);
    int win_w=1024;
    int min_px=css_len_to_px(args[0],parent,win_w);
    int val_px=css_len_to_px(args[1],parent,win_w);
    int max_px=css_len_to_px(args[2],parent,win_w);
    for(int i=0;i<argc;i++) free(args[i]);
    int chosen=val_px;
    if(chosen<min_px) chosen=min_px;
    if(chosen>max_px) chosen=max_px;
    char buf[32]; snprintf(buf,sizeof(buf),"%dpx",chosen);

    const char *clamp_start=strstr(v,"clamp");
    const char *clamp_end=strchr(clamp_start,')');

    char *trim=xstrdup(v); str_trim_inplace(trim);
    bool is_just_clamp = (strncmp(trim,"clamp",5)==0 && trim[strlen(trim)-1]==')');
    free(trim);
    if(is_just_clamp) return xstrdup(buf);

    size_t before_len=(size_t)(clamp_start - v);

    const char *cs=clamp_start;
    int dep=0; size_t ce=0;
    for(size_t i=0; cs[i]; i++){ if(cs[i]=='(') dep++; else if(cs[i]==')'){dep--; if(dep==0){ce=i; break;}} }
    size_t after_start=before_len+ce+1;
    size_t after_len=strlen(v+after_start);
    char *out=(char*)xmalloc(before_len+strlen(buf)+after_len+1);
    memcpy(out,v,before_len);
    memcpy(out+before_len,buf,strlen(buf));
    memcpy(out+before_len+strlen(buf),v+after_start,after_len+1);
    return out;
}

static char *handle_calc(const char *v, Node *node){

    const char *p=strstr(v,"calc");
    if(!p) return xstrdup(v);
    char *col=extract_first_color(v);
    (void)col;

    const char *l=strchr(p,'(');
    const char *r=strrchr(p,')');
    if(!l||!r) return xstrdup(v);
    char *inner=xstrndup(l+1,(size_t)(r-l-1));

    char *tok=NULL;
    for(char *c=inner; *c; ){
        while(*c && isspace((unsigned char)*c)) c++;
        if(!*c) break;
        char *start=c;
        while(*c && !isspace((unsigned char)*c) && *c!='+' && *c!='-' && *c!='*' && *c!='/' && *c!=',') c++;
        size_t len=(size_t)(c-start);
        if(len>0){
            char *cand=xstrndup(start,len);
            if(strstr(cand,"px")||strstr(cand,"%")||strstr(cand,"rem")||strstr(cand,"em")||strstr(cand,"vw")){
                tok=cand;
                break;
            }
            free(cand);
        }
        if(*c) c++;
    }
    free(inner);
    if(tok){
        int parent=16; if(node) parent=node_font_size(node->parent?node->parent:node);
        int px=css_len_to_px(tok,parent,1024);
        free(tok);
        char buf[32]; snprintf(buf,sizeof(buf),"%dpx",px);
        return xstrdup(buf);
    }
    return xstrdup(v);
}

char *style_resolve_value(Node *node, const char *raw){
    if(!raw) return xstrdup("");
    char *expanded=expand_vars_recursive(node,raw,0);

    char *cur=expanded;

    if(strstr(cur,"color-mix")){
        char *next=handle_color_mix(cur,node);
        free(cur);
        cur=next;

    }
    if(strstr(cur,"clamp")){
        char *next=handle_clamp(cur,node);
        free(cur);
        cur=next;
    }
    if(strstr(cur,"calc(")){
        char *next=handle_calc(cur,node);
        free(cur);
        cur=next;
    }
    if(strstr(cur,"min(") || strstr(cur,"max(")){

        char *next=handle_calc(cur,node);
        if(next!=cur){ free(cur); cur=next; }
    }

    if((strstr(cur,"linear-gradient")||strstr(cur,"radial-gradient")||strstr(cur,"url(")) && !extract_first_color(cur)){

        char *trim=xstrdup(cur); str_trim_inplace(trim);
        bool is_grad = (strstr(trim,"gradient")!=NULL || strstr(trim,"url(")!=NULL) && strlen(trim)>0 && trim[0]!='\0';
        free(trim);
        if(is_grad){

            char *col=extract_first_color(cur);
            if(col){ free(cur); cur=col; }
            else { free(cur); cur=xstrdup(""); }
        }
    }

    str_trim_inplace(cur);
    return cur;
}

static void resolve_node_cb(Node *n, int depth, void *ctx){
    (void)depth; (void)ctx;
    if(n->type!=NODE_ELEMENT && n->type!=NODE_DOCUMENT) return;

    size_t cnt=n->style_count;
    if(cnt==0) return;

    for(size_t i=0;i<cnt;i++){
        const char *prop=n->style[i].name;

        const char *raw=n->style[i].value;
        if(!raw) continue;

        if(strstr(raw,"var(") || strstr(raw,"color-mix") || strstr(raw,"clamp") || strstr(raw,"calc") || strstr(raw,"gradient") || strstr(raw,"url(")){
            char *resolved=style_resolve_value(n,raw);
            free(n->style[i].value);
            n->style[i].value=resolved;
        } else if(prop[0]=='-' && prop[1]=='-'){

            if(strstr(raw,"var(")){
                char *resolved=style_resolve_value(n,raw);
                free(n->style[i].value);
                n->style[i].value=resolved;
            }
        } else {

        }
    }
}

void style_resolve_all(Node *root){
    dom_walk(root, resolve_node_cb, NULL);

    dom_walk(root, resolve_node_cb, NULL);
}

int resolve_font_size(const char *s, int parent) {
    if (!s) return parent;
    char *tmp=xstrdup(s);
    str_trim_inplace(tmp);
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
int node_font_size(Node *n) {
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
char *node_text_content(Node *n) {
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
void font_cb(Node *n, int depth, void *ctx) {
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
