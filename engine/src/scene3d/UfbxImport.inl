// =============================================================================
//  Aurea / scene3d / UfbxImport.inl — FBX e OBJ (ufbx) → SceneAsset.
//
//  Incluído no fim de GltfImporter.cpp: a mesma unidade de tradução, para usar
//  as mesmas etapas (normais planas, tangentes, redução de textura, validação
//  final e otimização) sem duplicar código — o SceneAsset que sai daqui é
//  indistinguível do de um glTF para o renderer.
//
//  Convenções: a ufbx converte para Y para cima, destro, metros (as do glTF),
//  aplica as "geometry transforms" do FBX na própria malha e triangula.
//  Cores de material do FBX são de tela (sRGB) e viram lineares aqui.
// =============================================================================

namespace {

Vec3 to_vec3(ufbx_vec3 v) noexcept { return Vec3{static_cast<f32>(v.x), static_cast<f32>(v.y), static_cast<f32>(v.z)}; }

f32 srgb_to_linear1(f32 c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Mat4 to_mat4(const ufbx_matrix& m) noexcept {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        r.col[c] = Vec4{static_cast<f32>(m.cols[c].x), static_cast<f32>(m.cols[c].y), static_cast<f32>(m.cols[c].z),
                        c == 3 ? 1.0f : 0.0f};
    }
    return r;
}

/// Vértice achatado para soldar cantos iguais (meshopt remap).
struct FlatVertex {
    Vec3 p;
    Vec3 n;
    Vec2 uv;
    u32 color;
    u16 joints[4];
    f32 weights[4];
};

struct UfbxBuild {
    const ufbx_scene* scene = nullptr;
    std::string baseDir;
    const ImportOptions* options = nullptr;
    SceneAsset* A = nullptr;
    std::unordered_map<const ufbx_texture*, i32> images;
    u32 imagesUsed = 0;
    std::vector<std::string> warnings;

    i32 image_for(const ufbx_texture* t) {
        if (!t) return -1;
        if (auto it = images.find(t); it != images.end()) return it->second;
        std::vector<u8> bytes;
        const u8* data = nullptr;
        usize size = 0;
        if (t->content.size > 0) {
            data = static_cast<const u8*>(t->content.data);
            size = t->content.size;
        } else {
            // Arquivo ao lado do modelo: o nome relativo primeiro, depois só o
            // nome do arquivo (caminho absoluto de outra máquina não existe aqui).
            std::vector<std::string> tries;
            if (t->relative_filename.length) tries.push_back(baseDir + std::string(t->relative_filename.data, t->relative_filename.length));
            if (t->filename.length) tries.push_back(std::string(t->filename.data, t->filename.length));
            for (const ufbx_string* s : {&t->relative_filename, &t->filename, &t->absolute_filename}) {
                if (!s->length) continue;
                std::string full(s->data, s->length);
                const usize slash = full.find_last_of("/\\");
                tries.push_back(baseDir + (slash == std::string::npos ? full : full.substr(slash + 1)));
            }
            for (const std::string& f : tries) {
                if (read_file(f, bytes)) break;
            }
            data = bytes.data();
            size = bytes.size();
        }
        i32 index = -1;
        if (data && size) {
            int w = 0, h = 0, comp = 0;
            stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
            if (px) {
                Image img;
                img.name = std::string(t->name.data, t->name.length);
                img.width = static_cast<u32>(w);
                img.height = static_cast<u32>(h);
                img.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
                img.hasAlpha = comp == 2 || comp == 4;
                stbi_image_free(px);
                if (options->maxTextureSize) downscale_to(img, options->maxTextureSize);
                index = static_cast<i32>(A->images.size());
                A->images.push_back(std::move(img));
                ++imagesUsed;
            }
        }
        if (index < 0) {
            const std::string w = "textura ausente: " + std::string(t->relative_filename.data ? t->relative_filename.data : t->name.data);
            if (std::find(warnings.begin(), warnings.end(), w) == warnings.end()) warnings.push_back(w);
        }
        images.emplace(t, index);
        return index;
    }

    Material material(const ufbx_material* m) {
        Material out;
        out.name = std::string(m->name.data, m->name.length);
        const ufbx_material_pbr_maps& p = m->pbr;
        Vec4 base{1, 1, 1, 1};
        if (p.base_color.has_value) {
            base = Vec4{static_cast<f32>(p.base_color.value_vec4.x), static_cast<f32>(p.base_color.value_vec4.y),
                        static_cast<f32>(p.base_color.value_vec4.z), 1.0f};
            if (p.base_color.value_components >= 4) base.w = static_cast<f32>(p.base_color.value_vec4.w);
        }
        const f32 factor = p.base_factor.has_value ? static_cast<f32>(p.base_factor.value_real) : 1.0f;
        out.baseColor = Vec4{srgb_to_linear1(base.x) * factor, srgb_to_linear1(base.y) * factor,
                             srgb_to_linear1(base.z) * factor, base.w};
        if (p.base_color.texture && p.base_color.texture_enabled) out.baseColorTex.image = image_for(p.base_color.texture);
        else if (p.base_color.texture) out.baseColorTex.image = image_for(p.base_color.texture);
        out.roughness = p.roughness.has_value ? std::clamp(static_cast<f32>(p.roughness.value_real), 0.0f, 1.0f) : 0.6f;
        out.metallic = p.metalness.has_value ? std::clamp(static_cast<f32>(p.metalness.value_real), 0.0f, 1.0f) : 0.0f;
        if (p.normal_map.texture) out.normalTex.image = image_for(p.normal_map.texture);
        if (p.ambient_occlusion.texture) out.occlusionTex.image = image_for(p.ambient_occlusion.texture);
        if (p.emission_color.has_value) {
            const f32 ef = p.emission_factor.has_value ? static_cast<f32>(p.emission_factor.value_real) : 1.0f;
            out.emissive = Vec3{srgb_to_linear1(static_cast<f32>(p.emission_color.value_vec3.x)),
                                srgb_to_linear1(static_cast<f32>(p.emission_color.value_vec3.y)),
                                srgb_to_linear1(static_cast<f32>(p.emission_color.value_vec3.z))} * ef;
        }
        if (p.emission_color.texture) out.emissiveTex.image = image_for(p.emission_color.texture);
        const f32 opacity = p.opacity.has_value ? static_cast<f32>(p.opacity.value_real) : 1.0f;
        if (opacity < 0.999f) {
            out.alphaMode = AlphaMode::Blend;
            out.baseColor.w *= std::clamp(opacity, 0.0f, 1.0f);
        } else if (out.baseColorTex.valid() && A->images[static_cast<usize>(out.baseColorTex.image)].hasAlpha) {
            out.alphaMode = AlphaMode::Mask;   // folhagem/recorte: canal alfa da textura
        }
        // FBX costuma vir sem face de trás modelada (planos, cabelo, folhas).
        out.doubleSided = true;
        return out;
    }
};

} // namespace

ImportResult import_ufbx_file(const std::string& path, const ImportOptions& options, ImportProgress* progress) {
    const u64 tParse = monotonic_ns();
    set_phase(progress, ImportPhase::Parsing);
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.load_external_files = true;             // .mtl do OBJ
    opts.ignore_missing_external_files = true;   // .mtl ausente: cinza + aviso, não recusa
    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        std::string why(err.description.data, err.description.length);
        return fail_result(err.type == UFBX_ERROR_FILE_NOT_FOUND ? ImportError::FileNotFound : ImportError::InvalidFormat,
                           "arquivo FBX/OBJ ilegivel: " + why);
    }
    struct Guard { ufbx_scene* s; ~Guard() { ufbx_free_scene(s); } } guard{scene};
    if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");

    auto asset = std::make_unique<SceneAsset>();
    SceneAsset& A = *asset;
    A.stats.parseMs = ms_since(tParse);
    UfbxBuild b;
    b.scene = scene;
    b.baseDir = dir_of(path);
    b.options = &options;
    b.A = &A;

    // --- Materiais --------------------------------------------------------------
    const u64 tImg = monotonic_ns();
    set_phase(progress, ImportPhase::Textures);
    for (usize i = 0; i < scene->materials.count; ++i) A.materials.push_back(b.material(scene->materials.data[i]));
    A.stats.imagesMs = ms_since(tImg);

    // --- Nós (a raiz da ufbx leva a conversão de eixos/unidade) -------------------
    A.nodes.resize(scene->nodes.count);
    for (usize i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        Node& o = A.nodes[i];
        o.name = std::string(n->name.data, n->name.length);
        o.parent = n->parent ? static_cast<i32>(n->parent->typed_id) : -1;
        for (usize c = 0; c < n->children.count; ++c) o.children.push_back(static_cast<i32>(n->children.data[c]->typed_id));
        o.translation = to_vec3(n->local_transform.translation);
        o.rotation = Quat{static_cast<f32>(n->local_transform.rotation.x), static_cast<f32>(n->local_transform.rotation.y),
                          static_cast<f32>(n->local_transform.rotation.z), static_cast<f32>(n->local_transform.rotation.w)};
        o.scale = to_vec3(n->local_transform.scale);
        if (!n->parent) A.roots.push_back(static_cast<i32>(i));
    }

    // --- Malhas -------------------------------------------------------------------
    const u64 tGeo = monotonic_ns();
    set_phase(progress, ImportPhase::Geometry);
    std::unordered_map<const ufbx_mesh*, i32> meshIndex;
    std::unordered_map<const ufbx_mesh*, i32> skinOfMesh;
    std::vector<u32> tri;
    for (usize mi = 0; mi < scene->meshes.count; ++mi) {
        if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
        const ufbx_mesh* m = scene->meshes.data[mi];
        tri.resize(m->max_face_triangles * 3);
        const ufbx_skin_deformer* skin = m->skin_deformers.count ? m->skin_deformers.data[0] : nullptr;
        if (skin && skin->clusters.count) {
            Skin s;
            s.name = std::string(m->name.data, m->name.length);
            for (usize c = 0; c < skin->clusters.count; ++c) {
                const ufbx_skin_cluster* cl = skin->clusters.data[c];
                s.joints.push_back(cl->bone_node ? static_cast<i32>(cl->bone_node->typed_id) : -1);
                s.inverseBind.push_back(to_mat4(cl->geometry_to_bone));
            }
            skinOfMesh[m] = static_cast<i32>(A.skins.size());
            A.skins.push_back(std::move(s));
        }
        Mesh mesh;
        mesh.name = std::string(m->name.data, m->name.length);
        for (usize pi = 0; pi < m->material_parts.count; ++pi) {
            const ufbx_mesh_part& part = m->material_parts.data[pi];
            if (part.num_triangles == 0) continue;
            std::vector<FlatVertex> corners;
            corners.reserve(part.num_triangles * 3);
            for (usize fi = 0; fi < part.face_indices.count; ++fi) {
                const ufbx_face face = m->faces.data[part.face_indices.data[fi]];
                const u32 nt = ufbx_triangulate_face(tri.data(), tri.size(), m, face);
                for (u32 k = 0; k < nt * 3; ++k) {
                    const u32 ix = tri[k];
                    FlatVertex v{};
                    v.p = to_vec3(ufbx_get_vertex_vec3(&m->vertex_position, ix));
                    v.n = m->vertex_normal.exists ? to_vec3(ufbx_get_vertex_vec3(&m->vertex_normal, ix)) : Vec3{0, 0, 0};
                    if (m->vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&m->vertex_uv, ix);
                        v.uv = Vec2{static_cast<f32>(uv.x), 1.0f - static_cast<f32>(uv.y)};   // FBX: origem embaixo
                    }
                    v.color = 0xFFFFFFFFu;
                    if (m->vertex_color.exists) {
                        const ufbx_vec4 c = ufbx_get_vertex_vec4(&m->vertex_color, ix);
                        v.color = pack_rgba8(Vec4{srgb_to_linear1(static_cast<f32>(c.x)), srgb_to_linear1(static_cast<f32>(c.y)),
                                                  srgb_to_linear1(static_cast<f32>(c.z)), static_cast<f32>(c.w)});
                    }
                    if (skin) {
                        const u32 vi = m->vertex_indices.data[ix];
                        if (vi < skin->vertices.count) {
                            const ufbx_skin_vertex sv = skin->vertices.data[vi];
                            f32 total = 0.0f;
                            const u32 nw = std::min<u32>(4, sv.num_weights);   // já ordenados do maior peso
                            for (u32 w = 0; w < nw; ++w) {
                                const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                                v.joints[w] = static_cast<u16>(sw.cluster_index);
                                v.weights[w] = static_cast<f32>(sw.weight);
                                total += v.weights[w];
                            }
                            if (total > 0.0f) for (f32& w : v.weights) w /= total;
                            else v.weights[0] = 1.0f;
                        } else {
                            v.weights[0] = 1.0f;
                        }
                    }
                    corners.push_back(v);
                }
            }
            if (corners.empty()) continue;
            // Solda cantos idênticos (FBX guarda por canto; o GPU quer indexado).
            std::vector<u32> remap(corners.size());
            const usize unique = meshopt_generateVertexRemap(remap.data(), nullptr, corners.size(), corners.data(),
                                                             corners.size(), sizeof(FlatVertex));
            std::vector<FlatVertex> verts(unique);
            meshopt_remapVertexBuffer(verts.data(), corners.data(), corners.size(), sizeof(FlatVertex), remap.data());
            Primitive p;
            p.indices.assign(remap.begin(), remap.end());
            p.positions.reserve(unique);
            bool hasNormals = m->vertex_normal.exists;
            for (const FlatVertex& v : verts) {
                p.positions.push_back(v.p);
                if (hasNormals) p.normals.push_back(v.n);
                p.uv0.push_back(v.uv);
                if (m->vertex_color.exists) p.colors.push_back(v.color);
                if (skin) {
                    for (int c = 0; c < 4; ++c) p.joints.push_back(v.joints[c]);
                    p.weights.push_back(Vec4{v.weights[0], v.weights[1], v.weights[2], v.weights[3]});
                }
                p.bounds.add(v.p);
            }
            if (!hasNormals) flat_normals(p);
            const i32 matIndex = part.index < m->materials.count && m->materials.data[part.index]
                               ? static_cast<i32>(m->materials.data[part.index]->typed_id) : -1;
            p.material = matIndex;
            if (matIndex >= 0 && A.materials[static_cast<usize>(matIndex)].normalTex.valid() && m->vertex_uv.exists) {
                generate_tangents(p, p.uv0);
            }
            mesh.bounds.add(p.bounds.min);
            mesh.bounds.add(p.bounds.max);
            mesh.primitives.push_back(std::move(p));
        }
        if (mesh.primitives.empty()) continue;
        meshIndex[m] = static_cast<i32>(A.meshes.size());
        A.meshes.push_back(std::move(mesh));
    }
    for (usize i = 0; i < scene->nodes.count; ++i) {
        const ufbx_node* n = scene->nodes.data[i];
        if (!n->mesh) continue;
        if (auto it = meshIndex.find(n->mesh); it != meshIndex.end()) A.nodes[i].mesh = it->second;
        if (auto it = skinOfMesh.find(n->mesh); it != skinOfMesh.end()) A.nodes[i].skin = it->second;
    }
    A.stats.geometryMs = ms_since(tGeo);

    // --- Animações: cada "take" assado em chaves lineares por nó ------------------
    for (usize si = 0; si < scene->anim_stacks.count; ++si) {
        const ufbx_anim_stack* st = scene->anim_stacks.data[si];
        ufbx_bake_opts bo{};
        ufbx_error berr{};
        ufbx_baked_anim* baked = ufbx_bake_anim(scene, st->anim, &bo, &berr);
        if (!baked) continue;
        Animation an;
        an.name = std::string(st->name.data, st->name.length);
        an.duration = static_cast<f32>(baked->playback_duration);
        const f64 t0 = baked->playback_time_begin;
        auto add_vec3 = [&](i32 node, AnimPath path, const ufbx_baked_vec3_list& keys) {
            if (keys.count == 0) return;
            AnimSampler s;
            s.components = 3;
            for (usize k = 0; k < keys.count; ++k) {
                s.times.push_back(static_cast<f32>(keys.data[k].time - t0));
                const Vec3 v = to_vec3(keys.data[k].value);
                s.values.insert(s.values.end(), {v.x, v.y, v.z});
            }
            an.channels.push_back(AnimChannel{node, path, static_cast<u32>(an.samplers.size())});
            an.samplers.push_back(std::move(s));
        };
        for (usize bn = 0; bn < baked->nodes.count; ++bn) {
            const ufbx_baked_node& nd = baked->nodes.data[bn];
            const i32 node = static_cast<i32>(nd.typed_id);
            // A constant channel belongs to this take, not necessarily the
            // scene's bind pose. Keep even one baked key so switching takes
            // cannot silently restore another take's translation/scale/rotation.
            add_vec3(node, AnimPath::Translation, nd.translation_keys);
            add_vec3(node, AnimPath::Scale, nd.scale_keys);
            if (nd.rotation_keys.count > 0) {
                AnimSampler s;
                s.components = 4;
                for (usize k = 0; k < nd.rotation_keys.count; ++k) {
                    s.times.push_back(static_cast<f32>(nd.rotation_keys.data[k].time - t0));
                    const ufbx_quat q = nd.rotation_keys.data[k].value;
                    s.values.insert(s.values.end(), {static_cast<f32>(q.x), static_cast<f32>(q.y), static_cast<f32>(q.z),
                                                     static_cast<f32>(q.w)});
                }
                an.channels.push_back(AnimChannel{node, AnimPath::Rotation, static_cast<u32>(an.samplers.size())});
                an.samplers.push_back(std::move(s));
            }
        }
        ufbx_free_baked_anim(baked);
        if (!an.channels.empty() && an.duration > 0.0f) A.animations.push_back(std::move(an));
    }

    A.warnings.insert(A.warnings.end(), b.warnings.begin(), b.warnings.end());
    ImportResult r = finalize_asset(std::move(asset), options, progress, b.imagesUsed);
    if (r.ok()) {
        const usize slash = path.find_last_of("/\\");
        r.asset->sourceName = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    return r;
}

ImportResult import_scene_file(const std::string& path, const ImportOptions& options, ImportProgress* progress) {
    std::string ext;
    const usize dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == "fbx" || ext == "obj") return import_ufbx_file(path, options, progress);
    return import_gltf_file(path, options, progress);
}
