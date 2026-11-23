        auto& timeline = project_->timeline();
        const u32 primarySource = aep.comps[roots.front()].id;
        const usize newComps = standaloneTitle ? aep.comps.size() - 1 : aep.comps.size();
        if (timeline.composition_count() + newComps > 256 ||
            comp->layers().count() + roots.size() > kMaxLayerCount) {
            report.message = "O projeto aberto não tem espaço para as composições importadas.";
            return report;
        }
        std::unordered_map<u32, CompositionId> compositionIds;
        for (const auto& source : aep.comps) {
            const CompositionId id = standaloneTitle && source.id == primarySource
                ? compId : timeline.create_composition(source.name, source.width, source.height, source.fps);
            auto* imported = timeline.composition(id);
            if (!imported) {
                for (const auto& [sourceId, added] : compositionIds)
                    if (added != compId) (void)timeline.remove_composition(added);
                report.message = "Não foi possível preparar as composições do projeto.";
                return report;
            }
            compositionIds.emplace(source.id, id);
            imported->set_name(source.name);
            // A background color in AE is for the composition viewer. Nested
            // layers need to retain their alpha when drawn over the parent.
            imported->set_transparent_background(true);
            imported->set_duration(FrameIndex{std::max<i64>(1, std::llround(source.duration * imported->fps()))});
            imported->motion_blur().enabled = source.motionBlur;
            imported->motion_blur().shutterAngle = static_cast<f32>(source.shutterAngle);
            imported->motion_blur().shutterPhase = static_cast<f32>(source.shutterPhase);
            if (std::abs(imported->fps() - source.fps) > .001)
                report.note("composicao", source.name, "a taxa de quadros foi ajustada ao limite do Aurea; a duração em segundos foi preservada");
        }

        std::unordered_map<u32, const ae::ProjectMedia*> mediaBySource;
        for (const auto& source : aep.media) mediaBySource.emplace(source.id, &source);
        std::unordered_map<u32, AssetId> mediaIds;
        auto source_asset = [&](const ae::ProjectMedia& item, AssetKind kind) {
            if (const auto found = mediaIds.find(item.id); found != mediaIds.end()) return found->second;
            const std::string& filename = item.filename.empty() ? item.name : item.filename;
            AssetId assetId{};
            u32 matches = 0;
            if (item.resolvedPath.empty()) project_->for_each_asset([&](AssetId id, const Asset& asset) {
                if (asset.kind != kind || asset.sourcePath.empty() ||
                    (asset.originalFilename != filename && asset.name != filename)) return;
                if (kind != AssetKind::Audio &&
                    (asset.video.width != item.width || asset.video.height != item.height)) return;
                assetId = id;
                ++matches;
            });
            if (matches != 1) {
                Asset asset;
                asset.kind = kind;
                asset.name = item.name;
                asset.originalFilename = filename;
                asset.sourcePath = item.resolvedPath;
                asset.fileSizeBytes = item.fileBytes;
                asset.profile.codecTag = item.codecTag;
                asset.timebaseFps = item.fps > 0 ? std::clamp(item.fps, 1.0, 1000.0) : 30.0;
                asset.video.width = item.width;
                asset.video.height = item.height;
                asset.video.fps = asset.timebaseFps;
                asset.duration = FrameIndex{std::llround(std::clamp(item.duration, 0.0, 86400.0) * asset.timebaseFps)};
                asset.video.frameCount = asset.duration;
                asset.audio.channels = item.channels;
                if (item.sampleRate) {
                    asset.audio.sampleRate = item.sampleRate;
                    asset.audio.sampleCount = FrameIndex{std::llround(std::clamp(item.duration, 0.0, 86400.0) * item.sampleRate)};
                }
                assetId = project_->add_asset(std::move(asset));
                if (item.resolvedPath.empty()) {
                    report.skip("midia", filename, item.linkProblem.empty()
                        ? "arquivo de origem ausente; vincule a mídia original ou importe um pacote com ela"
                        : item.linkProblem);
                } else {
                    if (!item.recoveredFrom.empty()) report.note("midia", filename, "variante de mídia vinculada: " + item.recoveredFrom);
                    if (!standaloneTitle && kind == AssetKind::Video) {
                        if (const auto* added = project_->asset(assetId)) (void)media_.proxies().request(*added);
                    }
                    if (!standaloneTitle && kind == AssetKind::Image) {
                        const auto decoded = aeImages.find(item.resolvedPath);
                        if (decoded != aeImages.end() && decoded->second->rgba.capacity() + sizeof(ImagePixels) <= source_asset_room_locked())
                            images_.emplace(assetId.pack(), decoded->second);
                        else
                            report.note("midia", filename, "imagem vinculada; reabra o projeto para tentar decodificá-la novamente");
                    }
                }
            }
            mediaIds.emplace(item.id, assetId);
            return assetId;
        };

        for (auto& source : aep.comps) {
            const CompositionId id = compositionIds.at(source.id);
            (void)timeline.set_current(id);
            auto* imported = timeline.composition(id);
            host.set_composition(imported);
            std::unordered_map<u32, LayerId> ids;
            const bool solo = std::any_of(source.layers.begin(), source.layers.end(), [](const auto& l) {
                return (l.flagsB & 8) != 0;
            });
            const f64 fps = imported->fps();
            for (auto it = source.layers.rbegin(); it != source.layers.rend(); ++it) {
                auto& entry = *it;
                const f64 origin = entry.start + (entry.stretch < 0 ? entry.stretch / 3000.0 : 0);
                const f64 in = origin + entry.in * entry.stretch;
                const f64 out = origin + entry.out * entry.stretch;
                const i64 first = std::clamp<i64>(std::llround(std::min(in, out) * fps), 0, imported->duration().value);
                const i64 last = std::clamp<i64>(std::llround(std::max(in, out) * fps), 0, imported->duration().value);
                if (first >= last) continue;
                const auto child = compositionIds.find(entry.source);
                const auto media = mediaBySource.find(entry.source);
                const ae::ProjectMedia* item = media != mediaBySource.end() ? media->second : nullptr;
                const Composition* nested = child == compositionIds.end() ? nullptr : timeline.composition(child->second);
                ae::NewLayerSpec spec;
                spec.name = entry.name;
                if (spec.name.empty() && item) spec.name = item->name;
                if (spec.name.empty() && nested) spec.name = nested->name();
                if (spec.name.empty()) spec.name = "Camada " + std::to_string(entry.id);
                spec.width = item ? item->width : 0;
                spec.height = item ? item->height : 0;
                if (item) std::copy_n(item->color, 4, spec.color);
                const bool adjustment = (entry.flagsB & 2) != 0;
                const bool null = (entry.flagsB & 0x80) != 0;
                const bool soloed = !solo || (entry.flagsB & 8);
                LayerId layerId{};
                if (adjustment) {
                    layerId = imported->add_layer(LayerKind::Adjustment, spec.name);
                } else if (null) {
                    layerId = host.create_layer(ae::NewLayer::Null, spec);
                } else if (entry.type == 1) {
                    spec.lightKind = entry.light == 0 ? 0 : entry.light == 1 ? 2 : 1;
                    layerId = host.create_layer(ae::NewLayer::Light, spec);
                } else if (entry.type == 2) {
                    layerId = host.create_layer(ae::NewLayer::Camera, spec);
                } else if (entry.type == 3) {
                    layerId = host.create_layer(ae::NewLayer::Text, spec);
                    report.skip("texto", spec.name, "o documento de texto do AE ainda precisa ser preenchido");
                } else if (entry.type == 4) {
                    layerId = host.create_layer(ae::NewLayer::Shape, spec);
                } else if (entry.type == 0 && nested) {
                    layerId = imported->add_layer(LayerKind::Composition, spec.name);
                } else if (entry.type == 0 && item && item->solid) {
                    layerId = host.create_layer(ae::NewLayer::Solid, spec);
                } else if (entry.type == 0 && item) {
                    const AssetKind mediaKind = item->width == 0 || item->height == 0 ? AssetKind::Audio :
                                           item->duration == 0 ? AssetKind::Image : AssetKind::Video;
                    const LayerKind layerKind = mediaKind == AssetKind::Audio ? LayerKind::Audio :
                                                mediaKind == AssetKind::Image ? LayerKind::Image : LayerKind::Video;
                    layerId = imported->add_layer(layerKind, spec.name);
                    if (auto* layer = imported->layer(layerId)) layer->source = source_asset(*item, mediaKind);
                } else {
                    layerId = host.create_layer(ae::NewLayer::Null, spec);
                    report.skip("camada", spec.name, "tipo ou fonte sem equivalente no Aurea; a estrutura foi preservada como nulo");
                }
                auto* layer = imported->layer(layerId);
                if (!layer) {
                    report.skip("camada", spec.name, "não foi possível criar a camada");
                    continue;
                }
                if (layer->kind == LayerKind::Shape && entry.type == 4) {
                    layer->shape.shapeType = kShapeVector;
                    layer->shape.filled = false;
                }
                layer->threeD = (entry.flagsB & 4) != 0 || entry.type == 1 || entry.type == 2;
                layer->adjustment = adjustment;
                layer->visible = soloed && !(entry.flagsA & 2) &&
                    (layer->kind == LayerKind::Audio ? (entry.flagsC & 2) != 0 : (entry.flagsC & 1) != 0);
                layer->muted = !soloed || !(entry.flagsC & 2);
                layer->motionBlur = entry.flagsC & 8;
                if (entry.flagsC & 0x10) layer->frameBlend = (entry.flagsA & 4) ? 2 : 1;
                layer->blendMode = ae_blend(entry.blend, entry.transfer);
                layer->start = FrameIndex{first};
                layer->end = FrameIndex{last};
                if (layer->kind == LayerKind::Composition) layer->nested.composition = child->second;
                if (layer->kind == LayerKind::Video || layer->kind == LayerKind::Audio || nested) {
                    layer->reversed = entry.stretch < 0;
                    layer->speed = static_cast<f32>(1.0 / std::abs(entry.stretch));
                    const i64 sourceStart = layer->reversed ? last - 1 : first;
                    const f64 offset = (static_cast<f64>(sourceStart) - origin * fps) / entry.stretch;
                    layer->offset = FrameIndex{std::llround(std::clamp(offset, 0.0, 1.0e9))};
                }
                const f32 width = static_cast<f32>(nested ? nested->width() : spec.width ? spec.width : imported->width());
                const f32 height = static_cast<f32>(nested ? nested->height() : spec.height ? spec.height : imported->height());
                if (layer->kind != LayerKind::Camera && layer->kind != LayerKind::Light &&
                    layer->kind != LayerKind::Text && layer->kind != LayerKind::Null) {
                    layer->transform.anchor = {width * .5f, height * .5f, 0};
                    layer->transform.position = {imported->width() * .5f, imported->height() * .5f, 0};
                }
                ae::apply_project_properties(entry, host, layerId, report);
                layer = imported->layer(layerId);
                if (layer) {
                    layer->locked = (entry.flagsC & 0x20) != 0;
                    text::migrate_text_effects(*layer);
                }
                ids.emplace(entry.id, layerId);
                ++report.layers;
            }
            for (usize index = 0; index < source.layers.size(); ++index) {
                const auto& entry = source.layers[index];
                const auto found = ids.find(entry.id);
                if (found == ids.end()) continue;
                auto* layer = imported->layer(found->second);
                if (entry.parent) {
                    const auto parent = ids.find(entry.parent);
                    if (parent == ids.end() || !host.set_parent(found->second, parent->second, false))
                        report.skip("camada", layer->name, "vínculo de pai inválido no projeto de origem");
                }
                if (entry.matteMode >= 1 && entry.matteMode <= 4) {
                    const u32 matteId = entry.legacyMatte && index ? source.layers[index - 1].id : entry.matte;
                    const auto matte = ids.find(matteId);
                    if (matte != ids.end() && matte->second != found->second) {
                        layer->matteSource = matte->second;
                        layer->matteMode = static_cast<MatteMode>(entry.matteMode);
                    } else {
                        report.skip("camada", layer->name, "fonte do track matte não encontrada");
                    }
                }
            }
            imported->rebuild_draw_order();
        }

        (void)timeline.set_current(compId);
        comp = timeline.composition(compId);
        // Keep the existing edit intact. Each top-level AE comp gets its own editable layer.
        for (auto i = roots.rbegin(); i != roots.rend(); ++i) {
            if (standaloneTitle && *i == roots.front()) continue;
            const auto& source = aep.comps[*i];
            const LayerId layerId = comp->add_layer(LayerKind::Composition, source.name);
            auto* layer = comp->layer(layerId);
            layer->nested.composition = compositionIds.at(source.id);
            layer->transform.anchor = {source.width * .5f, source.height * .5f, 0};
            layer->transform.position = {comp->width() * .5f, comp->height() * .5f, 0};
            layer->end = FrameIndex{std::max<i64>(1, std::llround(source.duration * comp->fps()))};
            layer->visible = *i == roots.front();
            layer->muted = !layer->visible;
            if (layer->visible && layer->end.value > comp->duration().value) comp->set_duration(layer->end);
        }
        comp->rebuild_draw_order();
        if (before) history_.before_mutation(*before, compId, "importar projeto do After Effects");
        bump_model_revision_locked();
        if (comp->duration().value != durationBefore) configure_playback_locked(comp);
        project_->mark_dirty();
        request_render();
        report.ok = true;
        report.changes += static_cast<u32>(aep.comps.size()) + report.layers;
        ae::finish_report(report);
        return report;
