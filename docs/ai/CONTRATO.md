# Aurea AI — contrato v1

Fonte da verdade deste documento: **o app não conhece endereço de servidor.**
Ele conhece o *lugar* de um documento de discovery. O endereço do servidor mora
nesse documento e muda sozinho quando o Colab reinicia.

Três componentes separados, de propósito (§22 do pedido):

```
Aurea (Android / iOS)
   │  1. lê o documento de discovery           (sem segredo)
   │  2. fala com o servidor por HTTPS         (token de cliente)
   ▼
Aurea AI API (FastAPI, no Colab)
   │  3. enfileira e executa                   (token de admin, só no servidor)
   ▼
ComfyUI API  →  MiniMax H3  →  A100
```

O app **nunca** fala com o ComfyUI. O ComfyUI é backend de execução, sem UI
exposta.

---

## 1. Discovery

**Onde.** Um arquivo de texto no repositório, lido por `raw.githubusercontent.com`.
Escolhido porque é o único serviço de discovery já existente, com escrita
autenticada e leitura pública, sem custo e sem inventar infraestrutura:

```
<AUREA_DISCOVERY_REPO>/<AUREA_DISCOVERY_BRANCH>/discovery/aurea-h3.json
```

O repositório e o branch entram no app por `BuildConfig` (constante de build),
nunca digitados pelo usuário. Isso é uma *localização de documento*, não um
endereço de servidor: não contém IP, porta, túnel nem host de inferência.

**Quem escreve.** Só o Colab, com um token do GitHub (`AUREA_DISCOVERY_TOKEN`,
variável de ambiente no Colab). O token **não** entra no APK nem no IPA.

**Documento:**

```json
{
  "service": "aurea-h3",
  "version": 1,
  "endpoint": "https://<tunel-atual>",
  "online": true,
  "gpu": "NVIDIA A100-SXM4-80GB",
  "model": "MiniMax-H3",
  "capabilities": ["text_to_video", "image_to_video", "audio"],
  "updatedAt": 1750000000,
  "appToken": "<token de cliente>"
}
```

**Heartbeat.** O Colab reescreve o documento a cada 20 s. O app considera
**offline** quando `updatedAt` tem mais de 90 s (3 batidas perdidas). Um Colab
novo sobrescreve o mesmo arquivo, então o endereço antigo é substituído sem o
app saber que houve troca.

**Validação.** O app só aceita o documento se `service == "aurea-h3"`,
`version == 1`, `endpoint` começa com `https://` e `updatedAt` está dentro da
janela. Documento inválido é o mesmo que ausente.

---

## 2. Autenticação

Três tokens, propósitos diferentes:

| token | onde vive | para quê |
|---|---|---|
| `AUREA_DISCOVERY_TOKEN` | Colab (env) | escrever o documento de discovery |
| `AUREA_SERVER_TOKEN(S)` | Colab (env) → publicado no discovery | usar a API de geração |
| `AUREA_ADMIN_TOKEN` | Colab (env), **só** | cancelar job de outro, mexer na fila |

**O usuário não digita token nenhum.** O token de cliente sai no documento de
discovery, no campo `appToken`, e o app autentica com o que leu de lá. É isso
que faz o app abrir já conectado — sem campo, sem botão de conectar, sem nada
guardado no aparelho.

O `AUREA_ADMIN_TOKEN` **nunca** entra no documento: o documento é público, e o
administrativo cancela job dos outros. Publicar um e não o outro é o que separa
"usar" de "administrar".

Consequência assumida: o token de cliente é público, porque o documento é
público. Ele só dá acesso à geração, e trocá-lo é trocar `AUREA_SERVER_TOKENS`
no Colab — o app pega o novo na batida seguinte, sem atualização do aplicativo.
Se um dia for preciso esconder também o token de cliente, o caminho é publicar
um token de vida curta que o próprio servidor rotaciona.

Com `AUREA_SERVER_TOKENS` (lista, um por tester) cada aparelho tem identidade
própria: o histórico de um não aparece no outro, e perder um aparelho é revogar
um token só. Com `AUREA_SERVER_TOKEN` (um só) todos compartilham a identidade.

Todas as chamadas do app levam `Authorization: Bearer <appToken do discovery>`.

---

## 3. API

Prefixo `/api/v1`. Erro sempre no mesmo formato:

```json
{ "error": "codigo_curto", "detail": "frase para humano" }
```

### `GET /api/v1/health`
Público (sem token) — é o que o app usa para validar o servidor antes de tudo.

```json
{
  "status": "ok",
  "service": "aurea-ai",
  "engine": "minimax-h3",
  "version": 1,
  "gpu": "NVIDIA A100-SXM4-80GB",
  "vram_total_mb": 81153,
  "ready": true,
  "queue": 0,
  "capabilities": { "t2v": true, "i2v": true, "audio": true }
}
```

O app **exige** `service == "aurea-ai"`. Qualquer HTTP 200 não vale como servidor
válido.

### `GET /api/v1/capabilities`
Autenticado. É o que **monta a UI** (§23): a tela não oferece o que o backend não
souber fazer.

```json
{
  "engine": "minimax-h3",
  "modes": ["text_to_video", "image_to_video"],
  "durations": [5, 10, 15],
  "aspectRatios": ["9:16", "16:9", "1:1", "4:5"],
  "resolutions": ["preview", "standard", "high"],
  "fps": [24],
  "audio": true,
  "maxConcurrentJobs": 1,
  "queueLength": 0
}
```

### `POST /api/v1/generations`
Autenticado. `202` com `{ "jobId": "<uuid>", "status": "queued", "queuePosition": 0 }`.

```json
{
  "mode": "text_to_video",
  "prompt": "...",
  "negativePrompt": "",
  "duration": 5,
  "aspectRatio": "16:9",
  "resolution": "standard",
  "fps": 24,
  "audio": true,
  "seed": -1,
  "turbo": true,
  "imageAssetId": "<só em image_to_video>"
}
```

Validação: `mode` em `modes`; `duration` em `durations` (§23 — nada de valor
arbitrário); `aspectRatio` em `aspectRatios`; `prompt` de 1 a 2000 caracteres;
`imageAssetId` obrigatório em `image_to_video` e proibido em `text_to_video`.

### `GET /api/v1/generations/{jobId}`
Autenticado. Só o dono do job.

```json
{
  "jobId": "…", "status": "generating", "progress": 0.63,
  "stage": "Sampling", "queuePosition": 0,
  "elapsedSeconds": 84.0, "error": null, "result": null
}
```

`status` ∈ `queued | loading_model | encoding_prompt | generating | decoding |
encoding_video | completed | failed | cancelled`.

`result` só em `completed`:

```json
{
  "videoUrl": "/api/v1/generations/<id>/video",
  "thumbnailUrl": "/api/v1/generations/<id>/thumbnail",
  "duration": 5.0, "width": 1344, "height": 768, "fps": 24, "hasAudio": true
}
```

As URLs são **relativas ao servidor** e, no máximo, assinadas com validade. O app
resolve contra o endpoint do discovery. Nenhum caminho do Colab aparece.

### `GET /api/v1/generations/{jobId}/video` e `/thumbnail`
Autenticado, dono do job, `Content-Type` real, `Range` respeitado para o player.

### `GET /api/v1/generations/{jobId}/events`
WebSocket. Autenticado. Mensagens:

```json
{ "type": "progress", "progress": 0.72, "step": 6, "totalSteps": 8, "stage": "Sampling" }
{ "type": "completed", "result": { … } }
{ "type": "failed", "error": "codigo_curto" }
```

Se o WebSocket cair, o app volta para polling em `GET /generations/{jobId}` a
cada 2 s. O contrato é o mesmo; só muda o transporte.

### `DELETE /api/v1/generations/{jobId}`
Autenticado, dono do job. Cancela de verdade: `queued` sai da fila, `generating`
interrompe o workflow no ComfyUI. Responde `{ "status": "cancelled" }`.

### `POST /api/v1/assets` (multipart)
Autenticado. Campo `file`. Limite 12 MB, só `image/png`, `image/jpeg`,
`image/webp`. O nome do arquivo é descartado (sanitizado e substituído por UUID).
Responde `{ "assetId": "<uuid>" }`.

### `GET /api/v1/generations`
Autenticado. Histórico do dono do token, mais recentes primeiro, limite 50.

---

## 4. Fila (§18)

`MAX_GPU_JOBS = 1`. Um job roda por vez na A100; o resto fica `queued` com
`queuePosition`. O app mostra a posição. Concorrência é variável de ambiente
(`AUREA_MAX_GPU_JOBS`), então mudar não exige mexer no código.

---

## 5. Estados do app

```
enum AureaAiState { checking, connected, generating, reconnecting, disconnected, error }
```

`● Aurea AI / Conectado` · `○ Aurea AI / Offline` · `◌ Conectando…`

A tela **não** mostra URL, IP, porta, túnel, ComfyUI nem Colab.

Reconexão ao abrir: ler discovery → `GET /health` → validar `service` → ligar.
Falhou: repetir discovery com recuo exponencial 1, 2, 4, 8, 15, 30 s, para
sempre, sem travar a UI.

---

## 6. Contrato com o ComfyUI

O servidor carrega `workflows/h3_t2v.json` e `workflows/h3_i2v.json`, **edita os
inputs em memória** e envia para `POST /prompt` do ComfyUI. Nunca depende de
workflow aberto no navegador, nunca aceita workflow do cliente.

Progresso: `WS /ws?clientId=…` do ComfyUI (`progress`/`executing`/`executed`)
mapeado para os `stage` do contrato. Fila: `GET /queue` do ComfyUI.

Modelos (§12 e §20), em `/content/models` depois de virem do Drive:

```
minimax_h3_fl2va_pruned_int8_convrot.safetensors
qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors
minimax_h3_video_vae_int8_convrot.safetensors
minimax_h3_audio_vae_fp32.safetensors
+ LoRA Turbo
```

Ausente = baixar **só aquele arquivo**. Presente = validar tamanho/hash e não
baixar de novo.

---

## 7. O que o cliente nunca manda (§17)

Caminho de arquivo, nome de workflow, id de node, comando, URL de callback.
Só o que está na tabela do §3. O servidor nunca devolve caminho interno, nunca
aceita `../`, nunca executa workflow do cliente, nunca deixa upload sem limite.
