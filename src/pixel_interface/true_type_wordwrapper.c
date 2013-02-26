
/* true_type_wordwrapper.c
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

#include "true_type_wordwrapper.h"
#include "tools/z_ucs.h"
#include "tools/types.h"
#include "tools/unused.h"
#include "interpreter/fizmo.h"
#include "tools/tracelog.h"


static void set_font(true_type_wordwrapper *wrapper, true_type_font *new_font) {
  wrapper->current_font = new_font;
}


true_type_wordwrapper *create_true_type_wordwrapper(true_type_font *font,
    int line_length,
    void (*wrapped_text_output_destination)(z_ucs *output, void *parameter),
    void *destination_parameter, bool hyphenation_enabled) {
  true_type_wordwrapper *result = fizmo_malloc(sizeof(true_type_wordwrapper));

  result->line_length = line_length;
  result->input_buffer = NULL;
  result->input_buffer_size = 0;
  result->current_buffer_index = 0;
  result->last_word_end_index = -1;
  result->last_word_end_advance_position = 0;
  result->current_advance_position = 0;
  result->wrapped_text_output_destination = wrapped_text_output_destination;
  result->destination_parameter = destination_parameter;
  result->enable_hyphenation = hyphenation_enabled;
  result->metadata = NULL;
  result->metadata_size = 0;
  result->metadata_index = 0;
  set_font(result, font);

  return result;
}


void destroy_freetype_wrapper(true_type_wordwrapper *wrapper) {
  if (wrapper->input_buffer != NULL) {
    free(wrapper->input_buffer);
  }
  if (wrapper->metadata != NULL) {
    free(wrapper->metadata);
  }
  free(wrapper);
}


inline static int ensure_additional_buffer_capacity(
    true_type_wordwrapper *wrapper, int size) {
  z_ucs *ptr;
  long new_size;

  TRACE_LOG("new min size: %d, cursize: %d\n",
      wrapper->current_buffer_index + size,
      wrapper->input_buffer_size);
  if (wrapper->current_buffer_index + size > wrapper->input_buffer_size) {
    new_size = (size - (size % 1024) + 1024);
    if ((ptr = realloc(wrapper->input_buffer, new_size)) == NULL)
      return -1;
    wrapper->input_buffer = ptr;
    wrapper->input_buffer_size = new_size;
    TRACE_LOG("new size: %ld\n, new ptr: %p\n", wrapper->input_buffer_size,
        wrapper->input_buffer);
  }

  return 0;
}


int get_current_pixel_position(true_type_wordwrapper *wrapper) {
  return wrapper->current_advance_position;
}


void flush_line(true_type_wordwrapper *wrapper, long flush_index) {
  z_ucs buf;
  size_t chars_sent;
  z_ucs *ptr;
  int metadata_index = 0;
  int output_metadata_index, output_index;
  struct freetype_wordwrap_metadata *metadata_entry;
  int i;

  TRACE_LOG("flush on: %c %d \n",
      (char)wrapper->input_buffer[flush_index],
      wrapper->input_buffer[flush_index]);

  if (flush_index == -1) {
    flush_index = wrapper->current_buffer_index - 1;
  }
  ensure_additional_buffer_capacity(wrapper, 1);
  flush_index++;

  output_index = 0;
  while (metadata_index < wrapper->metadata_index) {

    output_metadata_index =
      wrapper->metadata[metadata_index].output_index;

    TRACE_LOG("mdoutput: mdindex: %d, flushindex: %d\n",
        output_metadata_index, flush_index);

    if (output_metadata_index > flush_index) {
     break;
    }

    if (output_index < output_metadata_index) {
      buf = wrapper->input_buffer[output_metadata_index];
      wrapper->input_buffer[output_metadata_index] = 0;
      wrapper->wrapped_text_output_destination(
          wrapper->input_buffer + output_index,
          wrapper->destination_parameter);
      wrapper->input_buffer[output_metadata_index] = buf;
      output_index = output_metadata_index;
    }

    while ( (metadata_index < wrapper->metadata_index)
        && (wrapper->metadata[metadata_index].output_index
          == output_metadata_index) ) {

      metadata_entry = &wrapper->metadata[metadata_index];

      TRACE_LOG("Output metadata prm %d at %ld.\n",
          metadata_entry->int_parameter,
          output_metadata_index);

      metadata_entry->metadata_output_function(
          metadata_entry->ptr_parameter,
          metadata_entry->int_parameter);

      metadata_index++;
    }
  }

  buf = wrapper->input_buffer[flush_index];
  TRACE_LOG("0 to %p\n", wrapper->input_buffer + flush_index);
  wrapper->input_buffer[flush_index] = 0;
  ptr = wrapper->input_buffer;
  while (*ptr) {
    TRACE_LOG("buf: %c %d\n", *ptr, *ptr);
    ptr++;
  }
  TRACE_LOG("flush-index: %d\n", flush_index);
  TRACE_LOG("flush-line (%d / %p): \"", z_ucs_len(wrapper->input_buffer),
      wrapper->input_buffer);
  TRACE_LOG_Z_UCS(wrapper->input_buffer);
  TRACE_LOG("\"\n");
  wrapper->wrapped_text_output_destination(
      wrapper->input_buffer + output_index,
      wrapper->destination_parameter);
  wrapper->input_buffer[flush_index] = buf;

  chars_sent = flush_index;
  TRACE_LOG("chars_sent: %ld, dst: %p, src: %p, bufind: %d\n",
      chars_sent,
      wrapper->input_buffer,
      wrapper->input_buffer + flush_index,
      wrapper->current_buffer_index);
  memmove(
      wrapper->input_buffer,
      wrapper->input_buffer + flush_index,
      chars_sent * sizeof(z_ucs));
  for (i=0; i<wrapper->metadata_index; i++)
    wrapper->metadata[i].output_index -= chars_sent;

  wrapper->current_buffer_index -= chars_sent;
  //wrapper->last_word_end_index = -1;
  //wrapper->last_word_end_advance_position = -1;
  //wrapper->current_advance_position = 0; // no need to adjust
}


void freetype_wrap_z_ucs(true_type_wordwrapper *wrapper, z_ucs *input) {
  z_ucs *input_index = input;
  z_ucs current_char, last_char; //, char_before_last_char;
  z_ucs buf_1;
  long flush_index;

  // In order to build an algorithm most suitable to both enabled and
  // disabled hyphenation, we'll collect input until we'll find the first
  // space or newline behind the right margin and then initiate a buffer
  // flush, which will then break according to the given situation.

  // Invoking a separate flush method makes sense since it also allows
  // a simple external triggering of the flush.
  
  // To minimize calculations, we keeping track of the last complete word's
  // rightmost char in last_word_end_index and the "word's last advance
  // position" in last_word_end_advance_position.

  // An "advance position" sums up all the "horiAdvance" distances for all
  // glyphs in the current word.
  
  // last_word_end_advance_position contains the advance position of
  // the last complete word/char which still fits into the current line.
  // This is updated every time we hit a space (so we can easily find the last
  // valid position to break).


  // We'll start by fetching the last char from the buffer (if possible) for
  // purposes of kerning.
  last_char
    = (wrapper->current_buffer_index > 0)
    ? wrapper->input_buffer[wrapper->current_buffer_index - 1]
    : 0;
 
  while (*input_index != 0) {
    current_char = *input_index;

    ensure_additional_buffer_capacity(wrapper, 1);
    wrapper->input_buffer[wrapper->current_buffer_index] = current_char;

    TRACE_LOG("buffer-add: %c / %ld / %p / cap:%ld / lweap:%ld \n",
        (char)current_char,
        wrapper->current_buffer_index,
        wrapper->input_buffer + wrapper->current_buffer_index,
        wrapper->current_advance_position,
        wrapper->last_word_end_advance_position);

    wrapper->current_buffer_index++;

    if (current_char != Z_UCS_NEWLINE) {
      wrapper->current_advance_position
        += tt_get_glyph_advance(wrapper->current_font, current_char, last_char);
    }

    // In case we're hitting a space or newline we have to evaluate whether
    // we're now at a position behind the right margin.
    if ((current_char == Z_UCS_SPACE) || (current_char == Z_UCS_NEWLINE)) {
      // We'll check whether things fit in to the current line.
      TRACE_LOG("lwei: %ld / lweap: %ld / ll: %d\n",
          wrapper->last_word_end_index,
          wrapper->last_word_end_advance_position,
          wrapper->line_length);

      if (wrapper->current_advance_position > wrapper->line_length) {
        // In case we exceed the right margin, we have to break the line
        // before the last word.

        if (wrapper->last_word_end_index < 0) {
          TRACE_LOG("break at %ld, 1\n", wrapper->current_buffer_index);
          // If the current line does not contain a single word end, we'll
          // have to break at the middle of the word.
          flush_index = wrapper->current_buffer_index;
          buf_1 = wrapper->input_buffer[flush_index];
          wrapper->input_buffer[flush_index] = Z_UCS_NEWLINE;
          flush_line(wrapper, flush_index);
          wrapper->input_buffer[flush_index] = buf_1;
          wrapper->current_advance_position = 0;
        }
        else {
          // Otherwise, we simply break after the last word.
          TRACE_LOG("break at %ld, 2\n", wrapper->last_word_end_index + 1);
          flush_index = wrapper->last_word_end_index;

          wrapper->input_buffer[flush_index] = Z_UCS_NEWLINE;
          flush_line(wrapper, flush_index);

          wrapper->current_advance_position
            -= wrapper->last_word_end_advance_position;

          wrapper->last_word_end_advance_position
            = wrapper->current_advance_position;

          wrapper->last_word_end_index
            = wrapper->current_buffer_index;
        }
      }
      else if (current_char == Z_UCS_NEWLINE) {
        // In case everything fits into the current line and the current
        // char is a newline, we can simply flush at the current position.
        flush_line(
            wrapper,
            wrapper->current_buffer_index - 1);

        wrapper->last_word_end_advance_position = 0;
        wrapper->current_advance_position = 0;
        wrapper->last_word_end_index = -1;
      }
      else {
        // If we're not past the right margin and the current char is not
        // a newline, we've encountered a space and a word end.
        if (last_char != Z_UCS_SPACE) {
          TRACE_LOG("lweap-at-space: %ld\n",
              wrapper->last_word_end_advance_position);
          wrapper->last_word_end_advance_position
            = wrapper->current_advance_position;
          wrapper->last_word_end_index
            = wrapper->current_buffer_index - 1;
        }
      }
    }

    input_index++;
    last_char = current_char;
  }
}


void freetype_wordwrap_flush_output(true_type_wordwrapper* wrapper) {
  flush_line(wrapper, -1);
}


void freetype_wordwrap_insert_metadata(true_type_wordwrapper *wrapper,
    void (*metadata_output)(void *ptr_parameter, uint32_t int_parameter),
    void *ptr_parameter, uint32_t int_parameter, true_type_font *new_font) {
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
  wrapper->metadata[wrapper->metadata_index].new_font
    = new_font;

  TRACE_LOG("Added new metadata entry at %ld with int-parameter %ld, ptr:%p.\n",
      wrapper->current_buffer_index,
      (long int)int_parameter, ptr_parameter);

  wrapper->metadata_index++;

}

void freetype_wordwrap_adjust_line_length(true_type_wordwrapper *wrapper,
    size_t new_line_length) {
  wrapper->line_length = new_line_length;
}

