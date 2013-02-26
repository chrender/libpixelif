
/* true_type_factory.c
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


#include <ft2build.h>
#include FT_FREETYPE_H

#include "true_type_factory.h"
#include "true_type_font.h"
#include "tools/i18n.h"
#include "tools/filesys.h"
#include "interpreter/fizmo.h"
#include "../locales/libpixelif_locales.h"

true_type_factory *create_true_type_factory(char *font_search_path) {
  true_type_factory *result;
  int ft_error;

  result = (true_type_factory*)fizmo_malloc(sizeof(true_type_factory));

  if ((ft_error = FT_Init_FreeType(&result->ftlibrary))) {
    i18n_translate_and_exit(
        libpixelif_module_name,
        i18n_libpixelif_FUNCTION_CALL_P0S_ABORTED_DUE_TO_ERROR,
        -1,
        "FT_Init_FreeType");
  }

  result->font_search_path = strdup(font_search_path);

  return result;
}


static unsigned long read_ft_stream(FT_Stream stream, unsigned long offset,
    unsigned char* buffer, unsigned long count) {
  z_file *fontfile = (z_file*)stream->descriptor.pointer;
  fsi->setfilepos(fontfile, offset, SEEK_SET);
  return fsi->getchars(buffer, count, fontfile);
}


static void close_ft_stream(FT_Stream stream) {
  fsi->closefile((z_file*)stream->descriptor.pointer);
}


true_type_font *create_true_type_font(true_type_factory *factory,
    char *font_filename, int pixel_size) {
  int ft_error;
  z_file *fontfile;
  char *token, *filename;
  FT_Open_Args *openArgs;
  FT_Stream stream;
  long filesize;
  true_type_font *result;

  if (factory->font_search_path == NULL)
    return NULL;

  token = strtok(factory->font_search_path, ":");
  fontfile = NULL;
  while (token) {
    filename = fizmo_malloc(strlen(token) + strlen(font_filename) + 2);
    strcpy(filename, token);
    strcat(filename, "/");
    strcat(filename, font_filename);
    printf("%s\n", font_filename);
    if ((fontfile = fsi->openfile(filename, FILETYPE_DATA, FILEACCESS_READ))
        != NULL) {  
      free(filename);
      break;
    }
    free(filename);
    token = strtok(NULL, ":");
  }

  if (fontfile == NULL) {
    printf("Font %s not found.\n", font_filename);
    return NULL;
  }

  printf("ok\n");

  fsi->setfilepos(fontfile, 0, SEEK_END);
  filesize = fsi->getfilepos(fontfile);
  fsi->setfilepos(fontfile, 0, SEEK_SET);

  printf("filesize: %ld\n", filesize);

  openArgs = (FT_Open_Args *)fizmo_malloc(sizeof(FT_Open_Args));
  stream = (FT_Stream)fizmo_malloc(sizeof(FT_StreamRec));
  openArgs->flags = FT_OPEN_STREAM;
  openArgs->stream = stream;
  openArgs->stream->base = NULL;
  openArgs->stream->size = filesize;
  openArgs->stream->pos = 0;
  openArgs->stream->descriptor.pointer = fontfile;
  openArgs->stream->pathname.pointer = NULL;
  openArgs->stream->read = read_ft_stream;
  openArgs->stream->close = close_ft_stream;

  result = (true_type_font*)fizmo_malloc(sizeof(true_type_font));

  ft_error = FT_Open_Face(factory->ftlibrary, openArgs, 0, &result->face);

  if ( ft_error == FT_Err_Unknown_File_Format ) {
    // ... the font file could be opened and read, but it appears
    // ... that its font format is unsupported
    return NULL;
  }
  else if ( ft_error ) {
    // ... another ft_error code means that the font file could not
    // ... be opened or read, or simply that it is broken...
    return NULL;
  }

  ft_error = FT_Set_Pixel_Sizes(
      result->face,
      0,
      pixel_size);

  //result->has_kerning = FT_HAS_KERNING(result->face);

  return result;
}


void destroy_true_type_factory(true_type_factory *factory) {
  FT_Done_FreeType(factory->ftlibrary);
  if (factory->font_search_path != NULL) {
    free(factory->font_search_path);
  }
  free(factory);
}

