// SPDX-License-Identifier: Apache-2.0
#include "probes/tcp_client.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace probes {

namespace {

#ifdef _WIN32
struct WinsockInit {
    WinsockInit()
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};

void ensure_winsock()
{
    static WinsockInit init;
}

int last_error() { return WSAGetLastError(); }
void close_socket(intptr_t s) { closesocket(static_cast<SOCKET>(s)); }
void set_blocking(intptr_t s, bool blocking)
{
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(static_cast<SOCKET>(s), static_cast<long>(FIONBIO), &mode);
}
bool in_progress(int err) { return err == WSAEWOULDBLOCK; }
#else
void ensure_winsock() {}
int last_error() { return errno; }
void close_socket(intptr_t s) { ::close(static_cast<int>(s)); }
void set_blocking(intptr_t s, bool blocking)
{
    int flags = fcntl(static_cast<int>(s), F_GETFL, 0);
    if (flags < 0) {
        return;
    }
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    fcntl(static_cast<int>(s), F_SETFL, flags);
}
bool in_progress(int err) { return err == EINPROGRESS; }
#endif

bool wait_for(intptr_t s, bool writable, int timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
#ifdef _WIN32
    FD_SET(static_cast<SOCKET>(s), &set);
    int nfds = 0;
#else
    FD_SET(static_cast<int>(s), &set);
    int nfds = static_cast<int>(s) + 1;
#endif
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(nfds, writable ? nullptr : &set, writable ? &set : nullptr, nullptr, &tv);
    return r > 0;
}

}  // namespace

TcpClient::TcpClient()
{
    ensure_winsock();
}

TcpClient::~TcpClient()
{
    close();
}

bool TcpClient::connect(const std::string &host, uint16_t port, int timeout_ms)
{
    close();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &result) != 0 || !result) {
        error_ = "cannot resolve " + host;
        return false;
    }

    bool ok = false;
    int detail = 0;
    for (addrinfo *ai = result; ai && !ok; ai = ai->ai_next) {
        intptr_t s = static_cast<intptr_t>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (s == invalid_socket) {
            detail = last_error();
            continue;
        }
        set_blocking(s, false);
        int r = ::connect(static_cast<decltype(::socket(0, 0, 0))>(s), ai->ai_addr,
                          static_cast<socklen_type>(ai->ai_addrlen));
        int connect_error = r == 0 ? 0 : last_error();
        if (r == 0 || in_progress(connect_error)) {
            if (r == 0 || wait_for(s, true, timeout_ms)) {
                int err = 0;
                socklen_type len = sizeof(err);
                getsockopt(static_cast<decltype(::socket(0, 0, 0))>(s), SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char *>(&err), &len);
                detail = err;
                if (err == 0) {
                    ok = true;
                    socket_ = s;
                    set_blocking(s, true);
                    int one = 1;
                    setsockopt(static_cast<decltype(::socket(0, 0, 0))>(s), IPPROTO_TCP, TCP_NODELAY,
                               reinterpret_cast<const char *>(&one), sizeof(one));
                }
            }
        } else {
            detail = connect_error;
        }
        if (!ok) {
            close_socket(s);
        }
    }
    freeaddrinfo(result);
    if (!ok) {
        error_ = "no connection to " + host + ":" + service + " (error " + std::to_string(detail) + ")";
    }
    return ok;
}

void TcpClient::close()
{
    if (socket_ != invalid_socket) {
        close_socket(socket_);
        socket_ = invalid_socket;
    }
}

int TcpClient::receive(char *buffer, size_t size, int timeout_ms)
{
    if (socket_ == invalid_socket) {
        return -1;
    }
    if (!wait_for(socket_, false, timeout_ms)) {
        return 0;
    }
#ifdef _WIN32
    int r = recv(static_cast<SOCKET>(socket_), buffer, static_cast<int>(size), 0);
#else
    int r = static_cast<int>(recv(static_cast<int>(socket_), buffer, size, 0));
#endif
    if (r <= 0) {
        error_ = "connection closed";
        close();
        return -1;
    }
    return r;
}

bool TcpClient::send(const char *data, size_t size)
{
    if (socket_ == invalid_socket) {
        return false;
    }
    size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        int r = ::send(static_cast<SOCKET>(socket_), data + sent, static_cast<int>(size - sent), 0);
#else
        int r = static_cast<int>(::send(static_cast<int>(socket_), data + sent, size - sent, MSG_NOSIGNAL));
#endif
        if (r <= 0) {
            error_ = "connection closed";
            close();
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

}  // namespace probes
