// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "core/event_queue.h"

TEST_CASE("event queue keeps order and reports fullness")
{
    core::EventQueue<int> q(4);   // rounds up to 8 slots, 7 usable
    REQUIRE(q.capacity() == 7);
    for (int i = 0; i < 7; i++) {
        REQUIRE(q.push(i));
    }
    REQUIRE_FALSE(q.push(99));
    std::vector<int> out;
    REQUIRE(q.drain(out) == 7);
    REQUIRE(out.size() == 7);
    for (int i = 0; i < 7; i++) {
        REQUIRE(out[static_cast<size_t>(i)] == i);
    }
    REQUIRE(q.drain(out) == 0);
}

TEST_CASE("event queue works across threads")
{
    core::EventQueue<int> q(1024);
    const int total = 200000;
    std::thread producer([&] {
        for (int i = 0; i < total;) {
            if (q.push(i)) {
                i++;
            }
        }
    });
    std::vector<int> out;
    int expected = 0;
    while (expected < total) {
        out.clear();
        q.drain(out);
        for (int v : out) {
            REQUIRE(v == expected);
            expected++;
        }
    }
    producer.join();
    REQUIRE(expected == total);
}
