
/* true_type_font.c
 *
 * This file is part of fizmo.
 *
 * Copyright (c) 2011-2017 Christoph Ender.
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
#include "tools/tracelog.h"
#include "interpreter/fizmo.h"


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


static int get_glyph_size(true_type_font *font, z_ucs char_code,
    int *advance, int *bitmap_width) {

  FT_UInt glyph_index;
  FT_GlyphSlot slot;
  int ft_error;

  glyph_index = FT_Get_Char_Index(font->face, char_code);

  ft_error = FT_Load_Glyph(
      font->face,
      glyph_index,
      FT_LOAD_DEFAULT);

  if (ft_error != 0) {
    return -1;
  }
  else {
    slot = font->face->glyph;

    *advance = slot->advance.x / 64;
    *bitmap_width = slot->metrics.width / 64 + slot->metrics.horiBearingX / 64;

    return 0;
  }
}


//int tt_get_glyph_advance(true_type_font *font, z_ucs current_char,
//    z_ucs UNUSED(last_char)) {
int tt_get_glyph_size(true_type_font *font, z_ucs char_code,
    int *advance, int *bitmap_width) {
  int result;
  long new_glyph_cache_size;

  TRACE_LOG("tt_get_glyph_size invoked.\n");

  TRACE_LOG("Looking at %p\n", font->glyph_size_cache);
  if ((font->glyph_size_cache == NULL)
      || (font->glyph_size_cache_size < char_code)) {

    new_glyph_cache_size = char_code + 1024;

    TRACE_LOG("(Re-)allocating %d bytes for glyph cache.\n",
        sizeof(glyph_size) * new_glyph_cache_size);

    font->glyph_size_cache = fizmo_realloc(
        font->glyph_size_cache,
        sizeof(glyph_size) * new_glyph_cache_size);

    TRACE_LOG("Cache at %p.\n", font->glyph_size_cache);

    // fill uninitialzed memory.
    TRACE_LOG("clearning mem from %p, %d bytes.\n",
        font->glyph_size_cache + font->glyph_size_cache_size,
        (new_glyph_cache_size - font->glyph_size_cache_size)
        * sizeof(glyph_size));

    TRACE_LOG("memset: %p, %d.\n",
        font->glyph_size_cache + font->glyph_size_cache_size,
        (new_glyph_cache_size - font->glyph_size_cache_size)
        * sizeof(glyph_size));

    memset(
        font->glyph_size_cache + font->glyph_size_cache_size,
        0,
        (new_glyph_cache_size - font->glyph_size_cache_size)
        * sizeof(glyph_size));

    font->glyph_size_cache_size = new_glyph_cache_size;
  }

  TRACE_LOG("Access glyph cache at %p.\n",
      &font->glyph_size_cache[char_code].is_valid);

  if (font->glyph_size_cache[char_code].is_valid == 1) {
    TRACE_LOG("found glyph size cache hit for font %p, %c/%d.\n",
        font, char_code, char_code);

    TRACE_LOG("Reading from glyph cache at %p.\n",
        &font->glyph_size_cache[char_code].advance);
    *advance = font->glyph_size_cache[char_code].advance;
    *bitmap_width = font->glyph_size_cache[char_code].bitmap_width;
    /*
    printf("Fontcache for font %p, '%c' at %p: adv %d, bmitmapw: %d.\n",
        font, char_code, advance, *advance, *bitmap_width);
    */

    return 0;
  }
  else {
    /*
    printf("no glyph size cache hit for font %p, %c/%d.\n",
        font, char_code, char_code);
    */
    TRACE_LOG("no glyph size cache hit for %c/%d.\n", char_code, char_code);
    TRACE_LOG("font: %p.\n", font);
    result = get_glyph_size(font, char_code, advance, bitmap_width);

    if (result == 0) {
      TRACE_LOG("Writin to glyph cache at %p.\n",
          &font->glyph_size_cache[char_code].advance);
      font->glyph_size_cache[char_code].is_valid = 1;
      font->glyph_size_cache[char_code].advance = *advance;
      font->glyph_size_cache[char_code].bitmap_width = *bitmap_width;
    }

    return result;
  }
}



// note: glyph pixels are only drawn in case they are not completely
// equal to background color. this is required, since especially in case
// of italic faces hori_advance may be smaller(!) then the width of a
// glyph, causing characters to overlay on screen. this may be reproduced
// by using "sourcesanspro-it.ttf" in etude.z5 section 4 and looking at
// "test of italic (or underlined) text." where the closing bracket
// overwrites the right d's vertical stroke.
// If clip_top or clip_bottom are > 0, this amount of pixels is skipped
// when drawing the glyph. For example, when y=10, clip_top=10 and
// clip_bottom=5 and the vertical size of the glyph is 20, only the
// pixels 10, 11, 12, 13 and 14 are addressed on screen.
int tt_draw_glyph(true_type_font *font, int x, int y, int x_max,
    int clip_top, int clip_bottom,
    z_rgb_colour foreground_colour,
    z_rgb_colour background_colour,
    struct z_screen_pixel_interface *screen_pixel_interface,
    z_ucs charcode, int *last_gylphs_xcursorpos) {
  FT_GlyphSlot slot;
  FT_Bitmap bitmap;
  //FT_Vector kerning;
  //int ft_error,
  int pixel_bitmap_width, left_reverse_x, reverse_width;
  int screen_x, screen_y, advance, start_x, bitmap_x, bitmap_y;
  uint8_t pixel;
  uint8_t pixel2, pixel3;
  float pixel_value;
  float pixel_value2, pixel_value3;
  int dr, dg, db; // delta from foreground to background
  uint8_t br, bg, bb; // pre-evaluated background colors
  int draw_width, bitmap_start_y, top_space, max_y;
  int number_of_rows_available;


  FT_UInt glyph_index = FT_Get_Char_Index(font->face, charcode);

  // TODO: Evaluate ft_error
  // ft_error = FT_Load_Glyph(
  FT_Load_Glyph(
      font->face,
      glyph_index,
      FT_LOAD_DEFAULT);

  // TODO: Evaluate ft_error
  // ft_error = FT_Render_Glyph(
  FT_Render_Glyph(
      font->face->glyph,
      font->render_mode);

  slot = font->face->glyph;
  bitmap = slot->bitmap;
  advance = slot->advance.x / 64;

  pixel_bitmap_width
    = bitmap.pixel_mode == FT_PIXEL_MODE_LCD
    ? bitmap.width / 3 : bitmap.width;

  draw_width = advance > pixel_bitmap_width ? advance : pixel_bitmap_width;

  if (clip_top < 0) {
    clip_top = 0;
  }

  if (clip_bottom < 0) {
    clip_bottom = 0;
  }

  if ((last_gylphs_xcursorpos) && (*last_gylphs_xcursorpos >= 0)) {
    left_reverse_x
      = *last_gylphs_xcursorpos + 1;
    reverse_width
      = x + slot->bitmap_left + draw_width - *last_gylphs_xcursorpos + 1;
  }
  else {
    left_reverse_x = x;
    reverse_width = slot->bitmap_left + draw_width + 1;
  }

  if (last_gylphs_xcursorpos) {
    *last_gylphs_xcursorpos = x + slot->bitmap_left + draw_width;
  }

  if (left_reverse_x + reverse_width > x_max) {
    reverse_width = x_max - left_reverse_x + 1;
  }

  if (reverse_width < 0) {
    reverse_width = 0;
  }

  /*
  printf("fill glyph area at %d,%d / %dx%d, max: %d with %d for '%c'\n",
      left_reverse_x,
      y,
      reverse_width,
      font->line_height,
      x_max,
      background_colour,
      charcode);
  */

  screen_pixel_interface->fill_area(
      left_reverse_x,
      y,
      reverse_width,
      font->line_height - clip_top - clip_bottom,
      red_from_z_rgb_colour(background_colour),
      green_from_z_rgb_colour(background_colour),
      blue_from_z_rgb_colour(background_colour));

  x += slot->bitmap_left;

  /*
  printf("y: %d, %d, %d\n",
      y, (int)font->face->size->metrics.ascender/64, (int)slot->bitmap_top);
  */
  //y += font->face->size->metrics.ascender/64 - slot->bitmap_top;

  // Bitmaps for glyphs don't all have the same height. For smaller
  // (e.g. lowercase) letters bitmaps may be smaller.
  // To avoid drawing glyphs top-aligned we'll calculate the appropriate
  // top_space we have to skip at the top.
  top_space
    = font->face->size->metrics.ascender/64
    - slot->bitmap_top;

  max_y = y + font->line_height - clip_top - clip_bottom;

  // In case we've got to clip some pixels at the top we'll adjust
  // top_space (and clip_top) accordingly.
  if (clip_top > 0) {
    if (top_space > clip_top) {
      top_space -= clip_top;
      clip_top = 0;
    }
    else {
      clip_top -= top_space;
      top_space = 0;
    }
  }
  // compiler error? doesn't work without "number_of_rows_available" below:
  //if (y + top_space + (bitmap.rows - clip_top) < max_y) {

  number_of_rows_available = y + top_space + (bitmap.rows - clip_top);
  if (number_of_rows_available < max_y) {
    max_y = y + top_space + (bitmap.rows - clip_top);
  }
  y += top_space;
  bitmap_start_y = clip_top;

  TRACE_LOG("ascender: %ld\n", font->face->size->metrics.ascender/64);
  TRACE_LOG("bitmap_top: %d\n", slot->bitmap_top);

  // FIXME: Free glyph's memory.
  // FT_Done_FreeType

  //use_kerning = FT_HAS_KERNING(font->face);

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

  br = red_from_z_rgb_colour(background_colour);
  bg = green_from_z_rgb_colour(background_colour);
  bb = blue_from_z_rgb_colour(background_colour);

  dr = red_from_z_rgb_colour(foreground_colour) - br;
  dg = green_from_z_rgb_colour(foreground_colour) - bg;
  db = blue_from_z_rgb_colour(foreground_colour) - bb;

  start_x = x;
  screen_y = y;
  /*
  printf("Glyph display at %03d/%03d, %02d*%02d for char '%c'.\n", x, y,
      bitmap.width, bitmap.rows, charcode);
  */
  TRACE_LOG("Glyph display at %d / %d.\n", x, y);
  TRACE_LOG("clip_top: %d, clip_bottom: %d.\n", clip_top, clip_bottom);

    //= bitmap.rows > font->line_height - clip_top
    //? bitmap.rows - (font->line_height - clip_top)
    //: 0;

  TRACE_LOG("bitmap.rows: %d, clip_bottom: %d, bitmap_start_y: %d.\n",
      bitmap.rows, clip_bottom, bitmap_start_y);
  TRACE_LOG("diff: %d.\n", bitmap.rows - clip_bottom);

  if (bitmap.pixel_mode == FT_PIXEL_MODE_LCD) {
    for (
        bitmap_y = bitmap_start_y;
        screen_y < max_y;
        bitmap_y++, screen_y++) {
      TRACE_LOG("bitmap_y: %d, diff: %d.\n",
          bitmap_y, bitmap.rows - clip_bottom);
      screen_x = start_x;
      for (bitmap_x=0; bitmap_x<bitmap.width; bitmap_x+=3, screen_x++) {
        pixel = bitmap.buffer[bitmap_y*bitmap.pitch+ bitmap_x];
        pixel2 = bitmap.buffer[bitmap_y*bitmap.pitch + bitmap_x + 1];
        pixel3 = bitmap.buffer[bitmap_y*bitmap.pitch+ bitmap_x + 2];
        if (pixel && pixel2 && pixel3 ) {
          pixel_value = (float)pixel / (float)255;
          pixel_value2 = (float)pixel2 / (float)255;
          pixel_value3 = (float)pixel3 / (float)255;
          screen_pixel_interface->draw_rgb_pixel(
              screen_y,
              screen_x,
              br + pixel_value * dr,
              bg + pixel_value2 * dg,
              bb + pixel_value3 * db);
        }
      }
    }
  }
  else {
    for (
        bitmap_y = bitmap_start_y;
        screen_y < max_y;
        bitmap_y++, screen_y++) {
      TRACE_LOG("bitmap_y: %d, diff: %d.\n",
          bitmap_y, bitmap.rows - clip_bottom);
      screen_x = start_x;
      for (bitmap_x=0; bitmap_x<bitmap.width; bitmap_x++, screen_x++) {
        pixel = bitmap.buffer[bitmap_y*bitmap.pitch+ bitmap_x];
        if (pixel) {
          pixel_value = (float)pixel / (float)255;
          screen_pixel_interface->draw_rgb_pixel(
              screen_y,
              screen_x,
              br + pixel_value * dr,
              bg + pixel_value * dg,
              bb + pixel_value * db);
        }
      }
    }
  }

  TRACE_LOG("Glyph advance is %d.\n", advance);
  //FT_Done_Glyph(
  return left_reverse_x + reverse_width;
}


void tt_destroy_font(true_type_font *font) {
  if (font->glyph_size_cache != NULL) {
    free(font->glyph_size_cache);
  }
  FT_Done_Face(font->face);
  free(font);
}


