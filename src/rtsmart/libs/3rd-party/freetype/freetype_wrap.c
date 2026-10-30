/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freetype_wrap.h"

#include FT_CACHE_H
#include FT_CACHE_MANAGER_H
#include FT_CACHE_IMAGE_H
#include FT_CACHE_CHARMAP_H

#define CONFIG_FREETYPE_SUPPORT_CACHE 1

#define FREETYPE_DEFAULT_FONT_PATH "/sdcard/res/font/SourceHanSansSC-Normal-Min.ttf"

static FT_Library     s_ft_library;
static FTC_Manager    s_ft_cacheManager;
static FTC_ImageCache s_ft_imageCache;
static FTC_CMapCache  s_ft_cmapCache;

static int         s_ft_init_flag = 0;
static char        s_ft_font_path[128];
static const char* s_ft_dft_font_path = FREETYPE_DEFAULT_FONT_PATH;

static FT_Error ftwrap_face_requester(FTC_FaceID face_id, FT_Library library, FT_Pointer request_data, FT_Face* face)
{
    const char* font = (const char*)face_id;

    return FT_New_Face(library, font, 0, face);
}

void freetype_wrap_deinit(void)
{
    if (s_ft_cacheManager) {
        FTC_Manager_Done(s_ft_cacheManager);
        s_ft_cacheManager = NULL;
    }
    if (s_ft_library) {
        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;
    }

    s_ft_init_flag = 0;
    memset(s_ft_font_path, 0, sizeof(s_ft_font_path));
}

static int freetype_wrap_init(const char* font_path)
{
    FILE* fp = NULL;

    FT_Error    error;
    const char* _font_path = font_path ? font_path : s_ft_dft_font_path;

    if ((0x01 == s_ft_init_flag) && (0x00 == strncmp(_font_path, s_ft_font_path, sizeof(s_ft_font_path)))) {
        return FREETYPE_WRAP_ERR_NONE;
    }

    if (s_ft_init_flag) {
        if (s_ft_cacheManager) {
            FTC_Manager_Done(s_ft_cacheManager);
            s_ft_cacheManager = NULL;
        }
        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;
    }
    s_ft_init_flag = 0;
    memset(s_ft_font_path, 0, sizeof(s_ft_font_path));

    if (NULL == (fp = fopen(_font_path, "rb"))) {
        printf("Open font(%s) failed.\n", _font_path);
        return FREETYPE_WRAP_ERR_OPEN_FONT_FAILED;
    }
    fclose(fp);
    fp = NULL;

    // Initialize the FreeType library
    error = FT_Init_FreeType(&s_ft_library);
    if (error) {
        printf("Could not initialize FreeType library\n");
        return FREETYPE_WRAP_ERR_INIT_FREETYPE_FAILED;
    }

    error = FTC_Manager_New(s_ft_library, 4, 4, 1024 * 64, &ftwrap_face_requester, NULL, &s_ft_cacheManager);
    if (error) {
        printf("FTC_Manager_New failed %d\n", error);

        s_ft_cacheManager = NULL;

        // Handle error
        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;

        return FREETYPE_WRAP_ERR_NEW_MANAGER_FAILED;
    }

    error = FTC_CMapCache_New(s_ft_cacheManager, &s_ft_cmapCache);
    if (error) {
        printf("FTC_CMapCache_New failed %d\n", error);
        FTC_Manager_Done(s_ft_cacheManager);
        s_ft_cacheManager = NULL;

        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;

        return FREETYPE_WRAP_ERR_NEW_MAPCACHE_FAILED;
    }

    error = FTC_ImageCache_New(s_ft_cacheManager, &s_ft_imageCache);
    if (error) {
        // Handle error
        printf("FTC_ImageCache_New failed %d\n", error);

        FTC_Manager_Done(s_ft_cacheManager);
        s_ft_cacheManager = NULL;

        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;

        return FREETYPE_WRAP_ERR_NEW_IMGCACHE_FAILED;
    }

    s_ft_init_flag = 1;
    strncpy(s_ft_font_path, _font_path, sizeof(s_ft_font_path));

    return FREETYPE_WRAP_ERR_NONE;
}

// static void print_glyph(FT_GlyphSlot slot) {
//     FT_Bitmap *bitmap = &slot->bitmap;

//     printf("%dx%d, left %d, top %d, x %ld\n", bitmap->width, bitmap->rows, slot->bitmap_left, slot->bitmap_top,
//     slot->advance.x >> 6);

//     for(int i = 0; i < bitmap->rows; i++) {
//         for (int j = 0; j < bitmap->width; j++) {
//             if(bitmap->buffer[i * bitmap->width + j]) {
//                 printf("*");
//             } else {
//                 printf(" ");
//             }
//         }
//         printf("\n");
//     }
// }

// Function to decode a UTF-8 character to a Unicode code point
static FT_ULong utf8_to_unicode(const char** ptr)
{
    const unsigned char* p = (const unsigned char*)*ptr;
    FT_ULong             unicode;

    if (p[0] < 0x80) {
        unicode = p[0];
        *ptr += 1;
    } else if ((p[0] & 0xE0) == 0xC0) {
        unicode = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *ptr += 2;
    } else if ((p[0] & 0xF0) == 0xE0) {
        unicode = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *ptr += 3;
    } else if ((p[0] & 0xF8) == 0xF0) {
        unicode = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        *ptr += 4;
    } else {
        unicode = 0; // Invalid UTF-8 character
        *ptr += 1;
    }

    return unicode;
}

int freetype_wrap_draw_string(int x_off, int y_off, int char_size, const char* str, int color, const char* font_path,
                              freetype_wrap_draw_bitmap draw, void* draw_ctx)
{
    int error = 0;

    int         point_x = x_off;
    int         point_y = y_off + char_size;
    const char* text    = str;

    if (0x00 != (error = freetype_wrap_init(font_path))) {
        return error;
    }

    FTC_FaceID    face_id;
    FT_Face       face;
    FT_Glyph      glyph;
    FT_UInt       glyph_index, previous = 0;
    FT_Int        charmap_index;
    FTC_ScalerRec scaler;

    face_id = (FTC_FaceID)&s_ft_font_path;

    FTC_Manager_LookupFace(s_ft_cacheManager, face_id, &face);
    charmap_index = FT_Get_Charmap_Index(face->charmap);

    FT_Bool use_kerning = FT_HAS_KERNING(face);

    scaler.face_id = face_id;
    scaler.pixel   = 1;
    scaler.width   = char_size;
    scaler.height  = char_size;

    (void)previous;
    (void)use_kerning;

    while (*text) {
        if (*text == '\n') {
            point_x = x_off;
            point_y += char_size;
            text++;
            continue;
        }

        FT_ULong charcode = utf8_to_unicode(&text);
        if (0x00 == charcode) {
            continue;
        }

        glyph_index = FTC_CMapCache_Lookup(s_ft_cmapCache, face_id, charmap_index, charcode);
        FTC_ImageCache_LookupScaler(s_ft_imageCache, &scaler, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL, glyph_index,
                                    &glyph, NULL);

        if (use_kerning && previous && glyph_index) {
            FT_Vector delta;
            FT_Get_Kerning(face, previous, glyph_index, FT_KERNING_DEFAULT, &delta);
            point_x += delta.x >> 6;
        }
        previous = glyph_index;

        FT_BitmapGlyph bitmapGlyph = (FT_BitmapGlyph)glyph;
        FT_Bitmap*     bitmap      = &bitmapGlyph->bitmap;
        // Draw the bitmap
        if (draw) {
            draw(draw_ctx, color, point_x + bitmapGlyph->left, point_y - bitmapGlyph->top, bitmap);
        }

        // Advance the cursor to the start of the next character
        // point_x += (face->glyph->advance.x >> 6);
        point_x += (bitmapGlyph->root.advance.x >> 16);
    }

    return FREETYPE_WRAP_ERR_NONE;
}
