#ifndef EVE_EMU_H_
#define EVE_EMU_H_
#include <cstdint>
#include <string>
#include <vector>

// Software model of the FT812 (EVE2) graphics controller as the Tracker uses
// it: 1 MiB RAM_G, 8 KiB display list, the co-processor command FIFO, and a
// rasterizer for the display-list primitives the firmware emits (RECTS,
// LINES, LINE_STRIP, POINTS, BITMAPS with transforms, scissor, blend, text
// via custom L4 fonts). Co-processor commands are expanded into display-list
// words exactly where the real chip would put them, so the firmware's
// "render control to RAM_DL, copy to RAM_G, CMD_APPEND later" scheme works.
class EveEmu
{
  public:
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 480;
    static constexpr uint32_t kRamGSize = 1024 * 1024;
    static constexpr uint32_t kRamDlBase = 0x300000;
    static constexpr uint32_t kRamDlSize = 8192;
    static constexpr uint32_t kRamCmdBase = 0x308000;

    EveEmu();

    // Memory-mapped access (RAM_G and RAM_DL ranges)
    void memWrite(uint32_t addr, const void* data, uint32_t len);
    void memRead(uint32_t addr, void* out, uint32_t len);

    // Co-processor FIFO
    void fifoWrite32(uint32_t v);
    void fifoWriteBytes(const uint8_t* data, size_t len);
    void fifoWriteString(const char* s, uint32_t len); // NUL-terminated, padded to 4
    void execute(); // run everything queued so far
    uint32_t dlOffset() const { return cmdDl; } // REG_CMD_DL
    void getProps(uint32_t& addr, uint32_t& w, uint32_t& h) const;

    // Output
    const uint32_t* frontBuffer() const { return front.data(); } // 0x00RRGGBB
    uint32_t frameCounter() const { return frames; }
    bool savePPM(const std::string& path) const;

    // Diagnostics
    uint32_t unsupportedDl = 0;
    uint32_t unsupportedCmd = 0;
    uint32_t lastUnsupported = 0;
    uint64_t statCommands = 0;   // co-processor commands executed
    uint64_t statDlWords = 0;    // display-list words written
    uint64_t statExecCalls = 0;  // execute() invocations
    uint64_t statRenderNs = 0;   // time spent in render()
    // per-primitive profile: calls and nanoseconds (0 clear,1 rect,2 line,3 point,4 bitmap,5 gradient)
    uint64_t statPrimCalls[6] = { 0 };
    uint64_t statPrimNs[6] = { 0 };

  private:
    struct Handle
    {
        uint32_t source = 0;
        uint8_t format = 0;
        uint32_t stride = 0;
        uint32_t height = 0;
        uint8_t filter = 0, wrapX = 0, wrapY = 0;
        uint32_t width = 0, sizeH = 0;
    };
    struct GState
    {
        uint8_t r = 255, g = 255, b = 255, a = 255;
        uint8_t cr = 0, cg = 0, cb = 0, ca = 0;
        uint8_t blendSrc = 2, blendDst = 4; // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
        int lineWidth = 16, pointSize = 16;  // 1/16 px
        int scX = 0, scY = 0, scW = 2048, scH = 2048;
        uint8_t handle = 0, cell = 0, vertexFormat = 4;
        int vtx = 0, vty = 0; // 1/16 px
        double ta = 1, tb = 0, tc = 0, td = 0, te = 1, tf = 0; // screen -> bitmap
        uint8_t maskR = 1, maskG = 1, maskB = 1, maskA = 1;
        uint32_t paletteSource = 0;
    };
    struct Px
    {
        uint8_t r, g, b, a;
    };

    uint8_t* mem(uint32_t addr, uint32_t len);
    // co-processor
    bool runCommand(size_t& pos);
    void dlPush(uint32_t word);
    void cmdText(int16_t x, int16_t y, uint16_t font, uint16_t opt, const char* s);
    void cmdNumber(int16_t x, int16_t y, uint16_t font, uint16_t opt, int32_t n);
    void cmdSetFont2(uint32_t handle, uint32_t ptr, uint32_t firstchar);
    void cmdSetBitmap(uint32_t source, uint16_t fmt, uint16_t w, uint16_t h);
    void cmdSetMatrix();
    void cmdSnapshot2(uint32_t fmt, uint32_t ptr, int x, int y, int w, int h);
    // display list execution
    void render();
    void execPrimitiveVertex(int x16, int y16);
    void flushPrimitive();
    void drawPoint(double cx, double cy);
    void drawLine(double x0, double y0, double x1, double y1);
    void drawRect(double x0, double y0, double x1, double y1);
    void drawBitmap(int x, int y, uint8_t cell);
    void drawGradient(int x0, int y0, uint32_t c0, int x1, int y1, uint32_t c1);
    void clearRegion(bool color);
    void blendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, double coverage);
    bool fetchTexel(const Handle& h, uint32_t src, int u, int v, Px& out);
    static uint32_t bppOf(uint8_t format);

    std::vector<uint8_t> ramG;
    std::vector<uint8_t> ramDL;
    std::vector<uint8_t> fifo; // pending command bytes
    size_t fifoPos = 0;
    uint32_t cmdDl = 0;
    bool imageDataMode = false;
    std::vector<uint8_t> imageData; // bytes following CMD_LOADIMAGE

    // co-processor state
    uint32_t fontPtr[32] = { 0 };
    uint32_t fontFirst[32] = { 0 };
    uint32_t numberBase = 10;
    double mA = 1, mB = 0, mC = 0, mD = 0, mE = 1, mF = 0; // current matrix (bitmap -> screen)
    uint32_t propsAddr = 0, propsW = 0, propsH = 0;

    // graphics engine state (persistent across lists)
    Handle handles[32];
    GState gs;
    GState stack[4];
    int stackDepth = 0;
    uint8_t primitive = 0;
    int vtxCount = 0;
    double vx[2], vy[2];
    double stripX = 0, stripY = 0;
    bool stripHas = false;

    std::vector<uint32_t> back;   // 0x00RRGGBB
    std::vector<uint8_t> backA;   // destination alpha
    std::vector<uint32_t> front;
    uint32_t frames = 0;
};

#endif
