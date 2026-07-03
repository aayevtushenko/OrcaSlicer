#include "CustomNozzle.hpp"

#include "Config.hpp"

#include <array>
#include <cmath>

namespace Slic3r::CustomNozzle {

bool is_supported_bambu_nozzle_diameter(double diameter)
{
    constexpr std::array<double, 4> supported_diameters { 0.2, 0.4, 0.6, 0.8 };
    constexpr double tolerance = 1e-6;

    for (double supported : supported_diameters)
        if (std::abs(diameter - supported) <= tolerance)
            return true;
    return false;
}

bool bambu_nozzle_diameter_override_enabled(const ConfigOptionResolver &config, size_t extruder_id)
{
    const auto *override_enabled = config.option<ConfigOptionBools>("bambu_nozzle_diameter_override");
    if (override_enabled == nullptr || extruder_id >= override_enabled->values.size() || !override_enabled->values[extruder_id])
        return false;

    const auto *reported_diameters = config.option<ConfigOptionFloats>("bambu_nozzle_diameter");
    return reported_diameters != nullptr && extruder_id < reported_diameters->values.size() &&
           is_supported_bambu_nozzle_diameter(reported_diameters->values[extruder_id]);
}

double resolved_bambu_nozzle_diameter(const ConfigOptionResolver &config, size_t extruder_id)
{
    const auto *physical_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
    if (physical_diameters == nullptr || physical_diameters->values.empty())
        throw UnknownOptionException("nozzle_diameter");

    if (!bambu_nozzle_diameter_override_enabled(config, extruder_id))
        return physical_diameters->get_at(extruder_id);

    return config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values[extruder_id];
}

BambuNozzleCompatibility evaluate_bambu_nozzle_compatibility(
    const ConfigOptionResolver &config, size_t extruder_id, double device_diameter)
{
    constexpr double device_protocol_tolerance = 1e-3;

    const auto *physical_diameters = config.option<ConfigOptionFloats>("nozzle_diameter");
    if (physical_diameters == nullptr || physical_diameters->values.empty())
        throw UnknownOptionException("nozzle_diameter");

    BambuNozzleCompatibility result;
    result.physical_diameter = physical_diameters->get_at(extruder_id);
    result.reported_diameter = resolved_bambu_nozzle_diameter(config, extruder_id);
    result.device_diameter   = device_diameter;
    result.override_active   = bambu_nozzle_diameter_override_enabled(config, extruder_id);
    result.device_unknown    = device_diameter == 0.0;
    result.matches           = result.device_unknown ||
                               std::abs(result.reported_diameter - device_diameter) <= device_protocol_tolerance;
    return result;
}

} // namespace Slic3r::CustomNozzle
