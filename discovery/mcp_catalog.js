// =============================================================================
//  AureaMCP — o catálogo de ferramentas (docs/aureamcp/DESIGN.md §2).
//
//  Três origens, uma lista só no tools/list:
//    1. SESSÃO (este Worker): aurea_status, aurea_select_device.
//    2. MOTOR (Engine::assistant_call): o app manda a lista no "hello" (formato
//       "tools" da OpenAI, a mesma que o chat antigo usava). Ferramenta nova no
//       motor aparece aqui sem deploy. Sem app conectado ainda, vale a cópia
//       DEFAULT_ENGINE_TOOLS abaixo (0.5.3).
//    3. HOST (app: EditorStore no Android/PC, AureaModel no iOS): o esquema mora
//       AQUI (uma fonte para as duas plataformas); o app só diz quais executa.
//  AureaOS (aureaos.js) entra por último, também do Worker.
//
//  Cada ferramenta do app tem uma CAPACIDADE: o interruptor de permissão no app
//  que a libera. O Worker esconde as desligadas; o app recusa de novo ao executar.
// =============================================================================

export const CAPABILITIES = ["read", "edit", "projects", "delete", "media", "device_media", "export"];

const obj = (properties = {}, required = []) => ({ type: "object", properties, ...(required.length ? { required } : {}), additionalProperties: false });
const RO = { readOnlyHint: true, destructiveHint: false, openWorldHint: false };
const EDIT = { readOnlyHint: false, destructiveHint: false, openWorldHint: false };

/** Ferramentas da sessão: respondidas pelo Worker/relay, sem o app. */
export const SESSION_TOOLS = [
  {
    name: "aurea_status",
    title: "AureaMCP status",
    description: "Show which Aurea apps (phone/PC) are online for this account, which one receives the calls, the open project, the permissions the user granted, whether the user paused agents, and the AureaOS brain version. Call this first.",
    inputSchema: obj(),
    annotations: RO,
  },
  {
    name: "aurea_select_device",
    title: "Choose the Aurea device",
    description: "Send the next calls to another online Aurea app (id from aurea_status).",
    inputSchema: obj({ device: { type: "string", description: "Device id from aurea_status." } }, ["device"]),
    annotations: { ...EDIT, idempotentHint: true },
  },
  {
    name: "job_status",
    title: "Queued call status",
    description: "When the Aurea app is closed or asleep, light calls are QUEUED and the app is woken up (Android at once; iOS when the person taps the notification). The call then answers \"queued\" with a job_id. Pass that job_id here to get the state (queued, sent, done, failed, expired) and, when done, the tool result. Without job_id, lists your recent jobs.",
    inputSchema: obj({ job_id: { type: "string", description: "The job_id a queued call returned." } }),
    annotations: RO,
  },
  {
    name: "aurea_queue_policy",
    title: "Queue heavy calls",
    description: "By default, heavy calls (run_ae_script, render_frame, export_video, imports, opening or creating projects) fail at once when the Aurea app is not connected. Set allowHeavy true to queue them for this session too; they run in order when the app reconnects. Calls that need the person's approval (delete_project) are never queued.",
    inputSchema: obj({ allowHeavy: { type: "boolean" } }, ["allowHeavy"]),
    annotations: { ...EDIT, idempotentHint: true },
  },
];

/** Ferramentas do app fora do motor. `capability` = interruptor; `confirm` = o app sempre pergunta. */
export const HOST_TOOLS = [
  {
    name: "list_projects", capability: "read",
    description: "List the projects saved in the Aurea app: id, title, size, fps, duration in seconds, last change. Use the id with open_project, duplicate_project or delete_project.",
    inputSchema: obj({ query: { type: "string", description: "Part of the title." }, limit: { type: "integer", minimum: 1, maximum: 200 } }),
    annotations: RO,
  },
  {
    name: "open_project", capability: "projects",
    description: "Open a saved project in the editor (the current one is saved first).",
    inputSchema: obj({ id: { type: "string" } }, ["id"]),
    annotations: EDIT,
  },
  {
    name: "create_project", capability: "projects",
    description: "Create a new empty project and open it. Social video (Reels/TikTok/Shorts) is usually 1080x1920 at 30 fps.",
    inputSchema: obj({
      title: { type: "string", maxLength: 120 },
      width: { type: "integer", minimum: 16, maximum: 8192 },
      height: { type: "integer", minimum: 16, maximum: 8192 },
      fps: { type: "number", minimum: 1, maximum: 120 },
      background: { type: "array", items: { type: "number", minimum: 0, maximum: 1 }, minItems: 3, maxItems: 3, description: "RGB 0..1; omit for black." },
    }, ["title", "width", "height", "fps"]),
    annotations: EDIT,
  },
  {
    name: "rename_project", capability: "projects",
    description: "Rename the open project.",
    inputSchema: obj({ title: { type: "string", minLength: 1, maxLength: 120 } }, ["title"]),
    annotations: { ...EDIT, idempotentHint: true },
  },
  {
    name: "duplicate_project", capability: "projects",
    description: "Make a copy of a saved project.",
    inputSchema: obj({ id: { type: "string" } }, ["id"]),
    annotations: EDIT,
  },
  {
    name: "delete_project", capability: "delete", confirm: true,
    description: "Delete a saved project. The person must approve it in the app; it cannot be undone.",
    inputSchema: obj({ id: { type: "string" } }, ["id"]),
    annotations: { readOnlyHint: false, destructiveHint: true, openWorldHint: false },
  },
  {
    name: "save_project", capability: "edit",
    description: "Save the open project now (Aurea also saves on its own).",
    inputSchema: obj(),
    annotations: { ...EDIT, idempotentHint: true },
  },
  {
    name: "close_project", capability: "projects",
    description: "Save and close the open project (back to the app's Home).",
    inputSchema: obj(),
    annotations: EDIT,
  },
  {
    name: "render_frame", capability: "read",
    description: "Render the composition at a time and return it as an image, to check your work. Uses the playhead when seconds is omitted.",
    inputSchema: obj({
      seconds: { type: "number", minimum: 0 },
      maxSize: { type: "integer", minimum: 128, maximum: 1280, description: "Longest side in pixels (default 768)." },
    }),
    annotations: RO,
  },
  {
    name: "export_video", capability: "export",
    description: "Export the open composition to the device gallery. Starts a job and returns at once; poll export_status.",
    inputSchema: obj({
      shortSide: { type: "integer", enum: [480, 720, 1080, 1440, 2160], description: "Short side in pixels (default 1080)." },
      fps: { type: "number", minimum: 0, maximum: 120, description: "0 = the composition's." },
      codec: { type: "string", enum: ["h264", "hevc"] },
      quality: { type: "string", enum: ["low", "standard", "high"] },
      format: { type: "string", enum: ["video", "gif", "png"], description: "png = still image of the playhead frame." },
    }),
    annotations: EDIT,
  },
  {
    name: "export_status", capability: "export",
    description: "Progress of the current export: phase, frames done/total, ETA, and the saved file name when done.",
    inputSchema: obj(),
    annotations: RO,
  },
  {
    name: "cancel_export", capability: "export",
    description: "Cancel the running export.",
    inputSchema: obj(),
    annotations: EDIT,
  },
  {
    name: "import_media_url", capability: "media",
    description: "Download a video, image or audio file from an https URL into the device and add it to the open composition as a new layer.",
    inputSchema: obj({
      url: { type: "string", format: "uri", pattern: "^https://" },
      overlay: { type: "boolean", description: "true = on top of the selection as an overlay; false = into the main track." },
    }, ["url"]),
    annotations: { readOnlyHint: false, destructiveHint: false, openWorldHint: true },
  },
  {
    name: "list_device_media", capability: "device_media",
    description: "List recent photos and videos in the device gallery, newest first (id, name, kind, date, width, height, bytes, duration). Only what the person let Aurea see: with limited access (iOS 'selected photos', Android 14 'selected media') only those items appear.",
    inputSchema: obj({ kind: { type: "string", enum: ["all", "video", "image"] }, limit: { type: "integer", minimum: 1, maximum: 100 }, beforeId: { type: "string", description: "Page: the last id of the previous answer." } }),
    annotations: RO,
  },
  {
    name: "import_device_media", capability: "device_media",
    description: "Add gallery items (ids from list_device_media) to the open composition, in order. overlay = on top of the selection; otherwise into the main track. trimInSeconds/trimOutSeconds (optional) trim every video: only that range (plus a small margin) is copied into Aurea, saving space on long videos.",
    inputSchema: obj({
      ids: { type: "array", items: { type: "string" }, minItems: 1, maxItems: 20 },
      overlay: { type: "boolean" },
      trimInSeconds: { type: "number", minimum: 0, maximum: 86400, description: "Video start, in seconds of the original (default 0)." },
      trimOutSeconds: { type: "number", minimum: 0, maximum: 86400, description: "Video end, in seconds of the original (default: the end). Must be greater than trimInSeconds." },
    }, ["ids"]),
    annotations: EDIT,
  },
];

/** Ferramentas do motor que só leem (o espelho de McpCatalog.ENGINE_READ no Kotlin e no Swift). */
export const ENGINE_READ = new Set(["get_project_state", "list_ae_effects", "seek", "playback", "set_view",
  "get_layer_details", "get_keyframes", "list_effect_types", "get_effect", "list_presets", "list_fonts", "get_markers", "get_work_area"]);
/** Capacidade de uma ferramenta do motor: leitura conhecida = read; o resto mexe no projeto. */
export const engineCapability = (name) => (ENGINE_READ.has(name) ? "read" : "edit");

const LAYER = { type: ["string", "integer"], description: "Layer id from get_project_state, or the AE index (1 = top)." };
const PROP = { type: "string" };
const SECONDS = { type: "number", minimum: 0 };
const TIMES = { type: ["number", "array"], items: { type: "number" } };
const VALUE = { type: ["number", "array", "string"], items: { type: "number" } };
const EFFECT = { type: "integer", minimum: 0, description: "Effect id from get_layer_details." };

/** Cópia da lista do motor 0.5.3 (Engine::assistant_call) para quando nenhum app conectou ainda. */
export const DEFAULT_ENGINE_TOOLS = [
  ["get_project_state", "Read the open composition: size, fps, duration, playhead, selection, view mode and every layer (id, AE index, name, kind, time range in seconds, 3D, visible, locked, effects). Call it before editing and again after a script to confirm the result.", obj()],
  ["run_ae_script", "Run After Effects ExtendScript (ES3) on the open composition, in a sandbox. One run = one undo step. Layers, properties (matchNames like \"ADBE Position\"), keyframes, easing, effects, masks, text and text animators, expressions. Colors are [r,g,b] 0..1. Not available: new compositions, files, importing, rendering. Returns a report of what was applied and skipped.", obj({ code: { type: "string", description: "The ExtendScript source." } }, ["code"])],
  ["list_ae_effects", "Search the After Effects effects this app supports (matchName, name, parameter names). Empty query lists them all.", obj({ query: { type: "string" } })],
  ["select_layers", "Select layers by id (from get_project_state). An empty list clears the selection.", obj({ ids: { type: "array", items: { type: "string" } } }, ["ids"])],
  ["delete_layers", "Delete layers by id (from get_project_state). One undo step.", obj({ ids: { type: "array", items: { type: "string" } } }, ["ids"])],
  ["undo", "Undo the last steps (default 1).", obj({ steps: { type: "integer", minimum: 1, maximum: 50 } })],
  ["redo", "Redo steps that were undone (default 1).", obj({ steps: { type: "integer", minimum: 1, maximum: 50 } })],
  ["seek", "Move the playhead to a time in seconds.", obj({ seconds: { type: "number", minimum: 0 } }, ["seconds"])],
  ["playback", "Play or pause the preview.", obj({ action: { type: "string", enum: ["play", "pause"] } }, ["action"])],
  ["set_view", "Change the preview view like After Effects (preview only).", obj({ mode: { type: "string", enum: ["active_camera", "default_camera", "front", "back", "left", "right", "top", "bottom", "custom_view_1", "custom_view_2", "custom_view_3"] } }, ["mode"])],
  ["open_panel", "Open an editor screen for the person (media, export, captions, effects, text, audio, scene3d) and tell them what to tap.", obj({ panel: { type: "string", enum: ["media", "export", "captions", "effects", "text", "audio", "scene3d"] } }, ["panel"])],
  // Fase C (EngineMcpTools.cpp). A lista viva vem do app; esta é a cópia para o agente ver antes do app conectar.
  ["get_layer_details", "Read one layer in full: timing, parent, transform values and keyframe counts, effect stack (ids), text/shape/camera/light/audio data.", obj({ layer: LAYER, seconds: SECONDS }, ["layer"])],
  ["get_keyframes", "List keyframes of a property (seconds, value, interpolation, bezier); omit property for every animated channel. Property: position[.x], scale, rotation, anchor, opacity, skew.x, audio.volume, text.tracking, effect.<id>.<param>.", obj({ layer: LAYER, property: PROP }, ["layer"])],
  ["set_property", "Set a property value; with seconds (or when already animated) it writes a keyframe. Scale 1 = 100%, opacity 0..1, degrees, px. One undo step.", obj({ layer: LAYER, property: PROP, value: VALUE, seconds: SECONDS }, ["layer", "property", "value"])],
  ["add_keyframes", "Add several keyframes in one undo step; each {seconds, value, interpolation?, bezier? [x1,y1,x2,y2], power? 1..3}.", obj({ layer: LAYER, property: PROP, keys: { type: "array", minItems: 1, maxItems: 500, items: { type: "object" } } }, ["layer", "property", "keys"])],
  ["delete_keyframes", "Delete keyframes at seconds (number or list) or all: true.", obj({ layer: LAYER, property: PROP, seconds: TIMES, all: { type: "boolean" } }, ["layer", "property"])],
  ["move_keyframe", "Move a keyframe from fromSeconds to toSeconds.", obj({ layer: LAYER, property: PROP, fromSeconds: SECONDS, toSeconds: SECONDS }, ["layer", "property", "fromSeconds", "toSeconds"])],
  ["set_keyframe_easing", "Set the easing of the segment leaving each keyframe (all, or those at seconds): interpolation, bezier [x1,y1,x2,y2], power.", obj({ layer: LAYER, property: PROP, seconds: TIMES, interpolation: { type: "string" }, bezier: { type: "array", items: { type: "number" }, minItems: 4, maxItems: 4 }, power: { type: "integer", minimum: 1, maximum: 3 } }, ["layer", "property"])],
  ["list_effect_types", "Search the effects Aurea can add (key, name, category, AE matchName).", obj({ query: { type: "string" }, limit: { type: "integer", minimum: 1, maximum: 300 } })],
  ["get_effect", "Read every parameter of one effect (index, id, label, type, value, range, options, keyframes).", obj({ layer: LAYER, effect: EFFECT, seconds: SECONDS }, ["layer", "effect"])],
  ["add_effect", "Add an effect by key, name or AE matchName; returns its id. One undo step.", obj({ layer: LAYER, type: { type: "string" }, index: { type: "integer", minimum: 0 } }, ["layer", "type"])],
  ["remove_effect", "Remove an effect and its keyframes.", obj({ layer: LAYER, effect: EFFECT }, ["layer", "effect"])],
  ["move_effect", "Move an effect in the stack (0 = first).", obj({ layer: LAYER, effect: EFFECT, index: { type: "integer", minimum: 0 } }, ["layer", "effect", "index"])],
  ["set_effect_enabled", "Turn an effect on or off.", obj({ layer: LAYER, effect: EFFECT, enabled: { type: "boolean" } }, ["layer", "effect", "enabled"])],
  ["list_presets", "List text animation presets (kind text) or one effect's presets (kind effect, with layer and effect).", obj({ kind: { type: "string", enum: ["text", "effect"] }, layer: LAYER, effect: EFFECT })],
  ["apply_preset", "Apply a text preset, an effect preset or an Aurea preset JSON to a layer. One undo step.", obj({ layer: LAYER, kind: { type: "string", enum: ["text", "effect", "json"] }, preset: { type: ["integer", "string"] }, effect: EFFECT, json: { type: "string" }, durationSeconds: SECONDS }, ["layer", "kind"])],
  ["list_fonts", "List the device's font families and styles.", obj({ query: { type: "string" }, limit: { type: "integer", minimum: 1, maximum: 400 } })],
  ["set_text_style", "Change text content, font, weight, italic, size, color, stroke, alignment, tracking, lineHeight in one undo step.", obj({ layer: LAYER, text: { type: "string" }, font: { type: "string" }, weight: { type: "integer" }, italic: { type: "boolean" }, size: { type: "number" }, color: { type: "array", items: { type: "number" } }, strokeWidth: { type: "number" }, strokeColor: { type: "array", items: { type: "number" } }, alignment: { type: "string", enum: ["left", "center", "right"] }, tracking: { type: "number" }, lineHeight: { type: "number" } }, ["layer"])],
  ["get_markers", "Read composition markers (and a layer's, with layer).", obj({ layer: LAYER })],
  ["set_marker", "Create or update the marker at seconds (label, comment, durationSeconds, color #RRGGBB; layer for a layer marker).", obj({ seconds: SECONDS, layer: LAYER, label: { type: "string" }, comment: { type: "string" }, durationSeconds: SECONDS, color: { type: "string" } }, ["seconds"])],
  ["delete_marker", "Delete the marker at seconds.", obj({ seconds: SECONDS, layer: LAYER }, ["seconds"])],
  ["get_work_area", "Read the work area.", obj()],
  ["set_work_area", "Set the work area (startSeconds < endSeconds) or clear: true.", obj({ startSeconds: SECONDS, endSeconds: SECONDS, clear: { type: "boolean" } })],
].map(([name, description, parameters]) => ({ type: "function", function: { name, description, parameters } }));

const NAME = /^[a-z][a-z0-9_]{1,63}$/;
const RESERVED = new Set([...SESSION_TOOLS.map((t) => t.name), "search", "fetch"]);

/**
 * Converte a lista do motor (formato OpenAI) para ferramentas MCP. Defensivo:
 * vem do app (o app é da própria conta, mas um app velho ou corrompido não
 * pode derrubar o tools/list nem sobrepor ferramenta do servidor).
 */
export function engineToolsToMcp(list) {
  const out = [];
  if (!Array.isArray(list)) return out;
  for (const t of list.slice(0, 120)) {
    const fn = t?.function ?? t;
    const name = typeof fn?.name === "string" ? fn.name : "";
    if (!NAME.test(name) || RESERVED.has(name) || name.startsWith("aureaos_") || HOST_TOOLS.some((h) => h.name === name)) continue;
    const description = typeof fn.description === "string" ? fn.description.slice(0, 4000) : "";
    const schema = fn.parameters && typeof fn.parameters === "object" && !Array.isArray(fn.parameters) ? fn.parameters : obj();
    if (JSON.stringify(schema).length > 16_000) continue;
    const capability = engineCapability(name);
    out.push({ name, description, inputSchema: { type: "object", ...schema }, annotations: capability === "read" ? RO : EDIT, capability, origin: "engine" });
  }
  return out;
}

/** Ferramentas que o dispositivo oferece, filtradas pelas permissões que ele mandou. */
export function deviceTools(hello) {
  const permissions = hello?.permissions ?? {};
  const allowed = (cap) => permissions[cap] !== false && (cap !== "device_media" || permissions[cap] === true);
  const engine = engineToolsToMcp(hello?.engineTools ?? DEFAULT_ENGINE_TOOLS);
  const hostNames = new Set(Array.isArray(hello?.hostTools) ? hello.hostTools : HOST_TOOLS.filter((h) => !h.name.includes("device_media")).map((h) => h.name));
  const host = HOST_TOOLS.filter((h) => hostNames.has(h.name)).map((h) => ({ ...h, origin: "host" }));
  return [...engine, ...host].filter((t) => allowed(t.capability));
}

/** O que vai no tools/list (sem os campos internos). */
export const publicTool = ({ name, title, description, inputSchema, annotations }) =>
  ({ name, ...(title ? { title } : {}), description, inputSchema, ...(annotations ? { annotations } : {}) });

/** Ferramentas pesadas: uma por vez por conta (regra de CPU do aparelho). */
export const HEAVY = new Set(["run_ae_script", "render_frame", "export_video", "import_media_url", "import_device_media", "open_project", "create_project", "duplicate_project"]);

/** Ferramentas só de interface (nada a fazer com a pessoa fora do app): nunca vão para a fila. */
export const UI_ONLY = new Set(["open_panel", "playback", "set_view"]);

/**
 * Pode ir para a fila quando nenhum app está conectado?
 *   "never"  confirmação da pessoa, destrutiva, ou só de interface (falha na hora);
 *   "heavy"  pesada: só com o agente pedindo (aurea_queue_policy ou _meta "aurea/queue");
 *   "yes"    leve e segura.
 * `ask` = o aparelho pede aprovação para toda edição ("Perguntar antes"): edições viram "never".
 */
export function queueClass(def, { ask = false } = {}) {
  if (!def) return "never";
  if (def.confirm === true || def.annotations?.destructiveHint === true || UI_ONLY.has(def.name)) return "never";
  if (ask && def.capability !== "read" && def.name !== "export_status") return "never";
  return HEAVY.has(def.name) ? "heavy" : "yes";
}
