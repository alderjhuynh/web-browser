#define _POSIX_C_SOURCE 200809L
#include "gui.h"
#include "utils.h"
#include "dom.h"
#include "parser.h"
#include "css.h"
#include "style.h"
#include "fetch.h"
#include "color.h"
#include "layout.h"
#include "image.h"
#include "search.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>

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

typedef enum { GCMD_TEXT, GCMD_RECT, GCMD_HR, GCMD_IMAGE } GuiCmdType;
typedef struct {
    GuiCmdType type;
    int x, y, w, h;
    char *text;
    int  font_size;
    bool bold, italic, mono, underline;
    bool glue;
    SDL_Color color;
    SDL_Color bg;
    char *href;
    SDL_Texture *tex;
    int tex_w, tex_h;
} GuiCmd;

#define FONT_CACHE 256
typedef struct { int px; bool bold, italic, mono; TTF_Font *font; } FontCacheEnt;
static FontCacheEnt g_font_cache[FONT_CACHE];
static int g_font_cache_n = 0;

static float g_dpi = 1.0f;

typedef struct { const char *path[4]; int idx[4]; } FontFam;
#define FF(r,b,i,bi) {{r,b,i,bi},{0,0,0,0}}
static const FontFam SANS_FAMS[] = {
    FF("/System/Library/Fonts/Supplemental/Arial.ttf","/System/Library/Fonts/Supplemental/Arial Bold.ttf","/System/Library/Fonts/Supplemental/Arial Italic.ttf","/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf"),
    FF("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf"),
    FF("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf","/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf","/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf","/usr/share/fonts/truetype/noto/NotoSans-BoldItalic.ttf"),
    FF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"),
    FF("/usr/share/fonts/TTF/DejaVuSans.ttf","/usr/share/fonts/TTF/DejaVuSans-Bold.ttf","/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf","/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf"),
    FF("/System/Library/Fonts/Supplemental/Verdana.ttf","/System/Library/Fonts/Supplemental/Verdana Bold.ttf","/System/Library/Fonts/Supplemental/Verdana Italic.ttf","/System/Library/Fonts/Supplemental/Verdana Bold Italic.ttf"),
    FF("/opt/homebrew/share/fonts/DejaVuSans.ttf",NULL,NULL,NULL),
};
static const FontFam MONO_FAMS[] = {
    {{"/System/Library/Fonts/Menlo.ttc","/System/Library/Fonts/Menlo.ttc","/System/Library/Fonts/Menlo.ttc","/System/Library/Fonts/Menlo.ttc"},{0,1,2,3}},
    FF("/System/Library/Fonts/Supplemental/Courier New.ttf","/System/Library/Fonts/Supplemental/Courier New Bold.ttf","/System/Library/Fonts/Supplemental/Courier New Italic.ttf","/System/Library/Fonts/Supplemental/Courier New Bold Italic.ttf"),
    FF("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf","/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf","/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf","/usr/share/fonts/truetype/liberation/LiberationMono-BoldItalic.ttf"),
    FF("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSansMono-BoldOblique.ttf"),
    FF("/usr/share/fonts/TTF/DejaVuSansMono.ttf","/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf","/usr/share/fonts/TTF/DejaVuSansMono-Oblique.ttf","/usr/share/fonts/TTF/DejaVuSansMono-BoldOblique.ttf"),
};
static const FontFam *g_sans = NULL, *g_mono = NULL;

static bool file_exists(const char *p){ if(!p) return false; FILE *f=fopen(p,"rb"); if(f){ fclose(f); return true; } return false; }
static void gui_find_font_paths(void) {
    if (g_sans) return;
    for(size_t i=0;i<sizeof(SANS_FAMS)/sizeof(SANS_FAMS[0]);i++) if(file_exists(SANS_FAMS[i].path[0])){ g_sans=&SANS_FAMS[i]; break; }
    for(size_t i=0;i<sizeof(MONO_FAMS)/sizeof(MONO_FAMS[0]);i++) if(file_exists(MONO_FAMS[i].path[0])){ g_mono=&MONO_FAMS[i]; break; }
    if(!g_sans) g_sans=&SANS_FAMS[0];
    if(!g_mono) g_mono=g_sans;
}
static TTF_Font *gui_get_font_px(int px, bool bold, bool italic, bool mono){
    if(px<8) px=8;
    if(px>288) px=288;
    for(int i=0;i<g_font_cache_n;i++) if(g_font_cache[i].px==px && g_font_cache[i].bold==bold && g_font_cache[i].italic==italic && g_font_cache[i].mono==mono) return g_font_cache[i].font;
    if(g_font_cache_n>=FONT_CACHE) return g_font_cache[0].font;
    gui_find_font_paths();
    const FontFam *fam = mono ? g_mono : g_sans;
    int want = (bold?1:0) | (italic?2:0);
    int v = want;
    bool synth_b = false, synth_i = false;
    if(!file_exists(fam->path[v])){
        v = 0; synth_b = bold; synth_i = italic;
        if(want==3 && file_exists(fam->path[1])){ v=1; synth_b=false; synth_i=true; }
        else if(want==3 && file_exists(fam->path[2])){ v=2; synth_b=true; synth_i=false; }
    }
    TTF_Font *f = TTF_OpenFontIndex(fam->path[v], px, fam->idx[v]);
    if(!f && v!=0) { f = TTF_OpenFontIndex(fam->path[0], px, fam->idx[0]); synth_b=bold; synth_i=italic; }
    if(!f) return NULL;
    int style = TTF_STYLE_NORMAL;
    if(synth_b) style|=TTF_STYLE_BOLD;
    if(synth_i) style|=TTF_STYLE_ITALIC;
    TTF_SetFontStyle(f, style);
    TTF_SetFontHinting(f, g_dpi > 1.01f ? TTF_HINTING_NONE : TTF_HINTING_LIGHT);
    g_font_cache[g_font_cache_n++] = (FontCacheEnt){px,bold,italic,mono,f};
    return f;
}
static TTF_Font *gui_get_font(int sz, bool bold, bool italic, bool mono){ return gui_get_font_px(sz,bold,italic,mono); }
static TTF_Font *gui_get_render_font(int sz, bool bold, bool italic, bool mono){ return gui_get_font_px((int)lroundf(sz*g_dpi),bold,italic,mono); }
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
    bool pending_space;
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
    const char *base_url;
    SDL_Renderer *renderer;
    bool center_align;
    bool center_stack[32];
    int center_depth;
} LayoutCtx;

static Node *find_ancestor_form(Node *n);
static char *build_form_url(Node *form, Node *submitter, const char *base_url);
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
    c->pending_space = true;
}
static void lctx_free(LayoutCtx *c){
    for(size_t i=0;i<c->ncmd;i++){ free(c->cmds[i].text); free(c->cmds[i].href); if(c->cmds[i].tex && c->cmds[i].type!=GCMD_IMAGE) SDL_DestroyTexture(c->cmds[i].tex); }
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

    int total_w=0;
    for(size_t i=0;i<c->nline;i++){
        total_w += c->line[i].w;
        if(i+1<c->nline && !c->line[i+1].glue){
            TTF_Font *ff=gui_get_font(c->line[i].font_size,c->line[i].bold,c->line[i].italic,c->line[i].mono);
            int sw=4; if(ff) TTF_SizeUTF8(ff," ",&sw,NULL); else sw=c->line[i].font_size/3;
            total_w+=sw;
        }
    }
    int x = c->content_x + c->indent;
    if(c->center_align){
        int avail = c->content_w - c->indent;
        if(total_w < avail) x += (avail - total_w)/2;
    }
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
        if(i+1 < c->nline && !c->line[i+1].glue) x += c->line[i].w + space_w;
        else x += c->line[i].w;
    }

    c->nline=0;
    c->pending_space = true;
    c->cur_x = c->content_x + c->indent;
    c->cur_y += line_h + 4;
    c->cur_line_h=0; c->cur_line_ascent=0; c->cur_line_descent=0;
}

static void lctx_emit_word(LayoutCtx *c, const char *word, Node *parent_elem, bool glue){
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
    for(size_t i=0;i<c->nline;i++){
        cur_line_used += c->line[i].w;
        if(i+1<c->nline && !c->line[i+1].glue) cur_line_used += space_w;
    }
    if(c->nline>0 && !glue) cur_line_used += space_w;
    if(c->nline>0 && line_x + cur_line_used + w > c->content_x + c->content_w){
        lctx_flush(c);
        line_x = c->content_x + c->indent;
        cur_line_used=0;
    }

    GuiCmd cmd={0};
    cmd.type=GCMD_TEXT; cmd.text=xstrdup(word); cmd.font_size=fs; cmd.bold=bold; cmd.italic=italic; cmd.mono=mono; cmd.underline=underline; cmd.color=col; cmd.href=href_dup; cmd.w=w; cmd.h=h; cmd.glue=glue && c->nline>0;

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

static int parse_css_len_px(const char *s, int containing, int font_size, int win_w, int win_h);
static int resolve_len_with_min(const char *s, int containing, int font_size, int win_w, int win_h);

static void lctx_emit_placeholder(LayoutCtx *c, const char *msg){
     const char *alt = msg ? msg : "[image]";
     char buf[256]; snprintf(buf,sizeof(buf), "[IMG: %s]", alt);
     const char *color_s = "#666666";
     SDL_Color col = hex_to_sdl(color_s);
     int fs=14;
     TTF_Font *f = gui_get_font(fs,false,false,false);
     int w=0,h=0; if(f) TTF_SizeUTF8(f,buf,&w,&h); else {w=(int)strlen(buf)*8; h=14;}
     int bw = w+16, bh = h+10;
     GuiCmd rect={0}; rect.type=GCMD_RECT; rect.x=c->content_x + c->indent; rect.y=c->cur_y; rect.w=bw; rect.h=bh; rect.bg=(SDL_Color){220,220,220,255}; rect.color=(SDL_Color){180,180,180,255};
     if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
     c->cmds[c->ncmd++]=rect;

     if(c->cur_href && c->cur_href[0]){
         if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
         c->links[c->nlink].r = (SDL_Rect){rect.x, rect.y, rect.w, rect.h};
         c->links[c->nlink].href = xstrdup(c->cur_href);
         c->nlink++;
     }
     GuiCmd txt={0}; txt.type=GCMD_TEXT; txt.text=xstrdup(buf); txt.font_size=fs; txt.color=col; txt.x=rect.x+8; txt.y=rect.y+5; txt.w=w; txt.h=h;
     if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
     c->cmds[c->ncmd++]=txt;
     c->cur_y+=bh+6;
}
static void lctx_emit_img(LayoutCtx *c, Node *elem){
    lctx_flush(c);
    const char *src = node_get_attr(elem,"src");
    const char *alt = node_get_attr(elem,"alt");
    if(!src || !*src){
        lctx_emit_placeholder(c, alt ? alt : "[no src]");
        return;
    }
    const char *base = c->base_url;
    SDL_Renderer *ren = c->renderer;

    CachedImage *img = NULL;
    if(strncmp(src,"data:",5)==0){
        img = image_cache_get(src, base, ren);
    } else {
        img = image_cache_lookup(src, base);

        if(!img){
            image_cache_fetch_async(src, base);
            const char *show = alt ? alt : src;
            char short_src[128]; if(strlen(show)>120){ memcpy(short_src, show, 117); short_src[117]=0; strcat(short_src,"..."); show=short_src; }
            lctx_emit_placeholder(c, show);
            return;
        }

        if(!img->failed && img->surf && !img->tex && ren){
            img->tex = SDL_CreateTextureFromSurface(ren, img->surf);
        }
    }
    if(!img || img->failed || !img->surf){

        const char *show = alt ? alt : src;

        char short_src[128]; if(strlen(show)>120){ memcpy(short_src, show, 117); short_src[117]=0; strcat(short_src,"..."); show=short_src; }
        lctx_emit_placeholder(c, show);
        return;
    }
    int intrinsic_w = img->w;
    int intrinsic_h = img->h;
    if(intrinsic_w<=0) intrinsic_w=100;
    if(intrinsic_h<=0) intrinsic_h=100;
    int avail_w = c->content_w - c->indent;
    if(avail_w<20) avail_w = c->content_w;
    if(avail_w<20) avail_w=200;
    int fs = node_font_size(elem);

    int desired_w = intrinsic_w;
    int desired_h = intrinsic_h;

    const char *aw = node_get_attr(elem,"width");
    const char *ah = node_get_attr(elem,"height");
    bool has_aw=false, has_ah=false;
    int attr_w=0, attr_h=0;
    if(aw && *aw){ char *end=NULL; long v=strtol(aw,&end,10); if(end!=aw && v>0){ attr_w=(int)v; has_aw=true; } }
    if(ah && *ah){ char *end=NULL; long v=strtol(ah,&end,10); if(end!=ah && v>0){ attr_h=(int)v; has_ah=true; } }
    const char *sw = node_style_get(elem,"width");
    const char *sh = node_style_get(elem,"height");
    const char *maxw = node_style_get(elem,"max-width");

    if(sw && *sw && !streqi(sw,"auto")){
        int px = parse_css_len_px(sw, avail_w, fs, c->win_w, c->win_h);
        if(px>0) { desired_w = px; has_aw=true; }
        else if(strstr(sw,"%")){

        }
    } else if(has_aw){
        desired_w = attr_w;
    }
    if(sh && *sh && !streqi(sh,"auto")){
        int px = parse_css_len_px(sh, avail_w, fs, c->win_w, c->win_h);
        if(px>0) { desired_h = px; has_ah=true; }
    } else if(has_ah){
        desired_h = attr_h;
    }

    if(has_aw && !has_ah && intrinsic_w>0 && intrinsic_h>0){
        desired_h = (int)((double)desired_w * intrinsic_h / intrinsic_w);
    } else if(!has_aw && has_ah && intrinsic_w>0 && intrinsic_h>0){
        desired_w = (int)((double)desired_h * intrinsic_w / intrinsic_h);
    } else if(has_aw && has_ah){

    }

    if(maxw && *maxw){
        int max_px = resolve_len_with_min(maxw, avail_w, fs, c->win_w, c->win_h);
        if(max_px>0 && desired_w > max_px){
            double ratio = (double)max_px / desired_w;
            desired_w = max_px;
            desired_h = (int)(desired_h * ratio);
        }
    } else {

        if(desired_w > avail_w){
            double ratio = (double)avail_w / desired_w;
            desired_w = avail_w;
            desired_h = (int)(desired_h * ratio);
        }
    }

    int max_h = c->win_h - GUI_TOOLBAR_H - GUI_STATUS_H - 40;
    if(max_h<100) max_h=400;
    if(desired_h > max_h && desired_h>0){

        double ratio = (double)max_h / desired_h;

        desired_h = max_h;
        desired_w = (int)(desired_w * ratio);
        if(desired_w > avail_w) { double r2=(double)avail_w/desired_w; desired_w=avail_w; desired_h=(int)(desired_h*r2); }
    }
    if(desired_w<1) desired_w=1;
    if(desired_h<1) desired_h=1;

    int x = c->content_x + c->indent;
    if(c->center_align){
        int avail = c->content_w - c->indent;
        if(desired_w < avail) x += (avail - desired_w)/2;
    }
    int y = c->cur_y;

    SDL_Texture *tex = img->tex;
    if(!tex && ren && img->surf){
        tex = SDL_CreateTextureFromSurface(ren, img->surf);
        img->tex = tex;
    }
    if(tex){
        GuiCmd cmd={0}; cmd.type=GCMD_IMAGE; cmd.x=x; cmd.y=y; cmd.w=desired_w; cmd.h=desired_h; cmd.tex=tex; cmd.tex_w=intrinsic_w; cmd.tex_h=intrinsic_h;

        if(alt) cmd.href=xstrdup(alt); else if(src) cmd.href=xstrdup(src);
        if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
        c->cmds[c->ncmd++]=cmd;

        if(c->cur_href && c->cur_href[0]){
            if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
            c->links[c->nlink].r = (SDL_Rect){x, y, desired_w, desired_h};
            c->links[c->nlink].href = xstrdup(c->cur_href);
            c->nlink++;
        } else {

            Node *anc = elem->parent;
            Node *label = NULL;
            while(anc){
                if(anc->type==NODE_ELEMENT && streqi(anc->tag,"label")){ label=anc; break; }
                anc=anc->parent;
            }
            if(label){
                Node *form=find_ancestor_form(elem);
                if(form){

                    Node *input=NULL;
                    for(size_t i=0;i<label->child_count;i++){
                        Node *ch=label->children[i];
                        if(ch->type==NODE_ELEMENT && streqi(ch->tag,"input")){ input=ch; break; }

                        for(size_t j=0;j<ch->child_count;j++){
                            Node *gc=ch->children[j];
                            if(gc->type==NODE_ELEMENT && streqi(gc->tag,"input")){ input=gc; break; }
                        }
                        if(input) break;
                    }
                    char *href=build_form_url(form, input, c->base_url);
                    if(href && *href){
                        if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
                        c->links[c->nlink].r = (SDL_Rect){x, y, desired_w, desired_h};
                        c->links[c->nlink].href = href;
                        c->nlink++;
                    } else free(href);
                }
            }
        }
        c->cur_y += desired_h + 6;
    } else {

        lctx_emit_placeholder(c, alt ? alt : src);
    }
}
static void lctx_emit_bullet(LayoutCtx *c){

    int bx = c->content_x + c->indent - 14;
    if(bx < c->content_x) bx = c->content_x + c->indent;
    GuiCmd b={0}; b.type=GCMD_RECT; b.x=bx; b.y=c->cur_y+6; b.w=6; b.h=6; b.bg=(SDL_Color){0,0,0,255};
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=b;
}

static Node *find_ancestor_form(Node *n){
    for(Node *p=n ? n->parent : NULL; p; p=p->parent){
        if(p->type==NODE_ELEMENT && streqi(p->tag,"form")) return p;
    }
    return NULL;
}
static char *ctrl_url_encode(const char *s){
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
static void collect_form_inputs(Node *form, Node *submitter, char **out_qs){

    char *qs=NULL; size_t qlen=0, qcap=0;

    #define APPEND_PAIR(k,v) do{ char *ek=ctrl_url_encode(k); char *ev=ctrl_url_encode(v); size_t need = qlen + strlen(ek)+strlen(ev)+2 + (qlen?1:0); if(need+1>qcap){ qcap=need*2+64; qs=(char*)xrealloc(qs?qs:xmalloc(1), qcap); if(qlen==0) qs[0]=0; } if(qlen>0) qs[qlen++]='&'; strcpy(qs+qlen, ek); qlen+=strlen(ek); qs[qlen++]='='; strcpy(qs+qlen, ev); qlen+=strlen(ev); qs[qlen]=0; free(ek); free(ev); }while(0)

    Node **stack=(Node**)xmalloc(64*sizeof(Node*));
    size_t sp=0, cap=64;
    stack[sp++]=form;
    while(sp){
        Node *cur=stack[--sp];
        if(cur!=form && cur->type==NODE_ELEMENT && streqi(cur->tag,"input")){
            const char *name=node_get_attr(cur,"name");
            if(name && *name){
                const char *type=node_get_attr(cur,"type");
                const char *value=node_get_attr(cur,"value");
                if(!value) value="";
                if(!type) type="text";
                if(streqi(type,"hidden")||streqi(type,"text")||streqi(type,"search")||streqi(type,"email")||streqi(type,"password")||streqi(type,"number")||streqi(type,"url")||streqi(type,"tel")){
                    APPEND_PAIR(name,value);
                } else if(streqi(type,"checkbox")||streqi(type,"radio")){
                    if(node_get_attr(cur,"checked")!=NULL) APPEND_PAIR(name,value);
                } else if(streqi(type,"submit")||streqi(type,"button")||streqi(type,"image")){
                    if(cur==submitter){
                        APPEND_PAIR(name,value);
                        if(streqi(type,"image")){
                            APPEND_PAIR("x","0");
                            APPEND_PAIR("y","0");
                        }
                    }
                }
            }
        } else if(cur!=form && cur->type==NODE_ELEMENT && streqi(cur->tag,"textarea")){
            const char *name=node_get_attr(cur,"name");
            if(name && *name){
                char *txt=node_text_content(cur);
                APPEND_PAIR(name, txt?txt:"");
                free(txt);
            }
        } else if(cur!=form && cur->type==NODE_ELEMENT && streqi(cur->tag,"select")){
            const char *name=node_get_attr(cur,"name");
            if(name && *name){

                const char *sel_val=NULL;
                for(size_t i=0;i<cur->child_count;i++){
                    Node *opt=cur->children[i];
                    if(opt->type==NODE_ELEMENT && streqi(opt->tag,"option")){
                        if(node_get_attr(opt,"selected")!=NULL){
                            sel_val=node_get_attr(opt,"value");
                            if(!sel_val) sel_val=node_text_content(opt);
                            break;
                        }
                    }
                }
                if(!sel_val){

                    for(size_t i=0;i<cur->child_count;i++){
                        Node *opt=cur->children[i];
                        if(opt->type==NODE_ELEMENT && streqi(opt->tag,"option")){
                            sel_val=node_get_attr(opt,"value");
                            if(!sel_val) sel_val=node_text_content(opt);
                            break;
                        }
                    }
                }
                if(sel_val) APPEND_PAIR(name, sel_val);
            }
        } else if(cur!=form && cur->type==NODE_ELEMENT && streqi(cur->tag,"button")){
            const char *name=node_get_attr(cur,"name");
            if(name && *name && cur==submitter){
                const char *value=node_get_attr(cur,"value");
                char *txt=node_text_content(cur);
                const char *v = value ? value : (txt?txt:"");
                APPEND_PAIR(name, v);
                free(txt);
            }
        }

        for(size_t i=cur->child_count; i>0; ){
            i--;
            if(sp>=cap){ cap*=2; stack=(Node**)xrealloc(stack, cap*sizeof(Node*)); }
            stack[sp++]=cur->children[i];
        }
    }
    free(stack);
    if(!qs) qs=xstrdup("");
    *out_qs=qs;
    #undef APPEND_PAIR
}
static char *build_form_url(Node *form, Node *submitter, const char *base_url){
    const char *action=node_get_attr(form,"action");
    if(!action || !*action) action=base_url ? base_url : "";
    char *abs_action = image_resolve_url(action, base_url);
    char *qs=NULL;
    collect_form_inputs(form, submitter, &qs);
    char *result=NULL;
    if(qs && *qs){
        const char *sep = strchr(abs_action,'?') ? "&" : "?";

        char *hash=strchr(abs_action,'#');
        char *base_no_frag = hash ? xstrndup(abs_action, hash-abs_action) : xstrdup(abs_action);
        char *frag = hash ? xstrdup(hash) : xstrdup("");
        result=(char*)xmalloc(strlen(base_no_frag)+1+strlen(qs)+strlen(frag)+1);
        sprintf(result,"%s%s%s%s", base_no_frag, sep, qs, frag);
        free(base_no_frag); free(frag);
    } else {
        result=xstrdup(abs_action);
    }
    free(abs_action);
    free(qs);
    return result;
}
static void lctx_emit_control(LayoutCtx *c, Node *elem){
    lctx_flush(c);
    const char *tag=elem->tag;
    const char *type=node_get_attr(elem,"type");
    if(!type) type="";

    char *label=NULL;
    if(streqi(tag,"input")){
        if(streqi(type,"submit")||streqi(type,"button")){
            const char *v=node_get_attr(elem,"value");
            label=xstrdup(v && *v ? v : "Submit");
        } else if(streqi(type,"image")){
            const char *alt=node_get_attr(elem,"alt");
            const char *src=node_get_attr(elem,"src");
            label=xstrdup(alt && *alt ? alt : (src? src : "Image"));

            const char *src2=node_get_attr(elem,"src");
            if(src2 && *src2){

                const char *base=c->base_url;
                CachedImage *img=NULL;
                if(strncmp(src2,"data:",5)==0) img=image_cache_get(src2, base, c->renderer);
                else {
                    img=image_cache_lookup(src2, base);
                    if(!img) image_cache_fetch_async(src2, base);
                }
                if(img && !img->failed && img->surf){
                    int intrinsic_w=img->w, intrinsic_h=img->h;
                    if(intrinsic_w<=0) intrinsic_w=80;
                    if(intrinsic_h<=0) intrinsic_h=24;
                    int avail_w=c->content_w - c->indent;
                    int desired_w=intrinsic_w, desired_h=intrinsic_h;
                    if(desired_w>avail_w){ double r=(double)avail_w/desired_w; desired_w=avail_w; desired_h=(int)(desired_h*r); }
                    int x=c->content_x + c->indent;
                    if(c->center_align){ int avail=c->content_w - c->indent; if(desired_w<avail) x+=(avail-desired_w)/2; }
                    int y=c->cur_y;
                    SDL_Texture *tex=img->tex;
                    if(!tex && c->renderer && img->surf) { tex=SDL_CreateTextureFromSurface(c->renderer, img->surf); img->tex=tex; }
                    if(tex){
                        GuiCmd cmd={0}; cmd.type=GCMD_IMAGE; cmd.x=x; cmd.y=y; cmd.w=desired_w; cmd.h=desired_h; cmd.tex=tex;
                        if(label) cmd.href=xstrdup(label); else cmd.href=xstrdup(src2);
                        if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
                        c->cmds[c->ncmd++]=cmd;

                        Node *form=find_ancestor_form(elem);
                        char *href=NULL;
                        if(form) href=build_form_url(form, elem, c->base_url);
                        else {
                            const char *a=node_get_attr(elem,"formaction");
                            if(a) href=image_resolve_url(a, c->base_url);
                            else href=xstrdup(label?label:"");
                        }
                        if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
                        c->links[c->nlink].r=(SDL_Rect){x,y,desired_w,desired_h};
                        c->links[c->nlink].href=href;
                        c->nlink++;
                        c->cur_y+=desired_h+6;
                        free(label);
                        return;
                    }
                }
            }
            if(!label) label=xstrdup("Submit");
        } else if(streqi(type,"hidden")){
            free(label);
            return;
        } else if(streqi(type,"checkbox")||streqi(type,"radio")){
            const char *v=node_get_attr(elem,"value");
            label=xstrdup(v? v : (streqi(type,"checkbox")?"[ ]":"( )"));
        } else {

            const char *placeholder=node_get_attr(elem,"placeholder");
            const char *name=node_get_attr(elem,"name");
            const char *val=node_get_attr(elem,"value");
            char buf[256];
            if(val && *val) snprintf(buf,sizeof(buf),"[ %s ]", val);
            else if(placeholder && *placeholder) snprintf(buf,sizeof(buf),"[ %s ]", placeholder);
            else if(name && *name) snprintf(buf,sizeof(buf),"[ %s ]", name);
            else snprintf(buf,sizeof(buf),"[ input ]");
            label=xstrdup(buf);
        }
    } else if(streqi(tag,"button")){
        char *txt=node_text_content(elem);

        char *t=xstrdup(txt?txt:"Button");
        str_trim_inplace(t);
        if(!*t){ free(t); t=xstrdup("Button"); }
        label=t;
        free(txt);
    } else if(streqi(tag,"select")||streqi(tag,"textarea")){
        const char *name=node_get_attr(elem,"name");
        char buf[128]; snprintf(buf,sizeof(buf),"[ %s ]", name?name:"select");
        label=xstrdup(buf);
    } else {
        label=xstrdup(tag);
    }

    int fs=14;
    TTF_Font *f=gui_get_font(fs,false,false,false);
    int tw=0,th=0;
    if(f) TTF_SizeUTF8(f,label,&tw,&th); else { tw=(int)strlen(label)*8; th=14; }
    int bw=tw+20, bh=th+10;
    if(bw<60) bw=60;
    int x=c->content_x + c->indent;
    if(c->center_align){ int avail=c->content_w - c->indent; if(bw<avail) x+=(avail-bw)/2; }
    int y=c->cur_y;

    SDL_Color bg={220,220,220,255}, border={160,160,160,255};
    const char *bg_s=node_style_get(elem,"background-color");
    if(!bg_s) bg_s=node_style_get(elem,"background");
    if(bg_s){ SDL_Color bc=hex_to_sdl(bg_s); if(bc.r||bc.g||bc.b) bg=bc; }
    GuiCmd rect={0}; rect.type=GCMD_RECT; rect.x=x; rect.y=y; rect.w=bw; rect.h=bh; rect.bg=bg; rect.color=border;
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=rect;
    GuiCmd txt={0}; txt.type=GCMD_TEXT; txt.text=xstrdup(label); txt.font_size=fs; txt.color=(SDL_Color){30,30,30,255}; txt.x=x+10; txt.y=y+5; txt.w=tw; txt.h=th;
    if(c->ncmd>=c->cap){ c->cap=c->cap?c->cap*2:64; c->cmds=(GuiCmd*)xrealloc(c->cmds,c->cap*sizeof(GuiCmd)); }
    c->cmds[c->ncmd++]=txt;

    Node *form=find_ancestor_form(elem);
    char *href=NULL;
    if(form){
        href=build_form_url(form, elem, c->base_url);
    } else {
        const char *fa=node_get_attr(elem,"formaction");
        if(fa) href=image_resolve_url(fa, c->base_url);
        else {

            href=xstrdup("");
        }
        if(!href || !*href){
            free(href);

            href=xstrdup(label);
        }
    }
    if(href && *href){
        if(c->nlink>=c->link_cap){ c->link_cap=c->link_cap?c->link_cap*2:16; c->links=(void*)xrealloc(c->links,c->link_cap*sizeof(*c->links)); }
        c->links[c->nlink].r=(SDL_Rect){x,y,bw,bh};
        c->links[c->nlink].href=href;
        c->nlink++;
    } else free(href);
    c->cur_y+=bh+6;
    free(label);
}

static void layout_node_recursive(Node *n, LayoutCtx *c);

static void layout_children(Node *parent, LayoutCtx *c){
    for(size_t i=0;i<parent->child_count;i++) layout_node_recursive(parent->children[i],c);
}

static bool layout_should_hide(Node *n){
    if(!n || n->type!=NODE_ELEMENT) return false;
    const char *tag=n->tag;
    if(streqi(tag,"head")||streqi(tag,"meta")||streqi(tag,"link")||streqi(tag,"title")||streqi(tag,"style")||streqi(tag,"script")||streqi(tag,"noscript")||streqi(tag,"template")) return true;
    const char *disp=node_style_get(n,"display");
    if(disp && streqi(disp,"none")) return true;
    const char *pos=node_style_get(n,"position");
    if(pos && (streqi(pos,"absolute")||streqi(pos,"fixed"))){

        if(node_has_class(n,"hero-bg")||node_has_class(n,"grain")||node_has_class(n,"hero-torn")) return true;

        if(streqi(tag,"button") && node_has_class(n,"to-top")) return true;
    }

    if(node_get_attr(n,"hidden")!=NULL) return true;

    return false;
}
static bool is_flex(Node *n){
    const char *d=node_style_get(n,"display");
    return d && (streqi(d,"flex")||streqi(d,"inline-flex"));
}
static bool is_grid(Node *n){
    const char *d=node_style_get(n,"display");
    return d && (streqi(d,"grid")||streqi(d,"inline-grid"));
}
static int parse_gap_px(Node *n, int containing_w, int font_size){
    const char *g=node_style_get(n,"gap");
    if(!g) g=node_style_get(n,"column-gap");
    if(!g) g=node_style_get(n,"grid-gap");
    if(!g) return 0;

    char *copy=xstrdup(g);
    char *sp=strchr(copy,' ');
    if(sp) *sp='\0';
    int v=0;

    char *end=NULL; double num=strtod(copy,&end);
    if(end!=copy){
        if(strstr(copy,"rem")) v=(int)(num*16);
        else if(strstr(copy,"em")) v=(int)(num*font_size);
        else v=(int)num;
    }
    free(copy);
    if(v<0) v=0;

    if(v>50) v=50;
    (void)containing_w;
    return v;
}
static int grid_column_count(Node *n, int containing_w){
    const char *gtc=node_style_get(n,"grid-template-columns");

    if(gtc){
        if(strstr(gtc,"250px")) return 2;
        if(strstr(gtc,"repeat")){

            int min_col=280;
            const char *p=strstr(gtc,"minmax");
            if(p){

                const char *q=p+6;
                while(*q && (*q=='('||isspace((unsigned char)*q))) q++;
                char *end=NULL; double v=strtod(q,&end);
                if(end!=q) min_col=(int)v;
            }
            int cols = containing_w / (min_col + 16);
            if(cols<1) cols=1;
            if(cols>3) cols=3;
            if(cols<2) cols=2;
            return cols;
        }

        int cnt=1;
        int depth=0; bool inq=false; char qq=0;
        for(const char *cc=gtc; *cc; cc++){
            if(*cc=='"'||*cc=='\''){ if(!inq){inq=true;qq=*cc;} else if(*cc==qq) inq=false; }
            if(inq) continue;
            if(*cc=='(') depth++; else if(*cc==')' && depth>0) depth--;
            else if(*cc==' ' && depth==0) cnt++;
        }
        if(cnt>1) return cnt;
    }

    if(node_has_class(n,"reader")) return 2;
    if(node_has_class(n,"doc-grid")) return 3;
    return 1;
}

static int estimate_subtree_text_width(Node *n){
    int w=0;
    if(!n) return 0;
    if(n->type==NODE_TEXT){
        const char *txt=n->text? n->text: "";
        Node *pe=n->parent;
        int fs= pe? node_font_size(pe):16;
        bool bold=false, italic=false, mono=false;
        if(pe){
            const char *fw=node_style_get(pe,"font-weight");
            if(fw && (strcasecmp(fw,"bold")==0 || atoi(fw)>=600)) bold=true;
            if(streqi(pe->tag,"b")||streqi(pe->tag,"strong")||streqi(pe->tag,"h1")||streqi(pe->tag,"h2")||streqi(pe->tag,"h3")) bold=true;
            const char *fst=node_style_get(pe,"font-style");
            if(fst && streqi(fst,"italic")) italic=true;
            if(streqi(pe->tag,"i")||streqi(pe->tag,"em")) italic=true;
            const char *ff=node_style_get(pe,"font-family");
            if(ff && (strstr(ff,"Mono")||strstr(ff,"mono"))) mono=true;
        }
        TTF_Font *f=gui_get_font(fs,bold,italic,mono);
        const char *p=txt;
        bool first=true;
        while(*p){
            while(*p && isspace((unsigned char)*p)) p++;
            if(!*p) break;
            const char *start=p;
            while(*p && !isspace((unsigned char)*p)) p++;
            size_t len=(size_t)(p-start);
            char *word=xstrndup(start,len);
            int ww=0, hh=0;
            if(f) TTF_SizeUTF8(f,word,&ww,&hh); else ww=(int)len*(fs*6/10);
            free(word);
            if(!first){
                int sw=0; if(f) TTF_SizeUTF8(f," ",&sw,NULL); else sw=fs/3;
                w+=sw;
            }
            w+=ww;
            first=false;
        }
        return w;
    }
    for(size_t i=0;i<n->child_count;i++) w+=estimate_subtree_text_width(n->children[i]);
    return w;
}
static int estimate_flex_item_width(Node *item, int containing_w){
    if(!item) return 0;
    if(layout_should_hide(item)) return 0;
    const char *ws=node_style_get(item,"width");
    if(ws && !streqi(ws,"auto")){
        BoxModel bm; compute_box_model(item, containing_w, &bm);
        return bm.outer_w>0? bm.outer_w: 100;
    }

    if(is_flex(item) || is_grid(item)){
        const char *fd=node_style_get(item,"flex-direction");
        bool is_column = fd && streqi(fd,"column");
        if(is_column){
            int maxw=0;
            for(size_t i=0;i<item->child_count;i++){
                int cw=estimate_flex_item_width(item->children[i], containing_w);
                if(cw>maxw) maxw=cw;
            }
            BoxModel bm; compute_box_model(item, containing_w, &bm);
            int pad = bm.pl+bm.pr+bm.bl+bm.br+bm.ml+bm.mr;
            return (maxw>0? maxw:80)+pad;
        } else {
            int gap=parse_gap_px(item, containing_w, node_font_size(item));
            int total=0; int cnt=0;
            for(size_t i=0;i<item->child_count;i++){
                if(layout_should_hide(item->children[i])) continue;
                int cw=estimate_flex_item_width(item->children[i], containing_w);
                if(cnt>0) total+=gap;
                total+=cw;
                cnt++;
            }
            if(cnt==0){
                int tw=estimate_subtree_text_width(item);
                BoxModel bm; compute_box_model(item, containing_w, &bm);
                int pad=bm.pl+bm.pr+bm.bl+bm.br+bm.ml+bm.mr;
                return tw+pad;
            }
            BoxModel bm; compute_box_model(item, containing_w, &bm);
            int pad=bm.pl+bm.pr+bm.bl+bm.br+bm.ml+bm.mr;
            return total+pad;
        }
    }
    int tw=estimate_subtree_text_width(item);
    BoxModel bm; compute_box_model(item, containing_w, &bm);
    int pad=bm.pl+bm.pr+bm.bl+bm.br+bm.ml+bm.mr;
    int outer=tw+pad;
    if(outer<30) outer=30;

    if(outer>containing_w) outer=containing_w;
    return outer;
}
static void layout_flex_row(Node *parent, LayoutCtx *c, int content_x, int content_w, int start_y, int gap, bool center){
    int nchild=(int)parent->child_count;
    int *widths=(int*)xmalloc(nchild*sizeof(int));
    int total=0, visible=0;
    for(int i=0;i<nchild;i++){
        Node *ch=parent->children[i];
        if(layout_should_hide(ch)){ widths[i]=0; continue; }

        if(ch->type==NODE_TEXT){
            const char *t=ch->text;
            bool ws=true;
            for(const char *cc=t; *cc; cc++) if(!isspace((unsigned char)*cc)) ws=false;
            if(ws){ widths[i]=0; continue; }
        }
        int w=estimate_flex_item_width(ch, content_w);
        widths[i]=w;
        if(visible>0) total+=gap;
        total+=w;
        visible++;
    }
    int cur_x=content_x;
    if(center && total < content_w) cur_x += (content_w - total)/2;
    else if(parent && node_has_class(parent,"site-header")){

    }
    int max_h=0;
    for(int i=0;i<nchild;i++){
        Node *ch=parent->children[i];
        if(widths[i]==0) continue;
        int child_w=widths[i];
        int saved_cx=c->content_x, saved_cw=c->content_w, saved_y=c->cur_y, saved_indent=c->indent;
        bool saved_center=c->center_align;
        c->content_x=cur_x;
        c->content_w=child_w;
        c->cur_y=start_y;
        c->indent=0;

        c->center_align=false;
        c->center_depth=0;
        layout_node_recursive(ch,c);
        lctx_flush(c);
        int child_h=c->cur_y - start_y;
        if(child_h>max_h) max_h=child_h;
        c->content_x=saved_cx; c->content_w=saved_cw; c->cur_y=saved_y; c->indent=saved_indent; c->center_align=saved_center;
        cur_x += child_w + gap;
    }
    free(widths);
    c->cur_y = start_y + (max_h>0? max_h: 16);
}
static int parse_css_len_px(const char *s, int containing, int font_size, int win_w, int win_h){
    if(!s) return 0;
    char *t=xstrdup(s); str_trim_inplace(t);
    for(char *cc=t; *cc; cc++) *cc=(char)tolower((unsigned char)*cc);
    char *end=NULL; double v=strtod(t,&end);
    if(end==t){ free(t); return 0; }
    int res=0;
    if(strstr(t,"vh")) res=(int)(v*win_h/100.0);
    else if(strstr(t,"vw")) res=(int)(v*win_w/100.0);
    else if(strstr(t,"ch")) res=(int)(v*8);
    else if(strstr(t,"rem")) res=(int)(v*16);
    else if(strstr(t,"em")) res=(int)(v*font_size);
    else if(strstr(t,"%")) res=(int)(v*containing/100.0);
    else if(strstr(t,"px")) res=(int)v;
    else if(strstr(t,"pt")) res=(int)(v*4/3);
    else res=(int)v;
    free(t);
    return res;
}
static int resolve_len_with_min(const char *s, int containing, int font_size, int win_w, int win_h){
    if(!s) return 0;
    char *t=xstrdup(s); str_trim_inplace(t);

    if(strncasecmp(t,"min(",4)==0){
        char *p=strchr(t,'('); char *q=strrchr(t,')');
        if(p&&q&&q>p){
            char *inner=xstrndup(p+1,(size_t)(q-p-1));

            int best=INT_MAX;
            char *copy=xstrdup(inner);
            char *tok=strtok(copy,",");
            while(tok){
                str_trim_inplace(tok);
                int px=parse_css_len_px(tok, containing, font_size, win_w, win_h);
                if(px < best) best=px;
                tok=strtok(NULL,",");
            }
            free(copy); free(inner); free(t);
            return best==INT_MAX?0:best;
        }
    }
    if(strncasecmp(t,"max(",4)==0){
        char *p=strchr(t,'('); char *q=strrchr(t,')');
        if(p&&q&&q>p){
            char *inner=xstrndup(p+1,(size_t)(q-p-1));
            int best=0;
            char *copy=xstrdup(inner);
            char *tok=strtok(copy,",");
            while(tok){
                str_trim_inplace(tok);
                int px=parse_css_len_px(tok, containing, font_size, win_w, win_h);
                if(px > best) best=px;
                tok=strtok(NULL,",");
            }
            free(copy); free(inner); free(t);
            return best;
        }
    }
    int r=parse_css_len_px(t, containing, font_size, win_w, win_h);
    free(t);
    return r;
}

static void layout_grid_cells(Node *parent, LayoutCtx *c, int content_x, int content_w, int start_y, int gap, int cols){
    int nchild=(int)parent->child_count;

    Node **visible=(Node**)xmalloc(nchild*sizeof(Node*));
    int vcnt=0;
    for(int i=0;i<nchild;i++){
        Node *ch=parent->children[i];
        if(layout_should_hide(ch)) continue;
        if(ch->type==NODE_TEXT){
            const char *t=ch->text;
            bool ws=true;
            for(const char *cc=t;*cc;cc++) if(!isspace((unsigned char)*cc)) ws=false;
            if(ws) continue;
        }
        visible[vcnt++]=ch;
    }
    if(vcnt==0){ free(visible); return; }
    int effective_gap = gap>0? gap: 16;
    if(node_has_class(parent,"reader")) effective_gap=42;
    if(node_has_class(parent,"doc-grid")) effective_gap=16;
    int col_w = (content_w - (cols-1)*effective_gap)/cols;
    if(col_w<80) col_w=80;

    int col_widths[4]={0};
    if(node_has_class(parent,"reader") && cols==2){
        col_widths[0]=250;
        col_widths[1]= content_w - 250 - effective_gap;
        if(col_widths[1]<200) col_widths[1]=200;
    } else {
        for(int i=0;i<cols;i++) col_widths[i]=col_w;
    }
    int row=0, col=0;
    int row_y=start_y;
    int row_max_h=0;
    int col_x[4]={0};
    col_x[0]=content_x;
    for(int i=1;i<cols;i++) col_x[i]=col_x[i-1]+col_widths[i-1]+effective_gap;
    for(int i=0;i<vcnt;i++){
        Node *ch=visible[i];
        int cur_col = col;
        int cur_row = row;
        int cell_x = col_x[cur_col];
        int cell_w = col_widths[cur_col];
        int saved_cx=c->content_x, saved_cw=c->content_w, saved_y=c->cur_y, saved_indent=c->indent;
        bool saved_center=c->center_align;
        c->content_x=cell_x;
        c->content_w=cell_w;
        c->cur_y=row_y;
        c->indent=0;
        c->center_align=false;
        c->center_depth=0;
        layout_node_recursive(ch,c);
        lctx_flush(c);
        int child_h=c->cur_y - row_y;
        if(child_h>row_max_h) row_max_h=child_h;
        c->content_x=saved_cx; c->content_w=saved_cw; c->cur_y=saved_y; c->indent=saved_indent; c->center_align=saved_center;
        col++;
        if(col>=cols){
            row_y += row_max_h + effective_gap;
            row_max_h=0;
            col=0; row++;
        }
        (void)cur_row;
    }

    row_y += row_max_h;
    c->cur_y = row_y;
    free(visible);
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

                    lctx_emit_word(c, line, pe, false);
                }
                lctx_flush(c);
                line = strtok(NULL,"\n");
            }
            free(copy);
            return;
        }

        const char *p=txt;
        bool first_word=true;
        while(*p){
            while(*p && isspace((unsigned char)*p)) p++;
            if(!*p) break;
            const char *wstart=p;
            bool glue = first_word && !c->pending_space && wstart==txt;
            while(*p && !isspace((unsigned char)*p)) p++;
            size_t wl = p - wstart;
            char *word = xstrndup(wstart,wl);
            lctx_emit_word(c, word, pe, glue);
            free(word);
            first_word=false;
            c->pending_space = false;
        }
        if(*txt && isspace((unsigned char)txt[strlen(txt)-1])) c->pending_space = true;
        return;
    }
    if(n->type==NODE_DOCUMENT){
        layout_children(n,c);
        lctx_flush(c);
        return;
    }

    const char *tag = n->tag;

    if(layout_should_hide(n)) return;
    const char *disp = node_style_get(n,"display");
    bool disp_is_block=false;
    if(disp){
        if(streqi(disp,"block")||streqi(disp,"flex")||streqi(disp,"grid")) disp_is_block=true;
        else if(streqi(disp,"inline-block")) disp_is_block=true;
    }
    bool is_block = is_block_tag(tag) || disp_is_block || streqi(tag,"hr") || streqi(tag,"br") || streqi(tag,"img") || streqi(tag,"li");

    int saved_indent = c->indent;
    bool saved_center = c->center_align;
    const char *saved_href = c->cur_href;
    const char *href = node_get_attr(n,"href");
    if(href && streqi(tag,"a")) c->cur_href = href;

    const char *ta = node_style_get(n,"text-align");
    bool pushed_center=false;
    if(ta && streqi(ta,"center")){
        if(c->center_depth < 32) c->center_stack[c->center_depth++]=c->center_align;
        c->center_align=true;
        pushed_center=true;
    } else if(is_block && c->center_align){

    }

    bool has_bg=false; SDL_Color bgcol={0};
    const char *bg = node_style_get(n,"background");
    const char *bgc = node_style_get(n,"background-color");

    const char *bg_try = bg;
    char *norm_try = bg_try? normalize_color(bg_try): NULL;
    bool bg_ok = norm_try && *norm_try;
    free(norm_try);
    if(!bg_ok) bg_try = bgc;
    if(bg_try && *bg_try){ char *norm=normalize_color(bg_try); if(norm && *norm){ bgcol=hex_to_sdl(norm); has_bg=true; } free(norm); }

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
    if(streqi(tag,"input")||streqi(tag,"button")||streqi(tag,"select")||streqi(tag,"textarea")){
        lctx_emit_control(c,n);
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

        const char *maxw_s = node_style_get(n,"max-width");
        if(maxw_s){
            int max_px = resolve_len_with_min(maxw_s, containing_w, node_font_size(n), c->win_w, c->win_h);
            if(max_px>0 && box.outer_w > max_px){
                int new_outer = max_px;
                int new_content = new_outer - (box.pl+box.pr+box.bl+box.br);
                if(new_content<0) new_content=0;
                bool is_centered=false;
                const char *m=node_style_get(n,"margin");
                if(m && strstr(m,"auto")) is_centered=true;
                const char *mls=node_style_get(n,"margin-left");
                const char *mrs=node_style_get(n,"margin-right");
                if((mls && streqi(mls,"auto")) || (mrs && streqi(mrs,"auto"))) is_centered=true;
                if(node_has_class(n,"wrap")||node_has_class(n,"hero-inner")||node_has_class(n,"intro")||node_has_class(n,"paper-sheet")||node_has_class(n,"prose")) is_centered=true;
                if(is_centered){
                    box.ml = (containing_w - new_outer)/2;
                    box.mr = containing_w - new_outer - box.ml;
                    if(box.ml<0) box.ml=0;
                    if(box.mr<0) box.mr=0;
                }
                box.outer_w = new_outer;
                box.content_w = new_content;
                box.width = box.border_box? new_outer : new_content;
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

        if(is_flex(n)){
            int gap = parse_gap_px(n, containing_w, node_font_size(n));
            const char *jc=node_style_get(n,"justify-content");
            bool center = jc && streqi(jc,"center");

            layout_flex_row(n,c,content_x,content_w,c->cur_y,gap,center);

            lctx_flush(c);
        } else if(is_grid(n)){
            int gap = parse_gap_px(n, containing_w, node_font_size(n));
            int cols = grid_column_count(n, containing_w);
            layout_grid_cells(n,c,content_x,content_w,c->cur_y,gap,cols);
            lctx_flush(c);
        } else {
            layout_children(n,c);
            lctx_flush(c);
        }

        c->cur_y += box.pb + box.bb;

        const char *minh_s = node_style_get(n,"min-height");
        if(minh_s){
            int min_px = resolve_len_with_min(minh_s, containing_w, node_font_size(n), c->win_w, c->win_h);
            if(min_px>0){
                int cur_h = c->cur_y - block_outer_y;
                if(cur_h < min_px) c->cur_y += (min_px - cur_h);
            }
        }
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
    if(pushed_center){
        c->center_depth--;
        c->center_align = c->center_stack[c->center_depth];
    }

    c->cur_href = saved_href;
    c->indent = saved_indent;
    if(pushed_center) {  }
}

static void gui_build_layout(Node *root, LayoutCtx *out, int win_w,int win_h){
    lctx_init(out,win_w,win_h);
    layout_node_recursive(root,out);
    lctx_flush(out);
}
static void gui_build_layout_with_state(Node *root, LayoutCtx *out, int win_w,int win_h, BrowserState *bstate, SDL_Renderer *ren);

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
static void gui_build_layout_with_state(Node *root, LayoutCtx *out, int win_w,int win_h, BrowserState *bstate, SDL_Renderer *ren){
    lctx_init(out,win_w,win_h);
    out->base_url = bstate ? bstate->current_url : NULL;
    out->renderer = ren;
    layout_node_recursive(root,out);
    lctx_flush(out);
}
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
static bool g_mojeek_image_mode = false;
void set_mojeek_image_mode(bool on){ g_mojeek_image_mode = on; }
bool get_mojeek_image_mode(void){ return g_mojeek_image_mode; }

static char *make_mojeek_image_search_url(const char *query){
    char *enc=url_encode(query);
    char *u=(char*)xmalloc(strlen(enc)+128);

    sprintf(u,"https://www.mojeek.com/search?fmt=images&q=%s",enc);
    free(enc); return u;
}
static bool is_mojeek_image_query(const char *q, const char **out_rest){
    if(!q) return false;
    const char *prefixes[] = {"!mojeek ", "!mojeek:", "!m ", "!m:", "mojeek:", "mojeek ", "images:", "images ", "img:", "img ", NULL};
    for(int i=0; prefixes[i]; i++){
        size_t plen=strlen(prefixes[i]);
        if(strncasecmp(q, prefixes[i], plen)==0){
            const char *rest=q+plen;
            while(*rest && isspace((unsigned char)*rest)) rest++;
            if(out_rest) *out_rest=rest;
            return true;
        }
    }
    if(g_mojeek_image_mode){
        if(out_rest) *out_rest=q;
        return true;
    }
    return false;
}
static char *make_search_url(const char *query){
    const char *rest=NULL;
    if(is_mojeek_image_query(query, &rest)){
        return make_mojeek_image_search_url(rest);
    }
    return search_build_url(query);
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

    char *linked=NULL; size_t linked_len=0;
    if(b->root) linked=load_linked_stylesheets(b->root, url_for_addr, &linked_len);
    else { linked=xstrdup(""); linked_len=0; }
    char *combined=(char*)xmalloc(style_len+linked_len+2);
    combined[0]='\0';
    if(style) strcat(combined, style);
    if(linked_len>0){
        if(style_len>0) strcat(combined,"\n");
        strcat(combined, linked);
    }
    free(linked);
    b->rules = parse_css(combined,&b->nrule);

    free(b->style_text);
    b->style_text=combined;
    apply_styles(b->root,b->rules,b->nrule);

    if(url_for_addr){
        free(b->current_url); b->current_url=xstrdup(url_for_addr);
        addr_set_text(b, url_for_addr);
    }
    gui_build_layout_with_state(b->root,&b->lctx,b->win_w,b->win_h,b,b->ren);

    int max_y=0;
    for(size_t i=0;i<b->lctx.ncmd;i++) if(b->lctx.cmds[i].type==GCMD_TEXT||b->lctx.cmds[i].type==GCMD_RECT||b->lctx.cmds[i].type==GCMD_IMAGE) {
        int bottom = b->lctx.cmds[i].y + b->lctx.cmds[i].h;
        if(bottom>max_y) max_y=bottom;
    }
    if(max_y < b->lctx.content_h) max_y = b->lctx.content_h;

    if(b->lctx.cur_y>max_y) max_y=b->lctx.cur_y;
    b->total_h = max_y - b->lctx.content_y + 20;
    b->scroll_y=0;
    browser_set_status(b,"Loaded %zu bytes, %zu rules, %zu items", hlen, b->nrule, b->lctx.ncmd);
}

static char *url_percent_decode(const char *src){
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
static char *extract_uddg_target(const char *url){
    if(!url) return NULL;

    const char *p=strstr(url,"uddg=");
    if(!p) return NULL;
    p+=5;
    const char *end=strchr(p,'&');
    size_t vlen=end ? (size_t)(end-p) : strlen(p);

    const char *hash=strchr(p,'#');
    if(hash && (!end || hash<end)) vlen=(size_t)(hash-p);
    if(vlen==0) return NULL;
    char *enc=xstrndup(p,vlen);
    char *dec=url_percent_decode(enc);
    free(enc);

    if(strchr(dec,'%')){
        char *dec2=url_percent_decode(dec);
        free(dec);
        dec=dec2;
    }
    if(strncmp(dec,"http://",7)==0 || strncmp(dec,"https://",8)==0){
        return dec;
    }
    free(dec);
    return NULL;
}
static void browser_navigate(BrowserState *b, const char *input, bool push_hist){
    if(!input||!*input){ browser_set_status(b,"Empty address"); return; }
    char *trimmed=xstrdup(input); str_trim_inplace(trimmed);
    if(!*trimmed){ free(trimmed); return; }

    char *uddg = extract_uddg_target(trimmed);
    if(uddg){
        free(trimmed);
        trimmed=uddg;
        browser_set_status(b,"Redirecting to %s", trimmed);
    }

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
    } else if(strncmp(trimmed,"//",2)==0){
        fetch_url_str=(char*)xmalloc(strlen(trimmed)+8+1); sprintf(fetch_url_str,"https:%s",trimmed);
    } else if((trimmed[0]=='/' || trimmed[0]=='#') && b->current_url && strncmp(b->current_url,"http",4)==0){
        char *resolved=image_resolve_url(trimmed, b->current_url);
        if(resolved && (strncmp(resolved,"http://",7)==0||strncmp(resolved,"https://",8)==0)){
            fetch_url_str=resolved;
        } else {
            free(resolved);
            FILE *f=fopen(trimmed,"rb");
            if(f){ fclose(f); is_file_path=true; }
            else fetch_url_str=make_search_url(trimmed);
        }
    } else {

        FILE *f=fopen(trimmed,"rb");
        if(f){ fclose(f); is_file_path=true; }
        else if(looks_like_url(trimmed) && !strchr(trimmed,' ')){

            fetch_url_str=(char*)xmalloc(strlen(trimmed)+8+1); sprintf(fetch_url_str,"https://%s",trimmed);
        } else if(b->current_url && strncmp(b->current_url,"http",4)==0 && !strchr(trimmed,' ') && strchr(trimmed,'/')){

            char *resolved=image_resolve_url(trimmed, b->current_url);
            if(resolved && (strncmp(resolved,"http://",7)==0||strncmp(resolved,"https://",8)==0)){

                bool looks_like_domain = (strchr(trimmed,'.') && !strchr(trimmed,'/')) || (strchr(trimmed,'.') && strchr(trimmed,'/') && strchr(trimmed,'.') < strchr(trimmed,'/'));
                if(!looks_like_domain){
                    fetch_url_str=resolved;
                } else {
                    free(resolved);
                    fetch_url_str=make_search_url(trimmed);
                }
            } else {
                free(resolved);
                fetch_url_str=make_search_url(trimmed);
            }
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
    gui_build_layout_with_state(b->root,&b->lctx,b->win_w,b->win_h,b,b->ren);
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


static void fill_rrect(SDL_Renderer *ren, float x, float y, float w, float h, float r, SDL_Color c){
    if(r > h/2) r = h/2;
    if(r > w/2) r = w/2;
    SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a?c.a:255);
    float step = 1.0f / g_dpi;
    for(float dy=0; dy<h; dy+=step){
        float cy = dy + step/2, inset = 0;
        if(cy < r)        { float t = r - cy;     inset = r - sqrtf(fmaxf(0.f, r*r - t*t)); }
        else if(cy > h-r) { float t = cy-(h-r);   inset = r - sqrtf(fmaxf(0.f, r*r - t*t)); }
        SDL_FRect row = { x + inset, y + dy, w - 2*inset, step };
        SDL_RenderFillRectF(ren,&row);
    }
}
static void stroke_rrect(SDL_Renderer *ren, float x,float y,float w,float h,float r,float t,SDL_Color border,SDL_Color inner){
    fill_rrect(ren,x,y,w,h,r,border);
    fill_rrect(ren,x+t,y+t,w-2*t,h-2*t,fmaxf(0.f,r-t),inner);
}
static void thick_line(SDL_Renderer *ren,float x1,float y1,float x2,float y2,float th,SDL_Color c){
    SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,255);
    float dx=x2-x1, dy=y2-y1, len=sqrtf(dx*dx+dy*dy); if(len<0.001f) return;
    float nx=-dy/len, ny=dx/len, step=1.0f/g_dpi;
    for(float o=-th/2; o<=th/2+0.001f; o+=step) SDL_RenderDrawLineF(ren,x1+nx*o,y1+ny*o,x2+nx*o,y2+ny*o);
}
static void icon_chevron(SDL_Renderer *ren,float cx,float cy,int dir,SDL_Color c){
    float s=5.5f, tip=cx+dir*s*0.5f, arm=tip-dir*s;
    thick_line(ren,arm,cy-s,tip,cy,1.8f,c);
    thick_line(ren,tip,cy,arm,cy+s,1.8f,c);
}
static void icon_reload(SDL_Renderer *ren,float cx,float cy,SDL_Color c){
    float r=6.5f; const float a0=-50*3.14159265f/180, a1=240*3.14159265f/180;
    int seg=24; float px=cx+r*cosf(a0), py=cy+r*sinf(a0);
    for(int i=1;i<=seg;i++){
        float a=a0+(a1-a0)*i/seg, x=cx+r*cosf(a), y=cy+r*sinf(a);
        thick_line(ren,px,py,x,y,1.8f,c); px=x; py=y;
    }
    float hx=cx+r*cosf(a0), hy=cy+r*sinf(a0);
    thick_line(ren,hx,hy,hx+5.5f,hy-0.5f,1.8f,c);
    thick_line(ren,hx,hy,hx+0.5f,hy+5.5f,1.8f,c);
}
static int draw_text(SDL_Renderer *ren,const char *text,int sz,bool bold,SDL_Color col,float x,float y,float center_in_w,float center_in_h){
    if(!text||!*text) return 0;
    TTF_Font *f=gui_get_render_font(sz,bold,false,false);
    if(!f) return 0;
    SDL_Surface *s=TTF_RenderUTF8_Blended(f,text,col);
    if(!s) return 0;
    SDL_Texture *t=SDL_CreateTextureFromSurface(ren,s);
    float w=s->w/g_dpi, h=s->h/g_dpi;
    SDL_FRect dst={ center_in_w>0? x+(center_in_w-w)/2 : x, center_in_h>0? y+(center_in_h-h)/2 : y, w, h };
    if(t){ SDL_RenderCopyF(ren,t,NULL,&dst); SDL_DestroyTexture(t); }
    SDL_FreeSurface(s);
    return (int)(w+0.5f);
}
static int text_width(const char *text,int sz,bool bold){
    TTF_Font *f=gui_get_font(sz,bold,false,false); int w=0;
    if(f && text) TTF_SizeUTF8(f,text,&w,NULL);
    return w;
}
static void free_text_textures(BrowserState *b){
    for(size_t i=0;i<b->lctx.ncmd;i++){
        GuiCmd *c=&b->lctx.cmds[i];
        if(c->type==GCMD_TEXT && c->tex){ SDL_DestroyTexture(c->tex); c->tex=NULL; }
    }
}
static void browser_relayout(BrowserState *b);
static void update_dpi(BrowserState *b){
    int ow=0,oh=0; SDL_GetRendererOutputSize(b->ren,&ow,&oh);
    float d = (b->win_w>0 && ow>0) ? (float)ow/(float)b->win_w : 1.0f;
    if(d<1.0f) d=1.0f;
    bool changed = fabsf(d-g_dpi)>0.01f;
    if(changed){
        g_dpi=d;
        free_text_textures(b);
        gui_free_fonts();
    }
    SDL_RenderSetScale(b->ren,g_dpi,g_dpi);
    if(changed && b->root) browser_relayout(b);
}

static void render_browser(BrowserState *b){
    SDL_Renderer *ren=b->ren;
    update_dpi(b);

    SDL_SetRenderDrawColor(ren,255,255,255,255);
    SDL_RenderClear(ren);

    SDL_Color body_bg={255,255,255,255};
    if(b->root){
        Node *body=dom_find_tag(b->root,"body");
        if(body){
            const char *bg = node_style_get(body,"background");
            const char *bgc = node_style_get(body,"background-color");
            const char *use = bg;
            char *norm = bg? normalize_color(bg): NULL;
            bool ok = norm && *norm;
            free(norm);
            if(!ok) use = bgc;
            if(use) body_bg=hex_to_sdl(use);
        }
    }

    const SDL_Color C_BAR   ={241,243,244,255}, C_LINE={218,220,224,255}, C_ICON={60,64,67,255},
                    C_ICON_OFF={189,193,198,255}, C_PILL={233,236,240,255}, C_BLUE={26,115,232,255},
                    C_WHITE={255,255,255,255}, C_TEXT={32,33,36,255}, C_HINT={128,134,139,255};
    SDL_Rect toolbar={0,0,b->win_w,GUI_TOOLBAR_H};
    SDL_SetRenderDrawColor(ren, C_BAR.r,C_BAR.g,C_BAR.b,255);
    SDL_RenderFillRect(ren,&toolbar);
    SDL_SetRenderDrawColor(ren,C_LINE.r,C_LINE.g,C_LINE.b,255);
    SDL_RenderDrawLine(ren,0,GUI_TOOLBAR_H-1, b->win_w, GUI_TOOLBAR_H-1);

    float bcy = GUI_TOOLBAR_H/2.0f - 0.5f;
    float bx0 = 8 + GUI_BTN_W/2.0f;
    icon_chevron(ren, bx0 + 2,                        bcy, -1, b->back_n? C_ICON : C_ICON_OFF);
    icon_chevron(ren, bx0 + (GUI_BTN_W+GUI_BTN_GAP) - 2, bcy,  1, b->fwd_n ? C_ICON : C_ICON_OFF);
    icon_reload (ren, bx0 + 2*(GUI_BTN_W+GUI_BTN_GAP),   bcy, C_ICON);

    SDL_Rect ar = addr_rect(b);
    SDL_Rect go = {ar.x+ar.w+8, 6, 52, GUI_TOOLBAR_H-12};

    if(b->addr_focused) stroke_rrect(ren,ar.x,ar.y,ar.w,ar.h,ar.h/2.0f,2.0f,C_BLUE,C_WHITE);
    else                fill_rrect(ren,ar.x,ar.y,ar.w,ar.h,ar.h/2.0f,C_PILL);

    {
        const int ASZ=14, PADX=16;
        int maxw = ar.w - 2*PADX;
        float start_x = ar.x + PADX;
        SDL_Rect clip={ar.x+PADX-2, ar.y+2, ar.w-2*PADX+4, ar.h-4};
        SDL_RenderSetClipRect(ren,&clip);
        if(b->addr_text && *b->addr_text){
            int tw=text_width(b->addr_text,ASZ,false), offset=0;
            if(tw>maxw && b->addr_focused){
                char *pre = xstrndup(b->addr_text,b->addr_cursor);
                int prew=text_width(pre,ASZ,false); free(pre);
                if(prew > maxw-10) offset = prew - (maxw-10);
            }
            draw_text(ren,b->addr_text,ASZ,false,C_TEXT,start_x-offset,ar.y,0,ar.h);
            if(b->addr_focused && (SDL_GetTicks()/500)%2==0){
                char *pre = xstrndup(b->addr_text,b->addr_cursor);
                int prew=text_width(pre,ASZ,false); free(pre);
                float cx = start_x - offset + prew;
                thick_line(ren,cx,ar.y+8,cx,ar.y+ar.h-8,1.2f,C_TEXT);
            }
        } else {
            draw_text(ren,"Search or enter address",ASZ,false,C_HINT,start_x,ar.y,0,ar.h);
        }
        SDL_RenderSetClipRect(ren,NULL);
    }

    fill_rrect(ren,go.x,go.y,go.w,go.h,go.h/2.0f,C_BLUE);
    draw_text(ren,"Go",14,true,C_WHITE,go.x,go.y,go.w,go.h);

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
        if(cmd->type!=GCMD_IMAGE) continue;
        int draw_y = cmd->y - b->scroll_y;
        if(draw_y + cmd->h < b->lctx.content_y || draw_y > b->lctx.content_y + b->lctx.content_h) continue;
        if(cmd->tex){
            SDL_Rect dst={cmd->x, draw_y, cmd->w, cmd->h};
            SDL_RenderCopy(ren, cmd->tex, NULL, &dst);

            SDL_SetRenderDrawColor(ren, 200,200,200,255);
            SDL_RenderDrawRect(ren,&dst);
        }
    }

    for(size_t i=0;i<b->lctx.ncmd;i++){
        GuiCmd *cmd=&b->lctx.cmds[i];
        if(cmd->type!=GCMD_TEXT) continue;
        int draw_y = cmd->y - b->scroll_y;
        if(draw_y + cmd->h < b->lctx.content_y || draw_y > b->lctx.content_y + b->lctx.content_h) continue;
        if(!cmd->tex){
            TTF_Font *f=gui_get_render_font(cmd->font_size, cmd->bold, cmd->italic, cmd->mono);
            if(!f) continue;
            SDL_Surface *s=TTF_RenderUTF8_Blended(f, cmd->text, cmd->color);
            if(!s) continue;
            cmd->tex=SDL_CreateTextureFromSurface(ren,s);
            cmd->tex_w=s->w; cmd->tex_h=s->h;
            cmd->w=(int)ceilf(s->w/g_dpi); cmd->h=(int)ceilf(s->h/g_dpi);
            SDL_FreeSurface(s);
        }
        if(cmd->tex){
            SDL_FRect dst={(float)cmd->x, (float)draw_y, cmd->tex_w/g_dpi, cmd->tex_h/g_dpi};
            SDL_RenderCopyF(ren,cmd->tex,NULL,&dst);
            if(cmd->underline){
                SDL_SetRenderDrawColor(ren, cmd->color.r, cmd->color.g, cmd->color.b, 255);
                float uy = dst.y + dst.h - 2.0f;
                SDL_RenderDrawLineF(ren, dst.x, uy, dst.x+dst.w, uy);
            }
        }
    }
    SDL_RenderSetClipRect(ren,NULL);

    if(b->total_h > b->lctx.content_h){
        int bar_h = content_bg.h;
        float ratio = (float)b->lctx.content_h / (float)b->total_h;
        int thumb_h = (int)(bar_h * ratio);
        if(thumb_h<32) thumb_h=32;
        if(thumb_h>bar_h) thumb_h=bar_h;
        int max_scroll = b->total_h - b->lctx.content_h;
        float thumb_y = content_bg.y + (max_scroll? (float)b->scroll_y / max_scroll * (bar_h - thumb_h) : 0);
        fill_rrect(ren,(float)b->win_w-9,thumb_y+2,6,(float)thumb_h-4,3,(SDL_Color){154,160,166,255});
    }

    SDL_Rect status={0,b->win_h-GUI_STATUS_H,b->win_w,GUI_STATUS_H};
    SDL_SetRenderDrawColor(ren,C_BAR.r,C_BAR.g,C_BAR.b,255); SDL_RenderFillRect(ren,&status);
    SDL_SetRenderDrawColor(ren,C_LINE.r,C_LINE.g,C_LINE.b,255); SDL_RenderDrawLine(ren,0,b->win_h-GUI_STATUS_H,b->win_w,b->win_h-GUI_STATUS_H);
    {
        const char *msg = b->hover_href[0]? b->hover_href : b->status_msg;
        SDL_Rect sclip={0,status.y+1,b->win_w-8,status.h-1};
        SDL_RenderSetClipRect(ren,&sclip);
        draw_text(ren,msg?msg:"",12,false,(SDL_Color){95,99,104,255},8,(float)status.y,0,(float)status.h);
        SDL_RenderSetClipRect(ren,NULL);
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

int run_gui(const char *initial_url){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ fprintf(stderr,"SDL_Init: %s\n", SDL_GetError()); return 1; }
    if(TTF_Init()!=0){ fprintf(stderr,"TTF_Init: %s\n", TTF_GetError()); SDL_Quit(); return 1; }
    BrowserState b={0};
    b.win_w=1024; b.win_h=720;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    b.win = SDL_CreateWindow("C Web Browser", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, b.win_w,b.win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if(!b.win){ fprintf(stderr,"CreateWindow: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return 1; }
    b.ren = SDL_CreateRenderer(b.win,-1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!b.ren){ b.ren = SDL_CreateRenderer(b.win,-1, SDL_RENDERER_SOFTWARE); if(!b.ren){ fprintf(stderr,"Renderer: %s\n", SDL_GetError()); SDL_DestroyWindow(b.win); TTF_Quit(); SDL_Quit(); return 1; } }
    b.addr_cap=1024; b.addr_text=(char*)xmalloc(b.addr_cap); b.addr_text[0]=0; b.addr_len=0; b.addr_cursor=0;
    b.needs_relayout=false; b.running=true;
    b.current_url=xstrdup(initial_url?initial_url:"about:blank");
    addr_set_text(&b,b.current_url);
    browser_set_status(&b,"Ready");
    gui_find_font_paths();
    update_dpi(&b);

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

                        TTF_Font *af=gui_get_font(14,false,false,false);
                        if(af){
                            int rel = mx - (ar.x+16);

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
                                char *href=b.lctx.links[i].href;

                                char *resolved=NULL;
                                if(href && href[0] && strncmp(href,"http://",7)!=0 && strncmp(href,"https://",8)!=0 && strncmp(href,"data:",5)!=0 && strncmp(href,"about:",6)!=0){
                                    char *try_res=image_resolve_url(href, b.current_url);
                                    if(try_res && (strncmp(try_res,"http://",7)==0||strncmp(try_res,"https://",8)==0)){
                                        resolved=try_res;
                                    } else free(try_res);
                                }
                                if(resolved){
                                    browser_navigate(&b,resolved,true);
                                    free(resolved);
                                } else {
                                    browser_navigate(&b,href,true);
                                }
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

        if(image_cache_check_dirty()){
            image_cache_clear_dirty();
            browser_relayout(&b);
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
    image_cache_clear();
    gui_free_fonts();
    SDL_DestroyRenderer(b.ren); SDL_DestroyWindow(b.win);
    TTF_Quit(); SDL_Quit();
    return 0;
}

#else
int run_gui(const char *u){ (void)u; fprintf(stderr,"GUI not available: SDL2/SDL_ttf not found at compile time.\nInstall sdl2 + sdl2_ttf via 'brew install sdl2 sdl2_ttf' and rebuild with 'make gui'\n"); return 1; }
#endif
