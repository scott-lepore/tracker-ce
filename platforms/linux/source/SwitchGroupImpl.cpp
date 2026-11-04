#include "SwitchGroupImpl.h"
#include "PlatformGlue.h"

const uint16_t SwitchGroupImpl::kHoldStepDiff[kHoldSteps] = { 0, 100, 190, 235, 255, 270 };

bool SwitchGroupImpl::isButtonPressed(int buttonIdx)
{
    if (buttonIdx < 0 || buttonIdx >= kMaxButtons)
        return false;
    return pushed[buttonIdx] != 0;
}

void SwitchGroupImpl::setActionCallback(ActionType action, void (*callback)(uint8_t))
{
    switch (action)
    {
        case kPress: onPush = callback; break;
        case kRelease: onRelease = callback; break;
        case kHold: onHold = callback; break;
    }
}

void SwitchGroupImpl::simulateKey(uint8_t button, uint8_t action)
{
    if (button >= kMaxButtons)
        return;
    switch (action)
    {
        case 1:
            holdTimeMs[button] = linuxNowMs();
            holdFactor[button] = 0;
            pushed[button] = 1;
            if (onPush)
                onPush(button);
            break;
        case 2:
            pushed[button] = 1;
            holdFactor[button] = 1;
            if (onHold)
                onHold(button);
            break;
        default:
            pushed[button] = 0;
            if (onRelease)
                onRelease(button);
            break;
    }
}

void SwitchGroupImpl::update()
{
    uint32_t now = linuxNowMs();
    for (uint8_t i = 0; i < kMaxButtons; i++)
    {
        if (pushed[i] && ((now - holdTimeMs[i]) > (uint32_t)(holdTime - kHoldStepDiff[holdFactor[i]])))
        {
            if (holdFactor[i] < kHoldSteps - 1)
                holdFactor[i]++;
            holdTimeMs[i] = now;
            if (onHold)
                onHold(i);
        }
    }
}
