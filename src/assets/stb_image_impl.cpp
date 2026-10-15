// The one translation unit that compiles stb_image (third_party/stb/README.md): zlib inflate and TGA only.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_TGA
#define STBI_SUPPORT_ZLIB
#define STBI_NO_SIMD
#pragma warning(push, 0)
#include <stb_image.h>
#pragma warning(pop)
