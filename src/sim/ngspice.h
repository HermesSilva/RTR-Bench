// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the circuit simulator: a session of ngspice, loaded at run
// time as a shared library (ngspice.dll / libngspice.so next to the
// executable or on the system). The session runs one endless transient
// analysis of the circuit, paced by the bench clock: the sources the bench
// commands (outputs, potentiometers, switches) are asked for at every step,
// and the node voltages come back resampled at a fixed rate.
//
// ngspice keeps one circuit per process, hence one session. It stores every
// point of an analysis, so the endless transient is a sequence of runs, each
// starting from the state the previous one ended in.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/probe.h"

namespace sim {

class Ngspice {
public:
    static Ngspice &instance();
    ~Ngspice();
    Ngspice(const Ngspice &) = delete;
    Ngspice &operator=(const Ngspice &) = delete;

    enum class State { Missing, Idle, Running, Failed };
    // Loads the library on first use; false when it is not installed.
    bool available();
    State state() const;
    // The last error of the library or of the simulation, empty when none.
    std::string status() const;

    // Replaces the circuit and runs it. `lines` are the element lines (no
    // title, no analysis, no .end). With `carry` the node voltages of the
    // running circuit become the initial state of the new one, when the
    // nodes still exist; without it the lines carry their own `.ic`.
    void load(std::vector<std::string> lines, bool carry);
    void stop();

    // The sources declared `external` in the circuit, by name: a fixed
    // voltage (a control) or a wave (an output of the bench).
    void set_constant(const std::string &source, double volts);
    void set_wave(const std::string &source, const core::WaveSpec &spec, bool on);
    // A source that plays samples as they arrive (the audio of the
    // computer): `count` more of them at `rate` per second, in volts. The
    // source runs a few tens of milliseconds behind, so that the
    // simulation always has the sample it asks for.
    void push_stream(const std::string &source, const float *samples, size_t count, double rate);

    // The vectors the bench wants sampled ("n3", "vd1#branch").
    void set_watches(const std::vector<std::string> &vectors);
    // Moves the new samples of every watch into `out` (one list per watch,
    // all of the same length); returns that length.
    size_t drain(std::vector<std::vector<float>> &out);
    // The latest value of every vector of the circuit.
    void snapshot(std::vector<std::pair<std::string, double>> &out) const;

    // Simulated seconds since the first circuit; the simulation runs up to
    // the target and waits there.
    double time() const;
    void set_target(double seconds);

    static constexpr int64_t sample_ns = 20000;   // 50 kS/s

private:
    Ngspice();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sim
