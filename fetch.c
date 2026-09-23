#define _POSIX_C_SOURCE 200809L
#include "fetch.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#if defined(__has_include)
#  if __has_include(<curl/curl.h>)
#    define HAVE_CURL 1
#    include <curl/curl.h>
#  endif
#endif

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
static char *url_percent_decode_local(const char *src){
    size_t len=strlen(src);
    char *out=(char*)xmalloc(len+1);
    size_t oi=0;
    for(size_t i=0;i<len;i++){
        if(src[i]=='%' && i+2<len && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])){
            char hex[3]={src[i+1],src[i+2],0};
            out[oi++]=(char)strtol(hex,NULL,16);
            i+=2;
        } else if(src[i]=='+') out[oi++]=' ';
        else out[oi++]=src[i];
    }
    out[oi]=0;
    return out;
}
static char *extract_uddg_local(const char *url){
    if(!url) return NULL;
    const char *p=strstr(url,"uddg=");
    if(!p) return NULL;
    p+=5;
    const char *end=strchr(p,'&');
    const char *hash=strchr(p,'#');
    size_t vlen;
    if(end && hash) vlen=(size_t)((end<hash?end:hash)-p);
    else if(end) vlen=(size_t)(end-p);
    else if(hash) vlen=(size_t)(hash-p);
    else vlen=strlen(p);
    if(vlen==0) return NULL;
    char *enc=xstrndup(p,vlen);
    char *dec=url_percent_decode_local(enc);
    free(enc);
    if(strchr(dec,'%')){
        char *dec2=url_percent_decode_local(dec);
        free(dec); dec=dec2;
    }
    if(strncmp(dec,"http://",7)==0 || strncmp(dec,"https://",8)==0) return dec;
    free(dec); return NULL;
}
char *fetch_url(const char *url, size_t *out_len) {

    char *uddg_target = extract_uddg_local(url);
    if(uddg_target){
        fprintf(stderr,"fetch: bypassing DuckDuckGo redirect to %s\n", uddg_target);
        char *res = fetch_url(uddg_target, out_len);
        free(uddg_target);
        return res;
    }
    CURL *curl = curl_easy_init();
    if (!curl) { fprintf(stderr,"curl init failed\n"); return NULL; }
    struct Mem chunk = {0};
    chunk.data = (char*)xmalloc(1);
    chunk.size=0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
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

    bool is_mojeek = strstr(url, "mojeek.com") != NULL;
    bool need_fallback = false;
    if (code==403 && is_mojeek) need_fallback = true;
    else if (is_mojeek && chunk.data && (strstr(chunk.data, "403 - Forbidden") || strstr(chunk.data, "automated queries") || strstr(chunk.data, "<title>Captcha</title>"))) need_fallback = true;
    if (need_fallback) {
        bool is_image = strstr(url, "fmt=images") || strstr(url, "/images");
        const char *qp = strstr(url, "q=");
        if (qp) {
            qp += 2;
            const char *end = strchr(qp, '&');
            if (!end) end = strchr(qp, '#');
            size_t qlen = end ? (size_t)(end - qp) : strlen(qp);
            if (qlen > 0 && qlen < 512) {
                char *qenc = xstrndup(qp, qlen);

                char *bing_url = (char*)xmalloc(64 + qlen + 1);
                if (is_image) sprintf(bing_url, "https://www.bing.com/images/search?q=%s", qenc);
                else sprintf(bing_url, "https://www.bing.com/search?q=%s", qenc);
                fprintf(stderr,"fetch: Mojeek blocked (%ld), falling back to Bing %s\n", code, bing_url);
                free(qenc);
                free(chunk.data);
                curl_easy_cleanup(curl);
                char *res = fetch_url(bing_url, out_len);
                free(bing_url);
                return res;
            }
        }
        if (chunk.data && strstr(chunk.data, "automated queries")) {
            fprintf(stderr,"fetch: Mojeek automated queries block detected\n");
        }
    }
    curl_easy_cleanup(curl);
    if (out_len) *out_len = chunk.size;
    return chunk.data;
}
#else
char *fetch_url(const char *url, size_t *out_len) {
    (void)url; (void)out_len;
    fprintf(stderr,"fetch: libcurl not available (compile with -lcurl)\n");
    return NULL;
}
#endif

char *read_file(const char *path, size_t *out_len) {
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

static bool is_http_url(const char *s){
    return s && (strncmp(s,"http://",7)==0 || strncmp(s,"https://",8)==0);
}
static char *path_join(const char *dir, const char *href){
    if(!dir||!*dir) return xstrdup(href);
    size_t dl=strlen(dir);
    size_t hl=strlen(href);
    bool need_slash = (dl>0 && dir[dl-1]!='/');
    char *out=(char*)xmalloc(dl + (need_slash?1:0) + hl +1);
    memcpy(out,dir,dl);
    size_t pos=dl;
    if(need_slash) out[pos++]='/';
    memcpy(out+pos,href,hl+1);
    return out;
}
static char *base_dir_of_path(const char *base){
    if(!base) return NULL;
    const char *slash=strrchr(base,'/');
    if(!slash) return xstrdup(".");
    size_t n=(size_t)(slash - base);
    if(n==0) return xstrdup("/");
    char *d=(char*)xmalloc(n+1);
    memcpy(d,base,n);
    d[n]='\0';
    return d;
}
static char *base_dir_of_url(const char *url){
    if(!url) return NULL;
    const char *proto=strstr(url,"://");
    const char *start= proto ? proto+3 : url;
    const char *slash=strrchr(start,'/');
    if(!slash) return xstrdup(url);
    size_t n=(size_t)(slash - url);
    char *d=(char*)xmalloc(n+1);
    memcpy(d,url,n);
    d[n]='\0';
    return d;
}
static char *origin_of_url(const char *url){
    if(!url) return NULL;
    const char *proto=strstr(url,"://");
    if(!proto) return NULL;
    const char *host_start = proto+3;
    const char *slash = strchr(host_start,'/');
    size_t n;
    if(!slash) n=strlen(url);
    else n=(size_t)(slash - url);
    char *o=(char*)xmalloc(n+1);
    memcpy(o,url,n);
    o[n]='\0';
    return o;
}
static char *resolve_href_url(const char *href, const char *base_url){
    if(!href||!*href) return xstrdup("");
    if(strncmp(href,"data:",5)==0) return xstrdup(href);
    if(is_http_url(href)) return xstrdup(href);
    if(strncmp(href,"//",2)==0){
        char *out=(char*)xmalloc(strlen(href)+8+1);
        sprintf(out,"https:%s",href);
        return out;
    }
    if(!base_url||!*base_url) return xstrdup(href);
    bool base_is_url = is_http_url(base_url);
    if(!base_is_url) return xstrdup(href);
    if(href[0]=='/'){
        char *origin = origin_of_url(base_url);
        if(!origin) return xstrdup(href);
        char *raw=(char*)xmalloc(strlen(origin)+strlen(href)+1);
        sprintf(raw,"%s%s",origin,href);
        free(origin);
        return raw;
    }
    if(href[0]=='#') return xstrdup(href);
    char *dir = base_dir_of_url(base_url);
    char *raw = path_join(dir?dir:"", href);
    free(dir);
    return raw;
}

static void collect_links(Node *n, Node ***out, size_t *cnt, size_t *cap){
    if(!n) return;
    if(n->type==NODE_ELEMENT && streqi(n->tag,"link")){
        const char *rel=node_get_attr(n,"rel");
        const char *href=node_get_attr(n,"href");
        if(href && rel && strcasecmp(rel,"stylesheet")==0){
            if(*cnt>=*cap){ *cap=*cap?*cap*2:8; *out=(Node**)xrealloc(*out,*cap*sizeof(Node*));}
            (*out)[(*cnt)++]=n;
        }

        if(href && !rel){

        }
        if(href && rel && strstr(rel,"stylesheet") && strcasecmp(rel,"stylesheet")!=0){

            bool found=false;
            for(size_t i=0;i<*cnt;i++) if((*out)[i]==n) found=true;
            if(!found){
                if(*cnt>=*cap){ *cap=*cap?*cap*2:8; *out=(Node**)xrealloc(*out,*cap*sizeof(Node*));}
                (*out)[(*cnt)++]=n;
            }
        }
    }
    for(size_t i=0;i<n->child_count;i++) collect_links(n->children[i],out,cnt,cap);
}

char *load_linked_stylesheets(Node *root, const char *base_path_or_url, size_t *out_len){
    if(out_len) *out_len=0;
    if(!root) return xstrdup("");
    Node **links=NULL; size_t cnt=0, cap=0;
    collect_links(root,&links,&cnt,&cap);
    if(cnt==0){ free(links); return xstrdup(""); }
    char *combined=NULL; size_t clen=0, ccap=0;
    bool base_is_url = is_http_url(base_path_or_url);
    char *base_dir = NULL;
    if(base_path_or_url){
        base_dir = base_is_url? base_dir_of_url(base_path_or_url) : base_dir_of_path(base_path_or_url);
    }
    for(size_t i=0;i<cnt;i++){
        const char *href=node_get_attr(links[i],"href");
        if(!href||!*href) continue;

        if(strncmp(href,"data:",5)==0) continue;
        if(strstr(href,"fonts.googleapis.com")) continue;
        char *css=NULL; size_t css_len=0;
        if(is_http_url(href) || strncmp(href,"//",2)==0 || (base_is_url && href[0]=='/')){
            char *abs_url = resolve_href_url(href, base_path_or_url);
            css=fetch_url(abs_url,&css_len);
            free(abs_url);
        } else if(base_is_url){
            char *abs_url = resolve_href_url(href, base_path_or_url);
            css=fetch_url(abs_url,&css_len);
            free(abs_url);
        } else {

            char *abs_path=NULL;
            if(href[0]=='/' ){

                css=read_file(href,&css_len);
                if(!css && base_dir) {
                    const char *no_slash=href+1;
                    abs_path=path_join(base_dir,no_slash);
                    css=read_file(abs_path,&css_len);
                    free(abs_path);
                }
            } else {
                abs_path= base_dir ? path_join(base_dir, href) : xstrdup(href);
                css=read_file(abs_path,&css_len);
                free(abs_path);
            }
        }
        if(!css) continue;
        size_t need = clen + css_len + 2;
        if(need+1 > ccap){
            ccap = need*2+1024;
            combined=(char*)xrealloc(combined?combined:xmalloc(1),ccap);
            if(clen==0) combined[0]='\0';
        }
        if(clen>0 && combined[clen-1]!='\n'){
            combined[clen++]='\n';
        }
        memcpy(combined+clen,css,css_len);
        clen+=css_len;
        combined[clen]='\0';
        free(css);
    }
    free(links);
    free(base_dir);
    if(!combined){ combined=xstrdup(""); clen=0; }
    if(out_len) *out_len=clen;
    return combined;
}
