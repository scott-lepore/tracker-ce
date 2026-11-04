#ifndef EVE_IMAGE_H_
#define EVE_IMAGE_H_
#include <cstddef>
#include <cstdint>
#include <vector>
// Decode a JPEG or PNG in memory to RGB565 (little endian), as CMD_LOADIMAGE does by default.
bool eveDecodeImage(const uint8_t* data, size_t len, std::vector<uint8_t>& rgb565, int& w, int& h);
#endif
