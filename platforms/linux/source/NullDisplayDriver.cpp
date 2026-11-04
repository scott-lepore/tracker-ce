#include "NullDisplayDriver.h"
#include <cstring>

// Mirrors API_SendString(): writes the string plus terminator, padded to a
// 32-bit boundary, and returns the number of bytes consumed.
uint8_t NullDisplayDriver::sendString(const char* string, uint32_t length)
{
    uint32_t len = length ? length : (uint32_t)strlen(string);
    uint32_t padded = (len + 1 + 3) & ~3u;
    words += padded / 4;
    return (uint8_t)padded;
}
