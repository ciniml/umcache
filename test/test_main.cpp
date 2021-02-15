/**
 * User mode cache using userfaultfd.
 * Copyright(C) 2021 Kenta IDA
 * SPDX: BSL-1.0
 */
#include <gtest/gtest.h>

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}