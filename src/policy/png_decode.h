#ifndef UMI_PNG_DECODE_H
#define UMI_PNG_DECODE_H
/* PNG decoding for replacements: stb_image (public domain / MIT, third_party/stb_image.h),
   PNG only, reading from memory, no global error strings. */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MAX_DIMENSIONS 8192
/* GCC reports stb's unused static declarations at the end of the translation unit,
   so this one warning stays disabled after the pop. */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "stb_image.h"
#pragma GCC diagnostic pop

#endif
