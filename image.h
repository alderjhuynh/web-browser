#pragma once
#include <stddef.h>
#include <stdbool.h>

#if defined(__has_include)
#  if __has_include(<SDL.h>)
#    include <SDL.h>
#  elif __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#  else
     typedef struct SDL_Renderer SDL_Renderer;
     typedef struct SDL_Texture SDL_Texture;
     typedef struct SDL_Surface SDL_Surface;
     typedef struct SDL_RWops SDL_RWops;
#  endif
#else
  typedef struct SDL_Renderer SDL_Renderer;
  typedef struct SDL_Texture SDL_Texture;
  typedef struct SDL_Surface SDL_Surface;
  typedef struct SDL_RWops SDL_RWops;
#endif

typedef struct CachedImage {
    char *url;
    SDL_Surface *surf;
    SDL_Texture *tex;
    int w, h;
    bool failed;
} CachedImage;

char *image_resolve_url(const char *src, const char *base);

CachedImage *image_cache_get(const char *src, const char *base_url, SDL_Renderer *renderer);

CachedImage *image_cache_lookup(const char *src, const char *base_url);

void image_cache_fetch_async(const char *src, const char *base_url);

bool image_cache_check_dirty(void);
void image_cache_clear_dirty(void);

void image_cache_init(void);
void image_cache_clear(void);
size_t image_cache_count(void);
