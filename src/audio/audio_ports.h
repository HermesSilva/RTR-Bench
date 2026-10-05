// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the audio of the computer as ports of the bench: every
// capture device (microphone, line in) and every playback device
// (speakers, headphones) is a group on the rack, with a jack per channel
// (left and right). An input is a source of
// signal: wired into the circuit it drives the node with what the device
// hears. An output is a sink: what the node it is wired to does is played.
// Full scale of the device is 1 V.
//
// Every jack has a signal lamp: green while there is signal, red near full
// scale. For the lamps the sources (inputs and PLAYING) are listened to all
// the time, wired or not; an output is opened while it has a cable.
//
// On Windows every playback device has a third group, PLAYING: what the
// computer is playing on that device (any player, any program), as a source
// of signal. It is how the sound of a player reaches the bench without a
// virtual audio driver. A device does not play what comes from its own
// PLAYING group: that would feed back.
//
// A source is also a port of the bench: the input of any instrument
// (oscilloscope, multimeter) wired to its jack sees what it hears, at 48 kS/s.
//
// Jack to jack: a source wired straight to an output on the rack (take the
// cable at one, click the other) is heard there, without a circuit.
//
// To the application it is an instrument without a window: the rack draws
// its jacks, and its channels are wired like those of any instrument.
#pragma once

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

#include "app/instrument.h"

namespace app {

class App;

class AudioPorts : public InstrumentBase {
public:
    explicit AudioPorts(App &app);
    ~AudioPorts() override;

    Instrument kind() const override { return Instrument::Audio; }
    // One channel per jack: the inputs of the computer first, then its outputs.
    int channel_count() const override;
    int channel_colour_index(int channel) const override;
    std::string channel_name(int channel) const override;
    void draw(ui::Window &window) override { (void)window; }
    void feed(const std::vector<core::DigitalEvent> &events) override { (void)events; }
    void feed_analog(const std::vector<core::AnalogBlock> &blocks) override;
    void wiring_changed() override;
    void produce(int64_t now_ns, std::vector<core::DigitalEvent> &events, std::vector<core::AnalogBlock> &blocks) override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;
    bool channel_drives(int channel) const override;
    size_t drain_stream(int channel, std::vector<float> &out, double &rate) override;

    // A source as a port of the bench (App::audio_port_base + its channel);
    // null for an output.
    const core::PortInfo *source_info(int channel) const;

    // The groups, drawn by the rack inside its panel; they wrap into as
    // many rows as a panel of `width` needs.
    float layout_height(float width, float s) const;
    void draw_jacks(ui::Window &window, ImVec2 min, ImVec2 max);

    static constexpr int max_inputs = 6;
    static constexpr int max_outputs = 6;
    static constexpr double source_ohms = 600.0;   // an audio input into the circuit: a line output

private:
    // A cable from a source jack to an output jack of the rack.
    bool patch(int source, int sink);
    void unpatch_sink(int sink);
    int patch_source_of(int sink) const;   // -1 when the sink has none
    // A click on a jack, or near the end of a cable plugged into it: with a
    // cable in hand it is plugged; without, what is plugged there is taken
    // out and hangs from its other end, to be plugged somewhere else.
    void jack_clicked(int channel);

    struct Impl;
    App &app_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app
