#define _POSIX_C_SOURCE 200809L
#include "image.h"
#include "utils.h"
#include "fetch.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

#if defined(__has_include)
#  if __has_include(<SDL.h>)
#    define HAVE_SDL 1
#    include <SDL.h>
#  elif __has_include(<SDL2/SDL.h>)
#    define HAVE_SDL 1
#    include <SDL2/SDL.h>
#  endif
#endif
#if defined(__has_include)
#  if __has_include(<SDL_image.h>)
#    define HAVE_SDL_IMAGE 1
#    include <SDL_image.h>
#  elif __has_include(<SDL2/SDL_image.h>)
#    define HAVE_SDL_IMAGE 1
#    include <SDL2/SDL_image.h>
#  endif
#endif

#ifndef HAVE_SDL_IMAGE

#endif
#ifndef HAVE_SDL

#endif

static CachedImage *g_cache = NULL;
static size_t g_cache_n = 0, g_cache_cap = 0;
static bool g_img_inited = false;
#include <pthread.h>
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static char **g_pending_urls = NULL;
static size_t g_pending_n = 0, g_pending_cap = 0;
static bool g_pending_thread_running = false;
static bool g_cache_dirty = false;

static bool is_http_url(const char *s){
    return s && (strncmp(s,"http://",7)==0 || strncmp(s,"https://",8)==0);
}

static char *xstrdup_safe(const char *s){ return s ? xstrdup(s) : NULL; }

static int b64_val(char c){
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}
static unsigned char *base64_decode(const char *in, size_t *out_len){
    size_t len = strlen(in);

    char *tmp = (char*)xmalloc(len+1);
    size_t tlen=0;
    for(size_t i=0;i<len;i++) if(!isspace((unsigned char)in[i])) tmp[tlen++]=in[i];
    tmp[tlen]='\0';
    len=tlen;

    while(len && tmp[len-1]=='=') len--;
    if(len==0){ free(tmp); *out_len=0; return NULL; }
    size_t out_sz = (len*3)/4;
    unsigned char *out = (unsigned char*)xmalloc(out_sz+1);
    size_t oi=0;
    for(size_t i=0;i<len; ){
        int v[4]={0};
        int valid=0;
        for(int k=0;k<4;k++){
            if(i<len){ int vv=b64_val(tmp[i++]); if(vv>=0){ v[k]=vv; valid++; } else { v[k]=0; if(tmp[i-1]!='=') valid++; } } else v[k]=0;
        }
        if(valid>=2){
            out[oi++] = (unsigned char)((v[0]<<2)| (v[1]>>4));
            if(valid>=3) out[oi++] = (unsigned char)(((v[1]&0xF)<<4)| (v[2]>>2));
            if(valid>=4) out[oi++] = (unsigned char)(((v[2]&0x3)<<6)| v[3]);
        }
    }
    free(tmp);
    *out_len = oi;
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

static char *normalize_http_url(const char *url){
    if(!url) return NULL;
    if(!is_http_url(url)) return xstrdup(url);
    const char *proto=strstr(url,"://");
    if(!proto) return xstrdup(url);
    const char *host_start=proto+3;
    const char *slash=strchr(host_start,'/');
    if(!slash) return xstrdup(url);
    size_t origin_len=(size_t)(slash - url);
    char *origin=(char*)xmalloc(origin_len+1);
    memcpy(origin,url,origin_len);
    origin[origin_len]='\0';
    const char *path_and_query=slash;

    const char *qmark=strchr(path_and_query,'?');
    const char *hash=strchr(path_and_query,'#');
    const char *path_end=path_and_query+strlen(path_and_query);
    if(qmark && (!hash || qmark<hash)) path_end=qmark;
    else if(hash) path_end=hash;
    size_t path_len=(size_t)(path_end - path_and_query);
    char *path=(char*)xmalloc(path_len+1);
    memcpy(path,path_and_query,path_len);
    path[path_len]='\0';
    const char *suffix=path_end;

    char *copy=xstrdup(path);
    free(path);

    int cap=16, n=0;
    char **stack=(char**)xmalloc(cap*sizeof(char*));
    char *tok=strtok(copy,"/");
    while(tok){
        if(strcmp(tok,".")==0){

        } else if(strcmp(tok,"..")==0){
            if(n>0){ free(stack[n-1]); n--; }
        } else if(*tok){
            if(n>=cap){ cap*=2; stack=(char**)xrealloc(stack,cap*sizeof(char*));}
            stack[n++]=xstrdup(tok);
        }
        tok=strtok(NULL,"/");
    }
    free(copy);

    size_t need=1;
    for(int i=0;i<n;i++) need+=strlen(stack[i])+1;
    char *norm=(char*)xmalloc(origin_len+need+strlen(suffix)+2);
    strcpy(norm,origin);
    strcat(norm,"/");
    for(int i=0;i<n;i++){
        if(i) strcat(norm,"/");
        strcat(norm,stack[i]);
        free(stack[i]);
    }
    free(stack);
    free(origin);

    strcat(norm,suffix);
    return norm;
}
char *image_resolve_url(const char *src, const char *base){
    if(!src||!*src) return xstrdup("");

    if(strncmp(src,"data:",5)==0) return xstrdup(src);
    if(is_http_url(src)){
        char *norm=normalize_http_url(src);
        return norm?norm:xstrdup(src);
    }
    if(strncmp(src,"//",2)==0){
        char *out=(char*)xmalloc(strlen(src)+8+1);
        sprintf(out,"https:%s",src);
        char *norm=normalize_http_url(out);
        free(out);
        return norm?norm:xstrdup(src);
    }
    if(!base||!*base) return xstrdup(src);
    bool base_is_url = is_http_url(base);

    if(src[0]=='#') return xstrdup(src);

    char *raw=NULL;
    if(src[0]=='/'){
        if(base_is_url){
            char *origin = origin_of_url(base);
            if(!origin) return xstrdup(src);
            raw=(char*)xmalloc(strlen(origin)+strlen(src)+1);
            sprintf(raw,"%s%s",origin,src);
            free(origin);
        } else {

            raw=xstrdup(src);
        }
    } else {

        if(base_is_url){
            char *dir = base_dir_of_url(base);
            raw = path_join(dir?dir:"", src);
            free(dir);
        } else {
            char *dir = base_dir_of_path(base);
            raw = path_join(dir?dir:"", src);
            free(dir);
        }
    }
    if(raw && base_is_url){
        char *norm=normalize_http_url(raw);
        free(raw);
        return norm?norm:xstrdup(src);
    }
    return raw?raw:xstrdup(src);
}

void image_cache_init(void){
    if(g_img_inited) return;
#ifdef HAVE_SDL_IMAGE
    int flags = IMG_INIT_PNG | IMG_INIT_JPG;
#ifdef IMG_INIT_WEBP
    flags |= IMG_INIT_WEBP;
#endif
    IMG_Init(flags);
#endif
    g_img_inited=true;
}
void image_cache_clear(void){
    for(size_t i=0;i<g_cache_n;i++){
        free(g_cache[i].url);
#ifdef HAVE_SDL
        if(g_cache[i].tex) SDL_DestroyTexture(g_cache[i].tex);
        if(g_cache[i].surf) SDL_FreeSurface(g_cache[i].surf);
#else
        (void)g_cache[i].tex; (void)g_cache[i].surf;
#endif
    }
    free(g_cache);
    g_cache=NULL; g_cache_n=0; g_cache_cap=0;
#ifdef HAVE_SDL_IMAGE
    if(g_img_inited) IMG_Quit();
#endif
    g_img_inited=false;
}
size_t image_cache_count(void){ return g_cache_n; }
static CachedImage *cache_find(const char *abs);
static CachedImage *cache_add(const char *abs);

CachedImage *image_cache_lookup(const char *src, const char *base_url){
    if(!src||!*src) return NULL;
    char *abs = image_resolve_url(src, base_url);
    if(!abs) return NULL;
    pthread_mutex_lock(&g_cache_mutex);
    CachedImage *found = cache_find(abs);
    pthread_mutex_unlock(&g_cache_mutex);
    free(abs);
    return found;
}

static void *pending_fetch_thread(void *arg){
    (void)arg;

    char **to_fetch = NULL;
    size_t n = 0;
    pthread_mutex_lock(&g_cache_mutex);
    n = g_pending_n;
    if(n>0){
        to_fetch = (char**)xmalloc(n*sizeof(char*));
        for(size_t i=0;i<n;i++) to_fetch[i]=xstrdup(g_pending_urls[i]);

        for(size_t i=0;i<g_pending_n;i++) free(g_pending_urls[i]);
        g_pending_n=0;
    }
    g_pending_thread_running = false;
    pthread_mutex_unlock(&g_cache_mutex);
    if(!to_fetch) return NULL;
    for(size_t i=0;i<n;i++){
        char *url = to_fetch[i];

        image_cache_get(url, NULL, NULL);
        pthread_mutex_lock(&g_cache_mutex);
        g_cache_dirty = true;
        pthread_mutex_unlock(&g_cache_mutex);
        free(url);
    }
    free(to_fetch);
    return NULL;
}

void image_cache_fetch_async(const char *src, const char *base_url){
    if(!src||!*src) return;
    char *abs = image_resolve_url(src, base_url);
    if(!abs) return;

    pthread_mutex_lock(&g_cache_mutex);
    if(cache_find(abs)){
        pthread_mutex_unlock(&g_cache_mutex);
        free(abs);
        return;
    }
    for(size_t i=0;i<g_pending_n;i++) if(strcmp(g_pending_urls[i], abs)==0){
        pthread_mutex_unlock(&g_cache_mutex);
        free(abs);
        return;
    }
    if(g_pending_n>=g_pending_cap){
        g_pending_cap = g_pending_cap? g_pending_cap*2 : 16;
        g_pending_urls = (char**)xrealloc(g_pending_urls, g_pending_cap*sizeof(char*));
    }
    g_pending_urls[g_pending_n++] = abs;
    bool need_thread = !g_pending_thread_running;
    if(need_thread) g_pending_thread_running = true;
    pthread_mutex_unlock(&g_cache_mutex);
    if(need_thread){
        pthread_t th;
        pthread_create(&th, NULL, pending_fetch_thread, NULL);
        pthread_detach(th);
    }
}

bool image_cache_check_dirty(void){
    pthread_mutex_lock(&g_cache_mutex);
    bool d = g_cache_dirty;
    pthread_mutex_unlock(&g_cache_mutex);
    return d;
}
void image_cache_clear_dirty(void){
    pthread_mutex_lock(&g_cache_mutex);
    g_cache_dirty = false;
    pthread_mutex_unlock(&g_cache_mutex);
}

static CachedImage *cache_find(const char *abs){
    for(size_t i=0;i<g_cache_n;i++) if(strcmp(g_cache[i].url, abs)==0) return &g_cache[i];
    return NULL;
}
static CachedImage *cache_add(const char *abs){
    if(g_cache_n>=g_cache_cap){
        g_cache_cap = g_cache_cap? g_cache_cap*2 : 16;
        g_cache = (CachedImage*)xrealloc(g_cache, g_cache_cap*sizeof(CachedImage));
    }
    CachedImage *c = &g_cache[g_cache_n++];
    memset(c,0,sizeof(*c));
    c->url = xstrdup(abs);
    return c;
}

CachedImage *image_cache_get(const char *src, const char *base_url, SDL_Renderer *renderer){
    if(!src||!*src) return NULL;
    char *abs = image_resolve_url(src, base_url);
    if(!abs) return NULL;
    pthread_mutex_lock(&g_cache_mutex);
    CachedImage *found = cache_find(abs);
    if(found){
        pthread_mutex_unlock(&g_cache_mutex);
        free(abs);

#ifdef HAVE_SDL
        if(!found->failed && found->surf && !found->tex && renderer){

            found->tex = SDL_CreateTextureFromSurface(renderer, found->surf);
        }
#else
        (void)renderer;
#endif
        return found;
    }
    CachedImage *ent = cache_add(abs);
    pthread_mutex_unlock(&g_cache_mutex);
    free(abs);

    image_cache_init();

    unsigned char *data=NULL;
    size_t data_len=0;
    bool need_free_data=false;

    if(strncmp(src,"data:",5)==0 || strncmp(ent->url,"data:",5)==0){
        const char *uri = ent->url;
        const char *comma = strchr(uri,',');
        if(!comma){
            ent->failed=true;
            return ent;
        }
        bool is_b64 = (strstr(uri,";base64")!=NULL);
        const char *payload = comma+1;
        if(is_b64){
            data = base64_decode(payload, &data_len);
            need_free_data=true;
            if(!data){ ent->failed=true; return ent; }
        } else {

            data = (unsigned char*)xstrdup(payload);
            data_len = strlen(payload);
            need_free_data=true;
        }
    } else if(is_http_url(ent->url)){

        size_t flen=0;
        char *fdata = fetch_url(ent->url, &flen);
        if(!fdata){
            ent->failed=true;
            return ent;
        }
        data = (unsigned char*)fdata;
        data_len = flen;
        need_free_data=true;
    } else {

        char *path = xstrdup(ent->url);
        char *q=strchr(path,'?'); if(q) *q='\0';
        char *h=strchr(path,'#'); if(h) *h='\0';
        size_t flen=0;
        char *fdata = read_file(path, &flen);
        free(path);
        if(!fdata){

            fdata = read_file(src, &flen);
            if(!fdata){
                ent->failed=true;
                return ent;
            }
        }
        data = (unsigned char*)fdata;
        data_len = flen;
        need_free_data=true;
    }

    if(!data || data_len==0){
        ent->failed=true;
        if(need_free_data) free(data);
        return ent;
    }

#if defined(HAVE_SDL) && defined(HAVE_SDL_IMAGE)
    SDL_RWops *rw = SDL_RWFromMem(data, (int)data_len);
    if(!rw){
        ent->failed=true;
        if(need_free_data) free(data);
        return ent;
    }
    SDL_Surface *surf = IMG_Load_RW(rw, 1);
    if(!surf){

        ent->failed=true;
        if(need_free_data) free(data);
        return ent;
    }
    ent->surf = surf;
    ent->w = surf->w;
    ent->h = surf->h;
#ifdef HAVE_SDL
    if(renderer){
        ent->tex = SDL_CreateTextureFromSurface(renderer, surf);
        if(!ent->tex){

        }
    }
#else
    (void)renderer;
#endif
    if(need_free_data) free(data);
    return ent;
#else
    (void)renderer;

    ent->failed=true;
    if(need_free_data) free(data);
    return ent;
#endif
}
