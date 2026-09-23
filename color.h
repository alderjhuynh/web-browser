#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t r, g, b, a;
    bool ok;
} Color;

bool color_parse(const char *s, Color *out);

char *normalize_color(const char *color);

char *color_to_hex(const Color *c);

typedef struct { uint8_t r,g,b,a; } Rgba;
Rgba color_to_rgba(const Color *c);

Rgba color_from_string(const char *s);
