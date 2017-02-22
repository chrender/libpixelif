
/* true_type_font.h
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


#ifndef true_type_font_h_INCLUDED
#define true_type_font_h_INCLUDED

#include <ft2build.h>
#include FT_FREETYPE_H

#include "true_type_font.h"
#include "../screen_interface/screen_pixel_interface.h"


typedef struct glyph_size_struct {
    int is_valid;
    int advance;
    int bitmap_width;
} glyph_size;

struct true_type_font_struct {
  FT_Face face;
  //bool has_kerning;
  int font_height_in_pixel;
  int line_height;
  z_ucs last_char; // for kerning
  FT_Render_Mode render_mode;
  glyph_size *glyph_size_cache;
  long glyph_size_cache_size;
};

typedef struct true_type_font_struct true_type_font;

int tt_get_glyph_size(true_type_font *font, z_ucs char_code,
    int *advance, int *bitmap_width);
int tt_draw_glyph(true_type_font *font, int x, int y, int x_max,
    int clip_top, int clip_bottom,
    z_rgb_colour foreground_colour, z_rgb_colour background_colour,
    struct z_screen_pixel_interface *screen_pixel_interface,
    z_ucs charcode, int *last_gylphs_xcursorpos);
void tt_destroy_font(true_type_font *font);

#endif // true_type_font_h_INCLUDED

