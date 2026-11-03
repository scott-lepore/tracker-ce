#include "DevSerialImpl.h"

#include "ButtonsBaseImpl.h"
#include "PlatformImpl.h"
#include "usb_serial.h"

int DevSerialImpl::available()
{
    return Serial.available();
}

int DevSerialImpl::read(uint8_t* buf, size_t len)
{
    return usb_serial_read(buf, len);
}

size_t DevSerialImpl::write(const uint8_t* buf, size_t len)
{
    return Serial.write(buf, len);
}

bool DevSerialImpl::connected()
{
    return Serial.dtr() != 0;
}

static DevSerialImpl devSerial;
static PlatformImpl* devPlatform = nullptr;

static void devButtonHook(uint8_t idx, uint8_t action)
{
    static_cast<ButtonsBaseImpl*>(devPlatform->getButtons())->simulateKey(idx, action);
}

static void devPadHook(uint8_t idx, uint8_t action)
{
    static_cast<ButtonsBaseImpl*>(devPlatform->getPads())->simulateKey(idx, action);
}

void devSerialStart(PlatformImpl* platform)
{
    devPlatform = platform;
    DevMonitorHooks hooks;
    hooks.button = devButtonHook;
    hooks.pad = devPadHook;
    devMonitorInit(&devSerial, platform, hooks);
}

void devSerialUpdate()
{
    devMonitorUpdate();
}
