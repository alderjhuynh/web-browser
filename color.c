#define _POSIX_C_SOURCE 200809L
#include "color.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>

typedef struct { const char *name; const char *hex; } NamedColor;
static const NamedColor NAMED_COLORS[] = {
    {"black","#000000"}, {"white","#ffffff"}, {"red","#ff0000"},
    {"green","#008000"}, {"blue","#0000ff"}, {"yellow","#ffff00"},
    {"orange","#ffa500"}, {"purple","#800080"}, {"pink","#ffc0cb"},
    {"gray","#808080"}, {"grey","#808080"}, {"silver","#c0c0c0"},
    {"navy","#000080"}, {"teal","#008080"}, {"aqua","#00ffff"},
    {"cyan","#00ffff"}, {"lime","#00ff00"}, {"maroon","#800000"},
    {"olive","#808000"}, {"fuchsia","#ff00ff"}, {"magenta","#ff00ff"},
    {"brown","#a52a2a"}, {"coral","#ff7f50"}, {"crimson","#dc143c"},
    {"darkblue","#00008b"}, {"darkcyan","#008b8b"}, {"darkgray","#a9a9a9"},
    {"darkgrey","#a9a9a9"}, {"darkgreen","#006400"}, {"darkred","#8b0000"},
    {"dimgray","#696969"}, {"dimgrey","#696969"},
    {"dodgerblue","#1e90ff"}, {"firebrick","#b22222"},
    {"forestgreen","#228b22"}, {"gainsboro","#dcdcdc"}, {"gold","#ffd700"},
    {"goldenrod","#daa520"}, {"hotpink","#ff69b4"}, {"indianred","#cd5c5c"},
    {"indigo","#4b0082"}, {"ivory","#fffff0"}, {"khaki","#f0e68c"},
    {"lavender","#e6e6fa"}, {"lawngreen","#7cfc00"}, {"lemonchiffon","#fffacd"},
    {"lightblue","#add8e6"}, {"lightcoral","#f08080"}, {"lightcyan","#e0ffff"},
    {"lightgray","#d3d3d3"}, {"lightgrey","#d3d3d3"},
    {"lightgreen","#90ee90"}, {"lightpink","#ffb6c1"}, {"lightyellow","#ffffe0"},
    {"limegreen","#32cd32"}, {"linen","#faf0e6"},
    {"mediumblue","#0000cd"}, {"mediumpurple","#9370db"},
    {"mediumseagreen","#3cb371"}, {"midnightblue","#191970"},
    {"mintcream","#f5fffa"}, {"mistyrose","#ffe4e1"}, {"moccasin","#ffe4b5"},
    {"oldlace","#fdf5e6"}, {"orchid","#da70d6"},
    {"palegoldenrod","#eee8aa"}, {"palegreen","#98fb98"},
    {"paleturquoise","#afeeee"}, {"papayawhip","#ffefd5"},
    {"peachpuff","#ffdab9"}, {"peru","#cd853f"}, {"plum","#dda0dd"},
    {"powderblue","#b0e0e6"}, {"rosybrown","#bc8f8f"}, {"royalblue","#4169e1"},
    {"saddlebrown","#8b4513"}, {"salmon","#fa8072"}, {"sandybrown","#f4a460"},
    {"seagreen","#2e8b57"}, {"seashell","#fff5ee"}, {"sienna","#a0522d"},
    {"skyblue","#87ceeb"}, {"slateblue","#6a5acd"}, {"slategray","#708090"},
    {"slategrey","#708090"}, {"snow","#fffafa"}, {"springgreen","#00ff7f"},
    {"steelblue","#4682b4"}, {"tan","#d2b48c"}, {"thistle","#d8bfd8"},
    {"tomato","#ff6347"}, {"turquoise","#40e0d0"}, {"violet","#ee82ee"},
    {"wheat","#f5deb3"}, {"whitesmoke","#f5f5f5"}, {"yellowgreen","#9acd32"},

    {"rebeccapurple","#663399"}, {"aliceblue","#f0f8ff"}, {"antiquewhite","#faebd7"},
    {"aquamarine","#7fffd4"}, {"azure","#f0ffff"}, {"beige","#f5f5dc"},
    {"bisque","#ffe4c4"}, {"blanchedalmond","#ffebcd"}, {"blueviolet","#8a2be2"},
    {"burlywood","#deb887"}, {"cadetblue","#5f9ea0"}, {"chartreuse","#7fff00"},
    {"chocolate","#d2691e"}, {"cornflowerblue","#6495ed"}, {"cornsilk","#fff8dc"},
    {"darkgoldenrod","#b8860b"}, {"darkkhaki","#bdb76b"}, {"darkmagenta","#8b008b"},
    {"darkolivegreen","#556b2f"}, {"darkorange","#ff8c00"}, {"darkorchid","#9932cc"},
    {"darksalmon","#e9967a"}, {"darkseagreen","#8fbc8f"}, {"darkslateblue","#483d8b"},
    {"darkslategray","#2f4f4f"}, {"darkslategrey","#2f4f4f"}, {"darkturquoise","#00ced1"},
    {"darkviolet","#9400d3"}, {"deeppink","#ff1493"}, {"deepskyblue","#00bfff"},
    {"floralwhite","#fffaf0"}, {"ghostwhite","#f8f8ff"}, {"honeydew","#f0fff0"},
    {"lightsalmon","#ffa07a"}, {"lightseagreen","#20b2aa"}, {"lightskyblue","#87cefa"},
    {"lightslategray","#778899"}, {"lightslategrey","#778899"}, {"lightsteelblue","#b0c4de"},
    {"mediumaquamarine","#66cdaa"}, {"mediumorchid","#ba55d3"}, {"mediumslateblue","#7b68ee"},
    {"mediumspringgreen","#00fa9a"}, {"mediumturquoise","#48d1cc"}, {"mediumvioletred","#c71585"},
    {"navajowhite","#ffdead"}, {"olivedrab","#6b8e23"}, {"orangered","#ff4500"},
    {"palevioletred","#db7093"}, {"saddlebrown","#8b4513"}, {"seagreen","#2e8b57"},
    {"slateblue","#6a5acd"}, {"slategray","#708090"}, {"snow","#fffafa"},
    {NULL,NULL}
};

static bool hex_digit(int c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static int hex_val(int c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}

static bool parse_hex(const char *s, Color *out){
    if(!s || s[0]!='#') return false;
    size_t len=strlen(s);

    if(len==4){
        for(int i=1;i<4;i++) if(!hex_digit(s[i])) return false;
        out->r = (uint8_t)(hex_val(s[1])*17);
        out->g = (uint8_t)(hex_val(s[2])*17);
        out->b = (uint8_t)(hex_val(s[3])*17);
        out->a=255; out->ok=true; return true;
    } else if(len==5){
        for(int i=1;i<5;i++) if(!hex_digit(s[i])) return false;
        out->r = (uint8_t)(hex_val(s[1])*17);
        out->g = (uint8_t)(hex_val(s[2])*17);
        out->b = (uint8_t)(hex_val(s[3])*17);
        out->a = (uint8_t)(hex_val(s[4])*17);
        out->ok=true; return true;
    } else if(len==7){
        for(int i=1;i<7;i++) if(!hex_digit(s[i])) return false;
        out->r = (uint8_t)(hex_val(s[1])*16+hex_val(s[2]));
        out->g = (uint8_t)(hex_val(s[3])*16+hex_val(s[4]));
        out->b = (uint8_t)(hex_val(s[5])*16+hex_val(s[6]));
        out->a=255; out->ok=true; return true;
    } else if(len==9){
        for(int i=1;i<9;i++) if(!hex_digit(s[i])) return false;
        out->r = (uint8_t)(hex_val(s[1])*16+hex_val(s[2]));
        out->g = (uint8_t)(hex_val(s[3])*16+hex_val(s[4]));
        out->b = (uint8_t)(hex_val(s[5])*16+hex_val(s[6]));
        out->a = (uint8_t)(hex_val(s[7])*16+hex_val(s[8]));
        out->ok=true; return true;
    }
    return false;
}

static double clamp_d(double v,double lo,double hi){ return v<lo?lo:(v>hi?hi:v); }

typedef struct { double val; bool is_percent; bool is_angle; double angle_deg; } Num;
static bool parse_number_token(const char *s, size_t *pos, Num *out){
    size_t i=*pos;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if(!s[i]) return false;

    if(s[i]=='/'||s[i]==',' ) { i++; while(s[i] && isspace((unsigned char)s[i])) i++; }
    if(!s[i]) return false;
    char *end=NULL;
    double v=strtod(s+i,&end);
    if(end==s+i) return false;
    out->val=v;
    out->is_percent=false;
    out->is_angle=false;
    out->angle_deg=v;
    if(end && *end=='%'){ out->is_percent=true; end++; }
    else if(end && *end){

        if(strncasecmp(end,"deg",3)==0){ out->is_angle=true; out->angle_deg=v; end+=3; }
        else if(strncasecmp(end,"rad",3)==0){ out->is_angle=true; out->angle_deg=v*180.0/M_PI; end+=3; }
        else if(strncasecmp(end,"grad",4)==0){ out->is_angle=true; out->angle_deg=v*0.9; end+=4; }
        else if(strncasecmp(end,"turn",4)==0){ out->is_angle=true; out->angle_deg=v*360.0; end+=4; }
    }
    *pos = (size_t)(end - s);
    return true;
}

static uint8_t to_u8(double v){ if(v<0) v=0; if(v>255) v=255; return (uint8_t)(v+0.5); }

static bool parse_rgb(const char *s, Color *out){

    const char *p=strchr(s,'(');
    const char *q=strrchr(s,')');
    if(!p||!q||q<p) return false;
    char *inner=xstrndup(p+1, (size_t)(q - p -1));

    size_t pos=0;
    Num nums[4]={0};
    int n=0;
    size_t len=strlen(inner);

    pos=0;
    while(n<4){

        while(pos<len && (isspace((unsigned char)inner[pos])||inner[pos]==','||inner[pos]=='/')) pos++;
        if(pos>=len) break;
        if(inner[pos]==')' || inner[pos]=='('){ pos++; continue; }
        char *end=NULL;
        double v=strtod(inner+pos,&end);
        if(end==inner+pos) { pos++; continue; }
        bool pct=false;
        if(end && *end=='%'){ pct=true; end++; }
        nums[n].val=v;
        nums[n].is_percent=pct;
        n++;
        pos = (size_t)(end - inner);

    }
    free(inner);
    if(n<3) return false;
    double r=nums[0].val;
    double g=nums[1].val;
    double b=nums[2].val;
    if(nums[0].is_percent) r=r*255/100.0;
    if(nums[1].is_percent) g=g*255/100.0;
    if(nums[2].is_percent) b=b*255/100.0;
    double a=255;
    if(n>=4){
        double av=nums[3].val;
        if(nums[3].is_percent) a=av*255/100.0;
        else {
            if(av<=1.0 && av>=0) a=av*255;
            else a=av;
        }
    }
    out->r=to_u8(r); out->g=to_u8(g); out->b=to_u8(b); out->a=to_u8(a);
    out->ok=true;
    return true;
}

static double hue2rgb(double p,double q,double t){
    if(t<0) t+=1;
    if(t>1) t-=1;
    if(t<1.0/6) return p + (q-p)*6*t;
    if(t<1.0/2) return q;
    if(t<2.0/3) return p + (q-p)*(2.0/3 - t)*6;
    return p;
}
static bool parse_hsl(const char *s, Color *out){
    const char *p=strchr(s,'(');
    const char *q=strrchr(s,')');
    if(!p||!q||q<p) return false;
    char *inner=xstrndup(p+1, (size_t)(q - p -1));
    size_t pos=0;
    size_t len=strlen(inner);

    double h=0,sat=0,light=0,alpha=1;
    bool has_h=false,has_s=false,has_l=false,has_a=false;

    pos=0;
    int idx=0;
    Num tmp;
    while(idx<4){

        while(pos<len && (isspace((unsigned char)inner[pos])||inner[pos]==',')) pos++;

        if(pos<len && inner[pos]=='/'){ pos++; while(pos<len && isspace((unsigned char)inner[pos])) pos++; }
        if(pos>=len) break;
        size_t save=pos;
        if(!parse_number_token(inner,&pos,&tmp)){
            pos=save+1; continue;
        }
        if(idx==0){ h=tmp.is_angle? tmp.angle_deg : tmp.val; has_h=true; }
        else if(idx==1){ sat= tmp.is_percent? tmp.val/100.0 : tmp.val; if(!tmp.is_percent) sat/=100.0; has_s=true; }
        else if(idx==2){ light= tmp.is_percent? tmp.val/100.0 : tmp.val; if(!tmp.is_percent) light/=100.0; has_l=true; }
        else if(idx==3){
            double av=tmp.val;
            if(tmp.is_percent) av=av/100.0;
            alpha=clamp_d(av,0,1);
            has_a=true;
        }
        idx++;
    }
    free(inner);
    if(!has_h||!has_s||!has_l) return false;
    h = fmod(h,360.0); if(h<0) h+=360.0;
    sat=clamp_d(sat,0,1);
    light=clamp_d(light,0,1);
    if(!has_a) alpha=1;
    double r,g,b;
    if(sat==0){
        r=g=b=light;
    } else {
        double q2 = light < 0.5 ? light*(1+sat) : light+sat - light*sat;
        double p2 = 2*light - q2;
        double hk = h/360.0;
        r = hue2rgb(p2,q2,hk+1.0/3);
        g = hue2rgb(p2,q2,hk);
        b = hue2rgb(p2,q2,hk-1.0/3);
    }
    out->r = to_u8(r*255);
    out->g = to_u8(g*255);
    out->b = to_u8(b*255);
    out->a = to_u8(alpha*255);
    out->ok=true;
    return true;
}

bool color_parse(const char *s, Color *out){
    if(!s||!out) return false;
    memset(out,0,sizeof(*out));
    out->a=255; out->ok=false;
    char *tmp=xstrdup(s);
    str_trim_inplace(tmp);
    char *lower=str_lower_dup(tmp);
    free(tmp);
    tmp=lower;
    if(tmp[0]=='\0'){ free(tmp); return false; }

    if(strcmp(tmp,"transparent")==0){
        out->r=0; out->g=0; out->b=0; out->a=0; out->ok=true; free(tmp); return true;
    }
    if(strcmp(tmp,"currentcolor")==0 || strcmp(tmp,"inherit")==0 || strcmp(tmp,"initial")==0 || strcmp(tmp,"unset")==0){
        free(tmp); return false;
    }

    for(int i=0; NAMED_COLORS[i].name; i++){
        if(strcmp(tmp, NAMED_COLORS[i].name)==0){
            Color c={0};
            if(parse_hex(NAMED_COLORS[i].hex,&c)){ *out=c; free(tmp); return true; }
        }
    }
    if(tmp[0]=='#'){
        Color c={0};
        if(parse_hex(tmp,&c)){ *out=c; free(tmp); return true; }
        free(tmp); return false;
    }
    if(strncmp(tmp,"rgb",3)==0){
        Color c={0};
        if(parse_rgb(tmp,&c)){ *out=c; free(tmp); return true; }
        free(tmp); return false;
    }
    if(strncmp(tmp,"hsl",3)==0){
        Color c={0};
        if(parse_hsl(tmp,&c)){ *out=c; free(tmp); return true; }
        free(tmp); return false;
    }

    free(tmp);
    return false;
}

char *color_to_hex(const Color *c){
    if(!c||!c->ok) return xstrdup("");
    char *res=(char*)xmalloc(8);
    snprintf(res,8,"#%02x%02x%02x", c->r,c->g,c->b);
    return res;
}

char *normalize_color(const char *color){
    if(!color) return xstrdup("");
    Color c;
    if(!color_parse(color,&c) || !c.ok) return xstrdup("");

    if(c.a==0){

        char *tmp=xstrdup(color);
        str_trim_inplace(tmp);
        char *low=str_lower_dup(tmp);
        free(tmp);
        tmp=low;
        bool is_transparent = (strcmp(tmp,"transparent")==0);
        free(tmp);
        if(is_transparent) return xstrdup("");
    }
    if(c.a!=255){

    }
    return color_to_hex(&c);
}

Rgba color_to_rgba(const Color *c){
    Rgba r={0,0,0,255};
    if(c&&c->ok){ r.r=c->r; r.g=c->g; r.b=c->b; r.a=c->a; }
    return r;
}
Rgba color_from_string(const char *s){
    Color c;
    if(color_parse(s,&c) && c.ok) return color_to_rgba(&c);
    return (Rgba){0,0,0,255};
}
