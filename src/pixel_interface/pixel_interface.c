
/* pixel_interface.c
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
static true_type_factory *font_factory;
static int screen_height_in_pixel = -1;
static int screen_width_in_pixel = -1;
static int nof_active_z_windows = 0;
static int statusline_window_id = -1;
static int custom_left_margin = 8;
static int custom_right_margin = 8;
static bool hyphenation_enabled = true;
static bool using_colors = false;
static bool color_disabled = false;
static bool disable_more_prompt = false;
static z_ucs *libpixelif_more_prompt;
static z_ucs *libpixelif_score_string;
static z_ucs *libpixelif_turns_string;
static int libpixelif_right_status_min_size;
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
static true_type_font *regular_font;
static true_type_font *italic_font;
static true_type_font *bold_font;
static true_type_font *bold_italic_font;
static true_type_font *fixed_regular_font;
static true_type_font *fixed_italic_font;
static true_type_font *fixed_bold_font;
static true_type_font *fixed_bold_italic_font;
static int line_height;
static int fixed_width_char_width = 8;
static true_type_wordwrapper *preloaded_wordwrapper;

// Scrolling upwards:
// It is always assumed that there's no output to window[0] and it's
// corresponding outputhistory as long as upscrolling is active. As
// soon as any output is received or the window size is changed, upscroling
// is terminated and the screen returns to the bottom of the output -- this
// also automatically shows the new output which has arrived, which should in
// most cases result from a interrupt routine.
static int top_upscroll_line = -1; // != -1 indicates upscroll is active.
static bool upscroll_hit_top = false;
static history_output *history; // shared by upscroll and screen-refresh
//static int history_screen_line, last_history_screen_line;

// This flag is set to true when an read_line is currently underway. It's
// used by screen refresh functions like "new_pixel_screen_size".
static bool input_line_on_screen = false;
static int nof_input_lines = 0;
static z_ucs *current_input_buffer = NULL;
//static z_ucs newline_string[] = { '\n', 0 };

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
static int font_height_in_pixel = 14;
static char last_font_size_config_value_as_string[MAX_VALUE_AS_STRING_LEN];

//static z_ucs newline_string[] = { Z_UCS_NEWLINE, 0 };

static char *config_option_names[] = {
  "left-margin", "right-margin", "disable-hyphenation", "disable-color",
  "regular-font", "italic-font", "bold-font", "bold-italic-font",
  "fixed-regular-font", "fixed-italic-font", "fixed-bold-font",
  "fixed-bold-italic-font",
  "font-search-path", "font-size", NULL };


  /*
static void refresh_cursor(int UNUSED(window_id)) {
  TRACE_LOG("refresh_cursor for win %d:%d/%d/%d/%d\n",
      window_id,
      z_windows[window_id]->ypos, z_windows[window_id]->ycursorpos,
      z_windows[window_id]->xpos, z_windows[window_id]->xcursorpos);

  screen_pixel_interface->goto_yx(
      z_windows[window_id]->ypos + z_windows[window_id]->ycursorpos - 1,
      z_windows[window_id]->xpos + z_windows[window_id]->xcursorpos - 1);
}
  */

static int draw_glyph_string(z_ucs *z_ucs_output, int window_number,
    true_type_font *font);


static void clear_to_eol(int window_number) {
  int width, yspace, height;

  // Clear the remaining space on the line. This also ensures that the
  // background color for the line will be the current output_color.

  //printf("finalize: %d.\n", z_windows[window_number]->xpos
  //+ z_windows[window_number]->rightmost_filled_xpos - 1);

  width
    = z_windows[window_number]->xsize
    - z_windows[window_number]->rightmost_filled_xpos;

  yspace
    = z_windows[window_number]->ysize
    - z_windows[window_number]->lower_padding
    - z_windows[window_number]->ycursorpos;

  height
    = yspace < line_height
    ? yspace
    : line_height;

  /*
  printf("clear-to-eol: %d, %d, %d, %d.\n",
      z_windows[window_number]->xpos
      + z_windows[window_number]->rightmost_filled_xpos,
      z_windows[window_number]->ypos
      + z_windows[window_number]->ycursorpos,
      width,
      height);
  */

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
  freetype_wordwrap_flush_output(z_windows[window_number]->wordwrapper);
  clear_to_eol(window_number);
}


// Will return true in case winch event was encountered during "more" prompt,
// false otherwise.
static bool break_line(int window_number) {
  z_ucs input, event_type;
  int pixels_to_scroll_up, i;

  //printf("Breaking line.\n");
  TRACE_LOG("Breaking line.\n");

  clear_to_eol(window_number);

  pixels_to_scroll_up
    = z_windows[window_number]->ycursorpos + 2*line_height
    + z_windows[window_number]->lower_padding
    - z_windows[window_number]->ysize;
  //printf("pixels to scroll up: %d\n", pixels_to_scroll_up);

  if (window_number == 1) {
    if (pixels_to_scroll_up <= 0) {
      z_windows[window_number]->ycursorpos += line_height;
      z_windows[window_number]->xcursorpos = 0;
    }
    else {
      // 8.6.2 The upper window should never be scrolled: it is legal for
      // character to be printed on the bottom right position of the upper
      // window (but the position of the cursor after this operation is
      // undefined: the author suggests that it stay put).
    }
  }
  else {
    z_windows[window_number]->xcursorpos = 0;

    if (pixels_to_scroll_up > 0) {
      //printf("scrolling line up by %d pixels.\n", pixels_to_scroll_up);
      TRACE_LOG("scrolling line up by %d pixels.\n", pixels_to_scroll_up);

      /*
         printf("copy-area: %d/%d to %d/%d, %d x %d\n",
         z_windows[window_number]->ypos + pixels_to_scroll_up,
         z_windows[window_number]->xpos,
         z_windows[window_number]->ypos,
         z_windows[window_number]->xpos,
         z_windows[window_number]->xsize,
         z_windows[window_number]->ysize - pixels_to_scroll_up
         - z_windows[window_number]->lower_padding);
         */

      screen_pixel_interface->copy_area(
          z_windows[window_number]->ypos,
          z_windows[window_number]->xpos,
          z_windows[window_number]->ypos + pixels_to_scroll_up,
          z_windows[window_number]->xpos,
          z_windows[window_number]->ysize - pixels_to_scroll_up
          - z_windows[window_number]->lower_padding,
          z_windows[window_number]->xsize);

      /*
      // begin QUARTZ_WM_WORKAROUND
      screen_pixel_interface->fill_area(
      z_windows[window_number]->xpos
      + z_windows[window_number]->xsize
      - z_windows[window_number]->rightmargin
      +1,
      z_windows[window_number]->ypos
      + z_windows[window_number]->ysize - line_height,
      z_windows[window_number]->rightmargin,
      line_height,
      z_to_rgb_colour(z_windows[window_number]->output_background_colour));
      // end ENABLE_QUARTZ_WM_WORKAROUND
      */

      /*
      printf("%d, %d, %d\n", z_windows[window_number]->ycursorpos,
          z_windows[window_number]->ysize,
          z_windows[window_number]->lower_padding);
      */
      z_windows[window_number]->ycursorpos
        = z_windows[window_number]->ysize - line_height
        - z_windows[window_number]->lower_padding;
    }
    else {
      //printf("breaking line, moving cursor downm no scrolling.\n");
      TRACE_LOG("breaking line, moving cursor downm no scrolling.\n");
      z_windows[window_number]->ycursorpos += line_height;
    }
    TRACE_LOG("new y-cursorpos: %d\n", z_windows[window_number]->ycursorpos);
    //printf("new y-cursorpos: %d\n", z_windows[window_number]->ycursorpos);
  }

  z_windows[window_number]->last_gylphs_xcursorpos = -1;
  z_windows[window_number]->rightmost_filled_xpos
    = z_windows[window_number]->xcursorpos;

  nof_break_line_invocations++;
  z_windows[window_number]->nof_consecutive_lines_output++;

  TRACE_LOG("consecutive lines: %d.\n",
      z_windows[window_number]->nof_consecutive_lines_output);

  // FIXME: Implement height 255
  if ( (z_windows[window_number]->nof_consecutive_lines_output
        >= (screen_height_in_pixel / line_height) - 1)
      && (disable_more_prompt == false)
      && (winch_found == false)) {
    TRACE_LOG("Displaying more prompt.\n");
    //printf("Displaying more prompt.\n");

    // Loop below will result in recursive "z_ucs_output_window_target"
    // call. Dangerous?
    for (i=0; i<nof_active_z_windows; i++)
      if ( (i != window_number)
          && (bool_equal(z_windows[i]->buffering, true))) {
        flush_window(i);
      }

    //screen_pixel_interface->z_ucs_output(libpixelif_more_prompt);
    draw_glyph_string(
        libpixelif_more_prompt,
        window_number,
        z_windows[window_number]->output_true_type_font);
    clear_to_eol(0);

    // In case we're just starting the story and there's no status line yet:
    if (ver <= 3)
      display_status_line();

    screen_pixel_interface->update_screen();
    //refresh_cursor(window_number);

    // FIXME: Check for sound interrupt?
    do {
      event_type = screen_pixel_interface->get_next_event(&input, 0);
      if (event_type == EVENT_WAS_TIMEOUT) {
        TRACE_LOG("timeout.\n");
      }
    }
    while (event_type == EVENT_WAS_TIMEOUT);

    z_windows[window_number]->xcursorpos
      = z_windows[window_number]->leftmargin;
    //refresh_cursor(window_number);
    //screen_pixel_interface->clear_to_eol();
    screen_pixel_interface->fill_area(
        z_windows[window_number]->xpos,
        z_windows[window_number]->ypos
        + z_windows[window_number]->ysize - line_height,
        z_windows[window_number]->xsize,
        line_height,
        z_to_rgb_colour(z_windows[window_number]->output_background_colour));

    if (event_type == EVENT_WAS_WINCH)
    {
      winch_found = true;
      // The more prompt was "interrupted" by a window screen size
      // change. We'll now have to initiate a redraw. Since a redraw
      // is always based on the history, which is not synced to the
      // output this function receives, we'll now forget about the
      // current output and then initiate a redraw from before the
      // next input.
      return true;
    }

    z_windows[window_number]->nof_consecutive_lines_output = 0;
    TRACE_LOG("more prompt finished: %d.\n", event_type);
  }

  // Start new line: Fill right margin with background colour.
  screen_pixel_interface->fill_area(
      z_windows[window_number]->xpos,
      z_windows[window_number]->ypos + z_windows[window_number]->ycursorpos,
      z_windows[window_number]->leftmargin,
      line_height,
      z_to_rgb_colour(z_windows[window_number]->output_background_colour));

  return false;
}


static int draw_glyph(z_ucs charcode, int window_number,
    true_type_font *font) {
  int x, y, x_max, advance, bitmap_width, result = 0;
  bool reverse = false;
  z_rgb_colour foreground_colour, background_colour;

  if (charcode == Z_UCS_NEWLINE) {
    break_line(window_number);
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

  //advance = tt_get_glyph_advance(font, charcode, 0);
  tt_get_glyph_size(font, charcode, &advance, &bitmap_width);

  TRACE_LOG("Max draw glyph size: %d.\n",
      z_windows[window_number]->xsize - z_windows[window_number]->rightmargin);

  TRACE_LOG(
      "leftmargin:%d, xcursorpos:%d, advance:%d, xsize:%d, rightmargin:%d\n",
      z_windows[window_number]->leftmargin,
      z_windows[window_number]->xcursorpos,
      advance,
      z_windows[window_number]->xsize,
      z_windows[window_number]->rightmargin);

  if (z_windows[window_number]->leftmargin
      + z_windows[window_number]->xcursorpos
      + bitmap_width
      > z_windows[window_number]->xsize
      - z_windows[window_number]->rightmargin) {
    break_line(window_number);
    result = 1;
  }

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
  z_windows[window_number]->rightmost_filled_xpos
    = tt_draw_glyph(
        font,
        x,
        y,
        x_max,
        reverse == false ? foreground_colour : background_colour,
        reverse == false ? background_colour : foreground_colour,
        screen_pixel_interface,
        charcode,
        &z_windows[window_number]->last_gylphs_xcursorpos);

  z_windows[window_number]->xcursorpos
    += advance;

  //printf("advance: %d, xpos: %d, char: %c\n", advance, z_windows[window_number]->xcursorpos, charcode);

  return result;
}


static int draw_glyph_string(z_ucs *z_ucs_output, int window_number,
    true_type_font *font) {
  int result = 0;

  /*
  int i = 0;
  printf("--- (at y:%d, x:%d)\n", z_windows[window_number]->ycursorpos,
      z_windows[window_number]->xcursorpos);
  while (z_ucs_output[i]) {
    printf("%c", z_ucs_output[i]);
    i++;
  }
  printf("\n---\n");
  */

  while (*z_ucs_output) {
    result += draw_glyph(*z_ucs_output, window_number, font);
    z_ucs_output++;
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


static void switch_to_window(int window_id) {
  TRACE_LOG("Switching to window %d.\n", window_id);

  active_z_window_id = window_id;
  //refresh_cursor(active_z_window_id);
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
  draw_glyph_string(
      z_ucs_output,
      window_number,
      z_windows[window_number]->output_true_type_font);

  /*
  z_ucs input, event_type;
  z_ucs buf = 0; // init to 0 to calm compiler.
  z_ucs *linebreak;
  int space_on_line, i;

  if (*z_ucs_output == 0)
    return;

  update_output_colours(window_number);
  //refresh_cursor(window_number);

  TRACE_LOG("output-window-target, win %d, %dx%d.\n",
      window_number,
      z_windows[window_number]->xsize,
      z_windows[window_number]->ysize);

  while (*z_ucs_output != 0)
  {
    TRACE_LOG("Output queue: \"");
    TRACE_LOG_Z_UCS(z_ucs_output);
    TRACE_LOG("\".\n");

    space_on_line = z_windows[window_number]->xsize
      -  z_windows[window_number]->rightmargin
      - (z_windows[window_number]->xcursorpos - 1);

    TRACE_LOG("space on line: %d, xcursorpos: %d.\n",
        space_on_line, z_windows[window_number]->xcursorpos - 1);

    // Find a suitable spot to break the line. Check if data contains some
    // newline char.
    linebreak = z_ucs_chr(z_ucs_output, Z_UCS_NEWLINE);

    // In case we cannot put anymore on this line anyway, simple advance
    // to the next newline or finish output.
    if ( (space_on_line <= 0)
        && (z_windows[window_number]->wrapping == false) )
    {
      // If wrapping is not allowed and there's either no newline found or
      // we're at the bottom of the window, simply quit.
      if ( (linebreak == NULL) 
          ||
          (z_windows[window_number]->ycursorpos
           == z_windows[window_number]->ysize) )
        return;

      // Else we can advance to the next line.
      z_ucs_output = linebreak + 1;
      z_windows[window_number]->xcursorpos
        = 1 + z_windows[window_number]->leftmargin;
      z_windows[window_number]->ycursorpos++;
      continue;
    }

    if (space_on_line < 0)
    {
      // This may happen in case the cursor moved off the right side of
      // a non-wrapping window.
      space_on_line = 0;
    }

    // In case no newline was found or in case the found newline is too far
    // away to fit on the current line ...
    if ( (linebreak == NULL) || (linebreak - z_ucs_output > space_on_line) )
    {
      // ... test if the rest of the line fits on screen. If it doesn't
      // we'll put as much on the line as possible and break after that.

      linebreak 
        = (signed)z_ucs_len(z_ucs_output) > space_on_line
        ? z_ucs_output + space_on_line
        : NULL;
    }

    // Direct output of the current line including the newline char does
    // not work if margins are used and probably (untested) if a window
    // is not at the left side.

    // In case we're breaking in the middle of the line, we'll have to
    // put a '\0' there and store the original data in "buf".
    if (linebreak != NULL)
    {
      TRACE_LOG("linebreak found.\n");
      buf = *linebreak;
      *linebreak = 0;
      TRACE_LOG("buf: %d\n", buf);
    }

    TRACE_LOG("Output at %d/%d:\"",
        z_windows[window_number]->xcursorpos,
        z_windows[window_number]->ycursorpos);
    //refresh_cursor(window_number);
    TRACE_LOG_Z_UCS(z_ucs_output);

    TRACE_LOG("\".\n");

    // Output data as far space on the line permits.
    screen_pixel_interface->z_ucs_output(z_ucs_output);
    z_windows[window_number]->xcursorpos += z_ucs_len(z_ucs_output);

    if (linebreak != NULL)
    {
      // At the end of the line
      TRACE_LOG("At end of line.\n");

      // At this point we know we've just finalized one line and
      // have to enter a new one.

      // In order to keep clean margins, remove current style.
      screen_pixel_interface->set_text_style(0);

      if ( (z_windows[window_number]->ycursorpos
            == z_windows[window_number]->ysize)
          && (z_windows[window_number]->wrapping == true) )
      {
        // Due to the if clause regarding wrapping above, at this point we
        // now we're allowed to scroll at the bottom of the window.
        screen_pixel_interface->copy_area(
            z_windows[window_number]->ypos,
            z_windows[window_number]->xpos,
            z_windows[window_number]->ypos+1,
            z_windows[window_number]->xpos,
            z_windows[window_number]->ysize-1,
            z_windows[window_number]->xsize);
      }
      else
      { 
        // If we're not at the bottom of the window, simply move to next line.
        z_windows[window_number]->ycursorpos++;
      }

      // Clear line, including left margin, to EOL.
      z_windows[window_number]->xcursorpos = 1;
      //refresh_cursor(window_number);
      screen_pixel_interface->clear_to_eol();

      // Move cursor to right margin and re-activate current style setting.
      z_windows[window_number]->xcursorpos
        = 1 + z_windows[window_number]->leftmargin;
      //refresh_cursor(window_number);
      screen_pixel_interface->set_text_style(
          z_windows[window_number]->output_text_style);

      // Restore char at linebreak and additionally skip newline if
      // required.
      *linebreak = buf;
      z_ucs_output = linebreak;
      if (*z_ucs_output == Z_UCS_NEWLINE)
      {
        TRACE_LOG("newline-skip.\n");
        z_ucs_output++;
      }

      if (z_windows[window_number]->wrapping == true)
      {
        z_windows[window_number]->nof_consecutive_lines_output++;

        TRACE_LOG("consecutive lines: %d.\n",
            z_windows[window_number]->nof_consecutive_lines_output);

        // FIXME: Implement height 255
        if (
            (z_windows[window_number]->nof_consecutive_lines_output
             == z_windows[window_number]->ysize - 1)
            &&
            (disable_more_prompt == false)
            &&
            (winch_found == false)
           )
        {
          TRACE_LOG("Displaying more prompt.\n");

          // Loop below will result in recursive "z_ucs_output_window_target"
          // call. Dangerous?
          for (i=0; i<nof_active_z_windows; i++)
            if (
                (i != window_number)
                &&
                (bool_equal(z_windows[i]->buffering, true))
               )
              flush_window(i);

          screen_pixel_interface->z_ucs_output(libpixelif_more_prompt);
          screen_pixel_interface->update_screen();
          //refresh_cursor(window_number);

          // FIXME: Check for sound interrupt?
          do
          {
            event_type = screen_pixel_interface->get_next_event(&input, 0);

            if (event_type == EVENT_WAS_TIMEOUT)
            {
              TRACE_LOG("timeout.\n");
            }
          }
          while (event_type == EVENT_WAS_TIMEOUT);

          z_windows[window_number]->xcursorpos
            = z_windows[window_number]->leftmargin + 1;
          //refresh_cursor(window_number);
          screen_pixel_interface->clear_to_eol();

          if (event_type == EVENT_WAS_WINCH)
          {
            winch_found = true;
            // The more prompt was "interrupted" by a window screen size
            // change. We'll now have to initiate a redraw. Since a redraw
            // is always based on the history, which is not synced to the
            // output this function receives, we'll now forget about the
            // current output and then initiate a redraw from before the
            // next input.
            break;
          }

          z_windows[window_number]->nof_consecutive_lines_output = 0;
          TRACE_LOG("more prompt finished: %d.\n", event_type);
        }
      }
    }
    else
      z_ucs_output += z_ucs_len(z_ucs_output);
  }

  TRACE_LOG("z_ucs_output_window_target finished.\n");
  */
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
  return screen_width_in_pixel / fixed_width_char_width;
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
  else {
    return screen_pixel_interface->get_config_value(key);
  }
}


static char **get_config_option_names() {
  return config_option_names;
}


static void z_ucs_output(z_ucs *z_ucs_output) {
  TRACE_LOG("Output: \"");
  TRACE_LOG_Z_UCS(z_ucs_output);
  TRACE_LOG("\" to window %d, buffering: %d.\n",
      active_z_window_id,
      active_z_window_id != -1 ? z_windows[active_z_window_id]->buffering : -1);

  /*
  if (((z_windows[active_z_window_id]->fixedfont_forced_by_flags2 == false)
        && (z_mem[0x11] & 2))
      || ((z_windows[active_z_window_id]->fixedfont_forced_by_flags2 == true)
        && ((z_mem[0x11] & 2)) == 0)) {
    // "Game sets to force printing in fixed-pitch font" has changed.
    if ((z_mem[0x11] & 2)) {
      // Fixed-width turned on.
      z_windows[active_z_window_id]->fixedfont_forced_by_flags2 = true;
      TRACE_LOG("fixedfont_forced_by_flags2 turned on.\n");

      if (bool_equal(z_windows[active_z_window_id]->buffering, false)) {
        update_window_true_type_font(active_z_window_id, true);
      }
      else {
        if ((z_windows[active_z_window_id]->current_wrapper_style
              & Z_STYLE_FIXED_PITCH) == 0) {
          freetype_wordwrap_insert_metadata(
              z_windows[active_z_window_id]->wordwrapper,
              &wordwrap_output_style,
              (void*)(&z_windows[active_z_window_id]->window_number),
              (uint32_t)z_windows[active_z_window_id]->current_wrapper_style
              | Z_STYLE_FIXED_PITCH,
              evaluate_font(
                z_windows[active_z_window_id]->current_wrapper_style,
                z_windows[active_z_window_id]->current_wrapper_font,
                true));
        }
      }
    }
    else {
      // Fixed width turned off.
      z_windows[active_z_window_id]->fixedfont_forced_by_flags2 = false;
      TRACE_LOG("fixedfont_forced_by_flags2 turned off.\n");

      if (bool_equal(z_windows[active_z_window_id]->buffering, false)) {
        update_window_true_type_font(active_z_window_id, false);
      }
      else {
        if ((z_windows[active_z_window_id]->current_wrapper_style
              & Z_STYLE_FIXED_PITCH) == 0) {
          freetype_wordwrap_insert_metadata(
              z_windows[active_z_window_id]->wordwrapper,
              &wordwrap_output_style,
              (void*)(&z_windows[active_z_window_id]->window_number),
              (uint32_t)z_windows[active_z_window_id]->current_wrapper_style,
              evaluate_font(
                z_windows[active_z_window_id]->current_wrapper_style,
                z_windows[active_z_window_id]->current_wrapper_font,
                false));
        }
      }
    }
  }
  */

  if (bool_equal(z_windows[active_z_window_id]->buffering, false)) {
    z_ucs_output_window_target(
        z_ucs_output,
        (void*)(&z_windows[active_z_window_id]->window_number));
  }
  else {
    freetype_wrap_z_ucs(
        z_windows[active_z_window_id]->wordwrapper, z_ucs_output);
  }

  TRACE_LOG("z_ucs_output finished.\n");
}


static void update_fixed_width_char_width() {
  int bitmap_width;

  tt_get_glyph_size(
      fixed_regular_font, '0', &fixed_width_char_width, &bitmap_width);

  //printf("fixed_width_char_width: %d\n", fixed_width_char_width);
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
    set_configuration_value("regular-font", "SourceSansPro-Regular.ttf");
    set_configuration_value("italic-font", "SourceSansPro-It.ttf");
    set_configuration_value("bold-font", "SourceSansPro-Bold.ttf");
    set_configuration_value("bold-italic-font", "SourceSansPro-BoldIt.ttf");

    set_configuration_value("fixed-regular-font", "SourceCodePro-Regular.ttf");
    set_configuration_value("fixed-italic-font", "SourceCodePro-Regular.ttf");
    set_configuration_value("fixed-bold-font", "SourceCodePro-Bold.ttf");
    set_configuration_value("fixed-bold-italic-font", "SourceCodePro-Bold.ttf");
  }

  line_height = font_height_in_pixel + 4;

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
  screen_width_in_pixel
    = screen_pixel_interface->get_screen_width_in_pixels();

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

  TRACE_LOG("Number of active windows: %d.\n", nof_active_z_windows);

  bytes_to_allocate = sizeof(struct z_window*) * nof_active_z_windows;

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

    if (i == 0) {
      z_windows[i]->ysize = screen_height_in_pixel;
      z_windows[i]->xsize = screen_width_in_pixel;
      z_windows[i]->scrolling_active = true;
      z_windows[i]->stream2copying_active = true;
      if (ver != 6) {
        z_windows[i]->leftmargin = custom_left_margin;
        z_windows[i]->rightmargin = custom_right_margin;
      }
      if (statusline_window_id > 0) {
        z_windows[i]->ysize -= line_height;
        z_windows[i]->ypos += line_height;
      }
    }
    else {
      z_windows[i]->scrolling_active = false;
      z_windows[i]->stream2copying_active = false;
      z_windows[i]->leftmargin = 0;
      z_windows[i]->rightmargin = 0;

      if (i == 1) {
        z_windows[i]->ysize = 0;
        z_windows[i]->xsize = screen_width_in_pixel;
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
        z_windows[i]->xsize = screen_width_in_pixel;
        //z_windows[i]->text_style = Z_STYLE_REVERSE_VIDEO;
        //z_windows[i]->output_text_style = Z_STYLE_REVERSE_VIDEO;
      }
      else {
        z_windows[i]->ysize = 0;
        z_windows[i]->xsize = 0;
      }
    }

    z_windows[i]->ycursorpos
      = (ver >= 5 ? 0 : (z_windows[i]->ysize - line_height - 4));
    z_windows[i]->xcursorpos = 0;
    z_windows[i]->last_gylphs_xcursorpos = -1;
    z_windows[i]->rightmost_filled_xpos
      = z_windows[i]->xcursorpos;
    z_windows[i]->lower_padding = 4;

    z_windows[i]->newline_routine = 0;
    z_windows[i]->interrupt_countdown = 0;

    if (i == statusline_window_id) {
      z_windows[i]->background_colour = Z_COLOUR_BLACK;
      z_windows[i]->output_background_colour = Z_COLOUR_BLACK;
      z_windows[i]->foreground_colour = Z_COLOUR_WHITE;
      z_windows[i]->output_foreground_colour = Z_COLOUR_WHITE;
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
  }

  active_z_window_id = 0;

  /*
  refresh_wordwrapper = create_true_type_wordwrapper(
      regular_font,
      z_windows[0]->xsize-z_windows[0]->leftmargin-z_windows[0]->rightmargin,
      &z_ucs_output_refresh_destination,
      NULL,
      hyphenation_enabled);
  */

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

  //  -> "Score: x  Turns: x ",
  libpixelif_right_status_min_size
    = z_ucs_len(libpixelif_score_string)
    + z_ucs_len(libpixelif_turns_string)
    + 9; // 5 Spaces, 2 colons, 2 digits.

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
      event_type = screen_pixel_interface->get_next_event(&input, 0);
    while (event_type == EVENT_WAS_WINCH);
  }

  screen_pixel_interface->close_interface(error_message);

  for (i=0; i<nof_active_z_windows; i++) {
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

  tt_destroy_font(regular_font);

  destroy_true_type_factory(font_factory);

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
        z_windows[0]->xcursorpos = 0;
        z_windows[0]->last_gylphs_xcursorpos = -1;
        z_windows[0]->rightmost_filled_xpos
          = z_windows[0]->xcursorpos;
        z_windows[0]->ycursorpos = 0;
        TRACE_LOG("Re-adjusting cursor for lower window.\n");
      }

      if (z_windows[1]->ycursorpos > z_windows[1]->ysize) {
        z_windows[1]->xcursorpos = 0;
        z_windows[1]->last_gylphs_xcursorpos = -1;
        z_windows[1]->rightmost_filled_xpos
          = z_windows[1]->xcursorpos;
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

    last_split_window_size = nof_lines;
  }
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


static void set_text_style(z_style text_style) {
  int i;

  //printf("New text style is %d.\n", text_style);
  TRACE_LOG("New text style is %d.\n", text_style);

  for (i=0; i<nof_active_z_windows; i++) {
    TRACE_LOG("Evaluating style for window %d.\n", i);
    if (bool_equal(z_windows[i]->buffering, false)) {
      if (text_style & Z_STYLE_NONRESET)
        z_windows[i]->output_text_style |= text_style;
      else
        z_windows[i]->output_text_style = text_style;
      TRACE_LOG("Resulting text style is %d.\n",
          z_windows[i]->output_text_style);
      update_window_true_type_font(i);
    }
    else {
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

  if (window_number == -1) {
    index = 0;
    end_index = highest_valid_window_id;
  }
  else if ( (window_number >= 0)
      && (window_number <= highest_valid_window_id)) {
    index = window_number;
    end_index = window_number;
  }
  else
    return;
   
  while (index <= end_index) {
    TRACE_LOG("Processing window %d.\n", index);

    if (bool_equal(z_windows[index]->buffering, false)) {
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
  int i;

  // 8.7.2.4: An interpreter should use a fixed-pitch font when printing
  // on the upper window.

  /*
  if (version < 6) {
    width_if (active_z_window_id
    */

  TRACE_LOG("New font is %d.\n", font);

  for (i=0; i<nof_active_z_windows; i++) {

    if (i != 1) {
      if (bool_equal(z_windows[i]->buffering, false)) {
        z_windows[i]->output_font = font;
        update_window_true_type_font(i);
      }
      else {
        z_windows[i]->current_wrapper_font = font;
        freetype_wordwrap_insert_metadata(
            z_windows[i]->wordwrapper,
            &wordwrap_output_font,
            (void*)(&z_windows[i]->window_number),
            (uint32_t)font,
            evaluate_font(
              z_windows[i]->current_wrapper_style,
              z_windows[i]->current_wrapper_font));
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


static void draw_cursor() {
  screen_pixel_interface->fill_area(
      z_windows[0]->xpos + z_windows[0]->leftmargin
      + z_windows[0]->xcursorpos,
      z_windows[0]->ypos + z_windows[0]->ycursorpos,
      1,
      line_height - 2,
      z_to_rgb_colour(Z_COLOUR_BLUE));
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
  int nof_line_breaks, nof_new_input_lines;
  int last_active_z_window_id = -1;
  if (input_line_on_screen == false)
    return;

  if (active_z_window_id != 0) {
    last_active_z_window_id = active_z_window_id;
    switch_to_window(0);
  }

  /*
  printf("refresh: curx:%d, cury:%d\n", 
      *current_input_x, *current_input_y);
  */
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

  nof_line_breaks = draw_glyph_string(current_input_buffer, 0, bold_font);
  TRACE_LOG("nof_line_breaks: %d\n", nof_line_breaks);
  TRACE_LOG("current_input_y: %d\n", *current_input_y);
  nof_new_input_lines = nof_line_breaks - (nof_input_lines - 1);
  if (nof_new_input_lines > 0) {
    *current_input_y -= line_height * nof_new_input_lines;
    nof_input_lines += nof_new_input_lines;
    TRACE_LOG("current_input_y: %d, nof_input_lines:%d\n",
        *current_input_y, nof_input_lines);
  }


  if (display_cursor == true) {
    draw_cursor();
  }

  if (last_active_z_window_id != -1)
    switch_to_window(last_active_z_window_id);
}


static int number_length(int number) {
  if (number == 0)
    return 1;
  else
    return ((int)log10(abs(number))) + (number < 0 ? 1 : 0) + 1;
}


static void show_status(z_ucs *room_description, int status_line_mode,
    int16_t parameter1, int16_t parameter2) {
  int desc_len = z_ucs_len(room_description);
  int score_length, turn_length, rightside_length, room_desc_space;
  z_ucs rightside_buf_zucs[libpixelif_right_status_min_size + 12];
  z_ucs buf = 0;
  z_ucs *ptr;
  char latin1_buf[8];
  int last_active_z_window_id;
  //int i;
  //z_ucs *ptr2;

  TRACE_LOG("statusline: \"");
  TRACE_LOG_Z_UCS(room_description);
  TRACE_LOG("\".\n");

  TRACE_LOG("statusline-xsize: %d, screen:%d.\n",
      z_windows[statusline_window_id]->xsize, screen_width_in_pixel);

  if (statusline_window_id > 0) {
    z_windows[statusline_window_id]->ycursorpos = 0;
    z_windows[statusline_window_id]->xcursorpos = 0;
    z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
    z_windows[statusline_window_id]->rightmost_filled_xpos
      = z_windows[statusline_window_id]->xcursorpos;

    last_active_z_window_id = active_z_window_id;
    switch_to_window(statusline_window_id);
    erase_window(statusline_window_id);

    if (status_line_mode == SCORE_MODE_SCORE_AND_TURN) {
      score_length = number_length(parameter1);
      turn_length = number_length(parameter2);

      rightside_length
        = libpixelif_right_status_min_size - 2 + score_length + turn_length;

      room_desc_space
        = z_windows[statusline_window_id]->xsize - rightside_length - 3;

      if (room_desc_space < desc_len) {
        buf = room_description[room_desc_space];
        room_description[room_desc_space] = 0;
      }

      z_windows[statusline_window_id]->xcursorpos = 2;
      z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
      z_windows[statusline_window_id]->rightmost_filled_xpos
        = z_windows[statusline_window_id]->xcursorpos;
      //refresh_cursor(statusline_window_id);
      z_ucs_output(room_description);

      z_windows[statusline_window_id]->xcursorpos
        = z_windows[statusline_window_id]->xsize - rightside_length + 1;
      z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
      z_windows[statusline_window_id]->rightmost_filled_xpos
        = z_windows[statusline_window_id]->xcursorpos;
      //refresh_cursor(statusline_window_id);

      ptr = z_ucs_cpy(rightside_buf_zucs, libpixelif_score_string);
      sprintf(latin1_buf, ": %d  ", parameter1);
      ptr = z_ucs_cat_latin1(ptr, latin1_buf);
      ptr = z_ucs_cat(ptr, libpixelif_turns_string);
      sprintf(latin1_buf, ": %d", parameter2);
      ptr = z_ucs_cat_latin1(ptr, latin1_buf);

      z_ucs_output(rightside_buf_zucs);

      if (buf != 0)
        room_description[room_desc_space] = buf;
    }
    else if (status_line_mode == SCORE_MODE_TIME) {
      // FIXME:
      room_desc_space = z_windows[statusline_window_id]->xsize - 8;

      if (room_desc_space < desc_len) {
        buf = room_description[room_desc_space];
        room_description[room_desc_space] = 0;
      }

      z_windows[statusline_window_id]->xcursorpos = 2;
      z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
      z_windows[statusline_window_id]->rightmost_filled_xpos
        = z_windows[statusline_window_id]->xcursorpos;
      //refresh_cursor(statusline_window_id);
      z_ucs_output(room_description);

      z_windows[statusline_window_id]->xcursorpos
        = z_windows[statusline_window_id]->xsize - 100;
      z_windows[statusline_window_id]->last_gylphs_xcursorpos = -1;
      z_windows[statusline_window_id]->rightmost_filled_xpos
        = z_windows[statusline_window_id]->xcursorpos;
      //refresh_cursor(statusline_window_id);

      sprintf(latin1_buf, "%02d:%02d", parameter1, parameter2);
      latin1_string_to_zucs_string(rightside_buf_zucs, latin1_buf, 8);
      z_ucs_output(rightside_buf_zucs);

      if (buf != 0)
        room_description[room_desc_space] = buf;
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

  if (last_split_window_size > 0) {
    style_buf = z_windows[1]->output_text_style;
    foreground_buf = z_windows[1]->output_foreground_colour;
    background_buf = z_windows[1]->output_background_colour;

    xcurs_buf = z_windows[1]->xcursorpos;
    ycurs_buf = z_windows[1]->ycursorpos;
    z_windows[1]->xcursorpos = 0;
    z_windows[1]->ycursorpos = 0;
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
      for (x=0; x<x_width; x++) {
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

        draw_glyph(
            upper_window_buffer->content[
            upper_window_buffer->width*y + x].character,
            1,
            z_windows[1]->output_true_type_font);
      }
    }
    z_windows[1]->xcursorpos = xcurs_buf;
    z_windows[1]->ycursorpos = ycurs_buf;

    z_windows[1]->output_foreground_colour = foreground_buf;
    z_windows[1]->output_background_colour = background_buf;
    z_windows[1]->output_text_style = style_buf;
    update_window_true_type_font(1);
  }
}


static void refresh_screen() {
  int last_active_z_window_id = -1;
  int y_height_to_fill;
  int saved_padding, last_output_height, nof_paragraphs_to_repeat;

  if (active_z_window_id != 0) {
    last_active_z_window_id = active_z_window_id;
    switch_to_window(0);
  }
  disable_more_prompt = true;

  TRACE_LOG("Refreshing screen, size: %d*%d.\n",
      screen_width_in_pixel, screen_height_in_pixel);
  //screen_pixel_interface->set_text_style(0);
  erase_window(0);

  if (last_active_z_window_id != -1)
    switch_to_window(last_active_z_window_id);

  disable_more_prompt = false;

  if ((history = init_history_output(outputhistory[0], &history_target))
      == NULL)
    return;
  TRACE_LOG("History: %p\n", history);

  if ((history = init_history_output(outputhistory[0],&history_target)) == NULL)
    return;

  y_height_to_fill = z_windows[0]->ysize - z_windows[0]->lower_padding;
  saved_padding = z_windows[0]->lower_padding;
  while (output_rewind_paragraph(history) == 0) {
    if (y_height_to_fill < 1)
      break;

    nof_paragraphs_to_repeat = 1;
    //printf("y_height_to_fill: %d\n", y_height_to_fill);
    if (y_height_to_fill < line_height) {
      // At this point we know we've got to refresh a partial line. To
      // make this easy, we'll simply repeat the last iteration, but
      // request 2 paragraphs for output, so scrolling will take care
      // of this.

      //printf("last_output_height: %d\n", last_output_height);
      y_height_to_fill += last_output_height + line_height;
      nof_paragraphs_to_repeat = 2;
    }

    z_windows[0]->xcursorpos = 0;
    z_windows[0]->last_gylphs_xcursorpos = -1;
    z_windows[0]->rightmost_filled_xpos = z_windows[0]->xcursorpos;
    z_windows[0]->ycursorpos = y_height_to_fill - line_height;
    z_windows[0]->lower_padding =
      z_windows[0]->ysize - y_height_to_fill;
    /*
    printf("new ycurs: %d, lowerpad: %d\n", z_windows[0]->ycursorpos,
        z_windows[0]->lower_padding);
    */

    if (nof_paragraphs_to_repeat == 2) {
      // Since we're now overwriting we'll have to erase at least the
      // current line we're writing to -- scrolling will take case of
      // the rest above.
      clear_to_eol(0);
    }

    /*
       printf("start: y_height_to_fill: %d, cursorpos: %d, pad: %d\n",
       y_height_to_fill, z_windows[0]->ycursorpos,
       z_windows[0]->lower_padding);
       */

    //printf("Start paragraph repetition.\n");
    nof_break_line_invocations = 0;
    output_repeat_paragraphs(history, nof_paragraphs_to_repeat, true, false);
    flush_window(0);
    clear_to_eol(0);
    freetype_wordwrap_reset_position(z_windows[0]->wordwrapper);
    //printf("End paragraph repetition.\n");
    last_output_height = (nof_break_line_invocations + 1) * line_height;
    y_height_to_fill -= last_output_height;
    z_windows[0]->ycursorpos = y_height_to_fill - line_height;

    /*
       printf("end: %d breaks, y_height_to_fill: %d, cursorpos: %d, pad: %d\n",
       nof_break_line_invocations, y_height_to_fill,
       z_windows[0]->ycursorpos,
       z_windows[0]->lower_padding);
       */
  }
  //printf("done, y_height_to_fill is %d.\n", y_height_to_fill);
  destroy_history_output(history);
  z_windows[0]->lower_padding = saved_padding;
  /*
  printf(":: %d, %d. %d, %d\n",
      z_windows[0]->ysize, nof_input_lines, line_height,
    z_windows[0]->lower_padding);
  */
  z_windows[0]->ycursorpos
    = z_windows[0]->ysize
    - (nof_input_lines > 1 ? nof_input_lines : 1) * line_height
    - z_windows[0]->lower_padding;
  //flush_all_buffered_windows();
  if (input_line_on_screen == true) {
    *current_input_y = z_windows[0]->ypos + z_windows[0]->ycursorpos;
    refresh_input_line(true);
  }
  else {
    z_windows[0]->xcursorpos = 0;
    z_windows[0]->last_gylphs_xcursorpos = -1;
    z_windows[0]->rightmost_filled_xpos = z_windows[0]->xcursorpos;
  }

  refresh_upper_window();

  if (ver <= 3)
    display_status_line();

  screen_pixel_interface->update_screen();
}


void preload_wrap_zucs_output(z_ucs *UNUSED(z_ucs_output),
    void *UNUSED(window_number_as_void)) {
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
    bool UNUSED(disable_command_history), bool return_on_escape) {
  int timeout_millis = -1, event_type, i;
  bool input_in_progress = true;
  z_ucs input; //, buf;
  int current_tenth_seconds = 0;
  int input_size = preloaded_input;
  int timed_routine_retval; //, index;
  int input_x, input_y; // Leftmost position of the input line on-screen.
  int input_index = preloaded_input;
  z_ucs input_buffer[maximum_length + 1];
  current_input_buffer = input_buffer;
  current_input_x = &input_x;
  current_input_y = &input_y;
  history_output *preload_history;

  /*
  z_ucs char_buf[] = { 0, 0 };
  //int original_screen_width = screen_width_in_pixel;
  //int original_screen_height = screen_height_in_pixel;

  int input_scroll_x = 0;
  int input_display_width; // Width of the input line on-screen.
  int cmd_history_index = 0;//, cmd_index;
  zscii *cmd_history_ptr;

  current_input_size = &input_size;
  current_input_scroll_x = &input_scroll_x;
  current_input_index = &input_index;
  current_input_display_width = &input_display_width;
  */

  TRACE_LOG("maxlen:%d, preload: %d.\n", maximum_length, preloaded_input);
  TRACE_LOG("y: %d, %d\n",
      z_windows[active_z_window_id]->ypos,
      z_windows[active_z_window_id]->ycursorpos);

  flush_all_buffered_windows();
  for (i=0; i<nof_active_z_windows; i++)
    z_windows[i]->nof_consecutive_lines_output = 0;

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

  //timeout_millis = (is_timed_keyboard_input_available() == true ? 100 : 0);

  TRACE_LOG("1/10s: %d, routine: %d.\n",
      tenth_seconds, verification_routine);

  if ((tenth_seconds != 0) && (verification_routine != 0)) {
    TRACE_LOG("timed input in read_line every %d tenth seconds.\n",
        tenth_seconds);

    timed_input_active = true;

    timeout_millis = (is_timed_keyboard_input_available() == true ? 100 : 0);

    if (tenth_seconds_elapsed != NULL)
      *tenth_seconds_elapsed = 0;
  }
  else
    timed_input_active = false;

  TRACE_LOG("y: %d, %d\n",
      z_windows[active_z_window_id]->ypos,
      z_windows[active_z_window_id]->ycursorpos);

  TRACE_LOG("nof preloaded chars: %d\n", preloaded_input);
  for (i=0; i<preloaded_input; i++)
    input_buffer[i] = zscii_input_char_to_z_ucs(dest[i]);
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

    if ((preload_history = init_history_output(outputhistory[0],
            &preload_history_target)) != NULL) {

      TRACE_LOG("History: %p\n", history);

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

      if (output_rewind_paragraph(preload_history) < 0) {
        printf("err\n");
      }
      output_repeat_paragraphs(preload_history, 1, false, false);
      //printf("wrapper-xpos: %ld\n", preloaded_wordwrapper->current_pixel_pos);
      input_x = get_current_pixel_position(preloaded_wordwrapper)
        + z_windows[active_z_window_id]->xpos
        + z_windows[active_z_window_id]->leftmargin;
      input_y
        = z_windows[active_z_window_id]->ypos
        + z_windows[active_z_window_id]->ycursorpos; // - 1 - line_height;
      destroy_freetype_wrapper(preloaded_wordwrapper);
      destroy_history_output(preload_history);
    }
  }

  TRACE_LOG("input_x, input_y: %d, %d\n", input_x, input_y);

  /*
  input_display_width
    = z_windows[active_z_window_id]->xsize
    - (z_windows[active_z_window_id]->xcursorpos - 1)
    - z_windows[active_z_window_id]->rightmargin
    - preloaded_input;

  TRACE_LOG("input_x:%d, input_y:%d.\n", input_x, input_y);
  TRACE_LOG("input width: %d.\n", input_display_width);

  //REMOVE:
  //input_display_width = 5;

  */










  /*
  printf("XXX\n");
  if ((history = init_history_output(outputhistory[0],&history_target))
      != NULL) {
    TRACE_LOG("History: %p\n", history);

    TRACE_LOG("Trying to find paragraph to fill %d lines.\n",
        z_windows[0]->ysize);

    if (output_rewind_paragraph(history) < 0) {
      printf("err\n");
    }

    output_repeat_paragraphs(history, 1, false, false);

  paragraphs_to_output = 0;
  while (refresh_newline_counter < z_windows[0]->ysize)
  {
    TRACE_LOG("Current refresh_newline_counter: %d, paragraphs: %d\n",
        refresh_newline_counter, paragraphs_to_output);

    if (output_rewind_paragraph(history) < 0)
      break;

    TRACE_LOG("Start paragraph repetition.\n");
    output_repeat_paragraphs(history, 1, false, false);


    destroy_history_output(history);
  }
  */

  TRACE_LOG("xpos:%d\n", z_windows[active_z_window_id]->xcursorpos);

  input_line_on_screen = true;
  nof_input_lines = 1;
  refresh_input_line(true);
  screen_pixel_interface->update_screen();

  while (input_in_progress == true) {
    event_type = screen_pixel_interface->get_next_event(&input, timeout_millis);
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
          refresh_input_line(false);

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
      //handle_scrolling(event_type);
    }
    else {
      if (top_upscroll_line != -1) {
        // End up-scroll.
        TRACE_LOG("Ending scrollback.\n");
        top_upscroll_line = -1;
        upscroll_hit_top = false;
        destroy_history_output(history);
        screen_pixel_interface->set_cursor_visibility(true);
        refresh_screen();
      }

      if (event_type == EVENT_WAS_INPUT) {
        //printf("%c / %d\n", input, input);
        if (input == Z_UCS_NEWLINE) {
          input_in_progress = false;
        }
        else if (input == 12) {
          TRACE_LOG("Got CTRL-L.\n");
          //refresh_screen();
          //screen_pixel_interface->redraw_screen_from_scratch();
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
            /*
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
            */
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

          /*
          if (z_windows[active_z_window_id]->xcursorpos -1
              + z_windows[active_z_window_id]->rightmargin
              == z_windows[active_z_window_id]->xsize)
            input_scroll_x++;
          else
            z_windows[active_z_window_id]->xcursorpos++;

          buf = 0;
          TRACE_LOG("out:%d, %d, %d\n",
              input_size, input_scroll_x, input_display_width);

          if (input_size - input_scroll_x >= input_display_width+1) {
            buf = input_buffer[input_scroll_x + input_display_width];
            input_buffer[input_scroll_x + input_display_width] = 0;
          }
          */

          refresh_input_line(true);
          //refresh_cursor(active_z_window_id);
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
        /*
        if (input_index > 0) {
          if (z_windows[active_z_window_id]->xpos
              + z_windows[active_z_window_id]->xcursorpos - 1
              > input_x) {
            z_windows[active_z_window_id]->xcursorpos--;
            //refresh_cursor(active_z_window_id);
            screen_pixel_interface->update_screen();
          }
          else {
            screen_pixel_interface->copy_area(
                input_y,
                z_windows[active_z_window_id]->xpos
                + z_windows[active_z_window_id]->xcursorpos,
                input_y,
                z_windows[active_z_window_id]->xpos
                + z_windows[active_z_window_id]->xcursorpos - 1,
                1,
                input_display_width - 1);

            //screen_pixel_interface->goto_yx(input_y, input_x);
            char_buf[0] = input_buffer[input_scroll_x - 1];
            input_scroll_x--;
            //screen_pixel_interface->z_ucs_output(char_buf);
            //screen_pixel_interface->goto_yx(input_y, input_x);
            //refresh_cursor(active_z_window_id);
            screen_pixel_interface->update_screen();
          }

          input_index--;
        }
        */
      }
      else if (event_type == EVENT_WAS_CODE_CURSOR_RIGHT) {
        /*
        // Verify if we're at the end of input (plus one more char since
        // the cursor must also be allowed behind the input for appending):
        if (input_index < input_size) {
          // Check if advancing the cursor right would move it behind the
          // rightmost allowed input column.
          if (z_windows[active_z_window_id]->xpos
              + z_windows[active_z_window_id]->xcursorpos
              < input_x + input_display_width) {
            // In this case, the cursor is still left of the right border, so
            // we can just move it left:
            z_windows[active_z_window_id]->xcursorpos++;
            //refresh_cursor(active_z_window_id);
          }
          else {
            // If the cursor moves behind the current rightmost position, we
            // have to scroll the input line.
            screen_pixel_interface->copy_area(
                input_y,
                input_x,
                input_y,
                input_x + 1,
                1,
                input_display_width - 1);

            // Verify if the cusor is still over the input line or if it's
            // now on the "append" column behind.
            if (input_index == input_size - 1) {
              // In case we're at the end we have to fill the new, empty
              // column with a space.
              char_buf[0] = Z_UCS_SPACE;
            }
            else {
              // If the cursor is still over the input, we'll fill the
              // rightmost column with appropriate char.
              char_buf[0] = input_buffer[input_scroll_x + input_display_width];
            }
            // After determining how to fill the rightmost column, jump to
            // the correct position on-screen, display the new char and
            // re-position the cursor over the rightmost column.

            input_scroll_x++;
          }
          screen_pixel_interface->update_screen();

          // No matter whether we had to scroll or not, as long as we were
          // not at the end of the input, the cursor was moved right, and thus
          // the input index has to be altered.
          input_index++;
        }
        */
      }
      /*
      else if ( (disable_command_history == false)
          && (
            (
             (event_type == EVENT_WAS_CODE_CURSOR_UP)
             && (cmd_history_index < get_number_of_stored_commands())
            )
            || ( (event_type == EVENT_WAS_CODE_CURSOR_DOWN)
              && (cmd_history_index != 0)))) {
        TRACE_LOG("old history index: %d.\n", cmd_history_index);

        if (cmd_history_index > 0) {
          input_size = strlen((char*)cmd_history_ptr);
          if (input_size > input_display_width+1) {
            input_scroll_x = input_size - input_display_width;
            z_windows[active_z_window_id]->xcursorpos
              = input_x + input_display_width;
          }
          else {
            input_scroll_x = 0;
            z_windows[active_z_window_id]->xcursorpos = input_x + input_size;
          }

          input_index = input_size;

          for (i=0; i<=input_size; i++)
            input_buffer[i] = zscii_input_char_to_z_ucs(*(cmd_history_ptr++));

          TRACE_LOG("out:%d, %d, %d\n",
              input_size, input_scroll_x, input_display_width);

          if (input_size - input_scroll_x >= input_display_width+1) {
            buf = input_buffer[input_scroll_x + input_display_width];
            input_buffer[input_scroll_x + input_display_width] = 0;
          }
          else
            buf = 0;

          if (buf != 0)
            input_buffer[input_scroll_x + input_display_width] = buf;
          screen_pixel_interface->clear_to_eol();
        }
        else {
          input_size = 0;
          input_scroll_x = 0;
          input_index = 0;
          z_windows[active_z_window_id]->xcursorpos = input_x;
          //screen_pixel_interface->goto_yx(input_y, input_x);
          screen_pixel_interface->clear_to_eol();
        }

        //refresh_cursor(active_z_window_id);
        screen_pixel_interface->update_screen();
      }
      */
      else if (event_type == EVENT_WAS_WINCH) {
        TRACE_LOG("winch.\n");
        new_pixel_screen_size(
            screen_pixel_interface->get_screen_height_in_pixels(),
            screen_pixel_interface->get_screen_width_in_pixels());
      }
      else if (event_type == EVENT_WAS_CODE_CTRL_A) {
        /*
        if (input_index > 0) {
          if (input_scroll_x > 0) {
            input_scroll_x  = 0;
            index = input_display_width < input_size
              ? input_display_width : input_size;
            char_buf[0] = input_buffer[index];
            input_buffer[index] = 0;
            input_buffer[index] = char_buf[0];
          }

          z_windows[active_z_window_id]->xcursorpos = input_x;
          input_index = 0;
          //refresh_cursor(active_z_window_id);
          screen_pixel_interface->update_screen();
        }
        */
      }
      else if (event_type == EVENT_WAS_CODE_CTRL_E) {
        /*
        TRACE_LOG("input_size:%d, input_display_width:%d.\n",
            input_size, input_display_width);

        if (input_size > input_display_width - 1) {
          input_scroll_x = input_size - input_display_width + 1;
          screen_pixel_interface->clear_to_eol();
          z_windows[active_z_window_id]->xcursorpos
            = input_x + input_display_width - 1;
        }
        else
          z_windows[active_z_window_id]->xcursorpos = input_x + input_size;

        input_index = input_size;
        //refresh_cursor(active_z_window_id);
        screen_pixel_interface->update_screen();
        */
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
    //TRACE_LOG("input_scroll_x:%d\n", input_scroll_x);
  }
  TRACE_LOG("x-readline-ycursorpos: %d.\n", z_windows[0]->ycursorpos);

  /*
  //screen_pixel_interface->goto_yx(input_y, input_x);
  screen_pixel_interface->clear_to_eol();
  z_windows[active_z_window_id]->xcursorpos
    = input_x - (z_windows[active_z_window_id]->xpos - 1);
  //refresh_cursor(active_z_window_id);
  */

  //refresh_input_line(false);
  clear_input_line();

  z_windows[0]->ycursorpos = *current_input_y - z_windows[0]->ypos;
  z_windows[0]->xcursorpos = *current_input_x - z_windows[0]->xpos
    - z_windows[0]->leftmargin;

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
  for (i=0; i<nof_active_z_windows; i++)
    z_windows[i]->nof_consecutive_lines_output = 0;
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

  while (input_in_progress == true)
  {
    event_type = screen_pixel_interface->get_next_event(&input, timeout_millis);
    //printf("event: %d\n", event_type);

    if ( (event_type == EVENT_WAS_CODE_PAGE_UP)
        || (event_type == EVENT_WAS_CODE_PAGE_DOWN)) {
      //handle_scrolling(event_type);
    }
    else {
      if (top_upscroll_line != -1) {
        // End up-scroll.
        TRACE_LOG("Ending scrollback.\n");
        top_upscroll_line = -1;
        upscroll_hit_top = false;
        destroy_history_output(history);
        screen_pixel_interface->set_cursor_visibility(true);
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

  return result;
}


static void set_window(int16_t window_number) {
  if ( (window_number >= 0)
      && (window_number <=
        nof_active_z_windows - (statusline_window_id >= 0 ? 1 : 0))) {
    if ((ver != 6) && (window_number == 1)) {
      z_windows[1]->ycursorpos = 0;
      z_windows[1]->xcursorpos = 0;
      z_windows[1]->last_gylphs_xcursorpos = -1;
      z_windows[1]->rightmost_filled_xpos
        = z_windows[1]->xcursorpos;
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
        ? z_windows[window_number]->ysize - line_height
        : pixel_line;

      z_windows[window_number]->xcursorpos
        = pixel_column > z_windows[window_number]->xsize
        ? z_windows[window_number]->xsize - pixel_column
        /*
           ? (z_windows[window_number]->wrapping == false
           ? z_windows[window_number]->xsize + 1
           : z_windows[window_number]->xsize)
           */
        : pixel_column;

      z_windows[window_number]->last_gylphs_xcursorpos = -1;
      z_windows[window_number]->rightmost_filled_xpos
        = z_windows[window_number]->xcursorpos;

      //refresh_cursor(window_number);
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
  if (interface_open == true) {
    refresh_screen();
    //screen_pixel_interface->update_screen();
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


void fizmo_register_screen_pixel_interface(struct z_screen_pixel_interface
    *new_screen_pixel_interface) {
  if (screen_pixel_interface == NULL) {
    TRACE_LOG("Registering screen pixel interface at %p.\n",
        new_screen_pixel_interface);

    screen_pixel_interface = new_screen_pixel_interface;
    set_configuration_value("enable-font3-conversion", "true");

    fizmo_register_screen_interface(&z_pixel_interface);
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

  for (i=0; i<nof_active_z_windows; i++)
    consecutive_lines_buffer[i] = z_windows[i]->nof_consecutive_lines_output;
  disable_more_prompt = true;

  dy = newysize - screen_height_in_pixel;

  screen_width_in_pixel = newxsize;
  screen_height_in_pixel = newysize;
  update_fixed_width_char_width();

  fizmo_new_screen_size(
      get_screen_width_in_characters(),
      get_screen_height_in_lines());

  printf("new pixel-window-size: %d*%d.\n",
      screen_width_in_pixel, screen_height_in_pixel);
  TRACE_LOG("new pixel-window-size: %d*%d.\n",
      screen_width_in_pixel, screen_height_in_pixel);

  z_windows[1]->ysize = last_split_window_size * line_height;
  if (last_split_window_size > newysize - status_offset)
    z_windows[1]->ysize = newysize - status_offset;

  // Crop cursor positions and windows to new screen size
  for (i=0; i<nof_active_z_windows; i++) {
    // Expand window 0 to new screensize for version != 6.
    if (ver != 6) {
      if (i == 0) {
        if (z_windows[0]->xsize < screen_width_in_pixel)
          z_windows[0]->xsize = screen_width_in_pixel;

        z_windows[0]->ysize
          = screen_height_in_pixel - status_offset - z_windows[1]->ysize;

        z_windows[0]->ycursorpos += dy;
      }
      else if (i == 1) {
        if (z_windows[1]->xsize != screen_width_in_pixel)
          z_windows[1]->xsize = screen_width_in_pixel;
      }
      else if (i == statusline_window_id) {
        z_windows[statusline_window_id]->xsize = screen_width_in_pixel;
      }
    }

    if (z_windows[i]->ypos > screen_height_in_pixel - 1)
      z_windows[i]->ypos = screen_height_in_pixel - 1;

    if (z_windows[i]->xpos > screen_width_in_pixel - 1)
      z_windows[i]->xpos = screen_width_in_pixel - 1;

    if (z_windows[i]->ypos + z_windows[i]->ysize > screen_height_in_pixel)
      z_windows[i]->ysize = screen_height_in_pixel - z_windows[i]->ypos;

    if (z_windows[i]->xpos + z_windows[i]->xsize > screen_width_in_pixel) {
      z_windows[i]->xsize = screen_width_in_pixel - z_windows[i]->xpos;

      if (z_windows[i]->xsize - z_windows[i]->leftmargin
          - z_windows[i]->rightmargin < 1) {
        z_windows[i]->leftmargin = 0;
        z_windows[i]->rightmargin = 0;
      }
    }

    /*
    printf("New line length for window %i: %d.\n", i,
        z_windows[i]->xsize - z_windows[i]->rightmargin
        - z_windows[i]->leftmargin);
    */
    freetype_wordwrap_adjust_line_length(
        z_windows[i]->wordwrapper,
        z_windows[i]->xsize - z_windows[i]->rightmargin
        - z_windows[i]->leftmargin);

    if (z_windows[i]->ycursorpos + 2*line_height
        > z_windows[i]->ysize - z_windows[i]->lower_padding) {
      z_windows[i]->ycursorpos
        = z_windows[i]->ysize - line_height - z_windows[i]->lower_padding;
      TRACE_LOG("new ycursorpos[%d]: %d\n", i, z_windows[i]->ycursorpos);
    }

    if (z_windows[i]->xcursorpos > z_windows[i]->xsize - fixed_width_char_width
        - z_windows[i]->rightmargin - z_windows[i]->rightmargin) {
      z_windows[i]->xcursorpos = z_windows[i]->xsize - fixed_width_char_width
        - z_windows[i]->rightmargin - z_windows[i]->rightmargin;
    }
  }

  /*
  if (input_line_on_screen == true) {
    TRACE_LOG("input-x-before: %d\n", *current_input_x);
    TRACE_LOG("input-y-before: %d\n", *current_input_y);
    //TRACE_LOG("current_input_display_width: %d\n",
    TRACE_LOG("%d/%d/%d y:%d,%d\n", z_windows[0]->xpos, z_windows[0]->xsize,
        *current_input_x, z_windows[0]->ypos, z_windows[0]->ysize);

    // If the screen is redrawn, the screen contents are always aligned
    // to the bottom so we also have to move the input line downward.
    *current_input_y
      = z_windows[0]->ypos + z_windows[0]->ysize - line_height
      - z_windows[0]->lower_padding;

    //TRACE_LOG("current_input_display_width: %d\n",
    // *current_input_display_width);

    TRACE_LOG("input-x-after: %d\n", *current_input_x);
    TRACE_LOG("input-y-after: %d\n", *current_input_y);
  }
  */

  //wordwrap_output_left_padding(z_windows[i]->wordwrapper);
  refresh_screen();

  for (i=0; i<nof_active_z_windows; i++)
    z_windows[i]->nof_consecutive_lines_output = consecutive_lines_buffer[i];
  disable_more_prompt = false;
}


char *get_screen_pixel_interface_version() {
  return screen_pixel_interface_version;
}

