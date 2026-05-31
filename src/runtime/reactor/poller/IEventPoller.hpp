#include <cstdint>
#include "../Event.hpp"

class IEventPoller {
public:
    virtual ~IEventPoller() = default;

    virtual void add(int fd, uint32_t events) = 0;
    virtual void modify(int fd, uint32_t events) = 0;
    virtual void remove(int fd) = 0;

    virtual int wait(Event* events,
                     int maxEvents,
                     int timeoutMs) = 0;
};