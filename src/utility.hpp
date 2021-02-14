#pragma once
#include <cstdint>

template<typename T>
static constexpr bool is_power_of_two(T n) { return (n & (n - 1)) == 0; }

template <typename T>
static std::size_t bits(T n) {
    std::size_t i = 0;
    for(; n > 0; ++i, n >>= 1);
    return i;
}