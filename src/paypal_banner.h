#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

HBITMAP load_paypal_banner(int max_width, int max_height, int *out_width, int *out_height);
void draw_paypal_banner(const DRAWITEMSTRUCT &item, HBITMAP bitmap, int width, int height);
void open_paypal_page();
