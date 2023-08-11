/*
 * Copyright (c) 2019, Marcus Geelnard <m at bitsnbites dot eu>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef REDIS_SOCKCOMPAT_H
#define REDIS_SOCKCOMPAT_H

#ifndef _WIN32
/* For POSIX systems we use the standard BSD socket API. */
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

namespace redis_sockcompat {
    ssize_t send_redis_sockcompat(int fd, const void *buf, size_t len, int flags) {
        return send(fd, buf, len, flags);
    }
}
#else
/* For Windows we use winsock. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* To get WSAPoll etc. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stddef.h>

#ifdef _MSC_VER
typedef signed long ssize_t;
#endif

namespace redis_sockcompat {
    static int _wsaErrorToErrno(int err) {
        switch (err) {
            case WSAEWOULDBLOCK:
                return EWOULDBLOCK;
            case WSAEINPROGRESS:
                return EINPROGRESS;
            case WSAEALREADY:
                return EALREADY;
            case WSAENOTSOCK:
                return ENOTSOCK;
            case WSAEDESTADDRREQ:
                return EDESTADDRREQ;
            case WSAEMSGSIZE:
                return EMSGSIZE;
            case WSAEPROTOTYPE:
                return EPROTOTYPE;
            case WSAENOPROTOOPT:
                return ENOPROTOOPT;
            case WSAEPROTONOSUPPORT:
                return EPROTONOSUPPORT;
            case WSAEOPNOTSUPP:
                return EOPNOTSUPP;
            case WSAEAFNOSUPPORT:
                return EAFNOSUPPORT;
            case WSAEADDRINUSE:
                return EADDRINUSE;
            case WSAEADDRNOTAVAIL:
                return EADDRNOTAVAIL;
            case WSAENETDOWN:
                return ENETDOWN;
            case WSAENETUNREACH:
                return ENETUNREACH;
            case WSAENETRESET:
                return ENETRESET;
            case WSAECONNABORTED:
                return ECONNABORTED;
            case WSAECONNRESET:
                return ECONNRESET;
            case WSAENOBUFS:
                return ENOBUFS;
            case WSAEISCONN:
                return EISCONN;
            case WSAENOTCONN:
                return ENOTCONN;
            case WSAETIMEDOUT:
                return ETIMEDOUT;
            case WSAECONNREFUSED:
                return ECONNREFUSED;
            case WSAELOOP:
                return ELOOP;
            case WSAENAMETOOLONG:
                return ENAMETOOLONG;
            case WSAEHOSTUNREACH:
                return EHOSTUNREACH;
            case WSAENOTEMPTY:
                return ENOTEMPTY;
            default:
                /* We just return a generic I/O error if we could not find a relevant error. */
                return EIO;
        }
    }

    static void _updateErrno(int success) {
        errno = success ? 0 : _wsaErrorToErrno(WSAGetLastError());
    }

    ssize_t send_redis_sockcompat(SOCKET sockfd, const void *buf, size_t len, int flags) {
        int ret = send(sockfd, (const char*)buf, (int)len, flags);
        _updateErrno(ret != SOCKET_ERROR);
        return ret != SOCKET_ERROR ? ret : -1;
    }
}

#endif /* _WIN32 */

#endif /* REDIS_SOCKCOMPAT_H */
