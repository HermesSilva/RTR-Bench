// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - a small blocking TCP client with timeouts, the same on Windows
// (Winsock) and POSIX. Used by the probes that talk over a socket.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace probes {

class TcpClient {
public:
    TcpClient();
    ~TcpClient();
    TcpClient(const TcpClient &) = delete;
    TcpClient &operator=(const TcpClient &) = delete;

    // Connects within `timeout_ms`; false on failure (see error()).
    bool connect(const std::string &host, uint16_t port, int timeout_ms);
    void close();
    bool connected() const { return socket_ != invalid_socket; }

    // Receives up to `size` bytes. Returns the count, 0 on timeout, -1 when
    // the connection is gone.
    int receive(char *buffer, size_t size, int timeout_ms);

    // Sends everything; false when the connection is gone.
    bool send(const char *data, size_t size);

    const std::string &error() const { return error_; }

private:
    static constexpr intptr_t invalid_socket = -1;
    intptr_t socket_ = invalid_socket;
    std::string error_;
};

}  // namespace probes
