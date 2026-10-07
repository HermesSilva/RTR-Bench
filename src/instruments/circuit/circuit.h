// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the circuit bench: a schematic that is alive. Parts are
// placed and wired on a grid while the simulator (ngspice) runs the circuit
// in step with the bench clock; any point of the schematic takes a cable
// from an instrument: an input reads the voltage of the node, an output
// (generator, supply) drives it. Potentiometers turn and switches move
// while the circuit runs.
#pragma once

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

#include "app/instrument.h"
#include "core/circuit.h"

namespace app {

class App;

class CircuitBench : public InstrumentBase {
public:
    explicit CircuitBench(App &app);
    ~CircuitBench() override;

    Instrument kind() const override { return Instrument::Circuit; }
    int channel_count() const override { return 0; }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override { (void)events; }
    void produce(int64_t now_ns, std::vector<core::DigitalEvent> &events, std::vector<core::AnalogBlock> &blocks) override;
    bool port_current(int port, float &amps) const override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    static constexpr double supply_ohms = 0.05;      // output resistance of a supply output
    static constexpr double generator_ohms = 50.0;   // and of a generator output
    static constexpr double stream_ohms = 600.0;     // and of an audio input of the computer (a line output)
    static constexpr float logic_threshold = 1.8f;   // the logic level of a node, for digital inputs

private:
    // Editing.
    void changed();   // the schematic changed: nets now, the simulator at the next frame
    void add_wire(core::GridPoint a, core::GridPoint b);
    void move_pins(const std::vector<core::GridPoint> &from, const std::vector<core::GridPoint> &to, int moving_id);
    void move_part(size_t index, int dx, int dy);
    // The block: parts and wires selected together with a rectangle.
    bool in_block(int id) const;
    void clear_block();
    void select_block(float x0, float y0, float x1, float y1);
    void move_block(int dx, int dy);
    void remove_block();
    void rotate_part(size_t index);
    void remove_part(size_t index);
    int part_index(int id) const;
    void plug_cable(core::GridPoint at);
    void drop_unused_taps();
    void toggle_or_edit(size_t index, bool edit);
    void undo();
    void redo();
    void duplicate_part(size_t index);
    bool export_netlist();

    // Projects: a circuit with its cables, one file each in the projects
    // folder (`.RT-Lab` next to the executable).
    void clear_circuit();
    bool open_project(const std::string &name);
    bool save_project(const std::string &name);
    void draw_project_popups(float s);

    // Simulation.
    void rebuild();
    void set_watches();
    void update_sources();
    bool vector_value(const std::string &name, double &value) const;
    double net_volts(int net) const;
    std::vector<core::Drive> wired_drives() const;
    std::vector<core::Shunt> wired_shunts() const;
    std::vector<core::Load> wired_loads() const;

    // Drawing.
    void draw_toolbar(ui::Window &window, ImVec2 min, ImVec2 max);
    void draw_canvas(ui::Window &window, ImVec2 min, ImVec2 max);
    void draw_value_editor(float s);
    void draw_add_dialog(float s);
    void draw_properties(ui::Window &window, ImVec2 min, ImVec2 max);
    void resize_grip(ui::Window &window, ImVec2 corner);

    App &app_;
    core::Circuit circuit_;
    core::NetMap nets_;
    bool dirty_ = true;            // the simulator has an older circuit
    bool watches_dirty_ = true;

    // The simulator side.
    std::vector<core::Drive> drives_;
    std::vector<core::Shunt> shunts_;   // the multimeter tips that measure current
    std::vector<core::Load> loads_;     // what the inputs of the bench put on their taps
    std::vector<std::pair<std::string, double>> vectors_;            // latest value of every vector
    std::vector<std::pair<core::GridPoint, std::string>> point_net_; // where the nets of the running circuit are
    struct Watch {
        int slot;
        bool current;   // the current of the drive on the tap, not its voltage
    };
    std::vector<Watch> watches_;
    std::vector<std::vector<float>> samples_;
    std::vector<std::pair<int, float>> drive_amps_;   // by slot
    std::vector<std::pair<int, int>> tap_level_;      // by slot
    std::vector<float> stream_;                       // samples of an audio input on their way to the simulator
    bool synced_ = false;
    int64_t sync_bench_ns_ = 0;
    double sync_sim_ = 0.0;
    int64_t stream_next_ns_ = 0;
    double speed_ = 0.0;
    double speed_sim_ = 0.0;
    int64_t speed_bench_ns_ = 0;

    // The view and the tool in hand.
    float zoom_ = 1.0f;
    ImVec2 pan_ = ImVec2(240.0f, 160.0f);
    bool placing_ = false;
    const core::CatalogEntry *place_entry_ = nullptr;   // what ADD put in hand

    // The ADD dialog: the catalog by category, with a search.
    bool add_ask_ = false;
    char add_search_[48] = "";
    std::string add_category_;   // empty: every category

    // The properties panel of the selected part.
    int prop_id_ = -1;           // the part the value field was filled for
    char prop_value_[32] = "";
    char prop_value2_[32] = "";

    // Undo and redo: the circuit as it was before each change.
    std::vector<core::Circuit> undo_;
    std::vector<core::Circuit> redo_;
    core::Circuit last_;
    bool restoring_ = false;

    bool paused_ = false;        // the RUN key: the simulation holds its time
    bool fit_ask_ = false;       // the FIT key: the whole schematic in the window
    bool project_bad_ = true;    // the message on the status line is an error
    int menu_wire_ = -1;         // the wire the context menu is about
    int place_rotation_ = 0;
    bool wiring_ = false;
    core::GridPoint wire_from_{0, 0};
    int selected_id_ = -1;
    int selected_wire_ = -1;     // the wire that was clicked: Del removes it
    int drag_id_ = -1;
    bool drag_moved_ = false;
    core::GridPoint drag_grid_{0, 0};
    bool panning_ = false;
    std::vector<int> block_;            // ids of the parts selected together
    std::vector<char> block_wires_;     // per wire: selected with them
    bool banding_ = false;              // the selection rectangle is being drawn
    float band_x_ = 0.0f;               // where it started, in grid units
    float band_y_ = 0.0f;
    bool block_drag_ = false;
    bool block_moved_ = false;
    // The circuit as it was when a drag started: every position of the
    // mouse moves from there, not from the previous position.
    core::Circuit drag_circuit_;
    std::vector<char> drag_wires_;
    int edit_id_ = -1;
    bool edit_open_ = false;
    char edit_text_[32] = "";
    std::string hover_text_;
    std::string project_;                     // the open project, empty for a circuit never saved
    std::vector<std::string> project_files_;  // the list OPEN shows
    bool open_ask_ = false;
    bool save_ask_ = false;
    char save_text_[48] = "";
    std::string project_error_;

    // The window: resizable, its size kept with the circuit.
    int want_w_ = 0;
    int want_h_ = 0;
    int last_w_ = 0;
    int last_h_ = 0;
    int grip_w_ = 0;
    int grip_h_ = 0;
    int grip_x_ = 0;
    int grip_y_ = 0;
};

}  // namespace app
