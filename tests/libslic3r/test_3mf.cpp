
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/operations.hpp>

#include <catch2/catch_tostring.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <type_traits> // for std::enable_if_t
#include <typeinfo>    // for typeid
#include <utility>

namespace Catch {
    template <typename T>
    struct is_eigen_matrix : std::is_base_of<Eigen::MatrixBase<T>, T> {};

    template <typename T>
    struct StringMaker<T, std::enable_if_t<is_eigen_matrix<T>::value>> {
        static std::string convert(const T& eigen_obj) {
            // Newline at end of rows
            Eigen::IOFormat fmt(4, 0, ", ", "\n", "[", "]");
            std::stringstream ss;
            ss << "Matrix<" << typeid(eigen_obj).name() << "> = \n";
            ss << eigen_obj.format(fmt);
            return ss.str();
        }
    };
    
    // We must manually specialize for Eigen::Transform as it doesn't derive from MatrixBase.
    // It's defined as: Eigen::Transform<Scalar, Dim, Mode, Options>
    template <typename Scalar, int Dim, int Mode, int Options>
    struct StringMaker<Eigen::Transform<Scalar, Dim, Mode, Options>> {
        static std::string convert(const Eigen::Transform<Scalar, Dim, Mode, Options>& trafo) {
            // We print the underlying matrix 
            const auto& matrix = trafo.matrix();

            // Newline at end of rows
            Eigen::IOFormat fmt(4, 0, ", ", "\n", "[", "]");
            std::stringstream ss;
            
            ss << "Transform<Mode=" << Mode << ", Dim=" << Dim << "> = \n"; 
            ss << matrix.format(fmt);
            return ss.str();
        }
    };
    
    // Quaternions also need an explicit specialization
    template <typename Scalar, int Options>
    struct StringMaker<Eigen::Quaternion<Scalar, Options>> {
        static std::string convert(const Eigen::Quaternion<Scalar, Options>& quat) {
            std::stringstream ss;
            ss << "Quaternion(w=" << quat.w() << ", x=" << quat.x() << ", y=" << quat.y() << ", z=" << quat.z() << ")";
            return ss.str();
        }
    };
} // end namespace Catch

#include <catch2/catch_all.hpp>

using namespace Slic3r;

namespace {

struct ArchiveReaderGuard
{
    mz_zip_archive archive {};
    bool initialized {false};
    ~ArchiveReaderGuard() { if (initialized) mz_zip_reader_end(&archive); }
};

struct TempFileGuard
{
    explicit TempFileGuard(std::string path) : path(std::move(path)) {}
    ~TempFileGuard() { if (!path.empty()) { boost::system::error_code ec; boost::filesystem::remove(path, ec); } }
    std::string release() { return std::exchange(path, std::string {}); }
    std::string path;
};

struct LoadedBbsDataGuard
{
    LoadedBbsDataGuard(PlateDataPtrs &plates, std::vector<Preset *> &presets) : plates(plates), presets(presets) {}
    ~LoadedBbsDataGuard()
    {
        release_PlateData_list(plates);
        for (Preset *preset : presets)
            delete preset;
    }
    PlateDataPtrs &plates;
    std::vector<Preset *> &presets;
};

std::string extract_archive_file(const std::string &archive_path, const char *file_name)
{
    ArchiveReaderGuard reader;
    REQUIRE(mz_zip_reader_init_file(&reader.archive, archive_path.c_str(), 0));
    reader.initialized = true;

    const int file_index = mz_zip_reader_locate_file(&reader.archive, file_name, nullptr, 0);
    REQUIRE(file_index >= 0);

    mz_zip_archive_file_stat stat {};
    REQUIRE(mz_zip_reader_file_stat(&reader.archive, file_index, &stat));
    std::string contents(size_t(stat.m_uncomp_size), '\0');
    REQUIRE(mz_zip_reader_extract_to_mem(&reader.archive, file_index, contents.data(), contents.size(), 0));
    return contents;
}

std::string xml_element_with_id(const std::string &xml, const char *element_name, int id)
{
    const std::string prefix = std::string("<") + element_name + " id=\"" + std::to_string(id) + "\"";
    const size_t begin = xml.find(prefix);
    REQUIRE(begin != std::string::npos);
    const size_t end = xml.find("/>", begin);
    REQUIRE(end != std::string::npos);
    return xml.substr(begin, end + 2 - begin);
}

struct TemporaryDirGuard {
    std::string previous = temporary_dir();
    TemporaryDirGuard() { set_temporary_dir(boost::filesystem::temp_directory_path().string()); }
    ~TemporaryDirGuard() { set_temporary_dir(previous); }
};

std::string store_custom_nozzle_bbs_3mf(BambuMetadataMode metadata_mode, bool override_enabled = true, bool include_bbox = false,
                                        bool two_extruders = false)
{
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    if (two_extruders) {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats { 1.57, 1.2 });
        config.set_key_value("bambu_nozzle_diameter_override", new ConfigOptionBools { override_enabled, override_enabled });
        config.set_key_value("bambu_nozzle_diameter", new ConfigOptionFloats { 0.8, 0.6 });
        config.set_key_value("filament_map", new ConfigOptionInts { 1, 2 });
    } else {
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats { 1.57 });
        config.set_key_value("bambu_nozzle_diameter_override", new ConfigOptionBools { override_enabled });
        config.set_key_value("bambu_nozzle_diameter", new ConfigOptionFloats { 0.8 });
    }

    PlateData plate;
    plate.plate_index = 0;
    plate.is_sliced_valid = true;
    plate.printer_model_id = "N2S";
    FilamentInfo filament;
    filament.id = 0;
    filament.type = "PLA";
    filament.nozzle_diameter = override_enabled ? 1.57 : 1.23;
    plate.slice_filaments_info.push_back(filament);
    if (two_extruders) {
        FilamentInfo second_filament;
        second_filament.id = 1;
        second_filament.type = "PETG";
        second_filament.nozzle_diameter = 1.2;
        plate.slice_filaments_info.push_back(second_filament);
        plate.filament_maps = { 1, 2 };
    }
    PlateDataPtrs plates { &plate };

    PlateBBoxData bbox;
    bbox.bbox_all = { 0.0, 0.0, 10.0, 10.0 };
    bbox.bbox_objs.push_back(BBoxData { 1, { 0.0, 0.0, 10.0, 10.0 }, 100.0f, 0.5f, "test" });
    bbox.first_extruder = 0;
    bbox.nozzle_diameter = override_enabled ? 1.57f : 1.23f;

    const boost::filesystem::path archive_path = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("orca-custom-nozzle-%%%%-%%%%.3mf");
    const std::string archive_path_string = archive_path.string();
    TempFileGuard failed_export_cleanup(archive_path_string);

    StoreParams params;
    params.path = archive_path_string.c_str();
    params.model = &model;
    params.plate_data_list = plates;
    params.config = &config;
    params.strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::SkipAuxiliary;
    if (!include_bbox)
        params.strategy = params.strategy | SaveStrategy::SkipStatic;
    else
        params.id_bboxes = { &bbox };
    params.bambu_metadata_mode = metadata_mode;
    REQUIRE(store_bbs_3mf(params));
    return failed_export_cleanup.release();
}

} // namespace


SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Model model;
        WHEN("3mf model is read") {
            std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
            DynamicPrintConfig config;
            ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
            bool ret = load_3mf(path.c_str(), config, ctxt, &model, false);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &src_model);
        src_model.add_default_instances();

        ModelObject* src_object = src_model.objects.front();

        // apply generic transformation to the 1st volume
        Geometry::Transformation src_volume_transform;
        src_volume_transform.set_offset({ 10.0, 20.0, 0.0 });
        src_volume_transform.set_rotation({ Geometry::deg2rad(25.0), Geometry::deg2rad(35.0), Geometry::deg2rad(45.0) });
        src_volume_transform.set_scaling_factor({ 1.1, 1.2, 1.3 });
        src_volume_transform.set_mirror({ -1.0, 1.0, -1.0 });
        src_object->volumes.front()->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Geometry::Transformation src_instance_transform;
        src_instance_transform.set_offset({ 5.0, 10.0, 0.0 });
        src_instance_transform.set_rotation({ Geometry::deg2rad(12.0), Geometry::deg2rad(13.0), Geometry::deg2rad(14.0) });
        src_instance_transform.set_scaling_factor({ 0.9, 0.8, 0.7 });
        src_instance_transform.set_mirror({ 1.0, -1.0, -1.0 });
        src_object->instances.front()->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            store_3mf(test_file.c_str(), &src_model, nullptr, false);

            // load back the model from the 3mf file
            Model dst_model;
            DynamicPrintConfig dst_config;
            {
                ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
                load_3mf(test_file.c_str(), dst_config, ctxt, &dst_model, false);
            }
            boost::filesystem::remove(test_file);

            // compare meshes
            TriangleMesh src_mesh = src_model.mesh();
            TriangleMesh dst_mesh = dst_model.mesh();

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res) {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i) {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
        }
    }
}

SCENARIO("2D convex hull of sinking object", "[3mf][.]") {
    GIVEN("model") {
        // load a model
        Model model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        REQUIRE(load_stl(src_file.c_str(), &model));
        model.add_default_instances();

        WHEN("model is rotated, scaled and set as sinking") {
            ModelObject* object = model.objects[0];
            object->center_around_origin(false);

	    // This outputs the same exact data as the Prusaslicer test
	    object->volumes[0]->mesh().write_ascii("/tmp/orca.ascii");

            // set instance's attitude so that it is rotated, scaled (and sinking? how is it sinking? the rotation? does it matter if it's sinking?)
            ModelInstance* instance = object->instances[0];
            instance->set_rotation(X, -M_PI / 4.0);
            instance->set_offset(Vec3d::Zero());
            instance->set_scaling_factor({ 2.0, 2.0, 2.0 });

            // calculate 2D convex hull
	    auto trafo = instance->get_transformation().get_matrix();

	    // This matrix is the same exact matrix as the Prusaslicer test
	    CAPTURE(trafo);
            Polygon hull_2d = object->convex_hull_2d(trafo);

	    // But we get different hull_2d.points here (and somehow decimal numbers despite being int64_t values, but that's probabaly printing configuration somewhere -- Prusaslicer's prints out with newlines between the X&Y and not one between coordinates, which is about the worse possible output).
	    // I think it's something to do with PrusaSlicer ignoring everything under the Z plane, which makes sense from the results.
	    // See the comments added to ModelObject::convex_hull_2d for more information.

            // verify result
            Points result = {
                { -91501496, -15914144 },
                { 91501496, -15914144 },
                { 91501496, 4243 },
                { 78229680, 4246883 },
                { 56898100, 4246883 },
                { -85501496, 4242641 },
                { -91501496, 4243 }
            };

            THEN("2D convex hull should match with reference") {
                // Allow 1um error due to floating point rounding.
                bool res = hull_2d.points.size() == result.size();
                if (res) {
                    for (size_t i = 0; i < result.size(); ++ i) {
                        const Point &p1 = result[i];
                        const Point &p2 = hull_2d.points[i];
                        CHECK((std::abs(p1.x() - p2.x()) > 1 || std::abs(p1.y() - p2.y()) > 1));
                    }
                }

                CAPTURE(hull_2d.points);
                REQUIRE(res);
            }
        }
    }
}

TEST_CASE("Bambu transport 3MF projects only nozzle identity metadata", "[3mf][bbs][custom_nozzle]")
{
    TemporaryDirGuard temporary_dir_guard;

    SECTION("transport mode is a no-op when the explicit override is disabled") {
        const std::string path = store_custom_nozzle_bbs_3mf(BambuMetadataMode::ReportedCompatibility, false, true);
        TempFileGuard archive_cleanup(path);
        const std::string slice_info = extract_archive_file(path, "Metadata/slice_info.config");
        const nlohmann::json bbox = nlohmann::json::parse(extract_archive_file(path, "Metadata/plate_1.json"));

        CHECK(slice_info.find("key=\"nozzle_diameters\" value=\"1.57\"") != std::string::npos);
        const std::string filament_1 = xml_element_with_id(slice_info, "filament", 1);
        const std::string nozzle_0 = xml_element_with_id(slice_info, "nozzle", 0);
        CHECK(filament_1.find("nozzle_diameter=\"1.23\"") != std::string::npos);
        CHECK(nozzle_0.find("nozzle_diameter=\"1.57\"") != std::string::npos);
        CHECK(slice_info.find("orca_physical_nozzle_diameters") == std::string::npos);
        CHECK(slice_info.find("orca_bambu_nozzle_override") == std::string::npos);
        CHECK(bbox.at("nozzle_diameter").get<double>() == Catch::Approx(1.23));
    }

    SECTION("ordinary project metadata remains physical") {
        const std::string path = store_custom_nozzle_bbs_3mf(BambuMetadataMode::Physical, true, true);
        TempFileGuard archive_cleanup(path);
        const std::string slice_info = extract_archive_file(path, "Metadata/slice_info.config");
        const nlohmann::json bbox = nlohmann::json::parse(extract_archive_file(path, "Metadata/plate_1.json"));

        CHECK(slice_info.find("key=\"nozzle_diameters\" value=\"1.57\"") != std::string::npos);
        CHECK(slice_info.find("nozzle_diameter=\"1.57\"") != std::string::npos);
        CHECK(slice_info.find("orca_physical_nozzle_diameters") == std::string::npos);
        CHECK(slice_info.find("orca_bambu_nozzle_override") == std::string::npos);
        CHECK(bbox.at("nozzle_diameter").get<double>() == Catch::Approx(1.57));

        DynamicPrintConfig loaded_config;
        ConfigSubstitutionContext substitutions { ForwardCompatibilitySubstitutionRule::Disable };
        Model loaded_model;
        PlateDataPtrs loaded_plates;
        std::vector<Preset *> loaded_presets;
        LoadedBbsDataGuard loaded_cleanup(loaded_plates, loaded_presets);
        bool is_bbl_3mf = false;
        bool is_orca_3mf = false;
        Semver file_version;
        REQUIRE(load_bbs_3mf(path.c_str(), &loaded_config, &substitutions, &loaded_model, &loaded_plates, &loaded_presets,
                             &is_bbl_3mf, &is_orca_3mf, &file_version, nullptr, LoadStrategy::LoadConfig));
        const auto *loaded_physical = loaded_config.option<ConfigOptionFloats>("nozzle_diameter");
        const auto *loaded_override = loaded_config.option<ConfigOptionBools>("bambu_nozzle_diameter_override");
        const auto *loaded_reported = loaded_config.option<ConfigOptionFloats>("bambu_nozzle_diameter");
        REQUIRE(loaded_physical != nullptr);
        REQUIRE(loaded_override != nullptr);
        REQUIRE(loaded_reported != nullptr);
        CHECK(loaded_physical->get_at(0) == Catch::Approx(1.57));
        CHECK(loaded_override->get_at(0));
        CHECK(loaded_reported->get_at(0) == Catch::Approx(0.8));

    }

    SECTION("two extruders project plate, mapped filament, and nozzle nodes independently") {
        const std::string path = store_custom_nozzle_bbs_3mf(BambuMetadataMode::ReportedCompatibility, true, false, true);
        TempFileGuard archive_cleanup(path);
        const std::string slice_info = extract_archive_file(path, "Metadata/slice_info.config");

        CHECK(slice_info.find("key=\"nozzle_diameters\" value=\"0.8,0.6\"") != std::string::npos);
        const std::string filament_1 = xml_element_with_id(slice_info, "filament", 1);
        const std::string filament_2 = xml_element_with_id(slice_info, "filament", 2);
        CHECK(filament_1.find("group_id=\"0\"") != std::string::npos);
        CHECK(filament_1.find("nozzle_diameter=\"0.80\"") != std::string::npos);
        CHECK(filament_2.find("group_id=\"1\"") != std::string::npos);
        CHECK(filament_2.find("nozzle_diameter=\"0.60\"") != std::string::npos);

        const std::string nozzle_0 = xml_element_with_id(slice_info, "nozzle", 0);
        const std::string nozzle_1 = xml_element_with_id(slice_info, "nozzle", 1);
        CHECK(nozzle_0.find("extruder_id=\"1\"") != std::string::npos);
        CHECK(nozzle_0.find("nozzle_diameter=\"0.8\"") != std::string::npos);
        CHECK(nozzle_1.find("extruder_id=\"2\"") != std::string::npos);
        CHECK(nozzle_1.find("nozzle_diameter=\"0.6\"") != std::string::npos);
    }

    SECTION("outgoing Bambu metadata is reported while physical provenance survives import") {
        const std::string path = store_custom_nozzle_bbs_3mf(BambuMetadataMode::ReportedCompatibility, true, true);
        TempFileGuard archive_cleanup(path);
        const std::string slice_info = extract_archive_file(path, "Metadata/slice_info.config");
        const nlohmann::json bbox = nlohmann::json::parse(extract_archive_file(path, "Metadata/plate_1.json"));

        CHECK(slice_info.find("key=\"nozzle_diameters\" value=\"0.8\"") != std::string::npos);
        const std::string filament_1 = xml_element_with_id(slice_info, "filament", 1);
        const std::string nozzle_0 = xml_element_with_id(slice_info, "nozzle", 0);
        CHECK(filament_1.find("group_id=\"0\"") != std::string::npos);
        CHECK(filament_1.find("nozzle_diameter=\"0.80\"") != std::string::npos);
        CHECK(nozzle_0.find("extruder_id=\"1\"") != std::string::npos);
        CHECK(nozzle_0.find("nozzle_diameter=\"0.8\"") != std::string::npos);
        CHECK(slice_info.find("key=\"orca_physical_nozzle_diameters\" value=\"1.57\"") != std::string::npos);
        CHECK(slice_info.find("key=\"orca_bambu_nozzle_override\" value=\"true\"") != std::string::npos);
        CHECK(bbox.at("nozzle_diameter").get<double>() == Catch::Approx(0.8));

        DynamicPrintConfig loaded_config;
        ConfigSubstitutionContext substitutions { ForwardCompatibilitySubstitutionRule::Disable };
        Model loaded_model;
        PlateDataPtrs loaded_plates;
        std::vector<Preset *> loaded_presets;
        LoadedBbsDataGuard loaded_cleanup(loaded_plates, loaded_presets);
        bool is_bbl_3mf = false;
        bool is_orca_3mf = false;
        Semver file_version;
        REQUIRE(load_bbs_3mf(path.c_str(), &loaded_config, &substitutions, &loaded_model, &loaded_plates, &loaded_presets,
                             &is_bbl_3mf, &is_orca_3mf, &file_version, nullptr, LoadStrategy::LoadConfig));
        REQUIRE(loaded_plates.size() == 1);
        CHECK(loaded_plates.front()->nozzle_diameters == "0.8");
        CHECK(loaded_plates.front()->orca_physical_nozzle_diameters == "1.57");
        CHECK(loaded_plates.front()->orca_bambu_nozzle_override);
        const auto *loaded_physical = loaded_config.option<ConfigOptionFloats>("nozzle_diameter");
        const auto *loaded_override = loaded_config.option<ConfigOptionBools>("bambu_nozzle_diameter_override");
        const auto *loaded_reported = loaded_config.option<ConfigOptionFloats>("bambu_nozzle_diameter");
        REQUIRE(loaded_physical != nullptr);
        REQUIRE(loaded_override != nullptr);
        REQUIRE(loaded_reported != nullptr);
        CHECK(loaded_physical->get_at(0) == Catch::Approx(1.57));
        CHECK(loaded_override->get_at(0));
        CHECK(loaded_reported->get_at(0) == Catch::Approx(0.8));

        const boost::filesystem::path roundtrip_path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("orca-custom-nozzle-roundtrip-%%%%-%%%%.3mf");
        TempFileGuard roundtrip_cleanup(roundtrip_path.string());
        loaded_plates.front()->is_sliced_valid = true;
        StoreParams roundtrip_params;
        const std::string roundtrip_path_string = roundtrip_path.string();
        roundtrip_params.path = roundtrip_path_string.c_str();
        roundtrip_params.model = &loaded_model;
        roundtrip_params.plate_data_list = loaded_plates;
        roundtrip_params.project_presets = loaded_presets;
        roundtrip_params.config = &loaded_config;
        roundtrip_params.strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::SkipAuxiliary;
        roundtrip_params.bambu_metadata_mode = BambuMetadataMode::ReportedCompatibility;
        REQUIRE(store_bbs_3mf(roundtrip_params));

        DynamicPrintConfig twice_loaded_config;
        ConfigSubstitutionContext twice_substitutions { ForwardCompatibilitySubstitutionRule::Disable };
        Model twice_loaded_model;
        PlateDataPtrs twice_loaded_plates;
        std::vector<Preset *> twice_loaded_presets;
        LoadedBbsDataGuard twice_loaded_cleanup(twice_loaded_plates, twice_loaded_presets);
        bool twice_is_bbl_3mf = false;
        bool twice_is_orca_3mf = false;
        Semver twice_file_version;
        REQUIRE(load_bbs_3mf(roundtrip_path_string.c_str(), &twice_loaded_config, &twice_substitutions, &twice_loaded_model,
                             &twice_loaded_plates, &twice_loaded_presets, &twice_is_bbl_3mf, &twice_is_orca_3mf,
                             &twice_file_version, nullptr, LoadStrategy::LoadConfig));
        REQUIRE(twice_loaded_plates.size() == 1);
        CHECK(twice_loaded_plates.front()->nozzle_diameters == "0.8");
        CHECK(twice_loaded_plates.front()->orca_physical_nozzle_diameters == "1.57");
        CHECK(twice_loaded_plates.front()->orca_bambu_nozzle_override);
        REQUIRE(twice_loaded_config.option<ConfigOptionFloats>("nozzle_diameter") != nullptr);
        REQUIRE(twice_loaded_config.option<ConfigOptionBools>("bambu_nozzle_diameter_override") != nullptr);
        REQUIRE(twice_loaded_config.option<ConfigOptionFloats>("bambu_nozzle_diameter") != nullptr);
        CHECK(twice_loaded_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0) == Catch::Approx(1.57));
        CHECK(twice_loaded_config.option<ConfigOptionBools>("bambu_nozzle_diameter_override")->get_at(0));
        CHECK(twice_loaded_config.option<ConfigOptionFloats>("bambu_nozzle_diameter")->get_at(0) == Catch::Approx(0.8));

    }
}

TEST_CASE("Orca physical nozzle provenance recovery is strict and does not enable overrides", "[3mf][bbs][custom_nozzle]")
{
    SECTION("valid consistent provenance reconstructs only physical diameters") {
        PlateData first;
        first.orca_bambu_nozzle_override = true;
        first.orca_physical_nozzle_diameters = "1.57,1.2";
        PlateData second;
        second.orca_bambu_nozzle_override = true;
        second.orca_physical_nozzle_diameters = "1.57,1.2";
        PlateDataPtrs plates { &first, &second };
        DynamicPrintConfig config;

        REQUIRE(recover_orca_physical_nozzle_diameters(plates, config));
        const auto *physical = config.option<ConfigOptionFloats>("nozzle_diameter");
        REQUIRE(physical != nullptr);
        REQUIRE(physical->values.size() == 2);
        CHECK(physical->values[0] == Catch::Approx(1.57));
        CHECK(physical->values[1] == Catch::Approx(1.2));
        CHECK(config.option("bambu_nozzle_diameter_override") == nullptr);
        CHECK(config.option("bambu_nozzle_diameter") == nullptr);
    }

    SECTION("malformed or ambiguous provenance is rejected without partial parsing") {
        for (const std::string malformed : { "1.57junk", "1.57,", ",1.57", "nan", "0.004" }) {
            PlateData plate;
            plate.orca_bambu_nozzle_override = true;
            plate.orca_physical_nozzle_diameters = malformed;
            PlateDataPtrs plates { &plate };
            DynamicPrintConfig config;
            CHECK_FALSE(recover_orca_physical_nozzle_diameters(plates, config));
            CHECK(config.option("nozzle_diameter") == nullptr);
        }

        PlateData first;
        first.orca_bambu_nozzle_override = true;
        first.orca_physical_nozzle_diameters = "1.57";
        PlateData second;
        second.orca_bambu_nozzle_override = true;
        second.orca_physical_nozzle_diameters = "0.8";
        PlateDataPtrs inconsistent { &first, &second };
        DynamicPrintConfig config;
        CHECK_FALSE(recover_orca_physical_nozzle_diameters(inconsistent, config));
        CHECK(config.option("nozzle_diameter") == nullptr);
    }

    SECTION("existing physical config and foreign standard metadata are never overwritten") {
        PlateData plate;
        plate.nozzle_diameters = "0.8";
        PlateDataPtrs plates { &plate };
        DynamicPrintConfig empty_config;
        CHECK_FALSE(recover_orca_physical_nozzle_diameters(plates, empty_config));
        CHECK(empty_config.option("nozzle_diameter") == nullptr);

        plate.orca_bambu_nozzle_override = true;
        plate.orca_physical_nozzle_diameters = "1.57";
        DynamicPrintConfig existing_config;
        existing_config.set_key_value("nozzle_diameter", new ConfigOptionFloats { 0.6 });
        CHECK_FALSE(recover_orca_physical_nozzle_diameters(plates, existing_config));
        CHECK(existing_config.option<ConfigOptionFloats>("nozzle_diameter")->get_at(0) == Catch::Approx(0.6));
    }
}
