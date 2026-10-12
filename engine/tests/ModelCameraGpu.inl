// Câmeras do arquivo do modelo 3D na timeline (0.1.8): um glTF com uma câmera
// animada vira uma camada de câmera filha do modelo, ligada (não havia outra),
// e a vista da composição é a da câmera do arquivo em cada instante.
namespace {
std::string write_camera_gltf() {
    const f32 pos[12] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    const u16 idx[6] = {0, 1, 2, 0, 2, 3};
    const f32 times[2] = {0.0f, 1.0f};
    const f32 moves[6] = {0.0f, 0.0f, 5.0f, 2.0f, 0.0f, 5.0f};
    std::vector<u8> bin(48 + 12 + 8 + 24);
    std::memcpy(bin.data(), pos, 48);
    std::memcpy(bin.data() + 48, idx, 12);
    std::memcpy(bin.data() + 60, times, 8);
    std::memcpy(bin.data() + 68, moves, 24);
    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],)";
    json += R"("nodes":[{"mesh":0},{"name":"CamArquivo","camera":0,"translation":[0,0,5]}],)";
    json += R"("cameras":[{"name":"Camera Blender","type":"perspective","perspective":{"yfov":0.8,"znear":0.1}}],)";
    json += R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)";
    json += R"("animations":[{"name":"voo","samplers":[{"input":2,"output":3,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}],)";
    json += R"("buffers":[{"byteLength":92,"uri":"data:application/octet-stream;base64,)" + gltf_b64(bin) + R"("}],)";
    json += R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12},)";
    json += R"({"buffer":0,"byteOffset":60,"byteLength":8},{"buffer":0,"byteOffset":68,"byteLength":24}],)";
    json += R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},)";
    json += R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"},)";
    json += R"({"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)";
    json += R"({"bufferView":3,"componentType":5126,"count":2,"type":"VEC3"}]})";
    const std::string path = "aurea_teste_camera_modelo.gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    return path;
}
} // namespace

AUREA_TEST(ModelCameraGpu, FileCameraBecomesATimelineCameraThatFollowsTheAnimation) {
    AUREA_REQUIRE_GPU();
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "camera do modelo").ok());
    ModelImport req;
    req.path = write_camera_gltf();
    auto imported = e.import_model(req);
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) { e.shutdown(); return; }
    const u64 modelId = *imported;
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* camera = nullptr;
    comp->layers().for_each([&](LayerId, const Layer& l) { if (l.kind == LayerKind::Camera) camera = &l; });
    AUREA_CHECK(camera != nullptr);
    if (!camera) { e.shutdown(); return; }
    AUREA_CHECK(camera->name == "Camera Blender");
    AUREA_CHECK(camera->parent == LayerId::unpack(modelId));
    AUREA_CHECK(camera->visible && camera->camera.active);
    AUREA_CHECK_NEAR(camera->camera.fov, 0.8f / kDeg2Rad, 0.01f);
    AUREA_CHECK(camera->tracks.find(TrackProperty::PositionX) != nullptr);   // anda: keyframes
    AUREA_CHECK_EQ(e.model_camera_count(modelId), 1u);
    const Layer* model = comp->layer(LayerId::unpack(modelId));
    const f32 s = model->model.unitScale;
    // O centro do quadrado (origem do arquivo) visto pela câmera do arquivo:
    // no início, bem à frente; em 0,5 s a câmera andou 1 m para a direita
    // (x do arquivo = x da composição) e o quadrado fica 1 m à esquerda.
    const Vec3 center{model->transform.position.x, model->transform.position.y, model->transform.position.z};
    auto in_view = [&](i64 frame) {
        const scene3d::SceneCamera cam = comp_camera(*comp, FrameIndex{model->start.value + frame}, 640, 360);
        return cam.view.transform_point(center);
    };
    const Vec3 a = in_view(0), b = in_view(15);   // 0,5 s (o clipe de 1 s volta ao início no 30)
    std::printf("    camera do arquivo: centro na vista (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f), escala %.1f px/m\n",
                a.x, a.y, a.z, b.x, b.y, b.z, s);
    AUREA_CHECK_NEAR(a.x, 0.0f, 0.5f);
    AUREA_CHECK_NEAR(a.y, 0.0f, 0.5f);
    AUREA_CHECK_NEAR(a.z, 5.0f * s, 1.0f);    // olha PARA o quadrado (z da vista > 0)
    AUREA_CHECK_NEAR(b.x, -1.0f * s, 1.0f);
    AUREA_CHECK_NEAR(b.z, 5.0f * s, 1.0f);
    // Mover o modelo leva a câmera junto (filha): a vista não muda.
    Layer* movable = comp->layer(LayerId::unpack(modelId));
    movable->transform.position.x += 100.0f;
    const Vec3 moved{movable->transform.position.x, movable->transform.position.y, movable->transform.position.z};
    const scene3d::SceneCamera cam = comp_camera(*comp, FrameIndex{model->start.value}, 640, 360);
    const Vec3 c = cam.view.transform_point(moved);
    AUREA_CHECK_NEAR(c.x, a.x, 0.5f);
    AUREA_CHECK_NEAR(c.z, a.z, 1.0f);
    // Pôr de novo: outra câmera, que nasce desligada (já há uma ligada).
    AUREA_CHECK_EQ(e.add_model_cameras(modelId), 1u);
    u32 cameras = 0, visible = 0;
    comp->layers().for_each([&](LayerId, const Layer& l) {
        if (l.kind != LayerKind::Camera) return;
        ++cameras;
        visible += l.visible ? 1u : 0u;
    });
    AUREA_CHECK_EQ(cameras, 2u);
    AUREA_CHECK_EQ(visible, 1u);
    e.shutdown();
    std::remove(req.path.c_str());
}
