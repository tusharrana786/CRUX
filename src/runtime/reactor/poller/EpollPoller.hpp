#pragma once

#include "IEventPoller.hpp"

class EpollPoller : public IEventPoller {
public:
    EpollPoller();
    ~EpollPoller() override;
    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;

    void add(int fd, uint32_t events) override;
    void modify(int fd, uint32_t events) override;
    void remove(int fd) override;

    int wait(Event* events,
             int maxEvents,
             int timeoutMs) override;

private:
    int epollFd_;
};