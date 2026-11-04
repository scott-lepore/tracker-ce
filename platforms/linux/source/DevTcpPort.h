#ifndef DEV_TCP_PORT_H_
#define DEV_TCP_PORT_H_
#include "DevMonitor.h"
#include <vector>

// The dev monitor's "USB serial" on the host: a single-client TCP server on
// 127.0.0.1:<port>. pyserial opens it as socket://127.0.0.1:<port>.
class DevTcpPort final : public DevSerialPort
{
  public:
    bool listen(int port);
    void close();
    int available() override;
    int read(uint8_t* buf, size_t len) override;
    size_t write(const uint8_t* buf, size_t len) override;
    bool connected() override;

  private:
    void poll();
    int listenFd = -1;
    int clientFd = -1;
    std::vector<uint8_t> rx;
};

#endif
