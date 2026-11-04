// Feeds CMD_LOADIMAGE + a JPEG + CMD_GETPROPS through the emulated FIFO the
// way the firmware does (header, then 1000-byte chunks via writeDataToCMD)
// and checks the decoded RGB565 bitmap in RAM_G.
#include "../source/eve/EveEmu.h"
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s image.jpg\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<uint8_t> jpg;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        jpg.insert(jpg.end(), buf, buf + n);
    fclose(f);

    EveEmu emu;
    const uint32_t addr = 200000;
    emu.fifoWrite32(0xFFFFFF24); // CMD_LOADIMAGE
    emu.fifoWrite32(addr);
    emu.fifoWrite32(2); // OPT_NODL
    emu.execute();
    for (size_t off = 0; off < jpg.size(); off += 1000)
    {
        size_t len = std::min<size_t>(1000, jpg.size() - off);
        emu.fifoWriteBytes(&jpg[off], len);
        emu.execute(); // partial words may be pending; that's fine
    }
    while (true) // pad to 32-bit like the driver does
    {
        uint8_t z = 0;
        if (((jpg.size() + 3) & ~3u) == jpg.size()) break;
        emu.fifoWriteBytes(&z, 1);
        jpg.push_back(0);
    }
    emu.fifoWrite32(0xFFFFFF25); // CMD_GETPROPS
    emu.fifoWrite32(0);
    emu.fifoWrite32(0);
    emu.fifoWrite32(0);
    emu.execute();
    uint32_t a, w, h;
    emu.getProps(a, w, h);
    printf("props addr=%u w=%u h=%u unsupported=%u\n", a, w, h, emu.unsupportedCmd);
    if (w == 0 || h == 0) return 1;
    // sample a few pixels: centre should be light (white circle), corner blue-ish stripe
    auto px = [&](int x, int y) {
        uint8_t b[2];
        emu.memRead(addr + (uint32_t)(y * w + x) * 2, b, 2);
        uint16_t v = (uint16_t)(b[0] | (b[1] << 8));
        return v;
    };
    uint16_t c = px((int)w / 2, (int)h / 2), k = px(3, 3);
    printf("centre=0x%04x corner=0x%04x\n", c, k);
    return (c > 0xF000 && k != c) ? 0 : 1;
}
