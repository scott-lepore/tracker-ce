#include "PlatformImpl.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BUILD_ID
#define BUILD_ID "dev"
#endif
#ifdef DEBUG
#define DEBUG_TEXT " debug"
#else
#define DEBUG_TEXT
#endif

PlatformImpl::SystemInfo PlatformImpl::systemInfo = { BUILD_ID DEBUG_TEXT " linux" };

// Core-side input handlers (core/source/mtActions.cpp)
void onButtonPush(uint8_t n);
void onButtonRelease(uint8_t n);
void onButtonHold(uint8_t n);
void onPadPush(uint8_t n);
void onPadRelease(uint8_t n);
void onPadHold(uint8_t n);

void PlatformImpl::init(const Options& options)
{
    mkdir(options.stateDir.c_str(), 0775);
    configManager.init(options.stateDir + "/eeprom.bin");
    fileSystem.init(options.sdRoot);
    timer.start();

    buttons.setActionCallback(SwitchGroup::kPress, onButtonPush);
    buttons.setActionCallback(SwitchGroup::kRelease, onButtonRelease);
    buttons.setActionCallback(SwitchGroup::kHold, onButtonHold);
    pads.setActionCallback(SwitchGroup::kPress, onPadPush);
    pads.setActionCallback(SwitchGroup::kRelease, onPadRelease);
    pads.setActionCallback(SwitchGroup::kHold, onPadHold);
}

void PlatformImpl::update()
{
    buttons.update();
    pads.update();
}

void PlatformImpl::shutdown()
{
    timer.stop();
}

void PlatformImpl::setPowerMode(bool mode)
{
    fprintf(stderr, "setPowerMode(%d): %s\n", mode, mode ? "wake" : "power off, exiting");
    if (!mode)
    {
        fflush(stdout);
        _exit(0);
    }
}

void PlatformImpl::resetMCU()
{
    fprintf(stderr, "resetMCU(): exiting with status 2 (restart requested)\n");
    fflush(stdout);
    _exit(2);
}

void PlatformImpl::runBootloader(char* fileName)
{
    fprintf(stderr, "runBootloader(%s): not applicable on the host build\n", fileName);
}
