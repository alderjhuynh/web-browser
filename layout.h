#pragma once
#include "dom.h"
#include <stdbool.h>

typedef struct {
    int width; bool width_auto;
    int ml,mr,mt,mb; bool ml_auto,mr_auto;
    int pl,pr,pt,pb;
    int bl,br,bt,bb;
    int content_w;
    int outer_w;
    bool border_box;
} BoxModel;

void compute_box_model(Node *n, int containing_w, BoxModel *out);
int parse_px(const char *s,int parent_fs);
