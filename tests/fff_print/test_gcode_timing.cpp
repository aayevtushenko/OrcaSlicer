#include <catch2/catch_all.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_utils.hpp"

#include <fstream>
#include <map>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

// Regression coverage for filament/tool-change time being folded into the first
// pending motion block (an extrusion move) instead of the tool-change move, and
// for that delay being dropped entirely when too few motion blocks precede the
// change. See BambuStudio "seperate flush time from other types" (c54a8333c7)
// and the follow-up "unprocessed addtional time" fix (27ef0b1bef).
namespace {

constexpr size_t NORMAL = static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal);

FullPrintConfig make_config(double load_time, double unload_time, double tool_change_time)
{
    FullPrintConfig config; // default-initialized with the built-in defaults
    config.gcode_flavor.value = gcfMarlinFirmware;
    // Two filaments, both assigned to the same (single) extruder, so a T1 after
    // T0 is a same-extruder filament swap that costs unload + load time.
    config.filament_diameter.values = {1.75, 1.75};
    config.filament_map.values = {1, 1};
    config.machine_load_filament_time.value = load_time;
    config.machine_unload_filament_time.value = unload_time;
    config.machine_tool_change_time.value = tool_change_time;
    return config;
}

void run_processor(GCodeProcessor& proc, const FullPrintConfig& config, const char* gcode)
{
    // reserved_tag() selects between two tag tables based on this shared static, and
    // other tests in the binary mutate it -- pin it so our "; FEATURE:" role tags are
    // parsed deterministically regardless of test execution order.
    GCodeProcessor::s_IsBBLPrinter = true;
    ScopedTemporaryFile temp(".gcode");
    {
        std::ofstream os(temp.string());
        os << gcode;
    }
    proc.apply_config(config);
    // No producer marker in the gcode, so process_file keeps our applied config.
    proc.process_file(temp.string());
}

// Estimated time per extrusion role, grouped exactly the way libvgcode builds the
// feature-type legend: sum MoveVertex.time over EMoveType::Extrude moves keyed by
// extrusion_role (see ViewerImpl.cpp:1017 -- only Extrude moves are counted).
std::map<ExtrusionRole, double> role_times(const GCodeProcessorResult& r)
{
    std::map<ExtrusionRole, double> m;
    for (const auto& mv : r.moves)
        if (mv.type == EMoveType::Extrude)
            m[mv.extrusion_role] += mv.time[NORMAL];
    return m;
}

// Sum of estimated time attributed to tool-change moves.
double sum_tool_change_time(const GCodeProcessorResult& r)
{
    double t = 0.0;
    for (const auto& mv : r.moves)
        if (mv.type == EMoveType::Tool_change)
            t += mv.time[NORMAL];
    return t;
}

// Total filament-change delay, accumulated independently of the timing machinery.
double filament_change_delay(const GCodeProcessorResult& r)
{
    const auto& s = r.print_statistics;
    return s.total_filament_load_time + s.total_filament_unload_time + s.total_tool_change_time;
}

} // namespace

TEST_CASE("Filament-change time is attributed to tool-change moves, not extrusion roles", "[GCodeTiming]")
{
    // Relative extrusion (M83) so every "E5" is a real 5mm extrusion move rather
    // than a zero-delta travel. Two real travels precede T0 so its delay is flushed
    // cleanly. The extrusions after T0 span several roles (Outer wall, Sparse infill,
    // Inner wall); the first pending block at T1 is an "Outer wall" move, so the
    // buggy code folds the T1 delay into that role. The per-role check below verifies
    // EVERY role stays clean, not just one, and catches any role-to-role misattribution.
    const char* gcode =
        "M83\n"
        "; FEATURE: Outer wall\n"
        "G1 X10 Y10 Z0.2 F600\n"
        "G1 X0 Y0 F6000\n"
        "T0\n"
        "; FEATURE: Outer wall\n"
        "G1 X50 Y0 E5 F1800\n"
        "G1 X50 Y50 E5\n"
        "; FEATURE: Sparse infill\n"
        "G1 X0 Y50 E5\n"
        "G1 X0 Y0 E5\n"
        "T1\n"
        "; FEATURE: Inner wall\n"
        "G1 X50 Y0 E5\n"
        "G1 X50 Y50 E5\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);
    const GCodeProcessorResult& r_zero = proc_zero.get_result();

    const double load = 10.0;
    const double unload = 5.0;
    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(load, unload, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);

    // Preconditions: the filament changes were charged, and cost nothing in the
    // zero-time baseline.
    REQUIRE(delay > 0.0);
    REQUIRE_THAT(filament_change_delay(r_zero), WithinAbs(0.0, 1e-9));

    // The delay must not inflate the time of ANY extrusion role. Compare the full
    // per-role breakdown (exactly how the feature-type legend is built) between the
    // zero-delay and delayed runs -- every role must match to within tolerance.
    const auto roles_zero  = role_times(r_zero);
    const auto roles_delay = role_times(r_delay);
    // Guard: the gcode must genuinely exercise multiple distinct roles (Outer wall,
    // Sparse infill, Inner wall), otherwise this check would silently cover only one.
    REQUIRE(roles_zero.size() >= 3);
    REQUIRE(roles_zero.size() == roles_delay.size());
    for (const auto& [role, zero_time] : roles_zero) {
        INFO("extrusion role index = " << static_cast<int>(role));
        REQUIRE(roles_delay.count(role) == 1);
        REQUIRE_THAT(roles_delay.at(role), WithinAbs(zero_time, 1e-2));
    }

    // The delay must instead land on the tool-change moves, so per-move consumers
    // (layer-time view, layer slider) stay consistent.
    REQUIRE_THAT(sum_tool_change_time(r_delay), WithinAbs(delay, 1e-2));

    // Both tool changes occur on layer 1, so the delay must also be reflected in
    // the first-layer time.
    const double first_layer_delta = proc_delay.get_first_layer_time(PrintEstimatedStatistics::ETimeMode::Normal)
                                   - proc_zero.get_first_layer_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(first_layer_delta, WithinAbs(delay, 1e-2));
}

TEST_CASE("Filament-change time is not dropped when few motion blocks precede the change", "[GCodeTiming]")
{
    // Only a single motion block precedes T0, so the buggy code's "fewer than two
    // pending blocks" early-out discards that filament-change delay entirely,
    // making the total print time inconsistent with the reported statistics.
    const char* gcode =
        "; FEATURE: Outer wall\n"
        "G1 X10 Y10 Z0.2 F600\n"
        "T0\n"
        "G1 X50 Y0 E5 F1800\n"
        "G1 X50 Y50 E5\n"
        "T1\n"
        "G1 X0 Y50 E5\n"
        "G1 X0 Y0 E5\n";

    GCodeProcessor proc_zero;
    run_processor(proc_zero, make_config(0.0, 0.0, 0.0), gcode);

    const double load = 10.0;
    const double unload = 5.0;
    GCodeProcessor proc_delay;
    run_processor(proc_delay, make_config(load, unload, 0.0), gcode);
    const GCodeProcessorResult& r_delay = proc_delay.get_result();

    const double delay = filament_change_delay(r_delay);
    REQUIRE(delay > 0.0);

    // Every second of reported filament-change delay must be present in the total
    // estimated print time; none may be silently dropped.
    const double total_delta = proc_delay.get_time(PrintEstimatedStatistics::ETimeMode::Normal)
                             - proc_zero.get_time(PrintEstimatedStatistics::ETimeMode::Normal);
    REQUIRE_THAT(total_delta, WithinAbs(delay, 1e-2));
}
