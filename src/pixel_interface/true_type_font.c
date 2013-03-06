
/* true_type_font.c
 *
 * This file is part of fizmo.
 *
 * Copyright (c) 2011-2012 Christoph Ender.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <ft2build.h>
#include FT_FREETYPE_H

#include "true_type_font.h"
#include "tools/unused.h"


/*
int tt_get_glyph_get_distance_to_rightmost_pixel(true_type_font *font,
    z_ucs charcode) {
  FT_UInt glyph_index = FT_Get_Char_Index(font->face, charcode);
  FT_Load_Glyph(
      font->face,
      glyph_index,
      FT_LOAD_DEFAULT);
  return
    font->face->glyph->metrics.horiBearingX / 64 +
    font->face->glyph->metrics.width / 64;
}
*/


int tt_get_glyph_advance(true_type_font *font, z_ucs current_char,
    z_ucs UNUSED(last_char)) {
  FT_UInt glyph_index;
  FT_GlyphSlot slot;
  int ft_error;

  glyph_index = FT_Get_Char_Index(font->face, current_char);

  ft_error = FT_Load_Glyph(
      font->face,
      glyph_index,
      FT_LOAD_DEFAULT);

  slot = font->face->glyph;

  return slot->advance.x / 64;
}


// NOTE: Glyph pixels are only drawn in case they are not completely
// equal to background color. This is required, since especially in case
// of italic faces hori_advance may be smaller(!) then the width of a
// glyph, causing characters to overlay on screen. This may be reproduced
// by using "SourceSansPro-It.ttf" in etude.z5 section 4 and looking at
// "Test of italic (or underlined) text." where the closing bracket
// overwrites the right d's vertical stroke.
int tt_draw_glyph(true_type_font *font, int x, int y,
    struct z_screen_pixel_interface *screen_pixel_interface,
    z_ucs charcode) {
  FT_GlyphSlot slot;
  FT_Bitmap bitmap;
  //FT_Vector kerning;
  int ft_error;
  int screen_x, screen_y, advance, start_x, bitmap_x, bitmap_y;
  uint32_t pixel;

  FT_UInt glyph_index = FT_Get_Char_Index(font->face, charcode);

  ft_error = FT_Load_Glyph(
      font->face,
      glyph_index,
      FT_LOAD_DEFAULT);

  ft_error = FT_Render_Glyph(
      font->face->glyph,
      FT_RENDER_MODE_NORMAL);

  slot = font->face->glyph;
  x += slot->bitmap_left;
  y += font->face->size->metrics.ascender/64 - slot->bitmap_top;

  // FIXME: Free glyph's memory.
  // FT_Done_FreeType
  
  //use_kerning = FT_HAS_KERNING(font->face);
  advance = slot->advance.x / 64;

  /*
  if ( (following_charcode != 0) && (use_kerning) ) {
    error = FT_Get_Kerning(current_face, charcode, following_charcode,
        FT_KERNING_DEFAULT, &kerning);
    if (error) {
      printf("error: %d\n", error);
      exit(-1);
    }
    advance += kerning.x / 64;
  }
  */

  bitmap = slot->bitmap;

  start_x = x;
  screen_y = y;
  for (bitmap_y=0; bitmap_y<bitmap.rows; bitmap_y++, screen_y++) {
    screen_x = start_x;
    for (bitmap_x=0; bitmap_x<bitmap.width; bitmap_x++, screen_x++) {
      pixel = bitmap.buffer[bitmap_y*bitmap.width + bitmap_x];
      if (pixel) {
        screen_pixel_interface->draw_grayscale_pixel(
            screen_y, screen_x, 255-pixel);
      }
    }
  }

  return advance;
}


void tt_destroy_font(true_type_font *UNUSED(font)) {
}


