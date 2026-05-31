#pragma once

#include <cstdint>

struct Event {
    int fd;
    uint32_t events;
    void* userData = nullptr;
};