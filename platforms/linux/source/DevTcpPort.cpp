#include "DevTcpPort.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

bool DevTcpPort::listen(int port)
{
    listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd < 0)
        return false;
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(listenFd, 1) < 0)
    {
        perror("dev port");
        ::close(listenFd);
        listenFd = -1;
        return false;
    }
    return true;
}

void DevTcpPort::close()
{
    if (clientFd >= 0)
        ::close(clientFd);
    if (listenFd >= 0)
        ::close(listenFd);
    clientFd = listenFd = -1;
}

void DevTcpPort::poll()
{
    if (listenFd < 0)
        return;
    if (clientFd < 0)
    {
        int fd = accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0)
        {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            clientFd = fd;
            rx.clear();
        }
        return;
    }
    uint8_t buf[4096];
    while (true)
    {
        ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
        if (n > 0)
        {
            rx.insert(rx.end(), buf, buf + n);
            if (rx.size() > (1u << 20))
                break; // upload stream backlog; let the consumer drain
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
        {
            ::close(clientFd);
            clientFd = -1;
            rx.clear();
        }
        break;
    }
}

int DevTcpPort::available()
{
    poll();
    return (int)rx.size();
}

int DevTcpPort::read(uint8_t* buf, size_t len)
{
    poll();
    size_t n = rx.size() < len ? rx.size() : len;
    if (n == 0)
        return 0;
    memcpy(buf, rx.data(), n);
    rx.erase(rx.begin(), rx.begin() + n);
    return (int)n;
}

size_t DevTcpPort::write(const uint8_t* buf, size_t len)
{
    if (clientFd < 0)
        return len;
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(clientFd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            usleep(200);
            continue;
        }
        ::close(clientFd);
        clientFd = -1;
        break;
    }
    return sent;
}

bool DevTcpPort::connected()
{
    poll();
    return clientFd >= 0;
}
