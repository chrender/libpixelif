
/* pixel_interface.h
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


#ifndef pixelscreen_h_INCLUDED
#define pixelscreen_h_INCLUDED

#define LIBPIXELINTERFACE_VERSION "0.7.3"

#include "../screen_interface/screen_pixel_interface.h"

#define MAX_MARGIN_SIZE 100
#define MAX_MARGIN_AS_STRING_LEN 4
#define MAX_VALUE_AS_STRING_LEN 4

void fizmo_register_screen_pixel_interface(struct z_screen_pixel_interface
    *screen_pixel_interface);
void new_pixel_screen_size(int newysize, int newxsize);
void set_custom_left_pixel_margin(int width);
void set_custom_right_pixel_margin(int width);
char *get_screen_pixel_interface_version();

#endif // pixelscreen_h_INCLUDED

