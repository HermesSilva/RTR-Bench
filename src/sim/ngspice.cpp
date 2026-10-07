// SPDX-License-Identifier: Apache-2.0
#include "sim/ngspice.h"

#include "sim/digital.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sim {

namespace {

// The part of ngspice's sharedspice.h the bench uses. Only the leading
// fields of the structures are read, so they are declared up to there.
struct VecValue {
    char *name;
    double real;
    double imag;
};
struct VecValuesAll {
    int count;
    int index;
    VecValue **values;
};
struct VecInfoAll;

using SendChar = int(char *, int, void *);
using SendStat = int(char *, int, void *);
using ControlledExit = int(int, bool, bool, int, void *);
using SendData = int(VecValuesAll *, int, int, void *);
using SendInitData = int(VecInfoAll *, int, void *);
using BgRunning = int(bool, int, void *);
using GetSource = int(double *, double, char *, int, void *);
using GetSync = int(double, double *, double, int, int, int, void *);

struct Api {
    int (*init)(SendChar *, SendStat *, ControlledExit *, SendData *, SendInitData *, BgRunning *, void *) = nullptr;
    int (*init_sync)(GetSource *, GetSource *, GetSync *, int *, void *) = nullptr;
    int (*command)(char *) = nullptr;
    int (*circ)(char **) = nullptr;
    bool (*running)() = nullptr;
};

void *open_library(const char *name)
{
#ifdef _WIN32
    return LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename T>
bool symbol(void *library, const char *name, T &out)
{
#ifdef _WIN32
    // Through the generic function pointer: the type of the symbol is the
    // one its declaration in the Api says.
    out = reinterpret_cast<T>(reinterpret_cast<void (*)()>(GetProcAddress(static_cast<HMODULE>(library), name)));
#else
    out = reinterpret_cast<T>(dlsym(library, name));
#endif
    return out != nullptr;
}

std::string lower(const char *text)
{
    std::string out = text ? text : "";
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string number_text(double value)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", value);
    return text;
}

struct Source {
    std::string name;
    bool wave = false;
    double volts = 0.0;
    core::WaveSpec spec;
    bool on = false;
    // A stream: the latest samples, the last of them at the simulated time `end`.
    bool stream = false;
    bool started = false;
    std::vector<float> samples;
    double rate = 48000.0;
    double end = 0.0;
};

struct Resistance {
    std::string name;
    double ohms;
};

constexpr double stream_delay = 0.03;   // seconds a stream runs behind the simulation

constexpr double sample_seconds = static_cast<double>(Ngspice::sample_ns) / 1e9;
constexpr size_t max_pending = 200000;   // samples kept when nobody drains them

}  // namespace

struct Ngspice::Impl {
    Api api;
    bool tried = false;
    bool loaded = false;
    std::thread thread;
    mutable std::mutex mutex;
    std::condition_variable cv;
    State state = State::Missing;
    std::string error;

    // Requests to the session thread.
    bool quit = false;
    bool load_pending = false;
    bool stop_pending = false;
    bool pending_carry = false;
    std::vector<std::string> pending_lines;

    // The circuit and its current run.
    std::vector<std::string> lines;
    bool has_circuit = false;
    bool started = false;
    bool halt = false;          // the run must leave its wait
    bool dead = false;          // the library asked to exit: no more runs
    bool fresh = false;         // the next data point is the first of a run
    std::chrono::steady_clock::time_point started_at;
    double run_stop = 0.0;      // where this run ends, its own time
    double run_time = 0.0;      // where it got to
    double base = 0.0;          // simulated seconds before this run
    double target = 0.0;
    int64_t steps = 0;

    std::vector<std::string> names;   // the vectors of the run
    std::vector<double> latest;
    int time_index = -1;

    std::vector<std::string> watches;
    std::vector<int> watch_vector;    // index into names, -1 when the circuit has no such vector
    std::vector<std::vector<float>> samples;
    bool have_previous = false;
    double previous_time = 0.0;
    std::vector<double> previous;
    std::vector<double> current;
    int64_t next_sample = 0;

    std::vector<Source> sources;
    std::vector<Resistance> resistances;   // as the bench last gave them
    std::vector<Resistance> altered;       // those the running circuit does not have yet
    DigitalEngine digital;   // the gates, the delay lines and the island of the circuit
    int ident = 0;
    // RTR_BENCH_SIM_TRACE in the environment: everything ngspice says goes to stderr.
    const bool trace = std::getenv("RTR_BENCH_SIM_TRACE") != nullptr;

    bool load_library();
    void main();
    void halt_run(std::unique_lock<std::mutex> &lock);
    void alter_run(std::unique_lock<std::mutex> &lock);
    void start_run(std::unique_lock<std::mutex> &lock, bool carry);
    void command(const char *text) const;
    void map_watches();
    void on_text(const char *text);
    void on_data(VecValuesAll *all);
    double source_volts(const char *name, double seconds);
    Source &source(const std::string &name);

    static int cb_text(char *text, int, void *self)
    {
        static_cast<Impl *>(self)->on_text(text);
        return 0;
    }
    static int cb_stat(char *, int, void *) { return 0; }
    static int cb_exit(int, bool, bool, int, void *self)
    {
        auto *impl = static_cast<Impl *>(self);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->dead = true;
        impl->error = "ngspice stopped working";
        return 0;
    }
    static int cb_data(VecValuesAll *all, int, int, void *self)
    {
        static_cast<Impl *>(self)->on_data(all);
        return 0;
    }
    static int cb_init(VecInfoAll *, int, void *self)
    {
        auto *impl = static_cast<Impl *>(self);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->fresh = true;
        return 0;
    }
    static int cb_running(bool, int, void *) { return 0; }
    static int cb_source(double *volts, double seconds, char *name, int, void *self)
    {
        *volts = static_cast<Impl *>(self)->source_volts(name, seconds);
        return 0;
    }
};

bool Ngspice::Impl::load_library()
{
#ifdef _WIN32
    const char *const candidates[] = {"ngspice.dll", "libngspice-0.dll"};
    const char *const wanted = "ngspice.dll";
#else
    const char *const candidates[] = {"./libngspice.so.0", "libngspice.so.0", "libngspice.so"};
    const char *const wanted = "libngspice.so.0";
#endif
    void *library = nullptr;
    for (const char *name : candidates) {
        library = open_library(name);
        if (library) {
            break;
        }
    }
    if (!library) {
        error = std::string("ngspice not found (") + wanted + ")";
        return false;
    }
    bool complete = symbol(library, "ngSpice_Init", api.init);
    complete = symbol(library, "ngSpice_Init_Sync", api.init_sync) && complete;
    complete = symbol(library, "ngSpice_Command", api.command) && complete;
    complete = symbol(library, "ngSpice_Circ", api.circ) && complete;
    complete = symbol(library, "ngSpice_running", api.running) && complete;
    if (!complete) {
        error = "ngspice library without the shared interface";
        return false;
    }
    return true;
}

void Ngspice::Impl::command(const char *text) const
{
    std::string buffer(text);
    api.command(buffer.data());
}

void Ngspice::Impl::map_watches()
{
    watch_vector.assign(watches.size(), -1);
    for (size_t w = 0; w < watches.size(); w++) {
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == watches[w]) {
                watch_vector[w] = static_cast<int>(i);
                break;
            }
        }
    }
    have_previous = false;
}

// What ngspice prints. Errors are kept for the bench to show.
void Ngspice::Impl::on_text(const char *text)
{
    if (!text) {
        return;
    }
    const char *body = text;
    if (std::strncmp(body, "stderr ", 7) == 0 || std::strncmp(body, "stdout ", 7) == 0) {
        body += 7;
    }
    if (trace) {
        std::fprintf(stderr, "ngspice: %s\n", body);
    }
    const std::string low = lower(body);
    const char *const marks[] = {"error", "singular", "too small", "fail", "abort"};
    for (const char *mark : marks) {
        if (low.find(mark) != std::string::npos) {
            std::lock_guard<std::mutex> lock(mutex);
            error = body;
            return;
        }
    }
}

// One accepted time point of the run, from the simulation thread: keep the
// values, resample the watched ones on the fixed grid, and wait for the
// bench clock when ahead of it.
void Ngspice::Impl::on_data(VecValuesAll *all)
{
    if (!all || all->count <= 0 || !all->values) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex);
    const auto count = static_cast<size_t>(all->count);
    if (fresh || names.size() != count) {
        names.clear();
        time_index = -1;
        for (size_t i = 0; i < count; i++) {
            std::string name = lower(all->values[i]->name);
            if (name.size() > 3 && name.compare(0, 2, "v(") == 0 && name.back() == ')') {
                name = name.substr(2, name.size() - 3);
            }
            if (name == "time") {
                time_index = static_cast<int>(i);
            }
            names.push_back(std::move(name));
        }
        latest.assign(count, 0.0);
        map_watches();
        digital.map(names);
        fresh = false;
    }
    if (time_index < 0) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        latest[i] = all->values[i]->real;
    }
    run_time = latest[static_cast<size_t>(time_index)];
    steps++;
    const double total = base + run_time;
    digital.accept(total, latest);

    current.resize(watches.size());
    for (size_t w = 0; w < watches.size(); w++) {
        current[w] = watch_vector[w] >= 0 ? latest[static_cast<size_t>(watch_vector[w])] : 0.0;
    }
    if (!have_previous) {
        previous = current;
        previous_time = total;
        have_previous = true;
        next_sample = std::max(next_sample, static_cast<int64_t>(std::ceil(total / sample_seconds - 1e-9)));
    } else if (total > previous_time) {
        while (static_cast<double>(next_sample) * sample_seconds <= total) {
            double at = static_cast<double>(next_sample) * sample_seconds;
            double a = std::clamp((at - previous_time) / (total - previous_time), 0.0, 1.0);
            for (size_t w = 0; w < watches.size(); w++) {
                samples[w].push_back(static_cast<float>(previous[w] + a * (current[w] - previous[w])));
            }
            next_sample++;
        }
        previous = current;
        previous_time = total;
    }
    if (!samples.empty() && samples[0].size() > max_pending) {
        for (std::vector<float> &list : samples) {
            list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - max_pending / 2));
        }
    }
    cv.wait(lock, [&] { return halt || quit || total <= target; });
}

double Ngspice::Impl::source_volts(const char *name, double seconds)
{
    const std::string wanted = lower(name);
    std::lock_guard<std::mutex> lock(mutex);
    double digital_volts = 0.0;
    if (digital.source(wanted, base + seconds, digital_volts)) {
        return digital_volts;
    }
    for (const Source &s : sources) {
        if (s.name == wanted) {
            if (s.stream) {
                if (s.samples.empty()) {
                    return 0.0;
                }
                const auto last = static_cast<double>(s.samples.size() - 1);
                double position = std::clamp(last - (s.end - (base + seconds)) * s.rate, 0.0, last);
                auto index = static_cast<size_t>(position);
                double a = position - static_cast<double>(index);
                double v0 = s.samples[index];
                double v1 = s.samples[std::min(index + 1, s.samples.size() - 1)];
                return v0 + (v1 - v0) * a;
            }
            if (!s.wave) {
                return s.volts;
            }
            return s.on ? core::waveform_volts(s.spec, base + seconds) : 0.0;
        }
    }
    return 0.0;
}

Source &Ngspice::Impl::source(const std::string &name)
{
    const std::string wanted = lower(name.c_str());
    for (Source &s : sources) {
        if (s.name == wanted) {
            return s;
        }
    }
    sources.emplace_back();
    sources.back().name = wanted;
    return sources.back();
}

// Stops the run in progress, wherever it is; its time is added to the base.
void Ngspice::Impl::halt_run(std::unique_lock<std::mutex> &lock)
{
    halt = true;
    cv.notify_all();
    lock.unlock();
    if (api.running()) {
        command("bg_halt");
    }
    for (int i = 0; i < 2000 && api.running(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    lock.lock();
    halt = false;
    base += run_time;
    run_time = 0.0;
    started = false;
}

// Gives the running circuit the resistances that changed: the run stops
// where it is, takes them and goes on. The lines of the circuit take them
// too, for the runs that follow.
void Ngspice::Impl::alter_run(std::unique_lock<std::mutex> &lock)
{
    std::vector<Resistance> list;
    list.swap(altered);
    for (const Resistance &r : list) {
        for (std::string &line : lines) {
            if (line.compare(0, r.name.size() + 1, r.name + " ") == 0) {
                line = line.substr(0, line.rfind(' ') + 1) + number_text(r.ohms);
            }
        }
    }
    if (!started) {
        return;
    }
    halt = true;
    cv.notify_all();
    lock.unlock();
    if (api.running()) {
        command("bg_halt");
    }
    for (int i = 0; i < 2000 && api.running(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const Resistance &r : list) {
        command(("alter " + r.name + "=" + number_text(r.ohms)).c_str());
    }
    command("bg_resume");
    lock.lock();
    halt = false;
    started_at = std::chrono::steady_clock::now();
}

void Ngspice::Impl::start_run(std::unique_lock<std::mutex> &lock, bool carry)
{
    std::vector<std::string> deck;
    deck.emplace_back("RTR-Bench circuit");
    for (const std::string &line : lines) {
        std::string out = line;
        // An inductor starts with the current it had.
        if (carry && !out.empty() && (out[0] == 'l' || out[0] == 'L') && out.find(" ic=") == std::string::npos) {
            const std::string branch = lower(out.substr(0, out.find(' ')).c_str()) + "#branch";
            for (size_t i = 0; i < names.size(); i++) {
                if (names[i] == branch && std::isfinite(latest[i])) {
                    out += " ic=" + number_text(latest[i]);
                }
            }
        }
        deck.push_back(std::move(out));
    }
    if (carry) {
        for (size_t i = 0; i < names.size(); i++) {
            bool node = static_cast<int>(i) != time_index && !names[i].empty() && names[i].find('#') == std::string::npos;
            if (node && std::isfinite(latest[i])) {
                deck.push_back(".ic v(" + names[i] + ")=" + number_text(latest[i]));
            }
        }
    }
    // ngspice keeps every point of every vector: the run is as long as a
    // fixed amount of memory allows, then the next one takes over.
    run_stop = std::clamp(4e6 / (1e9 / static_cast<double>(sample_ns) * static_cast<double>(lines.size() + 4)), 0.5, 20.0);
    deck.push_back(".tran " + number_text(sample_seconds) + " " + number_text(run_stop) + " 0 " +
                   number_text(sample_seconds) + " uic");
    deck.emplace_back(".end");
    std::vector<char *> pointers;
    pointers.reserve(deck.size() + 1);
    for (std::string &line : deck) {
        pointers.push_back(line.data());
    }
    pointers.push_back(nullptr);

    const bool had_circuit = has_circuit;
    fresh = true;
    have_previous = false;
    run_time = 0.0;
    lock.unlock();
    if (had_circuit) {
        command("destroy all");
        command("remcirc");
    }
    api.circ(pointers.data());
    command("bg_run");
    lock.lock();
    has_circuit = true;
    started = true;
    started_at = std::chrono::steady_clock::now();
    state = State::Running;
}

// The session thread: every call into the library is made from here.
void Ngspice::Impl::main()
{
    // No initialization file is read: the bench ships none, and the analog
    // devices it uses are built into the library.
    api.init(&Impl::cb_text, &Impl::cb_stat, &Impl::cb_exit, &Impl::cb_data, &Impl::cb_init, &Impl::cb_running, this);
    api.init_sync(&Impl::cb_source, &Impl::cb_source, nullptr, &ident, this);

    std::unique_lock<std::mutex> lock(mutex);
    while (!quit) {
        cv.wait_for(lock, std::chrono::milliseconds(5), [&] { return quit || load_pending || stop_pending; });
        if (quit) {
            break;
        }
        if (!altered.empty() && !load_pending && !stop_pending) {
            alter_run(lock);
            continue;
        }
        // A run that ended by itself: at its stop time, or before it on an error.
        bool finished = started &&
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count() > 0.2 &&
                        !api.running();
        if (!load_pending && !stop_pending && !finished) {
            continue;
        }
        const double reached = run_time;
        if (started) {
            halt_run(lock);
        }
        if (dead) {
            load_pending = false;
            stop_pending = false;
            state = State::Failed;
            continue;
        }
        if (stop_pending) {
            stop_pending = false;
            state = State::Idle;
            if (!load_pending) {
                continue;
            }
        }
        bool carry = true;
        if (load_pending) {
            lines = std::move(pending_lines);
            pending_lines.clear();
            carry = pending_carry;
            load_pending = false;
            error.clear();
        } else if (reached < run_stop * 0.999) {
            state = State::Failed;
            if (error.empty()) {
                error = "the simulation stopped";
            }
            continue;
        }
        if (lines.empty()) {
            state = State::Idle;
            continue;
        }
        start_run(lock, carry);
    }
    if (started) {
        halt_run(lock);
    }
}

Ngspice::Ngspice() : impl_(std::make_unique<Impl>()) {}

Ngspice::~Ngspice()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->quit = true;
    }
    impl_->cv.notify_all();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

Ngspice &Ngspice::instance()
{
    static Ngspice session;
    return session;
}

bool Ngspice::available()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->tried) {
        impl_->tried = true;
        impl_->loaded = impl_->load_library();
        if (impl_->loaded) {
            impl_->state = State::Idle;
            Impl *impl = impl_.get();
            impl_->thread = std::thread([impl] { impl->main(); });
        }
    }
    return impl_->loaded;
}

Ngspice::State Ngspice::state() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}

std::string Ngspice::status() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

void Ngspice::load(std::vector<std::string> lines, bool carry)
{
    if (!available()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending_lines = std::move(lines);
        impl_->pending_carry = carry;
        impl_->load_pending = true;
        impl_->resistances.clear();
        impl_->altered.clear();
    }
    impl_->cv.notify_all();
}

void Ngspice::stop()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->loaded) {
            return;
        }
        impl_->load_pending = false;
        impl_->stop_pending = true;
        // A circuit that is stopped forgets: the gates start again, and the
        // delay lines are empty.
        impl_->digital.reset();
    }
    impl_->cv.notify_all();
}

void Ngspice::set_constant(const std::string &source, double volts)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Source &s = impl_->source(source);
    s.wave = false;
    s.stream = false;
    s.volts = volts;
}

void Ngspice::set_resistance(const std::string &resistor, double ohms)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->digital.set_resistance(resistor, ohms)) {
        return;   // of the island: not in the netlist
    }
    for (Resistance &r : impl_->resistances) {
        if (r.name != resistor) {
            continue;
        }
        if (std::fabs(r.ohms - ohms) <= 1e-9 * r.ohms) {
            return;
        }
        r.ohms = ohms;
        for (Resistance &a : impl_->altered) {
            if (a.name == resistor) {
                a.ohms = ohms;
                return;
            }
        }
        impl_->altered.push_back(r);
        return;
    }
    impl_->resistances.push_back(Resistance{resistor, ohms});
}

void Ngspice::push_stream(const std::string &source, const float *samples, size_t count, double rate)
{
    if (!samples || count == 0 || rate <= 0.0) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Source &s = impl_->source(source);
    s.wave = false;
    s.stream = true;
    s.rate = rate;
    s.samples.insert(s.samples.end(), samples, samples + count);
    const auto keep = static_cast<size_t>(rate);
    if (s.samples.size() > 2 * keep) {
        s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(s.samples.size() - keep));
    }
    // The newest sample is `stream_delay` ahead of the simulation; when the
    // two clocks drift apart the stream starts again from there.
    const double now = impl_->base + impl_->run_time;
    const double span = static_cast<double>(count) / rate;
    if (!s.started || s.end - now < 0.005 || s.end - now > 0.3) {
        s.end = now + span + stream_delay;
        s.started = true;
    } else {
        s.end += span;
    }
}

void Ngspice::set_digital(const core::Digital &digital)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->digital.set(digital);
}

void Ngspice::set_wave(const std::string &source, const core::WaveSpec &spec, bool on)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Source &s = impl_->source(source);
    s.wave = true;
    s.stream = false;
    s.spec = spec;
    s.on = on;
}

void Ngspice::set_watches(const std::vector<std::string> &vectors)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->watches = vectors;
    impl_->samples.assign(vectors.size(), {});
    impl_->map_watches();
}

size_t Ngspice::drain(std::vector<std::vector<float>> &out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.resize(impl_->samples.size());
    for (size_t w = 0; w < out.size(); w++) {
        out[w].clear();
        out[w].swap(impl_->samples[w]);
    }
    return out.empty() ? 0 : out[0].size();
}

void Ngspice::snapshot(std::vector<std::pair<std::string, double>> &out) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.clear();
    for (size_t i = 0; i < impl_->names.size() && i < impl_->latest.size(); i++) {
        out.emplace_back(impl_->names[i], impl_->latest[i]);
    }
}

double Ngspice::time() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->base + impl_->run_time;
}

int64_t Ngspice::steps() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->steps;
}

int64_t Ngspice::clocks() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->digital.clocks();
}

void Ngspice::set_target(double seconds)
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->target = seconds;
    }
    impl_->cv.notify_all();
}

}  // namespace sim
