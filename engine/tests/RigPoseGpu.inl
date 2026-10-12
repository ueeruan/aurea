// Posar o rig 3D na cena (0.1.8): um glTF com dois ossos e uma barra com skin.
// Girar o osso raiz pela API (o mesmo caminho do dedo na cena) deforma a malha
// na imagem renderizada; o keyframe de todos os ossos e o reset funcionam.
namespace {
std::string write_rig_gltf() {
    // Barra vertical de y = 0 a 2 (x ±0,15): base presa no osso 0, topo no 1.
    const f32 pos[12] = {-0.15f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.15f, 2.0f, 0.0f, -0.15f, 2.0f, 0.0f};
    const u16 idx[6] = {0, 1, 2, 0, 2, 3};
    const u8 joints[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 weights[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 ibm[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
                         1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    std::vector<u8> bin(268);
    std::memcpy(bin.data(), pos, 48);
    std::memcpy(bin.data() + 48, idx, 12);
    std::memcpy(bin.data() + 60, joints, 16);
    std::memcpy(bin.data() + 76, weights, 64);
    std::memcpy(bin.data() + 140, ibm, 128);
    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)";
    json += R"("nodes":[{"mesh":0,"skin":0},{"name":"raiz","children":[2]},{"name":"ponta","translation":[0,1,0]}],)";
    json += R"("skins":[{"joints":[1,2],"inverseBindMatrices":4}],)";
    json += R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1,"material":0}]}],)";
    json += R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"metallicFactor":0,"roughnessFactor":1},"doubleSided":true,"extensions":{"KHR_materials_unlit":{}}}],)";
    json += R"("extensionsUsed":["KHR_materials_unlit"],)";
    json += R"("buffers":[{"byteLength":268,"uri":"data:application/octet-stream;base64,)" + gltf_b64(bin) + R"("}],)";
    json += R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12},)";
    json += R"({"buffer":0,"byteOffset":60,"byteLength":16},{"buffer":0,"byteOffset":76,"byteLength":64},{"buffer":0,"byteOffset":140,"byteLength":128}],)";
    json += R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-0.15,0,0],"max":[0.15,2,0]},)";
    json += R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},)";
    json += R"({"bufferView":2,"componentType":5121,"count":4,"type":"VEC4"},)";
    json += R"({"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"},)";
    json += R"({"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}]})";
    const std::string path = "aurea_teste_rig_pose.gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    return path;
}

/// Caixa dos pixels acesos (vermelho > 128): largura e altura.
std::pair<u32, u32> lit_box(const std::vector<u16>& half, u32 w, u32 h) {
    u32 x0 = w, x1 = 0, y0 = h, y1 = 0;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            if (half_to_float(half[(static_cast<usize>(y) * w + x) * 4]) < 0.5f) continue;
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
    return x1 >= x0 && y1 >= y0 ? std::pair<u32, u32>{x1 - x0 + 1, y1 - y0 + 1} : std::pair<u32, u32>{0, 0};
}
} // namespace

AUREA_TEST(RigPoseGpu, TwistingTheRootBoneBendsTheSkinnedMeshAndKeysAllBones) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 160, H = 160;
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(W, H, 30.0, "rig").ok());
    ModelImport req;
    req.path = write_rig_gltf();
    auto imported = e.import_model(req);
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) { e.shutdown(); return; }
    const u64 id = *imported;
    TextureDesc d;
    d.width = W; d.height = H; d.format = SurfaceFormat::RGBA16F; d.renderTarget = true; d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(static_cast<usize>(W) * H * 4);
    auto render = [&] {
        AUREA_CHECK(e.render_offscreen(target, W, H).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), W * 8).ok());
        return lit_box(half, W, H);
    };
    // Dois ossos; a ponta tem a raiz como pai.
    std::vector<f32> bones(e.query_model_bones(id, nullptr, 0));
    AUREA_CHECK_EQ(bones.size(), usize{2} * Engine::kBoneFloats);
    (void)e.query_model_bones(id, bones.data(), static_cast<u32>(bones.size()));
    AUREA_CHECK_EQ(bones[0], 1.0f);
    AUREA_CHECK_EQ(bones[Engine::kBoneFloats + 0], 2.0f);
    AUREA_CHECK_EQ(bones[Engine::kBoneFloats + 1], 0.0f);
    const f32 tipX = bones[Engine::kBoneFloats + 7], tipY = bones[Engine::kBoneFloats + 8];
    const auto upright = render();
    // Giro de 90° em volta do olhar na RAIZ: a barra deita (e a ponta vai junto).
    AUREA_CHECK(e.drag_bone(id, 1, 0.0f, 0.0f, 90.0f, false));
    const auto lying = render();
    (void)e.query_model_bones(id, bones.data(), static_cast<u32>(bones.size()));
    std::printf("    rig: barra %ux%u -> %ux%u; ponta (%.0f, %.0f) -> (%.0f, %.0f)\n", upright.first, upright.second,
                lying.first, lying.second, tipX, tipY, bones[Engine::kBoneFloats + 7], bones[Engine::kBoneFloats + 8]);
    AUREA_CHECK(upright.second > upright.first * 2);
    AUREA_CHECK(lying.first > lying.second * 2);
    AUREA_CHECK(std::fabs(bones[Engine::kBoneFloats + 7] - tipX) > 10.0f);
    // Keyframe de todos os ossos no cabeçote; o segundo toque tira.
    AUREA_CHECK_EQ(e.toggle_bone_key(id, -1), 1);
    (void)e.query_model_bones(id, bones.data(), static_cast<u32>(bones.size()));
    AUREA_CHECK_EQ(bones[6], 1.0f);
    AUREA_CHECK_EQ(bones[Engine::kBoneFloats + 6], 1.0f);
    AUREA_CHECK_EQ(e.toggle_bone_key(id, -1), 0);
    // Resetar a pose volta a barra em pé.
    AUREA_CHECK(e.reset_bone_pose(id, -1));
    const auto reset = render();
    AUREA_CHECK_EQ(reset.first, upright.first);
    AUREA_CHECK_EQ(reset.second, upright.second);
    e.gpu()->destroy_texture(target);
    e.shutdown();
    std::remove(req.path.c_str());
}
