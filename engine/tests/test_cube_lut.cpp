#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/effects/CubeLut.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace aurea;
namespace {
std::string cube_identity() {
    std::string out = "TITLE \"Identity\"\nLUT_3D_SIZE 2\n";
    for (int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r)
        out += std::to_string(r)+" "+std::to_string(g)+" "+std::to_string(b)+"\n";
    return out;
}
}
AUREA_TEST(CubeLut, ParseDomainsOrderingAndOneDimensionalTables) {
    const auto cube = parse_cube_lut("\xef\xbb\xbf# comment\r\nDOMAIN_MIN -1 0 0\r\nDOMAIN_MAX 2 1 1\n"+cube_identity());
    AUREA_CHECK(cube.ok()); if (!cube.ok()) return;
    AUREA_CHECK_EQ(cube->values.size(), 8u);
    AUREA_CHECK_NEAR(cube->domainMin.x, -1, 1e-6);
    AUREA_CHECK_NEAR(cube->domainMax.x, 2, 1e-6);
    AUREA_CHECK_NEAR(cube->values[1].x, 1, 1e-6);
    AUREA_CHECK_NEAR(cube->values[1].z, 0, 1e-6);
    AUREA_CHECK_NEAR(cube->values[4].z, 1, 1e-6);
    const auto one = parse_cube_lut("LUT_1D_SIZE 2\nLUT_1D_INPUT_RANGE 0 2\n0 0 0\n1 2 3\n");
    AUREA_CHECK(one.ok()); if (!one.ok()) return;
    AUREA_CHECK_EQ(one->dimensions, 1u);
    AUREA_CHECK_NEAR(one->domainMax.y, 2, 1e-6);
    AUREA_CHECK_NEAR(one->values.back().z, 3, 1e-6);
}
AUREA_TEST(CubeLut, RejectMalformedUnboundedAndCombinedFiles) {
    for (const std::string s : {"", "LUT_3D_SIZE 1", "LUT_3D_SIZE 66", "LUT_1D_SIZE 65537",
             "LUT_3D_SIZE 4294967295", "LUT_1D_SIZE 2\nLUT_3D_SIZE 2", "LUT_1D_SIZE 2\n0 0 0\n",
             "LUT_1D_SIZE 2\n0 0 0\nnan 0 0", "LUT_1D_SIZE 2\n0 0 0\n1e999 0 0",
             "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n1 1 1", "LUT_1D_SIZE 2\nDOMAIN_MAX 0 1 1\n0 0 0\n1 1 1"})
        AUREA_CHECK(!parse_cube_lut(s).ok());
    AUREA_CHECK(!parse_cube_lut(std::string(kMaxCubeFileBytes+1, ' ')).ok());
    AUREA_CHECK(!parse_cube_lut(std::string(4097, ' ')+"\n"+cube_identity()).ok());
}
AUREA_TEST(CubeLut, ImportIsTransactionalAnimatableAndTravelsWithProject) {
    namespace fs = std::filesystem;
    const auto dir = fs::absolute("build/reference/cube-lut-roundtrip"); fs::create_directories(dir);
    const auto input = (dir/"identity.cube").string(); std::ofstream(input) << cube_identity();
    Engine e; EngineConfig config; config.workerCount=1; config.disableAutosave=true;
    AUREA_CHECK(e.initialize(config).ok()); AUREA_CHECK(e.new_project(64,64,30,nullptr).ok());
    auto comp = [&]() { return e.project()->timeline().composition(e.project()->timeline().current()); };
    const auto id = comp()->add_layer(LayerKind::Shape, "LUT");
    Command c; c.type = CommandType::EffectAdd; c.effect_add = {id, effect_type_id(effect_keys::kCubeLut), kInvalidIndex};
    AUREA_CHECK(e.apply_command(c).ok());
    const auto fx = comp()->layer(id)->effects.back().id;
    AUREA_CHECK(e.import_color_lut(id.pack(),fx,input.c_str()).ok());
    auto assetId = AssetId::unpack(comp()->layer(id)->effects.back().params[0].constant.ref);
    AUREA_CHECK(e.project()->asset(assetId)->kind == AssetKind::Lut);
    AUREA_CHECK(e.project()->unreferenced_assets().empty());
    AUREA_CHECK(e.color_lut_name(id.pack(),fx) == "identity.cube");
    const auto count = e.project()->asset_count();
    std::ofstream(dir/"broken.cube") << "LUT_3D_SIZE 66\n";
    AUREA_CHECK(!e.import_color_lut(id.pack(),fx,(dir/"broken.cube").string().c_str()).ok());
    AUREA_CHECK_EQ(e.project()->asset_count(),count);
    AUREA_CHECK_EQ(comp()->layer(id)->effects.back().params[0].constant.ref,assetId.pack());
    auto& t = comp()->layer(id)->tracks.get_or_create(TrackProperty::EffectParam,fx,param_track_key(1,0));
    t.set(FrameIndex{0},0,Interpolation::Linear); t.set(FrameIndex{30},100,Interpolation::Linear);
    const auto project = (dir/"source.aurea").string(); AUREA_CHECK(e.save_project(project.c_str()).ok());
    std::vector<package::MediaRef> refs; AUREA_CHECK(e.project_file_media(project.c_str(),refs).ok());
    AUREA_CHECK_EQ(refs.size(),1u);
    std::vector<package::MediaFile> files;
    for(const auto& r:refs) files.push_back({r.stored,r.resolved,r.name});
    const auto archive=(dir/"export.aureaproj").string();
    AUREA_CHECK(package::write_package(project,archive,"LUT","test",files).ok());
    const auto reopened=(dir/"reopened.aurea").string(); fs::remove(reopened);
    package::ImportResult imported;
    AUREA_CHECK(package::read_package(archive,reopened,(dir/"media").string(),imported).ok());
    AUREA_CHECK_EQ(imported.relinked,1u); AUREA_CHECK_EQ(imported.missing,0u);
    AUREA_CHECK(e.load_project(reopened.c_str()).ok());
    const auto* layer = comp()->layer(id); AUREA_CHECK(layer != nullptr);
    if (layer) {
        const auto& effect=layer->effects.back();
        const auto* params=e.effects().params(effect.type);
        AUREA_CHECK_NEAR(evaluate_param(layer->tracks,effect,1,params->at(1),FrameIndex{15}).v[0],50,.001);
        const auto* asset=e.project()->asset(AssetId::unpack(effect.params[0].constant.ref));
        AUREA_CHECK(asset && read_cube_lut(asset->sourcePath).ok());
    }
    e.shutdown();
}
