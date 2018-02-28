#ifndef ASCII_H
#define ASCII_H

#include <stdio.h>
#include <stdint.h>
#include <sstream>

namespace Helper {

void setSocketTimeout(boost::asio::ip::tcp::socket &socket, unsigned int timeout = 10) {
    timeval tv{timeout,0};

    if(setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 ) {
        setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        boost::asio::socket_base::keep_alive optionk(true);
        socket.set_option(optionk);

        boost::asio::ip::tcp::no_delay option(true);
        socket.set_option(option);
    }
}

}

#endif // ASCII_H
