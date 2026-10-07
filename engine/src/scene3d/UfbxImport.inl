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

/// Só o nome do arquivo de um caminho gravado no modelo (tira pastas de
/// qualquer sistema: "C:\tex\a.png", "../tex/a.png" → "a.png").
std::string file_name_only(const std::string& full) {
    const usize slash = full.find_last_of("/\\");
    return slash == std::string::npos ? full : full.substr(slash + 1);
}

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Arquivos da pasta do modelo, pelo nome em minúsculas. Texturas escolhidas
/// junto com o modelo no celular chegam aqui com o nome do seletor, que pode
/// não bater em maiúsculas com o que o FBX/MTL grava ("Wood.PNG" × "wood.png");
/// o sistema de arquivos do aparelho diferencia — a busca aqui não.
struct FolderIndex {
    std::string dir;
    bool listed = false;
    std::unordered_map<std::string, std::string> byLower;   ///< nome minúsculo → caminho real
    std::vector<std::string> mtl;                           ///< .mtl da pasta

    void list() {
        if (listed) return;
        listed = true;
        if (dir.empty()) return;
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const auto u8 = it->path().filename().u8string();
            const std::string name(u8.begin(), u8.end());
            const std::string low = lower_ascii(name);
            byLower.emplace(low, dir + name);
            if (low.size() > 4 && low.compare(low.size() - 4, 4, ".mtl") == 0) mtl.push_back(dir + name);
        }
        std::sort(mtl.begin(), mtl.end());
    }
    /// Caminho real do arquivo com esse nome (sem pasta, sem diferenciar
    /// maiúsculas). Vazio = não está na pasta.
    std::string find(const std::string& anyPath) {
        list();
        const std::string name = file_name_only(anyPath);
        if (name.empty()) return {};
        const auto it = byLower.find(lower_ascii(name));
        return it == byLower.end() ? std::string{} : it->second;
    }
};

/// Abre os arquivos externos do OBJ (.mtl) pela ufbx: o caminho gravado
/// primeiro; senão o mesmo nome na pasta do modelo sem diferenciar
/// maiúsculas; senão o ÚNICO .mtl da pasta (o app guarda o .obj com outro
/// nome, e o .mtl escolhido junto é o dele).
struct ExternalOpener {
    FolderIndex* folder = nullptr;
    std::vector<std::string> mtlRequested;   ///< nomes pedidos (para o aviso)
    bool mtlOpened = false;

    static bool open(void* user, ufbx_stream* stream, const char* path, size_t pathLen, const ufbx_open_file_info* info) {
        auto* self = static_cast<ExternalOpener*>(user);
        const bool mtl = info && info->type == UFBX_OPEN_FILE_OBJ_MTL;
        if (ufbx_open_file_ctx(stream, info ? info->context : 0, path, pathLen, nullptr, nullptr)) {
            if (mtl) self->mtlOpened = true;
            return true;
        }
        if (!mtl || !self->folder) return false;
        const std::string wanted(path, pathLen);
        const std::string name = file_name_only(wanted);
        if (std::find(self->mtlRequested.begin(), self->mtlRequested.end(), name) == self->mtlRequested.end())
            self->mtlRequested.push_back(name);
        std::string real = self->folder->find(wanted);
        if (real.empty() && self->folder->mtl.size() == 1) real = self->folder->mtl.front();
        if (real.empty()) return false;
        if (!ufbx_open_file_ctx(stream, info->context, real.c_str(), real.size(), nullptr, nullptr)) return false;
        self->mtlOpened = true;
        return true;
    }
};

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
    FolderIndex* folder = nullptr;
    std::vector<std::string> missing;   ///< nomes (sem pasta) das texturas não achadas
    u64 imageBytes = 0;                 ///< pixels guardados até agora (orçamento)
    u64 heldBytes = 0;                  ///< a cena lida pela ufbx (orçamento)

    /// Decodifica uma imagem (PNG/JPG/TGA/BMP/PSD…) da memória para o asset,
    /// já reduzida ao teto. Com orçamento, a imagem cujo decode (ou a soma
    /// guardada) passaria do teto é pulada com aviso — o modelo entra sem ela.
    i32 decode_image(const ufbx_texture* t, const u8* data, usize size) {
        if (!data || size == 0 || size > static_cast<usize>(INT32_MAX)) return -1;
        const u64 budget = options->memoryBudget;
        Image img;
        u32 w0 = 0, h0 = 0;
        const bool full = budget && imageBytes * 7 / 3 > budget / 2;
        const Decode d = full ? Decode::TooBig
                              : decode_capped(data, size, options->maxTextureSize, decode_room(budget, heldBytes + imageBytes),
                                              img, w0, h0);
        if (d == Decode::Failed) return -1;
        if (d == Decode::TooBig) {
            ++A->stats.texturesSkipped;
            const std::string w = "textura '" + std::string(t->name.data, t->name.length) +
                                  "' grande demais para a memoria deste aparelho: ignorada";
            if (std::find(warnings.begin(), warnings.end(), w) == warnings.end()) warnings.push_back(w);
            return -2;   // achada, mas fora do orçamento (não é "ausente")
        }
        if (img.width != w0 || img.height != h0) ++A->stats.texturesReduced;
        imageBytes += img.rgba.size();
        img.name = std::string(t->name.data, t->name.length);
        const i32 index = static_cast<i32>(A->images.size());
        A->images.push_back(std::move(img));
        ++imagesUsed;
        return index;
    }

    i32 image_for(const ufbx_texture* t) {
        if (!t) return -1;
        if (auto it = images.find(t); it != images.end()) return it->second;
        const ufbx_texture* const key = t;
        // Textura em camadas (Layered): a primeira textura de arquivo dela.
        const ufbx_texture* src = t;
        if (src->type != UFBX_TEXTURE_FILE && src->file_textures.count > 0 && src->file_textures.data[0])
            src = src->file_textures.data[0];
        // Conteúdo EMBUTIDO no FBX binário ("Video" com Content): pode estar na
        // própria textura, no vídeo ligado a ela ou no arquivo deduplicado da
        // cena (o mesmo nome de arquivo usado por várias texturas guarda o
        // conteúdo uma vez só). Decodificado da memória — o caminho gravado
        // (pasta de outra máquina) nem é consultado quando há conteúdo.
        i32 index = -1;
        const ufbx_blob* blobs[3] = {&src->content, src->video ? &src->video->content : nullptr,
                                     src->has_file && src->file_index < scene->texture_files.count
                                         ? &scene->texture_files.data[src->file_index].content : nullptr};
        for (const ufbx_blob* b : blobs) {
            if (index >= 0 || index == -2) break;
            if (b && b->size > 0) index = decode_image(t, static_cast<const u8*>(b->data), b->size);
        }
        t = src;
        std::vector<u8> bytes;
        const u8* data = nullptr;
        usize size = 0;
        if (index == -1) {
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
            bool found = false;
            for (const std::string& f : tries) {
                if (read_file(f, bytes)) { found = true; break; }
            }
            // Mesmo nome na pasta do modelo, sem diferenciar maiúsculas.
            if (!found && folder) {
                for (const ufbx_string* s : {&t->relative_filename, &t->filename, &t->absolute_filename}) {
                    if (!s->length) continue;
                    const std::string real = folder->find(std::string(s->data, s->length));
                    if (!real.empty() && read_file(real, bytes)) { found = true; break; }
                }
            }
            if (!found) {
                // O nome que o usuário precisa escolher: só o arquivo.
                std::string want;
                for (const ufbx_string* s : {&t->relative_filename, &t->filename, &t->absolute_filename}) {
                    if (s->length) want = file_name_only(std::string(s->data, s->length));
                    if (!want.empty()) break;
                }
                if (!want.empty() && std::find(missing.begin(), missing.end(), want) == missing.end())
                    missing.push_back(want);
            }
            data = bytes.data();
            size = bytes.size();
            index = decode_image(t, data, size);
        }
        if (index == -2) {
            index = -1;   // fora do orçamento: o aviso já foi dado
        } else if (index < 0) {
            const std::string w = "textura ausente: " + std::string(t->relative_filename.length ? t->relative_filename.data : t->name.data);
            if (std::find(warnings.begin(), warnings.end(), w) == warnings.end()) warnings.push_back(w);
        }
        images.emplace(key, index);
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
        // Textura difusa: a do mapa PBR; senão a ligada direto ao "DiffuseColor"
        // clássico (alguns exportadores não passam pelo mapeamento PBR).
        const ufbx_texture* baseTex = p.base_color.texture ? p.base_color.texture : m->fbx.diffuse_color.texture;
        if (baseTex) out.baseColorTex.image = image_for(baseTex);
        const bool fbx = scene->metadata.file_format == UFBX_FILE_FORMAT_FBX;
        if (fbx && out.baseColorTex.valid()) {
            // No FBX a textura ligada ao DiffuseColor SUBSTITUI a cor (conexão
            // do Maya; o Blender lê igual): muitos arquivos gravam cor preta ou
            // 0.8 junto com a textura — multiplicar deixava o modelo escuro ou
            // "sem textura". (No OBJ o .mtl manda multiplicar Kd: fica.)
            out.baseColor.x = out.baseColor.y = out.baseColor.z = 1.0f;
        }
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
        // Mesma regra da difusa: textura de emissão com cor preta gravada
        // ficaria apagada (emissivo × textura = 0).
        if (fbx && out.emissiveTex.valid() && out.emissive.x + out.emissive.y + out.emissive.z <= 0.0f) {
            const f32 ef = p.emission_factor.has_value ? static_cast<f32>(p.emission_factor.value_real) : 1.0f;
            out.emissive = Vec3{1.0f, 1.0f, 1.0f} * (ef > 0.0f ? ef : 1.0f);
        }
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

/// Progresso da leitura do arquivo pela ufbx (bytes lidos) e cancelamento.
ufbx_progress_result ufbx_progress_fn_aurea(void* user, const ufbx_progress* p) {
    auto* progress = static_cast<ImportProgress*>(user);
    if (!progress) return UFBX_PROGRESS_CONTINUE;
    if (p && p->bytes_total > 0)
        set_fraction(progress, static_cast<f32>(static_cast<f64>(p->bytes_read) / static_cast<f64>(p->bytes_total)));
    return cancelled(progress) ? UFBX_PROGRESS_CANCEL : UFBX_PROGRESS_CONTINUE;
}

/// Uma parte de material maior que isto é lida em pedaços: os cantos de um
/// pedaço (60 bytes cada) são soldados e simplificados antes do próximo. É o
/// "renderizar por partes" — o pico fica em ~50 MB, não no modelo inteiro.
constexpr usize kUfbxChunkTriangles = 262144;

/// Bytes por triângulo do pico de um pedaço: 3 cantos achatados + remapa +
/// os vértices soldados.
constexpr u32 kUfbxPartBytesPerTriangle = static_cast<u32>(3 * sizeof(FlatVertex) + 3 * 4 + 2 * sizeof(FlatVertex));

} // namespace

ImportResult import_ufbx_file(const std::string& path, const ImportOptions& options, ImportProgress* progress) {
    const u64 tParse = monotonic_ns();
    set_phase(progress, ImportPhase::Parsing);
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    // Herança de escala fora do padrão (Maya "Segment Scale Compensate", 3ds
    // Max/Blender por componente) é comum em esqueletos. O Aurea monta o mundo
    // como T·R·S encadeado (como o glTF); sem compensar, o osso filho herdava
    // a escala do pai que o arquivo mandava ignorar — a pose deixava de bater
    // com a de bind e o personagem saía com membros esticados/corpo torto.
    // A ufbx compensa escalando os filhos (ou cria nós auxiliares de escala
    // quando a escala é animada/não uniforme; os nós e a animação assada
    // incluem esses auxiliares).
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    opts.clean_skin_weights = true;          // pesos negativos/zero/NaN fora
    opts.use_blender_pbr_material = true;    // rugosidade/metal do Blender
    opts.load_external_files = true;             // .mtl do OBJ
    opts.ignore_missing_external_files = true;   // .mtl ausente: cinza + aviso, não recusa
    // OBJ sem `mtllib` (ou com o nome gravado errado): procura "<nome>.mtl" —
    // e, pelo abridor, o ÚNICO .mtl da pasta. É o que deixa o .mtl escolhido
    // DEPOIS do import religar os materiais.
    opts.obj_search_mtl_by_filename = true;
    FolderIndex folder;
    folder.dir = dir_of(path);
    ExternalOpener opener;
    opener.folder = &folder;
    opts.open_file_cb.fn = &ExternalOpener::open;
    opts.open_file_cb.user = &opener;
    opts.progress_cb.fn = &ufbx_progress_fn_aurea;
    opts.progress_cb.user = progress;
    if (options.memoryBudget) {
        // A ufbx respeita teto de memória: passou, devolve erro em vez de o
        // sistema matar o app no meio da leitura.
        const u64 cap = std::min<u64>(options.memoryBudget, static_cast<u64>(SIZE_MAX / 2));
        opts.result_allocator.memory_limit = static_cast<size_t>(cap * 7 / 10);
        opts.temp_allocator.memory_limit = static_cast<size_t>(cap / 2);
    }
    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) {
        std::string why(err.description.data, err.description.length);
        if (err.type == UFBX_ERROR_CANCELLED) return fail_result(ImportError::Cancelled, "cancelado");
        if (err.type == UFBX_ERROR_MEMORY_LIMIT || err.type == UFBX_ERROR_OUT_OF_MEMORY || err.type == UFBX_ERROR_ALLOCATION_LIMIT) {
            return fail_result(ImportError::TooHeavy, "a leitura do arquivo passou de " + mb_text(options.memoryBudget) +
                                                          " (o que este aparelho aguenta)");
        }
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
    b.folder = &folder;
    b.options = &options;
    b.A = &A;
    b.heldBytes = scene->metadata.result_memory_used;

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
        // Geometry helpers carry the mesh; the original instance owns its bindings.
        const ufbx_node* materialNode = n;
        while (materialNode->is_geometry_transform_helper && materialNode->parent)
            materialNode = materialNode->parent;
        if (materialNode->materials.count == 0) materialNode = n;
        for (usize m = 0; m < materialNode->materials.count; ++m)
            o.materials.push_back(materialNode->materials.data[m] ? static_cast<i32>(materialNode->materials.data[m]->typed_id) : -1);
        if (!n->parent) A.roots.push_back(static_cast<i32>(i));
    }

    // --- Orçamento: contagens exatas da cena lida, antes de montar as malhas -------
    u64 totalTris = 0, totalVerts = 0, largestPart = 0, totalFaces = 0;
    for (usize mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* m = scene->meshes.data[mi];
        totalTris += m->num_triangles;
        totalVerts += m->num_vertices;
        for (usize pi = 0; pi < m->material_parts.count; ++pi) {
            largestPart = std::max<u64>(largestPart, std::min<u64>(m->material_parts.data[pi].num_triangles, kUfbxChunkTriangles));
            totalFaces += m->material_parts.data[pi].face_indices.count;
        }
    }
    A.stats.sourceTriangles = static_cast<u32>(std::min<u64>(totalTris, UINT32_MAX));
    f32 keep = 1.0f;
    if (options.maxTriangles && totalTris > options.maxTriangles)
        keep = static_cast<f32>(static_cast<f64>(options.maxTriangles) / static_cast<f64>(totalTris));
    if (options.memoryBudget) {
        ModelCost c;
        c.valid = true;
        c.parseBytes = scene->metadata.result_memory_used;
        c.triangles = totalTris;
        c.vertices = totalVerts;
        c.largestPartTriangles = largestPart;
        c.partBytesPerTriangle = kUfbxPartBytesPerTriangle;
        c.textures = static_cast<u32>(A.images.size());
        c.texturePixels = b.imageBytes / 4;   // já decodificadas (e reduzidas) acima
        ModelBudget mb;
        mb.memoryBytes = options.memoryBudget;
        mb.maxTriangles = options.maxTriangles;
        mb.maxTextureSize = options.maxTextureSize;
        const u64 peak = estimate_import_peak(c, mb);
        if (peak > options.memoryBudget) {
            return fail_result(ImportError::TooHeavy, std::to_string(totalTris) + " triangulos precisam de ~" + mb_text(peak) +
                                                          "; este aparelho aguenta " + mb_text(options.memoryBudget));
        }
    }

    // --- Malhas -------------------------------------------------------------------
    const u64 tGeo = monotonic_ns();
    set_phase(progress, keep < 1.0f ? ImportPhase::Simplifying : ImportPhase::Geometry);
    u64 facesDone = 0;
    bool stop = false;
    std::unordered_map<const ufbx_mesh*, i32> meshIndex;
    std::unordered_map<const ufbx_mesh*, i32> skinOfMesh;
    std::vector<u32> tri;
    for (usize mi = 0; mi < scene->meshes.count; ++mi) {
        if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
        const ufbx_mesh* m = scene->meshes.data[mi];
        tri.resize(m->max_face_triangles * 3);
        const ufbx_skin_deformer* skin = m->skin_deformers.count ? m->skin_deformers.data[0] : nullptr;
        if (skin && skin->clusters.count == 0) skin = nullptr;
        // Vértice sem peso nenhum fica PRESO AO NÓ DA MALHA (é o que o FBX
        // faz). Antes ia inteiro para a junta 0 — um osso qualquer — e esticava
        // a malha até ele. A junta extra "rígida" é o próprio nó da malha, com
        // a inversa de bind = geometria→nó.
        const ufbx_node* meshNode = m->instances.count ? m->instances.data[0] : nullptr;
        u16 rigidJoint = 0;
        if (skin) {
            bool needRigid = skin->vertices.count < m->num_vertices;
            for (usize v = 0; v < skin->vertices.count && !needRigid; ++v) {
                const ufbx_skin_vertex& sv = skin->vertices.data[v];
                f64 total = 0.0;
                for (u32 w = 0; w < sv.num_weights; ++w) total += skin->weights.data[sv.weight_begin + w].weight;
                if (!(total > 0.0)) needRigid = true;
            }
            Skin s;
            s.name = std::string(m->name.data, m->name.length);
            // Inversa de bind = geometry_to_bone da ufbx: já inclui a
            // transformação geométrica do nó da malha e a pose de bind do
            // arquivo (TransformLink/Transform), na mesma unidade da cena —
            // a escala da conversão de unidade está na raiz e se cancela.
            for (usize c = 0; c < skin->clusters.count; ++c) {
                const ufbx_skin_cluster* cl = skin->clusters.data[c];
                if (cl->bone_node) {
                    s.joints.push_back(static_cast<i32>(cl->bone_node->typed_id));
                    s.inverseBind.push_back(to_mat4(cl->geometry_to_bone));
                } else {
                    // Osso quebrado: rígido no nó da malha, nunca na origem.
                    s.joints.push_back(meshNode ? static_cast<i32>(meshNode->typed_id) : -1);
                    s.inverseBind.push_back(meshNode ? to_mat4(meshNode->geometry_to_node) : Mat4::identity());
                }
            }
            if (needRigid && meshNode && skin->clusters.count < 0xFFFFu) {
                rigidJoint = static_cast<u16>(skin->clusters.count);
                s.joints.push_back(static_cast<i32>(meshNode->typed_id));
                s.inverseBind.push_back(to_mat4(meshNode->geometry_to_node));
            }
            skinOfMesh[m] = static_cast<i32>(A.skins.size());
            A.skins.push_back(std::move(s));
        }
        Mesh mesh;
        mesh.name = std::string(m->name.data, m->name.length);
        for (usize pi = 0; pi < m->material_parts.count && !stop; ++pi) {
            const ufbx_mesh_part& part = m->material_parts.data[pi];
            if (part.num_triangles == 0) continue;
            // Parte enorme: lida em pedaços (borda travada na simplificação
            // para os pedaços continuarem colados).
            const bool chunked = part.num_triangles > kUfbxChunkTriangles;
            std::vector<FlatVertex> corners;
            corners.reserve(std::min<usize>(part.num_triangles, kUfbxChunkTriangles) * 3);
            auto flush = [&]() {
                if (corners.empty()) return;
                // Solda cantos idênticos (FBX guarda por canto; o GPU quer indexado).
                std::vector<u32> remap(corners.size());
                const usize unique = meshopt_generateVertexRemap(remap.data(), nullptr, corners.size(), corners.data(),
                                                                 corners.size(), sizeof(FlatVertex));
                std::vector<FlatVertex> verts(unique);
                meshopt_remapVertexBuffer(verts.data(), corners.data(), corners.size(), sizeof(FlatVertex), remap.data());
                corners.clear();
                Primitive p;
                p.indices.assign(remap.begin(), remap.end());
                std::vector<u32>().swap(remap);
                p.positions.reserve(unique);
                const bool hasNormals = m->vertex_normal.exists;
                for (const FlatVertex& v : verts) {
                    p.positions.push_back(v.p);
                    if (hasNormals) p.normals.push_back(v.n);
                    p.uv0.push_back(v.uv);
                    if (m->vertex_color.exists) p.colors.push_back(v.color);
                    if (skin) {
                        for (int c = 0; c < 4; ++c) p.joints.push_back(v.joints[c]);
                        p.weights.push_back(Vec4{v.weights[0], v.weights[1], v.weights[2], v.weights[3]});
                    }
                }
                std::vector<FlatVertex>().swap(verts);
                // Orçamento: simplifica o pedaço antes do próximo, antes das
                // normais planas e das tangentes (que crescem com os vértices).
                if (keep < 1.0f && p.indices.size() >= 3 * 64) simplify_primitive(p, keep, options.simplifyError, true, chunked);
                for (const Vec3& v : p.positions) p.bounds.add(v);
                if (!hasNormals) flat_normals(p);
                const i32 matIndex = part.index < m->materials.count && m->materials.data[part.index]
                                   ? static_cast<i32>(m->materials.data[part.index]->typed_id) : -1;
                p.material = matIndex;
                p.materialSlot = static_cast<i32>(part.index);
                // Any instance may bind a normal map to this shared geometry.
                if (m->vertex_uv.exists) {
                    generate_tangents(p, p.uv0);
                }
                mesh.bounds.add(p.bounds.min);
                mesh.bounds.add(p.bounds.max);
                mesh.primitives.push_back(std::move(p));
            };
            for (usize fi = 0; fi < part.face_indices.count; ++fi) {
                if ((++facesDone & 4095u) == 0) {
                    if (cancelled(progress)) { stop = true; break; }
                    set_fraction(progress, static_cast<f32>(static_cast<f64>(facesDone) / static_cast<f64>(std::max<u64>(1, totalFaces))));
                }
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
                            // Mais de 4 influências: ficam as 4 maiores (a ufbx
                            // já ordena do maior peso) e renormaliza para 1.
                            const u32 nw = std::min<u32>(4, sv.num_weights);
                            for (u32 w = 0; w < nw; ++w) {
                                const ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                                v.joints[w] = static_cast<u16>(sw.cluster_index);
                                v.weights[w] = static_cast<f32>(sw.weight);
                                total += v.weights[w];
                            }
                            if (total > 0.0f) {
                                for (f32& w : v.weights) w /= total;
                            } else {
                                for (u32 w = 0; w < 4; ++w) { v.joints[w] = 0; v.weights[w] = 0.0f; }
                                v.joints[0] = rigidJoint;
                                v.weights[0] = 1.0f;
                            }
                        } else {
                            v.joints[0] = rigidJoint;
                            v.weights[0] = 1.0f;
                        }
                    }
                    corners.push_back(v);
                }
                if (corners.size() >= kUfbxChunkTriangles * 3) flush();
            }
            if (!stop) flush();
        }
        if (stop) return fail_result(ImportError::Cancelled, "cancelado");
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
        // Duração do "take"; take sem intervalo gravado (LocalStart = LocalStop,
        // comum em exportações de Mixamo/Blender por ação) usa o das chaves —
        // antes a animação inteira era descartada por ter duração zero.
        f64 t0 = baked->playback_time_begin;
        f64 dur = baked->playback_duration;
        if (!(dur > 1e-6) && baked->key_time_max > baked->key_time_min) {
            t0 = baked->key_time_min;
            dur = baked->key_time_max - baked->key_time_min;
        }
        an.duration = static_cast<f32>(dur);
        if (an.name.empty()) an.name = "Take " + std::to_string(si + 1);
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
    // .mtl pedido e nenhum aberto: o material inteiro está faltando. Só quando
    // o OBJ usa materiais (`usemtl`): um OBJ só de geometria não pede .mtl.
    if (!opener.mtlOpened && !opener.mtlRequested.empty() && scene->materials.count > 0) {
        A.missingTextures.push_back(opener.mtlRequested.front());
        A.warnings.push_back("material ausente: " + opener.mtlRequested.front());
    }
    A.missingTextures.insert(A.missingTextures.end(), b.missing.begin(), b.missing.end());
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
