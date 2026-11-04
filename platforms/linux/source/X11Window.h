#ifndef X11_WINDOW_H_
#define X11_WINDOW_H_
#include <cstdint>
#include <functional>

// Shows the emulated 800x480 panel in an X11 window (Xwayland is fine) and
// turns keyboard/mouse-wheel input into Tracker buttons, pads and jog steps.
//
// Keyboard map (see docs/linux_port.md):
//   pads      1..=  q..]  a..\  z../   (4 rows x 12, top row first)
//   F1..F8    screen buttons        Space play   Enter rec    Shift shift
//   arrows    up/down/left/right    Insert insert  Delete delete  Home copy
//   Ctrl+p params  e perform  f file  g config  m master  l sample loader
//   Ctrl+y sample play  d sample edit  r sample rec  s song  n note  i instr
//   Ctrl+v vol/fx2  x fx/fx1  t pattern
//   mouse wheel / PageUp / PageDown    jog
class X11Window
{
  public:
    typedef std::function<void(uint8_t idx, bool press)> KeyFn;
    typedef std::function<void(int delta)> JogFn;

    bool open(int scale);
    void close();
    bool isOpen() const { return display != nullptr; }
    void present(const uint32_t* rgb, uint32_t frameId); // 0x00RRGGBB, 800x480
    // Returns false when the user closed the window.
    bool poll(const KeyFn& button, const KeyFn& pad, const JogFn& jog);

  private:
    void* display = nullptr; // Display*
    unsigned long window = 0;
    void* gc = nullptr;      // GC
    void* image = nullptr;   // XImage*
    unsigned long wmDelete = 0;
    int scale = 1;
    uint32_t lastFrame = 0xFFFFFFFF;
    bool ctrl = false;
    bool pressed[256] = { false };
};

#endif
