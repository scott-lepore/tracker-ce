// Single translation unit for the stb_image implementation (JPEG + PNG only),
// used by EveEmu to emulate CMD_LOADIMAGE.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "third_party/stb_image.h"
#include "EveImage.h"

bool eveDecodeImage(const uint8_t* data, size_t len, std::vector<uint8_t>& rgb565, int& w, int& h)
{
    int comp = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 3);
    if (!px)
        return false;
    rgb565.resize((size_t)w * h * 2);
    for (int i = 0; i < w * h; i++)
    {
        uint16_t v = (uint16_t)(((px[3 * i] >> 3) << 11) | ((px[3 * i + 1] >> 2) << 5) | (px[3 * i + 2] >> 3));
        rgb565[2 * i] = (uint8_t)v;
        rgb565[2 * i + 1] = (uint8_t)(v >> 8);
    }
    stbi_image_free(px);
    return true;
}
