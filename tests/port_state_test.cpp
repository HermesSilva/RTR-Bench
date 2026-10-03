// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "core/port_state.h"

using core::DigitalEvent;
using core::PortState;

static DigitalEvent ev(int64_t ns, uint16_t port, uint8_t level, uint8_t kind = DigitalEvent::Transition)
{
    DigitalEvent e;
    e.ns = ns;
    e.port = port;
    e.level = level;
    e.kind = kind;
    return e;
}

TEST_CASE("port state follows transitions and counts them")
{
    PortState s(4);
    REQUIRE(s.at(1).level == -1);
    s.apply(ev(100, 1, 1));
    s.apply(ev(200, 1, 0));
    s.apply(ev(300, 1, 0));   // same level: not a transition
    s.end_frame();
    REQUIRE(s.at(1).level == 0);
    REQUIRE(s.at(1).transitions == 2);
    REQUIRE(s.at(1).recent == 2);
    REQUIRE(s.at(1).last_change_ns == 200);
    REQUIRE(s.last_ns() == 300);
    s.end_frame();
    REQUIRE(s.at(1).recent == 0);
    REQUIRE(s.at(1).transitions == 2);
}

TEST_CASE("snapshots set the level without counting a transition")
{
    PortState s(4);
    s.apply(ev(10, 2, 1, DigitalEvent::Snapshot));
    s.end_frame();
    REQUIRE(s.at(2).level == 1);
    REQUIRE(s.at(2).transitions == 0);
    REQUIRE(s.at(2).recent == 0);
}

TEST_CASE("out-of-range ports and reset")
{
    PortState s(2);
    s.apply(ev(10, 7, 1));   // ignored
    s.set_direction(1, core::PortDirection::Output);
    s.set_direction(9, core::PortDirection::Input);   // ignored
    REQUIRE(s.at(1).direction == core::PortDirection::Output);
    s.reset();
    REQUIRE(s.at(1).direction == core::PortDirection::Unknown);
    REQUIRE(s.last_ns() == -1);
}
