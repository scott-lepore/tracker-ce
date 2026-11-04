#ifndef SWITCH_GROUP_IMPL_H_
#define SWITCH_GROUP_IMPL_H_
#include "Platform.h"

// Software stand-in for the TCA8418 key matrix driver (Keys.cpp): same hold
// timing (progressive repeat) and the same press/release/hold callbacks.
class SwitchGroupImpl final : public SwitchGroup
{
  public:
    static constexpr int kMaxButtons = 48;

    void setHoldTime(int timeMs) override { holdTime = (uint16_t)timeMs; }
    bool isButtonPressed(int buttonIdx) override;
    void setActionCallback(ActionType action, void (*callback)(uint8_t)) override;

    // action: 0 = release, 1 = press, 2 = hold (same as DevMonitor hooks)
    void simulateKey(uint8_t button, uint8_t action);
    void update(); // drives hold repeats; call from the main loop

  private:
    static constexpr uint8_t kHoldSteps = 6;
    static const uint16_t kHoldStepDiff[kHoldSteps];
    void (*onPush)(uint8_t) = nullptr;
    void (*onRelease)(uint8_t) = nullptr;
    void (*onHold)(uint8_t) = nullptr;
    uint8_t pushed[kMaxButtons] = { 0 };
    uint32_t holdTimeMs[kMaxButtons] = { 0 };
    uint8_t holdFactor[kMaxButtons] = { 0 };
    uint16_t holdTime = 333;
};

#endif
