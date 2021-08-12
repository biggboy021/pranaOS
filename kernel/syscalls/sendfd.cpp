/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/filesystem/FileDescription.h>
#include <kernel/net/LocalSocket.h>
#include <kernel/Process.h>

namespace Kernel {

KResultOr<FlatPtr> Process::sys$sendfd(int sockfd, int fd)
{
    VERIFY_PROCESS_BIG_LOCK_ACQUIRED(this)
    REQUIRE_PROMISE(sendfd);
    auto socket_description = fds().file_description(sockfd);
    if (!socket_description)
        return EBADF;
    if (!socket_description->is_socket())
        return ENOTSOCK;
    auto& socket = *socket_description->socket();
    if (!socket.is_local())
        return EAFNOSUPPORT;
    if (!socket.is_connected())
        return ENOTCONN;

    auto passing_descriptor = fds().file_description(fd);
    if (!passing_descriptor)
        return EBADF;

    auto& local_socket = static_cast<LocalSocket&>(socket);
    return local_socket.sendfd(*socket_description, *passing_descriptor);
}

KResultOr<FlatPtr> Process::sys$recvfd(int sockfd, int options)
{
    VERIFY_PROCESS_BIG_LOCK_ACQUIRED(this)
    REQUIRE_PROMISE(recvfd);
    auto socket_description = fds().file_description(sockfd);
    if (!socket_description)
        return EBADF;
    if (!socket_description->is_socket())
        return ENOTSOCK;
    auto& socket = *socket_description->socket();
    if (!socket.is_local())
        return EAFNOSUPPORT;

    auto new_fd_or_error = m_fds.allocate();
    if (new_fd_or_error.is_error())
        return new_fd_or_error.error();
    auto new_fd = new_fd_or_error.release_value();

    auto& local_socket = static_cast<LocalSocket&>(socket);
    auto received_descriptor_or_error = local_socket.recvfd(*socket_description);

    if (received_descriptor_or_error.is_error())
        return received_descriptor_or_error.error();

    u32 fd_flags = 0;
    if (options & O_CLOEXEC)
        fd_flags |= FD_CLOEXEC;

    m_fds[new_fd.fd].set(*received_descriptor_or_error.value(), fd_flags);
    return new_fd.fd;
}

}