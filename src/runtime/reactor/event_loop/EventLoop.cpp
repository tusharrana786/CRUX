#include "EventLoop.hpp"
#include <unistd.h>

EventLoop::EventLoop() : running_(false) {}

void EventLoop::run() {
  running_ = true;

  while (running_) {
    Event events[10];
    int ready = poller_.wait(events, 10, -1);

    for (int i = 0; i < ready; i++) {
      auto it = callbacks_.find(events[i].fd);
      if (it != callbacks_.end()) {
        it->second(events[i].events);
      }
    }
  }
}

void EventLoop::stop() { running_ = false; }

void EventLoop::addFd(int fd, uint32_t events, EventCallback callback) {
  poller_.add(fd, events);

  callbacks_[fd] = std::move(callback);
}

void EventLoop::removeFd(int fd) {
    poller_.remove(fd);
    callbacks_.erase(fd);
}