#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include "dom.h"
#include "parser.h"
#include "css.h"
#include "style.h"
#include "color.h"
#include "fetch.h"
#include "layout.h"
#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

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
    fprintf(stderr,"  --mojeek, --images  Route bare search queries via Mojeek image search (https://www.mojeek.com/search?fmt=images)\n");
    fprintf(stderr,"                      Also supports per-query prefix in GUI: !mojeek, !m, mojeek:, images:, img:\n");
    fprintf(stderr,"Window controls (GUI):\n");
    fprintf(stderr,"  Type URL/search in top bar, Enter to go; Back/Forward/Reload buttons; wheel/page keys to scroll; click links; hover shows URL; Ctrl+L focuses bar; F5 reloads\n");
    fprintf(stderr,"  Image search: type '!mojeek cats' or '!m cats' or run with --mojeek flag\n");
}

static char *cli_url_encode(const char *s){
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
static char *cli_make_mojeek_url(const char *query){
    char *enc=cli_url_encode(query);
    char *u=(char*)xmalloc(strlen(enc)+128);
    sprintf(u,"https://www.mojeek.com/search?fmt=images&q=%s",enc);
    free(enc); return u;
}
static bool cli_is_mojeek_query(const char *q, const char **out_rest);
static char *cli_make_search_url(const char *query){

    const char *rest=NULL;
    if(cli_is_mojeek_query(query, &rest)) return cli_make_mojeek_url(rest);
    char *enc=cli_url_encode(query);
    char *u=(char*)xmalloc(strlen(enc)+64);
    sprintf(u,"https://html.duckduckgo.com/html/?q=%s",enc);
    free(enc); return u;
}
static bool cli_is_mojeek_query(const char *q, const char **out_rest){
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
    return false;
}

int main(int argc, char **argv) {
    const char *url_or_file=NULL;
    const char *query=NULL;
    bool dump=true;
    bool force_gui=false;
    bool force_cli=false;
    bool mojeek_flag=false;
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) { print_usage(argv[0]); return 0; }
        else if (strcmp(argv[i],"--gui")==0) { force_gui=true; if(i+1<argc && argv[i+1][0]!='-') url_or_file=argv[++i]; }
        else if (strcmp(argv[i],"--cli")==0) force_cli=true;
        else if (strcmp(argv[i],"--mojeek")==0 || strcmp(argv[i],"--mojeek-images")==0 || strcmp(argv[i],"--images")==0 || strcmp(argv[i],"--image")==0) mojeek_flag=true;
        else if (strcmp(argv[i],"--query")==0 && i+1<argc) query=argv[++i];
        else if (strcmp(argv[i],"--no-dump")==0) dump=false;
        else if (!url_or_file) url_or_file=argv[i];
        else { fprintf(stderr,"unknown arg %s\n", argv[i]); print_usage(argv[0]); return 1; }
    }

    if(mojeek_flag) set_mojeek_image_mode(true);

    char *cli_mojeek_url = NULL;
    if(url_or_file){
        const char *rest=NULL;
        bool is_mojeek_pref = cli_is_mojeek_query(url_or_file, &rest);
        if(is_mojeek_pref){
            cli_mojeek_url = cli_make_mojeek_url(rest);
            url_or_file = cli_mojeek_url;
        } else if(mojeek_flag){

            bool is_url = (strncmp(url_or_file,"http://",7)==0 || strncmp(url_or_file,"https://",8)==0 || strncmp(url_or_file,"about:",6)==0);
            bool is_file = false;
            if(!is_url){
                FILE *f=fopen(url_or_file,"rb");
                if(f){ fclose(f); is_file=true; }
            }
            if(!is_url && !is_file){
                cli_mojeek_url = cli_make_mojeek_url(url_or_file);
                url_or_file = cli_mojeek_url;
            }
        }
    }
    if(force_gui){
        const char *start = url_or_file ? url_or_file : "about:blank";
        return run_gui(start);
    }
    if(!force_cli){

    }

    char *effective_url = NULL;
    if(url_or_file && strstr(url_or_file,"uddg=")){
        const char *p=strstr(url_or_file,"uddg=");
        if(p){
            p+=5;
            const char *end=strchr(p,'&');
            const char *hash=strchr(p,'#');
            size_t vlen;
            if(end && hash) vlen=(size_t)((end<hash?end:hash)-p);
            else if(end) vlen=(size_t)(end-p);
            else if(hash) vlen=(size_t)(hash-p);
            else vlen=strlen(p);
            if(vlen){
                char *enc=xstrndup(p,vlen);

                char *dec=(char*)xmalloc(strlen(enc)+1);
                size_t oi=0;
                for(size_t i=0;i<strlen(enc);){
                    if(enc[i]=='%' && i+2<strlen(enc) && isxdigit((unsigned char)enc[i+1]) && isxdigit((unsigned char)enc[i+2])){
                        char hex[3]={enc[i+1],enc[i+2],0};
                        dec[oi++]=(char)strtol(hex,NULL,16);
                        i+=3;
                    } else dec[oi++]=enc[i++];
                }
                dec[oi]=0;
                free(enc);
                if(strncmp(dec,"http://",7)==0 || strncmp(dec,"https://",8)==0){
                    effective_url=dec;
                    printf("Bypassing DuckDuckGo redirect to %s\n", effective_url);
                    url_or_file=effective_url;
                } else free(dec);
            }
        }
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

        FILE *fcheck = fopen(url_or_file, "rb");
        if(fcheck){
            fclose(fcheck);
            html=read_file(url_or_file, &hlen);
            if (!html) { fprintf(stderr,"read failed, using TEST_HTML\n"); html=(char*)TEST_HTML; hlen=strlen(html); }
            else to_free=html;
        } else {

            char *search_url = NULL;
            const char *rest2=NULL;
            if(cli_is_mojeek_query(url_or_file, &rest2)){
                search_url = cli_make_mojeek_url(rest2);
            } else if(mojeek_flag){
                search_url = cli_make_mojeek_url(url_or_file);
            } else {
                search_url = cli_make_search_url(url_or_file);
            }
            printf("Fetching %s ...\n", search_url);
            if(effective_url) free(effective_url);
            effective_url = search_url;
            url_or_file = effective_url;
            html=fetch_url(url_or_file, &hlen);
            if (!html) { fprintf(stderr,"fetch failed, falling back to TEST_HTML\n"); html=(char*)TEST_HTML; hlen=strlen(html); }
            else to_free=html;
        }
    }

    char *style_text=NULL;
    size_t style_len=0;
    Node *root = parse_html(html, hlen, &style_text, &style_len);
    if (!root) { fprintf(stderr,"parse failed\n"); return 1; }

    char *linked_css=NULL; size_t linked_len=0;
    if(url_or_file) linked_css=load_linked_stylesheets(root, url_or_file, &linked_len);
    else linked_css=xstrdup("");

    char *combined_css=NULL;
    size_t combined_len = style_len + linked_len + 2;
    combined_css=(char*)xmalloc(combined_len+1);
    combined_css[0]='\0';
    if(style_text) strcat(combined_css, style_text);
    if(linked_len>0){
        if(style_len>0) strcat(combined_css, "\n");
        strcat(combined_css, linked_css);
    }
    size_t rule_count=0;
    CSSRule *rules = parse_css(combined_css, &rule_count);
    printf("Collected %zu bytes of <style> CSS + %zu linked, parsed %zu rules\n", style_len, linked_len, rule_count);
    apply_styles(root, rules, rule_count);
    free(combined_css);
    free(linked_css);

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
            const char *bg = node_style_get(body, "background");
            const char *bgc = node_style_get(body, "background-color");
            const char *use = bg;
            char *norm_try = bg? normalize_color(bg): NULL;
            bool ok = norm_try && *norm_try;
            free(norm_try);
            if(!ok) use = bgc;
            char *norm = normalize_color(use ? use : "");
            printf("\nBody background: %s -> %s\n", use?use:"(none)", norm);
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
    }

    for (size_t i=0;i<rule_count;i++) css_rule_free(&rules[i]);
    free(rules);
    free(style_text);
    if (to_free) free(to_free);
    if (effective_url) free(effective_url);
    if (cli_mojeek_url) free(cli_mojeek_url);

    node_free(root);
    return 0;
}
