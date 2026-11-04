#include "EveDisplayDriver.h"
#include <cstring>

// Same contract as API_SendString(): string + NUL, padded to 4 bytes;
// returns the number of bytes that went into the FIFO.
uint8_t EveDisplayDriver::sendString(const char* string, uint32_t length)
{
    uint32_t rl = (uint32_t)strlen(string);
    if (length == 0 || length > rl)
        length = rl;
    emu.fifoWriteString(string, length);
    return (uint8_t)((length + 1 + 3) & ~3u);
}
