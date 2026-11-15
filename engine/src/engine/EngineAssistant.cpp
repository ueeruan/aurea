// =============================================================================
//  Aurea / engine / EngineAssistant.cpp — ver assistant/Assistant.hpp
// =============================================================================
#include "aurea/assistant/Assistant.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/project/Presets.hpp"
#include "../project/AeImportBind.hpp"
#include "EngineMcpTools.hpp"

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

    // --- AureaMCP fase C (EngineMcpTools.cpp): ler e escrever com precisão -----------
    // Unidades: segundos da composição, escala em fração (1 = 100%), opacidade 0..1,
    // rotação em graus, px, cores [r,g,b(,a)] 0..1. "layer" = id (texto) ou índice do AE.
    {"get_layer_details",
     "Read one layer in full at the playhead (or at seconds): name, kind, timing, parent, blend, 3D, transform "
     "(value of position/scale/rotation/anchor/opacity/skew and how many keyframes each has), the effect stack "
     "(effect id, key, name, enabled) and kind-specific data (text content/font/size/colors/alignment/tracking, "
     "shape fill/stroke, camera lens, light, audio volume). Units: seconds, px, scale 1 = 100%, opacity 0..1, degrees.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"],"description":"Layer id from get_project_state, or the AE index (1 = top)."},"seconds":{"type":"number","minimum":0}},"required":["layer"],"additionalProperties":false})"},
    {"get_keyframes",
     "List the keyframes of a property (each channel: seconds in composition time, value, interpolation, bezier "
     "[x1,y1,x2,y2] of the segment that LEAVES the key, power). Omit property to list every animated channel of the layer. "
     "Property: position, position.x, scale, rotation, anchor, opacity, skew.x, audio.volume, text.tracking, "
     "effect.<effectId>.<param index or id>[.x|.y|.z|.r|.g|.b|.a].",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"}},"required":["layer"],"additionalProperties":false})"},
    {"set_property",
     "Set a property value. With seconds, or when the property already has keyframes, it writes a keyframe at that "
     "time (playhead by default), like After Effects; otherwise it changes the static value. One undo step. "
     "value: number or array ([x,y] / [x,y,z] / [r,g,b,a]); rotation as a number = Z; scale as a number = uniform; "
     "a layer-reference effect parameter takes a layer id string. Property syntax as in get_keyframes.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"},"value":{"type":["number","array","string"],"items":{"type":"number"}},"seconds":{"type":"number","minimum":0}},"required":["layer","property","value"],"additionalProperties":false})"},
    {"add_keyframes",
     "Add (or overwrite) several keyframes on one property in one undo step. Each key: seconds (composition time), "
     "value (number or array) and optional easing for the segment that starts at that key: interpolation "
     "(hold, linear, bezier, ease_in, ease_out, ease_in_out, bounce, elastic, overshoot, steps, repeat, ping_pong, wave, random), "
     "bezier [x1,y1,x2,y2] (x 0..1) and power 1..3 (stronger ease).",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"},"keys":{"type":"array","minItems":1,"maxItems":500,"items":{"type":"object","properties":{"seconds":{"type":"number","minimum":0},"value":{"type":["number","array"],"items":{"type":"number"}},"interpolation":{"type":"string"},"bezier":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"power":{"type":"integer","minimum":1,"maximum":3}},"required":["seconds","value"]}}},"required":["layer","property","keys"],"additionalProperties":false})"},
    {"delete_keyframes",
     "Delete keyframes of a property at the given times (seconds: number or list), or all of them with all: true. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"},"seconds":{"type":["number","array"],"items":{"type":"number"}},"all":{"type":"boolean"}},"required":["layer","property"],"additionalProperties":false})"},
    {"move_keyframe",
     "Move the keyframe of a property (every channel that has one) from one time to another. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"},"fromSeconds":{"type":"number","minimum":0},"toSeconds":{"type":"number","minimum":0}},"required":["layer","property","fromSeconds","toSeconds"],"additionalProperties":false})"},
    {"set_keyframe_easing",
     "Set the easing of the segment that leaves each keyframe (all keys of the property, or only those at seconds). "
     "interpolation and/or bezier [x1,y1,x2,y2] (x 0..1, e.g. [0.33,0,0.67,1] = easy ease), power 1..3. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"property":{"type":"string"},"seconds":{"type":["number","array"],"items":{"type":"number"}},"interpolation":{"type":"string"},"bezier":{"type":"array","items":{"type":"number"},"minItems":4,"maxItems":4},"power":{"type":"integer","minimum":1,"maximum":3}},"required":["layer","property"],"additionalProperties":false})"},
    {"list_effect_types",
     "Search the effects Aurea can add (key, name, category, parameter count and the After Effects matchName when there is one).",
     R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":300}},"additionalProperties":false})"},
    {"get_effect",
     "Read one effect of a layer: every parameter with index, id, label, type, value at the playhead (or seconds), default, "
     "range, unit, options and keyframe count. Change them with set_property effect.<effectId>.<index or id>.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"effect":{"type":"integer","minimum":0},"seconds":{"type":"number","minimum":0}},"required":["layer","effect"],"additionalProperties":false})"},
    {"add_effect",
     "Add an effect to a layer by key (from list_effect_types), name or AE matchName. index = position in the stack "
     "(0 = first; omit for the end). Returns the new effect id. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"type":{"type":"string"},"index":{"type":"integer","minimum":0}},"required":["layer","type"],"additionalProperties":false})"},
    {"remove_effect", "Remove an effect (id from get_layer_details) and its keyframes. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"effect":{"type":"integer","minimum":0}},"required":["layer","effect"],"additionalProperties":false})"},
    {"move_effect", "Move an effect in the layer's stack (index 0 = first, applied first). One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"effect":{"type":"integer","minimum":0},"index":{"type":"integer","minimum":0}},"required":["layer","effect","index"],"additionalProperties":false})"},
    {"set_effect_enabled", "Turn an effect on or off without removing it. One undo step.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"effect":{"type":"integer","minimum":0},"enabled":{"type":"boolean"}},"required":["layer","effect","enabled"],"additionalProperties":false})"},
    {"list_presets",
     "List presets: kind text = the text animation presets (id, name) for text layers; kind effect = the presets of one "
     "effect on a layer (needs layer and effect).",
     R"({"type":"object","properties":{"kind":{"type":"string","enum":["text","effect"]},"layer":{"type":["string","integer"]},"effect":{"type":"integer","minimum":0}},"additionalProperties":false})"},
    {"apply_preset",
     "Apply a preset to a layer (one undo step): kind text (preset id or name from list_presets; replaces the text animation), "
     "kind effect (preset of the effect id), or kind json (an Aurea preset JSON with aurea_preset 1: effects, text style, "
     "animation; durationSeconds stretches an animation preset).",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"kind":{"type":"string","enum":["text","effect","json"]},"preset":{"type":["integer","string"]},"effect":{"type":"integer","minimum":0},"json":{"type":"string"},"durationSeconds":{"type":"number","minimum":0}},"required":["layer","kind"],"additionalProperties":false})"},
    {"list_fonts", "List the font families on the device (and imported ones) with their styles, weights and italics.",
     R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":400}},"additionalProperties":false})"},
    {"set_text_style",
     "Change a text layer in one undo step: text (content), font (family from list_fonts), weight 100..900, italic, size px, "
     "color [r,g,b(,a)], strokeWidth px, strokeColor, alignment (left/center/right), tracking (thousandths of an em), lineHeight.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]},"text":{"type":"string"},"font":{"type":"string"},"weight":{"type":"integer","minimum":100,"maximum":1000},"italic":{"type":"boolean"},"size":{"type":"number","minimum":1,"maximum":2000},"color":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":4},"strokeWidth":{"type":"number","minimum":0,"maximum":200},"strokeColor":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":4},"alignment":{"type":"string","enum":["left","center","right"]},"tracking":{"type":"number"},"lineHeight":{"type":"number","minimum":0.1,"maximum":10}},"required":["layer"],"additionalProperties":false})"},
    {"get_markers", "Read the composition markers (and a layer's markers when layer is given): seconds, label, comment, duration, color.",
     R"({"type":"object","properties":{"layer":{"type":["string","integer"]}},"additionalProperties":false})"},
    {"set_marker",
     "Create or update the marker at seconds (composition, or on a layer when layer is given): label, comment, "
     "durationSeconds, color #RRGGBB. Fields you omit keep their value. One undo step.",
     R"({"type":"object","properties":{"seconds":{"type":"number","minimum":0},"layer":{"type":["string","integer"]},"label":{"type":"string"},"comment":{"type":"string"},"durationSeconds":{"type":"number","minimum":0},"color":{"type":"string","pattern":"^#?[0-9A-Fa-f]{6}$"}},"required":["seconds"],"additionalProperties":false})"},
    {"delete_marker", "Delete the marker at seconds (composition, or the layer's). One undo step.",
     R"({"type":"object","properties":{"seconds":{"type":"number","minimum":0},"layer":{"type":["string","integer"]}},"required":["seconds"],"additionalProperties":false})"},
    {"get_work_area", "Read the work area (start/end seconds; active = smaller than the composition).",
     R"({"type":"object","properties":{},"additionalProperties":false})"},
    {"set_work_area", "Set the work area (startSeconds < endSeconds) or clear it with clear: true. Preview loops inside it. One undo step.",
     R"({"type":"object","properties":{"startSeconds":{"type":"number","minimum":0},"endSeconds":{"type":"number","minimum":0},"clear":{"type":"boolean"}},"additionalProperties":false})"},
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
        // AureaMCP fase C: as ferramentas de leitura/escrita precisas (EngineMcpTools.cpp).
        if (std::string out; mcp_tools::call(*this, modelMutex_, name, args, out)) return out;
        return assistant::error("unknown tool: " + name);
    }
}

} // namespace aurea
