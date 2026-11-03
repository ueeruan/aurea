// =============================================================================
//  Aurea / engine / EngineAssistant.cpp — ver assistant/Assistant.hpp
// =============================================================================
#include "aurea/assistant/Assistant.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/project/Presets.hpp"
#include "../project/AeImportBind.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace aurea {
namespace assistant {

namespace {

const char* kViewNames[Engine::kViewModeCount] = {
    "active_camera", "default_camera", "front", "back", "left", "right", "top", "bottom",
    "custom_view_1", "custom_view_2", "custom_view_3",
};

const char* kind_name(LayerKind k) noexcept {
    switch (k) {
        case LayerKind::Video: return "video";
        case LayerKind::Image: return "image";
        case LayerKind::Audio: return "audio";
        case LayerKind::Text: return "text";
        case LayerKind::Shape: return "shape";
        case LayerKind::Null: return "null";
        case LayerKind::Adjustment: return "adjustment";
        case LayerKind::Camera: return "camera";
        case LayerKind::Light: return "light";
        case LayerKind::Model3D: return "model3d";
        case LayerKind::ParticleSystem: return "particles";
        case LayerKind::Composition: return "precomp";
        default: return "unknown";
    }
}

/// Uma ferramenta: nome, descrição e o JSON Schema dos argumentos (já pronto).
struct Tool { const char* name; const char* description; const char* parameters; };

constexpr Tool kTools[] = {
    {"get_project_state",
     "Read the open composition: size, fps, duration, playhead, selection, view mode and every layer "
     "(id, AE index, name, kind, time range in seconds, 3D, visible, locked, effects). Call it before editing "
     "and again after a script to confirm the result.",
     R"({"type":"object","properties":{},"additionalProperties":false})"},
    {"run_ae_script",
     "Run After Effects ExtendScript (ES3) on the open composition, in a sandbox. One run = one undo step. "
     "Supported: app.project.activeItem (the open comp); comp.layers.addText/addBoxText/addSolid/addShape/addNull/"
     "addCamera/addLight; comp.layer(index|name); comp.selectedLayers; layer.name/startTime/inPoint/outPoint/threeDLayer/"
     "enabled/remove()/duplicate()/moveToBeginning(); layer.property(\"ADBE ...\" or display name) down to leaves "
     "(ADBE Transform Group: ADBE Position, ADBE Scale, ADBE Rotate Z, ADBE Opacity, ADBE Anchor Point; "
     "ADBE Effect Parade.addProperty(matchName); ADBE Text Properties/ADBE Text Document with TextDocument "
     "text/fontSize/fillColor/strokeColor/strokeWidth/justification/tracking/font; text animators; masks); "
     "setValue, setValueAtTime(seconds, value), setValuesAtTimes, addKey, setTemporalEaseAtKey(KeyframeEase), "
     "setInterpolationTypeAtKey, expression. Colors are [r,g,b] 0..1. Not available: new compositions, precompose, "
     "files, importing media, rendering, alerts or windows. Returns a report of what was applied and what was skipped.",
     R"({"type":"object","properties":{"code":{"type":"string","description":"The ExtendScript source."}},"required":["code"],"additionalProperties":false})"},
    {"list_ae_effects",
     "Search the After Effects effects this app supports (matchName for Effects.addProperty, English name and "
     "the parameter names accepted by property()). Leave query empty to list them all.",
     R"({"type":"object","properties":{"query":{"type":"string","description":"Part of the name, e.g. blur, glow, color."}},"additionalProperties":false})"},
    {"select_layers",
     "Select layers by id (from get_project_state). An empty list clears the selection.",
     R"({"type":"object","properties":{"ids":{"type":"array","items":{"type":"string"}}},"required":["ids"],"additionalProperties":false})"},
    {"delete_layers",
     "Delete layers by id (from get_project_state). One undo step.",
     R"({"type":"object","properties":{"ids":{"type":"array","items":{"type":"string"}}},"required":["ids"],"additionalProperties":false})"},
    {"undo", "Undo the last steps (default 1).",
     R"({"type":"object","properties":{"steps":{"type":"integer","minimum":1,"maximum":50}},"additionalProperties":false})"},
    {"redo", "Redo steps that were undone (default 1).",
     R"({"type":"object","properties":{"steps":{"type":"integer","minimum":1,"maximum":50}},"additionalProperties":false})"},
    {"seek", "Move the playhead to a time in seconds.",
     R"({"type":"object","properties":{"seconds":{"type":"number","minimum":0}},"required":["seconds"],"additionalProperties":false})"},
    {"playback", "Play or pause the preview.",
     R"({"type":"object","properties":{"action":{"type":"string","enum":["play","pause"]}},"required":["action"],"additionalProperties":false})"},
    {"set_view",
     "Change the preview view like After Effects (only the preview; export always uses the active camera).",
     R"({"type":"object","properties":{"mode":{"type":"string","enum":["active_camera","default_camera","front","back","left","right","top","bottom","custom_view_1","custom_view_2","custom_view_3"]}},"required":["mode"],"additionalProperties":false})"},
    {"open_panel",
     "Open an editor screen for the user. Use it for what scripts cannot do: importing media, exporting the video, "
     "auto captions (transcription), the effects browser, text options, audio, the 3D scene. Then tell the user what to tap.",
     R"({"type":"object","properties":{"panel":{"type":"string","enum":["media","export","captions","effects","text","audio","scene3d"]}},"required":["panel"],"additionalProperties":false})"},
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string error(std::string_view message) {
    json::Writer w;
    w.begin_object().key("ok").value(false).key("error").value(message).end_object();
    return w.str();
}

std::string ok() {
    json::Writer w;
    w.begin_object().key("ok").value(true).end_object();
    return w.str();
}

/// Ids de camada: texto (os ids de 64 bits não cabem num número do JSON) ou número.
bool read_ids(const json::Value& args, std::vector<u64>& out) {
    const json::Value* ids = args.get("ids");
    if (!ids || !ids->is_array() || ids->array.size() > 500) return false;
    for (const auto& v : ids->array) {
        if (v.is_string()) {
            char* end = nullptr;
            const unsigned long long n = std::strtoull(v.string.c_str(), &end, 10);
            if (!end || *end || v.string.empty() || n == 0) return false;
            out.push_back(static_cast<u64>(n));
        } else if (v.is_number() && v.number > 0 && std::floor(v.number) == v.number) {
            out.push_back(static_cast<u64>(v.number));
        } else return false;
    }
    return true;
}

} // namespace

std::string tools_json() {
    std::string out = "[";
    bool first = true;
    for (const Tool& t : kTools) {
        json::Writer w;
        w.begin_object().key("name").value(t.name).key("description").value(t.description).end_object();
        // "parameters" já é JSON: entra cru antes de fechar o objeto da função.
        std::string fn = w.str();
        fn.insert(fn.size() - 1, std::string(",\"parameters\":") + t.parameters);
        out += (first ? "" : ",") + std::string("{\"type\":\"function\",\"function\":") + fn + "}";
        first = false;
    }
    return out + "]";
}

std::string system_prompt(const std::string& language, const std::string& appVersion) {
    std::string p =
        "You are the AI assistant inside AUREA " + appVersion + ", a mobile video and motion-design editor modeled on "
        "After Effects. You can act in the open project through tools; do the work instead of explaining how.\n"
        "Rules:\n"
        "- Always answer in the user's language (app language: " + language + "). Be brief and friendly.\n"
        "- Start by calling get_project_state when you need to know what exists. Refer to layers by id.\n"
        "- To create or change content (text, shapes, solids, nulls, cameras, lights, transform, keyframes, easing, "
        "effects, masks, text animators, expressions), write ExtendScript and call run_ae_script. Use "
        "app.project.activeItem as the composition; times are in seconds; positions in pixels of the composition "
        "(origin top-left). Prefer matchNames (\"ADBE Position\"). Use list_ae_effects before adding effects you are "
        "not sure about.\n"
        "- Read the script report. If something was skipped or failed, fix the script and try again (at most twice), "
        "then tell the user honestly what worked and what did not.\n"
        "- Scripts cannot import media, render or export, make new compositions or transcribe speech: use open_panel "
        "for those and tell the user which button to tap.\n"
        "- Every script run is one undo step; the user can also say \"undo\" (use the undo tool).\n"
        "- Never claim a change you did not make. After editing, summarize in one or two sentences what changed.\n"
        "- For social-media video (Reels, TikTok, Shorts) the usual composition is 1080x1920.";
    return p;
}

} // namespace assistant

std::string Engine::assistant_call(const std::string& name, const std::string& argsJson) noexcept {
    // O motor compila sem exceções no Android: cada caminho valida e devolve JSON.
    {
        if (argsJson.size() > 256 * 1024) return assistant::error("arguments too large");
        json::Value args;
        if (!argsJson.empty() && argsJson != "null") {
            if (!json::parse(argsJson, args) || !args.is_object()) return assistant::error("arguments must be a JSON object");
        }
        auto str = [&](const char* key) -> std::string {
            const json::Value* v = args.get(key);
            return v && v->is_string() ? v->string : std::string{};
        };
        auto steps = [&]() -> u32 {
            const json::Value* v = args.get("steps");
            if (!v || !v->is_number() || !std::isfinite(v->number)) return 1;
            return static_cast<u32>(std::clamp(v->number, 1.0, 50.0));
        };

        if (name == "get_project_state") {
            std::lock_guard<std::mutex> lock(modelMutex_);
            const Composition* comp = project_ ? current_composition() : nullptr;
            if (!comp) return assistant::error("no project open");
            const f64 fps = comp->fps() > 0 ? comp->fps() : 30.0;
            json::Writer w;
            w.begin_object().key("ok").value(true);
            w.key("composition").begin_object()
                .key("name").value(comp->name()).key("width").value(comp->width()).key("height").value(comp->height())
                .key("fps").value(fps).key("durationSeconds").value(static_cast<f64>(comp->duration().value) / fps).end_object();
            w.key("playheadSeconds").value(static_cast<f64>(playback_.current().value) / fps);
            w.key("viewMode").value(assistant::kViewNames[std::min(viewMode_, kViewModeCount - 1)]);
            w.key("selection").begin_array();
            for (u64 id : selection_) w.value(std::to_string(id));
            w.end_array();
            w.key("layers").begin_array();
            const u32 n = static_cast<u32>(comp->order().size());
            for (u32 z = 0; z < n; ++z) {
                const LayerId id = comp->order().at(z);
                const Layer* l = comp->layer(id);
                if (!l) continue;
                w.begin_object().key("id").value(std::to_string(id.pack()))
                    // Índice do AE (1 = a de cima), o mesmo de comp.layer(i) no script.
                    .key("index").value(static_cast<i64>(n - z))
                    .key("name").value(l->name).key("kind").value(assistant::kind_name(l->kind))
                    .key("startSeconds").value(static_cast<f64>(l->start.value) / fps)
                    .key("endSeconds").value(static_cast<f64>(l->end.value) / fps);
                if (l->threeD) w.key("threeD").value(true);
                if (!l->visible) w.key("visible").value(false);
                if (l->locked) w.key("locked").value(true);
                if (!l->captions.empty()) w.key("captionBlocks").value(static_cast<u32>(l->captions.size()));
                if (l->kind == LayerKind::Text && l->captions.empty()) w.key("text").value(l->text.content.substr(0, 200));
                if (!l->effects.empty()) {
                    w.key("effects").begin_array();
                    for (const auto& fx : l->effects) {
                        const Effect* e = effectRegistry_.find(fx.type);
                        w.value(e ? e->info().name : "?");
                    }
                    w.end_array();
                }
                w.end_object();
            }
            w.end_array().end_object();
            return w.str();
        }
        if (name == "run_ae_script") {
            const std::string code = str("code");
            if (code.empty()) return assistant::error("missing code");
            if (code.size() > 128 * 1024) return assistant::error("script too large");
            const ae::ImportReport report = import_ae_data(code, "assistant.jsx", 0, false);
            return std::string("{\"ok\":") + (report.ok ? "true" : "false") + ",\"report\":" + ae::report_json(report) + "}";
        }
        if (name == "list_ae_effects") {
            const std::string query = assistant::lower(str("query"));
            json::Writer w;
            w.begin_object().key("ok").value(true).key("effects").begin_array();
            u32 listed = 0;
            for (u32 i = 0; i < ae::detail::rule_count() && listed < 60; ++i) {
                const ae::detail::ERule& r = ae::detail::rule_at(i);
                if (!r.key) continue;   // sem equivalente no Aurea
                if (!query.empty() && assistant::lower(r.aeName).find(query) == std::string::npos
                    && assistant::lower(r.ae).find(query) == std::string::npos) continue;
                w.begin_object().key("matchName").value(r.ae).key("name").value(r.aeName);
                w.key("params").begin_array();
                for (u32 p = 0; p < r.paramCount && p < 24; ++p) if (r.params[p].aeName && *r.params[p].aeName) w.value(r.params[p].aeName);
                w.end_array().end_object();
                ++listed;
            }
            w.end_array().end_object();
            return w.str();
        }
        if (name == "select_layers") {
            std::vector<u64> ids;
            if (!assistant::read_ids(args, ids)) return assistant::error("ids must be a list of layer ids");
            if (ids.empty()) clear_selection(); else set_selection(ids.data(), static_cast<u32>(ids.size()));
            return assistant::ok();
        }
        if (name == "delete_layers") {
            std::vector<u64> ids;
            if (!assistant::read_ids(args, ids) || ids.empty()) return assistant::error("ids must be a non-empty list of layer ids");
            Command begin; begin.type = CommandType::UndoBeginGroup;
            (void)apply_command(begin, "assistente: apagar camadas");
            u32 deleted = 0;
            for (u64 id : ids) {
                Command del; del.type = CommandType::LayerDelete; del.layer_ref.layer = LayerId::unpack(id);
                if (apply_command(del).ok()) ++deleted;
            }
            Command end; end.type = CommandType::UndoEndGroup;
            (void)apply_command(end);
            json::Writer w;
            w.begin_object().key("ok").value(deleted > 0).key("deleted").value(deleted).end_object();
            return w.str();
        }
        if (name == "undo" || name == "redo") {
            const u32 n = steps();
            u32 done = 0;
            for (u32 i = 0; i < n; ++i) {
                Command c; c.type = name == "undo" ? CommandType::Undo : CommandType::Redo;
                if (!apply_command(c).ok()) break;
                ++done;
            }
            json::Writer w;
            w.begin_object().key("ok").value(done > 0).key("steps").value(done).end_object();
            return w.str();
        }
        if (name == "seek") {
            const json::Value* s = args.get("seconds");
            if (!s || !s->is_number() || !std::isfinite(s->number) || s->number < 0) return assistant::error("seconds must be a number >= 0");
            f64 fps = 30.0;
            {
                std::lock_guard<std::mutex> lock(modelMutex_);
                const Composition* comp = project_ ? current_composition() : nullptr;
                if (!comp) return assistant::error("no project open");
                fps = comp->fps() > 0 ? comp->fps() : 30.0;
            }
            Command c; c.type = CommandType::PlaybackSeek;
            c.seek.time = tick_at(FrameIndex{static_cast<i64>(std::llround(s->number * fps))}, fps);
            return apply_command(c).ok() ? assistant::ok() : assistant::error("could not seek");
        }
        if (name == "playback") {
            const std::string action = str("action");
            if (action != "play" && action != "pause") return assistant::error("action must be play or pause");
            Command c; c.type = action == "play" ? CommandType::PlaybackPlay : CommandType::PlaybackPause;
            return apply_command(c).ok() ? assistant::ok() : assistant::error("playback failed");
        }
        if (name == "set_view") {
            const std::string mode = str("mode");
            for (u32 i = 0; i < kViewModeCount; ++i)
                if (mode == assistant::kViewNames[i]) return set_view_mode(i) ? assistant::ok() : assistant::error("could not change view");
            return assistant::error("unknown view mode");
        }
        if (name == assistant::kUiOpenPanel) {
            const std::string panel = str("panel");
            static const char* panels[] = {"media", "export", "captions", "effects", "text", "audio", "scene3d"};
            if (std::find_if(std::begin(panels), std::end(panels), [&](const char* p) { return panel == p; }) == std::end(panels))
                return assistant::error("unknown panel");
            json::Writer w;
            w.begin_object().key("ok").value(true).key("ui").value(assistant::kUiOpenPanel).key("panel").value(panel).end_object();
            return w.str();
        }
        return assistant::error("unknown tool: " + name);
    }
}

} // namespace aurea
