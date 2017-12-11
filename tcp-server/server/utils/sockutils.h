#ifndef _SOCKET_UTILS
#define _SOCKET_UTILS

#include <fcntl.h>
#include <c++/5/iostream>

namespace socket_utils {
    bool set_socket_nonblock(int &sock) {
        auto &&flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) {
            std::cout <<  "fcntl failed (F_GETFL)" << std::endl;
            return false;
        }

        flags |= O_NONBLOCK;
        auto &&stat = fcntl(sock, F_SETFL, flags);
        if (stat == -1) {
            std::cout <<  "fcntl failed (F_SETFL)" << std::endl;
            return false;
        }
        return true;
    }
}
#endif