// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "core/scope_protocol.h"

using core::ScopeMessage;
using core::parse_scope_line;

TEST_CASE("version 1 event line")
{
    ScopeMessage m;
    REQUIRE(parse_scope_line("123456789 18 1", m));
    REQUIRE(m.kind == ScopeMessage::Event);
    REQUIRE(m.ns == 123456789);
    REQUIRE(m.pin == 18);
    REQUIRE(m.level == 1);
    REQUIRE(m.fsel == -1);
}

TEST_CASE("version 1 tolerates a carriage return and extra spaces")
{
    ScopeMessage m;
    REQUIRE(parse_scope_line("5  18   0\r", m));
    REQUIRE(m.level == 0);
}

TEST_CASE("version 1 rejects malformed lines")
{
    ScopeMessage m;
    REQUIRE_FALSE(parse_scope_line("", m));
    REQUIRE_FALSE(parse_scope_line("12 18", m));
    REQUIRE_FALSE(parse_scope_line("12 18 2", m));
    REQUIRE_FALSE(parse_scope_line("12 x 1", m));
    REQUIRE_FALSE(parse_scope_line("12 18 1 extra", m));
    REQUIRE_FALSE(parse_scope_line("12 -3 1", m));
}

TEST_CASE("version 2 lines")
{
    ScopeMessage m;
    REQUIRE(parse_scope_line("V 2 raspi4b 58", m));
    REQUIRE(m.kind == ScopeMessage::Version);
    REQUIRE(m.version == 2);
    REQUIRE(m.pins == 58);

    REQUIRE(parse_scope_line("E 1000 4 1", m));
    REQUIRE(m.kind == ScopeMessage::Event);
    REQUIRE(m.ns == 1000);
    REQUIRE(m.pin == 4);
    REQUIRE(m.level == 1);

    REQUIRE(parse_scope_line("F 2000 4 1", m));
    REQUIRE(m.kind == ScopeMessage::Function);
    REQUIRE(m.fsel == 1);

    REQUIRE(parse_scope_line("S 3000 4 0 1", m));
    REQUIRE(m.kind == ScopeMessage::Snapshot);
    REQUIRE(m.fsel == 0);
    REQUIRE(m.level == 1);

    REQUIRE(parse_scope_line("T 4000", m));
    REQUIRE(m.kind == ScopeMessage::Time);
    REQUIRE(m.ns == 4000);
}

TEST_CASE("version 2 rejects bad fields")
{
    ScopeMessage m;
    REQUIRE_FALSE(parse_scope_line("E 1000 4", m));
    REQUIRE_FALSE(parse_scope_line("F 1000 4 9", m));
    REQUIRE_FALSE(parse_scope_line("S 1000 4 0", m));
    REQUIRE_FALSE(parse_scope_line("X 1000", m));
    REQUIRE_FALSE(parse_scope_line("EE 1000 4 1", m));
    REQUIRE_FALSE(parse_scope_line("T", m));
}

TEST_CASE("large virtual times fit")
{
    ScopeMessage m;
    REQUIRE(parse_scope_line("9223372036854775807 0 1", m));
    REQUIRE(m.ns == 9223372036854775807LL);
    REQUIRE_FALSE(parse_scope_line("99999999999999999999999 0 1", m));
}
