#include "EpollPoller.hpp"

#include <cstring>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <vector>

EpollPoller::EpollPoller() {
  epollFd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epollFd_ == -1) {
    throw std::runtime_error("Failed to create epoll instance: " +
                             std::string(strerror(errno)));
  }
}

EpollPoller::~EpollPoller() { close(epollFd_); }

void EpollPoller::add(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
    throw std::runtime_error("Failed to add fd to epoll");
  }
}

void EpollPoller::modify(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;

  if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
    throw std::runtime_error("Failed to modify fd in epoll");
  }
}

void EpollPoller::remove(int fd) {
  if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
    throw std::runtime_error("Failed to remove fd from epoll");
  }
}

int EpollPoller::wait(Event *events, int maxEvents, int timeoutMs) {

  std::vector<epoll_event> epollEvents(maxEvents);

  int ready = epoll_wait(epollFd_, epollEvents.data(), maxEvents, timeoutMs);

  if (ready == -1) {
    if (errno == EINTR) {
      return 0;
    }
    throw std::runtime_error("epoll_wait failed");
  }

  for (int i = 0; i < ready; i++) {
    events[i].fd = epollEvents[i].data.fd;
    events[i].events = epollEvents[i].events;
  }

  return ready;
}