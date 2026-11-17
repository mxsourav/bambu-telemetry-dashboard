/* =====================================================================
 *  gfx_text.h  -  runtime bitmap-font renderer for the generated
 *  Antapani ExtraBold glyph sets (font_big.h / font_small.h)
 *
 *  Draws 1-bit MSB-first packed glyphs onto any LGFX_Sprite/LGFX target
 *  with a foreground color + optional transparent background.
 * ===================================================================== */
#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "font_small.h"
#include "font_big.h"

// Generic glyph-table lookup + blit.
struct GlyphIndex {
  const char*        name;    // debug
  int                count;
  int                height;  // line height
  const void*        index;   // array of {char c; u8 w,h,dx,dy,adv; u16 off;}
  const unsigned char* bits;
};

template <typename T>
static inline void drawGlyph(T& tgt, const GlyphIndex& fi, char ch,
                             int x, int y, uint16_t fg) {
  // struct layout: char c; uint8 w,h,dx,dy,adv; uint16 off  (packed 1-aligned)
  // We access via memcpy to avoid packing issues.
  for (int i = 0; i < fi.count; i++) {
    // stride of index entries: 1+1+1+1+1+1+2 = 8 bytes, no padding (all u8 + u16 aligned)
    const uint8_t* e = (const uint8_t*)fi.index + i * 8;
    if ((char)e[0] != ch) continue;
    uint8_t gw = e[1], gh = e[2], dx = e[3] - 128, dy = e[4] - 128;
    uint16_t off = e[6] | (e[7] << 8);
    if (gw == 0) return;             // no bitmap (space)
    const uint8_t* gb = fi.bits + off;
    int bpr = (gw + 7) >> 3;
    for (int yy = 0; yy < gh; yy++) {
      const uint8_t* row = gb + yy * bpr;
      for (int xx = 0; xx < gw; xx++) {
        if (row[xx >> 3] & (0x80 >> (xx & 7)))
          tgt.writePixel(x + dx + xx, y + dy + yy, fg);
      }
    }
    return;
  }
}

template <typename T>
static inline int textWidth(const GlyphIndex& fi, const char* s) {
  int w = 0;
  for (const char* p = s; *p; p++) {
    for (int i = 0; i < fi.count; i++) {
      const uint8_t* e = (const uint8_t*)fi.index + i * 8;
      if ((char)e[0] == *p) { w += e[5]; break; }
    }
  }
  return w;
}

template <typename T>
static inline void drawText(T& tgt, const GlyphIndex& fi, const char* s,
                            int x, int y, uint16_t fg) {
  int cx = x;
  for (const char* p = s; *p; p++) {
    drawGlyph(tgt, fi, *p, cx, y, fg);
    for (int i = 0; i < fi.count; i++) {
      const uint8_t* e = (const uint8_t*)fi.index + i * 8;
      if ((char)e[0] == *p) { cx += e[5]; break; }
    }
  }
}

// centered helpers
template <typename T>
static inline void drawTextCentered(T& tgt, const GlyphIndex& fi, const char* s,
                                    int cx, int y, uint16_t fg) {
  drawText(tgt, fi, s, cx - textWidth<int>(fi, s) / 2, y, fg);
}

static const GlyphIndex FONT_SMALL = {
  "FNT_SM", FNT_SM_GLYPH_COUNT, FNT_SM_HEIGHT, FNT_SM_index, FNT_SM_bits };
static const GlyphIndex FONT_BIG = {
  "FNT_BIG", FNT_BIG_GLYPH_COUNT, FNT_BIG_HEIGHT, FNT_BIG_index, FNT_BIG_bits };
