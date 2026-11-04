#ifndef CONFIG_MANAGER_IMPL_H_
#define CONFIG_MANAGER_IMPL_H_
#include "ConfigManager.h"
#include "LinuxConfig.h"
#include <stdint.h>
#include <string>

// 4 KiB "EEPROM" persisted to a file; written through on every writeData().
class ConfigManagerImpl final : public ConfigManager
{
  public:
    void init(const std::string& filePath);
    bool readData(int address, void* data, int size) override;
    void writeData(int address, void* data, int size) override;
    bool readRaw(int address, void* data, int size);
    void writeRaw(int address, const void* data, int size);

  private:
    void flush();
    std::string path;
    uint8_t image[kEepromSize] = { 0 };
};

#endif
