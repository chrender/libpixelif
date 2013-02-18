
/* freetype_wordwrapper.h
 *
 * This file is part of fizmo.
 *
 * Copyright (c) 2011-2013 Christoph Ender.
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


#ifndef freetype_wordwrapper_h_INCLUDED
#define freetype_wordwrapper_h_INCLUDED

#include "tools/types.h"

struct freetype_wordwrap_metadata {
  long output_index;
  void (*metadata_output_function)(void *ptr_parameter, uint32_t int_parameter);
  void *ptr_parameter;
  uint32_t int_parameter;
  FT_Face new_face;
};


typedef struct {
  FT_Face face;
  int line_length; // in pixels
  z_ucs *input_buffer;
  long input_buffer_size;
  long last_word_end_pixel_pos, current_pixel_pos;
  long last_word_end_index, current_buffer_index;
  bool enable_hyphenation;
  struct freetype_wordwrap_metadata *metadata;
  int metadata_size;
  int metadata_index;
  void (*wrapped_text_output_destination)(z_ucs *output, void *parameter);
  void *destination_parameter;
  FT_Bool use_kerning;
} freetype_wordwrapper;


freetype_wordwrapper *create_freetype_wordwrapper(FT_Face face, int line_length,
    void (*wrapped_text_output_destination)(z_ucs *output, void *parameter),
    void *destination_parameter, bool hyphenation_enabled);
void destroy_freetype_wrapper(freetype_wordwrapper * wrapper);
void freetype_wrap_z_ucs(freetype_wordwrapper *wrapper, z_ucs *input);
void freetype_wordwrap_flush_output(freetype_wordwrapper *wrapper);
void freetype_wordwrap_insert_metadata(freetype_wordwrapper *wrapper,
    void (*metadata_output)(void *ptr_parameter, uint32_t int_parameter),
    void *ptr_parameter, uint32_t int_parameter, FT_Face new_face);
void freetype_wordwrap_adjust_line_length(freetype_wordwrapper *wrapper,
    size_t new_line_length);

#endif // freetype_wordwrapper_h_INCLUDED

