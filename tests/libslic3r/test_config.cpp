#include <catch2/catch_all.hpp>

#include <boost/filesystem.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintConfigConstants.hpp"
#include "libslic3r/CustomNozzle.hpp"
#include "libslic3r/LocalesUtils.hpp"

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

SCENARIO("Bambu nozzle compatibility override configuration", "[Config][CustomNozzle]")
{
    GIVEN("a default full print configuration") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

        THEN("the override is disabled and defaults to a firmware-supported 0.8 mm identity") {
            const auto *enabled = config.option<ConfigOptionBools>("bambu_nozzle_diameter_override");
            const auto *diameter = config.option<ConfigOptionFloats>("bambu_nozzle_diameter");
            REQUIRE(enabled != nullptr);
            REQUIRE(diameter != nullptr);
            REQUIRE_FALSE(enabled->get_at(0));
            REQUIRE(diameter->get_at(0) == Catch::Approx(0.8));
            REQUIRE(CustomNozzle::resolved_bambu_nozzle_diameter(config, 0) == Catch::Approx(0.4));
        }

        WHEN("the override is enabled for a 1.57 mm physical nozzle") {
            config.option<ConfigOptionFloats>("nozzle_diameter")->values = { 1.57 };
            config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { true };
            config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { 0.8 };

            THEN("the helper reports the override and resolves the transport diameter") {
                REQUIRE(CustomNozzle::bambu_nozzle_diameter_override_enabled(config, 0));
                REQUIRE(CustomNozzle::resolved_bambu_nozzle_diameter(config, 0) == Catch::Approx(0.8));
                REQUIRE(config.validate().empty());
            }
        }

        WHEN("the override is enabled with an unsupported diameter") {
            config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { true };
            config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { 0.5 };

            THEN("configuration validation reports it and runtime resolution fails closed to physical") {
                REQUIRE(config.validate().count("bambu_nozzle_diameter") == 1);
                REQUIRE_FALSE(CustomNozzle::bambu_nozzle_diameter_override_enabled(config, 0));
                REQUIRE(CustomNozzle::resolved_bambu_nozzle_diameter(config, 0) == Catch::Approx(0.4));
            }
        }

        WHEN("override vectors are misaligned") {
            config.option<ConfigOptionFloats>("nozzle_diameter")->values = { 0.4, 1.57 };
            config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { false, true };
            config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { 0.8 };

            THEN("the missing exact entry cannot activate the override") {
                REQUIRE(config.validate().count("bambu_nozzle_diameter") == 1);
                REQUIRE_FALSE(CustomNozzle::bambu_nozzle_diameter_override_enabled(config, 1));
                REQUIRE(CustomNozzle::resolved_bambu_nozzle_diameter(config, 1) == Catch::Approx(1.57));
            }
        }

        WHEN("the extruder count grows") {
            config.set_num_extruders(2);

            THEN("both override fields grow with the physical nozzle vector") {
                REQUIRE(config.option<ConfigOptionFloats>("nozzle_diameter")->values.size() == 2);
                REQUIRE(config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values.size() == 2);
                REQUIRE(config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values.size() == 2);
                REQUIRE_FALSE(config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->get_at(1));
                REQUIRE(config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->get_at(1) == Catch::Approx(0.8));
            }
        }
    }

    GIVEN("a legacy partial configuration without override keys") {
        DynamicPrintConfig config;
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats { 1.57 });

        THEN("the override is disabled and transport resolution uses the physical diameter") {
            REQUIRE_FALSE(CustomNozzle::bambu_nozzle_diameter_override_enabled(config, 0));
            REQUIRE(CustomNozzle::resolved_bambu_nozzle_diameter(config, 0) == Catch::Approx(1.57));
        }
    }

    GIVEN("the supported Bambu nozzle identities") {
        THEN("only the discrete firmware values are accepted") {
            DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
            config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { true };
            for (double diameter : { 0.2, 0.4, 0.6, 0.8 }) {
                REQUIRE(CustomNozzle::is_supported_bambu_nozzle_diameter(diameter));
                config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { diameter };
                REQUIRE(config.validate().empty());
            }
            for (double diameter : { 0.0, 0.5, 1.0 })
                REQUIRE_FALSE(CustomNozzle::is_supported_bambu_nozzle_diameter(diameter));
        }
    }

    GIVEN("a physical 0.4 mm nozzle with the override disabled") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

        WHEN("the device reports 0.8 mm") {
            const auto result = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.8);

            THEN("physical identity is compared and the mismatch remains blocking") {
                REQUIRE(result.physical_diameter == Catch::Approx(0.4));
                REQUIRE(result.reported_diameter == Catch::Approx(0.4));
                REQUIRE(result.device_diameter == Catch::Approx(0.8));
                REQUIRE_FALSE(result.override_active);
                REQUIRE_FALSE(result.device_unknown);
                REQUIRE_FALSE(result.matches);
            }
        }
    }

    GIVEN("a physical 1.57 mm nozzle with the 0.8 mm override enabled") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.option<ConfigOptionFloats>("nozzle_diameter")->values = { 1.57 };
        config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { true };
        config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { 0.8 };

        WHEN("the device reports 0.8 mm") {
            const auto result = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.8);

            THEN("the reported identity matches and provenance remains available") {
                REQUIRE(result.physical_diameter == Catch::Approx(1.57));
                REQUIRE(result.reported_diameter == Catch::Approx(0.8));
                REQUIRE(result.device_diameter == Catch::Approx(0.8));
                REQUIRE(result.override_active);
                REQUIRE_FALSE(result.device_unknown);
                REQUIRE(result.matches);
            }
        }

        WHEN("the device reports a different supported identity") {
            const auto result = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.6);

            THEN("the override is active but the mismatch remains blocking") {
                REQUIRE(result.override_active);
                REQUIRE_FALSE(result.device_unknown);
                REQUIRE_FALSE(result.matches);
            }
        }

        WHEN("the device diameter is unknown") {
            const auto result = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.0);

            THEN("existing unknown-device compatibility behavior is preserved") {
                REQUIRE(result.override_active);
                REQUIRE(result.device_unknown);
                REQUIRE(result.matches);
            }
        }

        WHEN("the device value differs only within protocol precision") {
            const auto within_tolerance = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.8005);
            const auto outside_tolerance = CustomNozzle::evaluate_bambu_nozzle_compatibility(config, 0, 0.802);

            THEN("the one-micron tolerance is applied consistently") {
                REQUIRE(within_tolerance.matches);
                REQUIRE_FALSE(outside_tolerance.matches);
            }
        }
    }

    GIVEN("a custom-nozzle machine configuration saved as JSON") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.option<ConfigOptionFloats>("nozzle_diameter")->values = { 1.57 };
        config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->values = { true };
        config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->values = { 0.8 };

        const boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                             boost::filesystem::unique_path("orca-custom-nozzle-%%%%-%%%%.json");
        struct RemoveFile {
            boost::filesystem::path path;
            ~RemoveFile() {
                boost::system::error_code ec;
                boost::filesystem::remove(path, ec);
            }
        } cleanup { path };

        WHEN("the configuration is saved and loaded") {
            config.save_to_json(path.string(), "Custom A1 1.57", "User", "1.0.0");

            DynamicPrintConfig loaded;
            ConfigSubstitutionContext substitutions { ForwardCompatibilitySubstitutionRule::Disable };
            std::map<std::string, std::string> key_values;
            std::string reason;
            REQUIRE(loaded.load_from_json(path.string(), substitutions, true, key_values, reason) == 0);

            THEN("physical and Bambu compatibility values round-trip independently") {
                REQUIRE(loaded.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0) == Catch::Approx(1.57));
                REQUIRE(loaded.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->get_at(0));
                REQUIRE(loaded.option<ConfigOptionFloats>("bambu_nozzle_diameter")->get_at(0) == Catch::Approx(0.8));
            }
        }
    }
}

SCENARIO("Generic config validation performs as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN( "outer_wall_line_width is set to 250%, a valid value") {
            config.set_deserialize_strict("outer_wall_line_width", "250%");
            THEN( "The config is read as valid.") {
                REQUIRE(config.validate().empty());
            }
        }
        WHEN( "outer_wall_line_width is set to -10, an invalid value") {
            config.set("outer_wall_line_width", -10);
            THEN( "Validate returns error") {
                REQUIRE_FALSE(config.validate().empty());
            }
        }

        WHEN( "wall_loops is set to -10, an invalid value") {
            config.set("wall_loops", -10);
            THEN( "Validate returns error") {
                REQUIRE_FALSE(config.validate().empty());
            }
        }
    }
}

SCENARIO("Config accessor functions perform as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN("A boolean option is set to a boolean value") {
            REQUIRE_NOTHROW(config.set("gcode_comments", true));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing a 0 or 1") {
            CHECK_NOTHROW(config.set_deserialize_strict("gcode_comments", "1"));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing something other than 0 or 1") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", "Z"), BadOptionTypeException);
            }
            AND_THEN("Value is unchanged.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == false);
            }
        }
        WHEN("A boolean option is set to an int value") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", 1), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set from serialized string") {
            config.set_deserialize_strict("raft_layers", "20");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInt>("raft_layers")->getInt() == 20);
            }
        }
	WHEN("An integer-based option is set through the integer interface") {
	    config.set("raft_layers", 100);
	    THEN("The underlying value is set correctly.") {
		REQUIRE(config.opt<ConfigOptionInt>("raft_layers")->getInt() == 100);
	    }
        }
        WHEN("An floating-point option is set through the integer interface") {
            config.set("max_bridge_length", 10);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == 10.0);
            }
        }
        WHEN("A floating-point option is set through the double interface") {
            config.set("max_bridge_length", 5.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == 5.5);
            }
        }
        WHEN("An integer-based option is set through the double interface") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("top_shell_layers", 5.5), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set to a non-numeric value.") {
	    auto prev_value = config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat();
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("max_bridge_length", "zzzz"), BadOptionValueException);
            }
            THEN("The value does not change.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == prev_value);
            }
        }
        WHEN("A string option is set through the string interface") {
            config.set("machine_end_gcode", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the integer interface") {
            config.set("machine_end_gcode", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the double interface") {
            config.set("machine_end_gcode", 100.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }
        WHEN("A float or percent is set as a percent through the string interface.") {
            config.set_deserialize_strict("initial_layer_line_width", "100%");
            THEN("Value and percent flag are 100/true") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == true);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the string interface.") {
            config.set_deserialize_strict("initial_layer_line_width", "100");
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the int interface.") {
            config.set("initial_layer_line_width", 100);
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the double interface.") {
            config.set("initial_layer_line_width", 100.5);
            THEN("Value and percent flag are 100.5/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100.5);
            }
        }
        WHEN("A numeric vector is set from serialized string") {
	    config.set_deserialize_strict("temperature_vitrification", "10,20");
            THEN("The underlying value is set correctly.") {
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 10);
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(1) == 20);
            }
        }
	// FIXME: Design better accessors for vector elements
	// The following isn't supported and probably shouldn't be:
	// WHEN("An integer-based vector option is set through the integer interface") {
	//     config.set("temperature_vitrification", 100);
	//     THEN("The underlying value is set correctly.") {
	// 	REQUIRE(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 100);
	//     }
        // }
	WHEN("An integer-based vector option is set through the set_key_value interface") {
	    config.set_key_value("temperature_vitrification", new ConfigOptionInts{10,20});
	    THEN("The underlying value is set correctly.") {
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 10);
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(1) == 20);
	    }
        }
        WHEN("An invalid option is requested during set.") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }
        WHEN("An invalid option is requested during opt.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("getX called on an unset option.") {
            THEN("The default is returned.") {
                REQUIRE(config.opt_float("layer_height") == INITIAL_LAYER_HEIGHT);
                REQUIRE(config.opt_int("raft_layers") == INITIAL_RAFT_LAYERS);
                REQUIRE(config.opt_bool("reduce_crossing_wall") == INITIAL_REDUCE_CROSSING_WALL);
            }
        }

        WHEN("opt_float called on an option that has been set.") {
            config.set("layer_height", INITIAL_LAYER_HEIGHT*2);
            THEN("The set value is returned.") {
                REQUIRE(config.opt_float("layer_height") == INITIAL_LAYER_HEIGHT*2);
            }
        }
    }
}

SCENARIO("Config ini load/save interface", "[Config]") {
    WHEN("new_from_ini is called") {
		Slic3r::DynamicPrintConfig config;
		std::string path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
		config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("Config object contains ini file options.") {
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

// TODO: https://github.com/SoftFever/OrcaSlicer/issues/11269 - Is this test still relevant? Delete if not.
// It was failing so at least "nozzle_type" and "extruder_printable_area" could not be serialized
// and an exception was thrown, but "nozzle_type" has been around for at least 3 months now.
// So maybe this test and the serialization logic in Config.?pp should be deleted if it doesn't get used.
SCENARIO("DynamicPrintConfig serialization", "[Config]") {
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        FullPrintConfig full_print_config;
        DynamicPrintConfig cfg;
        cfg.apply(full_print_config, false);

        std::string serialized;
        // try {
            std::ostringstream ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        // } catch (const std::runtime_error & /* e */) {
        //     // e.what();
        // }
	CAPTURE(serialized.length());

        THEN("Config object contains ini file options.") {
            DynamicPrintConfig cfg2;
            // try {
                std::stringstream ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            // } catch (const std::runtime_error & /* e */) {
            //     // e.what();
            // }
	    CAPTURE(cfg.diff_report(cfg2));
            REQUIRE(cfg == cfg2);
        }
    }
}

SCENARIO("update_non_diff_values_to_base_config preserves child vectors when child has more extruders than parent",
         "[Config][Variant]") {
    GIVEN("A 2-extruder child printer config inheriting from a 1-extruder parent") {
        Slic3r::DynamicPrintConfig child;
        Slic3r::DynamicPrintConfig parent;

        child.set_key_value("nozzle_diameter",           new Slic3r::ConfigOptionFloats({0.4, 0.4}));
        child.set_key_value("printer_extruder_id",       new Slic3r::ConfigOptionInts({1, 2}));
        child.set_key_value("printer_extruder_variant",  new Slic3r::ConfigOptionStrings({"Direct Drive Standard", "Direct Drive Standard"}));
        child.set_key_value("retraction_length",         new Slic3r::ConfigOptionFloats({1.5, 1.5}));

        parent.set_key_value("nozzle_diameter",          new Slic3r::ConfigOptionFloats({0.4}));
        parent.set_key_value("printer_extruder_id",      new Slic3r::ConfigOptionInts({1}));
        parent.set_key_value("printer_extruder_variant", new Slic3r::ConfigOptionStrings({"Direct Drive Standard"}));
        parent.set_key_value("retraction_length",        new Slic3r::ConfigOptionFloats({0.8}));

        const Slic3r::t_config_option_keys keys = {
            "retraction_length", "printer_extruder_id", "printer_extruder_variant"
        };
        const std::set<std::string> different_keys = {
            "retraction_length", "printer_extruder_id", "printer_extruder_variant"
        };

        WHEN("update_non_diff_values_to_base_config is called") {
            std::string id_name  = "printer_extruder_id";
            std::string var_name = "printer_extruder_variant";
            child.update_non_diff_values_to_base_config(
                parent, keys, different_keys, id_name, var_name,
                Slic3r::printer_options_with_variant_1,
                Slic3r::printer_options_with_variant_2);

            THEN("printer_extruder_id retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionInts>("printer_extruder_id")->values.size() == 2);
            }
            THEN("printer_extruder_variant retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionStrings>("printer_extruder_variant")->values.size() == 2);
            }
            THEN("retraction_length retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionFloats>("retraction_length")->values.size() == 2);
            }
            THEN("printer_extruder_id values are preserved for both extruders") {
                auto* pe_id = child.option<Slic3r::ConfigOptionInts>("printer_extruder_id");
                REQUIRE(pe_id->values.size() == 2);
                REQUIRE(pe_id->values[0] == 1);
                REQUIRE(pe_id->values[1] == 2);
            }
        }
    }
}

SCENARIO("update_diff_values_to_child_config tolerates legacy machine-limit vector sizes",
         "[Config][Variant]") {
    // Regression: loading a user printer preset that inherits a non-BBL multi-extruder base and
    // overrides stride-2 machine limits used to throw in ConfigOptionVector::set_only_diff
    // ("invalid diff_index size"). The base's machine-limit vectors get length-extended by the
    // nozzle count while it carries no printer_extruder_variant, so the base length (nozzles*2)
    // no longer matches variant_index.size()*2. The throw was caught upstream and DELETED the
    // user's preset file. The merge must instead degrade gracefully.
    GIVEN("A 4-nozzle parent with stride-2 limits extended to nozzles*2 but no printer_extruder_variant") {
        Slic3r::DynamicPrintConfig parent;
        Slic3r::DynamicPrintConfig child;

        parent.set_key_value("nozzle_diameter",
            new Slic3r::ConfigOptionFloats({0.4, 0.4, 0.4, 0.4}));
        parent.set_key_value("machine_max_acceleration_x",
            new Slic3r::ConfigOptionFloats({25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000}));

        // Child user preset declares 4 extruder variants and overrides the machine limit.
        child.set_key_value("printer_extruder_id",
            new Slic3r::ConfigOptionInts({1, 2, 3, 4}));
        child.set_key_value("printer_extruder_variant",
            new Slic3r::ConfigOptionStrings({"Direct Drive Standard", "Direct Drive Standard",
                                             "Direct Drive Standard", "Direct Drive Standard"}));
        child.set_key_value("machine_max_acceleration_x",
            new Slic3r::ConfigOptionFloats({8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000}));

        WHEN("update_diff_values_to_child_config merges the child overrides") {
            std::string id_name  = "printer_extruder_id";
            std::string var_name = "printer_extruder_variant";

            THEN("it does not throw on the legacy size mismatch") {
                REQUIRE_NOTHROW(parent.update_diff_values_to_child_config(
                    child, id_name, var_name,
                    Slic3r::printer_options_with_variant_1,
                    Slic3r::printer_options_with_variant_2));

                AND_THEN("the child's overridden machine limit is preserved") {
                    auto* mx = parent.option<Slic3r::ConfigOptionFloats>("machine_max_acceleration_x");
                    REQUIRE(mx != nullptr);
                    REQUIRE(mx->values.size() >= 2);
                    REQUIRE_THAT(mx->values[0], Catch::Matchers::WithinAbs(8000.0, 1e-6));
                    REQUIRE_THAT(mx->values[1], Catch::Matchers::WithinAbs(8000.0, 1e-6));
                }
            }
        }
    }
}

// SCENARIO("DynamicPrintConfig JSON serialization", "[Config]") {
//     WHEN("DynamicPrintConfig is serialized and deserialized") {
// 	auto now = std::chrono::high_resolution_clock::now();
// 	auto timestamp = now.time_since_epoch().count();
// 	std::stringstream ss;
// 	ss << "catch_test_serialization_" << timestamp << ".json";
// 	std::string filename = (fs::temp_directory_path() / ss.str()).string();

// TODO: Finish making a unit test for JSON serialization
//         FullPrintConfig full_print_config;
//         DynamicPrintConfig cfg;
//         cfg.apply(full_print_config, false);

//         std::string serialized;
//         try {
//             std::ostringstream ss;
//             cereal::BinaryOutputArchive oarchive(ss);
//             oarchive(cfg);
//             serialized = ss.str();
//         } catch (const std::runtime_error & /* e */) {
//             // e.what();
//         }
// 	CAPTURE(serialized.length());

//         THEN("Config object contains ini file options.") {
//             DynamicPrintConfig cfg2;
//             try {
//                 std::stringstream ss(serialized);
//                 cereal::BinaryInputArchive iarchive(ss);
//                 iarchive(cfg2);
//             } catch (const std::runtime_error & /* e */) {
//                 // e.what();
//             }
// 	    CAPTURE(cfg.diff_report(cfg2));
//             REQUIRE(cfg == cfg2);
//         }
//     }
// }
