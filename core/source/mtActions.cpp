#include "core/interface.h"
#include "DevMonitor.h"
#include "mtSleep.h"
#include <stdint.h>

//-----------------------------------------------------------------
//-------------------------|   POTS   |----------------------------
//-----------------------------------------------------------------
void onPotChange(uint8_t n, int16_t value)
{
    if (devMonitorInputEvent('j', n, value))
        return;
    mtInterface.potChange(n, value);
}

//-----------------------------------------------------------------
//------------------------|   BUTTONS   |--------------------------
//-----------------------------------------------------------------
// 0 - release
// 1 - press
// 2 - hold
// 3 - double click
void onButtonPush(uint8_t n)
{
    if (devMonitorInputEvent('b', n, 1))
        return;
    mtInterface.buttonChange(n, buttonPress);
}

void onButtonRelease(uint8_t n)
{
    if (devMonitorInputEvent('b', n, 0))
        return;
    mtInterface.buttonChange(n, buttonRelease);
}

void onButtonHold(uint8_t n)
{
    if (devMonitorControllerMode())
        return;
    mtInterface.buttonChange(n, buttonHold);
}

//-----------------------------------------------------------------
//----------------------|   POWER BUTTON   |-----------------------
//-----------------------------------------------------------------
void onPowerButtonChange(int value)
{
    if (mtInterface.ip->isPowerButtonBlocked())
    {
        return;
    }

    if (!mtInterface.powerButtonChange(value)) // jesli nie ma ustawionej akcji
    {
        mtInterface.handlePowerButtonAction(value);
    }
}

//-----------------------------------------------------------------
//------------------------|   PADS   |-----------------------------
//-----------------------------------------------------------------
void onPadPush(uint8_t n)
{
    if (devMonitorInputEvent('p', n, 1))
        return;
    mtInterface.padPressed(n, 0, 0, 0);
}

void onPadRelease(uint8_t n)
{
    if (devMonitorInputEvent('p', n, 0))
        return;
    mtInterface.padReleased(n);
}

void onPadHold(uint8_t n)
{
    if (devMonitorControllerMode())
        return;
    mtInterface.padHold(n);
}

void onSDCardSlotChange(bool state)
{
    mtInterface.handleCardSlotAction(state);
}

void onBatteryCharging()
{
    // mtInterface.showChargingBatteryPopup();
}

void onBatteryEmpty()
{
    mtInterface.showEmptyBatteryPopup();
    lowPower.goLowPower();
}

void onBatteryLow()
{
    mtInterface.showLowBatteryPopup();
}
