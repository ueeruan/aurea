# AureaMCP: design (13/11/2026)

AureaMCP replaces the in-app AI chat (0.4.6 `assistant/`). Instead of a chat inside Aurea talking to a free OpenRouter model, the user connects the AI agent they already use (Claude.ai, Claude Desktop, Claude Code, ChatGPT, Codex, any MCP client) to their running Aurea app. The agent gets the whole editor plus the whole AureaOS method as MCP tools, prompts and resources.

Decisions made by the owner:
1. The connection goes through the cloud. The MCP server lives on the existing Worker `aurea-ai-discovery` and relays to the Aurea app the user has open on a phone or PC. The app shows a pairing flow. Any MCP client connects by URL.
2. Scope: everything. Every editor capability (the engine's assistant tools in `Engine::assistant_call`, reading the project, export, projects, files, media import) and everything from AureaOS.

The mascot (`docs/aureamcp/mascote-aureamcp.webp`, owned by the owner) is the AureaMCP brand image in the apps.

---

## 1. Architecture

```
 Claude.ai / Claude Desktop / ChatGPT          Claude Code / Codex CLI
   (remote connector, OAuth 2.1 + DCR)           (Bearer token, "amcp_…")
                 \                                   /
                  \   POST https://aurea-ai-discovery.aureaapp.workers.dev/mcp
                   v                                v
          +-----------------------------------------------------+
          |  Worker  (mcp.js)                                    |
          |   JSON-RPC 2.0 over Streamable HTTP (JSON replies)   |
          |   auth (mcp_auth.js: tokens, OAuth, pairing)  -> D1  |
          |   AureaOS brain (aureaos.js) <- R2 bundle            |
          +--------------------------+--------------------------+
                                     | RPC  (one object per account)
                                     v
          +-----------------------------------------------------+
          |  Durable Object AureaMcpRelay  ("mcp:<uid>")         |
          |   app WebSockets (hibernation), devices, sessions,   |
          |   pending calls + timeouts, pause flag, rate limit,  |
          |   activity log, last tool catalogue per device       |
          +--------------------------+--------------------------+
                                     | wss  /api/mcp/app/socket
                                     | (account session token)
                                     v
          +-----------------------------------------------------+
          |  Aurea app (Android, iOS, PC = Android Kotlin)       |
          |   AureaMCP screen (mascot, pair, status, log, stop)  |
          |   McpClient (WebSocket) -> McpExecutor               |
          |     engine tools  -> Engine::assistant_call          |
          |     host tools    -> EditorStore / AureaModel        |
          |   permission toggles + confirmation prompts          |
          +-----------------------------------------------------+
```

Why a relay and not a server in the app: phones sit behind NAT/CGNAT and sleep, and remote connectors (Claude.ai, ChatGPT) need a public HTTPS URL. The Worker gives one fixed URL; the app dials out.

### 1.1 MCP transport

- Endpoint: `POST /mcp` (Streamable HTTP, protocol `2025-06-18`; `2025-03-26` and `2025-11-25` accepted on `initialize`, the server answers with the version it speaks).
- Every POST carries one JSON-RPC message (batches are rejected, as in `2025-06-18`). Requests get one `application/json` response. Notifications and responses from the client get `202 Accepted`, no body.
- `GET /mcp` returns `405` (no server-initiated stream; nothing in AureaMCP needs to push to the agent). `DELETE /mcp` with `Mcp-Session-Id` ends the session.
- `initialize` creates a session (`Mcp-Session-Id`, 128-bit random, stored in the relay object with the client name and version). Later requests without it, or with an unknown one, get `404` / `400` as the spec says, so the client re-initialises.
- Methods: `initialize`, `ping`, `tools/list`, `tools/call`, `resources/list`, `resources/templates/list`, `resources/read`, `prompts/list`, `prompts/get`, `notifications/initialized`, `notifications/cancelled`.
- CORS is open (`*`, no credentials) because `/mcp` only accepts a Bearer token, never a cookie; the DNS-rebinding `Origin` rule in the spec targets local servers. Browser-based clients (MCP Inspector) work.
- Long work never blocks a request past ~50 s. Exports and big imports are jobs: the call starts them and `export_status` reads progress.

### 1.2 Authentication for MCP clients

Both kinds of credential map to the Aurea account (`users.id`) and live hashed in D1 (`mcp_tokens.token_hash = sha256(token)`; the raw token is shown once and never stored).

**A. Personal token (Claude Code, Codex CLI, scripts).** In the app: AureaMCP, Agents, "Token for Claude Code / Codex". The app calls `POST /api/mcp/tokens` with its session and shows the token once (`amcp_` + 43 base64url chars) plus ready-to-paste commands:

```
claude mcp add --transport http aurea https://aurea-ai-discovery.aureaapp.workers.dev/mcp \
  --header "Authorization: Bearer amcp_…"

# ~/.codex/config.toml
[mcp_servers.aurea]
url = "https://aurea-ai-discovery.aureaapp.workers.dev/mcp"
bearer_token_env_var = "AUREA_MCP_TOKEN"
```

Personal tokens expire after 365 days and can be revoked from the app.

**B. OAuth 2.1 with dynamic client registration (Claude.ai, Claude Desktop, ChatGPT connectors).** The user pastes the URL `https://aurea-ai-discovery.aureaapp.workers.dev/mcp` in the connector screen; the client discovers everything itself:

| Step | Endpoint | Spec |
|---|---|---|
| 401 with `WWW-Authenticate: Bearer resource_metadata=".../.well-known/oauth-protected-resource"` | `/mcp` | MCP auth, RFC 9728 |
| Protected resource metadata | `GET /.well-known/oauth-protected-resource` (also `/…/mcp`) | RFC 9728 |
| Authorization server metadata | `GET /.well-known/oauth-authorization-server` | RFC 8414 |
| Dynamic client registration (public clients, `token_endpoint_auth_method: none`) | `POST /mcp/oauth/register` | RFC 7591 |
| Authorization (PKCE S256 required) | `GET /mcp/oauth/authorize` | OAuth 2.1 |
| Token (`authorization_code`, `refresh_token`) | `POST /mcp/oauth/token` | OAuth 2.1, RFC 8707 `resource` |

The authorization page does not ask for a password. It shows an 8-character pairing code (`ABCD-2345`, alphabet without 0/O/1/I) and an "Open in Aurea" link (`aurea://mcp/pair?code=…`). The user opens Aurea, AureaMCP, "Connect agent", types or follows the code, sees "**Claude** (redirect to claude.ai) wants to control your Aurea. Allow?" and approves. The page polls `/mcp/oauth/authorize/status` (bound to the browser by an HttpOnly cookie) and, once approved, redirects to `redirect_uri?code=…&state=…`. The code is single-use, valid 60 s, tied to the client, the redirect URI and the PKCE challenge.

Tokens: access token 24 h (`amcp_…`), refresh token 90 days, rotated on every use (the old one is revoked; reuse of a revoked refresh token revokes the whole grant).

Phishing note (device-flow risk): an attacker could send someone a code from their own authorization. The approval screen shows the client name and the redirect host, warns "only approve a code you see right now in your own browser", codes expire in 10 minutes, and each pairing is limited per account and IP.

### 1.3 The relay (Durable Object `AureaMcpRelay`)

One object per account (`idFromName("mcp:" + uid)`), SQLite-backed like the others.

- **App sockets.** `GET /api/mcp/app/socket` (WebSocket upgrade, `Authorization: Bearer <account session>`; the Worker validates the session with `lerSessao` and forwards to the object with the uid). The object accepts with the hibernation API (`ctx.acceptWebSocket(ws, [deviceId])`), so an idle connection costs no duration. `ping`/`pong` is answered by `setWebSocketAutoResponse` without waking the object. Up to 4 devices per account; a fifth closes the oldest.
- **Hello.** The app's first message: `{t:"hello", device:{id,name,platform,app}, engineTools:[…OpenAI format…], hostTools:[names], permissions:{…}}`. The object keeps it in the socket attachment and in storage (`catalog:<device>`), so `tools/list` keeps working when the app is offline and new engine tools show up without a Worker deploy.
- **Calls.** `tools/call` → object RPC `callTool({session, agent, tool, args, timeoutMs})` → `{t:"call", id, tool, args, agent}` to the active device → waits for `{t:"result", id, ok, content|error}` (timeout 45 s by default, 90 s when the app must ask the user to confirm). A cancelled or timed-out call sends `{t:"cancel", id}`.
- **Active device.** The most recent one to send `focus` (the app sends it when it comes to the foreground), or the one picked with `aurea_select_device`.
- **One heavy job at a time.** `export_video`, `import_media_url`, `render_frame` and `run_ae_script` hold a per-account lock; a second heavy call returns `busy` right away.
- **Rate limit.** 120 calls per minute and 3 concurrent calls per account (in memory in the object; the object is a single thread per account).
- **Pause.** The app's Stop button sends `{t:"pause"}`; every call then fails with "The user paused AureaMCP in the app" until `{t:"resume"}`.
- **Activity.** The object pushes `{t:"agent", name, event}` and `{t:"log", …}` lines so the app shows who is connected and every tool call.
- **Revocation kick.** Revoking a token calls `revokeSessions(tokenId)` on the object, which drops the MCP sessions bound to it.

### 1.4 App side

All three apps get the same screen and the same client. The PC app compiles the Android Kotlin, so it gets everything with no extra work.

- **Entry.** The Sparkles button in the editor top bar opens AureaMCP instead of the chat. A flag (`AureaMcpFlags.legacyChat`, false by default) brings the old chat back until the owner confirms removal; the old code stays compiled.
- **Screen** (`AureaMcpScreen.kt` / `AureaMcpView.swift`):
  - mascot, title "AureaMCP", one-line pitch;
  - status pill: Offline / Connecting / Online, with the device name;
  - "Connect agent": enter the pairing code shown by Claude.ai/ChatGPT, then the approval card;
  - "Token for Claude Code / Codex": create (shown once, copy), with the commands above;
  - connected agents (name, last call), each with Revoke, plus "Disconnect all";
  - activity log of tool calls (time, agent, tool, ok/failed);
  - Stop (pause all agents) / Resume;
  - permission toggles: Read project, Edit, Projects (open/create/rename/duplicate), Delete projects, Import media from links, Device gallery, Export;
  - confirmation dialog for destructive tools (delete project) and, if the user turns it on, for every edit.
- **Client** (`McpClient.kt` / `McpClient.swift`): WebSocket to `/api/mcp/app/socket` with the account session token, exponential reconnect (1 s → 60 s), `ping` every 25 s, hello on connect, `focus` on resume. On Android it is a small RFC 6455 client over `SSLSocket` (no new library; the PC build compiles it unchanged); on iOS, `URLSessionWebSocketTask`.
- **Executor** (`McpExecutor.kt` / `McpExecutor.swift`): permission check first, then:
  - engine tools → `engine.assistantCall(name, args)` (the same C++ rules on both platforms), then the UI refresh;
  - host tools → `EditorStore` (Android/PC) or `AureaModel` (iOS);
  - frames → `engine.captureFrame(maxDim)` → JPEG → MCP `image` content.
- No secret lives in the app: it only holds the account session it already had. Tokens for agents are created by the server and shown once.

---

## 2. Tool catalogue

`tools/list` = server tools (always) + the active device's tools (from its hello, filtered by its permissions). Every tool has a JSON Schema and MCP annotations (`readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`). The capability in the right column is the app permission toggle that gates it.

### 2.1 Session (Worker)

| Tool | What it does | Capability |
|---|---|---|
| `aurea_status` | Devices online, active device, open project, permissions, AureaOS brain version, paused flag | — |
| `aurea_select_device` | Pick which phone/PC gets the calls | — |

### 2.2 Engine tools (from `Engine::assistant_call`, relayed)

| Tool | Notes | Capability |
|---|---|---|
| `get_project_state` | composition, playhead, selection, view, every layer | read |
| `run_ae_script` | ExtendScript (ES3) in the engine sandbox; one undo step; layers, properties, keyframes, easing, effects, masks, text animators, expressions | edit |
| `list_ae_effects` | supported AE effects + param names | read |
| `select_layers` | | edit |
| `delete_layers` | one undo step (undoable, so no confirmation) | edit |
| `undo`, `redo` | | edit |
| `seek`, `playback`, `set_view` | | read |
| `open_panel` | opens a screen for the user | edit |

Phase C engine tools (14/11/2026, `engine/src/engine/EngineMcpTools.cpp`, dispatched from `Engine::assistant_call`; schemas in the `kTools` table of `EngineAssistant.cpp`, so both apps send them in the hello with no app change). Units: composition seconds, scale as a fraction (1 = 100 %), opacity 0..1, degrees, px, colours `[r,g,b(,a)]` 0..1; `layer` = id string or AE index. Property address: `position`, `position.x`, `scale` (number = uniform), `rotation` (number = Z), `anchor`, `opacity`, `skew.x`, `audio.volume`, `text.tracking`, `effect.<effectId>.<param index or id>[.x|.y|.z|.r|.g|.b|.a]`. Every write is one undo step (UndoBeginGroup/EndGroup around the commands); reads take the model mutex; writes only go through `apply_command` and the public engine API.

| Tool | What it does | Capability |
|---|---|---|
| `get_layer_details` | timing, parent, blend, transform values + keyframe counts, effect stack (ids), text/shape/camera/light/audio | read |
| `get_keyframes` | keys of a property (seconds, value, interpolation, bezier, power), or every animated channel | read |
| `set_property` | static value, or a keyframe (with `seconds`, or when the property is already animated) | edit |
| `add_keyframes` / `delete_keyframes` / `move_keyframe` | batch keys with per-key easing; delete by time or all; move | edit |
| `set_keyframe_easing` | interpolation / bezier [x1,y1,x2,y2] / power for the segment leaving each key | edit |
| `list_effect_types` / `get_effect` | catalogue (key, name, category, AE matchName) / every parameter of one effect | read |
| `add_effect` / `remove_effect` / `move_effect` / `set_effect_enabled` | effect stack; `add_effect` takes a key, a name or an AE matchName | edit |
| `list_presets` / `apply_preset` | text animation presets (native + Motion model), effect presets, Aurea preset JSON | read / edit |
| `list_fonts` / `set_text_style` | device fonts; text content, font, weight, italic, size, colours, stroke, alignment, tracking, line height | read / edit |
| `get_markers` / `set_marker` / `delete_marker` | composition and layer markers (label, comment, duration, colour) | read / edit |
| `get_work_area` / `set_work_area` | work area in seconds, or clear | read / edit |

Rendering a frame at a time is the host tool `render_frame` (§2.3). Still open from the original list: `add_text_animator` (covered by `run_ae_script` and `apply_preset`), `get_audio_levels`.

### 2.3 Host tools (app, relayed)

| Tool | What it does | Capability | Confirm |
|---|---|---|---|
| `list_projects` | id, title, size, fps, duration, modified | read | |
| `open_project` | open by id | projects | |
| `create_project` | title, width, height, fps, background | projects | |
| `rename_project` | current project | projects | |
| `duplicate_project` | by id | projects | |
| `delete_project` | by id | delete | always |
| `save_project` | current project | edit | |
| `close_project` | back to Home | projects | |
| `render_frame` | JPEG of the composition at a time (max 1280 px) as MCP image | read | |
| `export_video` | MP4 (H.264/HEVC), short side, fps, quality; starts a job | export | |
| `export_status` / `cancel_export` | progress, result, cancel | export | |
| `import_media_url` | downloads an https URL (≤ 500 MB, video/image/audio types) and adds it as a layer | media | |
| `list_device_media` | recent photos and videos in the gallery, newest first (id, name, kind, date, size, duration; `beforeId` pages); only what the person let Aurea see | device_media | |
| `import_device_media` | adds gallery items by id, in order | device_media | |

Device gallery (phase D, 14/11/2026): the "Device gallery" toggle starts OFF and turning it on asks the system: Android 13+ READ_MEDIA_IMAGES/VIDEO (14+: also READ_MEDIA_VISUAL_USER_SELECTED; ≤ 12: READ_EXTERNAL_STORAGE), iOS PHPhotoLibrary `.readWrite`. Denied = the toggle goes back off. Limited access (Android 14 "selected media", iOS "selected photos") is honoured: the MediaStore/PhotoKit query only returns those items, the reply says `access: "limited"`, and iOS offers "Change selection" (presentLimitedLibraryPicker). No audio: READ_MEDIA_AUDIO left the Android app (Play policy) and Photos has none. Ids: Android `image:<id>`/`video:<id>`, iOS the PHAsset localIdentifier. Import goes through the normal media import (iCloud originals downloaded on iOS).

Export on Android 8–9: `export_video` asks WRITE_EXTERNAL_STORAGE first (the same request as the Export screen, shown by `McpPermissionHost` at the AureaMCP root); denied or app in the background (60 s), the export runs and stays in the app folder.

### 2.4 AureaOS (Worker, from the bundled brain)

AureaOS is the owner's motion method (`C:\Users\Ruan\AureaOS`): the rules (`CLAUDE.md`), the two skills (`metodo-aureaos`, `diretor-aureaos`), the reference library (`referencias/CATALOGO.md`, 30 references with `CENAS.md`, `deep.md`, `text.md`, `trans.md`, `meta.json`, `tags.json`), the easing and transition indices, the approved-effects library (`engine/fx.js`), and the approved example (`projects/ref02`).

- **Prompts**: `aureaos_metodo` (the mandatory method), `aureaos_diretor` (director flow; argument `briefing`), `aureaos_recriar` (recreate a reference; arguments `slug`, `cena`), `aureamcp_editar` (how to drive Aurea well: read state, script in small steps, render a frame to check).
- **Resources** (`aureaos://…`): `rules`, `skills/metodo`, `skills/diretor`, `catalogo`, `easings/verticais`, `easings/horizontais`, `texto-transicoes`, `fx/fx.js`, `fx/aurea.js`, `exemplo/ref02/scenes.js`, and the template `aureaos://ref/{slug}/{part}`.
- **Tools** (many clients use tools better than resources, ChatGPT in particular):

| Tool | What it does |
|---|---|
| `aureaos_search` | full-text and tag search over the catalogue (scene → technique → use); filters `formato`, `tags` |
| `aureaos_reference` | one part of a reference (`cenas`, `deep`, `text`, `trans`, `meta`, `tags`), paged |
| `aureaos_doc` | any bundled document, paged (rules, skills, indices, fx library, example) |
| `aureaos_style_match` | the `stylematch.py` ranking (format, talking head × voice-over, client reference, wanted/avoided styles) on the bundled metadata |
| `aureaos_beats` | the `beats.py` split of a script or timed words into beats with visual cues |
| `aureaos_fx` | the approved effects in `fx.js` with their measured numbers |
| `search` / `fetch` | ChatGPT-style aliases over the same brain (ChatGPT deep research needs these two names) |

The Python tools that read video (`camtrack`, `speedgraph`, `transcribe`…) cannot run in a Worker. Their results for the 30 references are already in the bundle; measuring a new video stays a local AureaOS job (Claude Code with the AureaOS folder), and transcription in Aurea goes through the app's automatic captions.

**Bundle.** `discovery/scripts/bundle-aureaos.mjs` (same idea as TwoMCP's `bundle-brain.mjs`) copies the text files into `discovery/.aureaos-bundle/<version>/`, writes `manifest.json` (path, bytes, sha256) and `index.json` (per reference: slug, format, summary, tags, scenes), and with `--upload` puts them in R2 under `aureamcp/brain/<version>/…` and points KV `aureamcp:brain:current` at the version. Limits: 1 MB per file (bigger files are skipped and listed), 24 MB per bundle, 60 KB per tool result (paged with `offset`). The Worker keeps the manifest and index in isolate memory for 5 minutes. Old versions stay in R2 so a bad sync can be rolled back by moving the KV pointer.

---

## 3. Security

- **Only the owner's own apps.** A call reaches only the relay object of the account the token belongs to (`idFromName("mcp:" + uid)`); the app socket is accepted only with a valid session of the same account. There is no cross-account addressing.
- **Credentials.** Tokens are 256-bit random, stored only as SHA-256. Bearer header only, never in a URL. Personal tokens 365 days; OAuth access 24 h, refresh 90 days with rotation and reuse detection. All compared by hash lookup, never logged.
- **Nothing destructive without the app.** Destructive tools are marked `destructiveHint` and the app always asks the person (90 s, then "denied"). Engine edits are undoable steps. The user can turn on "ask before every edit".
- **Permissions live in the app** (it executes) and are mirrored in the hello so the Worker hides disabled tools.
- **Rate limits.** OAuth register 20/h per IP; authorize 30/h per IP; pairing 10 per 10 min per account and 30/h per IP; token endpoint 60/15 min per IP; MCP calls 120/min and 3 concurrent per account; one heavy job at a time. Request bodies ≤ 512 KB; tool results ≤ 900 KB (WebSocket message cap is 1 MiB).
- **Media import.** Only `https://`, no private/loopback hosts, size and type caps, downloaded by the app into its cache. The Worker never fetches user URLs. DNS rebinding is closed by checking the address of the actual connection, not only the first lookup: Android wraps the TLS socket factory (`AddressCheckSocketFactory`) and refuses a private address before the TLS handshake and before any request byte; iOS reads the connected address from `URLSessionTaskMetrics` and discards the file if it is private.
- **Cancel.** `notifications/cancelled` from the agent goes to the relay (`cancelRequest`, matched by JSON-RPC request id, token and MCP session), which sends `{t:"cancel", id}` to the app and ends the call with "Cancelled."; the app cancels the tool coroutine/Task (downloads stop at the next block).
- **AureaOS is paid** (owner decision, 14/11/2026). The editor tools are free; every AureaOS tool (`aureaos_*`, `search`, `fetch`), prompt (`aureaos_*`) and `aureaos://` resource needs the account entitlement `aurea_aureaos`, checked in the Worker on every call (`entitlements.js temProduto`). Without it: `tools/list`, `prompts/list` and `resources/list` still list them with the description prefixed `[Paid: AureaOS]`; `tools/call` returns an `isError` result and `prompts/get` / `resources/read` return JSON-RPC error `-32003`, both with "AureaOS is a paid add-on... activate it in Aurea → AureaMCP"; no content leaves. D1 unavailable = not granted. `aurea_status` says whether it is active. The `aureamcp_editar` prompt and the server instructions (editor guide + the generic bezier → KeyframeEase bridge) stay free.
- **AureaOS purchase.** One-time, non-consumable product `aurea_aureaos` (Play "in-app product", App Store "Non-Consumable"), like `aureahyper_lifetime`. The app buys with Play Billing / StoreKit 2 and sends the receipt to `POST /api/entitlements/purchase {platform, product, token}` (session): the Worker verifies it with the store (`store_verify.js`, same secrets as the Mural), acknowledges it on the Play, and writes the `entitlements` row (`source` = google/apple, `note` = sha256 of the store order id / originalTransactionId). The same purchase on another Aurea account = 409; restoring on the same account = 200. `GET /api/entitlements` gains `purchasable` (empty while the store secrets are missing: the app shows "unavailable", never charges). The admin route can still grant it by e-mail.
- **No secrets in the app or the Worker code.** Nothing new to set: the relay uses existing bindings. (Optional future secret: none.)
- **Revocation.** App: revoke one agent or "disconnect all" → `mcp_tokens.revoked_at` set and the relay drops their sessions. Logging out of the app closes its socket (agents get "app offline"). Account deletion deletes every `mcp_*` row with the account. Kill switch: `AUREAMCP_ENABLED = "false"` turns every MCP route off (503).
- **Logs.** The Worker logs no arguments or results. The activity log lives in the app and in the relay's memory (last 100 lines, no arguments).
- **Prompt injection.** Content from projects and AureaOS is data. The server instructions tell the agent that, and destructive actions need the person's tap in the app anyway.

---

## 4. Data (D1 migration `0010_aureamcp.sql`)

- `mcp_clients` — OAuth clients from dynamic registration (id, name, redirect URIs, created).
- `mcp_auth_requests` — pending authorizations: client, redirect URI, PKCE challenge, state, scope, resource, `user_code_hash`, `browser_hash` (cookie), status, uid once approved, `code_hash` once issued, expiry.
- `mcp_tokens` — personal and OAuth grants: uid, kind, name, client, `token_hash`, `refresh_hash`, scopes, created/last used/expiry/revoked.

Applied only by the owner (`wrangler d1 migrations apply aurea-contas --remote`, after the D1 export).

---

## 5. Phases

| Phase | What | Size |
|---|---|---|
| A | This design | done |
| B1 | Worker: `/mcp` JSON-RPC, personal tokens, OAuth + DCR + pairing, relay object, AureaOS loader and tools, bundle script, migration, tests | L (done in this round) |
| B2 | Android/PC: AureaMCP screen replacing the chat entry (old chat behind a flag), pairing, tokens, WebSocket client, executor over engine + EditorStore, mascot, strings in 7 locales | L (done in this round) |
| B3 | iOS mirror: same screen, URLSession WebSocket client, executor over `AureaModel`, asset catalog, strings | M (done in this round, statically reviewed) |
| C | New engine tools (EngineMcpTools.cpp: 22 tools) + host tests (test_mcp_tools.cpp) | M (done 14/11) |
| D | Device gallery tools with the permission flow on both platforms; `aurea://mcp/pair` deep link (Android intent filter, iOS URL type + onOpenURL); agent cancel forwarded; DNS-rebinding window closed; Android 8–9 gallery permission in export_video; AureaOS paid gate + purchase | M (done 14/11, compiled/static only; Worker deployed as version 8d513d7a, no new migration) |
| E | Deploy: D1 export, migration, bundle upload, deploy, connector tests with Claude.ai, Claude Code, ChatGPT and Codex; then remove the old chat | S (owner) |
| F | Optional: SSE progress notifications for exports, MCP elicitation for confirmations on clients that support it, per-agent permission profiles | M |

Validation is recorded separately for implementation, compilation and device runs (AGENTS.md): a Worker test or a Kotlin compile does not prove the native screens work on a phone or an iPhone.

## 6. Purchase activation, 14 November 2026

The owner set the one-time Brazilian price to BRL 59.90. Register `aurea_aureaos` as a non-consumable in the Google Play app `com.aurea.editor` and the Apple app `com.aurea.aurea`. The native purchase screens display the price returned by the store.

Worker version `476ce907-bfa7-4362-8fb0-552d84e68d29` is deployed with migration `0011_entitlement_purchases.sql`. Receipt ownership is claimed atomically and retained across additional purchases or admin grants. Each native client requests availability for its own platform.

The owner confirmed that store credentials do not exist yet and requested that purchases remain blocked during Google Play closed testing. `AUREAOS_PURCHASES_ENABLED = "false"` explicitly blocks checkout on both platforms, even if credentials are added later. The agreed future price remains BRL 59.90. Store setup and purchase/restore validation are deferred by that decision. The D1 backup is `build/backups/aurea-contas-before-054-completion.sql`.
