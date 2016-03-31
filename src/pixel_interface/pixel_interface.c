
/* pixel_interface.c
 *
 * This file is part of fizmo.
 *
 * Copyright (c) 2011-2015 Christoph Ender.
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
 * ToDo: In case buffering is activated, current_wrapper_style and
 *       current_wrapper_font have to be set.
 *
 */

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

#include "tools/i18n.h"
#include "tools/tracelog.h"
#include "tools/types.h"
#include "tools/unused.h"
#include "tools/z_ucs.h"
#include "interpreter/cmd_hst.h"
#include "interpreter/config.h"
#include "interpreter/fizmo.h"
#include "interpreter/history.h"
#include "interpreter/streams.h"
#include "interpreter/text.h"
#include "interpreter/wordwrap.h"
#include "interpreter/zpu.h"
#include "interpreter/output.h"

#include "pixel_interface.h"
#include "true_type_wordwrapper.h"
#include "true_type_factory.h"
#include "true_type_font.h"
#include "../screen_interface/screen_pixel_interface.h"
#include "../locales/libpixelif_locales.h"

#define Z_STYLE_NONRESET (Z_STYLE_REVERSE_VIDEO | Z_STYLE_BOLD | Z_STYLE_ITALIC)

// 8.8.1
// The display is an array of pixels. Coordinates are usually given (in units)
// in the form (y,x), with (1,1) in the top left.

struct z_window {
  // Attributes as defined by Z-Machine-Spec:
  int ypos; // 0 is topmost position
  int xpos; // 0 is leftmost position
  int ysize;
  int xsize;
  int ycursorpos; // 0 is topmost position
  int xcursorpos; // 0 is leftmost position
  int last_gylphs_xcursorpos; // 0 is leftmost position
  int rightmost_filled_xpos;
  int lower_padding;
  int leftmargin;
  int rightmargin;
  uint32_t newline_routine;
  int interrupt_countdown;
  z_style text_style;
  z_colour foreground_colour;
  z_colour background_colour;
  z_font font_type;
  int font_size;
  int line_count;

  bool wrapping;
  bool scrolling_active;
  bool stream2copying_active;
  bool buffering;

  // Attributes for internal use:
  int window_number;
  int nof_consecutive_lines_output; // line counter for [more]
  int nof_lines_in_current_paragraph;

  z_colour output_foreground_colour;
  z_colour output_background_colour;
  z_style output_text_style;
  z_style output_font;
  true_type_font *output_true_type_font;

  true_type_wordwrapper *wordwrapper;
  z_style current_wrapper_style;
  z_font current_wrapper_font;
};

// Z-Spec 8.8.1: The display is an array of pixels. Coordinates are usually
// given (in units) in the form (y,x), with (1,1) in the top left.

static char *screen_pixel_interface_version = LIBPIXELINTERFACE_VERSION;
//static FT_Library ftlibrary;
static true_type_factory *font_factory = NULL;
static int screen_height_in_pixel = -1;
static int total_screen_width_in_pixel = -1;
static int screen_width_without_scrollbar = -1;
static int nof_active_z_windows = 0;
static int nof_total_z_windows = 0;
static int statusline_window_id = -1;
static int measurement_window_id = -1;
static int custom_left_margin = 8;
static int custom_right_margin = 8;
static int v3_status_bar_left_margin = 5;
static int v3_status_bar_right_margin = 5;
static int v3_status_bar_left_scoreturntime_margin= 5;
static bool hyphenation_enabled = true;
static bool using_colors = false;
static bool color_disabled = false;
static bool disable_more_prompt = false;
static z_ucs *libpixelif_more_prompt;
static z_ucs *libpixelif_score_string;
static z_ucs *libpixelif_turns_string;
static z_ucs space_string[] = { Z_UCS_SPACE, 0 };
//static int libpixelif_right_status_min_size;
static int active_z_window_id = -1;
//static true_type_wordwrapper *refresh_wordwrapper;
//static int refresh_newline_counter;
//static bool refresh_count_mode;
//static int refresh_lines_to_skip;
//static int refresh_lines_to_output;
static int last_split_window_size = 0;
static bool winch_found = false;
static bool interface_open = false;
/*
static FT_Face regular_face;
static FT_Face italic_face;
static FT_Face bold_face;
static FT_Face bold_italic_face;
static FT_Face current_face;
*/
static bool italic_font_available;
static bool bold_font_available;
static bool fixed_font_available;
static true_type_font *regular_font = NULL;
static true_type_font *italic_font = NULL;
static true_type_font *bold_font = NULL;
static true_type_font *bold_italic_font = NULL;
static true_type_font *fixed_regular_font = NULL;
static true_type_font *fixed_italic_font = NULL;
static true_type_font *fixed_bold_font = NULL;
static true_type_font *fixed_bold_italic_font = NULL;
static int line_height;
static int fixed_width_char_width = 8;
static true_type_wordwrapper *preloaded_wordwrapper;
static int scrollbar_width = 12;
static z_rgb_colour pixel_cursor_colour = Z_COLOUR_BLUE;

// Scrolling upwards:
// It is always assumed that there's no output to window[0] and it's
// corresponding outputhistory as long as upscrolling is active. As
// soon as any output is received or the window size is changed, upscrolling
// is terminated and the screen returns to the bottom of the output -- this
// also automatically shows the new output which has arrived, which should in
// most cases result from a interrupt routine.

static int top_upscroll_line = -1; // != -1 indicates upscroll is active, this
 // variable stores the topmost visible line on the screen (history-wise).
static bool upscroll_hit_top = false;
static history_output *history = NULL; // shared by upscroll and screen-refresh
static int redraw_pixel_lines_to_skip = 0; // will be skipped on top of redraw.
static int redraw_pixel_lines_to_draw = -1; // will be painted at most.

// line-measurement gets it's own history
static history_output *measurement_history = NULL;

static int history_screen_line; //, last_history_screen_line;

// This flag is set to true when an read_line is currently underway. It's
// used by screen refresh functions like "new_pixel_screen_size".
static bool input_line_on_screen = false;
static int nof_input_lines = 0;
static int input_index = 0;
static z_ucs *current_input_buffer = NULL;
static z_ucs newline_string[] = { '\n', 0 };

static bool timed_input_active;

static struct z_window **z_windows;
static struct z_screen_pixel_interface *screen_pixel_interface = NULL;

//static int *current_input_scroll_x, *current_input_index;
//static int *current_input_size;
static int *current_input_x, *current_input_y;
//static int *current_input_display_width;
static int nof_break_line_invocations = 0;

static char last_left_margin_config_value_as_string[MAX_MARGIN_AS_STRING_LEN];
static char last_right_margin_config_value_as_string[MAX_MARGIN_AS_STRING_LEN];
static char *regular_font_filename = NULL;
static char *italic_font_filename = NULL;
static char *bold_font_filename = NULL;
static char *bold_italic_font_filename = NULL;
static char *fixed_regular_font_filename = NULL;
static char *fixed_italic_font_filename = NULL;
static char *fixed_bold_font_filename = NULL;
static char *fixed_bold_italic_font_filename = NULL;
static char *font_search_path = FONT_DEFAULT_SEARCH_PATH;
static int font_height_in_pixel = 13;
static char last_font_size_config_value_as_string[MAX_VALUE_AS_STRING_LEN];
static long total_lines_in_history = 0;
static bool refresh_active = false; // When true, total_lines_in_history
  // is being updated during output.
static long total_nof_lines_stored = 0;

static bool history_is_being_remeasured = false;
// history-remeasurement means that the number of lines for paragraphs
// stored in the history has yet to be determined. This usually occures
// when the screen is resized and so the stored values are no longer
// valid. When this flag is set idle time during the get_next_event_wrapper
// function is used to refresh these values.

static char *config_option_names[] = {
  "left-margin", "right-margin", "disable-hyphenation", "disable-color",
  "regular-font", "italic-font", "bold-font", "bold-italic-font",
  "fixed-regular-font", "fixed-italic-font", "fixed-bold-font",
  "fixed-bold-italic-font", "font-search-path", "font-size",
  "cursor-color", NULL };


static int process_glyph_string(z_ucs *z_ucs_output, int window_number,
    true_type_font *font, bool *no_more_space);
static void wordwrap_output_style(void *window_number, uint32_t style_data);
static true_type_font *evaluate_font(z_style text_style, z_font font);
static history_output_target history_target;
static void z_ucs_output(z_ucs *z_ucs_output);


static void clear_to_eol(int window_number) {
  int width, yspace, height, clip_bottom;

  if (redraw_pixel_lines_to_skip > line_height) {
    return;
  }

  clip_bottom
    = (redraw_pixel_lines_to_draw >= 0)
    && (redraw_pixel_lines_to_draw < line_height)
    ? line_height - redraw_pixel_lines_to_draw
    : 0;

  // Clear the remaining space on the line. This also ensures that the
  // background color for the line will be the current output_color.

  width
    = z_windows[window_number]->xsize
    - z_windows[window_number]->rightmost_filled_xpos;

  yspace
    = z_windows[window_number]->ysize
    - z_windows[window_number]->lower_padding
    - z_windows[window_number]->ycursorpos;

  height
    = yspace < line_height - redraw_pixel_lines_to_skip - clip_bottom
    ? yspace
    : line_height - redraw_pixel_lines_to_skip - clip_bottom;

  TRACE_LOG("clear-to-eol: %d, %d, %d, %d, win %d.\n",
      z_windows[window_number]->xpos
      + z_windows[window_number]->rightmost_filled_xpos,
      z_windows[window_number]->ypos
      + z_windows[window_number]->ycursorpos,
      width,
      height,
      window_number);

  screen_pixel_interface->fill_area(
      (z_windows[window_number]->xpos
       + z_windows[window_number]->rightmost_filled_xpos),
      (z_windows[window_number]->ypos
       + z_windows[window_number]->ycursorpos),
      width,
      height,
      z_to_rgb_colour(z_windows[window_number]->output_background_colour));
}


static void flush_window(int window_number) {
  TRACE_LOG("flushing window %d\n", window_number);
  freetype_wordwrap_flush_output(z_windows[window_number]->wordwrapper);
  if (window_number != measurement_window_id) {
    clear_to_eol(window_number);
  }
}


static void history_has_to_be_remeasured() {
  history_is_being_remeasured = true;
  z_windows[measurement_window_id]->nof_consecutive_lines_output = 0;
}


static void refresh_scrollbar() {
  int bar_height, bar_position;

  TRACE_LOG("Refreshing scrollbar.\n");

  screen_pixel_interface->fill_area(
      screen_width_without_scrollbar,
      0,
      scrollbar_width,
      screen_height_in_pixel,
      0x00c0c0c0);

  if (history_is_being_remeasured == false) {

    bar_height
      = z_windows[0]->ysize
      / (double)(total_lines_in_history * line_height)
      * screen_height_in_pixel;

    // bar_height may be < 0 in case we've got less output than
    // screen size.
    if (bar_height > screen_height_in_pixel) {
      bar_height = screen_height_in_pixel;
    }

    TRACE_LOG("top_upscroll_line: %d.\n", top_upscroll_line)
    TRACE_LOG("z_windows[0]->ysize: %d, bar_height: %d.\n",
        z_windows[0]->ysize, bar_height);
    TRACE_LOG("total_lines_in_history: %d, line_height: %d.\n",
        total_lines_in_history, line_height);

    if (top_upscroll_line > 0) {
      bar_position
        = z_windows[0]->ysize
        * ( 1 - (top_upscroll_line
              / (((double)(total_lines_in_history * line_height) )
                + z_windows[0]->lower_padding) ) );
    }
    else {
      bar_position = (screen_height_in_pixel - bar_height);
    }
    TRACE_LOG("bar_position2: %d.\n", bar_position)

    TRACE_LOG("bar_height: %ld %d %d %d\n",
        total_lines_in_history,
        font_height_in_pixel,
        screen_height_in_pixel,
        bar_height);

    TRACE_LOG("bar-fill: %d %d %d %d\n",
        screen_width_without_scrollbar + 2,
        bar_position - bar_height,
        scrollbar_width - 4,
        bar_height);

    screen_pixel_interface->fill_area(
        screen_width_without_scrollbar + 2,
        bar_position,
        scrollbar_width - 4,
        bar_height,
        0x00404040);
  }

  TRACE_LOG("Scrollbar refreshed, %f%% full.\n",
      z_windows[0]->ysize
      / (double)(total_lines_in_history * line_height) * 100);
}


static void switch_to_window(int window_id) {
  TRACE_LOG("Switching to window %d.\n", window_id);

  active_z_window_id = window_id;
  //refresh_cursor(active_z_window_id);
}

static int init_history_remeasurement() {
  int last_active_z_window_id;

  TRACE_LOG("total_nof_lines_stored: %ld\n", total_nof_lines_stored);

  z_windows[measurement_window_id]->ypos
    = z_windows[0]->ypos;
  z_windows[measurement_window_id]->xpos
    = z_windows[0]->xpos;
  /*
     z_windows[measurement_window_id]->text_style
     = z_windows[0]->text_style;
     z_windows[measurement_window_id]->output_text_style
     = z_windows[0]->output_text_style;
     z_windows[measurement_window_id]->font_type
     = z_windows[0]->font_type;
     z_windows[measurement_window_id]->output_font
     = z_windows[0]->output_font;
     z_windows[measurement_window_id]->output_true_type_font
     = z_windows[0]->output_true_type_font;
     */
  z_windows[measurement_window_id]->nof_lines_in_current_paragraph
    = 0;
  z_windows[measurement_window_id]->ysize
    = z_windows[0]->ysize;
  z_windows[measurement_window_id]->xsize
    = z_windows[0]->xsize;
  z_windows[measurement_window_id]->scrolling_active
    = true;
  z_windows[measurement_window_id]->stream2copying_active
    = true;
  z_windows[measurement_window_id]->leftmargin
    = z_windows[0]->leftmargin;
  z_windows[measurement_window_id]->rightmargin
    = z_windows[0]->rightmargin;
  /*
     z_windows[measurement_window_id]->font_type
     = z_windows[0]->font_type;
     */
  z_windows[measurement_window_id]->lower_padding
    = z_windows[0]->lower_padding;
  z_windows[measurement_window_id]->newline_routine
    = 0;
  z_windows[measurement_window_id]->interrupt_countdown
    = 0;
  /*
     z_windows[measurement_window_id]->background_colour
     = z_windows[0]->background_colour;
     z_windows[measurement_window_id]->output_background_colour
     = z_windows[0]->output_background_colour;
     z_windows[measurement_window_id]->foreground_colour
     = z_windows[0]->foreground_colour;
     z_windows[measurement_window_id]->output_foreground_colour
     = z_windows[0]->output_foreground_colour;
     */
  z_windows[measurement_window_id]->line_count
    = 0;
  z_windows[measurement_window_id]->wrapping
    = true;
  z_windows[measurement_window_id]->buffering
    = true;
  z_windows[measurement_window_id]->current_wrapper_style
    = z_windows[measurement_window_id]->text_style;
  z_windows[measurement_window_id]->current_wrapper_font
    = z_windows[measurement_window_id]->font_type;

  z_windows[measurement_window_id]->xcursorpos
    = z_windows[measurement_window_id]->leftmargin;
  z_windows[measurement_window_id]->rightmost_filled_xpos
    = z_windows[measurement_window_id]->xcursorpos;
  z_windows[measurement_window_id]->last_gylphs_xcursorpos
    = -1;

  TRACE_LOG("adj measurement-wrap: %d, %d, %d\n",
      z_windows[measurement_window_id]->xsize,
      z_windows[measurement_window_id]->rightmargin,
      z_windows[measurement_window_id]->leftmargin);

  freetype_wordwrap_adjust_line_length(
      z_windows[measurement_window_id]->wordwrapper,
      z_windows[measurement_window_id]->xsize
      - z_windows[measurement_window_id]->rightmargin
      - z_windows[measurement_window_id]->leftmargin);

  last_active_z_window_id = active_z_window_id;
  switch_to_window(measurement_window_id);
  disable_more_prompt = true;

  if (history != NULL) {
    TRACE_LOG("History set, must not happen.\n");
    return -1;
  }

  TRACE_LOG("creating output history for re-measurement.\n");

  if ((measurement_history = init_history_output(
          outputhistory[0],
          &history_target,
          Z_HISTORY_OUTPUT_FROM_BUFFERBACK
          + Z_HISTORY_OUTPUT_WITHOUT_VALIDATION)) == NULL) {
    TRACE_LOG("Could not create history.\n");
  }

  return last_active_z_window_id;
}


static void remeasure_next_paragraph() {
  int return_code, last_lines_in_history, lines_in_paragraph;

  last_lines_in_history
    = z_windows[measurement_window_id]->nof_consecutive_lines_output;

  return_code
    = output_repeat_paragraphs(measurement_history, 1, true, true);
  if (return_code >= 0) {
    z_ucs_output(newline_string);
  }
  flush_window(measurement_window_id);

  lines_in_paragraph
    = z_windows[measurement_window_id]->nof_consecutive_lines_output
    - last_lines_in_history;

  alter_last_read_paragraph_attributes(
      measurement_history,
      lines_in_paragraph,
      z_windows[0]->xsize);

  TRACE_LOG("Remeasured paragraph had %d lines.\n", lines_in_paragraph);

  if (return_code < 0) {
    // Finished remeasuring.
    TRACE_LOG("output_repeat_paragraphs returned < 0.\n");
    flush_window(measurement_window_id);
    history_is_being_remeasured = false;
    total_lines_in_history
      = z_windows[measurement_window_id]->nof_consecutive_lines_output - 1;
    //printf("remeasure: total_lines_in_history: %ld.\n",
    //    total_lines_in_history);
    TRACE_LOG("Finished remeasuring, %d lines in history.\n",
        total_lines_in_history);
  }
  else {
    // Not finished, process next paragraph.
    //printf("nof_consecutive_lines_output: %d.\n",
    //    z_windows[measurement_window_id]->nof_consecutive_lines_output);
  }
}


static void end_history_remeasurement(int last_active_z_window_id) {
  destroy_history_output(measurement_history);
  measurement_history = NULL;
  disable_more_prompt = false;
  switch_to_window(last_active_z_window_id);

  TRACE_LOG("Consecutive measurement lines: %d.\n",
      z_windows[measurement_window_id]->nof_consecutive_lines_output - 1);
}


static int get_next_event_wrapper(z_ucs *input, int timeout_millis) {
  int event_type = EVENT_WAS_NOTHING, last_active_z_window_id;

  TRACE_LOG("get_next_event_wrapper, history_is_being_remeasured: %d.\n",
      history_is_being_remeasured);

  if (history_is_being_remeasured == true) {

    last_active_z_window_id = init_history_remeasurement();

    do {
      remeasure_next_paragraph();
      TRACE_LOG("Polling for next event.\n");
      event_type = screen_pixel_interface->get_next_event(
          input, timeout_millis, true);
      TRACE_LOG("event_type: %d\n", event_type);
    }
    while ( (history_is_being_remeasured == true)
        && (event_type == EVENT_WAS_NOTHING) );

    end_history_remeasurement(last_active_z_window_id);

    TRACE_LOG("total_lines_in_history recalc: %ld.\n", total_lines_in_history);
    refresh_scrollbar();
    screen_pixel_interface->update_screen();

    if (event_type != EVENT_WAS_NOTHING) {
      return event_type;
    }
  }

  TRACE_LOG("Waiting for next event.\n");

  return screen_pixel_interface->get_next_event(input, timeout_millis, false);
}


static void reset_xcursorpos(int window_id) {
  TRACE_LOG("Resetting xcursorpos for window %d.\n", window_id);
  z_windows[window_id]->xcursorpos = 0;
  z_windows[window_id]->last_gylphs_xcursorpos = -1;
  z_windows[window_id]->rightmost_filled_xpos
    = z_windows[window_id]->xcursorpos;
}


// Will return true in case break_line was successful and the cursor is now
// on a fresh line, false otherwise (if, for example, scrolling is disabled
// and the cursor is on the bottom-right position of the window).
static bool break_line(int window_number) {
  z_ucs input;
  int event_type;
  int yspace_in_pixels, nof_lines_to_scroll, i, fill_ypos, fill_height;

  //printf("Breaking line.\n");
  TRACE_LOG("Breaking line for window %d.\n", window_number);
  TRACE_LOG("breakline-redraw_pixel_lines_to_skip_ %d.\n",
      redraw_pixel_lines_to_skip);

  if (window_number != measurement_window_id) {
    clear_to_eol(window_number);

    // We'll calculate the space below current line.
    TRACE_LOG("ycursorpos: %d.\n", z_windows[window_number]->ycursorpos);
    TRACE_LOG("line_height: %d.\n", line_height);
    TRACE_LOG("lower_padding: %d.\n", z_windows[window_number]->lower_padding);
    TRACE_LOG("ysize: %d.\n", z_windows[window_number]->ysize);
    yspace_in_pixels
      = z_windows[window_number]->ysize
      - z_windows[window_number]->lower_padding
      - z_windows[window_number]->ycursorpos
      - line_height;
  }
  else {
    yspace_in_pixels = 0;
  }
  TRACE_LOG("yspace_in_pixels: %d\n", yspace_in_pixels);

  // 8.6.2 The upper window should never be scrolled: it is legal for
  // character to be printed on the bottom right position of the upper
  // window (but the position of the cursor after this operation is
  // undefined: the author suggests that it stay put).
  if ( (window_number == 1)
      || (z_windows[window_number]->scrolling_active == false) ) {
    /*
    printf("yspace_in_pixels:%d, line_height: %d.\n",
        yspace_in_pixels, line_height);
    */
    if (yspace_in_pixels >= line_height) {
      z_windows[window_number]->ycursorpos += line_height;
      reset_xcursorpos(window_number);
    }
    else {
      // Not enough space for a new line and scrolling is disabled (via flag)
      // or not possible (in window 1).
      TRACE_LOG("Not breaking scrolling-inactive line, returning.\n");
      //printf("Not breaking scrolling-inactive line, returning.\n");
      return false;
    }
  }
  else {
    // We're not in window 1 and are allowed to scroll.
    reset_xcursorpos(window_number);

    if (window_number != measurement_window_id) {
      // Only adjust y in case we're not measuring.

      if ((redraw_pixel_lines_to_draw == -1)
          && (yspace_in_pixels < line_height)) {
        // We know we never have to scroll in redraw_mode, so we can skip
        // this entire section when redrawing (which we determine by testing
        // redraw_pixel_lines_to_draw == -1).
        nof_lines_to_scroll
          = z_windows[window_number]->ycursorpos
          + line_height
          - (line_height - yspace_in_pixels);
        TRACE_LOG("nof_lines_to_scroll: %d.\n", nof_lines_to_scroll);

        /*
        printf("copy-area: %d/%d to %d/%d, %d x %d\n",
            z_windows[window_number]->ypos + yspace_in_pixels,
            z_windows[window_number]->xpos,
            z_windows[window_number]->ypos,
            z_windows[window_number]->xpos,
            z_windows[window_number]->xsize,
            z_windows[window_number]->ysize - yspace_in_pixels
            - z_windows[window_number]->lower_padding);
        */

        TRACE_LOG("copy_area: from %d/%d size %d*%d to %d/%d.\n",
            z_windows[window_number]->xpos,
            z_windows[window_number]->ypos + (line_height - yspace_in_pixels),
            z_windows[window_number]->xsize,
            nof_lines_to_scroll,
            z_windows[window_number]->xpos,
            z_windows[window_number]->ypos);

        screen_pixel_interface->copy_area(
            z_windows[window_number]->ypos,
            z_windows[window_number]->xpos,
            z_windows[window_number]->ypos + (line_height - yspace_in_pixels),
            z_windows[window_number]->xpos,
            nof_lines_to_scroll,
            z_windows[window_number]->xsize);

        z_windows[window_number]->ycursorpos
          = z_windows[window_number]->ysize
          - line_height
          - z_windows[window_number]->lower_padding;
      }
      else {
        TRACE_LOG(
            "breaking line, moving cursor down, no scrolling necessary.\n");
        if (redraw_pixel_lines_to_skip > 0) {
          // redraw_pixel_lines_to_skip cannot be greather than line_height,
          // since we're catching this case before break_line is invoked.
          z_windows[window_number]->ycursorpos
            += (line_height - redraw_pixel_lines_to_skip);
          redraw_pixel_lines_to_draw
            -= (line_height - redraw_pixel_lines_to_skip);
          redraw_pixel_lines_to_skip = 0;
        }
        else {
          z_windows[window_number]->ycursorpos += line_height;
          if (redraw_pixel_lines_to_draw != -1) {
           if ((redraw_pixel_lines_to_draw -= line_height) < 0) {
             redraw_pixel_lines_to_draw = 0;
           }
          }
        }
      }
    }
    TRACE_LOG("new y-cursorpos: %d\n", z_windows[window_number]->ycursorpos);
  }
  //printf("redraw_pixel_lines_to_draw: %d.\n", redraw_pixel_lines_to_draw);

  z_windows[window_number]->last_gylphs_xcursorpos = -1;
  z_windows[window_number]->rightmost_filled_xpos
    = z_windows[window_number]->xcursorpos;

  nof_break_line_invocations++;
  z_windows[window_number]->nof_consecutive_lines_output++;
  /*
  printf("Increasing noflicp from %d to %d.\n",
      z_windows[window_number]->nof_lines_in_current_paragraph,
      z_windows[window_number]->nof_lines_in_current_paragraph + 1);
  */
  z_windows[window_number]->nof_lines_in_current_paragraph++;
  if ( (input_line_on_screen == false)
         && (refresh_active == false)
         && (active_z_window_id == 0) ) {
    total_lines_in_history++;
    TRACE_LOG("total_lines_in_history: %ld.\n", total_lines_in_history);
    //printf("total_lines_in_history: %ld.\n", total_lines_in_history);
  }

  if (window_number != measurement_window_id) {
    TRACE_LOG("consecutive lines: %d.\n",
        z_windows[window_number]->nof_consecutive_lines_output);
    TRACE_LOG("lines in current paragraph: %d.\n",
        z_windows[window_number]->nof_lines_in_current_paragraph);
    // FIXME: Implement height 255
    if ( (z_windows[window_number]->nof_consecutive_lines_output
          >= (screen_height_in_pixel / line_height) - 1)
        && (disable_more_prompt == false)
        && (winch_found == false)) {
      TRACE_LOG("Displaying more prompt.\n");
      //printf("Displaying more prompt.\n");

      // Loop below will result in recursive "z_ucs_output_window_target"
      // call. Dangerous?
      for (i=0; i<nof_active_z_windows; i++) {
        if ( (i != window_number)
            && (bool_equal(z_windows[i]->buffering, true))) {
          flush_window(i);
        }
      }

      //screen_pixel_interface->z_ucs_output(libpixelif_more_prompt);
      process_glyph_string(
          libpixelif_more_prompt,
          window_number,
          z_windows[window_number]->output_true_type_font,
          NULL);
      clear_to_eol(0);

      // In case we're just starting the story and there's no status line yet:
      if (ver <= 3) {
        display_status_line();
      }

      refresh_scrollbar();
      screen_pixel_interface->update_screen();
      //refresh_cursor(window_number);

      // FIXME: Check for sound interrupt?
      do {
        event_type = get_next_event_wrapper(&input, 0);
        if (event_type == EVENT_WAS_TIMEOUT) {
          TRACE_LOG("timeout.\n");
        }
      }
      while (event_type == EVENT_WAS_TIMEOUT);

      reset_xcursorpos(window_number);
      //refresh_cursor(window_number);
      //screen_pixel_interface->clear_to_eol();
      screen_pixel_interface->fill_area(
          z_windows[window_number]->xpos,
          z_windows[window_number]->ypos
          + z_windows[window_number]->ysize - line_height,
          z_windows[window_number]->xsize,
          line_height,
          z_to_rgb_colour(z_windows[window_number]->output_background_colour));

      if (event_type == EVENT_WAS_WINCH) {
        winch_found = true;
        // The more prompt was "interrupted" by a window screen size change.
      }

      z_windows[window_number]->nof_consecutive_lines_output = 0;
      TRACE_LOG("more prompt finished: %d.\n", event_type);
    }

    if ( (fill_ypos = z_windows[window_number]->ypos
          + z_windows[window_number]->ycursorpos)
        <= z_windows[window_number]->ypos + z_windows[window_number]->ysize) {

      TRACE_LOG("fill_ypos: %d.\n", fill_ypos);
      TRACE_LOG("ypos + ycursorpos: %d.\n", z_windows[window_number]->ypos
          + z_windows[window_number]->ycursorpos);
      TRACE_LOG("ypos + ysize: %d.\n", z_windows[window_number]->ypos
          + z_windows[window_number]->ysize);

      fill_height = line_height;

      if (fill_ypos + fill_height
          >= z_windows[window_number]->ypos + z_windows[window_number]->ysize) {
        fill_height
          = z_windows[window_number]->ypos + z_windows[window_number]->ysize
          - fill_ypos;
      }

      // Start new line: Fill left margin with background colour.
      screen_pixel_interface->fill_area(
          z_windows[window_number]->xpos,
          fill_ypos,
          z_windows[window_number]->leftmargin,
          fill_height,
          z_to_rgb_colour(
            z_windows[window_number]->output_background_colour));
    }
  }

  return true;
}


static int get_glyph_string_size(z_ucs *string_to_measure,
    true_type_font *font) {
  // Measuring string width works by adding up all "advance" values except
  // for the last char, which has it's bitmap_width added instead.
  int advance = 0;
  int bitmap_width = 0;
  int result = 0;

  while (*string_to_measure != 0) {
    // We've found the next char to measure, so we know we have to add
    // the last char's advance value.
    result += advance;

    // Measure current glyph.
    tt_get_glyph_size(font, *string_to_measure, &advance, &bitmap_width);

    // Advance to next position.
    string_to_measure++;
  }

  // At the end of the string, we're adding the last char's bitmap_width.
  result += bitmap_width;

  return result;
}


// Returns the number of new lines printed, will set *no_more_space to
// true in case no_more_space!=NULL and no more output can be displayed (e.g.
// if scrolling_active is false and the cursor is on the window's
// bottom-right position).
static int process_glyph(z_ucs charcode, int window_number,
    true_type_font *font, bool *no_more_space) {
  int x, y, x_max, advance, bitmap_width, result = 0;
  bool reverse = false;
  z_rgb_colour foreground_colour, background_colour;

  // redraw_pixel_lines_to_draw will be 0 or greater in case of
  // redraws, -1 during normal operation.
  if (redraw_pixel_lines_to_draw == 0) {
    if (charcode == Z_UCS_NEWLINE) {
      z_windows[window_number]->nof_consecutive_lines_output++;
      return 1;
    }
    return 0;
  }

  if (redraw_pixel_lines_to_skip >= line_height) {
    if (charcode == Z_UCS_NEWLINE) {
      redraw_pixel_lines_to_skip -= line_height;
      z_windows[window_number]->nof_consecutive_lines_output++;
      return 1;
    }
    return 0;
  }

  if (charcode == Z_UCS_NEWLINE) {
    if (break_line(window_number) == false) {
      if (no_more_space != NULL)
        *no_more_space = true;
    }
    return 1;
  }

  /*
  if (bool_equal(z_windows[window_number]->buffering, true)) {
  }
  else {
    if (z_windows[window_number]->text_style & Z_STYLE_REVERSE_VIDEO)
      reverse = true;
  }
  */

  if (z_windows[window_number]->output_text_style & Z_STYLE_REVERSE_VIDEO)
    reverse = true;

  TRACE_LOG("reverse[%d] = %d.\n", window_number, reverse);

  //advance = tt_get_glyph_advance(font, charcode, 0);
  tt_get_glyph_size(font, charcode, &advance, &bitmap_width);

  // We're getting two separate values here: "bitmap_width" denotes the actual
  // size of the glyph (so we can know if it still fits on the current line),
  // "advance" tells how far the cursor should be advanced. Both values
  // don't have to be the same, for some glyphs and especialls for italic
  // fonts the cursor may stay inside the glyph.

  TRACE_LOG("Max draw glyph size: %d.\n",
      z_windows[window_number]->xsize - z_windows[window_number]->rightmargin);

  TRACE_LOG(
      "leftmargin:%d, xcursorpos:%d, advance:%d, xsize:%d, rightmargin:%d\n",
      z_windows[window_number]->leftmargin,
      z_windows[window_number]->xcursorpos,
      advance,
      z_windows[window_number]->xsize,
      z_windows[window_number]->rightmargin);

  /*
  printf(
      "%c leftmargin:%d, xcursorpos:%d, advance:%d, xsize:%d, rightmargin:%d\n",
      charcode,
      z_windows[window_number]->leftmargin,
      z_windows[window_number]->xcursorpos,
      advance,
      z_windows[window_number]->xsize,
      z_windows[window_number]->rightmargin);
  */

  TRACE_LOG(
      "ycursorpos:%d, ysize:%d\n",
      z_windows[window_number]->ycursorpos,
      z_windows[window_number]->ysize);

  if (z_windows[window_number]->leftmargin
      + z_windows[window_number]->xcursorpos
      + bitmap_width
      > z_windows[window_number]->xsize
      - z_windows[window_number]->rightmargin) {
    TRACE_LOG("Breaking line at %d.\n", z_windows[window_number]->xcursorpos);
    if (break_line(window_number) == false) {
      if (no_more_space != NULL) {
        *no_more_space = true;
      }
      return 0;
    }
    result = 1;
  }

  if (window_number != measurement_window_id) {
    x = z_windows[window_number]->xpos
      + z_windows[window_number]->leftmargin
      + z_windows[window_number]->xcursorpos;

    x_max
      = z_windows[window_number]->xsize
      - z_windows[window_number]->rightmargin
      - 1;

    y = z_windows[window_number]->ypos
      + z_windows[window_number]->ycursorpos;

    TRACE_LOG("Drawing glyph %c / %d at %d, %d.\n",
        charcode, charcode, x, y);

    foreground_colour = z_to_rgb_colour(
        z_windows[window_number]->output_foreground_colour);

    background_colour = z_to_rgb_colour(
        z_windows[window_number]->output_background_colour);

    TRACE_LOG("dg: %d: %d,%d\n", window_number,
        z_windows[window_number]->output_background_colour,
        z_windows[window_number]->output_foreground_colour);
    TRACE_LOG("ypos: %d, ysize: %d, x: %d, y: %d.\n",
        z_windows[window_number]->ypos,
        z_windows[window_number]->ysize,
        x,
        y);

    z_windows[window_number]->rightmost_filled_xpos
      = tt_draw_glyph(
          font,
          x,
          y,
          x_max,
          redraw_pixel_lines_to_skip,
          ( (redraw_pixel_lines_to_draw >= 0)
            && (redraw_pixel_lines_to_draw < line_height)
            ? line_height - redraw_pixel_lines_to_draw
            : 0),
          reverse == false ? foreground_colour : background_colour,
          reverse == false ? background_colour : foreground_colour,
          screen_pixel_interface,
          charcode,
          &z_windows[window_number]->last_gylphs_xcursorpos);
  }

  z_windows[window_number]->xcursorpos
    += advance;

  //printf("advance: %d, xpos: %d, char: %c\n", advance, z_windows[window_number]->xcursorpos, charcode);

  return result;
}


// Returns the number of new lines printed, will set *no_more_space to
// true in case no_more_space!=NULL and no more output can be displayed (e.g.
// if scrolling_active is false and the cursor is on the window's
// bottom-right position).
static int process_glyph_string(z_ucs *z_ucs_output, int window_number,
    true_type_font *font, bool *no_more_space) {
  bool my_no_more_space = false;
  int result = 0;

  TRACE_LOG("Processing glyph string: \"");
  TRACE_LOG_Z_UCS(z_ucs_output);
  TRACE_LOG("\" for window %d.\n", window_number);

  while ((*z_ucs_output) && (my_no_more_space == false)) {
    result += process_glyph(
        *z_ucs_output,
        window_number,
        font,
        &my_no_more_space);
    z_ucs_output++;
  }

  if ((my_no_more_space == true) && (no_more_space != NULL)) {
    *no_more_space = true;
  }

  return result;
}


static void wordwrap_output_colour(void *window_number, uint32_t color_data) {
  int window_id = *((int*)window_number);

  TRACE_LOG("wordwrap-color: %d, %d.\n",
      (z_colour)color_data & 0xff,
      (z_colour)(color_data >> 16));

  z_windows[window_id]->output_foreground_colour
    = (z_colour)color_data & 0xff;

  z_windows[window_id]->output_background_colour
    = (z_colour)(color_data >> 16);
}


static true_type_font *evaluate_font(z_style text_style, z_font font) {
  true_type_font *result;
  TRACE_LOG("evaluate-font: style %d, font %d.\n", text_style, font);

  if ( (font == Z_FONT_COURIER_FIXED_PITCH)
      || (text_style & Z_STYLE_FIXED_PITCH) ) {
    if (text_style & Z_STYLE_BOLD) {
      if (text_style & Z_STYLE_ITALIC) {
        result = fixed_bold_italic_font;
        TRACE_LOG("evaluted font: fixed_bold_italic_font\n");
      }
      else {
        result = fixed_bold_font;
        TRACE_LOG("evaluted font: fixed_bold_font\n");
      }
    }
    else if (text_style & Z_STYLE_ITALIC) {
      result = fixed_italic_font;
      TRACE_LOG("evaluted font: fixed_italic_font\n");
    }
    else {
      result = fixed_regular_font;
      TRACE_LOG("evaluted font: fixed_regular_font\n");
    }
  }
  else {
    if (text_style & Z_STYLE_BOLD) {
      if (text_style & Z_STYLE_ITALIC) {
        result = bold_italic_font;
        TRACE_LOG("evaluted font: bold_italic_font\n");
      }
      else {
        result = bold_font;
        TRACE_LOG("evaluted font: bold_font\n");
      }
    }
    else if (text_style & Z_STYLE_ITALIC) {
      result = italic_font;
      TRACE_LOG("evaluted font: italic_font\n");
    }
    else {
      result = regular_font;
      TRACE_LOG("evaluted font: regular_font\n");
    }
  }

  return result;
}


static void update_window_true_type_font(int window_id) {
  z_windows[window_id]->output_true_type_font = evaluate_font(
      z_windows[window_id]->output_text_style,
      z_windows[window_id]->output_font);
}


static void wordwrap_output_style(void *window_number, uint32_t style_data) {
  int window_id = *((int*)window_number);

  TRACE_LOG("wordwrap-style:%d.\n", style_data);
  TRACE_LOG("window: %d.\n", window_id);

  if (style_data & Z_STYLE_NONRESET)
    z_windows[window_id]->output_text_style |= style_data;
  else
    z_windows[window_id]->output_text_style = style_data;
  TRACE_LOG("Resulting wordwrap-style:%d.\n",
      z_windows[window_id]->output_text_style);
  update_window_true_type_font(window_id);
}


static void wordwrap_output_font(void *window_number, uint32_t font_data) {
  int window_id = *((int*)window_number);

  TRACE_LOG("wordwrap-font:%d.\n", font_data);
  TRACE_LOG("window: %d.\n", window_id);

  z_windows[window_id]->output_font = font_data;
  update_window_true_type_font(window_id);
}


static void flush_all_buffered_windows() {
  int i;

  for (i=0; i<nof_active_z_windows; i++)
    if (bool_equal(z_windows[i]->buffering, true))
      flush_window(i);
}


void z_ucs_output_window_target(z_ucs *z_ucs_output,
    void *window_number_as_void) {
  int window_number = *((int*)window_number_as_void);

  TRACE_LOG("drawing glyph string: \"");
  TRACE_LOG_Z_UCS(z_ucs_output);
  TRACE_LOG("\".\n");
  process_glyph_string(
      z_ucs_output,
      window_number,
      z_windows[window_number]->output_true_type_font,
      NULL);
}


static char* get_interface_name() {
  return screen_pixel_interface->get_interface_name();
}


static bool is_status_line_available() {
  return true;
}


static bool is_split_screen_available() {
  return true;
}


static bool is_variable_pitch_font_default() {
  return true;
}


static bool is_colour_available() {
  return using_colors;
}


static bool is_picture_displaying_available() {
  return false;
}


static bool is_bold_face_available() {
  return bold_font_available;
}


static bool is_italic_available() {
  return italic_font_available;
}


static bool is_fixed_space_font_available() {
  return fixed_font_available;
}


static bool is_timed_keyboard_input_available() {
  return screen_pixel_interface->is_input_timeout_available();
}


static bool is_preloaded_input_available() {
  return true;
}


static bool is_character_graphics_font_availiable() {
  return true;
}


static bool is_picture_font_availiable() {
  return false;
}


static uint8_t get_screen_height_in_lines() {
  /*
  printf("height: %d\n", screen_height_in_pixel / line_height);
  if (screen_height_in_pixel < 0)
    exit(-1);
  else
    */
  return screen_height_in_pixel / line_height;
}


static uint8_t get_screen_width_in_characters() {
  /*
  printf("width in chars (%d, %d): %d\n",
      custom_left_margin, custom_right_margin,
      (screen_width_in_pixel - custom_left_margin - custom_right_margin)
      / fixed_width_char_width);
  */
  return screen_width_without_scrollbar / fixed_width_char_width;
}


static uint8_t get_screen_height_in_units() {
  //return screen_height_in_pixel;
  return get_screen_height_in_lines();
}


static uint8_t get_screen_width_in_units() {
  //return screen_width_in_pixel;
  return get_screen_width_in_characters();
}


static uint8_t get_font_width_in_units() {
  return 1;
}


static uint8_t get_font_height_in_units() {
  return 1;
}


static z_colour get_default_foreground_colour() {
  return screen_pixel_interface->get_default_foreground_colour();
}


static z_colour get_default_background_colour() {
  return screen_pixel_interface->get_default_background_colour();
}


static uint8_t get_total_width_in_pixels_of_text_sent_to_output_stream_3() {
  return 0;
}


static int parse_config_parameter(char *key, char *value) {
  long long_value;
  char *endptr;
  short color_code;

  TRACE_LOG("pixel-if parsing config param key \"%s\", value \"%s\".\n",
      key, value != NULL ? value : "(null)");

  if ( (strcasecmp(key, "left-margin") == 0)
      || (strcasecmp(key, "right-margin") == 0)) {
    if ( (value == NULL) || (strlen(value) == 0) )
      return -1;
    long_value = strtol(value, &endptr, 10);
    free(value);
    if (*endptr != 0)
      return -1;
    if (strcasecmp(key, "left-margin") == 0)
      set_custom_left_pixel_margin(long_value);
    else
      set_custom_right_pixel_margin(long_value);
    return 0;
  }
  else if (strcasecmp(key, "disable-hyphenation") == 0) {
    if ( (value == NULL)
        || (*value == 0)
        || (strcmp(value, config_true_value) == 0))
      hyphenation_enabled = false;
    else
      hyphenation_enabled = true;
    free(value);
    return 0;
  }
  else if (strcasecmp(key, "disable-color") == 0) {
    if ( (value == NULL)
        || (*value == 0)
        || (strcmp(value, config_true_value) == 0))
      color_disabled = true;
    else
      color_disabled = false;
    free(value);
    return 0;
  }
  else if (strcasecmp(key, "enable-color") == 0) {
    if ( (value == NULL)
        || (*value == 0)
        || (strcmp(value, config_true_value) == 0))
      color_disabled = false;
    else
      color_disabled = true;
    free(value);
    return 0;
  }
  else if (strcasecmp(key, "regular-font") == 0) {
    if (regular_font_filename != NULL)
      free(regular_font_filename);
    regular_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "italic-font") == 0) {
    if (italic_font_filename != NULL)
      free(italic_font_filename);
    italic_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "bold-font") == 0) {
    if (bold_font_filename != NULL)
      free(bold_font_filename);
    bold_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "bold-italic-font") == 0) {
    if (bold_italic_font_filename != NULL)
      free(bold_italic_font_filename);
    bold_italic_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "fixed-regular-font") == 0) {
    if (fixed_regular_font_filename != NULL)
      free(fixed_regular_font_filename);
    fixed_regular_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "fixed-italic-font") == 0) {
    if (fixed_italic_font_filename != NULL)
      free(fixed_italic_font_filename);
    fixed_italic_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "fixed-bold-font") == 0) {
    if (fixed_bold_font_filename != NULL)
      free(fixed_bold_font_filename);
    fixed_bold_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "fixed-bold-italic-font") == 0) {
    if (fixed_bold_italic_font_filename != NULL)
      free(fixed_bold_italic_font_filename);
    fixed_bold_italic_font_filename = value;
    return 0;
  }
  else if (strcasecmp(key, "font-search-path") == 0) {
    if (font_search_path != NULL)
      free(font_search_path);
    font_search_path = value;
    return 0;
  }
  else if (strcasecmp(key, "font-size") == 0) {
    if ( (value == NULL) || (strlen(value) == 0) )
      return -1;
    long_value = strtol(value, &endptr, 10);
    free(value);
    if (*endptr != 0)
      return -1;
    font_height_in_pixel = long_value;
    return 0;
  }
  else if (strcasecmp(key, "cursor-color") == 0) {
    if (value == NULL)
      return -1;
    color_code = color_name_to_z_colour(value);
    free(value);
    if (color_code == -1)
      return -1;
    pixel_cursor_colour = color_code;
    return 0;
  }
  else {
    return screen_pixel_interface->parse_config_parameter(key, value);
  }
}


static char *get_config_value(char *key)
{
  if (strcasecmp(key, "left-margin") == 0) {
    snprintf(last_left_margin_config_value_as_string, MAX_MARGIN_AS_STRING_LEN,
        "%d", custom_left_margin);
    return last_left_margin_config_value_as_string;
  }
  else if (strcasecmp(key, "right-margin") == 0) {
    snprintf(last_right_margin_config_value_as_string, MAX_MARGIN_AS_STRING_LEN,
        "%d", custom_right_margin);
    return last_right_margin_config_value_as_string;
  }
  else if (strcasecmp(key, "disable-hyphenation") == 0) {
    return hyphenation_enabled == false
      ? config_true_value
      : config_false_value;
  }
  else if (strcasecmp(key, "disable-color") == 0) {
    return color_disabled == true
      ? config_true_value
      : config_false_value;
  }
  else if (strcasecmp(key, "enable-color") == 0) {
    return color_disabled == false
      ? config_true_value
      : config_false_value;
  }
  else if (strcasecmp(key, "regular-font") == 0) {
    return regular_font_filename;
  }
  else if (strcasecmp(key, "italic-font") == 0) {
    return italic_font_filename;
  }
  else if (strcasecmp(key, "bold-font") == 0) {
    return bold_font_filename;
  }
  else if (strcasecmp(key, "bold-italic-font") == 0) {
    return bold_italic_font_filename;
  }
  else if (strcasecmp(key, "fixed-regular-font") == 0) {
    return fixed_regular_font_filename;
  }
  else if (strcasecmp(key, "fixed-italic-font") == 0) {
    return fixed_italic_font_filename;
  }
  else if (strcasecmp(key, "fixed-bold-font") == 0) {
    return fixed_bold_font_filename;
  }
  else if (strcasecmp(key, "fixed-bold-italic-font") == 0) {
    return fixed_bold_italic_font_filename;
  }
  else if (strcasecmp(key, "font-search-path") == 0) {
    return font_search_path;
  }
  else if (strcasecmp(key, "font-size") == 0) {
    snprintf(last_font_size_config_value_as_string, MAX_VALUE_AS_STRING_LEN,
        "%d", font_height_in_pixel);
    return last_font_size_config_value_as_string;
  }
  else if (strcasecmp(key, "cursor-color") == 0) {
    return z_colour_names[pixel_cursor_colour];
  }
  else {
    return screen_pixel_interface->get_config_value(key);
  }
}


static char **get_config_option_names() {
  return config_option_names;
}


static void z_ucs_output(z_ucs *z_ucs_output) {
  /*
  if ( (active_z_window_id == 0) && (z_ucs_len(z_ucs_output) > 0) ) {
    printf("Output: \"");
    for (int i=0; i<z_ucs_len(z_ucs_output); i++) {
      printf("%c", z_ucs_output[i]);
    }
    printf("\".\n");
  }
  */

  TRACE_LOG("Output: \"");
  TRACE_LOG_Z_UCS(z_ucs_output);
  TRACE_LOG("\" to window %d, buffering: %d.\n",
      active_z_window_id,
      active_z_window_id != -1 ? z_windows[active_z_window_id]->buffering : -1);

  if ( (z_ucs_output != NULL) && (*z_ucs_output != 0) ) {
    if (interface_open != true) {
      screen_pixel_interface->console_output(z_ucs_output);
    }
    else {
      if (bool_equal(z_windows[active_z_window_id]->buffering, false)) {
        z_ucs_output_window_target(
            z_ucs_output,
            (void*)(&z_windows[active_z_window_id]->window_number));
      }
      else {
        freetype_wrap_z_ucs(
          z_windows[active_z_window_id]->wordwrapper, z_ucs_output);
      }
    }
    TRACE_LOG("z_ucs_output finished.\n");
  }
}


static void update_fixed_width_char_width() {
  int bitmap_width;

  tt_get_glyph_size(
      fixed_regular_font, '0', &fixed_width_char_width, &bitmap_width);

  //printf("fixed_width_char_width: %d\n", fixed_width_char_width);
}


static void erase_window(int16_t window_number) {
  if ( (window_number >= 0)
      && (window_number <=
        nof_active_z_windows - (statusline_window_id >= 0 ? 1 : 0))) {
    // Erasing window -1 clears the whole screen to the background colour of
    // the lower screen, collapses the upper window to height 0, moves the
    // cursor of the lower screen to bottom left (in Version 4) or top left
    // (in Versions 5 and later) and selects the lower screen. The same
    // operation should happen at the start of a game (Z-Spec 8.7.3.3).

    TRACE_LOG("erase: %d / %d\n",
        window_number, z_windows[window_number]->buffering);

    if (bool_equal(z_windows[window_number]->buffering, true))
      flush_window(window_number);

    screen_pixel_interface->fill_area(
        z_windows[window_number]->xpos,
        z_windows[window_number]->ypos,
        z_windows[window_number]->xsize,
        z_windows[window_number]->ysize,
        z_to_rgb_colour(z_windows[window_number]->output_background_colour));

    z_windows[window_number]->xcursorpos
      = z_windows[window_number]->leftmargin;
    z_windows[window_number]->last_gylphs_xcursorpos
      = -1;
    z_windows[window_number]->rightmost_filled_xpos
      = z_windows[window_number]->xcursorpos;
    z_windows[window_number]->ycursorpos
      = (ver >= 5 ? 0 : (z_windows[window_number]->ysize - line_height));

    z_windows[window_number]->nof_consecutive_lines_output = 0;
  }
}


static void link_interface_to_story(struct z_story *story) {
  int bytes_to_allocate;
  int len;
  int i;
  //int ft_error;

  /*
  FT_Face current_face;
  FT_GlyphSlot slot;
  FT_Bitmap bitmap;
  int x,y;
  */
  //int event_type;
  //z_ucs input;

  TRACE_LOG("Linking screen interface to pixel interface.\n");
  screen_pixel_interface->link_interface_to_story(story);

  TRACE_LOG("Linking complete.\n");

  /*
  if ((ft_error = FT_Init_FreeType(&ftlibrary))) {
    i18n_translate_and_exit(
        libpixelif_module_name,
        i18n_libpixelif_FUNCTION_CALL_P0S_ABORTED_DUE_TO_ERROR,
        -1,
        "FT_Init_FreeType");
  }
  */

  font_factory = create_true_type_factory(font_search_path);

  if (regular_font_filename == NULL) {
    set_configuration_value("regular-font", "FiraSans-Regular.ttf");
    set_configuration_value("italic-font", "FiraSans-RegularItalic.ttf");
    set_configuration_value("bold-font", "FiraSans-Medium.ttf");
    set_configuration_value("bold-italic-font", "FiraSans-MediumItalic.ttf");

    set_configuration_value("fixed-regular-font", "FiraMono-Regular.ttf");
    set_configuration_value("fixed-italic-font", "FiraMono-Regular.ttf");
    set_configuration_value("fixed-bold-font", "FiraMono-Bold.ttf");
    set_configuration_value("fixed-bold-italic-font", "FiraMono-Bold.ttf");
  }

  scrollbar_width *= screen_pixel_interface->get_device_to_pixel_ratio();
  font_height_in_pixel *= screen_pixel_interface->get_device_to_pixel_ratio();
  line_height = font_height_in_pixel
    + (4 * screen_pixel_interface->get_device_to_pixel_ratio());

  regular_font = create_true_type_font(font_factory, regular_font_filename,
      font_height_in_pixel, line_height);

  if ( (italic_font_filename == NULL)
      || (strcmp(italic_font_filename, regular_font_filename) == 0) ) {
    italic_font = regular_font;
    italic_font_available = false;
  }
  else {
    italic_font = create_true_type_font(font_factory, italic_font_filename,
        font_height_in_pixel, line_height);
    italic_font_available = true;
  }

  if ( (bold_font_filename == NULL)
      || (strcmp(bold_font_filename, regular_font_filename) == 0) ) {
    bold_font = regular_font;
    bold_font_available = false;
  }
  else {
    bold_font = create_true_type_font(font_factory, bold_font_filename,
        font_height_in_pixel, line_height);
    bold_font_available = true;
  }

  if ( (bold_italic_font_filename == NULL)
      || (strcmp(bold_italic_font_filename, regular_font_filename) == 0) ) {
    bold_italic_font = regular_font;
  }
  else {
    bold_italic_font
      = create_true_type_font(font_factory, bold_italic_font_filename,
          font_height_in_pixel, line_height);
  }

  if ( (fixed_regular_font_filename == NULL)
      || (strcmp(fixed_regular_font_filename, regular_font_filename) == 0) ) {
    fixed_regular_font = regular_font;
    fixed_font_available = false;
  }
  else {
    fixed_regular_font = create_true_type_font(font_factory,
        fixed_regular_font_filename, font_height_in_pixel, line_height);
    fixed_font_available = true;
  }

  if ( (fixed_italic_font_filename == NULL)
      || (strcmp(fixed_italic_font_filename,
          fixed_regular_font_filename) == 0) ) {
    fixed_italic_font = fixed_regular_font;
  }
  else {
    fixed_italic_font = create_true_type_font(font_factory,
        fixed_italic_font_filename, font_height_in_pixel, line_height);
  }

  if ( (fixed_bold_font_filename == NULL)
      || (strcmp(fixed_bold_font_filename,
          fixed_regular_font_filename) == 0) ) {
    fixed_bold_font = fixed_regular_font;
  }
  else {
    fixed_bold_font = create_true_type_font(font_factory,
        fixed_bold_font_filename, font_height_in_pixel, line_height);
  }

  if ( (fixed_bold_italic_font_filename == NULL)
      || (strcmp(fixed_bold_italic_font_filename,
          fixed_regular_font_filename) == 0) ) {
    fixed_bold_italic_font = fixed_regular_font;
  }
  else {
    fixed_bold_italic_font = create_true_type_font(font_factory,
        fixed_bold_italic_font_filename, font_height_in_pixel, line_height);
  }

  /*
  printf("regular font at %p.\n", regular_font);
  printf("bold font at %p.\n", bold_font);
  */

  update_fixed_width_char_width();

  if (ver >= 5) {
    if ( (color_disabled == false)
        && (screen_pixel_interface->is_colour_available() == true)) {
      // we'll be using colors for this story.
      using_colors = true;
      TRACE_LOG("Color enabled.\n");
    }
    else {
      TRACE_LOG("Color disabled.\n");
    }
  }

  screen_height_in_pixel
    = screen_pixel_interface->get_screen_height_in_pixels();
  total_screen_width_in_pixel
    = screen_pixel_interface->get_screen_width_in_pixels();
  screen_width_without_scrollbar
    = total_screen_width_in_pixel - scrollbar_width;

  if (ver <= 2)
    nof_active_z_windows = 1;
  else if (ver == 6)
    nof_active_z_windows = 8;
  else
    nof_active_z_windows = 2;

  if (ver <= 3) {
    statusline_window_id = nof_active_z_windows;
    nof_active_z_windows++;
  }

  measurement_window_id = nof_active_z_windows;
  nof_total_z_windows = nof_active_z_windows + 1;

  TRACE_LOG("Number of active windows: %d.\n", nof_active_z_windows);
  TRACE_LOG("Number of total windows: %d.\n", nof_total_z_windows);

  bytes_to_allocate = sizeof(struct z_window*) * nof_total_z_windows;

  z_windows = (struct z_window**)fizmo_malloc(bytes_to_allocate);

  bytes_to_allocate = sizeof(struct z_window);

  for (i=0; i<nof_active_z_windows; i++) {
    z_windows[i] = (struct z_window*)fizmo_malloc(bytes_to_allocate);
    z_windows[i]->window_number = i;
    z_windows[i]->ypos = 0;
    z_windows[i]->xpos = 0;
    z_windows[i]->text_style = Z_STYLE_ROMAN;
    z_windows[i]->output_text_style = Z_STYLE_ROMAN;
    z_windows[i]->font_type = Z_FONT_NORMAL;
    z_windows[i]->output_font = Z_FONT_NORMAL;
    z_windows[i]->output_true_type_font = regular_font;
    z_windows[i]->nof_consecutive_lines_output = 0;
    z_windows[i]->nof_lines_in_current_paragraph = 0;

    if (i == 0) {
      z_windows[i]->ysize = screen_height_in_pixel;
      z_windows[i]->xsize = screen_width_without_scrollbar;
      z_windows[i]->lower_padding = 4;
      z_windows[i]->scrolling_active = true;
      z_windows[i]->stream2copying_active = true;
      if (ver != 6) {
        z_windows[i]->leftmargin = custom_left_margin
          * screen_pixel_interface->get_device_to_pixel_ratio();
        z_windows[i]->rightmargin = custom_right_margin
          * screen_pixel_interface->get_device_to_pixel_ratio();
      }
      if (statusline_window_id > 0) {
        z_windows[i]->ysize -= line_height;
        z_windows[i]->ypos += line_height;
      }
    }
    else {
      z_windows[i]->lower_padding = 0;
      z_windows[i]->scrolling_active = false;
      z_windows[i]->stream2copying_active = false;
      z_windows[i]->leftmargin = 0;
      z_windows[i]->rightmargin = 0;

      if (i == 1) {
        z_windows[i]->ysize = 0;
        z_windows[i]->xsize = screen_width_without_scrollbar;
        if (statusline_window_id > 0)
          z_windows[i]->ypos += line_height;
        if (ver != 6) {
          z_windows[i]->font_type = Z_FONT_COURIER_FIXED_PITCH;
          z_windows[i]->output_font = Z_FONT_COURIER_FIXED_PITCH;
          z_windows[i]->output_true_type_font = fixed_regular_font;
        }
      }
      else if (i == statusline_window_id) {
        z_windows[i]->ysize = line_height;
        z_windows[i]->xsize = screen_width_without_scrollbar;
        z_windows[i]->scrolling_active = false;
      }
      else {
        z_windows[i]->ysize = 0;
        z_windows[i]->xsize = 0;
      }
    }

    z_windows[i]->newline_routine = 0;
    z_windows[i]->interrupt_countdown = 0;

    if (i == statusline_window_id) {
      z_windows[i]->background_colour = default_background_colour;
      z_windows[i]->output_background_colour = default_background_colour;
      z_windows[i]->foreground_colour = default_foreground_colour;
      z_windows[i]->output_foreground_colour = default_foreground_colour;
      z_windows[i]->text_style = Z_STYLE_REVERSE_VIDEO;
      z_windows[i]->output_text_style = Z_STYLE_REVERSE_VIDEO;
      z_windows[i]->font_type = Z_FONT_NORMAL;
      z_windows[i]->output_font = Z_FONT_NORMAL;
      z_windows[i]->output_true_type_font = bold_font;
    }
    else {
      z_windows[i]->foreground_colour = default_foreground_colour;
      z_windows[i]->background_colour = default_background_colour;
      z_windows[i]->output_foreground_colour = default_foreground_colour;
      z_windows[i]->output_background_colour = default_background_colour;
    }

    z_windows[i]->font_type = 0;
    z_windows[i]->font_size = 0;
    z_windows[i]->line_count = 0;
    z_windows[i]->wrapping = (i == 0) ? true : false;
    z_windows[i]->buffering = ((ver == 6) || (i == 0)) ? true : false;

    z_windows[i]->wordwrapper = create_true_type_wordwrapper(
        regular_font,
        z_windows[i]->xsize - z_windows[i]->leftmargin
        - z_windows[i]->rightmargin,
        &z_ucs_output_window_target,
        (void*)(&z_windows[i]->window_number),
        hyphenation_enabled);
    if (z_windows[i]->buffering == true) {
      z_windows[i]->current_wrapper_style = z_windows[i]->text_style;
      z_windows[i]->current_wrapper_font = z_windows[i]->font_type;
    }

    z_windows[i]->ycursorpos
      = (ver >= 5 ? 0 : (z_windows[i]->ysize - line_height - 4));

    reset_xcursorpos(i);

    erase_window(i);
  }

  z_windows[measurement_window_id]
    = (struct z_window*)fizmo_malloc(bytes_to_allocate);
  z_windows[measurement_window_id]->window_number
    = measurement_window_id;
  z_windows[measurement_window_id]->wordwrapper
    = create_true_type_wordwrapper(
        regular_font,
        z_windows[measurement_window_id]->xsize
        - z_windows[measurement_window_id]->leftmargin
        - z_windows[measurement_window_id]->rightmargin,
        &z_ucs_output_window_target,
        (void*)(&z_windows[measurement_window_id]->window_number),
        hyphenation_enabled);

  active_z_window_id = 0;

  // First, set default colors for the screen, then clear it to correctly
  // initialize everything with the desired colors.
  /*
  if (using_colors == true)
    screen_pixel_interface->set_colour(
        default_foreground_colour, default_background_colour);
    */
  //screen_pixel_interface->fill_area(
  //    1, 1, screen_width_in_pixel, screen_height_in_pixel);

  libpixelif_more_prompt
    = i18n_translate_to_string(
        libpixelif_module_name,
        i18n_libpixelif_MORE_PROMPT);

  len = z_ucs_len(libpixelif_more_prompt);

  libpixelif_more_prompt
    = (z_ucs*)fizmo_realloc(libpixelif_more_prompt, sizeof(z_ucs) * (len + 3));

  memmove(
      libpixelif_more_prompt + 1,
      libpixelif_more_prompt,
      len * sizeof(z_ucs));

  libpixelif_more_prompt[0] = '[';
  libpixelif_more_prompt[len+1] = ']';
  libpixelif_more_prompt[len+2] = 0;

  libpixelif_score_string =
    i18n_translate_to_string(
        libpixelif_module_name,
        i18n_libpixelif_SCORE);

  libpixelif_turns_string
    = i18n_translate_to_string(
        libpixelif_module_name,
        i18n_libpixelif_TURNS);

  /*
  //  -> "Score: x  Turns: x ",
  libpixelif_right_status_min_size
    = z_ucs_len(libpixelif_score_string)
    + z_ucs_len(libpixelif_turns_string)
    + 9; // 5 Spaces, 2 colons, 2 digits.
  */

  //refresh_cursor(active_z_window_id);

  /*
  // Advance the cursor for ZTUU. This will allow the player to read
  // the first line of text before it's overwritten by the status line.
  if (
      (strcmp(story->serial_code, "970828") == 0)
      &&
      (story->release_code == (uint16_t)16)
      &&
      (story->checksum == (uint16_t)4485)
     )
    waddch(z_windows[0]->curses_window, '\n');

  if (
      (story->title != NULL)
      &&
      (use_xterm_title == true)
     )
    printf("%c]0;%s%c", 033, story->title, 007);
  */

  interface_open = true;
}


static void reset_interface() {
  screen_pixel_interface->reset_interface();
}


static int pixel_close_interface(z_ucs *error_message) {
  int event_type, i;
  z_ucs input;

  if ( (error_message == NULL) && (interface_open == true) ) {
    streams_latin1_output("[");
    i18n_translate(
        libpixelif_module_name,
        i18n_libpixelif_PRESS_ANY_KEY_TO_QUIT);
    streams_latin1_output("]");
    flush_window(active_z_window_id);
    screen_pixel_interface->update_screen();

    do
      event_type = screen_pixel_interface->get_next_event(&input, 0, false);
    while (event_type == EVENT_WAS_WINCH);
  }

  screen_pixel_interface->close_interface(error_message);

  for (i=0; i<nof_total_z_windows; i++) {
    if (z_windows[i]->wordwrapper != NULL) {
      destroy_freetype_wrapper(z_windows[i]->wordwrapper);
    }
    free(z_windows[i]);
  }
  free(z_windows);

  if (fixed_bold_italic_font != fixed_regular_font) {
    tt_destroy_font(fixed_bold_italic_font);
  }

  if (fixed_bold_font != fixed_regular_font) {
    tt_destroy_font(fixed_bold_font);
  }

  if (fixed_italic_font != fixed_regular_font) {
    tt_destroy_font(fixed_italic_font);
  }

  if (fixed_regular_font != regular_font) {
    tt_destroy_font(fixed_regular_font);
  }

  if (bold_italic_font != regular_font) {
    tt_destroy_font(bold_italic_font);
  }

  if (bold_font != regular_font) {
    tt_destroy_font(bold_font);
  }

  if (italic_font != regular_font) {
    tt_destroy_font(italic_font);
  }

  if (regular_font != NULL) {
    tt_destroy_font(regular_font);
  }

  if (font_factory != NULL) {
    destroy_true_type_factory(font_factory);
  }

  interface_open = false;
  return 0;
}


static void set_buffer_mode(uint8_t new_buffer_mode) {
  // For non v6 versions:
  // If set to 1, text output on the lower window in stream 1 is buffered
  // up so that it can be word-wrapped properly. If set to 0, it isn't.
  if (new_buffer_mode == 0) {
    // In case buffering is not desired we might have to flush the buffer.
    if (z_windows[0]->buffering == true) {
      flush_window(0);
    }
    z_windows[0]->buffering = false;
  }
  else {
    z_windows[0]->current_wrapper_style = z_windows[0]->text_style;
    z_windows[0]->current_wrapper_font = z_windows[0]->font_type;
    z_windows[0]->buffering = true;
  }
}


static void split_window(int16_t nof_lines) {
  int pixel_delta;
  int nof_pixels = nof_lines * line_height;

  if (nof_pixels >= 0)
  {
    if (nof_pixels > screen_height_in_pixel)
      nof_pixels = screen_height_in_pixel;

    pixel_delta = nof_pixels - z_windows[1]->ysize;

    if (pixel_delta != 0) {
      TRACE_LOG("Old cursor y-pos for window 0: %d.\n",
          z_windows[0]->ycursorpos);

      TRACE_LOG("delta %d\n", pixel_delta);

      if (bool_equal(z_windows[0]->buffering, true))
        flush_window(0);

      z_windows[0]->ysize -= pixel_delta;
      z_windows[0]->ycursorpos -= pixel_delta;
      z_windows[0]->ypos += pixel_delta;
      z_windows[1]->ysize += pixel_delta;

      if (z_windows[0]->ycursorpos < 0) {
        reset_xcursorpos(0);
        z_windows[0]->ycursorpos = 0;
        TRACE_LOG("Re-adjusting cursor for lower window.\n");
      }

      if (z_windows[1]->ycursorpos > z_windows[1]->ysize) {
        reset_xcursorpos(1);
        z_windows[1]->ycursorpos = 0;
        TRACE_LOG("Re-adjusting cursor for upper window.\n");
      }

      TRACE_LOG("New cursor y-pos for window 0: %d (%d).\n",
          z_windows[0]->ycursorpos,
          z_windows[0]->ypos);
      TRACE_LOG("New cursor y-pos for window 1: %d.\n",
          z_windows[1]->ycursorpos);

      if (ver == 3) {
        screen_pixel_interface->fill_area(
            z_windows[1]->xpos,
            z_windows[1]->ypos,
            z_windows[1]->xsize,
            z_windows[1]->ysize,
            z_to_rgb_colour(z_windows[1]->output_background_colour));
      }
    }

    //printf("upper-y-size: %d.\n", z_windows[1]->ysize);

    last_split_window_size = nof_lines;
  }
}


static void set_text_style(z_style text_style) {
  int i, start_win_id, end_win_id;

  //printf("New text style is %d.\n", text_style);
  TRACE_LOG("New text style is %d.\n", text_style);

  if (active_z_window_id == measurement_window_id) {
    start_win_id = measurement_window_id;
    end_win_id = measurement_window_id;
  }
  else {
    start_win_id = 0;
    end_win_id = nof_active_z_windows - (statusline_window_id >= 0 ? 2 : 1);
    TRACE_LOG("end_win_id: %d, nof_wins: %d, statusline_window_id: %d.\n",
        end_win_id, nof_active_z_windows, statusline_window_id);
  }

  for (i=start_win_id; i<=end_win_id; i++) {
    TRACE_LOG("Evaluating style for window %d.\n", i);

    // We'll always set the style for the wordwrapper, even it is currently
    // not active, in case it is turned on again later.
    if (text_style & Z_STYLE_NONRESET)
      z_windows[i]->current_wrapper_style |= text_style;
    else
      z_windows[i]->current_wrapper_style = text_style;
    TRACE_LOG("Resulting text style is %d.\n",
        z_windows[i]->current_wrapper_style);

    TRACE_LOG("Evaluating new font for wordwrapper.\n");
    freetype_wordwrap_insert_metadata(
        z_windows[i]->wordwrapper,
        &wordwrap_output_style,
        (void*)(&z_windows[i]->window_number),
        (uint32_t)(text_style),
        evaluate_font(
          z_windows[i]->current_wrapper_style,
          z_windows[i]->current_wrapper_font));
    TRACE_LOG("Finished evaluating new font for wordwrapper.\n");

    // In case wordwrapping is currently not active, we'll directly set
    // the output parameters (which, during buffering, is otherwise done
    // in time by the wordwrapper).
    if (bool_equal(z_windows[i]->buffering, false)) {
      if (text_style & Z_STYLE_NONRESET)
        z_windows[i]->output_text_style |= text_style;
      else
        z_windows[i]->output_text_style = text_style;
      TRACE_LOG("Resulting text style is %d.\n",
          z_windows[i]->output_text_style);
      update_window_true_type_font(i);
    }
  }
  TRACE_LOG("Done evaluating set_text_style in interface.\n");
}


static void set_colour(z_colour foreground, z_colour background,
    int16_t window_number) {
  int index, end_index, highest_valid_window_id;

  TRACE_LOG("set-color: %d,%d,%d\n", foreground, background, window_number);

  if (using_colors != true)
    return;

  if ( (foreground < 0) || (background < 0) ) {
    TRACE_LOG("Colors < 0 not yet implemented.\n");
    exit(-102);
  }

  highest_valid_window_id
    = nof_active_z_windows - (statusline_window_id >= 0 ? 2 : 1);

  if (active_z_window_id == measurement_window_id) {
    index = measurement_window_id;
    end_index = measurement_window_id;
  }
  else if (window_number == -1) {
    index = 0;
    end_index = highest_valid_window_id;
  }
  else if ( (window_number >= 0)
      && (window_number <= highest_valid_window_id)) {
    index = window_number;
    end_index = window_number;
  }
  else {
    return;
  }

  while (index <= end_index) {
    TRACE_LOG("Processing window %d.\n", index);

    if (bool_equal(z_windows[index]->buffering, false)) {
      TRACE_LOG("new output/nonbuffering color(%d): %d/%d.\n",
          index, foreground, background);
      z_windows[index]->output_foreground_colour = foreground;
      z_windows[index]->output_background_colour = background;
    }
    else {
      TRACE_LOG("new metadata color(%d): %d/%d.\n",
          index, foreground, background);
      freetype_wordwrap_insert_metadata(
          z_windows[index]->wordwrapper,
          &wordwrap_output_colour,
          (void*)(&z_windows[index]->window_number),
          ((uint16_t)foreground | ((uint16_t)(background) << 16)),
          NULL);
    }

    index++;
  }
}


static void set_font(z_font font) {
  int i, start_win_id, end_win_id;

  // 8.7.2.4: An interpreter should use a fixed-pitch font when printing
  // on the upper window.

  /*
  if (version < 6) {
    width_if (active_z_window_id
    */

  TRACE_LOG("New font is %d.\n", font);

  if (active_z_window_id == measurement_window_id) {
    start_win_id = measurement_window_id;
    end_win_id = measurement_window_id;
  }
  else {
    start_win_id = 0;
    end_win_id = nof_active_z_windows - (statusline_window_id >= 0 ? 2 : 1);
  }

  for (i=start_win_id; i<=end_win_id; i++) {
    if (i != 1) {
      z_windows[i]->current_wrapper_font = font;
      freetype_wordwrap_insert_metadata(
          z_windows[i]->wordwrapper,
          &wordwrap_output_font,
          (void*)(&z_windows[i]->window_number),
          (uint32_t)font,
          evaluate_font(
            z_windows[i]->current_wrapper_style,
            z_windows[i]->current_wrapper_font));

      if (bool_equal(z_windows[i]->buffering, false)) {
        z_windows[i]->output_font = font;
        update_window_true_type_font(i);
      }
    }
  }
}


/*
static void history_set_text_style(z_style UNUSED(text_style)) {
  if (refresh_count_mode == false)
  {
    TRACE_LOG("historic-text-style: %d\n", text_style);
    if (bool_equal(z_windows[0]->buffering, false))
      z_windows[0]->output_text_style = text_style;
    else
      wordwrap_insert_metadata(
          refresh_wordwrapper,
          &wordwrap_output_style,
          (void*)(&z_windows[0]->window_number),
          (uint32_t)text_style);
  }
}


static void history_set_font(z_font UNUSED(font_type)) {
  if (refresh_count_mode == false)
  {
    TRACE_LOG("historic-font: %d\n", font_type);
    set_font(font_type);
  }
}


static void history_set_colour(z_colour UNUSED(foreground),
    z_colour UNUSED(background), int16_t UNUSED(window_number)) {
  if (using_colors != true)
    return;

  if (refresh_count_mode == false)
  {
    TRACE_LOG("historic-colour: %d, %d\n", foreground, background);

    if ( (foreground < 1) || (background < 1) )
    {
      TRACE_LOG("Colors < -1 not yet implemented.\n");
      exit(-1);
    }

    if (bool_equal(z_windows[0]->buffering, false))
      {
        z_windows[0]->output_foreground_colour = foreground;
        z_windows[0]->output_background_colour = background;
      }
    else
    {
      wordwrap_insert_metadata(
          refresh_wordwrapper,
          &wordwrap_output_colour,
          (void*)(&z_windows[0]->window_number),
          ((uint16_t)foreground | ((uint16_t)(background) << 16)));
    }
  }
}


static void history_z_ucs_output(z_ucs *output) {
  TRACE_LOG("history_z_ucs_output: \"");
  TRACE_LOG_Z_UCS(output);
  TRACE_LOG("\".\n");

  //while (*output) {
  //  printf("hs-output: %c\n", *output++);
  //}

  if (bool_equal(z_windows[0]->buffering, false))
    z_ucs_output_refresh_destination(output, NULL);
  else
    freetype_wrap_z_ucs(refresh_wordwrapper, output);
}


static history_output_target history_target =
{
  &history_set_text_style,
  &history_set_colour,
  &history_set_font,
  &history_z_ucs_output
};
*/


static history_output_target history_target =
{
  &set_text_style,
  &set_colour,
  &set_font,
  &z_ucs_output
};


static void preload_history_set_text_style(z_style UNUSED(text_style)) {
}


static void preload_history_set_font(z_font UNUSED(font_type)) {
}


static void preload_history_set_colour(z_colour UNUSED(foreground),
    z_colour UNUSED(background), int16_t UNUSED(window_number)) {
}


static void preload_history_z_ucs_output(z_ucs *output) {
  freetype_wrap_z_ucs(preloaded_wordwrapper, output);
}



static history_output_target preload_history_target =
{
  &preload_history_set_text_style,
  &preload_history_set_colour,
  &preload_history_set_font,
  &preload_history_z_ucs_output
};


static void draw_cursor(int cursor_x, int cursor_y) {
  screen_pixel_interface->fill_area(
      cursor_x,
      cursor_y,
      1 * screen_pixel_interface->get_device_to_pixel_ratio(),
      line_height - 2,
      z_to_rgb_colour(pixel_cursor_colour));
}


static void clear_input_line() {
  int i;

  // Fill first input line.
  screen_pixel_interface->fill_area(
      *current_input_x,
      *current_input_y,
      //z_windows[0]->xsize - z_windows[0]->rightmargin - *current_input_x + 2,
      z_windows[0]->xsize - z_windows[0]->rightmargin - *current_input_x,
      line_height,
      z_to_rgb_colour(z_windows[0]->output_background_colour));

  for (i=1; i<nof_input_lines; i++) {
    screen_pixel_interface->fill_area(
        z_windows[0]->xpos + z_windows[0]->leftmargin,
        *current_input_y + i*line_height,
        z_windows[0]->xsize - z_windows[0]->leftmargin
        - z_windows[0]->rightmargin,
        line_height,
        z_to_rgb_colour(z_windows[0]->output_background_colour));
  }
}


static void refresh_input_line(bool display_cursor) {
  int nof_line_breaks, nof_new_input_lines, output_index;
  int last_active_z_window_id = -1;
  bool my_no_more_space;
  int cursor_x, cursor_y;
  z_ucs *input_buffer_index;

  if (input_line_on_screen == false) {
    return;
  }

  TRACE_LOG("Refreshing input line.\n");

  if (active_z_window_id != 0) {
    last_active_z_window_id = active_z_window_id;
    switch_to_window(0);
  }

  //printf("refresh: curx:%d, cury:%d\n",
  //    *current_input_x, *current_input_y);
  TRACE_LOG("refresh: curx:%d, cury:%d\n",
      *current_input_x, *current_input_y);
  z_windows[0]->xcursorpos = *current_input_x - z_windows[0]->xpos
    - z_windows[0]->leftmargin;
  z_windows[0]->last_gylphs_xcursorpos = -1;
  z_windows[0]->rightmost_filled_xpos
    = z_windows[0]->xcursorpos;
  TRACE_LOG("xpos: %d, leftm:%d, xcurpos:%d\n",
      z_windows[0]->xpos,
      z_windows[0]->leftmargin,
      z_windows[0]->xcursorpos);

  z_windows[0]->ycursorpos = *current_input_y - z_windows[0]->ypos;

  clear_input_line();

  nof_line_breaks = 0;

  if (*current_input_buffer) {
    input_buffer_index = current_input_buffer;
    output_index = 0;
    my_no_more_space = false;

    while ((*input_buffer_index) && (my_no_more_space == false)) {

      if (output_index == input_index) {
        cursor_x = z_windows[0]->xpos + z_windows[0]->leftmargin
          + z_windows[0]->xcursorpos;
        cursor_y = z_windows[0]->ypos + z_windows[0]->ycursorpos;
      }

      nof_line_breaks += process_glyph(
          *input_buffer_index,
          0,
          bold_font,
          &my_no_more_space);

      output_index++;
      input_buffer_index++;
    }

    if (output_index == input_index) {
      cursor_x = z_windows[0]->xpos + z_windows[0]->leftmargin
        + z_windows[0]->xcursorpos;
      cursor_y = z_windows[0]->ypos + z_windows[0]->ycursorpos;
    }
  }
  else {
    cursor_x = z_windows[0]->xpos + z_windows[0]->leftmargin
      + z_windows[0]->xcursorpos;
    cursor_y = z_windows[0]->ypos + z_windows[0]->ycursorpos;
  }

  if (display_cursor == true) {
    draw_cursor(cursor_x, cursor_y);
  }

  TRACE_LOG("nof_line_breaks: %d\n", nof_line_breaks);
  TRACE_LOG("current_input_y: %d\n", *current_input_y);
  nof_new_input_lines = nof_line_breaks - (nof_input_lines - 1);
  if (nof_new_input_lines > 0) {
    *current_input_y -= line_height * nof_new_input_lines;
    nof_input_lines += nof_new_input_lines;
    TRACE_LOG("current_input_y: %d, nof_input_lines:%d\n",
        *current_input_y, nof_input_lines);
  }
  //printf("current_input_y: %d, nof_input_lines:%d\n",
  //    *current_input_y, nof_input_lines);

  if (last_active_z_window_id != -1) {
    switch_to_window(last_active_z_window_id);
  }
}


static void show_status(z_ucs *room_description, int status_line_mode,
    int16_t parameter1, int16_t parameter2) {
  int rightside_char_length, rightside_pixel_length, right_pos;
  //int room_desc_space;
  static int rightside_buf_zucs_len = 0;
  static z_ucs *rightside_buf_zucs = NULL;
  //z_ucs buf = 0;
  z_ucs *ptr;
  static char latin1_buf1[8];
  static char latin1_buf2[8];
  int last_active_z_window_id;

  TRACE_LOG("statusline: \"");
  TRACE_LOG_Z_UCS(room_description);
  TRACE_LOG("\".\n");

  TRACE_LOG("statusline-xsize: %d, screen:%d.\n",
      z_windows[statusline_window_id]->xsize, screen_width_without_scrollbar);

  if (statusline_window_id > 0) {
    last_active_z_window_id = active_z_window_id;
    switch_to_window(statusline_window_id);
    erase_window(statusline_window_id);

    z_windows[statusline_window_id]->ycursorpos = 0;
    reset_xcursorpos(statusline_window_id);

    while (z_windows[statusline_window_id]->xcursorpos
        < v3_status_bar_left_margin) {
      z_ucs_output(space_string);
    }

    z_windows[statusline_window_id]->xcursorpos = v3_status_bar_left_margin;
    z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
    z_windows[statusline_window_id]->rightmost_filled_xpos
      = z_windows[statusline_window_id]->xcursorpos;

    z_ucs_output(room_description);

    // Still space for score/turn/time?
    if (z_windows[statusline_window_id]->xcursorpos
        < z_windows[statusline_window_id]->xsize
        - v3_status_bar_left_scoreturntime_margin) {

      if (status_line_mode == SCORE_MODE_SCORE_AND_TURN) {
        // 8.2.3.1: The score may be assumed to be in the range -99 to 999
        // inclusive, and the turn number in the range 0 to 9999.

        sprintf(latin1_buf1, ": %d  ", parameter1);
        sprintf(latin1_buf2, ": %d", parameter2);

        rightside_char_length
          = z_ucs_len(libpixelif_score_string)
          + strlen(latin1_buf1)
          + z_ucs_len(libpixelif_turns_string)
          + strlen(latin1_buf2);

        if (rightside_buf_zucs_len < rightside_char_length) {
          // Allocate a little more so we should be done with one allocation
          // for the game.
          rightside_buf_zucs_len = rightside_char_length + 10;
          TRACE_LOG("Allocating %d bytes for rightside_buf_zucs_len.\n",
              rightside_buf_zucs_len);
          rightside_buf_zucs
            = (z_ucs*)fizmo_realloc(
                rightside_buf_zucs,
                rightside_buf_zucs_len * sizeof(z_ucs));
        }

        ptr = z_ucs_cpy(rightside_buf_zucs, libpixelif_score_string);
        ptr = z_ucs_cat_latin1(ptr, latin1_buf1);
        ptr = z_ucs_cat(ptr, libpixelif_turns_string);
        ptr = z_ucs_cat_latin1(ptr, latin1_buf2);
      }
      else if (status_line_mode == SCORE_MODE_TIME) {
        sprintf(latin1_buf1, "%02d:%02d", parameter1, parameter2);

        rightside_char_length = strlen(latin1_buf1);

        if (rightside_buf_zucs_len < rightside_char_length) {
          // Allocate a little more so we should be done with one allocation
          // for the game.
          rightside_buf_zucs_len = rightside_char_length + 10;
          TRACE_LOG("Allocating %d bytes for rightside_buf_zucs_len.\n",
              rightside_buf_zucs_len);
          rightside_buf_zucs
            = (z_ucs*)fizmo_realloc(
                rightside_buf_zucs,
                rightside_buf_zucs_len * sizeof(z_ucs));
        }

        latin1_string_to_zucs_string(rightside_buf_zucs, latin1_buf1, 8);
      }
      else {
        // Neither SCORE_MODE_SCORE_AND_TURN nor SCORE_MODE_TIME.
      }

      rightside_pixel_length
        = get_glyph_string_size(
            rightside_buf_zucs,
            z_windows[statusline_window_id]->output_true_type_font);

      right_pos
        = z_windows[statusline_window_id]->xsize
        - v3_status_bar_right_margin
        - rightside_pixel_length
        - v3_status_bar_left_scoreturntime_margin;

      // Pad up with spaces -- if there is actually space to fill.
      if (z_windows[statusline_window_id]->xcursorpos < right_pos) {

        while (z_windows[statusline_window_id]->xcursorpos < right_pos) {
          z_ucs_output(space_string);
        }

        z_windows[statusline_window_id]->xcursorpos = right_pos;
        z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
        z_windows[statusline_window_id]->rightmost_filled_xpos
          = z_windows[statusline_window_id]->xcursorpos;
      }
      else {
        // We need at least some space.
        z_ucs_output(space_string);
      }

      z_ucs_output(rightside_buf_zucs);

      while (z_windows[statusline_window_id]->xcursorpos
          < z_windows[statusline_window_id]->xsize) {
        z_ucs_output(space_string);
      }
    }

    switch_to_window(last_active_z_window_id);
  }
}


static void refresh_upper_window() {
  int x, y;
  int xcurs_buf, ycurs_buf, x_width;
  z_style current_style, style_buf;
  z_colour current_foreground, foreground_buf;
  z_colour current_background, background_buf;
  struct blockbuf_char *current_char;
  int last_glyphpos_buf, rightmost_buf;

  if (last_split_window_size > 0) {
    style_buf = z_windows[1]->output_text_style;
    foreground_buf = z_windows[1]->output_foreground_colour;
    background_buf = z_windows[1]->output_background_colour;

    xcurs_buf = z_windows[1]->xcursorpos;
    last_glyphpos_buf = z_windows[1]->last_gylphs_xcursorpos;
    rightmost_buf = z_windows[1]->rightmost_filled_xpos;
    ycurs_buf = z_windows[1]->ycursorpos;
    reset_xcursorpos(1);
    current_style = upper_window_buffer->content[0].style;
    current_foreground = upper_window_buffer->content[0].foreground_colour;
    current_background = upper_window_buffer->content[0].background_colour;
    x_width = get_screen_width_in_characters();

    erase_window(1);

    z_windows[1]->output_text_style = current_style;
    z_windows[1]->output_foreground_colour = current_foreground;
    z_windows[1]->output_background_colour = current_background;
    update_window_true_type_font(1);
    /*
    printf("bb: style:%d, f:%d, b:%d\n",
        current_style, current_foreground, current_background);
    */

    for (y=0; y<last_split_window_size; y++) {
      if (y > 0) {
        break_line(1);
      }
      for (x=0; x<x_width; x++) {
        /*
        printf("x, y, fg, bg: %d, %d, %d, %d.\n", x, y,
            z_windows[1]->output_foreground_colour,
            z_windows[1]->output_background_colour);
        */
        current_char
          = upper_window_buffer->content + upper_window_buffer->width*y + x;

        if (current_foreground != current_char->foreground_colour) {
          current_foreground = current_char->foreground_colour;
          z_windows[1]->output_foreground_colour = current_foreground;
        }

        if (current_background != current_char->background_colour) {
          current_background = current_char->background_colour;
          z_windows[1]->output_background_colour = current_background;
        }

        if (current_style!= current_char->style) {
          current_style = current_char->style;
          z_windows[1]->output_text_style = current_style;
          update_window_true_type_font(1);
          //printf("new-bb-style: %d\n", current_style);
        }

        process_glyph(
            upper_window_buffer->content[
            upper_window_buffer->width*y + x].character,
            1,
            z_windows[1]->output_true_type_font,
            NULL);
      }
    }
    z_windows[1]->xcursorpos = xcurs_buf;
    z_windows[1]->ycursorpos = ycurs_buf;
    z_windows[1]->last_gylphs_xcursorpos = last_glyphpos_buf;
    z_windows[1]->rightmost_filled_xpos = rightmost_buf;

    z_windows[1]->output_foreground_colour = foreground_buf;
    z_windows[1]->output_background_colour = background_buf;
    z_windows[1]->output_text_style = style_buf;
    update_window_true_type_font(1);
  }
}


void preload_wrap_zucs_output(z_ucs *UNUSED(z_ucs_output),
    void *UNUSED(window_number_as_void)) {
}


static void init_screen_redraw() {
  if (history != NULL) {
    TRACE_LOG("history not null, cannot scroll.\n");
    return;
  }
  if ((history = init_history_output(
          outputhistory[0],
          &history_target,
          Z_HISTORY_OUTPUT_WITHOUT_EXTRAS)) == NULL) {
    TRACE_LOG("Cannot init history.\n");
    return;
  }
  TRACE_LOG("History: %p\n", history);
  TRACE_LOG("z_windows[0]->ysize: %d.\n", z_windows[0]->ysize);
  history_screen_line = 0;
}


static void end_screen_redraw() {
  TRACE_LOG("Ending scrollback.\n");
  top_upscroll_line = -1;
  upscroll_hit_top = false;
  destroy_history_output(history);
  history = NULL;
  redraw_pixel_lines_to_skip = 0;
  redraw_pixel_lines_to_draw = -1;
  screen_pixel_interface->set_cursor_visibility(true);
  z_windows[0]->nof_lines_in_current_paragraph = 0;
}


static void redraw_screen_area(int top_line_to_redraw) {
  bool stored_more_disable_state;
  int paragraph_attr1, paragraph_attr2;
  int return_code;

  /*
  printf("Redrawing screen from line %d to %d.\n",
      top_line_to_redraw,
      top_line_to_redraw + redraw_pixel_lines_to_draw);
  printf("top_upscroll_line: %d.\n", top_upscroll_line);
  */

  TRACE_LOG("Redrawing screen from line %d to %d.\n",
      top_line_to_redraw,
      top_line_to_redraw + redraw_pixel_lines_to_draw);
  TRACE_LOG("top_upscroll_line: %d.\n", top_upscroll_line);

  stored_more_disable_state = disable_more_prompt;
  disable_more_prompt = true;

  // Check if the history is pointing at some place below the
  // current window to redraw.
  while (top_upscroll_line
      > (nof_input_lines - 1 + history_screen_line) * line_height
      + z_windows[0]->lower_padding) {

    /*
    printf("(nil - 1 + hsl) * line_height + low_padding = %d.\n",
        (nof_input_lines - 1 + history_screen_line) * line_height
        + z_windows[0]->lower_padding);
    */

    paragraph_attr1 = 0;
    // In case we're repeating the very last paragraph this likely
    // won't have its height stored in the history (simply because
    // it hasn't been finished yet). In order to detect this we'll
    // set paragraph_attr1 to 0 and compare later.

    /*
    printf("nof_input_lines: %d.\n", nof_input_lines);
    printf("history_screen_line: %d.\n", history_screen_line);
    printf("pre-rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
          paragraph_attr1, paragraph_attr2);
    */

    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);
    TRACE_LOG("pre-rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
          paragraph_attr1, paragraph_attr2);

    // Rewind history by one paragraph
    return_code = output_rewind_paragraph(history, NULL,
        &paragraph_attr1, &paragraph_attr2);
    TRACE_LOG("return_code: %d.\n", return_code);

    // Adapt line number that history is currently pointing to.

    if (return_code == 0) {
      /*
      printf("rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
          paragraph_attr1, paragraph_attr2);
      printf("history_screen_line: %d.\n", history_screen_line);
      */
      TRACE_LOG("rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
          paragraph_attr1, paragraph_attr2);
      TRACE_LOG("history_screen_line: %d.\n", history_screen_line);

      history_screen_line
        += paragraph_attr1 != 0 ? paragraph_attr1 : 1;
      //printf("rewind, history_screen_line: %d.\n", history_screen_line);
    }
    else if (return_code == 1) {
      //printf("Hit buffer back while rewinding / %d.\n",
      //    history_screen_line);
      TRACE_LOG("Hit buffer back while rewinding / %d.\n",
          history_screen_line);
      history_screen_line
        += paragraph_attr1 != 0 ? paragraph_attr1 : 1;
      break;
    }

    /*
    printf("tusl: %d, h*l*lp:%d\n", top_upscroll_line + 1,
        (nof_input_lines - 1 + history_screen_line) * line_height
        + z_windows[0]->lower_padding);
    */
  }

  /*
  printf("\n---\n(nil - 1 + hsl) * line_height + low_padding = %d.\n",
      (nof_input_lines - 1 + history_screen_line) * line_height
      + z_windows[0]->lower_padding);
  */

  TRACE_LOG("top_line_to_redraw: %d.\n", top_line_to_redraw);

  redraw_pixel_lines_to_skip
    = ( (nof_input_lines - 1 + history_screen_line) * line_height
        + z_windows[0]->lower_padding)
    - (top_upscroll_line + 1)
    + top_line_to_redraw;
  TRACE_LOG("redraw_pixel_lines_to_skip: %d.\n", redraw_pixel_lines_to_skip);
  //printf("redraw_pixel_lines_to_skip: %d.\n", redraw_pixel_lines_to_skip);

  TRACE_LOG("history_screen_line: %d.\n",
      history_screen_line);
  TRACE_LOG("line_height: %d.\n",
      line_height);
  TRACE_LOG("history_screen_line * line_height: %d.\n",
      history_screen_line * line_height);

  z_windows[0]->ycursorpos = top_line_to_redraw;
  TRACE_LOG("redraw at ypos: %d.\n", z_windows[0]->ycursorpos);
  reset_xcursorpos(0);

  while (redraw_pixel_lines_to_draw > 0) {
    //printf("redraw_pixel_lines_to_skip: %d.\n", redraw_pixel_lines_to_skip);
    //printf("redraw_pixel_lines_to_draw: %d.\n", redraw_pixel_lines_to_draw);
    z_windows[0]->nof_consecutive_lines_output = 0;
    //printf("ypos at %d.\n", z_windows[0]->ycursorpos);
    output_repeat_paragraphs(history, 1, true, true);
    z_ucs_output(newline_string);
    flush_window(0);
    //printf("redraw, history_screen_line: %d.\n", history_screen_line);
    history_screen_line -= z_windows[0]->nof_consecutive_lines_output;
    //printf("redraw, history_screen_line: %d.\n", history_screen_line);

    /*
    printf("total_nof_lines_stored: %ld\n", total_nof_lines_stored);
    printf("history_screen_line: %d.\n", history_screen_line);
    printf("redraw_pixel_lines_to_draw: %d.\n", redraw_pixel_lines_to_draw);
    */
    //screen_pixel_interface->update_screen();
    //event_type = get_next_event_wrapper(&input, 0);
  }

  disable_more_prompt = stored_more_disable_state;
}


void finish_history_remeasurement() {
  int last_active_z_window_id = -1;

  if (history_is_being_remeasured == true) {
    last_active_z_window_id = init_history_remeasurement();
    do {
      remeasure_next_paragraph();
    }
    while (history_is_being_remeasured == true);
    end_history_remeasurement(last_active_z_window_id);
    TRACE_LOG("total_lines_in_history recalc: %ld.\n", total_lines_in_history);
  }
}


static void refresh_screen() {
  int i, last_active_z_window_id = -1;
  int y_height_to_fill;
  int nof_paragraphs_to_repeat;
  int paragraph_attr1, paragraph_attr2;
  int rewind_return_code;
  int saved_padding;

  if (active_z_window_id != 0) {
    last_active_z_window_id = active_z_window_id;
    switch_to_window(0);
  }

  refresh_active = true;

  TRACE_LOG("Refreshing screen, size: %d*%d.\n",
      total_screen_width_in_pixel, screen_height_in_pixel);
  erase_window(0);

  finish_history_remeasurement();
  disable_more_prompt = true;
  init_screen_redraw();
  TRACE_LOG("History: %p\n", history);

  y_height_to_fill
    = z_windows[0]->ysize
    //- nof_input_lines  * line_height
    - z_windows[0]->lower_padding;
  /*
  printf("y_height_to_fill: %d, nof_input_lines: %d\n",
      y_height_to_fill, nof_input_lines);
  */

  for (i=0; i<nof_active_z_windows - (statusline_window_id >= 0 ? 1 : 0); i++) {
    if ( (ver == 6) || (i != 1) ) {
      z_windows[i]->text_style = Z_STYLE_ROMAN;
      z_windows[i]->output_text_style = Z_STYLE_ROMAN;
      z_windows[i]->font_type = Z_FONT_NORMAL;
      z_windows[i]->output_font = Z_FONT_NORMAL;
      z_windows[i]->output_true_type_font = regular_font;
    }
  }

  nof_paragraphs_to_repeat = 0;
  do {
    paragraph_attr1 = 0;
    paragraph_attr2 = 0;
    // In case we're repeating the very last paragraph this likely
    // won't have its height stored in the history (simply because
    // it hasn't been finished yet). In order to detect this we'll
    // set paragraph_attr1 to 0 and compare later.

    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);
    TRACE_LOG("pre-rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
        paragraph_attr1, paragraph_attr2);

    // Rewind history by one paragraph
    rewind_return_code = output_rewind_paragraph(history, NULL,
        &paragraph_attr1, &paragraph_attr2);
    if (rewind_return_code == 0) {
      TRACE_LOG("rewind: paragraph_attr1:%d, paragraph_attr2: %d.\n",
          paragraph_attr1, paragraph_attr2);
      TRACE_LOG("history_screen_line: %d.\n", history_screen_line);
      nof_paragraphs_to_repeat++;

      // Adapted line number that history is currently pointing to.
      history_screen_line
        += paragraph_attr1 != 0 ? paragraph_attr1 : 1;
    }
    else if (rewind_return_code == 1) {
      //printf("buffer back encountered.\n");
      // buffer back encountered
      break;
    }
    //printf("history_screen_line * line_height: %d, y_height_to_fill:%d.\n",
    //    history_screen_line * line_height, y_height_to_fill);
  }
  // Scroll up until we're above the lowest line to refresh.
  while (history_screen_line * line_height <= y_height_to_fill);

  saved_padding = z_windows[0]->lower_padding;
  //printf("lower_padding: %d.\n", z_windows[0]->lower_padding);
  z_windows[0]->lower_padding += (nof_input_lines - 1) * line_height;
  //printf("lower_padding: %d.\n", z_windows[0]->lower_padding);
  z_windows[0]->ycursorpos
    = z_windows[0]->ysize
    - z_windows[0]->lower_padding
    - line_height;
  TRACE_LOG("refresh ycursorpos: %d.\n", z_windows[0]->ycursorpos);
  reset_xcursorpos(0);
  freetype_wordwrap_reset_position(z_windows[0]->wordwrapper);
  output_repeat_paragraphs(history, nof_paragraphs_to_repeat, true, false);
  flush_window(0);
  clear_to_eol(0);
  z_windows[0]->lower_padding = saved_padding;

  end_screen_redraw();

  z_windows[0]->ycursorpos
    = z_windows[0]->ysize
    - (nof_input_lines > 1 ? nof_input_lines : 1) * line_height
    - z_windows[0]->lower_padding;
  if (input_line_on_screen == true) {
    *current_input_y = z_windows[0]->ypos + z_windows[0]->ycursorpos;
    refresh_input_line(true);
  }
  else {
    reset_xcursorpos(0);
    freetype_wordwrap_reset_position(z_windows[0]->wordwrapper);
  }

  if (last_active_z_window_id != -1) {
    switch_to_window(last_active_z_window_id);
  }

  refresh_upper_window();

  if (ver <= 3) {
    display_status_line();
  }

  refresh_scrollbar();
  screen_pixel_interface->update_screen();

  z_windows[0]->nof_consecutive_lines_output = 0;
  refresh_active = false;
  disable_more_prompt = false;
}


void handle_scrolling(int event_type) {
  int lines_to_copy; //, saved_padding; //, line_shift,
  int top_line_to_draw;
  //int paragraph_attr1, paragraph_attr2;
  //int nof_paragraphs;
  int max_top_scroll_line;
  int previous_upscroll_position;
  //int history_screen_line_buf;
  //int return_code;
  //int extra_padding;
  //z_ucs input;

  TRACE_LOG("Starting handle_scrolling.\n");

  if ( (event_type == EVENT_WAS_CODE_PAGE_DOWN)
      && (top_upscroll_line <= z_windows[0]->ysize) ) {
    TRACE_LOG("Already at bottom.\n");
    return;
  }

  //printf("total_lines_in_history: %ld.\n", total_lines_in_history);
  max_top_scroll_line
    = (nof_input_lines - 1 + total_lines_in_history) * line_height
    + z_windows[0]->lower_padding - 1;
  //printf("\n\ntotal_lines_in_history: %ld\n", total_lines_in_history);
  // We're always using the term "nof_input_lines - 1" since the last
  // history line and the first input line share the same line on screen.
  TRACE_LOG("max_top_scroll_line: %d.\n", max_top_scroll_line);
  //printf("max_top_scroll_line: %d.\n", max_top_scroll_line);

  if (max_top_scroll_line < z_windows[0]->ysize) {
    // We don't have yet enough history to be able to scroll.
    return;
  }

  if ( (event_type == EVENT_WAS_CODE_PAGE_UP)
      && (top_upscroll_line >= max_top_scroll_line) ) {
    TRACE_LOG("Already at top.\n");
    return;
  }

  // Since we need the paragraph measurements, we'll have to complete
  // remeasuring in case it hasn't yet been finished.
  finish_history_remeasurement();

  refresh_active = true;

  if (top_upscroll_line == -1) {
    // Value -1 means that upscrolling is not yet initialized and that
    // we're at the bottom of the output.
    init_screen_redraw();
    top_upscroll_line = z_windows[0]->ysize - 1;
  }

  previous_upscroll_position = top_upscroll_line;

  if (event_type == EVENT_WAS_CODE_PAGE_UP) {

    top_upscroll_line += (z_windows[0]->ysize / 2);

    if (top_upscroll_line >= max_top_scroll_line) {
      top_upscroll_line = max_top_scroll_line;
    }

    lines_to_copy
      = z_windows[0]->ysize
      - (top_upscroll_line - previous_upscroll_position);

    TRACE_LOG("top_upscroll_line: %d.\n", top_upscroll_line);
    TRACE_LOG("lines_to_copy: %d.\n", lines_to_copy);
    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);

    screen_pixel_interface->copy_area(
        z_windows[active_z_window_id]->ypos
        + z_windows[0]->ysize - lines_to_copy,
        z_windows[active_z_window_id]->xpos,
        z_windows[active_z_window_id]->ypos,
        z_windows[active_z_window_id]->xpos,
        lines_to_copy,
        z_windows[active_z_window_id]->xsize);
    //screen_pixel_interface->update_screen();
    //event_type = get_next_event_wrapper(&input, 0);

    redraw_pixel_lines_to_draw = z_windows[0]->ysize - lines_to_copy;

    screen_pixel_interface->fill_area(
        z_windows[0]->xpos,
        z_windows[0]->ypos,
        z_windows[0]->xsize,
        redraw_pixel_lines_to_draw,
        z_to_rgb_colour(z_windows[0]->output_background_colour));
    //screen_pixel_interface->update_screen();
    //event_type = get_next_event_wrapper(&input, 0);

    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);
    //saved_padding = z_windows[0]->lower_padding;
    //z_windows[0]->lower_padding += lines_to_copy;

    top_line_to_draw = 0;
  }
  else if (event_type == EVENT_WAS_CODE_PAGE_DOWN) {

    top_upscroll_line -= (z_windows[0]->ysize / 2);

    if (top_upscroll_line < z_windows[0]->ysize) {
      // End up-scroll.
      end_screen_redraw();
      refresh_screen();
      return;
    }

    lines_to_copy
      = z_windows[0]->ysize
      - (previous_upscroll_position - top_upscroll_line);

    TRACE_LOG("top_upscroll_line: %d.\n", top_upscroll_line);
    TRACE_LOG("lines_to_copy: %d.\n", lines_to_copy);
    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);

    screen_pixel_interface->copy_area(
        z_windows[active_z_window_id]->ypos,
        z_windows[active_z_window_id]->xpos,
        z_windows[active_z_window_id]->ypos
        + z_windows[0]->ysize - lines_to_copy,
        z_windows[active_z_window_id]->xpos,
        lines_to_copy,
        z_windows[active_z_window_id]->xsize);
    //screen_pixel_interface->update_screen();
    //event_type = get_next_event_wrapper(&input, 0);

    redraw_pixel_lines_to_draw = z_windows[0]->ysize - lines_to_copy;
    top_line_to_draw = z_windows[0]->ysize - redraw_pixel_lines_to_draw;

    screen_pixel_interface->fill_area(
        z_windows[0]->xpos,
        z_windows[0]->ypos + top_line_to_draw,
        z_windows[0]->xsize,
        redraw_pixel_lines_to_draw,
        //z_to_rgb_colour(Z_COLOUR_RED));
        z_to_rgb_colour(z_windows[0]->output_background_colour));
    //screen_pixel_interface->update_screen();
    //event_type = get_next_event_wrapper(&input, 0);

    TRACE_LOG("history_screen_line: %d.\n", history_screen_line);
    //saved_padding = z_windows[0]->lower_padding;
    //z_windows[0]->lower_padding += lines_to_copy;
  }
  else {
    // Neither up nor down?
    return;
  }

  redraw_screen_area(top_line_to_draw);

  freetype_wordwrap_reset_position(z_windows[0]->wordwrapper);
  refresh_scrollbar();
  screen_pixel_interface->update_screen();
  //disable_more_prompt = false;
  z_windows[0]->nof_consecutive_lines_output = 0;

  TRACE_LOG("End of handle_scrolling.\n");
  //printf("End of handle_scrolling.\n");
}


// NOTE: Keep in mind that the verification routine may recursively
// call a read (Border Zone does this).
// This function reads a maximum of maximum_length characters from stdin
// to dest. The number of characters read is returned. The input is NOT
// terminated with a newline (in order to conform to V5+ games).
// Returns -1 when int routine returns != 0
// Returns -2 when user ended input with ESC
//
// Input concept for pixel-interface:
// In case the user's input doesn't fit on a single line, text is scrolled
// upewards and input continues on the next line. The whole user input has
// to fit on the visible screen -- should, for some reason, the first line
// of user input ever reach the top row, no more input is accepted (this
// will probably never happen in reality, since even if a window would only
// allow 5 lines and 20 columns there would still be space for 100 chars of
// input).
//
// The "top left" input position is stored in input_x and input_y -- top left
// being quoted since once input wraps into a new line, there's an even more
// "lefter" position, but I guess you get the idea: Everything right and below
// of these coordinates belongs to the input and is refreshed accordingly.
// Determining this position however is quite a pain in the case of preloaded
// input (see below).
static int16_t read_line(zscii *dest, uint16_t maximum_length,
    uint16_t tenth_seconds, uint32_t verification_routine,
    uint8_t preloaded_input, int *tenth_seconds_elapsed,
    bool disable_command_history, bool return_on_escape) {
  int timeout_millis = -1, event_type, i;
  bool input_in_progress = true;
  z_ucs input; //, buf;
  int current_tenth_seconds = 0;
  int input_size = preloaded_input;
  int timed_routine_retval; //, index;
  int input_x, input_y, input_rightmost_x; // Leftmost position of the input line on-screen.
  input_index = preloaded_input;
  z_ucs input_buffer[maximum_length + 1];
  current_input_buffer = input_buffer;
  current_input_x = &input_x;
  current_input_y = &input_y;
  history_output *preload_history = NULL;
  int cmd_history_index = 0;
  zscii *cmd_history_ptr;

  TRACE_LOG("maxlen:%d, preload: %d.\n", maximum_length, preloaded_input);
  TRACE_LOG("y: %d, %d\n",
      z_windows[active_z_window_id]->ypos,
      z_windows[active_z_window_id]->ycursorpos);

  flush_all_buffered_windows();
  for (i=0; i<nof_active_z_windows; i++) {
    if (i != measurement_window_id) {
      z_windows[i]->nof_consecutive_lines_output = 0;
    }
  }

  TRACE_LOG("y: %d, %d\n",
      z_windows[active_z_window_id]->ypos,
      z_windows[active_z_window_id]->ycursorpos);

  /*
  if (z_windows[active_z_window_id]->xcursorpos
      + z_windows[active_z_window_id]->rightmargin
      > z_windows[active_z_window_id]->xsize - 1) {
    TRACE_LOG("breaking line, too short for input.\n");

    break_line(active_z_window_id);
    refresh_screen();
  }
  */

  if (winch_found == true) {
    new_pixel_screen_size(
        screen_pixel_interface->get_screen_height_in_pixels(),
        screen_pixel_interface->get_screen_width_in_pixels());
    winch_found = false;
  }

  TRACE_LOG("Flush finished.\n");

  TRACE_LOG("1/10s: %d, routine: %d.\n",
      tenth_seconds, verification_routine);

  if ((tenth_seconds != 0) && (verification_routine != 0)) {
    TRACE_LOG("timed input in read_line every %d tenth seconds.\n",
        tenth_seconds);

    timed_input_active = true;

    timeout_millis = (is_timed_keyboard_input_available() == true ? 100 : 0);

    if (tenth_seconds_elapsed != NULL) {
      *tenth_seconds_elapsed = 0;
    }
  }
  else {
    timed_input_active = false;
  }

  TRACE_LOG("y: %d, %d\n",
      z_windows[active_z_window_id]->ypos,
      z_windows[active_z_window_id]->ycursorpos);

  TRACE_LOG("nof preloaded chars: %d\n", preloaded_input);
  for (i=0; i<preloaded_input; i++) {
    input_buffer[i] = zscii_input_char_to_z_ucs(dest[i]);
  }
  input_buffer[i] = 0;

  if (preloaded_input < 1) {
    input_x
      = z_windows[active_z_window_id]->xpos
      + z_windows[active_z_window_id]->xcursorpos
      + z_windows[active_z_window_id]->leftmargin;
      //- 1;

    input_y
      = z_windows[active_z_window_id]->ypos
      + z_windows[active_z_window_id]->ycursorpos; // - 1 - line_height;

    input_rightmost_x
      = z_windows[active_z_window_id]->rightmost_filled_xpos;
  }
  else {
    // We cannot simply get the current cursor position and work from here:
    // Instead, it is necessary to evaluate the location of the original
    // prompt.

    // Since it's not possible to simply move left from the current
    // position -- there may be linebreaks in the preloaded input and
    // it's not possible to know where to start on the upper line, since
    // text is not right-aligned -- we need to evaluate the entire last
    // paragraph to find the correct input start position.

    if ((preload_history = init_history_output(
            outputhistory[0],
            &preload_history_target,
            Z_HISTORY_OUTPUT_WITHOUT_EXTRAS)) != NULL) {

      TRACE_LOG("History: %p\n", preload_history);

      TRACE_LOG("Trying to find paragraph to fill %d lines.\n",
          z_windows[0]->ysize);

      preloaded_wordwrapper = create_true_type_wordwrapper(
          regular_font,
          z_windows[active_z_window_id]->xsize
          - z_windows[active_z_window_id]->leftmargin
          - z_windows[active_z_window_id]->rightmargin,
          &preload_wrap_zucs_output,
          (void*)(&z_windows[active_z_window_id]->window_number),
          hyphenation_enabled);

      // First we need to evaluate the size of the last paragraph. Since
      // we're only evaluating the last paragraph, no newlines can occur
      // in the repeated output.

      if (output_rewind_paragraph(preload_history, NULL, NULL, NULL) < 0) {
        //printf("err\n");
      }
      output_repeat_paragraphs(preload_history, 1, false, false);
      input_x = get_current_pixel_position(preloaded_wordwrapper)
        + z_windows[active_z_window_id]->xpos
        + z_windows[active_z_window_id]->leftmargin;
      input_y
        = z_windows[active_z_window_id]->ypos
        + z_windows[active_z_window_id]->ycursorpos; // - 1 - line_height;
      input_rightmost_x
        = z_windows[active_z_window_id]->rightmost_filled_xpos;

      destroy_freetype_wrapper(preloaded_wordwrapper);
      destroy_history_output(preload_history);
      preload_history = NULL;
    }
  }

  TRACE_LOG("input_x, input_y: %d, %d\n", input_x, input_y);

  TRACE_LOG("xpos:%d\n", z_windows[active_z_window_id]->xcursorpos);

  input_line_on_screen = true;
  nof_input_lines = 1;
  refresh_input_line(true);
  refresh_scrollbar();
  screen_pixel_interface->update_screen();

  while (input_in_progress == true) {
    event_type = get_next_event_wrapper(&input, timeout_millis);
    TRACE_LOG("Evaluating event %d.\n", event_type);

    if (event_type == EVENT_WAS_TIMEOUT) {
      // Don't forget to restore current_input_buffer on recursive read.
      TRACE_LOG("timeout found.\n");

      if (timed_input_active == true)
      {
        current_tenth_seconds++;
        TRACE_LOG("%d / %d.\n", current_tenth_seconds, tenth_seconds);
        if (tenth_seconds_elapsed != NULL)
          (*tenth_seconds_elapsed)++;

        if (current_tenth_seconds == tenth_seconds) {
          current_tenth_seconds = 0;
          stream_output_has_occured = false;

          // Remove cursor:
          //refresh_input_line(false);
          clear_input_line();
          //(void)streams_z_ucs_output_user_input(input_buffer);
          z_windows[0]->xcursorpos = *current_input_x - z_windows[0]->xpos
            - z_windows[0]->leftmargin;
          z_windows[0]->last_gylphs_xcursorpos = -1;
          z_windows[0]->rightmost_filled_xpos
            = z_windows[0]->xcursorpos;
          z_windows[0]->ycursorpos = *current_input_y - z_windows[0]->ypos;
          process_glyph_string(input_buffer, 0, regular_font, NULL);

          TRACE_LOG("calling timed-input-routine at %x.\n",
              verification_routine);
          timed_routine_retval = interpret_from_call(verification_routine);
          TRACE_LOG("timed-input-routine finished.\n");

          if (terminate_interpreter != INTERPRETER_QUIT_NONE) {
            TRACE_LOG("Quitting after verification.\n");
            input_in_progress = false;
            input_size = 0;
          }
          else {
            if (stream_output_has_occured == true) {
              flush_all_buffered_windows();
              /*
              z_windows[active_z_window_id]->xcursorpos
                = *current_input_size > *current_input_display_width
                ? *current_input_x + *current_input_display_width
                : *current_input_x + *current_input_size;
              */
              z_windows[active_z_window_id]->last_gylphs_xcursorpos = -1;
            }

            if (timed_routine_retval != 0) {
              input_in_progress = false;
              input_size = 0;
            }
            else {
              // Re-display cursor.
              refresh_input_line(true);
              screen_pixel_interface->update_screen();
            }
          }
        }
      }
    }
    else if ( (event_type == EVENT_WAS_CODE_PAGE_UP)
        || (event_type == EVENT_WAS_CODE_PAGE_DOWN)) {
      handle_scrolling(event_type);
      /*
      printf("XXX : nlicp: %d\n",
          z_windows[0]->nof_lines_in_current_paragraph);
      */
    }
    else {
      if (top_upscroll_line != -1) {
        // End up-scroll.
        end_screen_redraw();
        refresh_screen();
      }

      if (event_type == EVENT_WAS_INPUT) {
        //printf("%c / %d\n", input, input);
        if (input == Z_UCS_NEWLINE) {
          input_in_progress = false;
        }
        else if (
            // Check if we have a valid input char.
            (unicode_char_to_zscii_input_char(input) != 0xff)
            &&
            (
             // We'll also only add new input if we're either not at the end
             // of a filled input line ...
             (input_size < maximum_length)
             ||
             // ... or if the cursor is left of the input end.
             (input_index < input_size))) {
          TRACE_LOG("New ZSCII input char %d / z_ucs code %d.\n",
              unicode_char_to_zscii_input_char(input), input);

          TRACE_LOG("Input_buffer at %p (length %d): \"",
              input_buffer, input_index);
          TRACE_LOG_Z_UCS(input_buffer);
          TRACE_LOG("\".\n");

          TRACE_LOG("input_index: %d, input_size: %d, maximum_length: %d.\n",
              input_index, input_size, maximum_length);

          if (input_index < input_size) {
            // In case we're not appending at the end of the input, we'll
            // provide space for a new char in the input (and lose the rightmost
            // char in case the input line is full):
            memmove(
                input_buffer + input_index + 1,
                input_buffer + input_index,
                sizeof(z_ucs) * (input_size - input_index + 1
                  - (input_size < maximum_length ? 0 : 1)));

            TRACE_LOG("%p, %p, %lu.\n",
                input_buffer + input_index + 1,
                input_buffer + input_index,
                sizeof(z_ucs) * (input_size - input_index + 1
                  - (input_size < maximum_length ? 0 : 1)));
          }
          else {
            input_buffer[input_index + 1] = 0;
          }

          input_buffer[input_index] = input;
          input_index++;

          TRACE_LOG("fresh input_buffer (length %d): \"", input_index);
          TRACE_LOG_Z_UCS(input_buffer);
          TRACE_LOG("\".\n");

          if (input_size < maximum_length)
            input_size++;

          TRACE_LOG("xcp %d, rm %d, xs: %d.\n",
              z_windows[active_z_window_id]->xcursorpos -1,
              z_windows[active_z_window_id]->rightmargin,
              z_windows[active_z_window_id]->xsize);

          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if (event_type == EVENT_WAS_CODE_BACKSPACE) {
        // We only have something to do if the cursor is not at the start of
        // the input.
        if (input_index > 0) {
          // We always have to move all chars from the cursor position onward
          // one position to the left.
          memmove(
              input_buffer + input_index - 1,
              input_buffer + input_index,
              sizeof(z_ucs)*(input_size - input_index + 1));

          input_size--;
          input_index--;
          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_LEFT) {
        if (input_index > 0) {
          input_index--;
          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_RIGHT) {
        if (input_index < input_size) {
          input_index++;
          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if ( (disable_command_history == false)
          && (
            (
             (event_type == EVENT_WAS_CODE_CURSOR_UP)
             && (cmd_history_index < get_number_of_stored_commands())
            )
            || ( (event_type == EVENT_WAS_CODE_CURSOR_DOWN)
              && (cmd_history_index != 0)))) {
        TRACE_LOG("old history index: %d.\n", cmd_history_index);
        cmd_history_index += event_type == EVENT_WAS_CODE_CURSOR_UP ? 1 : -1;
        cmd_history_ptr = get_command_from_history(cmd_history_index - 1);
        TRACE_LOG("cmd_history_ptr: %p.\n", cmd_history_ptr);

        if (cmd_history_index > 0) {
          input_size = strlen((char*)cmd_history_ptr);
          input_index = input_size;

          for (i=0; i<=input_size; i++) {
            input_buffer[i] = zscii_input_char_to_z_ucs(*(cmd_history_ptr++));
          }

        }
        else {
          input_size = 0;
          input_index = 0;
          input_buffer[0] = 0;
          z_windows[active_z_window_id]->xcursorpos = input_x;
          z_windows[active_z_window_id]->last_gylphs_xcursorpos = -1;
          z_windows[active_z_window_id]->rightmost_filled_xpos = input_x;
          clear_to_eol(active_z_window_id);
        }

        refresh_input_line(true);
        screen_pixel_interface->update_screen();
      }
      else if (event_type == EVENT_WAS_WINCH) {
        TRACE_LOG("winch.\n");
        new_pixel_screen_size(
            screen_pixel_interface->get_screen_height_in_pixels(),
            screen_pixel_interface->get_screen_width_in_pixels());
      }
      else if (event_type == EVENT_WAS_CODE_CTRL_A) {
        if (input_index > 0) {
          input_index = 0;
          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if (event_type == EVENT_WAS_CODE_CTRL_E) {
        if (input_index < input_size) {
          input_index = input_size;
          refresh_input_line(true);
          screen_pixel_interface->update_screen();
        }
      }
      else if (event_type == EVENT_WAS_CODE_CTRL_L) {
        TRACE_LOG("Got CTRL-L.\n");
        refresh_screen();
      }
      else if (event_type == EVENT_WAS_CODE_ESC) {
        if (return_on_escape == true) {
          input_in_progress = false;
          input_size = -2;
        }
      }
    }

    TRACE_LOG("readline-ycursorpos: %d.\n", z_windows[0]->ycursorpos);

    TRACE_LOG("current input_buffer: \"");
    TRACE_LOG_Z_UCS(input_buffer);
    TRACE_LOG("\".\n");
  }
  TRACE_LOG("x-readline-xcursorpos: %d.\n", z_windows[0]->xcursorpos);
  TRACE_LOG("x-readline-yursorpos: %d.\n", z_windows[0]->ycursorpos);
  TRACE_LOG("x-readline-rightmost: %d.\n", z_windows[0]->rightmost_filled_xpos);

  /*
  //screen_pixel_interface->goto_yx(input_y, input_x);
  screen_pixel_interface->clear_to_eol();
  z_windows[active_z_window_id]->xcursorpos
    = input_x - (z_windows[active_z_window_id]->xpos - 1);
  //refresh_cursor(active_z_window_id);
  */

  //refresh_input_line(false);

  z_windows[0]->ycursorpos = *current_input_y - z_windows[0]->ypos;
  z_windows[0]->xcursorpos = *current_input_x - z_windows[0]->xpos
    - z_windows[0]->leftmargin;
  z_windows[0]->rightmost_filled_xpos = input_rightmost_x;
  z_windows[0]->last_gylphs_xcursorpos = -1;

  clear_input_line();

  input_line_on_screen = false;
  nof_input_lines = 0;

  for (i=0; i<input_size; i++) {
    TRACE_LOG("converting:%c\n", input_buffer[i]);
    dest[i] = unicode_char_to_zscii_input_char(input_buffer[i]);
  }

  TRACE_LOG("len:%d\n", input_size);
  TRACE_LOG("after-readline-ycursorpos: %d.\n", z_windows[0]->ycursorpos);
  return input_size;
}


static int read_char(uint16_t tenth_seconds, uint32_t verification_routine,
    int *tenth_seconds_elapsed) {
  bool input_in_progress = true;
  int event_type;
  int timeout_millis = -1;
  z_ucs input;
  zscii result;
  //int i;
  int current_tenth_seconds = 0;
  int timed_routine_retval;
  int i;

  flush_all_buffered_windows();
  refresh_scrollbar();
  for (i=0; i<nof_active_z_windows; i++) {
    if (i != measurement_window_id) {
      z_windows[i]->nof_consecutive_lines_output = 0;
    }
  }
  screen_pixel_interface->update_screen();

  if (winch_found == true) {
    new_pixel_screen_size(
        screen_pixel_interface->get_screen_height_in_pixels(),
        screen_pixel_interface->get_screen_width_in_pixels());
    winch_found = false;
  }

  //screen_pixel_interface->update_screen();

  if ((tenth_seconds != 0) && (verification_routine != 0))
  {
    TRACE_LOG("timed input in read_line every %d tenth seconds.\n",
        tenth_seconds);

    timed_input_active = true;

    timeout_millis = (is_timed_keyboard_input_available() == true ? 100 : 0);

    if (tenth_seconds_elapsed != NULL)
      *tenth_seconds_elapsed = 0;
  }
  else
    timed_input_active = false;

  nof_input_lines = 1;

  while (input_in_progress == true)
  {
    event_type = get_next_event_wrapper(&input, timeout_millis);
    //printf("event: %d\n", event_type);

    if ( (event_type == EVENT_WAS_CODE_PAGE_UP)
        || (event_type == EVENT_WAS_CODE_PAGE_DOWN)) {
      handle_scrolling(event_type);
    }
    else {
      if (top_upscroll_line != -1) {
        // End up-scroll.
        end_screen_redraw();
        refresh_screen();
      }

      if (event_type == EVENT_WAS_INPUT) {
        if (input == 12) {
          TRACE_LOG("Got CTRL-L.\n");
          screen_pixel_interface->redraw_screen_from_scratch();
        }
        else {
          //printf("input:%d\n", input);
          result = unicode_char_to_zscii_input_char(input);

          if (result != 0xff)
            input_in_progress = false;
        }
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_UP) {
        result = 129;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_DOWN) {
        result = 130;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_LEFT) {
        result = 131;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_RIGHT) {
        result = 132;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_CODE_BACKSPACE)
      {
        result = 8;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_CODE_DELETE)
      {
        result = 127;
        input_in_progress = false;
      }
      else if (event_type == EVENT_WAS_TIMEOUT) {
        TRACE_LOG("timeout found.\n");

        if (timed_input_active == true) {
          current_tenth_seconds++;
          if (tenth_seconds_elapsed != NULL)
            (*tenth_seconds_elapsed)++;

          if (current_tenth_seconds == tenth_seconds) {
            current_tenth_seconds = 0;
            stream_output_has_occured = false;

            TRACE_LOG("calling timed-input-routine at %x.\n",
                verification_routine);
            timed_routine_retval = interpret_from_call(verification_routine);
            TRACE_LOG("timed-input-routine finished.\n");

            if (terminate_interpreter != INTERPRETER_QUIT_NONE) {
              TRACE_LOG("Quitting after verification.\n");
              input_in_progress = false;
              result = 0;
            }
            else {
              if (stream_output_has_occured == true) {
                flush_all_buffered_windows();
                screen_pixel_interface->update_screen();
              }

              if (timed_routine_retval != 0) {
                input_in_progress = false;
                result = 0;
              }
            }
          }
        }
      }
      else if (event_type == EVENT_WAS_WINCH) {
        TRACE_LOG("winch.\n");
        new_pixel_screen_size(
            screen_pixel_interface->get_screen_height_in_pixels(),
            screen_pixel_interface->get_screen_width_in_pixels());
      }
    }
  }

  nof_input_lines = 0;

  return result;
}


static void set_window(int16_t window_number) {
  if ( (window_number >= 0)
      && (window_number <=
        nof_active_z_windows - (statusline_window_id >= 0 ? 1 : 0))) {
    if ((ver != 6) && (window_number == 1)) {
      z_windows[1]->ycursorpos = 0;
      reset_xcursorpos(1);
    }

    switch_to_window(window_number);
  }
}


static void set_cursor(int16_t line, int16_t column, int16_t window_number) {
  int pixel_line, pixel_column;

  if (bool_equal(z_windows[window_number]->buffering, true))
    flush_window(window_number);

  // Move cursor in the current window to the position (x,y) (in units)
  // relative to (1,1) in the top left [...] Also, if the cursor would lie
  // outside the current margin settings, it is moved to the left margin
  // of the current line.)

  if ((column < 1) || (line == 0))
    return;
  else if (line < 0) {
    if (ver < 6)
      return;
    else {
      if (line == -1)
        screen_pixel_interface->set_cursor_visibility(false);
      else if (line == -2)
        screen_pixel_interface->set_cursor_visibility(true);
      else
        return;
    }
  }
  else {
    line--;
    column--;

    TRACE_LOG("set_cursor: %d %d %d %d\n",
        line, column, window_number, z_windows[window_number]->ysize);

    // 8.7.2.3: When the upper window is selected, its cursor position can
    // be moved with set_cursor.
    if (window_number == 1) {
      pixel_line = line * line_height;
      pixel_column = column * fixed_width_char_width;

      z_windows[window_number]->ycursorpos
        = pixel_line > (z_windows[window_number]->ysize - line_height)
        ? (z_windows[window_number]->ysize - line_height >= 0
            ? z_windows[window_number]->ysize - line_height
            : 0)
        : pixel_line;

      z_windows[window_number]->xcursorpos
        = pixel_column > z_windows[window_number]->xsize
        ? (z_windows[window_number]->xsize - pixel_column > 0
            ? z_windows[window_number]->xsize - pixel_column
            : 0)
        : pixel_column;

      z_windows[window_number]->last_gylphs_xcursorpos = -1;
      z_windows[window_number]->rightmost_filled_xpos
        = z_windows[window_number]->xcursorpos;

      TRACE_LOG("New xcursorpos: %d, ycursorpos: %d for window 1.\n",
          z_windows[window_number]->xcursorpos,
          z_windows[window_number]->ycursorpos);
    }
  }
}


static uint16_t get_cursor_row() {
  return (z_windows[active_z_window_id]->ypos - 1)
    + (z_windows[active_z_window_id]->ycursorpos - 1)
    + 1;
}


static uint16_t get_cursor_column() {
  return (z_windows[active_z_window_id]->xpos - 1)
    + (z_windows[active_z_window_id]->xcursorpos - 1)
    + 1;
}


static void erase_line_value(uint16_t UNUSED(start_position)) {
}


static void erase_line_pixels(uint16_t UNUSED(start_position)) {
}


static void output_interface_info() {
  screen_pixel_interface->output_interface_info();

  i18n_translate(
      libpixelif_module_name,
      i18n_libpixelif_LIBPIXELINTERFACE_VERSION_P0S,
      LIBPIXELINTERFACE_VERSION);
  streams_latin1_output("\n");
}


static bool input_must_be_repeated_by_story() {
  return true;
}


static void game_was_restored_and_history_modified() {
  TRACE_LOG("Setting history_is_being_remeasured to true.\n");
  history_has_to_be_remeasured();
  if (interface_open == true) {
    refresh_screen();
  }
}


static int prompt_for_filename(char *UNUSED(filename_suggestion),
    z_file **UNUSED(result_file), char *UNUSED(directory),
    int UNUSED(filetype_or_mode), int UNUSED(fileaccess)) {
  return -3;
}


static struct z_screen_interface z_pixel_interface = {
  &get_interface_name,
  &is_status_line_available,
  &is_split_screen_available,
  &is_variable_pitch_font_default,
  &is_colour_available,
  &is_picture_displaying_available,
  &is_bold_face_available,
  &is_italic_available,
  &is_fixed_space_font_available,
  &is_timed_keyboard_input_available,
  &is_preloaded_input_available,
  &is_character_graphics_font_availiable,
  &is_picture_font_availiable,
  &get_screen_height_in_lines,
  &get_screen_width_in_characters,
  &get_screen_width_in_units,
  &get_screen_height_in_units,
  &get_font_width_in_units,
  &get_font_height_in_units,
  &get_default_foreground_colour,
  &get_default_background_colour,
  &get_total_width_in_pixels_of_text_sent_to_output_stream_3,
  &parse_config_parameter,
  &get_config_value,
  &get_config_option_names,
  &link_interface_to_story,
  &reset_interface,
  &pixel_close_interface,
  &set_buffer_mode,
  &z_ucs_output,
  &read_line,
  &read_char,
  &show_status,
  &set_text_style,
  &set_colour,
  &set_font,
  &split_window,
  &set_window,
  &erase_window,
  &set_cursor,
  &get_cursor_row,
  &get_cursor_column,
  &erase_line_value,
  &erase_line_pixels,
  &output_interface_info,
  &input_must_be_repeated_by_story,
  &game_was_restored_and_history_modified,
  &prompt_for_filename,
  NULL,
  NULL
};


static void pixelif_paragraph_attribute_function(int *parameter1,
 int *parameter2) {
  TRACE_LOG("interface_open: %d.\n", interface_open);
  if (interface_open == true) {
    //printf("paragraph_attribute_function invoked, returning %d / %d.\n",
    //    z_windows[active_z_window_id]->nof_lines_in_current_paragraph,
    //    z_windows[0]->xsize);
    TRACE_LOG("paragraph_attribute_function invoked, returning %d / %d.\n",
        z_windows[active_z_window_id]->nof_lines_in_current_paragraph,
        z_windows[0]->xsize);
    //printf("add: %d\n",
    //    z_windows[active_z_window_id]->nof_lines_in_current_paragraph);
    *parameter1 = z_windows[active_z_window_id]->nof_lines_in_current_paragraph;
    *parameter2 = z_windows[0]->xsize;
    total_nof_lines_stored += *parameter1;

    //printf("Resetting nof_lines_in_current_paragraph.\n");
    z_windows[active_z_window_id]->nof_lines_in_current_paragraph = 0;

    return;
  }
}


static void pixelif_paragraph_removal_function(int parameter1,
    int parameter2) {
  //printf("remove: %d\n", parameter1);
  total_lines_in_history -= parameter1;
  //printf("total_lines_in_history: %ld.\n", total_lines_in_history);
  return;
}


void fizmo_register_screen_pixel_interface(struct z_screen_pixel_interface
    *new_screen_pixel_interface) {
  if (screen_pixel_interface == NULL) {
    TRACE_LOG("Registering screen pixel interface at %p.\n",
        new_screen_pixel_interface);

    screen_pixel_interface = new_screen_pixel_interface;
    set_configuration_value("enable-font3-conversion", "true");

    fizmo_register_screen_interface(&z_pixel_interface);

    fizmo_register_paragraph_attribute_function(
        &pixelif_paragraph_attribute_function);

    fizmo_register_paragraph_removal_function(
        &pixelif_paragraph_removal_function);

    set_configuration_value("flush-output-on-newline", "true");
  }
}


void set_custom_left_pixel_margin(int width) {
  //printf("custom\n");
  //custom_left_margin = (width > 0 ? width * fixed_width_char_width : 0);
  custom_left_margin = (width > 0 ? width * 8 : 0);
}


void set_custom_right_pixel_margin(int width) {
  //printf("custom\n");
  //custom_right_margin = (width > 0 ? width * fixed_width_char_width : 0);
  custom_right_margin = (width > 0 ? width * 8 : 0);
}


// This function will redraw the screen on a resize.
void new_pixel_screen_size(int newysize, int newxsize) {
  int i, dy, status_offset = statusline_window_id > 0 ? line_height : 0;
  int consecutive_lines_buffer[nof_active_z_windows];

  if ( (newysize < 1) || (newxsize < 1) )
    return;

  // End up-scroll.
  end_screen_redraw();

  TRACE_LOG("Setting history_is_being_remeasured to true.\n");
  history_has_to_be_remeasured();

  for (i=0; i<nof_active_z_windows; i++) {
    consecutive_lines_buffer[i] = z_windows[i]->nof_consecutive_lines_output;
  }
  disable_more_prompt = true;

  dy = newysize - screen_height_in_pixel;

  total_screen_width_in_pixel = newxsize;
  screen_width_without_scrollbar = newxsize - scrollbar_width;
  screen_height_in_pixel = newysize;
  update_fixed_width_char_width();

  fizmo_new_screen_size(
      get_screen_width_in_characters(),
      get_screen_height_in_lines());

  //printf("new pixel-window-size: %d*%d.\n",
  //    total_screen_width_in_pixel, screen_height_in_pixel);
  TRACE_LOG("new pixel-window-size: %d*%d.\n",
      total_screen_width_in_pixel, screen_height_in_pixel);

  z_windows[1]->ysize = last_split_window_size * line_height;
  if (last_split_window_size > newysize - status_offset)
    z_windows[1]->ysize = newysize - status_offset;

  // Crop cursor positions and windows to new screen size
  for (i=0; i<nof_active_z_windows; i++) {
    // Expand window 0 to new screensize for version != 6.
    if (ver != 6) {
      if (i == 0) {
        if (z_windows[0]->xsize < screen_width_without_scrollbar)
          z_windows[0]->xsize = screen_width_without_scrollbar;

        z_windows[0]->ysize
          = screen_height_in_pixel - status_offset - z_windows[1]->ysize;

        z_windows[0]->ycursorpos += dy;
      }
      else if (i == 1) {
        if (z_windows[1]->xsize != screen_width_without_scrollbar)
          z_windows[1]->xsize = screen_width_without_scrollbar;
      }
      else if (i == statusline_window_id) {
        z_windows[statusline_window_id]->xsize = screen_width_without_scrollbar;
      }
    }

    if (z_windows[i]->ypos > screen_height_in_pixel - 1)
      z_windows[i]->ypos = screen_height_in_pixel - 1;

    if (z_windows[i]->xpos > screen_width_without_scrollbar - 1)
      z_windows[i]->xpos = screen_width_without_scrollbar - 1;

    if (z_windows[i]->ypos + z_windows[i]->ysize > screen_height_in_pixel)
      z_windows[i]->ysize = screen_height_in_pixel - z_windows[i]->ypos;

    if (z_windows[i]->xpos + z_windows[i]->xsize
        > screen_width_without_scrollbar) {
      z_windows[i]->xsize
        = screen_width_without_scrollbar - z_windows[i]->xpos;

      if (z_windows[i]->xsize - z_windows[i]->leftmargin
          - z_windows[i]->rightmargin < 1) {
        z_windows[i]->leftmargin = 0;
        z_windows[i]->rightmargin = 0;
      }
    }

    TRACE_LOG("New line length for window %i: %d.\n", i,
        z_windows[i]->xsize - z_windows[i]->rightmargin
        - z_windows[i]->leftmargin);
    freetype_wordwrap_adjust_line_length(
        z_windows[i]->wordwrapper,
        z_windows[i]->xsize - z_windows[i]->rightmargin
        - z_windows[i]->leftmargin);

    if (z_windows[i]->ycursorpos + 2*line_height
        > z_windows[i]->ysize - z_windows[i]->lower_padding) {
      z_windows[i]->ycursorpos
        = z_windows[i]->ysize - line_height - z_windows[i]->lower_padding;
      if (z_windows[i]->ycursorpos < 0)
        z_windows[i]->ycursorpos = 0;
      TRACE_LOG("new ycursorpos[%d]: %d\n", i, z_windows[i]->ycursorpos);
    }

    if (z_windows[i]->xcursorpos > z_windows[i]->xsize - fixed_width_char_width
        - z_windows[i]->rightmargin - z_windows[i]->rightmargin) {
      z_windows[i]->xcursorpos = z_windows[i]->xsize - fixed_width_char_width
        - z_windows[i]->rightmargin - z_windows[i]->rightmargin;
    }
  }

  refresh_screen();

  for (i=0; i<nof_active_z_windows; i++) {
    z_windows[i]->nof_consecutive_lines_output = consecutive_lines_buffer[i];
  }
  disable_more_prompt = false;
}


char *get_screen_pixel_interface_version() {
  return screen_pixel_interface_version;
}

