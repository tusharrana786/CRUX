#include "../poller/EpollPoller.hpp"
#include <cstdint>
#include <functional>
#include <unordered_map>

typedef std::function<void(uint32_t)> EventCallback;

class EventLoop {
public:
  EventLoop();

  void run();
  void stop();
  void addFd(int fd, uint32_t events, EventCallback callback);
  void removeFd(int fd);

private:
  EpollPoller poller_;

  bool running_;

  std::unordered_map<int, EventCallback> callbacks_;
};