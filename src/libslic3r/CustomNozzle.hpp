#ifndef slic3r_CustomNozzle_hpp_
#define slic3r_CustomNozzle_hpp_

#include <cstddef>

namespace Slic3r {

class ConfigOptionResolver;

namespace CustomNozzle {

struct BambuNozzleCompatibility
{
    double physical_diameter { 0.0 };
    double reported_diameter { 0.0 };
    double device_diameter { 0.0 };
    bool   override_active { false };
    bool   device_unknown { false };
    bool   matches { false };
};

// Bambu firmware currently exposes only these nozzle identities. This predicate
// is shared by configuration validation and the transport-only resolver.
bool is_supported_bambu_nozzle_diameter(double diameter);

// Report whether the explicit Bambu compatibility projection is enabled and
// valid for this exact extruder. Missing, misaligned, or invalid data resolves
// to disabled.
bool bambu_nozzle_diameter_override_enabled(const ConfigOptionResolver &config, size_t extruder_id);

// Resolve the nozzle identity to report at Bambu transport boundaries. Invalid
// override data fails closed to the physical diameter. This function must not
// be used for slicing, flow, geometry, or extrusion math.
double resolved_bambu_nozzle_diameter(const ConfigOptionResolver &config, size_t extruder_id);

// Evaluate one exact extruder against the Bambu device identity. Device state
// uses millimetres rounded to protocol precision; zero retains the established
// "unknown means compatible" behavior.
BambuNozzleCompatibility evaluate_bambu_nozzle_compatibility(
    const ConfigOptionResolver &config, size_t extruder_id, double device_diameter);

} // namespace CustomNozzle
} // namespace Slic3r

#endif // slic3r_CustomNozzle_hpp_
