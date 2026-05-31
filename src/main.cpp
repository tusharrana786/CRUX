#include "./runtime/reactor/poller/EpollPoller.hpp"

#include <iostream>
#include <unistd.h>
#include <sys/epoll.h>

int main() {
    EpollPoller poller;

    poller.add(STDIN_FILENO, EPOLLIN);

    while (true) {
        Event events[10];

        int ready = poller.wait(events, 10, -1);

        for (int i = 0; i < ready; i++) {
            std::cout << "fd readable: "
                      << events[i].fd
                      << std::endl;
        }
    }
}