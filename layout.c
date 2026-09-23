#define _POSIX_C_SOURCE 200809L
#include "layout.h"
#include "utils.h"
#include "style.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static bool str_is_auto(const char *s){
    if(!s) return false;
    char *t=xstrdup(s);
    str_trim_inplace(t);
    bool r = (strcasecmp(t,"auto")==0);
    free(t);
    return r;
}
static int resolve_len(const char *s, int containing, int font_size, bool *is_auto){
    if(is_auto) *is_auto=false;
    if(!s) return 0;
    char *t=xstrdup(s);
    str_trim_inplace(t);
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
void compute_box_model(Node *n, int containing_w, BoxModel *out){
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
int parse_px(const char *s,int parent_fs){
    if(!s) return 0;
    char *e=NULL; double v=strtod(s,&e);
    if(!e||e==s) return 0;
    if(strstr(s,"em")) return (int)(v*parent_fs);
    if(strstr(s,"px")) return (int)v;
    if(strstr(s,"%")) return (int)(v*parent_fs/100);
    return (int)v;
}
