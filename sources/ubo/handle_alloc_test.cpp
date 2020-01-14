#include "gtest/gtest.h"

TEST(handle_alloc_t, test_alloc) {
    handle_alloc_t<128> allocator;
    handle_t h0 = allocator.alloc();
    handle_t h1 = allocator.alloc();
    EXPECT_EQ(0, h0);
    EXPECT_EQ(1, h1);
}

TEST(handle_alloc_t, test_dealloc) {
    handle_alloc_t<128> allocator;
    handle_t h0 = allocator.alloc();
    EXPECT_EQ(0, h0);
    allocator.free(h0);
    handle_t h1 = allocator.alloc();
    EXPECT_EQ(0, h1);
}

TEST(handle_alloc_t, test_randome) {
    handle_alloc_t<255> allocator;
    handle_t h0 = allocator.alloc();
    handle_t h1 = allocator.alloc();
    EXPECT_EQ(0, h0);
    EXPECT_EQ(1, h1);
    allocator.free(h0);
    handle_t h2 = allocator.alloc();
    EXPECT_EQ(0, h2);
    handle_t h3 = allocator.alloc();
    EXPECT_EQ(2, h3);
    allocator.free(0);
    allocator.free(1);
    allocator.free(2);
    allocator.alloc();
    allocator.alloc();
    allocator.alloc();
    EXPECT_EQ(3, allocator.alloc());
}

TEST(handle_alloc_t, test_sequential) {
    handle_alloc_t<255> allocator;
    for (int i = 0; i < 255; i++)
        allocator.alloc();
    for (int i = 0; i < 255; i++)
        allocator.free(i);
    EXPECT_EQ(254, allocator.alloc());
}
