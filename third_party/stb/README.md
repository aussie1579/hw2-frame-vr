# stb_image

`stb_image.h` v2.30 from https://github.com/nothings/stb, commit `013ac3beddff3dbffafd5177e7972067cd2b5083`
(2024-05-31, the latest change to this file), SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`.
Public domain / MIT (dual, see the end of the file).

Used by `src/assets/` for the zlib inflate (`stbi_zlib_decode_malloc_guesssize_headerflag`) of files in the game's
`.big` archives and for decoding TGA textures. Compiled once, in `src/assets/stb_image_impl.cpp`.
