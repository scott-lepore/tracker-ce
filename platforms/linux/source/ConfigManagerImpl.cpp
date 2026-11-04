#include "ConfigManagerImpl.h"
#include <cstdio>
#include <cstring>

void ConfigManagerImpl::init(const std::string& filePath)
{
    path = filePath;
    memset(image, 0xFF, sizeof(image));
    FILE* f = fopen(path.c_str(), "rb");
    if (f)
    {
        size_t n = fread(image, 1, sizeof(image), f);
        (void)n;
        fclose(f);
    }
}

bool ConfigManagerImpl::readRaw(int address, void* data, int size)
{
    if (address < 0 || size < 0 || address + size > kEepromSize)
        return false;
    memcpy(data, image + address, size);
    return true;
}

void ConfigManagerImpl::writeRaw(int address, const void* data, int size)
{
    if (address < 0 || size < 0 || address + size > kEepromSize)
        return;
    memcpy(image + address, data, size);
    flush();
}

bool ConfigManagerImpl::readData(int address, void* data, int size)
{
    return readRaw(address + CONFIG_EEPROM_ADRESS, data, size);
}

void ConfigManagerImpl::writeData(int address, void* data, int size)
{
    writeRaw(address + CONFIG_EEPROM_ADRESS, data, size);
}

void ConfigManagerImpl::flush()
{
    if (path.empty())
        return;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return;
    fwrite(image, 1, sizeof(image), f);
    fclose(f);
}
