// SPDX-License-Identifier: Apache-2.0
#include "audio/audio_ports.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include <miniaudio.h>

#include "app/app.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/wire.h"
#include "ui/window.h"

namespace app {

namespace {

constexpr unsigned sample_rate = 48000;
constexpr int max_channels = 2;                         // left and right
constexpr size_t capture_keep = sample_rate;            // one second, when nobody takes it
constexpr size_t playback_high = sample_rate / 4;       // more than this waiting: the oldest goes
constexpr size_t playback_target = sample_rate * 8 / 100;
constexpr int64_t sample_ns = 1000000000LL / sample_rate;   // of the blocks the instruments get
constexpr float lamp_on = 0.01f;     // a jack has signal from -40 dB of full scale
constexpr float lamp_clip = 0.9f;    // and is near full scale from here
constexpr float lamp_decay = 0.9f;   // of the level a lamp shows, per frame

// The geometry of the groups on the rack, in logical pixels.
constexpr float group_h = 48.0f;
constexpr float group_row = 62.0f;
constexpr float group_gap = 10.0f;
constexpr float group_min_w = 150.0f;
constexpr float jack_pitch = 46.0f;

float group_width(int channels)
{
    return std::max(group_min_w, static_cast<float>(channels) * jack_pitch + 24.0f);
}

}  // namespace

struct AudioPorts::Impl {
    // One side of a device: left or right.
    struct Side {
        std::vector<float> ring;   // capture: heard, not yet taken; playback: to be played
        float peak = 0.0f;         // of the last samples, for the lamp of the jack
        int port = -1;             // what the jack is wired to
        bool patched = false;      // a source with a cable to an output of the rack
        std::vector<float> patch_ring;   // what it heard, on its way to those outputs
        int64_t next_ns = 0;       // a source the instruments listen to: the time of its next sample
        bool streaming = false;
        // Playback: the resampler from the rate of the port to that of the device.
        double position = 0.0;
        float last = 0.0f;
    };
    struct Device {
        std::string name;
        ma_device_id id;
        bool capture = false;    // a source of signal: an input, or what a playback device is playing
        bool loopback = false;   // the second kind of source
        int system_index = -1;   // of a playback device and of its loopback: the same number
        bool blocked = false;    // an output whose own loopback is in use: it would feed back
        int channels = 2;
        bool open = false;
        bool failed = false;
        ma_device device;
        std::mutex mutex;
        Side side[max_channels];
        int first_channel = 0;   // the bench channel of its left side
    };
    struct Jack {
        size_t device;
        int side;
    };

    ma_context context;
    bool ready = false;
    std::vector<std::unique_ptr<Device>> devices;   // the inputs, then the outputs
    std::vector<Jack> jacks;                        // by bench channel
    std::vector<std::pair<int, int>> patches;       // source channel, output channel
    std::vector<float> moving;                      // samples on their way across a patch
    std::vector<core::PortInfo> infos;              // by bench channel: the sources as ports

    static void callback(ma_device *device, void *output, const void *input, ma_uint32 frames)
    {
        auto *d = static_cast<Device *>(device->pUserData);
        const auto channels = static_cast<size_t>(d->channels);
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->capture) {
            const auto *in = static_cast<const float *>(input);
            if (!in) {
                return;
            }
            for (size_t c = 0; c < channels; c++) {
                Side &side = d->side[c];
                std::vector<float> &ring = side.ring;
                for (ma_uint32 i = 0; i < frames; i++) {
                    float v = in[static_cast<size_t>(i) * channels + c];
                    side.peak = std::max(side.peak, std::fabs(v));
                    if (side.port >= 0) {
                        ring.push_back(v);   // only a wired jack has someone to take them
                    }
                    if (side.patched) {
                        side.patch_ring.push_back(v);
                    }
                }
                if (side.patch_ring.size() > capture_keep) {
                    side.patch_ring.erase(side.patch_ring.begin(),
                                          side.patch_ring.begin() + static_cast<std::ptrdiff_t>(side.patch_ring.size() - capture_keep / 2));
                }
                if (ring.size() > capture_keep) {
                    ring.erase(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(ring.size() - capture_keep / 2));
                }
            }
            return;
        }
        auto *out = static_cast<float *>(output);
        if (!out) {
            return;
        }
        for (size_t c = 0; c < channels; c++) {
            std::vector<float> &ring = d->side[c].ring;
            size_t have = std::min(static_cast<size_t>(frames), ring.size());
            for (ma_uint32 i = 0; i < frames; i++) {
                out[static_cast<size_t>(i) * channels + c] = i < have ? ring[i] : 0.0f;
            }
            ring.erase(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(have));
        }
    }

    bool open(Device &d)
    {
        ma_device_type type = d.loopback ? ma_device_type_loopback : (d.capture ? ma_device_type_capture : ma_device_type_playback);
        ma_device_config config = ma_device_config_init(type);
        if (d.capture) {
            config.capture.pDeviceID = &d.id;
            config.capture.format = ma_format_f32;
            config.capture.channels = static_cast<ma_uint32>(d.channels);
        } else {
            config.playback.pDeviceID = &d.id;
            config.playback.format = ma_format_f32;
            config.playback.channels = static_cast<ma_uint32>(d.channels);
        }
        config.sampleRate = sample_rate;
        config.dataCallback = &Impl::callback;
        config.pUserData = &d;
        if (ma_device_init(&context, &config, &d.device) != MA_SUCCESS) {
            return false;
        }
        if (ma_device_start(&d.device) != MA_SUCCESS) {
            ma_device_uninit(&d.device);
            return false;
        }
        d.open = true;
        return true;
    }

    static void close(Device &d)
    {
        if (d.open) {
            ma_device_uninit(&d.device);
            d.open = false;
        }
        std::lock_guard<std::mutex> lock(d.mutex);
        for (Side &side : d.side) {
            side.ring.clear();
            side.peak = 0.0f;
            side.position = 0.0;
            side.last = 0.0f;
        }
    }
};

AudioPorts::AudioPorts(App &app) : app_(app), impl_(std::make_unique<Impl>())
{
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        std::fprintf(stderr, "rtr-bench: no audio on this computer\n");
        return;
    }
    impl_->ready = true;
    ma_device_info *playback = nullptr;
    ma_device_info *capture = nullptr;
    ma_uint32 playback_count = 0;
    ma_uint32 capture_count = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &playback_count, &capture, &capture_count) != MA_SUCCESS) {
        return;
    }
    // The default device of each kind first, as the system has it.
    auto add = [&](const ma_device_info *list, ma_uint32 count, bool is_capture, bool is_loopback, int limit) {
        for (int pass = 0; pass < 2; pass++) {
            for (ma_uint32 i = 0; i < count; i++) {
                bool is_default = list[i].isDefault != 0;
                int have = 0;
                for (const auto &d : impl_->devices) {
                    have += d->capture == is_capture && d->loopback == is_loopback ? 1 : 0;
                }
                if (is_default != (pass == 0) || have >= limit) {
                    continue;
                }
                auto device = std::make_unique<Impl::Device>();
                device->name = list[i].name;
                device->id = list[i].id;
                device->capture = is_capture;
                device->loopback = is_loopback;
                device->system_index = is_capture && !is_loopback ? -1 : static_cast<int>(i);
                // One jack per channel of the device, two at most.
                ma_device_info info;
                if (ma_context_get_device_info(&impl_->context,
                                               is_capture && !is_loopback ? ma_device_type_capture : ma_device_type_playback,
                                               &list[i].id, &info) == MA_SUCCESS &&
                    info.nativeDataFormatCount > 0 && info.nativeDataFormats[0].channels == 1) {
                    device->channels = 1;
                }
                device->first_channel = static_cast<int>(impl_->jacks.size());
                for (int side = 0; side < device->channels; side++) {
                    impl_->jacks.push_back(Impl::Jack{impl_->devices.size(), side});
                }
                impl_->devices.push_back(std::move(device));
            }
        }
    };
    add(capture, capture_count, true, false, max_inputs);
#ifdef _WIN32
    // What each playback device is playing, as a source (WASAPI loopback).
    add(playback, playback_count, true, true, max_outputs);
#endif
    add(playback, playback_count, false, false, max_outputs);
    impl_->infos.resize(impl_->jacks.size());
    for (size_t c = 0; c < impl_->jacks.size(); c++) {
        core::PortInfo &info = impl_->infos[c];
        info.index = App::audio_port_base + static_cast<int>(c);
        info.name = AudioPorts::channel_name(static_cast<int>(c));
        info.digital = false;
        info.analog = true;
        info.drivable = false;
    }
    // The sources are listened to from the start, for their lamps.
    for (auto &d : impl_->devices) {
        if (d->capture) {
            d->failed = !impl_->open(*d);
        }
    }
}

AudioPorts::~AudioPorts()
{
    for (auto &d : impl_->devices) {
        Impl::close(*d);
    }
    if (impl_->ready) {
        ma_context_uninit(&impl_->context);
    }
}

int AudioPorts::channel_count() const
{
    return static_cast<int>(impl_->jacks.size());
}

int AudioPorts::channel_colour_index(int channel) const
{
    return channel % ui::channel_count;
}

// "IN 1 L", "OUT 2 R": the device by its number among those of its kind.
std::string AudioPorts::channel_name(int channel) const
{
    if (channel < 0 || static_cast<size_t>(channel) >= impl_->jacks.size()) {
        return "AUDIO";
    }
    const Impl::Jack &jack = impl_->jacks[static_cast<size_t>(channel)];
    const Impl::Device &d = *impl_->devices[jack.device];
    int number = 1;
    for (size_t k = 0; k < jack.device; k++) {
        number += impl_->devices[k]->capture == d.capture && impl_->devices[k]->loopback == d.loopback ? 1 : 0;
    }
    std::string name = (d.loopback ? "PLAYING " : (d.capture ? "IN " : "OUT ")) + std::to_string(number);
    if (d.channels > 1) {
        name += jack.side == 0 ? " L" : " R";
    }
    return name;
}

const core::PortInfo *AudioPorts::source_info(int channel) const
{
    return channel_drives(channel) ? &impl_->infos[static_cast<size_t>(channel)] : nullptr;
}

bool AudioPorts::channel_drives(int channel) const
{
    if (channel < 0 || static_cast<size_t>(channel) >= impl_->jacks.size()) {
        return false;
    }
    return impl_->devices[impl_->jacks[static_cast<size_t>(channel)].device]->capture;
}

int AudioPorts::patch_source_of(int sink) const
{
    for (const auto &entry : impl_->patches) {
        if (entry.second == sink) {
            return entry.first;
        }
    }
    return -1;
}

void AudioPorts::unpatch_sink(int sink)
{
    auto &patches = impl_->patches;
    patches.erase(std::remove_if(patches.begin(), patches.end(), [sink](const std::pair<int, int> &e) { return e.second == sink; }),
                  patches.end());
    wiring_changed();
}

// A cable from a source to an output. Refused when the output is the very
// device the source taps: the sound would go round and round.
bool AudioPorts::patch(int source, int sink)
{
    const auto count = static_cast<int>(impl_->jacks.size());
    if (source < 0 || sink < 0 || source >= count || sink >= count || !channel_drives(source) || channel_drives(sink)) {
        return false;
    }
    const Impl::Device &from = *impl_->devices[impl_->jacks[static_cast<size_t>(source)].device];
    const Impl::Device &to = *impl_->devices[impl_->jacks[static_cast<size_t>(sink)].device];
    if (from.loopback && from.system_index == to.system_index) {
        return false;
    }
    app_.unwire(this->id(), sink);   // an output takes one cable
    auto &patches = impl_->patches;
    patches.erase(std::remove_if(patches.begin(), patches.end(), [sink](const std::pair<int, int> &e) { return e.second == sink; }),
                  patches.end());
    patches.emplace_back(source, sink);
    wiring_changed();
    return true;
}

void AudioPorts::jack_clicked(int channel)
{
    if (channel < 0 || static_cast<size_t>(channel) >= impl_->jacks.size()) {
        return;
    }
    const bool source = channel_drives(channel);
    const int port = App::audio_port_base + channel;
    InstrumentId held{Instrument::Scope, 0};
    int held_channel = 0;
    const bool in_hand = app_.offered(held, held_channel);
    if (in_hand && held == this->id()) {
        // A cable that hangs from another jack of the rack: a source and
        // an output make a patch; anything else is not pertinent.
        if (held_channel != channel && channel_drives(held_channel) != source) {
            patch(source ? channel : held_channel, source ? held_channel : channel);
        }
        app_.cancel_wiring();
        return;
    }
    if (in_hand && source) {
        app_.select_port(port);   // the cable of an instrument: its input listens here, when pertinent
        return;
    }
    if (in_hand || app_.selected_port() >= 0) {
        app_.offer_channel(this->id(), channel);   // a cable from an instrument output, the circuit or a port
        return;
    }
    // No cable in hand: what is plugged here comes out and hangs from its
    // other end.
    if (!source && patch_source_of(channel) >= 0) {
        int from = patch_source_of(channel);
        unpatch_sink(channel);
        app_.hold_cable(this->id(), from);
        return;
    }
    if (source) {
        for (size_t k = impl_->patches.size(); k-- > 0;) {
            if (impl_->patches[k].first == channel) {
                int sink = impl_->patches[k].second;
                unpatch_sink(sink);
                app_.hold_cable(this->id(), sink);
                return;
            }
        }
        InstrumentId listener{Instrument::Scope, 0};
        int listener_channel = 0;
        if (app_.port_wired_to(port, listener, listener_channel)) {
            app_.select_port(port);   // the input of an instrument was here: its cable is in hand
            return;
        }
    }
    app_.offer_channel(this->id(), channel);   // a cable to the circuit comes out; else a new cable starts
}

// Every frame: what the patched sources heard goes to their outputs. Both
// run at the same rate; their clocks differ, which the output absorbs.
void AudioPorts::produce(int64_t now_ns, std::vector<core::DigitalEvent> &events, std::vector<core::AnalogBlock> &blocks)
{
    (void)events;
    for (size_t channel = 0; channel < impl_->jacks.size(); channel++) {
        const Impl::Jack &jack = impl_->jacks[channel];
        Impl::Device &from = *impl_->devices[jack.device];
        if (!from.capture) {
            continue;
        }
        // Who listens to this source: outputs of the rack (patches) and
        // inputs of instruments (wires to its port).
        const int port = App::audio_port_base + static_cast<int>(channel);
        InstrumentId listener{Instrument::Scope, 0};
        int listener_channel = 0;
        const bool tapped = app_.port_wired_to(port, listener, listener_channel);
        bool patched = false;
        for (const auto &entry : impl_->patches) {
            patched = patched || entry.first == static_cast<int>(channel);
        }
        Impl::Side &source = from.side[jack.side];
        {
            std::lock_guard<std::mutex> lock(from.mutex);
            source.patched = tapped || patched;
            impl_->moving.clear();
            impl_->moving.swap(source.patch_ring);
        }
        if (!tapped) {
            source.streaming = false;
        }
        if (impl_->moving.empty()) {
            continue;
        }
        if (tapped) {
            // As a block on the time line of the bench, each one going on
            // from the last; started again when the two clocks drift apart.
            const auto count = static_cast<int64_t>(impl_->moving.size());
            int64_t drift = now_ns - (source.next_ns + count * sample_ns);
            if (!source.streaming || drift > 300000000LL || drift < -300000000LL) {
                source.next_ns = now_ns - count * sample_ns;
                source.streaming = true;
            }
            core::AnalogBlock block;
            block.t0_ns = source.next_ns;
            block.dt_ns = sample_ns;
            block.port = static_cast<uint16_t>(port & 0xFFFF);
            block.volts = impl_->moving;
            blocks.push_back(std::move(block));
            source.next_ns += count * sample_ns;
        }
        for (const auto &entry : impl_->patches) {
            if (entry.first != static_cast<int>(channel)) {
                continue;
            }
            const Impl::Jack &out = impl_->jacks[static_cast<size_t>(entry.second)];
            Impl::Device &to = *impl_->devices[out.device];
            if (!to.open) {
                continue;
            }
            std::lock_guard<std::mutex> lock(to.mutex);
            Impl::Side &side = to.side[out.side];
            for (float v : impl_->moving) {
                side.peak = std::max(side.peak, std::fabs(v));
                side.ring.push_back(std::clamp(v, -1.0f, 1.0f));
            }
            if (side.ring.size() > playback_high) {
                side.ring.erase(side.ring.begin(), side.ring.begin() + static_cast<std::ptrdiff_t>(side.ring.size() - playback_target));
            }
        }
    }
}

void AudioPorts::save(nlohmann::json &out) const
{
    out["patches"] = nlohmann::json::array();
    for (const auto &entry : impl_->patches) {
        out["patches"].push_back({entry.first, entry.second});
    }
}

void AudioPorts::load(const nlohmann::json &in)
{
    if (!in.contains("patches") || !in["patches"].is_array()) {
        return;
    }
    for (const nlohmann::json &j : in["patches"]) {
        if (j.is_array() && j.size() == 2 && j[0].is_number_integer() && j[1].is_number_integer()) {
            patch(j[0].get<int>(), j[1].get<int>());
        }
    }
}

// A device is open while one of its jacks has a cable.
void AudioPorts::wiring_changed()
{
    // An output that got a cable from the bench gives up its patch.
    auto &patches = impl_->patches;
    patches.erase(std::remove_if(patches.begin(), patches.end(),
                                 [this](const std::pair<int, int> &e) { return app_.wired_port(this->id(), e.second) >= 0; }),
                  patches.end());
    // The playback devices whose sound is being taken: they must not play
    // what comes back from the bench, or the sound goes round and round.
    std::vector<int> taken;
    for (auto &dp : impl_->devices) {
        Impl::Device &d = *dp;
        for (int side = 0; d.loopback && side < d.channels; side++) {
            bool patched = false;
            for (const auto &entry : patches) {
                patched = patched || entry.first == d.first_channel + side;
            }
            if (patched || app_.wired_port(this->id(), d.first_channel + side) >= 0) {
                taken.push_back(d.system_index);
            }
        }
    }
    for (auto &dp : impl_->devices) {
        Impl::Device &d = *dp;
        bool wired = false;
        {
            std::lock_guard<std::mutex> lock(d.mutex);
            for (int side = 0; side < d.channels; side++) {
                const int channel = d.first_channel + side;
                bool patched = false;
                for (const auto &entry : patches) {
                    patched = patched || entry.first == channel || entry.second == channel;
                }
                d.side[side].port = app_.wired_port(this->id(), channel);
                d.side[side].patched = patched && d.capture;
                wired = wired || d.side[side].port >= 0 || patched;
                if (d.side[side].port < 0 && !patched) {
                    d.side[side].ring.clear();
                }
                if (!d.side[side].patched) {
                    d.side[side].patch_ring.clear();
                }
            }
        }
        if (d.capture) {
            continue;   // a source stays open, for its lamp
        }
        d.blocked = std::find(taken.begin(), taken.end(), d.system_index) != taken.end();
        if (d.blocked) {
            Impl::close(d);
            continue;
        }
        if (wired && !d.open && !d.failed) {
            d.failed = !impl_->open(d);
            if (d.failed) {
                std::fprintf(stderr, "rtr-bench: cannot open the audio device %s\n", d.name.c_str());
            }
        } else if (!wired) {
            Impl::close(d);
            d.failed = false;
        }
    }
}

// What a side of an input heard since the last call, at the rate of the device.
size_t AudioPorts::drain_stream(int channel, std::vector<float> &out, double &rate)
{
    out.clear();
    rate = sample_rate;
    if (!channel_drives(channel)) {
        return 0;
    }
    const Impl::Jack &jack = impl_->jacks[static_cast<size_t>(channel)];
    Impl::Device &d = *impl_->devices[jack.device];
    Impl::Side &side = d.side[jack.side];
    std::lock_guard<std::mutex> lock(d.mutex);
    out.swap(side.ring);
    return out.size();
}

// The outputs play what arrives at the port they are wired to: 1 V is full
// scale, resampled from the rate of the port to that of the device.
void AudioPorts::feed_analog(const std::vector<core::AnalogBlock> &blocks)
{
    for (auto &dp : impl_->devices) {
        Impl::Device &d = *dp;
        if (d.capture || !d.open) {
            continue;
        }
        for (int s = 0; s < d.channels; s++) {
            Impl::Side &side = d.side[s];
            if (side.port < 0) {
                continue;
            }
            for (const core::AnalogBlock &b : blocks) {
                if (b.port != static_cast<uint16_t>(side.port & 0xFFFF) || b.volts.empty() || b.dt_ns <= 0) {
                    continue;
                }
                // Input samples per output sample; the sample before the block is index -1.
                const double step = 1e9 / (static_cast<double>(sample_rate) * static_cast<double>(b.dt_ns));
                const auto count = static_cast<double>(b.volts.size());
                std::lock_guard<std::mutex> lock(d.mutex);
                float peak = 0.0f;
                for (; side.position <= count - 1.0; side.position += step) {
                    double base = std::floor(side.position);
                    auto index = static_cast<std::ptrdiff_t>(base);
                    auto f = static_cast<float>(side.position - base);
                    float v0 = index < 0 ? side.last : b.volts[static_cast<size_t>(index)];
                    float v1 = b.volts[static_cast<size_t>(index + 1)];
                    float v = std::clamp(v0 + (v1 - v0) * f, -1.0f, 1.0f);
                    peak = std::max(peak, std::fabs(v));
                    side.ring.push_back(v);
                }
                side.position -= count;
                side.last = b.volts.back();
                side.peak = std::max(peak, side.peak);
                // The simulation and the device have each its clock: when
                // too much waits to be played, the oldest goes.
                if (side.ring.size() > playback_high) {
                    side.ring.erase(side.ring.begin(),
                                    side.ring.begin() + static_cast<std::ptrdiff_t>(side.ring.size() - playback_target));
                }
            }
        }
    }
}

// The height the groups take in a panel of `width`: they wrap into rows.
float AudioPorts::layout_height(float width, float s) const
{
    int rows = 1;
    float x = 0.0f;
    for (const auto &d : impl_->devices) {
        float w = group_width(d->channels) * s;
        if (x > 0.0f && x + w > width) {
            rows++;
            x = 0.0f;
        }
        x += w + group_gap * s;
    }
    return static_cast<float>(rows) * group_row * s;
}

// One group per device, titled with its kind and name, a jack per channel.
void AudioPorts::draw_jacks(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (impl_->devices.empty()) {
        ui::group_frame(ImVec2(min.x, min.y + 12.0f * s), ImVec2(max.x, min.y + (12.0f + group_h) * s), "AUDIO", s);
        ui::label(ImVec2(min.x + 12.0f * s, min.y + 28.0f * s), "no audio devices", true);
        return;
    }
    const float radius = 9.0f * s;
    float x = min.x;
    float y = min.y + 12.0f * s;
    std::vector<ImVec2> centres(impl_->jacks.size(), ImVec2(0.0f, 0.0f));
    for (auto &dp : impl_->devices) {
        Impl::Device &d = *dp;
        const float w = group_width(d.channels) * s;
        if (x > min.x && x + w > max.x) {
            x = min.x;
            y += group_row * s;
        }
        // The title: the kind and as much of the name as the group holds.
        const std::string kind = d.loopback ? "PLAYING  " : (d.capture ? "IN  " : "OUT  ");
        std::string title = kind + d.name;
        while (title.size() > kind.size() + 2 && ImGui::CalcTextSize(title.c_str()).x > w - 28.0f * s) {
            title.pop_back();
        }
        if (title.size() < kind.size() + d.name.size()) {
            title += "..";
        }
        ImVec2 group_min(x, y);
        ImVec2 group_max(x + w, y + group_h * s);
        ui::group_frame(group_min, group_max, title.c_str(), s);
        // The whole name when the mouse is on the group.
        if (ImGui::IsMouseHoveringRect(group_min, group_max) && ImGui::IsWindowHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", d.name.c_str());
            ImGui::Text("%s", d.loopback  ? "what the computer plays on this device: a source of signal for the circuit"
                              : d.capture ? "input: a source of signal for the circuit"
                                          : "output: plays the node it is wired to");
            if (d.failed) {
                ImGui::Text("cannot be opened");
            }
            if (d.blocked) {
                ImGui::Text("silent: its own PLAYING group is in use, it would feed back");
            }
            ImGui::EndTooltip();
        }
        for (int side = 0; side < d.channels; side++) {
            const int channel = d.first_channel + side;
            float peak = 0.0f;
            int port = -1;
            {
                // The lamp shows the highest level since the last frame and
                // lets it fall from there.
                std::lock_guard<std::mutex> lock(d.mutex);
                peak = d.side[side].peak;
                d.side[side].peak *= lamp_decay;
                port = d.side[side].port;
            }
            // The jacks spread across the group.
            float slot = w / static_cast<float>(d.channels);
            ImVec2 centre(x + slot * (static_cast<float>(side) + 0.5f), y + 8.0f * s + radius);
            ui::JackLook look;
            look.name = d.channels > 1 ? (side == 0 ? "L" : "R") : "";
            look.level = -1;      // the signal lamp beside the jack says it
            look.active = false;
            look.input = !d.capture;   // an output of the computer takes signal from the bench
            look.output = d.capture;
            centres[static_cast<size_t>(channel)] = centre;
            // A patch has the colour of its source, at both ends.
            int patch_source = d.capture ? -1 : patch_source_of(channel);
            for (const auto &entry : impl_->patches) {
                if (d.capture && entry.first == channel) {
                    patch_source = channel;
                }
            }
            look.wire_colour = port >= 0 ? app_.wire_colour(this->id(), channel)
                               : (patch_source >= 0 ? app_.wire_colour(this->id(), patch_source) : 0);
            if (look.wire_colour == 0 && d.capture) {
                look.wire_colour = app_.port_wire_colour(App::audio_port_base + channel);   // an instrument listens here
            }
            char id[32];
            std::snprintf(id, sizeof(id), "##audio%d", channel);
            if (ui::jack(id, centre, radius, look, s)) {
                jack_clicked(channel);
            }
            if (app_.channel_offered(this->id(), channel)) {
                draw->AddCircle(centre, radius + 3.0f * s, t.led_warn, 24, 2.0f * s);
            }
            // The signal lamp: green with signal, red near full scale.
            ui::led(ImVec2(centre.x - radius - 9.0f * s, centre.y), 3.5f * s, peak >= lamp_clip ? t.led_stop : t.led_run,
                    peak >= lamp_on, s);
            app_.anchor_channel(this->id(), channel, window, centre.x, centre.y);
        }
        x += w + group_gap * s;
    }
    // The cables from jack to jack, on the rack itself.
    for (const auto &entry : impl_->patches) {
        ui::draw_wire(draw, centres[static_cast<size_t>(entry.first)], centres[static_cast<size_t>(entry.second)],
                      app_.wire_colour(this->id(), entry.first), s);
    }
    // A click near an end of one of them takes that end, as on every cable
    // of the bench.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() &&
        !app_.cable_offered() && app_.selected_port() < 0) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float reach = App::grab_radius * s;
        auto near_jack = [&](int channel) {
            const ImVec2 c = centres[static_cast<size_t>(channel)];
            return (c.x - mouse.x) * (c.x - mouse.x) + (c.y - mouse.y) * (c.y - mouse.y) <= reach * reach;
        };
        for (size_t k = impl_->patches.size(); k-- > 0;) {
            const std::pair<int, int> entry = impl_->patches[k];
            if (near_jack(entry.second)) {
                unpatch_sink(entry.second);
                app_.hold_cable(this->id(), entry.first);
                break;
            }
            if (near_jack(entry.first)) {
                unpatch_sink(entry.second);
                app_.hold_cable(this->id(), entry.second);
                break;
            }
        }
    }
}

}  // namespace app
