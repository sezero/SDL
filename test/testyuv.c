/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>
#include "testyuv_cvt.h"
#include "testutils.h"

/* 422 (YUY2, etc) and P010 formats are the largest */
#define MAX_YUV_SURFACE_SIZE(W, H, P) ((H + 1) * ((W + 1) + P) * 4)

/* Return true if the YUV format is packed pixels */
static bool is_packed_yuv_format(Uint32 format)
{
    return format == SDL_PIXELFORMAT_YUY2 || format == SDL_PIXELFORMAT_UYVY || format == SDL_PIXELFORMAT_YVYU;
}

/* Create a surface with a good pattern for verifying YUV conversion */
static SDL_Surface *generate_test_pattern(int pattern_size)
{
    SDL_Surface *pattern = SDL_CreateSurface(pattern_size, pattern_size, SDL_PIXELFORMAT_RGB24);

    if (pattern) {
        int i, x, y;
        Uint8 *p, c;
        const int thickness = 2; /* Important so 2x2 blocks of color are the same, to avoid Cr/Cb interpolation over pixels */

        /* R, G, B in alternating horizontal bands */
        for (y = 0; y < pattern->h - (thickness - 1); y += thickness) {
            for (i = 0; i < thickness; ++i) {
                p = (Uint8 *)pattern->pixels + (y + i) * pattern->pitch + ((y / thickness) % 3);
                for (x = 0; x < pattern->w; ++x) {
                    *p = 0xFF;
                    p += 3;
                }
            }
        }

        /* Black and white in alternating vertical bands */
        c = 0xFF;
        for (x = 1 * thickness; x < pattern->w; x += 2 * thickness) {
            for (i = 0; i < thickness; ++i) {
                p = (Uint8 *)pattern->pixels + (x + i) * 3;
                for (y = 0; y < pattern->h; ++y) {
                    SDL_memset(p, c, 3);
                    p += pattern->pitch;
                }
            }
            if (c) {
                c = 0x00;
            } else {
                c = 0xFF;
            }
        }
    }
    return pattern;
}

static bool verify_yuv_data(Uint32 format, SDL_Colorspace colorspace, const Uint8 *yuv, int yuv_pitch, SDL_Surface *surface, int tolerance)
{
    const int size = (surface->h * surface->pitch);
    Uint8 *rgb;
    bool result = false;

    rgb = (Uint8 *)SDL_malloc(size);
    if (!rgb) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Out of memory");
        return false;
    }

    if (SDL_ConvertPixelsAndColorspace(surface->w, surface->h, format, colorspace, 0, yuv, yuv_pitch, surface->format, SDL_COLORSPACE_SRGB, 0, rgb, surface->pitch)) {
        int x, y;
        result = true;
        for (y = 0; y < surface->h; ++y) {
            const Uint8 *actual = rgb + y * surface->pitch;
            const Uint8 *expected = (const Uint8 *)surface->pixels + y * surface->pitch;
            for (x = 0; x < surface->w; ++x) {
                int deltaR = (int)actual[0] - expected[0];
                int deltaG = (int)actual[1] - expected[1];
                int deltaB = (int)actual[2] - expected[2];
                int distance = (deltaR * deltaR + deltaG * deltaG + deltaB * deltaB);
                if (distance > tolerance) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pixel at %d,%d was 0x%.2x,0x%.2x,0x%.2x, expected 0x%.2x,0x%.2x,0x%.2x, distance = %d", x, y, actual[0], actual[1], actual[2], expected[0], expected[1], expected[2], distance);
                    result = false;
                }
                actual += 3;
                expected += 3;
            }
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(format), SDL_GetPixelFormatName(surface->format), SDL_GetError());
    }
    SDL_free(rgb);

    return result;
}

static bool run_automated_tests(int pattern_size, int extra_pitch)
{
    const Uint32 formats[] = {
        SDL_PIXELFORMAT_YV12,
        SDL_PIXELFORMAT_IYUV,
        SDL_PIXELFORMAT_NV12,
        SDL_PIXELFORMAT_NV21,
        SDL_PIXELFORMAT_YUY2,
        SDL_PIXELFORMAT_UYVY,
        SDL_PIXELFORMAT_YVYU
    };
    int i, j;
    SDL_Surface *pattern = generate_test_pattern(pattern_size);
    const int yuv_len = MAX_YUV_SURFACE_SIZE(pattern->w, pattern->h, extra_pitch);
    Uint8 *yuv1 = (Uint8 *)SDL_malloc(yuv_len);
    Uint8 *yuv2 = (Uint8 *)SDL_malloc(yuv_len);
    int yuv1_pitch, yuv2_pitch;
    YUV_CONVERSION_MODE mode;
    SDL_Colorspace colorspace;
    const int tight_tolerance = 20;
    const int loose_tolerance = 333;
    bool result = false;

    if (!pattern || !yuv1 || !yuv2) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't allocate test surfaces");
        goto done;
    }

    mode = GetYUVConversionModeForResolution(pattern->w, pattern->h);
    colorspace = GetColorspaceForYUVConversionMode(mode);

    /* Verify conversion from YUV formats */
    for (i = 0; i < SDL_arraysize(formats); ++i) {
        if (!ConvertRGBtoYUV(formats[i], pattern->pixels, pattern->pitch, yuv1, pattern->w, pattern->h, mode, 0, 100)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ConvertRGBtoYUV() doesn't support converting to %s", SDL_GetPixelFormatName(formats[i]));
            goto done;
        }
        yuv1_pitch = CalculateYUVPitch(formats[i], pattern->w);
        if (!verify_yuv_data(formats[i], colorspace, yuv1, yuv1_pitch, pattern, tight_tolerance)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from %s to RGB", SDL_GetPixelFormatName(formats[i]));
            goto done;
        }
    }

    /* Verify conversion to YUV formats */
    for (i = 0; i < SDL_arraysize(formats); ++i) {
        yuv1_pitch = CalculateYUVPitch(formats[i], pattern->w) + extra_pitch;
        if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, pattern->format, SDL_COLORSPACE_SRGB, 0, pattern->pixels, pattern->pitch, formats[i], colorspace, 0, yuv1, yuv1_pitch)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(pattern->format), SDL_GetPixelFormatName(formats[i]), SDL_GetError());
            goto done;
        }
        if (!verify_yuv_data(formats[i], colorspace, yuv1, yuv1_pitch, pattern, tight_tolerance)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from RGB to %s", SDL_GetPixelFormatName(formats[i]));
            goto done;
        }
    }

    /* Verify conversion between YUV formats */
    for (i = 0; i < SDL_arraysize(formats); ++i) {
        for (j = 0; j < SDL_arraysize(formats); ++j) {
            yuv1_pitch = CalculateYUVPitch(formats[i], pattern->w) + extra_pitch;
            yuv2_pitch = CalculateYUVPitch(formats[j], pattern->w) + extra_pitch;
            if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, pattern->format, SDL_COLORSPACE_SRGB, 0, pattern->pixels, pattern->pitch, formats[i], colorspace, 0, yuv1, yuv1_pitch)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(pattern->format), SDL_GetPixelFormatName(formats[i]), SDL_GetError());
                goto done;
            }
            if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, formats[i], colorspace, 0, yuv1, yuv1_pitch, formats[j], colorspace, 0, yuv2, yuv2_pitch)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(formats[i]), SDL_GetPixelFormatName(formats[j]), SDL_GetError());
                goto done;
            }
            if (!verify_yuv_data(formats[j], colorspace, yuv2, yuv2_pitch, pattern, tight_tolerance)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from %s to %s", SDL_GetPixelFormatName(formats[i]), SDL_GetPixelFormatName(formats[j]));
                goto done;
            }
        }
    }

    /* Verify conversion between YUV formats in-place */
    for (i = 0; i < SDL_arraysize(formats); ++i) {
        for (j = 0; j < SDL_arraysize(formats); ++j) {
            if (is_packed_yuv_format(formats[i]) != is_packed_yuv_format(formats[j])) {
                /* Can't change plane vs packed pixel layout in-place */
                continue;
            }

            yuv1_pitch = CalculateYUVPitch(formats[i], pattern->w) + extra_pitch;
            yuv2_pitch = CalculateYUVPitch(formats[j], pattern->w) + extra_pitch;
            if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, pattern->format, SDL_COLORSPACE_SRGB, 0, pattern->pixels, pattern->pitch, formats[i], colorspace, 0, yuv1, yuv1_pitch)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(pattern->format), SDL_GetPixelFormatName(formats[i]), SDL_GetError());
                goto done;
            }
            if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, formats[i], colorspace, 0, yuv1, yuv1_pitch, formats[j], colorspace, 0, yuv1, yuv2_pitch)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(formats[i]), SDL_GetPixelFormatName(formats[j]), SDL_GetError());
                goto done;
            }
            if (!verify_yuv_data(formats[j], colorspace, yuv1, yuv2_pitch, pattern, tight_tolerance)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from %s to %s", SDL_GetPixelFormatName(formats[i]), SDL_GetPixelFormatName(formats[j]));
                goto done;
            }
        }
    }

    /* Verify round trip through BT.2020 */
    colorspace = SDL_COLORSPACE_BT2020_FULL;
    if (!ConvertRGBtoYUV(SDL_PIXELFORMAT_P010, pattern->pixels, pattern->pitch, yuv1, pattern->w, pattern->h, YUV_CONVERSION_BT2020, 0, 100)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ConvertRGBtoYUV() doesn't support converting to %s", SDL_GetPixelFormatName(SDL_PIXELFORMAT_P010));
        goto done;
    }
    yuv1_pitch = CalculateYUVPitch(SDL_PIXELFORMAT_P010, pattern->w);
    if (!verify_yuv_data(SDL_PIXELFORMAT_P010, colorspace, yuv1, yuv1_pitch, pattern, tight_tolerance)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from %s to RGB", SDL_GetPixelFormatName(SDL_PIXELFORMAT_P010));
        goto done;
    }

    /* The pitch needs to be Uint16 aligned for P010 pixels */
    yuv1_pitch = CalculateYUVPitch(SDL_PIXELFORMAT_P010, pattern->w) + ((extra_pitch + 1) & ~1);
    if (!SDL_ConvertPixelsAndColorspace(pattern->w, pattern->h, pattern->format, SDL_COLORSPACE_SRGB, 0, pattern->pixels, pattern->pitch, SDL_PIXELFORMAT_P010, colorspace, 0, yuv1, yuv1_pitch)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't convert %s to %s: %s", SDL_GetPixelFormatName(pattern->format), SDL_GetPixelFormatName(SDL_PIXELFORMAT_P010), SDL_GetError());
        goto done;
    }
    /* Going through XRGB2101010 format during P010 conversion is slightly lossy, so use looser tolerance here */
    if (!verify_yuv_data(SDL_PIXELFORMAT_P010, colorspace, yuv1, yuv1_pitch, pattern, loose_tolerance)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed conversion from RGB to %s", SDL_GetPixelFormatName(SDL_PIXELFORMAT_P010));
        goto done;
    }

    result = true;

done:
    SDL_free(yuv1);
    SDL_free(yuv2);
    SDL_DestroySurface(pattern);
    return result;
}

static bool run_colorspace_test(void)
{
    bool result = false;
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct {
        const char *name;
        SDL_Colorspace colorspace;
    } colorspaces[] = {
#define COLORSPACE(X) { #X, X }
        COLORSPACE(SDL_COLORSPACE_JPEG),
#if 0 /* We don't support converting color primaries here */
        COLORSPACE(SDL_COLORSPACE_BT601_LIMITED),
        COLORSPACE(SDL_COLORSPACE_BT601_FULL),
#endif
        COLORSPACE(SDL_COLORSPACE_BT709_LIMITED),
        COLORSPACE(SDL_COLORSPACE_BT709_FULL)
#undef COLORSPACE
    };
    SDL_Surface *rgb = NULL;
    SDL_Surface *yuv = NULL;
    SDL_Texture *texture = NULL;
    int allowed_error = 2;
    int i;

    if (!SDL_CreateWindowAndRenderer("testyuv", 320, 240, 0, &window, &renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
        goto done;
    }

    rgb = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_XRGB8888);
    if (!rgb) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create RGB surface: %s", SDL_GetError());
        goto done;
    }
    SDL_FillSurfaceRect(rgb, NULL, SDL_MapSurfaceRGB(rgb, 255, 0, 0));

    for (i = 0; i < SDL_arraysize(colorspaces); ++i) {
        bool next = false;
        Uint8 r, g, b, a;

        SDL_Log("Checking colorspace %s", colorspaces[i].name);

        yuv = SDL_ConvertSurfaceAndColorspace(rgb, SDL_PIXELFORMAT_NV12, NULL, colorspaces[i].colorspace, 0);
        if (!yuv) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create YUV surface: %s", SDL_GetError());
            goto done;
        }

        if (!SDL_ReadSurfacePixel(yuv, 0, 0, &r, &g, &b, &a)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't read YUV surface: %s", SDL_GetError());
            goto done;
        }

        if (SDL_abs((int)r - 255) > allowed_error ||
            SDL_abs((int)g - 0) > allowed_error ||
            SDL_abs((int)b - 0) > allowed_error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed color conversion, expected 255,0,0, got %d,%d,%d", r, g, b);
        }

        texture = SDL_CreateTextureFromSurface(renderer, yuv);
        if (!texture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create YUV texture: %s", SDL_GetError());
            goto done;
        }

        SDL_DestroySurface(yuv);
        yuv = NULL;

        SDL_RenderTexture(renderer, texture, NULL, NULL);
        yuv = SDL_RenderReadPixels(renderer, NULL);

        if (!SDL_ReadSurfacePixel(yuv, 0, 0, &r, &g, &b, &a)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't read YUV surface: %s", SDL_GetError());
            goto done;
        }

        if (SDL_abs((int)r - 255) > allowed_error ||
            SDL_abs((int)g - 0) > allowed_error ||
            SDL_abs((int)b - 0) > allowed_error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed renderer color conversion, expected 255,0,0, got %d,%d,%d", r, g, b);
        }

        while (!next) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        result = true;
                        goto done;
                    }
                    if (event.key.key == SDLK_SPACE) {
                        next = true;
                    }
                    break;
                case SDL_EVENT_QUIT:
                    result = true;
                    goto done;
                }
            }

            SDL_RenderTexture(renderer, texture, NULL, NULL);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, 4, 4, colorspaces[i].name);
            SDL_RenderPresent(renderer);
            SDL_Delay(10);
        }

        SDL_DestroyTexture(texture);
        texture = NULL;
    }

    result = true;

done:
    SDL_DestroySurface(rgb);
    SDL_DestroySurface(yuv);
    SDL_DestroyTexture(texture);
    SDL_Quit();
    return result;
}

int main(int argc, char **argv)
{
    struct
    {
        bool enable_intrinsics;
        int pattern_size;
        int extra_pitch;
    } automated_test_params[] = {
        /* Test: single pixel */
        { false, 1, 0 },
        /* Test: even width and height */
        { false, 2, 0 },
        { false, 4, 0 },
        /* Test: odd width and height */
        { false, 1, 0 },
        { false, 3, 0 },
        /* Test: even width and height, extra pitch */
        { false, 2, 3 },
        { false, 4, 3 },
        /* Test: odd width and height, extra pitch */
        { false, 1, 3 },
        { false, 3, 3 },
        /* Test: even width and height with intrinsics */
        { true, 32, 0 },
        /* Test: odd width and height with intrinsics */
        { true, 33, 0 },
        { true, 37, 0 },
        /* Test: even width and height with intrinsics, extra pitch */
        { true, 32, 3 },
        /* Test: odd width and height with intrinsics, extra pitch */
        { true, 33, 3 },
        { true, 37, 3 },
    };
    char *filename = NULL;
    SDL_Surface *original;
    SDL_Surface *converted;
    SDL_Surface *bmp;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *output[3];
    const char *titles[3] = { "ORIGINAL", "SOFTWARE", "HARDWARE" };
    char title[128];
    YUV_CONVERSION_MODE yuv_mode;
    const char *yuv_mode_name;
    Uint32 yuv_format = SDL_PIXELFORMAT_YV12;
    const char *yuv_format_name;
    SDL_Colorspace yuv_colorspace;
    Uint32 rgb_format = SDL_PIXELFORMAT_RGBX8888;
    SDL_Colorspace rgb_colorspace = SDL_COLORSPACE_SRGB;
    SDL_PropertiesID props;
    bool monochrome = false;
    int luminance = 100;
    int current = 0;
    int pitch;
    Uint8 *raw_yuv;
    Uint64 then, now;
    int i, iterations = 100;
    bool should_run_automated_tests = false;
    bool should_run_colorspace_test = false;
    SDLTest_CommonState *state;

    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, 0);
    if (!state) {
        return 1;
    }

    /* Parse commandline */
    for (i = 1; i < argc;) {
        int consumed;

        consumed = SDLTest_CommonArg(state, i);
        if (!consumed) {
            if (SDL_strcmp(argv[i], "--jpeg") == 0) {
                SetYUVConversionMode(YUV_CONVERSION_JPEG);
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--bt601") == 0) {
                SetYUVConversionMode(YUV_CONVERSION_BT601);
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--bt709") == 0) {
                SetYUVConversionMode(YUV_CONVERSION_BT709);
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--bt2020") == 0) {
                SetYUVConversionMode(YUV_CONVERSION_BT2020);
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--auto") == 0) {
                SetYUVConversionMode(YUV_CONVERSION_AUTOMATIC);
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--yv12") == 0) {
                yuv_format = SDL_PIXELFORMAT_YV12;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--iyuv") == 0) {
                yuv_format = SDL_PIXELFORMAT_IYUV;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--yuy2") == 0) {
                yuv_format = SDL_PIXELFORMAT_YUY2;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--uyvy") == 0) {
                yuv_format = SDL_PIXELFORMAT_UYVY;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--yvyu") == 0) {
                yuv_format = SDL_PIXELFORMAT_YVYU;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--nv12") == 0) {
                yuv_format = SDL_PIXELFORMAT_NV12;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--nv21") == 0) {
                yuv_format = SDL_PIXELFORMAT_NV21;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--rgb555") == 0) {
                rgb_format = SDL_PIXELFORMAT_XRGB1555;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--rgb565") == 0) {
                rgb_format = SDL_PIXELFORMAT_RGB565;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--rgb24") == 0) {
                rgb_format = SDL_PIXELFORMAT_RGB24;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--argb") == 0) {
                rgb_format = SDL_PIXELFORMAT_ARGB8888;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--abgr") == 0) {
                rgb_format = SDL_PIXELFORMAT_ABGR8888;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--rgba") == 0) {
                rgb_format = SDL_PIXELFORMAT_RGBA8888;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--bgra") == 0) {
                rgb_format = SDL_PIXELFORMAT_BGRA8888;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--monochrome") == 0) {
                monochrome = true;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--luminance") == 0 && argv[i+1]) {
                luminance = SDL_atoi(argv[i+1]);
                consumed = 2;
            } else if (SDL_strcmp(argv[i], "--automated") == 0) {
                should_run_automated_tests = true;
                consumed = 1;
            } else if (SDL_strcmp(argv[i], "--colorspace-test") == 0) {
                should_run_colorspace_test = true;
                consumed = 1;
            } else if (!filename) {
                filename = argv[i];
                consumed = 1;
            }
        }
        if (consumed <= 0) {
            static const char *options[] = {
                "[--jpeg|--bt601|--bt709|--bt2020|--auto]",
                "[--yv12|--iyuv|--yuy2|--uyvy|--yvyu|--nv12|--nv21]",
                "[--rgb555|--rgb565|--rgb24|--argb|--abgr|--rgba|--bgra]",
                "[--monochrome] [--luminance N%]",
                "[--automated] [--colorspace-test]",
                "[sample.bmp]",
                NULL,
            };
            SDLTest_CommonLogUsage(state, argv[0], options);
            SDL_Quit();
            SDLTest_CommonDestroyState(state);
            return 1;
        }
        i += consumed;
    }

    /* Run automated tests */
    if (should_run_automated_tests) {
        for (i = 0; i < (int)SDL_arraysize(automated_test_params); ++i) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Running automated test, pattern size %d, extra pitch %d, intrinsics %s",
                        automated_test_params[i].pattern_size,
                        automated_test_params[i].extra_pitch,
                        automated_test_params[i].enable_intrinsics ? "enabled" : "disabled");
            if (!run_automated_tests(automated_test_params[i].pattern_size, automated_test_params[i].extra_pitch)) {
                return 2;
            }
        }
        return 0;
    }

    if (should_run_colorspace_test) {
        if (!run_colorspace_test()) {
            return 2;
        }
        return 0;
    }

    filename = GetResourceFilename(filename, "testyuv.bmp");
    bmp = SDL_LoadBMP(filename);
    original = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(bmp);
    if (!original) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", filename, SDL_GetError());
        return 3;
    }

    yuv_mode = GetYUVConversionModeForResolution(original->w, original->h);
    switch (yuv_mode) {
    case YUV_CONVERSION_JPEG:
        yuv_mode_name = "JPEG";
        break;
    case YUV_CONVERSION_BT601:
        yuv_mode_name = "BT.601";
        break;
    case YUV_CONVERSION_BT709:
        yuv_mode_name = "BT.709";
        break;
    case YUV_CONVERSION_BT2020:
        yuv_mode_name = "BT.2020";
        yuv_format = SDL_PIXELFORMAT_P010;
        rgb_format = SDL_PIXELFORMAT_XBGR2101010;
        rgb_colorspace = SDL_COLORSPACE_HDR10;
        break;
    default:
        yuv_mode_name = "UNKNOWN";
        break;
    }
    yuv_colorspace = GetColorspaceForYUVConversionMode(yuv_mode);

    raw_yuv = SDL_calloc(1, MAX_YUV_SURFACE_SIZE(original->w, original->h, 0));
    ConvertRGBtoYUV(yuv_format, original->pixels, original->pitch, raw_yuv, original->w, original->h, yuv_mode, monochrome, luminance);
    pitch = CalculateYUVPitch(yuv_format, original->w);

    converted = SDL_CreateSurface(original->w, original->h, rgb_format);
    if (!converted) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create converted surface: %s", SDL_GetError());
        return 3;
    }

    then = SDL_GetTicks();
    for (i = 0; i < iterations; ++i) {
        SDL_ConvertPixelsAndColorspace(original->w, original->h, yuv_format, yuv_colorspace, 0, raw_yuv, pitch, rgb_format, rgb_colorspace, 0, converted->pixels, converted->pitch);
    }
    now = SDL_GetTicks();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d iterations in %" SDL_PRIu64 " ms, %.2fms each", iterations, (now - then), (float)(now - then) / iterations);

    window = SDL_CreateWindow("YUV test", original->w, original->h, 0);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window: %s", SDL_GetError());
        return 4;
    }

    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create renderer: %s", SDL_GetError());
        return 4;
    }

    output[0] = SDL_CreateTextureFromSurface(renderer, original);
    output[1] = SDL_CreateTextureFromSurface(renderer, converted);
    props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, yuv_colorspace);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, yuv_format);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, original->w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, original->h);
    output[2] = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (!output[0] || !output[1] || !output[2]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't set create texture: %s", SDL_GetError());
        return 5;
    }
    SDL_UpdateTexture(output[2], NULL, raw_yuv, pitch);

    yuv_format_name = SDL_GetPixelFormatName(yuv_format);
    if (SDL_strncmp(yuv_format_name, "SDL_PIXELFORMAT_", 16) == 0) {
        yuv_format_name += 16;
    }

    {
        int done = 0;
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event) > 0) {
                if (event.type == SDL_EVENT_QUIT) {
                    done = 1;
                }
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_ESCAPE) {
                        done = 1;
                    } else if (event.key.key == SDLK_LEFT) {
                        --current;
                    } else if (event.key.key == SDLK_RIGHT) {
                        ++current;
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.x < (original->w / 2)) {
                        --current;
                    } else {
                        ++current;
                    }
                }
            }

            /* Handle wrapping */
            if (current < 0) {
                current += SDL_arraysize(output);
            }
            if (current >= SDL_arraysize(output)) {
                current -= SDL_arraysize(output);
            }

            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, output[current], NULL, NULL);
            SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
            if (current == 0) {
                SDLTest_DrawString(renderer, 4, 4, titles[current]);
            } else {
                (void)SDL_snprintf(title, sizeof(title), "%s %s %s", titles[current], yuv_format_name, yuv_mode_name);
                SDLTest_DrawString(renderer, 4, 4, title);
            }
            SDL_RenderPresent(renderer);
            SDL_Delay(10);
        }
    }
    SDL_free(raw_yuv);
    SDL_free(filename);
    SDL_DestroySurface(original);
    SDL_DestroySurface(converted);
    SDLTest_CleanupTextDrawing();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDLTest_CommonDestroyState(state);
    return 0;
}
