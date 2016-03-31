
/* true_type_wordwrapper.h
 *
 * This file is part of fizmo.
 *
 * Copyright (c) 2011-2016 Christoph Ender.
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


#ifndef true_type_wordwrapper_h_INCLUDED
#define true_type_wordwrapper_h_INCLUDED

#include "tools/types.h"
#include "true_type_font.h"

struct freetype_wordwrap_metadata {
  long output_index;
  void (*metadata_output_function)(void *ptr_parameter, uint32_t int_parameter);
  void *ptr_parameter;
  true_type_font *font;
  uint32_t int_parameter;
};


typedef struct {
  true_type_font *current_font;
  int line_length; // in pixels
  bool enable_hyphenation;
  struct freetype_wordwrap_metadata *metadata;
  int metadata_size;
  int metadata_index;
  true_type_font *font_at_buffer_start;
  void (*wrapped_text_output_destination)(z_ucs *output, void *parameter);
  void *destination_parameter;
  z_ucs *input_buffer;
  long input_buffer_size;
  long current_buffer_index;
  long last_word_end_index; // last word end buffer index
  long last_word_end_advance_position; // right position of last word in line
  long last_word_end_width_position;
  long current_advance_position;
  long last_width_position;
  true_type_font *last_chars_font;
  long current_width_position;
  int space_bitmap_width;
  int space_advance;
  int dash_bitmap_width;
  int dash_advance;
} true_type_wordwrapper;


true_type_wordwrapper *create_true_type_wordwrapper(
    true_type_font *current_font, int line_length,
    void (*wrapped_text_output_destination)(z_ucs *output, void *parameter),
    void *destination_parameter, bool hyphenation_enabled);
void destroy_freetype_wrapper(true_type_wordwrapper * wrapper);
int get_current_pixel_position(true_type_wordwrapper *wrapper);
void freetype_wrap_z_ucs(true_type_wordwrapper *wrapper, z_ucs *input);
void freetype_wordwrap_flush_output(true_type_wordwrapper *wrapper);
void freetype_wordwrap_insert_metadata(true_type_wordwrapper *wrapper,
    void (*metadata_output)(void *ptr_parameter, uint32_t int_parameter),
    void *ptr_parameter, uint32_t int_parameter, true_type_font *new_font);
void freetype_wordwrap_adjust_line_length(true_type_wordwrapper *wrapper,
    size_t new_line_length);
void freetype_wordwrap_reset_position(true_type_wordwrapper *wrapper);

#endif // true_type_wordwrapper_h_INCLUDED

