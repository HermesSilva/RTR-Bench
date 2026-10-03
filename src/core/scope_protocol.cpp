// SPDX-License-Identifier: Apache-2.0
#include "core/scope_protocol.h"

#include <cstdlib>
#include <vector>

namespace core {

namespace {

// Splits on spaces and tabs; at most 6 fields matter.
size_t split(std::string_view line, std::string_view *fields, size_t max)
{
    size_t count = 0;
    size_t i = 0;
    while (i < line.size() && count < max) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            i++;
        }
        size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            i++;
        }
        if (i > start) {
            fields[count++] = line.substr(start, i - start);
        }
    }
    return count;
}

bool to_i64(std::string_view s, int64_t &value)
{
    if (s.empty() || s.size() > 20) {
        return false;
    }
    char buffer[24];
    s.copy(buffer, s.size());
    buffer[s.size()] = '\0';
    char *end = nullptr;
    long long v = std::strtoll(buffer, &end, 10);
    if (end != buffer + s.size()) {
        return false;
    }
    value = v;
    return true;
}

bool to_int(std::string_view s, int &value)
{
    int64_t v = 0;
    if (!to_i64(s, v) || v < -2147483647LL || v > 2147483647LL) {
        return false;
    }
    value = static_cast<int>(v);
    return true;
}

}  // namespace

bool parse_scope_line(std::string_view line, ScopeMessage &out)
{
    std::string_view f[6];
    size_t n = split(line, f, 6);
    out = ScopeMessage{};
    if (n == 0) {
        return false;
    }

    // Version 1: three numbers.
    if (f[0][0] >= '0' && f[0][0] <= '9') {
        if (n != 3 || !to_i64(f[0], out.ns) || !to_int(f[1], out.pin) || !to_int(f[2], out.level)) {
            return false;
        }
        out.kind = ScopeMessage::Event;
        return out.pin >= 0 && (out.level == 0 || out.level == 1);
    }

    if (f[0].size() != 1) {
        return false;
    }
    switch (f[0][0]) {
    case 'E':
        if (n != 4 || !to_i64(f[1], out.ns) || !to_int(f[2], out.pin) || !to_int(f[3], out.level)) {
            return false;
        }
        out.kind = ScopeMessage::Event;
        return out.pin >= 0 && (out.level == 0 || out.level == 1);
    case 'F':
        if (n != 4 || !to_i64(f[1], out.ns) || !to_int(f[2], out.pin) || !to_int(f[3], out.fsel)) {
            return false;
        }
        out.kind = ScopeMessage::Function;
        return out.pin >= 0 && out.fsel >= 0 && out.fsel <= 7;
    case 'S':
        if (n != 5 || !to_i64(f[1], out.ns) || !to_int(f[2], out.pin) || !to_int(f[3], out.fsel) ||
            !to_int(f[4], out.level)) {
            return false;
        }
        out.kind = ScopeMessage::Snapshot;
        return out.pin >= 0 && out.fsel >= 0 && out.fsel <= 7 && (out.level == 0 || out.level == 1);
    case 'V':
        if (n < 2 || !to_int(f[1], out.version)) {
            return false;
        }
        if (n >= 4 && !to_int(f[3], out.pins)) {
            return false;
        }
        out.kind = ScopeMessage::Version;
        return out.version >= 1;
    case 'T':
        if (n != 2 || !to_i64(f[1], out.ns)) {
            return false;
        }
        out.kind = ScopeMessage::Time;
        return true;
    default:
        return false;
    }
}

}  // namespace core
