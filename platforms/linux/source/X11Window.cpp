#include "X11Window.h"
#include "eve/EveEmu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef HAVE_X11
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace
{
constexpr int W = EveEmu::kWidth, H = EveEmu::kHeight;

// Tracker button indices (platforms/mk66 ButtonsImpl order, see trackerdev.py)
enum : uint8_t
{
    B_PARAMS = 8, B_PERFORM = 9, B_FILE = 10, B_CONFIG = 11, B_MASTER = 12, B_SLOAD = 13,
    B_SPLAY = 14, B_SEDIT = 15, B_SREC = 16, B_SONG = 17, B_NOTE = 18, B_INSTR = 19, B_VOL = 20,
    B_FX = 21, B_PATTERN = 22, B_PLAY = 23, B_COPY = 24, B_INSERT = 25, B_UP = 26, B_DELETE = 27,
    B_REC = 28, B_SHIFT = 29, B_LEFT = 30, B_DOWN = 31, B_RIGHT = 32,
};

int padFor(KeySym k)
{
    static const KeySym rows[4][12] = {
        { XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9, XK_0, XK_minus, XK_equal },
        { XK_q, XK_w, XK_e, XK_r, XK_t, XK_y, XK_u, XK_i, XK_o, XK_p, XK_bracketleft, XK_bracketright },
        { XK_a, XK_s, XK_d, XK_f, XK_g, XK_h, XK_j, XK_k, XK_l, XK_semicolon, XK_apostrophe, XK_backslash },
        { XK_z, XK_x, XK_c, XK_v, XK_b, XK_n, XK_m, XK_comma, XK_period, XK_slash, XK_less, NoSymbol },
    };
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 12; c++)
            if (rows[r][c] != NoSymbol && rows[r][c] == k)
                return r * 12 + c;
    return -1;
}

int ctrlButtonFor(KeySym k)
{
    switch (k)
    {
        case XK_p: return B_PARAMS;
        case XK_e: return B_PERFORM;
        case XK_f: return B_FILE;
        case XK_g: return B_CONFIG;
        case XK_m: return B_MASTER;
        case XK_l: return B_SLOAD;
        case XK_y: return B_SPLAY;
        case XK_d: return B_SEDIT;
        case XK_r: return B_SREC;
        case XK_s: return B_SONG;
        case XK_n: return B_NOTE;
        case XK_i: return B_INSTR;
        case XK_v: return B_VOL;
        case XK_x: return B_FX;
        case XK_t: return B_PATTERN;
        default: return -1;
    }
}

int buttonFor(KeySym k)
{
    switch (k)
    {
        case XK_F1: return 0; case XK_F2: return 1; case XK_F3: return 2; case XK_F4: return 3;
        case XK_F5: return 4; case XK_F6: return 5; case XK_F7: return 6; case XK_F8: return 7;
        case XK_space: return B_PLAY;
        case XK_Return: case XK_KP_Enter: return B_REC;
        case XK_Shift_L: case XK_Shift_R: return B_SHIFT;
        case XK_Up: return B_UP; case XK_Down: return B_DOWN;
        case XK_Left: return B_LEFT; case XK_Right: return B_RIGHT;
        case XK_Insert: return B_INSERT;
        case XK_Delete: case XK_BackSpace: return B_DELETE;
        case XK_Home: return B_COPY;
        default: return -1;
    }
}
} // namespace

bool X11Window::open(int s)
{
    scale = s < 1 ? 1 : (s > 4 ? 4 : s);
    Display* d = XOpenDisplay(nullptr);
    if (!d)
    {
        fprintf(stderr, "x11: cannot open display\n");
        return false;
    }
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    Window w = XCreateSimpleWindow(d, root, 0, 0, W * scale, H * scale, 0, 0, BlackPixel(d, screen));
    XStoreName(d, w, "Tracker CE (Linux)");
    XSizeHints hints;
    hints.flags = PMinSize | PAspect;
    hints.min_width = W / 2;
    hints.min_height = H / 2;
    hints.min_aspect.x = hints.max_aspect.x = W;
    hints.min_aspect.y = hints.max_aspect.y = H;
    XSetWMNormalHints(d, w, &hints);
    XSelectInput(d, w, KeyPressMask | KeyReleaseMask | ButtonPressMask | ExposureMask | StructureNotifyMask);
    Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &del, 1);
    Bool supported;
    XkbSetDetectableAutoRepeat(d, True, &supported);
    XMapWindow(d, w);

    display = d;
    window = w;
    gc = XCreateGC(d, w, 0, nullptr);
    wmDelete = del;
    winW = W * scale;
    winH = H * scale;
    ensureImage(winW, winH);
    XFlush(d);
    return true;
}

void X11Window::ensureImage(int w, int h)
{
    if (image && imgW == w && imgH == h) return;
    Display* d = (Display*)display;
    if (image) XDestroyImage((XImage*)image);
    int screen = DefaultScreen(d);
    char* data = (char*)malloc((size_t)w * h * 4);
    image = XCreateImage(d, DefaultVisual(d, screen), DefaultDepth(d, screen), ZPixmap, 0, data, w, h, 32, w * 4);
    imgW = w;
    imgH = h;
    lastFrame = 0xFFFFFFFF;
}

void X11Window::close()
{
    if (!display) return;
    Display* d = (Display*)display;
    if (image) XDestroyImage((XImage*)image); // frees data too
    if (gc) XFreeGC(d, (GC)gc);
    XDestroyWindow(d, window);
    XCloseDisplay(d);
    display = nullptr;
    image = nullptr;
    gc = nullptr;
}

void X11Window::present(const uint32_t* rgb, uint32_t frameId)
{
    if (!display || frameId == lastFrame) return;
    lastFrame = frameId;
    // aspect-fit the 800x480 frame into the window, nearest-neighbour scaling
    int fitW = winW, fitH = winW * H / W;
    if (fitH > winH) { fitH = winH; fitW = winH * W / H; }
    if (fitW < 1 || fitH < 1) return;
    ensureImage(fitW, fitH);
    XImage* img = (XImage*)image;
    uint32_t* dst = (uint32_t*)img->data;
    if (fitW == W && fitH == H)
    {
        memcpy(dst, rgb, (size_t)W * H * 4);
    }
    else
    {
        for (int y = 0; y < fitH; y++)
        {
            const uint32_t* src = rgb + (int)((int64_t)y * H / fitH) * W;
            uint32_t* row = dst + (size_t)y * fitW;
            for (int x = 0; x < fitW; x++)
                row[x] = src[(int)((int64_t)x * W / fitW)];
        }
    }
    int ox = (winW - fitW) / 2, oy = (winH - fitH) / 2;
    XPutImage((Display*)display, window, (GC)gc, img, 0, 0, ox, oy, fitW, fitH);
    XFlush((Display*)display);
}

bool X11Window::poll(const KeyFn& button, const KeyFn& pad, const JogFn& jog)
{
    if (!display) return true;
    Display* d = (Display*)display;
    while (XPending(d))
    {
        XEvent ev;
        XNextEvent(d, &ev);
        switch (ev.type)
        {
            case Expose:
                lastFrame = 0xFFFFFFFF; // force redraw on next present
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != winW || ev.xconfigure.height != winH)
                {
                    winW = ev.xconfigure.width;
                    winH = ev.xconfigure.height;
                    XClearWindow(d, window);
                    lastFrame = 0xFFFFFFFF;
                }
                break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wmDelete) return false;
                break;
            case ButtonPress:
                if (ev.xbutton.button == Button4) jog(1);
                else if (ev.xbutton.button == Button5) jog(-1);
                break;
            case KeyPress:
            case KeyRelease:
            {
                bool press = ev.type == KeyPress;
                KeySym k = XkbKeycodeToKeysym(d, ev.xkey.keycode, 0, 0);
                uint8_t code = (uint8_t)ev.xkey.keycode;
                if (press && pressed[code]) break; // auto-repeat
                pressed[code] = press;
                if (k == XK_Control_L || k == XK_Control_R)
                {
                    ctrl = press;
                    break;
                }
                if (k == XK_Prior) { if (press) jog(1); break; }
                if (k == XK_Next) { if (press) jog(-1); break; }
                int b = buttonFor(k);
                if (b >= 0) { button((uint8_t)b, press); break; }
                if (ctrl)
                {
                    int cb = ctrlButtonFor(k);
                    if (cb >= 0) { button((uint8_t)cb, press); break; }
                }
                int p = padFor(k);
                if (p >= 0) pad((uint8_t)p, press);
                break;
            }
            default: break;
        }
    }
    return true;
}

#else // !HAVE_X11

bool X11Window::open(int)
{
    fprintf(stderr, "x11: built without X11 support\n");
    return false;
}
void X11Window::close() {}
void X11Window::present(const uint32_t*, uint32_t) {}
bool X11Window::poll(const KeyFn&, const KeyFn&, const JogFn&) { return true; }

#endif
