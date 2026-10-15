#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "input/sprites.h"

// The game's own textures for the VR interface (command, tactics, stance and formation icons, taskbar icons), read
// at runtime from its Relic "_ARCHIVE" v2 .big files (format: tools/big_extract.py) - never copied or modified.
namespace hw2vr::assets {

class Archive {
public:
    bool open(const std::wstring& path);  // reads the table of contents only
    bool is_open() const { return !files_.empty(); }
    // A file by its path inside the archive ("ui\newui\taskbar\selectall.tga", any case, / or \).
    bool read(const std::string& name, std::vector<uint8_t>* out) const;

private:
    struct Entry {
        uint8_t flags;  // 0 stored, 16/32 zlib
        uint32_t offset, csize, usize;
    };
    std::wstring path_;
    std::unordered_map<std::string, Entry> files_;
};

// Homeworld2.big next to the running game (HomeworldRM\Data), opened once.
const Archive& game_archive();

// DDS (DXT1/3/5 or 32-bit BGRA, top mip level) or TGA, as a straight-alpha image (rows top to bottom).
bool decode_image(const std::vector<uint8_t>& file, const std::string& name, sprites::Image* out);

// One of the game's icons. Button icons are strips of state cells stacked downwards (the button's uvRect and
// stateIconCells in ui\newui\newtaskbar.lua): the 128 x 512 command and formation icons have four 128-pixel cells
// (0 disabled, 1 normal, 2 hover, 3 pressed), the 128 x 1024 HUD and stance icons eight, menu_ico_sensors4k five
// of 136 pixels; small taskbar TGAs are a single image (cell_height 0).
struct IconRef {
    const char* name = "";  // path in Homeworld2.big; "" = no icon
    int cell_height = 128;  // pixels; 0 = the whole image
    int cell = 1;
};
// Loads it from the game archive. Empty image on failure (or no name).
sprites::Image load_icon(const IconRef& icon);

}  // namespace hw2vr::assets
