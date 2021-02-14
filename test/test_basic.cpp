#include <gtest/gtest.h>
#include <umcache.hpp>
#include <utility.hpp>

TEST(Util, Bits)
{
    EXPECT_EQ(bits(0), 0);
    EXPECT_EQ(bits(1), 1);
    EXPECT_EQ(bits(2), 2);
    EXPECT_EQ(bits(3), 2);
    EXPECT_EQ(bits(4), 3);
    EXPECT_EQ(bits(7), 3);
    EXPECT_EQ(bits(8), 4);
    EXPECT_EQ(bits(15), 4);
    EXPECT_EQ(bits(16), 5);
    EXPECT_EQ(bits(31), 5);
    EXPECT_EQ(bits(65535), 16);
    EXPECT_EQ(bits(65536), 17);
}
TEST(Util, IsPOT)
{
    EXPECT_TRUE(is_power_of_two(0));
    EXPECT_TRUE(is_power_of_two(1));
    EXPECT_TRUE(is_power_of_two(2));
    EXPECT_TRUE(is_power_of_two(4));
    EXPECT_TRUE(is_power_of_two(8));
    EXPECT_TRUE(is_power_of_two(16));
    EXPECT_TRUE(is_power_of_two(32));
    EXPECT_TRUE(is_power_of_two(65536));
    EXPECT_TRUE(is_power_of_two(std::size_t(1)<<32));
    EXPECT_FALSE(is_power_of_two(3));
    EXPECT_FALSE(is_power_of_two((std::size_t(1)<<32)-1));
}

TEST(Basic, Construct)
{
    alignas(4096) std::uint8_t backend[8192];
    UserModeCache cache(4096, backend, sizeof(backend));
    EXPECT_TRUE(cache);
    EXPECT_NE(cache.get(), nullptr);
}

TEST(Basic, Cache)
{
    alignas(4096) std::uint8_t backend[8192];
    UserModeCache cache(4096, backend, sizeof(backend));
    EXPECT_TRUE(cache);
    EXPECT_NE(cache.get(), nullptr);

    auto frontend = reinterpret_cast<std::uint8_t*>(cache.get());
    std::printf("frontend: %p, backend: %p\n", frontend, backend);
    frontend[0] = 0;
    frontend[4096] = 1;
    frontend[0] = 2;
    EXPECT_EQ(backend[0], 0);
    EXPECT_EQ(backend[4096], 1);
    EXPECT_EQ(frontend[0], 2);

}