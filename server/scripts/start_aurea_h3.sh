#!/usr/bin/env bash
# Sobe o ComfyUI com o MiniMax H3 e a Aurea AI API na frente, num Colab.
#
# Idempotente de proposito: rodar duas vezes nao reinstala nada. O que ja existe
# e reaproveitado, inclusive os pesos (12 GB de download nao se repetem).
#
# Variaveis lidas:
#   AUREA_DRIVE_MODELS   pasta no Drive com os .safetensors (opcional)
#   AUREA_SERVER_TOKENS  um ou mais tokens de cliente, separados por virgula
#   AUREA_ADMIN_TOKEN    token administrativo (nunca sai daqui)
#   AUREA_DISCOVERY_REPO owner/repo onde publicar o discovery
#   AUREA_DISCOVERY_TOKEN token do GitHub com permissao de escrita no repo
#   AUREA_TUNNEL         cloudflare (padrao) | manual | nenhum
#   AUREA_PUBLIC_URL     endereco fixo, quando AUREA_TUNNEL=manual
#
# Uso:  bash scripts/start_aurea_h3.sh
set -euo pipefail

RAIZ_AUREA="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMFY_DIR="${COMFY_DIR:-/content/ComfyUI}"
MODELOS_DIR="${AUREA_MODELS_DIR:-/content/models}"
LOG_DIR="${AUREA_LOG_DIR:-/content/aurea-logs}"
PORTA_COMFY="${AUREA_COMFY_PORT:-8188}"
PORTA_API="${AUREA_PORT:-8000}"

mkdir -p "$MODELOS_DIR" "$LOG_DIR"

dizer() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
erro()  { printf '\n\033[1;31m!!  %s\033[0m\n' "$*" >&2; }

# ---------------------------------------------------------------------------
dizer "1/7  Conferindo a GPU"
if ! command -v nvidia-smi >/dev/null 2>&1; then
  erro "nvidia-smi nao existe. Este notebook precisa de GPU (A100)."
  exit 1
fi
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
VRAM_MB=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits | head -1 | tr -d ' ')
if [ "${VRAM_MB:-0}" -lt 40000 ]; then
  erro "VRAM de ${VRAM_MB} MB. O H3 em int8 precisa de ~40 GB; use A100."
  exit 1
fi

# ---------------------------------------------------------------------------
dizer "2/7  ComfyUI"
if [ ! -d "$COMFY_DIR" ]; then
  git clone --depth 1 https://github.com/comfyanonymous/ComfyUI.git "$COMFY_DIR"
fi
cd "$COMFY_DIR"
python -m pip install -q --upgrade pip
python -m pip install -q -r requirements.txt
# Os requirements do ComfyUI sobem um torch sem suporte a sm_80 as vezes.
python - <<'PY'
import torch
if not torch.cuda.is_available():
    raise SystemExit("torch sem CUDA; refaca a etapa de instalacao")
print("torch", torch.__version__, "| cuda", torch.version.cuda,
      "| gpu", torch.cuda.get_device_name(0))
PY

# ---------------------------------------------------------------------------
dizer "3/7  Nodes do MiniMax H3"
NODES="$COMFY_DIR/custom_nodes"
mkdir -p "$NODES"
# O pack do H3 traz os nos MiniMaxH3Sampler / MiniMaxH3AudioDecode.
# Se a origem do pack mudar, ajuste AUREA_H3_NODES_REPO e o nome da pasta.
H3_REPO="${AUREA_H3_NODES_REPO:-https://github.com/MiniMax-AI/ComfyUI-MiniMax-H3.git}"
H3_DIR="$NODES/ComfyUI-MiniMax-H3"
if [ ! -d "$H3_DIR" ]; then
  if git clone --depth 1 "$H3_REPO" "$H3_DIR" 2>/dev/null; then
    :
  else
    erro "nao consegui clonar $H3_REPO"
    erro "instale o pack do H3 a mao em $NODES e rode de novo."
    erro "sem ele, 'python -m scripts.verificar_workflow' vai acusar os nos que faltam."
  fi
fi
if [ -f "$H3_DIR/requirements.txt" ]; then
  python -m pip install -q -r "$H3_DIR/requirements.txt" || \
    erro "alguns requirements do pack falharam; confira com verificar_workflow"
fi

# ---------------------------------------------------------------------------
dizer "4/7  cloudflared"
if ! command -v cloudflared >/dev/null 2>&1; then
  ARCH="$(uname -m)"
  case "$ARCH" in
    x86_64)  DEB="cloudflared-linux-amd64.deb" ;;
    aarch64) DEB="cloudflared-linux-arm64.deb" ;;
    *) erro "arquitetura $ARCH nao prevista; instale o cloudflared a mao"; DEB="" ;;
  esac
  if [ -n "$DEB" ]; then
    curl -fsSL -o /tmp/cloudflared.deb \
      "https://github.com/cloudflare/cloudflared/releases/latest/download/$DEB"
    dpkg -i /tmp/cloudflared.deb >/dev/null 2>&1 || {
      erro "dpkg falhou; tentando o binario solto"
      curl -fsSL -o /usr/local/bin/cloudflared \
        "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-$([ "$ARCH" = x86_64 ] && echo amd64 || echo arm64)"
      chmod +x /usr/local/bin/cloudflared
    }
  fi
fi
cloudflared --version || erro "cloudflared indisponivel: use AUREA_TUNNEL=manual"

# ---------------------------------------------------------------------------
dizer "5/7  Pesos"
export AUREA_MODELS_DIR="$MODELOS_DIR"
if [ -n "${AUREA_DRIVE_MODELS:-}" ]; then
  python -m scripts.preparar_modelos --modelos "$MODELOS_DIR" --drive "$AUREA_DRIVE_MODELS" || true
else
  python -m scripts.preparar_modelos --modelos "$MODELOS_DIR" || true
fi

# ---------------------------------------------------------------------------
dizer "6/7  ComfyUI no ar (porta $PORTA_COMFY)"
if curl -fsS "http://127.0.0.1:$PORTA_COMFY/system_stats" >/dev/null 2>&1; then
  echo "ja estava rodando; reaproveitando"
else
  cd "$COMFY_DIR"
  nohup python main.py --listen 127.0.0.1 --port "$PORTA_COMFY" \
      --disable-auto-launch --output-directory /content/comfy-output \
      > "$LOG_DIR/comfyui.log" 2>&1 &
  echo $! > "$LOG_DIR/comfyui.pid"
  for i in $(seq 1 60); do
    if curl -fsS "http://127.0.0.1:$PORTA_COMFY/system_stats" >/dev/null 2>&1; then
      echo "ComfyUI pronto em ${i}s"
      break
    fi
    sleep 1
    [ "$i" = 60 ] && { erro "ComfyUI nao subiu; veja $LOG_DIR/comfyui.log"; tail -30 "$LOG_DIR/comfyui.log"; }
  done
fi

# ---------------------------------------------------------------------------
dizer "7/7  Aurea AI API"
cd "$RAIZ_AUREA"
python -m pip install -q -r requirements.txt

export AUREA_WORKFLOWS_DIR="$RAIZ_AUREA/workflows"
export AUREA_COMFY_URL="http://127.0.0.1:$PORTA_COMFY"
export AUREA_UPLOAD_DIR="${AUREA_UPLOAD_DIR:-/content/aurea-assets}"
export AUREA_MODE="${AUREA_MODE:-producao}"

echo "--- conferindo os workflows contra o ComfyUI instalado ---"
python -m scripts.verificar_workflow "$AUREA_COMFY_URL" || \
  erro "os workflows nao batem com este ComfyUI. O servidor sobe, mas vai recusar gerar."

nohup python -m aurea_ai --porta "$PORTA_API" > "$LOG_DIR/aurea-ai.log" 2>&1 &
echo $! > "$LOG_DIR/aurea-ai.pid"

for i in $(seq 1 40); do
  if curl -fsS "http://127.0.0.1:$PORTA_API/api/v1/health" >/dev/null 2>&1; then break; fi
  sleep 1
done

# O endereco publico so aparece depois do tunel; o servidor publica sozinho no
# discovery. Aqui mostramos o que ele publicou, para conferencia.
echo
echo "--- saude (local) ---"
curl -fsS "http://127.0.0.1:$PORTA_API/api/v1/health" | python -m json.tool || true

echo
echo "--- discovery publicado ---"
if [ -n "${AUREA_DISCOVERY_REPO:-}" ]; then
  sleep 3
  curl -fsS "https://raw.githubusercontent.com/${AUREA_DISCOVERY_REPO}/${AUREA_DISCOVERY_BRANCH:-main}/discovery/aurea-h3.json" \
    | python -m json.tool || erro "ainda nao publicado; veja o log abaixo"
else
  erro "AUREA_DISCOVERY_REPO nao definido: o app NAO vai encontrar este servidor sozinho."
fi

echo
echo "Logs: $LOG_DIR/aurea-ai.log   $LOG_DIR/comfyui.log"
echo "Para desligar: kill \$(cat $LOG_DIR/aurea-ai.pid) \$(cat $LOG_DIR/comfyui.pid)"
