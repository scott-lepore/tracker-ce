#include "EveEmu.h"
#include "EveImage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
// Display list opcodes (FT81x)
enum : uint8_t
{
    OP_DISPLAY = 0x00, OP_BITMAP_SOURCE = 0x01, OP_CLEAR_COLOR_RGB = 0x02, OP_TAG = 0x03,
    OP_COLOR_RGB = 0x04, OP_BITMAP_HANDLE = 0x05, OP_CELL = 0x06, OP_BITMAP_LAYOUT = 0x07,
    OP_BITMAP_SIZE = 0x08, OP_ALPHA_FUNC = 0x09, OP_STENCIL_FUNC = 0x0A, OP_BLEND_FUNC = 0x0B,
    OP_STENCIL_OP = 0x0C, OP_POINT_SIZE = 0x0D, OP_LINE_WIDTH = 0x0E, OP_CLEAR_COLOR_A = 0x0F,
    OP_COLOR_A = 0x10, OP_CLEAR_STENCIL = 0x11, OP_CLEAR_TAG = 0x12, OP_STENCIL_MASK = 0x13,
    OP_TAG_MASK = 0x14, OP_BITMAP_TRANSFORM_A = 0x15, OP_BITMAP_TRANSFORM_B = 0x16,
    OP_BITMAP_TRANSFORM_C = 0x17, OP_BITMAP_TRANSFORM_D = 0x18, OP_BITMAP_TRANSFORM_E = 0x19,
    OP_BITMAP_TRANSFORM_F = 0x1A, OP_SCISSOR_XY = 0x1B, OP_SCISSOR_SIZE = 0x1C, OP_CALL = 0x1D,
    OP_JUMP = 0x1E, OP_BEGIN = 0x1F, OP_COLOR_MASK = 0x20, OP_END = 0x21, OP_SAVE_CONTEXT = 0x22,
    OP_RESTORE_CONTEXT = 0x23, OP_RETURN = 0x24, OP_MACRO = 0x25, OP_CLEAR = 0x26,
    OP_VERTEX_FORMAT = 0x27, OP_BITMAP_LAYOUT_H = 0x28, OP_BITMAP_SIZE_H = 0x29,
    OP_PALETTE_SOURCE = 0x2A, OP_VERTEX_TRANSLATE_X = 0x2B, OP_VERTEX_TRANSLATE_Y = 0x2C,
    OP_NOP = 0x2D,
    // private extension: CMD_GRADIENT expanded by this emulator (5 words)
    OP_EXT_GRADIENT = 0x3E,
};
enum : uint8_t
{
    PRIM_BITMAPS = 1, PRIM_POINTS = 2, PRIM_LINES = 3, PRIM_LINE_STRIP = 4,
    PRIM_EDGE_STRIP_R = 5, PRIM_EDGE_STRIP_L = 6, PRIM_EDGE_STRIP_A = 7, PRIM_EDGE_STRIP_B = 8,
    PRIM_RECTS = 9,
};
enum : uint8_t
{
    FMT_ARGB1555 = 0, FMT_L1 = 1, FMT_L4 = 2, FMT_L8 = 3, FMT_RGB332 = 4, FMT_ARGB2 = 5,
    FMT_ARGB4 = 6, FMT_RGB565 = 7, FMT_PALETTED565 = 14, FMT_PALETTED4444 = 15,
    FMT_PALETTED8 = 16, FMT_L2 = 17, FMT_ARGB8_SNAPSHOT = 0x20,
};
constexpr uint32_t CMD_DLSTART = 0xFFFFFF00, CMD_SWAP = 0xFFFFFF01, CMD_INTERRUPT = 0xFFFFFF02,
                   CMD_BGCOLOR = 0xFFFFFF09, CMD_FGCOLOR = 0xFFFFFF0A, CMD_GRADIENT = 0xFFFFFF0B,
                   CMD_TEXT = 0xFFFFFF0C, CMD_MEMSET = 0xFFFFFF1B, CMD_MEMZERO = 0xFFFFFF1C,
                   CMD_MEMCPY = 0xFFFFFF1D, CMD_APPEND = 0xFFFFFF1E, CMD_SNAPSHOT = 0xFFFFFF1F,
                   CMD_INFLATE = 0xFFFFFF22, CMD_GETPTR = 0xFFFFFF23, CMD_LOADIMAGE = 0xFFFFFF24,
                   CMD_GETPROPS = 0xFFFFFF25, CMD_LOADIDENTITY = 0xFFFFFF26,
                   CMD_TRANSLATE = 0xFFFFFF27, CMD_SCALE = 0xFFFFFF28, CMD_ROTATE = 0xFFFFFF29,
                   CMD_SETMATRIX = 0xFFFFFF2A, CMD_SETFONT = 0xFFFFFF2B, CMD_NUMBER = 0xFFFFFF2E,
                   CMD_COLDSTART = 0xFFFFFF32, CMD_SETROTATE = 0xFFFFFF36, CMD_SNAPSHOT2 = 0xFFFFFF37,
                   CMD_SETBASE = 0xFFFFFF38, CMD_SETFONT2 = 0xFFFFFF3B, CMD_SETSCRATCH = 0xFFFFFF3C,
                   CMD_ROMFONT = 0xFFFFFF3F, CMD_SYNC = 0xFFFFFF42, CMD_SETBITMAP = 0xFFFFFF43,
                   CMD_MEMWRITE = 0xFFFFFF1A, CMD_MEMCRC = 0xFFFFFF18, CMD_REGREAD = 0xFFFFFF19,
                   CMD_STOP = 0xFFFFFF17, CMD_GETMATRIX = 0xFFFFFF33, CMD_BITMAP_TRANSFORM = 0xFFFFFF21;
constexpr uint16_t OPT_CENTERX = 512, OPT_CENTERY = 1024, OPT_RIGHTX = 2048, OPT_SIGNED = 256;

inline uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline int sext(uint32_t v, int bits)
{
    uint32_t m = 1u << (bits - 1);
    return (int)((v ^ m) - m);
}
struct PrimTimer
{
    uint64_t& ns;
    std::chrono::steady_clock::time_point t0;
    PrimTimer(uint64_t& n, uint64_t& calls) : ns(n), t0(std::chrono::steady_clock::now()) { calls++; }
    ~PrimTimer()
    {
        ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
    }
};
inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

EveEmu::EveEmu()
  : ramG(kRamGSize, 0)
  , ramDL(kRamDlSize, 0)
  , back(kWidth * kHeight, 0)
  , backA(kWidth * kHeight, 0)
  , front(kWidth * kHeight, 0)
{
}

// ------------------------------------------------------------- memory -----
uint8_t* EveEmu::mem(uint32_t addr, uint32_t len)
{
    if (addr + len <= kRamGSize && addr + len >= addr)
        return &ramG[addr];
    if (addr >= kRamDlBase && addr + len <= kRamDlBase + kRamDlSize)
        return &ramDL[addr - kRamDlBase];
    return nullptr;
}

void EveEmu::memWrite(uint32_t addr, const void* data, uint32_t len)
{
    uint8_t* p = mem(addr, len);
    if (p)
        memcpy(p, data, len);
}

void EveEmu::memRead(uint32_t addr, void* out, uint32_t len)
{
    uint8_t* p = mem(addr, len);
    if (p)
        memcpy(out, p, len);
    else
        memset(out, 0, len);
}

// --------------------------------------------------------------- FIFO -----
void EveEmu::fifoWrite32(uint32_t v)
{
    uint8_t b[4];
    wr32(b, v);
    fifo.insert(fifo.end(), b, b + 4);
}

void EveEmu::fifoWriteBytes(const uint8_t* data, size_t len)
{
    fifo.insert(fifo.end(), data, data + len);
}

void EveEmu::fifoWriteString(const char* s, uint32_t len)
{
    uint32_t n = len ? len : (uint32_t)strlen(s);
    fifo.insert(fifo.end(), (const uint8_t*)s, (const uint8_t*)s + n);
    fifo.push_back(0);
    while ((fifo.size() & 3) != 0)
        fifo.push_back(0);
}

void EveEmu::getProps(uint32_t& addr, uint32_t& w, uint32_t& h) const
{
    addr = propsAddr;
    w = propsW;
    h = propsH;
}

void EveEmu::execute()
{
    statExecCalls++;
    while (fifoPos + 4 <= fifo.size())
    {
        size_t pos = fifoPos;
        if (!runCommand(pos))
            break; // incomplete command, wait for more data
        statCommands++;
        fifoPos = pos;
    }
    if (fifoPos == fifo.size())
    {
        fifo.clear();
        fifoPos = 0;
    }
    else if (fifoPos > 65536)
    {
        fifo.erase(fifo.begin(), fifo.begin() + fifoPos);
        fifoPos = 0;
    }
}

void EveEmu::dlPush(uint32_t word)
{
    statDlWords++;
    if (cmdDl + 4 <= kRamDlSize)
    {
        wr32(&ramDL[cmdDl], word);
        cmdDl += 4;
    }
}

// Returns false if the command at pos is not complete yet.
bool EveEmu::runCommand(size_t& pos)
{
    const size_t avail = fifo.size() - pos;
    auto word = [&](size_t i) { return rd32(&fifo[pos + 4 * i]); };
    uint32_t cmd = word(0);

    if (imageDataMode)
    {
        // Raw JPEG/PNG bytes follow CMD_LOADIMAGE. The chip parses the stream
        // to find its end; we collect until the firmware's CMD_GETPROPS (its
        // next command) and decode the whole buffer then. A JPEG cannot
        // contain FF FF FF 25, so the sentinel is safe.
        if (cmd == CMD_GETPROPS)
        {
            imageDataMode = false;
            std::vector<uint8_t> px;
            int w = 0, h = 0;
            if (eveDecodeImage(imageData.data(), imageData.size(), px, w, h))
            {
                memWrite(propsAddr, px.data(), (uint32_t)px.size());
                propsW = (uint32_t)w;
                propsH = (uint32_t)h;
            }
            else
            {
                unsupportedCmd++;
                lastUnsupported = CMD_LOADIMAGE;
            }
            imageData.clear();
        }
        else
        {
            imageData.insert(imageData.end(), &fifo[pos], &fifo[pos + 4]);
            pos += 4;
            return true;
        }
    }

    if ((cmd & 0xFFFFFF00) != 0xFFFFFF00)
    {
        dlPush(cmd); // plain display-list word
        pos += 4;
        return true;
    }

    auto need = [&](size_t words) { return avail >= words * 4; };
    switch (cmd)
    {
        case CMD_DLSTART:
            cmdDl = 0;
            pos += 4;
            return true;
        case CMD_SWAP:
            render();
            pos += 4;
            return true;
        case CMD_COLDSTART:
        case CMD_LOADIDENTITY:
            if (cmd == CMD_LOADIDENTITY)
            {
                mA = 1; mB = 0; mC = 0; mD = 0; mE = 1; mF = 0;
            }
            pos += 4;
            return true;
        case CMD_SETMATRIX:
            cmdSetMatrix();
            pos += 4;
            return true;
        case CMD_SYNC:
        case CMD_STOP:
            pos += 4;
            return true;
        case CMD_SETROTATE:
        case CMD_SETBASE:
        case CMD_SETSCRATCH:
        case CMD_INTERRUPT:
        case CMD_BGCOLOR:
        case CMD_FGCOLOR:
        case CMD_ROTATE:
            if (!need(2)) return false;
            if (cmd == CMD_SETBASE)
                numberBase = word(1) ? word(1) : 10;
            else if (cmd == CMD_ROTATE)
            {
                double ang = (double)(int32_t)word(1) / 65536.0 * 2.0 * M_PI;
                double c = cos(ang), s = sin(ang);
                // current = current * R
                double a = mA * c + mB * s, b = -mA * s + mB * c;
                double d = mD * c + mE * s, e = -mD * s + mE * c;
                mA = a; mB = b; mD = d; mE = e;
            }
            pos += 8;
            return true;
        case CMD_TRANSLATE:
        case CMD_SCALE:
        case CMD_MEMZERO:
        case CMD_APPEND:
        case CMD_INFLATE:
        case CMD_GETPTR:
        case CMD_LOADIMAGE:
        case CMD_REGREAD:
            if (!need(3)) return false;
            if (cmd == CMD_TRANSLATE)
            {
                double tx = (double)(int32_t)word(1) / 65536.0, ty = (double)(int32_t)word(2) / 65536.0;
                mC += mA * tx + mB * ty;
                mF += mD * tx + mE * ty;
            }
            else if (cmd == CMD_SCALE)
            {
                double sx = (double)(int32_t)word(1) / 65536.0, sy = (double)(int32_t)word(2) / 65536.0;
                mA *= sx; mD *= sx; mB *= sy; mE *= sy;
            }
            else if (cmd == CMD_MEMZERO)
            {
                uint8_t* p = mem(word(1), word(2));
                if (p) memset(p, 0, word(2));
            }
            else if (cmd == CMD_APPEND)
            {
                uint32_t ptr = word(1), num = word(2) & ~3u;
                const uint8_t* src = mem(ptr, num);
                if (src)
                    for (uint32_t i = 0; i < num; i += 4)
                        dlPush(rd32(src + i));
            }
            else if (cmd == CMD_LOADIMAGE)
            {
                propsAddr = word(1);
                propsW = propsH = 0;
                imageDataMode = true;
                imageData.clear();
            }
            pos += 12;
            return true;
        case CMD_MEMCPY:
        case CMD_MEMSET:
        case CMD_SETFONT2:
        case CMD_SETBITMAP:
        case CMD_GETPROPS:
        case CMD_NUMBER:
        case CMD_SETFONT:
        case CMD_MEMCRC:
            if (!need(4)) return false;
            if (cmd == CMD_MEMCPY)
            {
                uint32_t dst = word(1), src = word(2), num = word(3);
                uint8_t* d = mem(dst, num);
                uint8_t* s = mem(src, num);
                if (d && s) memmove(d, s, num);
            }
            else if (cmd == CMD_MEMSET)
            {
                uint8_t* p = mem(word(1), word(3));
                if (p) memset(p, (int)(word(2) & 0xFF), word(3));
            }
            else if (cmd == CMD_SETFONT2)
                cmdSetFont2(word(1), word(2), word(3));
            else if (cmd == CMD_SETFONT)
                cmdSetFont2(word(1), word(2), 0);
            else if (cmd == CMD_SETBITMAP)
                cmdSetBitmap(word(1), (uint16_t)(word(2) & 0xFFFF), (uint16_t)(word(2) >> 16),
                             (uint16_t)(word(3) & 0xFFFF));
            else if (cmd == CMD_GETPROPS)
            {
                // results are written back into the FIFO slots on the chip
                wr32(&fifo[pos + 4], propsAddr);
                wr32(&fifo[pos + 8], propsW);
                wr32(&fifo[pos + 12], propsH);
            }
            else if (cmd == CMD_NUMBER)
            {
                uint32_t xy = word(1), of = word(2);
                cmdNumber((int16_t)(xy & 0xFFFF), (int16_t)(xy >> 16), (uint16_t)(of & 0xFFFF),
                          (uint16_t)(of >> 16), (int32_t)word(3));
            }
            pos += 16;
            return true;
        case CMD_GRADIENT:
        case CMD_SNAPSHOT2:
        case CMD_MEMWRITE:
            if (!need(5)) return false;
            if (cmd == CMD_GRADIENT)
            {
                dlPush((uint32_t)OP_EXT_GRADIENT << 24);
                for (int i = 1; i <= 4; i++)
                    dlPush(word(i));
            }
            else if (cmd == CMD_SNAPSHOT2)
            {
                uint32_t xy = word(3), wh = word(4);
                cmdSnapshot2(word(1), word(2), (int16_t)(xy & 0xFFFF), (int16_t)(xy >> 16),
                             (int16_t)(wh & 0xFFFF), (int16_t)(wh >> 16));
            }
            else
            {
                // CMD_MEMWRITE ptr num data...: variable length
                uint32_t num = word(2);
                size_t total = 12 + ((num + 3) & ~3u);
                if (avail < total) return false;
                memWrite(word(1), &fifo[pos + 12], num);
                pos += total;
                return true;
            }
            pos += 20;
            return true;
        case CMD_TEXT:
        {
            if (!need(3)) return false;
            size_t strStart = pos + 12;
            size_t end = strStart;
            while (end < fifo.size() && fifo[end] != 0)
                end++;
            if (end >= fifo.size()) return false; // terminator not yet received
            uint32_t xy = word(1), of = word(2);
            cmdText((int16_t)(xy & 0xFFFF), (int16_t)(xy >> 16), (uint16_t)(of & 0xFFFF),
                    (uint16_t)(of >> 16), (const char*)&fifo[strStart]);
            size_t len = end - strStart + 1;
            pos = strStart + ((len + 3) & ~(size_t)3);
            return true;
        }
        case CMD_SNAPSHOT:
            if (!need(2)) return false;
            cmdSnapshot2(FMT_ARGB8_SNAPSHOT, word(1), 0, 0, kWidth, kHeight);
            pos += 8;
            return true;
        case CMD_ROMFONT:
            if (!need(3)) return false;
            pos += 12;
            return true;
        case CMD_GETMATRIX:
            if (!need(7)) return false;
            pos += 28;
            return true;
        case CMD_BITMAP_TRANSFORM:
            if (!need(14)) return false;
            pos += 56;
            return true;
        default:
            unsupportedCmd++;
            lastUnsupported = cmd;
            pos += 4; // unknown: skip the opcode and hope the stream resyncs
            return true;
    }
}

// --------------------------------------------------- co-processor cmds ----
void EveEmu::cmdSetFont2(uint32_t handle, uint32_t ptr, uint32_t firstchar)
{
    if (handle >= 32) return;
    const uint8_t* blk = mem(ptr, 148);
    if (!blk) return;
    fontPtr[handle] = ptr;
    fontFirst[handle] = firstchar;
    // FT81x semantics: glyph data follows the 148-byte block, and the chip
    // points BITMAP_SOURCE firstchar glyphs *before* it so that cell == ASCII
    // code works everywhere (firmware text and CMD_TEXT alike). The address
    // goes negative and wraps in the 22-bit address space; texel fetches wrap
    // the same way. The block's own data pointer (offset 144) is ignored.
    uint32_t format = rd32(blk + 128), stride = rd32(blk + 132), width = rd32(blk + 136),
             height = rd32(blk + 140);
    dlPush(((uint32_t)OP_BITMAP_HANDLE << 24) | handle);
    dlPush(((uint32_t)OP_BITMAP_SOURCE << 24) |
           ((ptr + 148 - firstchar * stride * height) & 0x3FFFFF));
    dlPush(((uint32_t)OP_BITMAP_LAYOUT << 24) | ((format & 31) << 19) | ((stride & 1023) << 9) |
           (height & 511));
    dlPush(((uint32_t)OP_BITMAP_SIZE << 24) | ((width & 511) << 9) | (height & 511));
}

uint32_t EveEmu::bppOf(uint8_t f)
{
    switch (f)
    {
        case FMT_L1: return 1;
        case FMT_L2: return 2;
        case FMT_L4: return 4;
        case FMT_L8: case FMT_RGB332: case FMT_ARGB2: case FMT_PALETTED565:
        case FMT_PALETTED4444: case FMT_PALETTED8: return 8;
        case FMT_ARGB1555: case FMT_ARGB4: case FMT_RGB565: return 16;
        default: return 8;
    }
}

void EveEmu::cmdSetBitmap(uint32_t source, uint16_t fmt, uint16_t w, uint16_t h)
{
    uint32_t stride = (w * bppOf((uint8_t)fmt) + 7) / 8;
    dlPush(((uint32_t)OP_BITMAP_SOURCE << 24) | (source & 0x3FFFFF));
    dlPush(((uint32_t)OP_BITMAP_LAYOUT_H << 24) | (((stride >> 10) & 3) << 2) | ((h >> 9) & 3));
    dlPush(((uint32_t)OP_BITMAP_LAYOUT << 24) | ((fmt & 31) << 19) | ((stride & 1023) << 9) |
           (h & 511));
    dlPush(((uint32_t)OP_BITMAP_SIZE_H << 24) | (((w >> 9) & 3) << 2) | ((h >> 9) & 3));
    dlPush(((uint32_t)OP_BITMAP_SIZE << 24) | ((w & 511) << 9) | (h & 511)); // NEAREST, BORDER
}

void EveEmu::cmdSetMatrix()
{
    // Graphics engine wants screen -> bitmap, i.e. the inverse of the
    // bitmap -> screen matrix built by TRANSLATE/SCALE/ROTATE.
    double det = mA * mE - mB * mD;
    if (fabs(det) < 1e-9) det = 1e-9;
    double ia = mE / det, ib = -mB / det, id = -mD / det, ie = mA / det;
    double ic = -(ia * mC + ib * mF), iff = -(id * mC + ie * mF);
    auto fx88 = [](double v, int bits) {
        int32_t q = (int32_t)lrint(v * 256.0);
        return (uint32_t)q & ((1u << bits) - 1);
    };
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_A << 24) | fx88(ia, 17));
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_B << 24) | fx88(ib, 17));
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_C << 24) | fx88(ic, 24));
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_D << 24) | fx88(id, 17));
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_E << 24) | fx88(ie, 17));
    dlPush(((uint32_t)OP_BITMAP_TRANSFORM_F << 24) | fx88(iff, 24));
}

void EveEmu::cmdText(int16_t x, int16_t y, uint16_t font, uint16_t opt, const char* s)
{
    font &= 31;
    uint32_t ptr = fontPtr[font];
    if (ptr == 0 && font >= 16)
    {
        // ROM font requested; alias to the loaded custom font with the largest glyphs
        uint32_t best = 0, bestH = 0;
        for (uint32_t h = 0; h < 32; h++)
        {
            const uint8_t* b = fontPtr[h] ? mem(fontPtr[h], 148) : nullptr;
            if (b && rd32(b + 140) >= bestH)
            {
                bestH = rd32(b + 140);
                best = h;
            }
        }
        if (bestH == 0) return;
        font = (uint16_t)best;
        ptr = fontPtr[font];
    }
    const uint8_t* blk = mem(ptr, 148);
    if (!blk) return;
    uint32_t height = rd32(blk + 140);
    int total = 0;
    for (const char* p = s; *p; p++)
        total += blk[(uint8_t)*p & 127];
    if (opt & OPT_CENTERX) x -= (total - 1) / 2; // matches the chip's rounding (verified vs device)
    else if (opt & OPT_RIGHTX) x -= total - 1;
    if (opt & OPT_CENTERY) y -= (int)height / 2;

    dlPush((uint32_t)OP_SAVE_CONTEXT << 24);
    dlPush(((uint32_t)OP_BITMAP_HANDLE << 24) | font);
    dlPush(((uint32_t)OP_BEGIN << 24) | PRIM_BITMAPS);
    bool fmtSet = false;
    int cx = x;
    for (const char* p = s; *p; p++)
    {
        uint8_t ch = (uint8_t)*p & 127;
        uint8_t w = blk[ch];
        uint8_t cell = ch;
        if (w)
        {
            if (cx >= 0 && cx <= 511 && y >= 0 && y <= 511)
            {
                dlPush((2u << 30) | ((uint32_t)cx << 21) | ((uint32_t)y << 12) | ((uint32_t)font << 7) | cell);
            }
            else
            {
                if (!fmtSet)
                {
                    dlPush(((uint32_t)OP_VERTEX_FORMAT << 24) | 0);
                    fmtSet = true;
                }
                dlPush(((uint32_t)OP_CELL << 24) | cell);
                dlPush((1u << 30) | (((uint32_t)cx & 32767) << 15) | ((uint32_t)y & 32767));
            }
        }
        cx += w;
    }
    dlPush((uint32_t)OP_END << 24);
    dlPush((uint32_t)OP_RESTORE_CONTEXT << 24);
}

void EveEmu::cmdNumber(int16_t x, int16_t y, uint16_t font, uint16_t opt, int32_t n)
{
    char buf[40];
    const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint32_t base = numberBase < 2 || numberBase > 36 ? 10 : numberBase;
    bool neg = (opt & OPT_SIGNED) && n < 0;
    uint32_t v = neg ? (uint32_t)(-(int64_t)n) : (uint32_t)n;
    int minDigits = opt & 0xFF;
    char tmp[36];
    int len = 0;
    do
    {
        tmp[len++] = digits[v % base];
        v /= base;
    } while (v && len < 34);
    while (len < minDigits && len < 34)
        tmp[len++] = '0';
    int o = 0;
    if (neg) buf[o++] = '-';
    while (len) buf[o++] = tmp[--len];
    buf[o] = 0;
    cmdText(x, y, font, (uint16_t)(opt & ~0xFF), buf);
}

void EveEmu::cmdSnapshot2(uint32_t fmt, uint32_t ptr, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            int sx = x + i, sy = y + j;
            uint32_t px = (sx >= 0 && sx < kWidth && sy >= 0 && sy < kHeight) ? front[sy * kWidth + sx] : 0;
            uint8_t r = (uint8_t)(px >> 16), g = (uint8_t)(px >> 8), b = (uint8_t)px;
            if (fmt == FMT_ARGB8_SNAPSHOT)
            {
                uint8_t out[4] = { b, g, r, 255 };
                memWrite(ptr + (uint32_t)(j * w + i) * 4, out, 4);
            }
            else if (fmt == FMT_RGB565)
            {
                uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                uint8_t out[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
                memWrite(ptr + (uint32_t)(j * w + i) * 2, out, 2);
            }
            else // ARGB4
            {
                uint16_t v = (uint16_t)(0xF000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
                uint8_t out[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
                memWrite(ptr + (uint32_t)(j * w + i) * 2, out, 2);
            }
        }
    }
}

// ------------------------------------------------------------- render -----
void EveEmu::render()
{
    auto t0 = std::chrono::steady_clock::now();
    gs = GState();
    stackDepth = 0;
    primitive = 0;
    vtxCount = 0;
    stripHas = false;

    uint32_t pc = 0;
    const uint32_t end = std::min(cmdDl, kRamDlSize);
    while (pc + 4 <= end)
    {
        uint32_t w = rd32(&ramDL[pc]);
        pc += 4;
        uint32_t top = w >> 30;
        if (top == 1) // VERTEX2F
        {
            int x = sext((w >> 15) & 32767, 15), y = sext(w & 32767, 15);
            int sh = 4 - gs.vertexFormat;
            execPrimitiveVertex(x << sh, y << sh);
            continue;
        }
        if (top == 2) // VERTEX2II
        {
            int x = (int)((w >> 21) & 511), y = (int)((w >> 12) & 511);
            uint8_t handle = (uint8_t)((w >> 7) & 31), cell = (uint8_t)(w & 127);
            uint8_t sh = gs.handle, sc = gs.cell;
            gs.handle = handle;
            gs.cell = cell;
            execPrimitiveVertex(x << 4, y << 4);
            gs.handle = sh;
            gs.cell = sc;
            continue;
        }
        uint8_t op = (uint8_t)(w >> 24);
        uint32_t arg = w & 0xFFFFFF;
        switch (op)
        {
            case OP_DISPLAY: pc = end; break;
            case OP_BITMAP_SOURCE: handles[gs.handle].source = arg & 0x3FFFFF; break;
            case OP_CLEAR_COLOR_RGB: gs.cr = (uint8_t)(arg >> 16); gs.cg = (uint8_t)(arg >> 8); gs.cb = (uint8_t)arg; break;
            case OP_TAG: case OP_TAG_MASK: case OP_CLEAR_TAG: case OP_STENCIL_FUNC:
            case OP_STENCIL_OP: case OP_CLEAR_STENCIL: case OP_STENCIL_MASK: case OP_ALPHA_FUNC:
            case OP_NOP: case OP_MACRO: break;
            case OP_COLOR_RGB: gs.r = (uint8_t)(arg >> 16); gs.g = (uint8_t)(arg >> 8); gs.b = (uint8_t)arg; break;
            case OP_BITMAP_HANDLE: gs.handle = (uint8_t)(arg & 31); break;
            case OP_CELL: gs.cell = (uint8_t)(arg & 127); break;
            case OP_BITMAP_LAYOUT:
            {
                Handle& h = handles[gs.handle];
                h.format = (uint8_t)((arg >> 19) & 31);
                h.stride = (h.stride & ~1023u) | ((arg >> 9) & 1023);
                h.height = (h.height & ~511u) | (arg & 511);
                break;
            }
            case OP_BITMAP_LAYOUT_H:
            {
                Handle& h = handles[gs.handle];
                h.stride = (h.stride & 1023) | (((arg >> 2) & 3) << 10);
                h.height = (h.height & 511) | ((arg & 3) << 9);
                break;
            }
            case OP_BITMAP_SIZE:
            {
                Handle& h = handles[gs.handle];
                h.filter = (uint8_t)((arg >> 20) & 1);
                h.wrapX = (uint8_t)((arg >> 19) & 1);
                h.wrapY = (uint8_t)((arg >> 18) & 1);
                h.width = (h.width & ~511u) | ((arg >> 9) & 511);
                h.sizeH = (h.sizeH & ~511u) | (arg & 511);
                break;
            }
            case OP_BITMAP_SIZE_H:
            {
                Handle& h = handles[gs.handle];
                h.width = (h.width & 511) | (((arg >> 2) & 3) << 9);
                h.sizeH = (h.sizeH & 511) | ((arg & 3) << 9);
                break;
            }
            case OP_BLEND_FUNC: gs.blendSrc = (uint8_t)((arg >> 3) & 7); gs.blendDst = (uint8_t)(arg & 7); break;
            case OP_POINT_SIZE: gs.pointSize = (int)(arg & 8191); break;
            case OP_LINE_WIDTH: gs.lineWidth = (int)(arg & 4095); break;
            case OP_CLEAR_COLOR_A: gs.ca = (uint8_t)arg; break;
            case OP_COLOR_A: gs.a = (uint8_t)arg; break;
            case OP_BITMAP_TRANSFORM_A: gs.ta = sext(arg & 0x1FFFF, 17) / 256.0; break;
            case OP_BITMAP_TRANSFORM_B: gs.tb = sext(arg & 0x1FFFF, 17) / 256.0; break;
            case OP_BITMAP_TRANSFORM_C: gs.tc = sext(arg & 0xFFFFFF, 24) / 256.0; break;
            case OP_BITMAP_TRANSFORM_D: gs.td = sext(arg & 0x1FFFF, 17) / 256.0; break;
            case OP_BITMAP_TRANSFORM_E: gs.te = sext(arg & 0x1FFFF, 17) / 256.0; break;
            case OP_BITMAP_TRANSFORM_F: gs.tf = sext(arg & 0xFFFFFF, 24) / 256.0; break;
            case OP_SCISSOR_XY: gs.scX = (int)((arg >> 11) & 2047); gs.scY = (int)(arg & 2047); break;
            case OP_SCISSOR_SIZE: gs.scW = (int)((arg >> 12) & 4095); gs.scH = (int)(arg & 4095); break;
            case OP_BEGIN: primitive = (uint8_t)(arg & 15); vtxCount = 0; stripHas = false; break;
            case OP_END: primitive = 0; vtxCount = 0; stripHas = false; break;
            case OP_COLOR_MASK:
                gs.maskR = (arg >> 3) & 1; gs.maskG = (arg >> 2) & 1; gs.maskB = (arg >> 1) & 1; gs.maskA = arg & 1;
                break;
            case OP_SAVE_CONTEXT:
                if (stackDepth < 4) stack[stackDepth++] = gs;
                break;
            case OP_RESTORE_CONTEXT:
                if (stackDepth > 0) gs = stack[--stackDepth];
                break;
            case OP_CLEAR:
                if (arg & 4) clearRegion(true);
                break;
            case OP_VERTEX_FORMAT: gs.vertexFormat = (uint8_t)(arg & 7); break;
            case OP_PALETTE_SOURCE: gs.paletteSource = arg & 0x3FFFFF; break;
            case OP_VERTEX_TRANSLATE_X: gs.vtx = sext(arg & 0x1FFFF, 17); break;
            case OP_VERTEX_TRANSLATE_Y: gs.vty = sext(arg & 0x1FFFF, 17); break;
            case OP_EXT_GRADIENT:
            {
                if (pc + 16 > end) { pc = end; break; }
                uint32_t p0 = rd32(&ramDL[pc]), c0 = rd32(&ramDL[pc + 4]);
                uint32_t p1 = rd32(&ramDL[pc + 8]), c1 = rd32(&ramDL[pc + 12]);
                pc += 16;
                drawGradient((int16_t)(p0 & 0xFFFF), (int16_t)(p0 >> 16), c0,
                             (int16_t)(p1 & 0xFFFF), (int16_t)(p1 >> 16), c1);
                break;
            }
            case OP_CALL: case OP_JUMP: case OP_RETURN:
            default:
                unsupportedDl++;
                lastUnsupported = w;
                break;
        }
    }
    front = back;
    frames++;
    statRenderNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
}

void EveEmu::execPrimitiveVertex(int x16, int y16)
{
    x16 += gs.vtx;
    y16 += gs.vty;
    double x = x16 / 16.0, y = y16 / 16.0;
    switch (primitive)
    {
        case PRIM_POINTS: drawPoint(x, y); break;
        case PRIM_BITMAPS: drawBitmap((int)floor(x), (int)floor(y), gs.cell); break;
        case PRIM_LINES:
        case PRIM_RECTS:
            vx[vtxCount] = x;
            vy[vtxCount] = y;
            vtxCount++;
            if (vtxCount == 2)
            {
                if (primitive == PRIM_LINES) drawLine(vx[0], vy[0], vx[1], vy[1]);
                else drawRect(vx[0], vy[0], vx[1], vy[1]);
                vtxCount = 0;
            }
            break;
        case PRIM_LINE_STRIP:
            if (stripHas) drawLine(stripX, stripY, x, y);
            stripX = x;
            stripY = y;
            stripHas = true;
            break;
        default: break; // edge strips: not used by the firmware
    }
}

// ------------------------------------------------------------ raster ------
void EveEmu::blendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, double coverage)
{
    if (x < gs.scX || y < gs.scY || x >= gs.scX + gs.scW || y >= gs.scY + gs.scH) return;
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    int sa = coverage >= 1.0 ? a : (int)(a * coverage + 0.5);
    uint32_t& d = back[y * kWidth + x];
    uint8_t& da = backA[y * kWidth + x];
    const bool normal = gs.blendSrc == 2 && gs.blendDst == 4;
    const bool fullMask = gs.maskR && gs.maskG && gs.maskB;
    if (normal)
    {
        if (sa <= 0) return;
        if (sa >= 255 && fullMask)
        {
            d = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            if (gs.maskA) da = 255;
            return;
        }
        int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int inv = 255 - sa;
        int nr = (r * sa + dr * inv + 127) / 255;
        int ng = (g * sa + dg * inv + 127) / 255;
        int nb = (b * sa + db * inv + 127) / 255;
        if (!gs.maskR) nr = dr;
        if (!gs.maskG) ng = dg;
        if (!gs.maskB) nb = db;
        if (gs.maskA) da = (uint8_t)std::min(255, (sa * 255 + da * inv + 127) / 255);
        d = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
        return;
    }
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    auto factor = [&](uint8_t f) -> int { // in 1/255
        switch (f)
        {
            case 0: return 0;
            case 1: return 255;
            case 2: return sa;
            case 3: return da;
            case 4: return 255 - sa;
            case 5: return 255 - da;
            default: return 255;
        }
    };
    int fs = factor(gs.blendSrc), fd = factor(gs.blendDst);
    int nr = clamp8((r * fs + dr * fd + 127) / 255);
    int ng = clamp8((g * fs + dg * fd + 127) / 255);
    int nb = clamp8((b * fs + db * fd + 127) / 255);
    int na = clamp8((sa * fs + da * fd + 127) / 255);
    if (!gs.maskR) nr = dr;
    if (!gs.maskG) ng = dg;
    if (!gs.maskB) nb = db;
    if (gs.maskA) da = (uint8_t)na;
    d = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
}

void EveEmu::clearRegion(bool)
{
    PrimTimer timer_(statPrimNs[0], statPrimCalls[0]);
    int x0 = std::max(0, gs.scX), y0 = std::max(0, gs.scY);
    int x1 = std::min(kWidth, gs.scX + gs.scW), y1 = std::min(kHeight, gs.scY + gs.scH);
    uint32_t c = ((uint32_t)gs.cr << 16) | ((uint32_t)gs.cg << 8) | gs.cb;
    for (int y = y0; y < y1; y++)
    {
        std::fill(&back[y * kWidth + x0], &back[y * kWidth + x1], c);
        std::fill(&backA[y * kWidth + x0], &backA[y * kWidth + x1], gs.ca);
    }
}

void EveEmu::drawPoint(double cx, double cy)
{
    PrimTimer timer_(statPrimNs[3], statPrimCalls[3]);
    double r = gs.pointSize / 16.0;
    int x0 = (int)floor(cx - r - 1), x1 = (int)ceil(cx + r + 1);
    int y0 = (int)floor(cy - r - 1), y1 = (int)ceil(cy + r + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            double dx = x - cx, dy = y - cy;
            double cov = clampd(0.5 - (sqrt(dx * dx + dy * dy) - r), 0, 1);
            if (cov > 0) blendPixel(x, y, gs.r, gs.g, gs.b, gs.a, cov);
        }
}

void EveEmu::drawLine(double x0, double y0, double x1, double y1)
{
    PrimTimer timer_(statPrimNs[2], statPrimCalls[2]);
    double hw = gs.lineWidth / 16.0; // EVE: line width is the half width
    int bx0 = (int)floor(std::min(x0, x1) - hw - 1), bx1 = (int)ceil(std::max(x0, x1) + hw + 1);
    int by0 = (int)floor(std::min(y0, y1) - hw - 1), by1 = (int)ceil(std::max(y0, y1) + hw + 1);
    double ddx = x1 - x0, ddy = y1 - y0, len2 = ddx * ddx + ddy * ddy;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++)
        {
            double px = x, py = y;
            double t = len2 > 0 ? clampd(((px - x0) * ddx + (py - y0) * ddy) / len2, 0, 1) : 0;
            double qx = x0 + t * ddx, qy = y0 + t * ddy;
            double d = sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy)) - hw;
            double cov = clampd(0.5 - d, 0, 1);
            if (cov > 0) blendPixel(x, y, gs.r, gs.g, gs.b, gs.a, cov);
        }
}

void EveEmu::drawRect(double x0, double y0, double x1, double y1)
{
    PrimTimer timer_(statPrimNs[1], statPrimCalls[1]);
    // EVE rectangles extend by the line width beyond the two vertices, with
    // corners rounded by that same radius.
    double r = gs.lineWidth / 16.0;
    double lx = std::min(x0, x1), hx = std::max(x0, x1), ly = std::min(y0, y1), hy = std::max(y0, y1);
    int bx0 = (int)floor(lx - r - 1), bx1 = (int)ceil(hx + r + 1);
    int by0 = (int)floor(ly - r - 1), by1 = (int)ceil(hy + r + 1);
    // pixels (centre at integer coords) at least 0.5 inside the expanded box are fully covered
    int ix0 = (int)ceil(lx - r + 0.5), ix1 = (int)floor(hx + r - 0.5);
    int iy0 = (int)ceil(ly - r + 0.5), iy1 = (int)floor(hy + r - 0.5);
    // keep the corner regions (radius r) out of the fast fill
    int cx0 = (int)ceil(lx), cx1 = (int)floor(hx) - 1, cy0 = (int)ceil(ly), cy1 = (int)floor(hy) - 1;
    const bool solid = gs.a == 255 && gs.blendSrc == 2 && gs.blendDst == 4 && gs.maskR && gs.maskG && gs.maskB;
    const uint32_t solidColor = ((uint32_t)gs.r << 16) | ((uint32_t)gs.g << 8) | gs.b;
    // clip window for the span fill (scissor and screen)
    const int cx = std::max(0, gs.scX), cx1e = std::min(kWidth, gs.scX + gs.scW);
    const int cy = std::max(0, gs.scY), cy1e = std::min(kHeight, gs.scY + gs.scH);
    for (int y = by0; y <= by1; y++)
    {
        bool rowInside = y >= iy0 && y <= iy1;
        bool rowCornerFree = y >= cy0 && y <= cy1; // between the corner arcs vertically
        for (int x = bx0; x <= bx1; x++)
        {
            bool colInside = x >= ix0 && x <= ix1;
            bool colCornerFree = x >= cx0 && x <= cx1;
            if (rowInside && colInside && (rowCornerFree || colCornerFree))
            {
                if (solid && y >= cy && y < cy1e && x >= cx && x < cx1e)
                {
                    // opaque normal blend: fill the rest of the solid span in one go
                    int xe = std::min(ix1, cx1e - 1);
                    if (!rowCornerFree) xe = std::min(xe, cx1);
                    std::fill(&back[y * kWidth + x], &back[y * kWidth + xe + 1], solidColor);
                    if (gs.maskA) std::fill(&backA[y * kWidth + x], &backA[y * kWidth + xe + 1], (uint8_t)255);
                    x = xe;
                    continue;
                }
                blendPixel(x, y, gs.r, gs.g, gs.b, gs.a, 1.0);
                continue;
            }
            double px = x, py = y;
            double dx = std::max(std::max(lx - px, px - hx), 0.0);
            double dy = std::max(std::max(ly - py, py - hy), 0.0);
            double d = sqrt(dx * dx + dy * dy) - r;
            double cov = clampd(0.5 - d, 0, 1);
            if (cov > 0) blendPixel(x, y, gs.r, gs.g, gs.b, gs.a, cov);
        }
    }
}

void EveEmu::drawGradient(int x0, int y0, uint32_t c0, int x1, int y1, uint32_t c1)
{
    PrimTimer timer_(statPrimNs[5], statPrimCalls[5]);
    int sx0 = std::max(0, gs.scX), sy0 = std::max(0, gs.scY);
    int sx1 = std::min(kWidth, gs.scX + gs.scW), sy1 = std::min(kHeight, gs.scY + gs.scH);
    double ddx = x1 - x0, ddy = y1 - y0, len2 = ddx * ddx + ddy * ddy;
    int r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
    int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++)
        {
            double t = len2 > 0 ? clampd(((x - x0) * ddx + (y - y0) * ddy) / len2, 0, 1) : 0;
            int ti = (int)(t * 256.0 + 0.5);
            uint8_t r = clamp8(r0 + (((r1 - r0) * ti) >> 8));
            uint8_t g = clamp8(g0 + (((g1 - g0) * ti) >> 8));
            uint8_t b = clamp8(b0 + (((b1 - b0) * ti) >> 8));
            blendPixel(x, y, r, g, b, gs.a, 1.0);
        }
}

bool EveEmu::fetchTexel(const Handle& h, uint32_t src, int u, int v, Px& out)
{
    uint32_t bpp = bppOf(h.format);
    int texW = (int)(h.stride * 8 / (bpp ? bpp : 8));
    int texH = (int)h.height;
    if (texW <= 0 || texH <= 0) return false;
    if (h.wrapX) u = ((u % texW) + texW) % texW;
    if (h.wrapY) v = ((v % texH) + texH) % texH;
    if (u < 0 || v < 0 || u >= texW || v >= texH) return false;
    uint32_t off = (src + (uint32_t)v * h.stride) & 0x3FFFFF; // 22-bit address wrap
    const uint8_t* row = mem(off, h.stride);
    if (!row) return false;
    switch (h.format)
    {
        case FMT_L1: { int bit = (row[u >> 3] >> (7 - (u & 7))) & 1; out = { 255, 255, 255, (uint8_t)(bit * 255) }; return true; }
        case FMT_L2: { int v2 = (row[u >> 2] >> (6 - 2 * (u & 3))) & 3; out = { 255, 255, 255, (uint8_t)(v2 * 85) }; return true; }
        case FMT_L4: { int n = (u & 1) ? (row[u >> 1] & 15) : (row[u >> 1] >> 4); out = { 255, 255, 255, (uint8_t)(n * 17) }; return true; }
        case FMT_L8: out = { 255, 255, 255, row[u] }; return true;
        case FMT_RGB332: { uint8_t p = row[u]; out = { (uint8_t)((p >> 5) * 255 / 7), (uint8_t)(((p >> 2) & 7) * 255 / 7), (uint8_t)((p & 3) * 85), 255 }; return true; }
        case FMT_ARGB2: { uint8_t p = row[u]; out = { (uint8_t)(((p >> 4) & 3) * 85), (uint8_t)(((p >> 2) & 3) * 85), (uint8_t)((p & 3) * 85), (uint8_t)((p >> 6) * 85) }; return true; }
        case FMT_ARGB4: { uint16_t p = (uint16_t)(row[2 * u] | (row[2 * u + 1] << 8)); out = { (uint8_t)(((p >> 8) & 15) * 17), (uint8_t)(((p >> 4) & 15) * 17), (uint8_t)((p & 15) * 17), (uint8_t)((p >> 12) * 17) }; return true; }
        case FMT_ARGB1555: { uint16_t p = (uint16_t)(row[2 * u] | (row[2 * u + 1] << 8)); out = { (uint8_t)(((p >> 10) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 31) * 255 / 31), (uint8_t)((p & 31) * 255 / 31), (uint8_t)((p >> 15) * 255) }; return true; }
        case FMT_RGB565: { uint16_t p = (uint16_t)(row[2 * u] | (row[2 * u + 1] << 8)); out = { (uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31), 255 }; return true; }
        case FMT_PALETTED565:
        case FMT_PALETTED4444:
        {
            uint8_t idx = row[u];
            const uint8_t* pal = mem(gs.paletteSource + idx * 2, 2);
            if (!pal) return false;
            uint16_t p = (uint16_t)(pal[0] | (pal[1] << 8));
            if (h.format == FMT_PALETTED565)
                out = { (uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31), 255 };
            else
                out = { (uint8_t)(((p >> 8) & 15) * 17), (uint8_t)(((p >> 4) & 15) * 17), (uint8_t)((p & 15) * 17), (uint8_t)((p >> 12) * 17) };
            return true;
        }
        case FMT_PALETTED8:
        {
            uint8_t idx = row[u];
            const uint8_t* pal = mem(gs.paletteSource + idx * 4, 4);
            if (!pal) return false;
            out = { pal[2], pal[1], pal[0], pal[3] };
            return true;
        }
        default: return false;
    }
}

void EveEmu::drawBitmap(int x, int y, uint8_t cell)
{
    PrimTimer timer_(statPrimNs[4], statPrimCalls[4]);
    const Handle& h = handles[gs.handle];
    int w = (int)h.width, hh = (int)h.sizeH;
    if (w == 0) w = 1024;   // EVE: 0 means maximum
    if (hh == 0) hh = 1024;
    uint32_t src = (h.source + (uint32_t)cell * h.stride * h.height) & 0x3FFFFF;
    bool identity = gs.ta == 1 && gs.tb == 0 && gs.tc == 0 && gs.td == 0 && gs.te == 1 && gs.tf == 0;
    int x0 = std::max(x, std::max(0, gs.scX)), y0 = std::max(y, std::max(0, gs.scY));
    int x1 = std::min(x + w, std::min(kWidth, gs.scX + gs.scW));
    int y1 = std::min(y + hh, std::min(kHeight, gs.scY + gs.scH));
    if (x1 <= x0 || y1 <= y0) return;

    const uint32_t bpp = bppOf(h.format);
    const int texW = (int)(h.stride * 8 / (bpp ? bpp : 8));
    const int texH = (int)h.height;
    if (texW <= 0 || texH <= 0) return;

    if (identity && !h.wrapX && !h.wrapY)
    {
        // Fast path: 1:1 texels, BORDER wrap, per-row pointers, inline blend.
        const bool normal = gs.blendSrc == 2 && gs.blendDst == 4 && gs.maskR && gs.maskG && gs.maskB;
        const int cr = gs.r, cg = gs.g, cb = gs.b, ca = gs.a;
        int vEnd = std::min(y1 - y, texH);
        int uEnd = std::min(x1 - x, texW);
        for (int v = y0 - y; v < vEnd; v++)
        {
            uint32_t off = (src + (uint32_t)v * h.stride) & 0x3FFFFF;
            const uint8_t* row = mem(off, h.stride);
            if (!row) continue;
            int py = y + v;
            uint32_t* drow = &back[py * kWidth];
            uint8_t* arow = &backA[py * kWidth];
            for (int u = x0 - x; u < uEnd; u++)
            {
                int tr, tg, tb, ta;
                switch (h.format)
                {
                    case FMT_L4: { int n = (u & 1) ? (row[u >> 1] & 15) : (row[u >> 1] >> 4); if (!n) continue; tr = tg = tb = 255; ta = n * 17; break; }
                    case FMT_L8: { ta = row[u]; if (!ta) continue; tr = tg = tb = 255; break; }
                    case FMT_L2: { int n = (row[u >> 2] >> (6 - 2 * (u & 3))) & 3; if (!n) continue; tr = tg = tb = 255; ta = n * 85; break; }
                    case FMT_L1: { int bit = (row[u >> 3] >> (7 - (u & 7))) & 1; if (!bit) continue; tr = tg = tb = 255; ta = 255; break; }
                    case FMT_RGB565: { uint16_t p = (uint16_t)(row[2 * u] | (row[2 * u + 1] << 8)); tr = (p >> 11) * 255 / 31; tg = ((p >> 5) & 63) * 255 / 63; tb = (p & 31) * 255 / 31; ta = 255; break; }
                    case FMT_ARGB4: { uint16_t p = (uint16_t)(row[2 * u] | (row[2 * u + 1] << 8)); ta = (p >> 12) * 17; if (!ta) continue; tr = ((p >> 8) & 15) * 17; tg = ((p >> 4) & 15) * 17; tb = (p & 15) * 17; break; }
                    default:
                    {
                        Px t;
                        if (!fetchTexel(h, src, u, v, t) || t.a == 0) continue;
                        tr = t.r; tg = t.g; tb = t.b; ta = t.a;
                        break;
                    }
                }
                int r = tr * cr / 255, g = tg * cg / 255, b = tb * cb / 255, a = ta * ca / 255;
                int px = x + u;
                if (normal)
                {
                    if (a <= 0) continue;
                    uint32_t& d = drow[px];
                    if (a >= 255)
                    {
                        d = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                        if (gs.maskA) arow[px] = 255;
                        continue;
                    }
                    int inv = 255 - a;
                    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
                    d = ((uint32_t)((r * a + dr * inv + 127) / 255) << 16) |
                        ((uint32_t)((g * a + dg * inv + 127) / 255) << 8) |
                        (uint32_t)((b * a + db * inv + 127) / 255);
                    if (gs.maskA) arow[px] = (uint8_t)std::min(255, (a * 255 + arow[px] * inv + 127) / 255);
                }
                else
                {
                    blendPixel(px, py, (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a, 1.0);
                }
            }
        }
        return;
    }

    // General path: transforms, wrapping, bilinear filtering.
    for (int py = y0; py < y1; py++)
    {
        for (int px = x0; px < x1; px++)
        {
            double lx = px - x, ly = py - y; // offset inside the bitmap region
            double u, v;
            if (identity) { u = lx; v = ly; }
            else
            {
                // Transformed bitmaps: the chip picks texel floor(u - 0.5)
                // (calibrated against device screenshots of rotated labels).
                u = gs.ta * lx + gs.tb * ly + gs.tc - 0.5;
                v = gs.td * lx + gs.te * ly + gs.tf - 0.5;
            }
            Px t;
            if (h.filter == 0 || identity)
            {
                if (!fetchTexel(h, src, (int)floor(u), (int)floor(v), t))
                    continue;
            }
            else
            {
                int iu = (int)floor(u), iv = (int)floor(v);
                double fu = u - iu, fv = v - iv;
                Px p00{}, p10{}, p01{}, p11{};
                bool b00 = fetchTexel(h, src, iu, iv, p00), b10 = fetchTexel(h, src, iu + 1, iv, p10);
                bool b01 = fetchTexel(h, src, iu, iv + 1, p01), b11 = fetchTexel(h, src, iu + 1, iv + 1, p11);
                if (!b00) p00 = { 0, 0, 0, 0 };
                if (!b10) p10 = { 0, 0, 0, 0 };
                if (!b01) p01 = { 0, 0, 0, 0 };
                if (!b11) p11 = { 0, 0, 0, 0 };
                auto mix = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
                    return clamp8((int)lrint((a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv));
                };
                t = { mix(p00.r, p10.r, p01.r, p11.r), mix(p00.g, p10.g, p01.g, p11.g),
                      mix(p00.b, p10.b, p01.b, p11.b), mix(p00.a, p10.a, p01.a, p11.a) };
                if (t.a == 0) continue;
            }
            uint8_t r = (uint8_t)(t.r * gs.r / 255), g = (uint8_t)(t.g * gs.g / 255),
                    b = (uint8_t)(t.b * gs.b / 255), a = (uint8_t)(t.a * gs.a / 255);
            blendPixel(px, py, r, g, b, a, 1.0);
        }
    }
}

bool EveEmu::savePPM(const std::string& path) const
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", kWidth, kHeight);
    std::vector<uint8_t> row(kWidth * 3);
    for (int y = 0; y < kHeight; y++)
    {
        for (int x = 0; x < kWidth; x++)
        {
            uint32_t p = front[y * kWidth + x];
            row[3 * x] = (uint8_t)(p >> 16);
            row[3 * x + 1] = (uint8_t)(p >> 8);
            row[3 * x + 2] = (uint8_t)p;
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    return true;
}
