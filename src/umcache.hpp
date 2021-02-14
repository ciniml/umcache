#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include <limits>
#include <initializer_list>
#include <cassert>
#include <thread>
#include <vector>

struct EventFd;

class UserModeCache {
private:
    int uffd = -1;
    std::unique_ptr<EventFd> shutdown_event;
    std::size_t page_size = 0;
    std::size_t tag_shift = 0;
    void* frontend = nullptr;
    void* backend = nullptr;
    std::size_t cache_size = 0;
    std::size_t backend_size = 0;
    std::unique_ptr<std::thread> handler_thread;
    std::vector<std::size_t> tags;
    std::unique_ptr<void, decltype(&::free)> cache;

    void fault_handler();
public:
    UserModeCache(std::size_t cache_size, void* backend, std::size_t backend_size);
    UserModeCache(const UserModeCache&) = delete;
    UserModeCache(UserModeCache&&) = delete;
    ~UserModeCache();

    void* get() const { return this->frontend; }
    operator bool() const { return this->uffd >= 0; }
    operator void*() const { return this->get(); }
};