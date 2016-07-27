
/* true_type_wordwrapper.c
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


//#include <ft2build.h>
//#include FT_FREETYPE_H
#include "true_type_wordwrapper.h"
#include "tools/z_ucs.h"
#include "tools/types.h"
#include "tools/unused.h"
#include "interpreter/fizmo.h"
#include "tools/tracelog.h"
#include "interpreter/hyphenation.h"


static z_ucs newline_string[] = { Z_UCS_NEWLINE, 0 };
static z_ucs minus_string[] = { Z_UCS_MINUS, 0 };

static void set_font(true_type_wordwrapper *wrapper, true_type_font *new_font) {
  TRACE_LOG("Wordwrapper setting new font to %p.\n", new_font);
  wrapper->current_font = new_font;
  tt_get_glyph_size(wrapper->current_font, Z_UCS_SPACE,
      &wrapper->space_bitmap_width, &wrapper->space_advance);
  tt_get_glyph_size(wrapper->current_font, Z_UCS_MINUS,
      &wrapper->dash_bitmap_width, &wrapper->dash_advance);
}


void freetype_wordwrap_reset_position(true_type_wordwrapper *wrapper) {
  wrapper->last_word_end_index = -1;
  wrapper->last_word_end_advance_position = 0;
  wrapper->last_word_end_width_position = 0;
  wrapper->current_advance_position = 0;
  wrapper->last_width_position = 0;
  wrapper->current_width_position = 0;
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
  freetype_wordwrap_reset_position(result);
  result->wrapped_text_output_destination = wrapped_text_output_destination;
  result->destination_parameter = destination_parameter;
  result->enable_hyphenation = hyphenation_enabled;
  result->metadata = NULL;
  result->metadata_size = 0;
  result->metadata_index = 0;
  result->font_at_buffer_start = font;
  set_font(result, font);

  TRACE_LOG("Created new wordwrapper %p with line length %d.\n",
      result, line_length);

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

  TRACE_LOG("new min size: %d, cursize: %d, buffer at %p\n",
      wrapper->current_buffer_index + size,
      wrapper->input_buffer_size,
      wrapper->input_buffer);
  if (wrapper->current_buffer_index + size > wrapper->input_buffer_size) {
    new_size = (size - (size % 1024) + 1024) * sizeof(z_ucs);
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


// This function will simply forget the first char in the input buffer. The
// main work it has to do is to correctly adjust the metadata.
void forget_first_char_in_buffer(true_type_wordwrapper *wrapper) {
  int metadata_index = 0;
  struct freetype_wordwrap_metadata *metadata_entry;
  int i;

  if (wrapper->current_buffer_index < 1) {
    return;
  }

  while (metadata_index < wrapper->metadata_index) {
    //printf("mdindex: %d\n", metadata_index);
    if (wrapper->metadata[metadata_index].output_index > 0) {
      break;
    }

    // There's actually metadata for the very first char in the buffer.

    metadata_entry = &wrapper->metadata[metadata_index];

    metadata_entry->metadata_output_function(
        metadata_entry->ptr_parameter,
        metadata_entry->int_parameter);

    metadata_index++;
  }

  if (metadata_index > 0) {
    //printf("Removing %d metadata entries.\n", metadata_index);
    memmove(
        wrapper->metadata,
        &wrapper->metadata[metadata_index],
        (wrapper->metadata_index - metadata_index) * sizeof(
          struct freetype_wordwrap_metadata));
    wrapper->metadata_index -= metadata_index;
  }

  for (i=0; i<wrapper->metadata_index; i++) {
    wrapper->metadata[i].output_index -= 1;
  }

  //printf("Moving %ld chars.\n", (wrapper->current_buffer_index - 1));
  memmove(
      wrapper->input_buffer,
      wrapper->input_buffer + 1,
      (wrapper->current_buffer_index - 1) * sizeof(z_ucs));

  wrapper->current_buffer_index--;
}


void flush_line(true_type_wordwrapper *wrapper, long flush_index,
    bool append_minus, bool append_newline) {
  z_ucs buf_1; //, buf_2;
  size_t chars_sent;
  int chars_to_move;
  int metadata_index = 0;
  long output_metadata_index, output_index;
  struct freetype_wordwrap_metadata *metadata_entry;
  int i;

  if (flush_index == -1) {
    flush_index = wrapper->current_buffer_index - 1;
  }
  else
  {
    TRACE_LOG("flush on: %c %d \n",
      (char)wrapper->input_buffer[flush_index],
      wrapper->input_buffer[flush_index]);
  }
  // Ensure there's enough space to place a terminating 0.
  ensure_additional_buffer_capacity(wrapper, 1);

  output_index = 0;
  while (metadata_index < wrapper->metadata_index) {
    TRACE_LOG("Found some metadata, starting at metadata index %d of %d.\n",
        metadata_index, wrapper->metadata_index);

    // Look which buffer position is affected by the next metadata entry.
    output_metadata_index =
      wrapper->metadata[metadata_index].output_index;

    TRACE_LOG("mdoutput: mdindex: %d, flushindex: %d, output_index:%d\n",
        output_metadata_index, flush_index, output_index);

    // In case it's behind the current flush position, there's nothing
    // more to do.
    if (output_metadata_index > flush_index) {
     break;
    }

    // In case current output position is before the metadata entry's
    // output position, we'll flush everything until this position.
    if (output_index < output_metadata_index) {
      TRACE_LOG("Flusing up to next metadata entry at %ld\n",
          output_metadata_index);

      buf_1 = wrapper->input_buffer[output_metadata_index];
      wrapper->input_buffer[output_metadata_index] = 0;
      wrapper->wrapped_text_output_destination(
          wrapper->input_buffer + output_index,
          wrapper->destination_parameter);
      wrapper->input_buffer[output_metadata_index] = buf_1;
      output_index = output_metadata_index;
    }

    TRACE_LOG("metadata_index: %d,output_index: %d,output_metadata_index:%d\n",
        metadata_index, output_index, output_metadata_index);
    TRACE_LOG("wrapper->metadata[%d].output_index: %d\n",
        metadata_index,
        wrapper->metadata[metadata_index].output_index);
    TRACE_LOG("wrapper->metadata_index: %d\n",
        wrapper->metadata_index);
    // We can now flush all the metadata entries at the current position.
    TRACE_LOG("%d / %d\n",
        wrapper->metadata[metadata_index].output_index + 1,
        output_metadata_index + 1);

    while ( (metadata_index < wrapper->metadata_index)
        && (wrapper->metadata[metadata_index].output_index
          == output_metadata_index) ) {

      metadata_entry = &wrapper->metadata[metadata_index];

      TRACE_LOG("Output metadata prm %d at buffer position %ld.\n",
          metadata_entry->int_parameter,
          output_metadata_index);

      metadata_entry->metadata_output_function(
          metadata_entry->ptr_parameter,
          metadata_entry->int_parameter);

      if (metadata_entry->font != NULL) {
        wrapper->font_at_buffer_start = metadata_entry->font;
      }

      metadata_index++;
    }
  }

  if (metadata_index > 0) {
    TRACE_LOG("Removing %d metadata entries.\n", metadata_index);
    memmove(
        wrapper->metadata,
        &wrapper->metadata[metadata_index],
        (wrapper->metadata_index - metadata_index) * sizeof(
          struct freetype_wordwrap_metadata));
    wrapper->metadata_index -= metadata_index;
  }

  /*
  if (append_newline == true) {
    buf_1 = wrapper->input_buffer[flush_index + 1];
    buf_2 = wrapper->input_buffer[flush_index + 2];
    wrapper->input_buffer[flush_index + 1] = Z_UCS_NEWLINE;
    wrapper->input_buffer[flush_index + 2] = 0;
    TRACE_LOG("flush-index: %d\n", flush_index);
    TRACE_LOG("flush-line (%d / %p): \"", z_ucs_len(wrapper->input_buffer),
        wrapper->input_buffer);
    TRACE_LOG_Z_UCS(wrapper->input_buffer);
    TRACE_LOG("\"\n");
    wrapper->wrapped_text_output_destination(
        wrapper->input_buffer + output_index,
        wrapper->destination_parameter);
    wrapper->input_buffer[flush_index + 1] = buf_1;
    wrapper->input_buffer[flush_index + 2] = buf_2;
  }
  else {
    buf_1 = wrapper->input_buffer[flush_index + 1];
    wrapper->input_buffer[flush_index + 1] = 0;
    TRACE_LOG("flush-index: %d\n", flush_index);
    TRACE_LOG("flush-line (%d / %p): \"", z_ucs_len(wrapper->input_buffer),
        wrapper->input_buffer);
    TRACE_LOG_Z_UCS(wrapper->input_buffer);
    TRACE_LOG("\"\n");
    wrapper->wrapped_text_output_destination(
        wrapper->input_buffer + output_index,
        wrapper->destination_parameter);
    wrapper->input_buffer[flush_index + 1] = buf_1;
  }
  */

  buf_1 = wrapper->input_buffer[flush_index + 1];
  wrapper->input_buffer[flush_index + 1] = 0;
  TRACE_LOG("flush-index: %d\n", flush_index);
  TRACE_LOG("flush-line (%d / %p): \"", z_ucs_len(wrapper->input_buffer),
      wrapper->input_buffer);
  TRACE_LOG_Z_UCS(wrapper->input_buffer);
  TRACE_LOG("\"\n");
  wrapper->wrapped_text_output_destination(
      wrapper->input_buffer + output_index,
      wrapper->destination_parameter);
  wrapper->input_buffer[flush_index + 1] = buf_1;

  if (append_minus == true) {
    wrapper->wrapped_text_output_destination(
        minus_string,
        wrapper->destination_parameter);
  }

  if (append_newline == true) {
    wrapper->wrapped_text_output_destination(
        newline_string,
        wrapper->destination_parameter);
  }

  chars_sent = flush_index + 1;
  TRACE_LOG("chars_sent: %ld, dst: %p, src: %p, bufindex: %ld\n",
      chars_sent,
      wrapper->input_buffer,
      wrapper->input_buffer + flush_index,
      wrapper->current_buffer_index);
  if ((chars_to_move = wrapper->current_buffer_index - chars_sent) > 0) {
    memmove(
        wrapper->input_buffer,
        wrapper->input_buffer + flush_index + 1,
        chars_to_move * sizeof(z_ucs));
  }
  for (i=0; i<wrapper->metadata_index; i++)
    wrapper->metadata[i].output_index -= chars_sent;

  wrapper->current_buffer_index -= chars_sent;
}


void freetype_wrap_z_ucs(true_type_wordwrapper *wrapper, z_ucs *input,
    bool end_line_after_end_of_input) {
  z_ucs *hyphenated_word, *input_index = input;
  z_ucs current_char, last_char, buf;
  long wrap_width_position, end_index, hyph_index;
  long buf_index, last_valid_hyph_index, output_metadata_index;
  long last_valid_hyph_position, hyph_position;
  int hyph_font_dash_bitmap_width, hyph_font_dash_advance;
  int metadata_index = 0, advance, bitmap_width;
  true_type_font *hyph_font;
  bool process_line_end = end_line_after_end_of_input;

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

  while ( ((input != NULL) && (*input_index != 0))
      || (process_line_end == true) ) {

    if ((input != NULL) && (*input_index != 0)) {
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

      tt_get_glyph_size(wrapper->current_font, current_char,
          &advance, &bitmap_width);
      wrapper->current_width_position
        = wrapper->current_advance_position + bitmap_width;
      //printf("current_width_position: %ld for '%c'\n",
      //    wrapper->current_width_position, current_char);
      wrapper->current_advance_position += advance;

      /*
      printf("check:'%c',font:%p,advpos,ll,lweim,cwwp,bmw: %ld/%d/%ld/%ld/%d\n",
          current_char,
          wrapper->current_font,
          wrapper->current_advance_position,
          wrapper->line_length,
          wrapper->last_word_end_index,
          wrapper->current_width_position,
          bitmap_width);

      printf("bmw, lwp, ll: %d, %ld, %d.\n",
          bitmap_width,
          wrapper->last_width_position,
          wrapper->line_length);
      */
    }
    else {
      current_char = 0;
      bitmap_width = 0;
      advance = 0;
    }

    if ( (
          (bitmap_width == 0)
          && (wrapper->last_width_position >= wrapper->line_length)
         )
        || (wrapper->current_width_position >= wrapper->line_length) ) {

      wrapper->last_width_position
        = wrapper->current_width_position;
      wrapper->last_chars_font
        = wrapper->current_font;

      /*
      printf("linebehind: %ld, %d.\n",
          wrapper->current_width_position, wrapper->line_length);
      */

      TRACE_LOG("Behind past line, match on space|newline, breaking.\n");
      // In case we're past the right margin, we'll take a look how
      // to best break the line.

      if ((current_char == Z_UCS_SPACE) || (current_char == Z_UCS_NEWLINE)
          || (((input == NULL) || (*input_index == 0))
            && (process_line_end == true)) ) {
        // Here we've found a completed word past the lind end. At this
        // point we'll break the line without exception.
        /*
        printf("last_word_end_width_position: %ld, line_length: %d.\n",
            wrapper->last_word_end_width_position, wrapper->line_length);
        */

        if (wrapper->enable_hyphenation == true) {
          end_index = wrapper->current_buffer_index - 2;
          TRACE_LOG("end_index: %d / %c, lwei: %d\n",
              end_index, wrapper->input_buffer[end_index],
              wrapper->last_word_end_index);
          while ( (end_index >= 0)
              && (end_index > wrapper->last_word_end_index)
              && ( (wrapper->input_buffer[end_index] == Z_UCS_COMMA)
                || (wrapper->input_buffer[end_index] == Z_UCS_DOT) ) ) {
            end_index--;
          }
          TRACE_LOG("end end_index: %d / %c, lwei: %d\n",
              end_index, wrapper->input_buffer[end_index],
              wrapper->last_word_end_index);
          if (end_index > wrapper->last_word_end_index) {
            end_index++;
            buf = wrapper->input_buffer[end_index];
            wrapper->input_buffer[end_index] = 0;
            if ((hyphenated_word = hyphenate(wrapper->input_buffer
                    + wrapper->last_word_end_index + 1)) == NULL) {
              TRACE_LOG("Error hyphenating.\n");
            }
            else {
              TRACE_LOG("hyphenated word: \"");
              TRACE_LOG_Z_UCS(hyphenated_word);
              TRACE_LOG("\".\n");

              wrap_width_position
                = wrapper->last_word_end_advance_position
                + wrapper->space_bitmap_width;
              hyph_index = wrapper->last_word_end_index + 1;
              buf_index = 0;
              last_valid_hyph_index = -1;

              // We'll now have to find the correct font for the start
              // of our hyphenated word. For that we'll remember the current
              // font at buffer start and iterate though all the metadata
              // until we're at the hyphenated word's beginning.
              hyph_font = wrapper->font_at_buffer_start;
              tt_get_glyph_size(hyph_font, Z_UCS_MINUS,
                  &hyph_font_dash_bitmap_width, &hyph_font_dash_advance);
              metadata_index = 0;

              while ( (buf_index < z_ucs_len(hyphenated_word))
                  && (wrap_width_position + hyph_font_dash_bitmap_width
                    <= wrapper->line_length) ) {
                /*
                   printf("Checking buf char %ld / %c, hi:%ld\n",
                   buf_index, hyphenated_word[buf_index],
                   hyph_index);
                   */
                TRACE_LOG("Checking buf char %ld / %c, hi:%ld\n",
                    buf_index, hyphenated_word[buf_index],
                    hyph_index);

                while (metadata_index < wrapper->metadata_index) {
                  //printf("metadata: %d of %d.\n",
                  //    metadata_index, wrapper->metadata_index);

                  output_metadata_index =
                    wrapper->metadata[metadata_index].output_index;

                  //printf("output_metadata_index: %ld, hyph_index: %ld\n",
                  //    output_metadata_index, hyph_index);
                  //printf("hyph_font: %p.\n", hyph_font);

                  if (output_metadata_index <= hyph_index) {
                    if (wrapper->metadata[metadata_index].font != NULL) {
                      hyph_font = wrapper->metadata[metadata_index].font;
                      tt_get_glyph_size(hyph_font, Z_UCS_MINUS,
                          &hyph_font_dash_bitmap_width,
                          &hyph_font_dash_advance);
                    }
                    metadata_index++;
                  }
                  else {
                    break;
                  }
                }

                if (hyphenated_word[buf_index] == Z_UCS_SOFT_HYPEN) {
                  last_valid_hyph_index = hyph_index + 1;
                }
                else if (hyphenated_word[buf_index] == Z_UCS_MINUS) {
                  last_valid_hyph_index = hyph_index;
                }

                if (hyphenated_word[buf_index] != Z_UCS_SOFT_HYPEN) {
                  //printf("hyph_font: %p.\n", hyph_font);
                  tt_get_glyph_size(hyph_font,
                      hyphenated_word[buf_index],
                      &advance, &bitmap_width);
                  //wrap_width_position += bitmap_width;
                  wrap_width_position += advance;
                  hyph_index++;
                }

                if ( (hyphenated_word[buf_index] == Z_UCS_SOFT_HYPEN)
                    || (hyphenated_word[buf_index] == Z_UCS_MINUS) ) {
                  last_valid_hyph_position = wrap_width_position;
                }

                buf_index++;

                /*
                   printf("(%ld + %d) = %ld <= %d\n",
                   wrap_width_position, hyph_font_dash_bitmap_width,
                   wrap_width_position + hyph_font_dash_bitmap_width,
                   wrapper->line_length);
                   */
              }

              free(hyphenated_word);

              if (last_valid_hyph_index != -1) {
                /*
                   printf("Found valid hyph pos at %ld / %c.\n",
                   last_valid_hyph_index,
                   wrapper->input_buffer[last_valid_hyph_index]);
                   */
                TRACE_LOG("Found valid hyph pos at %ld / %c.\n",
                    last_valid_hyph_index,
                    wrapper->input_buffer[last_valid_hyph_index]);
                hyph_index = last_valid_hyph_index;
                hyph_position = last_valid_hyph_position;
              }
              else {
                hyph_index = wrapper->last_word_end_index;
                hyph_position = wrapper->last_word_end_advance_position;
                //printf("no valid hyph, hyph_index: %ld.\n", hyph_index);
              }
            }
            wrapper->input_buffer[end_index] = buf;
          } // endif (end_index > wrapper->last_work_end_index)
          else {
            hyph_index = end_index;
            //printf("Hyph at %ld.\n", hyph_index);
          }
        } // endif (wrapper->enable_hyphentation == true)
        else {
          // Check for dashes inside the last word.
          // Example: "first-class car", where the word end we've now
          // found is between "first-class" and "car".
          wrap_width_position = wrapper->current_width_position;
          hyph_index = wrapper->current_buffer_index - 2;
          while ( (hyph_index >= 0)
              && (hyph_index > wrapper->last_word_end_index)) {
            /*
            printf("hyph_index: %ld / '%c'.\n",
                hyph_index, wrapper->input_buffer[hyph_index]);
            */

            if ( (wrapper->input_buffer[hyph_index] == Z_UCS_MINUS)
                && (wrap_width_position <= wrapper->line_length) ) {
              // Found a dash to break on
              break;
            }
            tt_get_glyph_size(wrapper->current_font,
                wrapper->input_buffer[hyph_index],
                &advance, &bitmap_width);
            wrap_width_position -= bitmap_width;
            hyph_index--;
          }
          hyph_position = wrap_width_position;
        }

        //printf("breaking on char %ld / %c.\n",
        //    hyph_index, wrapper->input_buffer[hyph_index]);
        TRACE_LOG("breaking on char %ld / %c.\n",
            hyph_index, wrapper->input_buffer[hyph_index]);

        if (wrapper->input_buffer[hyph_index] == Z_UCS_MINUS) {
          // We're wrappring on a in-word-dash.
          flush_line(wrapper, hyph_index, false, true);
        }
        else if (wrapper->input_buffer[hyph_index] == Z_UCS_SPACE) {
          flush_line(wrapper, hyph_index - 1, false, true);
          // In case we're wrapping between words without hyphenation or
          // in-word-dashes we'll have to get rid of the remaining leading
          // space-char in the input buffer.
          forget_first_char_in_buffer(wrapper);
        }
        else {
          if (wrapper->input_buffer[hyph_index - 2] == Z_UCS_MINUS) {
            flush_line(wrapper, hyph_index - 3, true, true);
            forget_first_char_in_buffer(wrapper);
          }
          else {
            flush_line(wrapper, hyph_index - 2, true, true);
          }
        }

        wrapper->current_advance_position
          -= hyph_position - wrapper->dash_bitmap_width;

        /*
        wrapper->last_width_position
          = wrapper->current_width_position
          - wrapper->last_word_end_advance_position;
          */

        wrapper->current_width_position
          = wrapper->current_advance_position;

        wrapper->last_word_end_advance_position
          = wrapper->current_advance_position;

        wrapper->last_word_end_width_position
          = wrapper->current_width_position;

        wrapper->last_word_end_index = -1;
      }
      else if (wrapper->current_advance_position > wrapper->line_length * 2) {
        // In case we haven't found a word end we'll only force a line
        // break in case we've filled two full lines of text.

        /*
        printf("flush on: %c\n", wrapper->input_buffer[
            wrapper->current_buffer_index - 2]);
        */
        flush_line(wrapper, wrapper->current_buffer_index - 2, false, true);

        wrapper->current_advance_position
          = advance;

        wrapper->current_width_position
          = wrapper->current_advance_position + bitmap_width;

        wrapper->last_word_end_index = -1;
        wrapper->last_word_end_advance_position = 0;
        wrapper->last_word_end_width_position = 0;

        //printf("first buf: %c\n", wrapper->input_buffer[0]);

        /*
        printf("current_buffer_index: %ld\n", wrapper->current_buffer_index);
        printf("current_advance_position: %ld\n",
            wrapper->current_advance_position);
        printf("break at %ld, 1\n", wrapper->last_char_in_line_index);
        old_index = wrapper->current_buffer_index;
        flush_line(wrapper, wrapper->last_char_in_line_index, false, true);
        chars_sent_count = old_index - wrapper->current_buffer_index;

        printf("chars_sent_count: %ld\n", chars_sent_count);
        printf("current_buffer_index: %ld\n", wrapper->current_buffer_index);

        tt_get_glyph_size(wrapper->last_chars_in_line_font,
            wrapper->input_buffer[wrapper->current_buffer_index],
            &advance, &bitmap_width);

        wrapper->current_advance_position
          -= wrapper->last_chars_in_line_advance_position;

        wrapper->last_width_position
          = wrapper->current_advance_position
          + bitmap_width;

        wrapper->last_char_in_line_index
          = 0;

        wrapper->last_word_end_advance_position
          -= wrapper->last_chars_in_line_advance_position;

        wrapper->last_word_end_width_position
          -= wrapper->last_chars_in_line_advance_position;

        wrapper->last_word_end_index
          -= chars_sent_count;

        printf("current_advance_position: %ld\n",
            wrapper->current_advance_position);
        */
      }
      else {
        // Otherwise, we'll do nothing and keep collecting chars until
        // we later find a word and or exceed the double line length.
      }
    }
    else {
      wrapper->last_width_position
        = wrapper->current_width_position;
    }

    if (current_char == Z_UCS_NEWLINE) {
      TRACE_LOG("Flushing on newline at current position.\n");
      // In case everything fits into the current line and the current
      // char is a newline, we can simply flush at the current position.
      flush_line(wrapper, wrapper->current_buffer_index - 1, false, false);

      wrapper->last_word_end_advance_position = 0;
      wrapper->last_word_end_width_position = 0;
      wrapper->current_advance_position = 0;
      wrapper->current_width_position = 0;
      wrapper->last_width_position = 0;
      wrapper->last_word_end_index = -1;
    }

    if ( (current_char == Z_UCS_SPACE) && (last_char != Z_UCS_SPACE) ) {
      TRACE_LOG("lweap-at-space: %ld\n",
          wrapper->last_word_end_advance_position);
      wrapper->last_word_end_advance_position
        = wrapper->current_advance_position;
      wrapper->last_word_end_width_position
        = wrapper->current_width_position;
      wrapper->last_word_end_index
        = wrapper->current_buffer_index - 1;
    }

    if ( (input != NULL) && (*input_index != 0) ) {
      input_index++;
      last_char = current_char;
    }
    else if (process_line_end == true) {
      process_line_end = false;
    }
  }
}


void end_current_line(true_type_wordwrapper *wrapper) {
  freetype_wrap_z_ucs(wrapper, NULL, true);
}


void freetype_wordwrap_flush_output(true_type_wordwrapper* wrapper) {
  if ( (wrapper->current_buffer_index > 0)
      || (wrapper->metadata_index > 0) ) {
    flush_line(wrapper, -1, false, false);
  }
}


void freetype_wordwrap_insert_metadata(true_type_wordwrapper *wrapper,
    void (*metadata_output)(void *ptr_parameter, uint32_t int_parameter),
    void *ptr_parameter, uint32_t int_parameter, true_type_font *new_font) {
  size_t bytes_to_allocate;

  TRACE_LOG("freetype_wordwrap_insert_metadata, font %p.\n", new_font);

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
  wrapper->metadata[wrapper->metadata_index].font
    = new_font;

  if (new_font != NULL) {
    set_font(wrapper, new_font);
  }

  TRACE_LOG("Added new metadata entry at %ld with int-parameter %ld, ptr:%p.\n",
      wrapper->current_buffer_index,
      (long int)int_parameter, ptr_parameter);

  wrapper->metadata_index++;
}


void freetype_wordwrap_adjust_line_length(true_type_wordwrapper *wrapper,
    size_t new_line_length) {
  TRACE_LOG("wordwrapper adjusted for new line length %d.\n",
      new_line_length);
  wrapper->line_length = new_line_length;
}

