
/* freetype_wordwrapper.c
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


/*
#include <string.h>
#include <math.h>
*/

#include <ft2build.h>
#include FT_FREETYPE_H

/*
#include "tools/i18n.h"
#include "interpreter/cmd_hst.h"
#include "interpreter/config.h"
#include "interpreter/history.h"
#include "interpreter/streams.h"
#include "interpreter/text.h"
#include "interpreter/wordwrap.h"
#include "interpreter/zpu.h"
#include "interpreter/output.h"

#include "../screen_interface/screen_pixel_interface.h"
#include "../locales/libpixelif_locales.h"
*/

#include "freetype_wordwrapper.h"
#include "tools/z_ucs.h"
#include "tools/types.h"
#include "tools/unused.h"
#include "interpreter/fizmo.h"
#include "tools/tracelog.h"



static void freetype_init_face(freetype_wordwrapper *wrapper, FT_Face face) {
  wrapper->face = face;
  wrapper->use_kerning = FT_HAS_KERNING(face);
}


freetype_wordwrapper *create_freetype_wordwrapper(FT_Face face, int line_length,
    void (*wrapped_text_output_destination)(z_ucs *output, void *parameter),
    void *destination_parameter, bool hyphenation_enabled) {
  freetype_wordwrapper *result = fizmo_malloc(sizeof(freetype_wordwrapper));

  result->line_length = line_length;
  result->input_buffer = NULL;
  result->input_buffer_size = 0;
  result->current_buffer_index = 0;
  result->current_pixel_pos = 0;
  result->last_word_end_index = 0;
  result->last_word_end_pixel_pos = 0;
  result->wrapped_text_output_destination = wrapped_text_output_destination;
  result->destination_parameter = destination_parameter;
  result->enable_hyphenation = hyphenation_enabled;
  result->metadata = NULL;
  result->metadata_size = 0;
  result->metadata_index = 0;
  freetype_init_face(result, face);

  return result;
}


void destroy_freetype_wrapper(freetype_wordwrapper *wrapper) {
  if (wrapper->input_buffer != NULL) {
    free(wrapper->input_buffer);
  }
  if (wrapper->metadata != NULL) {
    free(wrapper->metadata);
  }
  free(wrapper);
}


inline static int ensure_additional_buffer_capacity(
    freetype_wordwrapper *wrapper, int size) {
  z_ucs *ptr;
  long new_size;

  if (wrapper->current_buffer_index + size > wrapper->input_buffer_size) {
    new_size = (size - (size % 1024) + 1024);
    if ((ptr = realloc(wrapper->input_buffer, new_size)) == NULL)
      return -1;
    wrapper->input_buffer = ptr;
    wrapper->input_buffer_size = new_size;
  }

  return 0;
}


void freetype_wrap_z_ucs(freetype_wordwrapper *wrapper, z_ucs *input) {
  FT_GlyphSlot slot;
  //FT_Vector kerning;
  int advance, ft_error;
  z_ucs *input_index = input;
  z_ucs last_char, current_char;
  bool line_breaking;
  z_ucs buf_1, buf_2;

  last_char
    = ((wrapper->current_buffer_index > 0) && (wrapper->use_kerning))
    ? wrapper->input_buffer[wrapper->current_buffer_index - 1]
    : 0;

  while (*input_index != 0) {
    current_char = *input_index;

    FT_UInt glyph_index = FT_Get_Char_Index(wrapper->face, current_char);

    ft_error = FT_Load_Glyph(
        wrapper->face,
        glyph_index,
        FT_LOAD_DEFAULT);

    slot = wrapper->face->glyph;
    advance = slot->advance.x / 64;

    if ((current_char == Z_UCS_NEWLINE)
        || (wrapper->current_pixel_pos + advance > wrapper->line_length)) {
      // We'll have to break the line.
      ensure_additional_buffer_capacity(wrapper, 2);
      if ((current_char == Z_UCS_NEWLINE)
          || (wrapper->last_word_end_index == 0)) {
        wrapper->input_buffer[wrapper->current_buffer_index] = Z_UCS_NEWLINE;
        wrapper->input_buffer[wrapper->current_buffer_index + 1] = 0;
        wrapper->wrapped_text_output_destination(
            wrapper->input_buffer, wrapper->destination_parameter);
        wrapper->current_buffer_index = 0;
        wrapper->current_pixel_pos = 0;
      }
      else {
        buf_1 = wrapper->input_buffer[wrapper->last_word_end_index + 1];
        buf_2 = wrapper->input_buffer[wrapper->last_word_end_index + 2];
        wrapper->input_buffer[wrapper->last_word_end_index + 1] = Z_UCS_NEWLINE;
        wrapper->input_buffer[wrapper->last_word_end_index + 2] = 0;
        wrapper->wrapped_text_output_destination(
            wrapper->input_buffer, wrapper->destination_parameter);
        wrapper->input_buffer[wrapper->last_word_end_index + 1] = buf_1;
        wrapper->input_buffer[wrapper->last_word_end_index + 2] = buf_2;
        printf("current: %ld\n", wrapper->current_buffer_index);
        /*
        TRACE_LOG("moving %ld bytes from %p to %p.\n",
            (wrapper->current_buffer_index - wrapper->last_word_end_index)
            * sizeof(z_ucs),
            wrapper->input_buffer + wrapper->last_word_end_index + 1,
            wrapper->input_buffer);
        printf("moving %ld bytes from %p to %p.\n",
            (wrapper->current_buffer_index + 1 - chars_sent) * sizeof(z_ucs),
            wrapper->input_buffer + chars_sent,
            wrapper->input_buffer);
        */
        printf("Copying %ld bytes.\n", ((wrapper->current_buffer_index
                - (wrapper->last_word_end_index + 2)) * sizeof(z_ucs)));
        memmove(
            wrapper->input_buffer,
            wrapper->input_buffer + wrapper->last_word_end_index + 2,
            ((wrapper->current_buffer_index
              - (wrapper->last_word_end_index + 2)) * sizeof(z_ucs)));
        wrapper->current_buffer_index -= (wrapper->last_word_end_index + 2);
        wrapper->current_pixel_pos -= wrapper->last_word_end_pixel_pos;
      }
      wrapper->last_word_end_index = 0;
      wrapper->last_word_end_pixel_pos = 0;
      line_breaking = true;
    }
    else {
      line_breaking = false;
    }

    if ((wrapper->current_buffer_index > 0) && (current_char == Z_UCS_SPACE)) {
      wrapper->last_word_end_index = wrapper->current_buffer_index - 1;
      wrapper->last_word_end_pixel_pos = wrapper->current_pixel_pos;
    }

    /*
    if ((line_breaking == false) && (last_char) && (wrapper->use_kerning)) {
      // In case we're not breaking the line, apply kerning if possible.
      ft_error = FT_Get_Kerning(wrapper->face, last_char, *input_index,
          FT_KERNING_DEFAULT, &kerning);
      if (ft_error) {
        printf("error: %d\n", ft_error);
        exit(-1);
      }
      advance += kerning.x / 64;
    }
    */

    if ((current_char == Z_UCS_NEWLINE)
        || ((wrapper->current_buffer_index == 0) && (current_char == Z_UCS_SPACE))) {
      last_char = 0;
    }
    else {
      ensure_additional_buffer_capacity(wrapper, 1);
      wrapper->input_buffer[wrapper->current_buffer_index] = current_char;
      wrapper->current_pixel_pos += advance;
      last_char = current_char;
      wrapper->current_buffer_index++;
    }

    printf("current pixel pos:%ld/%c/%ld\n", wrapper->current_pixel_pos,
        current_char, wrapper->current_buffer_index);

    input_index++;
  }
}


void freetype_wordwrap_flush_output(freetype_wordwrapper* wrapper) {
  printf("flush-ww\n");
  ensure_additional_buffer_capacity(wrapper, 1);
  wrapper->input_buffer[wrapper->current_buffer_index] = 0;
  wrapper->wrapped_text_output_destination(wrapper->input_buffer,
      wrapper->destination_parameter);
  //chars_sent = z_ucs_len(wrapper->input_buffer);
  wrapper->current_buffer_index = 0;
  wrapper->current_pixel_pos = 0;
}


void freetype_wordwrap_insert_metadata(freetype_wordwrapper *wrapper,
    void (*metadata_output)(void *ptr_parameter, uint32_t int_parameter),
    void *ptr_parameter, uint32_t int_parameter, FT_Face new_face) {
  size_t bytes_to_allocate;
  
  // Before adding new metadata, check if we need to allocate more space.
  if (wrapper->metadata_index == wrapper->metadata_size)
  {
    bytes_to_allocate
      = (size_t)(
          (wrapper->metadata_size + 32)
          * sizeof(struct freetype_wordwrap_metadata));

    TRACE_LOG("Allocating %d bytes for wordwrap-metadata.\n",
        (int)bytes_to_allocate);

    wrapper->metadata = (struct freetype_wordwrap_metadata*)fizmo_realloc(
        wrapper->metadata, bytes_to_allocate);

    wrapper->metadata_size += 32;

    TRACE_LOG("Wordwrap-metadata at %p.\n", wrapper->metadata);
  }

  TRACE_LOG("Current wordwrap-metadata-index is %d.\n",
      wrapper->metadata_index);

  TRACE_LOG("Current wordwrap-metadata-entry at %p.\n",
      &(wrapper->metadata[wrapper->metadata_index]));

  wrapper->metadata[wrapper->metadata_index].output_index
    = wrapper->current_buffer_index;
  wrapper->metadata[wrapper->metadata_index].metadata_output_function
    = metadata_output;
  wrapper->metadata[wrapper->metadata_index].ptr_parameter
    = ptr_parameter;
  wrapper->metadata[wrapper->metadata_index].int_parameter
    = int_parameter;
  wrapper->metadata[wrapper->metadata_index].new_face
    = new_face;

  TRACE_LOG("Added new metadata entry at %ld with int-parameter %ld, ptr:%p.\n",
      wrapper->current_buffer_index,
      (long int)int_parameter, ptr_parameter);

  wrapper->metadata_index++;

}

void freetype_wordwrap_adjust_line_length(freetype_wordwrapper *wrapper,
    size_t new_line_length) {
  wrapper->line_length = new_line_length;
}

