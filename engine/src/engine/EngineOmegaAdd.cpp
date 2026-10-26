// =============================================================================
//  Aurea / engine / EngineOmegaAdd.cpp
//
//  Omega 0.3.2, área 08 (adicionar e exportar): as regras de edição que as
//  telas novas de adicionar pedem ao motor.
//
//  - LOTE de importação (navegador de mídia, "Ver tudo"): vários vídeos e
//    imagens, cada um com a sua duração ou o seu corte, entram num passo de
//    desfazer só, arrumados no MESMO início ou EM SEQUÊNCIA. A cópia e a
//    decodificação continuam na plataforma, uma mídia por vez (memória
//    limitada); o motor abre o passo, aplica tempo e corte e fecha.
//  - GRUPO VAZIO: uma composição nova e vazia mostrada por uma camada.
//  - ELEMENTO/PROJETO: outro projeto vira uma camada de grupo, pelo mesmo
//    caminho do colar entre projetos (mídia importada com handles novos).
//
//  O desfazer é por snapshot (command/History.hpp): a importação captura o
//  "antes"; o que esta unidade muda logo depois, sem outra ação no meio, cai
//  no mesmo passo. O lote usa o grupo do histórico para juntar tudo.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/media/ImageResample.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aurea {

namespace {

/// "Grupo vazio", "Grupo vazio 2"…: o menor número livre entre as camadas.
std::string omega_numbered_name(const Composition& comp, const std::string& base) {
    std::vector<u32> used;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const Layer* l = comp.layer(comp.order().at(i));
        if (!l) continue;
        if (l->name == base) { used.push_back(1); continue; }
        if (l->name.size() <= base.size() + 1 || l->name.compare(0, base.size(), base) != 0 || l->name[base.size()] != ' ') continue;
        const std::string tail = l->name.substr(base.size() + 1);
        if (tail.empty() || tail.size() > 6 || !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        used.push_back(static_cast<u32>(std::stoul(tail)));
    }
    for (u32 n = 1;; ++n) {
        if (std::find(used.begin(), used.end(), n) == used.end()) return n == 1 ? base : base + " " + std::to_string(n);
    }
}

/// Fim do conteúdo da composição sem a camada `self` (a regra do "aparar ao conteúdo").
i64 omega_content_end(const Composition& comp, const Layer* self) {
    i64 end = 0;
    comp.layers().for_each([&](LayerId, const Layer& layer) {
        if (&layer == self) return;
        end = std::max(end, layer.end.value);
    });
    return end;
}

/// Os parâmetros que efeitos novos ganharam depois que o projeto foi salvo
/// (o mesmo que a abertura de projeto faz): sem eles EffectSetParam recusa.
void omega_complete_effect_params(Composition& c, const EffectRegistry& registry) {
    for (u32 i = 0; i < c.order().size(); ++i) {
        Layer* layer = c.layer(c.order().at(i));
        if (!layer) continue;
        for (auto& effect : layer->effects) {
            (void)motion_tile::upgrade_legacy_layout(*layer, effect);
            const auto* specs = registry.params(effect.type);
            if (!specs || effect.params.size() >= specs->count()) continue;
            EffectInstance defaults;
            initialize_instance(defaults, *specs);
            for (u32 p = static_cast<u32>(effect.params.size()); p < specs->count(); ++p) {
                auto slot = defaults.params[p];
                if ((specs->at(p).flags & kParamLegacyZero) != 0) slot.constant = ParamValue::scalar(0.0f);
                if (specs->at(p).type == ParamType::Curve) {
                    effect.curves.push_back(defaults.curves[slot.constant.ref]);
                    slot.constant.ref = effect.curves.size() - 1;
                } else if (specs->at(p).type == ParamType::Gradient) {
                    effect.gradients.push_back(defaults.gradients[slot.constant.ref]);
                    slot.constant.ref = effect.gradients.size() - 1;
                }
                effect.params.push_back(slot);
            }
        }
    }
}

} // namespace

// =============================================================================
// Lote de importação (P1-2)
// =============================================================================
void Engine::begin_import_batch() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    if (!project_) return;
    // Já aberto (e o histórico não foi limpo por outro projeto no meio): não empilha.
    if (omegaImportBatch_ && history_.in_group()) return;
    history_.begin_group("importar midia");
    omegaImportBatch_ = true;
    omegaImportBatchFrame_ = std::max<i64>(0, playback_.current().value);
}

Result<u32> Engine::end_import_batch(const u64* ids, u32 count, MediaBatchPlacement placement) noexcept {
    i64 frame = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        frame = omegaImportBatch_ ? omegaImportBatchFrame_ : std::max<i64>(0, playback_.current().value);
    }
    // arrange_layer_times toma o lock do modelo sozinho: chamado fora dele.
    // Mesmo início: todas no cabeçote de quando o lote abriu (a importação já as
    // pôs lá; isto só garante, se o cabeçote andou no meio). Em sequência: a
    // partir desse cabeçote, uma depois da outra na ORDEM DE ESCOLHA.
    u32 moved = 0;
    Status arranged = OkStatus;
    if (ids && count > 0) {
        const auto start = arrange_layer_times(ids, count, LayerTimeArrangement::StartsAtPlayhead, frame);
        if (start.ok()) moved += *start;
        else arranged = start.status();
        if (placement == MediaBatchPlacement::Sequence && count >= 2) {
            const auto sequence = arrange_layer_times(ids, count, LayerTimeArrangement::Sequence, frame);
            if (sequence.ok()) moved = std::max(moved, *sequence);
            else arranged = sequence.status();
        }
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (omegaImportBatch_) {
        history_.end_group();
        omegaImportBatch_ = false;
    }
    if (project_ && ids && count > 0) {
        if (Composition* comp = current_composition()) {
            selection_.clear();
            for (u32 i = 0; i < count; ++i)
                if (comp->layer(LayerId::unpack(ids[i]))) selection_.push_back(ids[i]);
            std::sort(selection_.begin(), selection_.end());
            selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
        }
        bump_model_revision_locked();
        request_render();
    }
    // Uma camada travada ou um intervalo inválido não desfaz o que entrou.
    if (!arranged.ok()) AUREA_LOG_WARN("lote de importacao: arranjo recusado (%d)", arranged.raw());
    return moved;
}

Status Engine::apply_import_timing(u64 layerId, i64 durationFrames, i64 sourceInUs, i64 sourceOutUs) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return Status{Errc::NotFound, "camada nao encontrada"};
    // SEM before_mutation: ainda é o passo de desfazer da importação (ou do lote).
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    const i64 oldEnd = l->end.value;
    bool changed = false;
    if (l->kind == LayerKind::Image && durationFrames > 0) {
        const i64 frames = std::clamp<i64>(durationFrames, 1, i64{1} << 30);
        l->end = FrameIndex{l->start.value + frames};
        l->fadeIn = FrameIndex{std::min(l->fadeIn.value, frames)};
        l->fadeOut = FrameIndex{std::min(l->fadeOut.value, frames)};
        changed = true;
    } else if (l->kind == LayerKind::Video && sourceInUs >= 0 && sourceOutUs > sourceInUs) {
        // A fonte em quadros da composição (o `offset` mora nessa unidade).
        i64 sourceFrames = 0;
        if (const Asset* a = project_->asset(l->source)) {
            const f64 assetFps = a->video.fps > 0.0 ? a->video.fps : (a->timebaseFps > 0.0 ? a->timebaseFps : fps);
            if (a->video.frameCount.value > 0)
                sourceFrames = static_cast<i64>(std::ceil(static_cast<f64>(a->video.frameCount.value) * fps / assetFps - 1e-6));
        }
        i64 in = std::llround(static_cast<f64>(sourceInUs) * fps / 1e6);
        i64 length = std::max<i64>(1, std::llround(static_cast<f64>(sourceOutUs - sourceInUs) * fps / 1e6));
        if (sourceFrames > 0) {
            in = std::clamp<i64>(in, 0, std::max<i64>(0, sourceFrames - 1));
            length = std::clamp<i64>(length, 1, std::max<i64>(1, sourceFrames - in));
        }
        l->offset = FrameIndex{in};
        l->end = FrameIndex{l->start.value + length};
        l->fadeIn = FrameIndex{std::min(l->fadeIn.value, length)};
        l->fadeOut = FrameIndex{std::min(l->fadeOut.value, length)};
        changed = true;
    }
    if (!changed) return OkStatus;
    l->cacheKey = 0;
    // A composição acompanha: cresce quando a camada passa do fim; quando era
    // esta camada que marcava o fim (o primeiro clipe adota a duração dele),
    // encolhe junto com o corte — sem sobrar um rabo vazio.
    const i64 newEnd = l->end.value;
    const i64 duration = comp->duration().value;
    if (newEnd > duration) comp->set_duration(FrameIndex{newEnd});
    else if (newEnd < oldEnd && oldEnd == duration) {
        const i64 others = omega_content_end(*comp, l);
        if (others < oldEnd) comp->set_duration(FrameIndex{std::max<i64>(1, std::max(newEnd, others))});
    }
    comp->touch();
    configure_playback_locked(comp);
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return OkStatus;
}

std::vector<u64> Engine::import_media_batch(const std::vector<MediaBatchItem>& items, MediaBatchPlacement placement,
                                            Status* firstError) noexcept {
    std::vector<u64> ids;
    if (firstError) *firstError = OkStatus;
    if (items.empty()) return ids;
    begin_import_batch();
    for (const MediaBatchItem& item : items) {
        Result<u64> r = item.video
            ? import_video(item.source)
            : import_image(item.rgba, item.width, item.height, item.name.empty() ? nullptr : item.name.c_str(),
                           item.sourcePath.empty() ? nullptr : item.sourcePath.c_str(), item.orientation);
        if (!r.ok()) {
            if (firstError && firstError->ok()) *firstError = r.status();
            continue;
        }
        const Status timed = apply_import_timing(*r, item.video ? 0 : item.durationFrames,
                                                 item.video ? item.sourceInUs : 0, item.video ? item.sourceOutUs : 0);
        if (!timed.ok() && firstError && firstError->ok()) *firstError = timed;
        ids.push_back(*r);
    }
    (void)end_import_batch(ids.data(), static_cast<u32>(ids.size()), placement);
    return ids;
}

// =============================================================================
// Grupo vazio (P1-5)
// =============================================================================
Result<u64> Engine::add_empty_group(const char* name) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    Timeline& tl = project_->timeline();
    const CompositionId parentId = tl.current();
    const std::string label = omega_numbered_name(*comp, name && *name ? std::string(name) : std::string("Grupo vazio"));
    history_.before_mutation(*comp, parentId, "grupo vazio");
    bump_model_revision_locked();
    const CompositionId cid = tl.create_composition(label, comp->width(), comp->height(), comp->fps());
    Composition* child = cid.valid() ? tl.composition(cid) : nullptr;
    comp = tl.composition(parentId);   // a criação pode ter realocado
    if (!child || !comp) return Status{Errc::OutOfMemory, "composicao nao criada"};
    child->set_transparent_background(true);
    child->set_nesting_depth(comp->nesting_depth() + 1);
    // A cena nova acende e amostra como a de onde veio (como a pré-composição).
    child->environment() = comp->environment();
    child->environment().showBackground = false;
    child->shadows() = comp->shadows();
    child->post_process().quality3d = comp->post_process().quality3d;

    const LayerId pl = comp->add_layer(LayerKind::Composition, label);
    Layer* p = comp->layer(pl);
    if (!p) return Status{Errc::OutOfMemory, "camada nao criada"};
    const i64 t = std::max<i64>(0, playback_.current().value);
    p->nested.composition = cid;
    p->start = FrameIndex{t};
    p->end = FrameIndex{new_layer_end_locked(comp, p, t)};
    // O tempo do grupo começa no início da camada (o que entrar nele aparece já).
    p->offset = FrameIndex{0};
    child->set_duration(FrameIndex{std::max<i64>(comp->duration().value, p->end.value - p->start.value)});
    const Vec3 center{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    p->transform.anchor = center;
    p->transform.position = center;
    p->transform.scale = Vec3{1, 1, 1};
    p->transform.rotation = Vec3{0, 0, 0};
    p->transform.opacity = 1.0f;
    if (p->end.value > comp->duration().value) {
        comp->set_duration(p->end);
        configure_playback_locked(comp);
    }
    comp->rebuild_draw_order();
    selection_.assign(1, pl.pack());
    project_->mark_dirty();
    request_render();
    return pl.pack();
}

// =============================================================================
// Elemento/Projeto (P1-6)
// =============================================================================
Result<u64> Engine::insert_project_as_layer(const char* projectPath, const char* name) noexcept {
    if (!projectPath || !*projectPath) return Status{Errc::InvalidArgument, "projeto sem caminho"};
    const std::string path = projectPath;
    u64 session = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_ || !current_composition()) return Status{Errc::InvalidState, "nenhum projeto aberto"};
        // Ciclo: o projeto aberto não entra nele mesmo.
        if (!project_->path().empty() && project_->path() == path)
            return Status{Errc::InvalidArgument, "o projeto nao pode entrar nele mesmo"};
        session = projectSession_;
    }

    // Leitura FORA do lock do modelo (disco e decodificação de imagens).
    Project loaded;
    LoadOptions options;
    options.lazyAssets = true;
    options.tolerateCorruptSections = false;
    std::string error;
    if (const Status s = ProjectSerializer::load(loaded, path, options, nullptr, &error); !s.ok()) return s;
    const CompositionId rootId = loaded.timeline().root();
    const Composition* root = loaded.timeline().composition(rootId);
    if (!root) return Status{Errc::ProjectCorrupted, "projeto sem composicao"};
    loaded.timeline().for_each_composition([&](CompositionId, Composition& c) { omega_complete_effect_params(c, effectRegistry_); });

    ClipboardMedia bundle;
    bundle.session = session + 1;   // "outro projeto": o colar importa a mídia
    loaded.timeline().for_each_composition([&](CompositionId id, const Composition& c) {
        bundle.compositions.emplace(id.pack(), std::shared_ptr<const Composition>(c.clone()));
    });
    std::vector<std::pair<u64, std::string>> images;
    std::vector<std::string> fonts;
    loaded.for_each_asset([&](AssetId id, Asset& a) {
        Asset copy = a;
        if (!copy.sourcePath.empty()) copy.sourcePath = resolve_asset_path(copy.sourcePath);
        if (!copy.proxyPath.empty()) copy.proxyPath = resolve_asset_path(copy.proxyPath);
        if (copy.kind == AssetKind::Image && !a.sourcePath.empty()) images.emplace_back(id.pack(), a.sourcePath);
        bundle.assets.emplace(id.pack(), std::move(copy));
    });
    loaded.timeline().for_each_composition([&](CompositionId, const Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            const Layer* l = c.layer(c.order().at(i));
            if (l && l->kind == LayerKind::Text && !l->text.fontPath.empty()) fonts.push_back(resolve_asset_path(l->text.fontPath));
        }
    });
    // Imagens: o projeto guarda a origem; os pixels voltam pela plataforma
    // (como na abertura). Modelos 3D, HDRI e LUT reabrem com o projeto.
    u32 missing = 0;
    if (config_.imageLoader) {
        for (const auto& [key, stored] : images) {
            const auto it = bundle.assets.find(key);
            if (it == bundle.assets.end()) continue;
            const Asset& a = it->second;
            ImagePixels px;
            const std::string resolved = resolve_asset_path(stored);
            if (!config_.imageLoader(resolved.c_str(), px, config_.imageLoaderContext) || px.width == 0 || px.height == 0
                || px.width > 65536 || px.height > 65536 || px.rgba.size() != static_cast<u64>(px.width) * px.height * 4) {
                ++missing;
                continue;
            }
            const bool oriented = a.profile.profile == image::kDisplayOriented;
            const u32 savedW = a.video.width, savedH = a.video.height;
            if (oriented && savedW && savedH && !(px.orientation <= 1 && px.width == savedW && px.height == savedH)) {
                std::vector<u8> shown;
                bool ok = false;
                try { ok = image::reopen_pixels(px.rgba.data(), px.width, px.height, px.orientation, savedW, savedH, shown); }
                catch (const std::bad_alloc&) { ok = false; }
                if (!ok) { ++missing; continue; }
                px.rgba = std::move(shown);
                px.width = savedW; px.height = savedH; px.orientation = 1;
            }
            if ((savedW && px.width != savedW) || (savedH && px.height != savedH)) { ++missing; continue; }
            bundle.images.emplace(key, std::make_shared<const ImagePixels>(std::move(px)));
        }
    }
    std::sort(fonts.begin(), fonts.end());
    fonts.erase(std::unique(fonts.begin(), fonts.end()), fonts.end());
    for (const std::string& f : fonts) (void)text::FontManager::instance().add_file(f, true);

    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_ || session != projectSession_) return Status{Errc::Cancelled, "projeto alterado durante a leitura"};
    drain_commands_locked();
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    Timeline& tl = project_->timeline();
    const CompositionId parentId = tl.current();
    history_.before_mutation(*comp, parentId, "inserir projeto");
    bump_model_revision_locked();

    // A camada do grupo, montada fora da composição: o colar entre projetos cria
    // a composição nova (e as aninhadas) e religa a mídia.
    Layer group;
    group.kind = LayerKind::Composition;
    group.name = name && *name ? std::string(name) : root->name();
    group.nested.composition = rootId;
    std::vector<Layer> layers{group};
    restore_clipboard_media_locked(bundle, layers);
    comp = tl.composition(parentId);   // criar composições pode ter realocado
    if (!comp) return Status{Errc::InvalidState, "composicao sumiu"};
    const CompositionId cid = layers[0].nested.composition;
    Composition* child = cid.valid() ? tl.composition(cid) : nullptr;
    if (!child) return Status{Errc::OutOfMemory, "composicao nao criada"};
    child->set_nesting_depth(comp->nesting_depth() + 1);

    const LayerId pl = comp->add_layer(LayerKind::Composition, layers[0].name);
    Layer* p = comp->layer(pl);
    if (!p) return Status{Errc::OutOfMemory, "camada nao criada"};
    const i64 t = std::max<i64>(0, playback_.current().value);
    // A duração do projeto inserido, na taxa daqui (o grupo toca 1:1 no tempo).
    const f64 childFps = child->fps() > 0.0 ? child->fps() : comp->fps();
    const i64 length = std::max<i64>(1, std::llround(static_cast<f64>(std::max<i64>(1, child->duration().value))
                                                    * comp->fps() / (childFps > 0.0 ? childFps : 30.0)));
    p->nested.composition = cid;
    p->start = FrameIndex{t};
    p->end = FrameIndex{t + length};
    p->offset = FrameIndex{0};
    // Cabe no quadro daqui (como uma mídia importada), centrado.
    const f32 cw = static_cast<f32>(std::max<u32>(1, child->width()));
    const f32 ch = static_cast<f32>(std::max<u32>(1, child->height()));
    const f32 fit = std::min(static_cast<f32>(comp->width()) / cw, static_cast<f32>(comp->height()) / ch);
    p->transform.anchor = Vec3{cw * 0.5f, ch * 0.5f, 0.0f};
    p->transform.position = Vec3{static_cast<f32>(comp->width()) * 0.5f, static_cast<f32>(comp->height()) * 0.5f, 0.0f};
    p->transform.scale = Vec3{fit, fit, 1.0f};
    p->transform.rotation = Vec3{0, 0, 0};
    p->transform.opacity = 1.0f;
    if (p->end.value > comp->duration().value) comp->set_duration(p->end);
    configure_playback_locked(comp);
    comp->rebuild_draw_order();
    selection_.assign(1, pl.pack());
    project_->mark_dirty();
    request_render();
    AUREA_LOG_INFO("projeto inserido como grupo: %u imagem(ns) ausente(s)", missing);
    return pl.pack();
}

} // namespace aurea
