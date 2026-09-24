"""START definitivo da Aurea AI no Colab (A100) — uma celula, do zero ao ONLINE.

Rode esta celula inteira. No fim ela imprime `AUREA AI ONLINE` com o endereco
de agora ja publicado no discovery — e o app conecta sozinho, sem APK novo.

O que ela faz, em ordem, parando no primeiro que falhar:

    Drive -> confere backup -> confere A100 -> restaura o ComfyUI
    -> copia os 6 modelos H3 do Drive para o SSD LOCAL
    -> confere tamanho de cada um
    -> sobe o ComfyUI com os modelos locais
    -> /system_stats local 200
    -> cria o Cloudflare Tunnel
    -> espera o DNS
    -> /system_stats externo 200 + JSON valido
    -> le AUREA_DISCOVERY_SECRET (Secret do Colab, nunca impresso)
    -> publica a URL no Worker
    -> GET /server e confere que devolveu EXATAMENTE esta URL
    -> imprime AUREA AI ONLINE

Por que copiar para o SSD local e obrigatorio, e nao um detalhe:

    Com os pesos lidos por symlink do Drive, o CLIPLoader e o VAELoader
    travavam. Copiados para o disco da instancia, o H3 gerou. Nao e otimizacao:
    e a diferenca entre funcionar e nao funcionar.

O que esta celula NAO faz: mexer no workflow, nos modelos do Drive, nos
anuncios ou no editor. Os originais do Drive so sao lidos.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

# =============================================================================
# Configuracao
# =============================================================================

DRIVE_RAIZ = "/content/drive/MyDrive"
MODELOS_LOCAIS = Path("/content/models")          # SSD da instancia, nao o Drive
COMFY = Path("/content/ComfyUI")
PORTAL = 8188
WORKER = "https://aurea-ai-discovery.aureaapp.workers.dev"
SECRET_NO_COLAB = "AUREA_DISCOVERY_SECRET"

# nome do arquivo -> pasta em models/
MODELOS = {
    "minimax_h3_fl2v_turbo_4step_v1.0_768p_comfyui_bf16.safetensors": "loras",
    "minimax_h3_fl2v_turbo_8step_v1.0_comfyui_bf16.safetensors": "loras",
    "minimax_h3_fl2va_pruned_int8_convrot.safetensors": "diffusion_models",
    "qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors": "text_encoders",
    "minimax_h3_audio_vae_fp32.safetensors": "vae",
    "minimax_h3_video_vae_int8_convrot.safetensors": "vae",
}


def log(msg: str) -> None:
    print(f"[aurea] {msg}", flush=True)


def morrer(msg: str) -> None:
    """Falha alto. Um START que segue depois de um erro mente para o usuario."""
    print(f"\n❌ {msg}", flush=True)
    sys.exit(1)


# =============================================================================
# 1. Ambiente
# =============================================================================

def conferir_gpu() -> None:
    try:
        saida = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=60,
        ).stdout.strip()
    except Exception as e:  # noqa: BLE001
        morrer(f"nvidia-smi nao respondeu ({e}). O runtime esta com GPU?")
    if "A100" not in saida:
        log(f"AVISO: a GPU e '{saida}', nao uma A100. Vai rodar, mas mais devagar.")
    else:
        log(f"GPU: {saida}")


def montar_drive() -> None:
    from google.colab import drive  # type: ignore

    if not Path(DRIVE_RAIZ).is_dir():
        drive.mount("/content/drive")
    if not Path(DRIVE_RAIZ).is_dir():
        morrer("o Drive nao montou")
    log("Drive montado")


# =============================================================================
# 2. ComfyUI
# =============================================================================

def restaurar_comfyui() -> None:
    if COMFY.is_dir():
        log(f"ComfyUI ja esta em {COMFY}")
    else:
        log("clonando o ComfyUI…")
        subprocess.run(
            ["git", "clone", "--depth", "1",
             "https://github.com/comfyanonymous/ComfyUI.git", str(COMFY)],
            check=True,
        )
    req = COMFY / "requirements.txt"
    if req.is_file():
        subprocess.run([sys.executable, "-m", "pip", "install", "-q", "-r", str(req)], check=True)
    log("ComfyUI pronto")


# =============================================================================
# 3. Drive -> SSD local (o passo que resolveu o travamento)
# =============================================================================

def achar_no_drive(nome: str) -> Path | None:
    """Procura o arquivo no Drive pelo nome. Nao adivinha caminho."""
    if not Path(DRIVE_RAIZ).is_dir():
        return None
    for raiz, dirs, arquivos in os.walk(DRIVE_RAIZ):
        # Pastas enormes que nunca tem modelo.
        dirs[:] = [d for d in dirs if not d.startswith(".") and d not in
                   ("Colab Notebooks", "ComfyUI", "outputs", "temp")]
        if nome in arquivos:
            return Path(raiz) / nome
    return None


def copiar_modelos() -> None:
    MODELOS_LOCAIS.mkdir(parents=True, exist_ok=True)
    faltando: list[str] = []

    for nome, pasta in MODELOS.items():
        destino = MODELOS_LOCAIS / pasta / nome
        destino.parent.mkdir(parents=True, exist_ok=True)

        origem = achar_no_drive(nome)
        if origem is None:
            faltando.append(nome)
            continue

        tamanho_origem = origem.stat().st_size
        if destino.is_file() and destino.stat().st_size == tamanho_origem:
            log(f"ja no SSD ({tamanho_origem / 1e9:.1f} GB): {pasta}/{nome}")
            continue

        # Cópia de verdade, não symlink. É o ponto inteiro deste passo.
        log(f"copiando {tamanho_origem / 1e9:.1f} GB do Drive para o SSD: {pasta}/{nome}")
        tmp = destino.with_suffix(destino.suffix + ".parcial")
        shutil.copyfile(origem, tmp)          # origem fica intacta no Drive
        tmp.replace(destino)
        copiado = destino.stat().st_size
        if copiado != tamanho_origem:
            morrer(f"{nome}: copiou {copiado} bytes, esperava {tamanho_origem}")
        log(f"  ok: {pasta}/{nome}")

    if faltando:
        morrer("nao achei no Drive: " + ", ".join(faltando))

    # Confere o conjunto inteiro antes de subir o Comfy.
    for nome, pasta in MODELOS.items():
        destino = MODELOS_LOCAIS / pasta / nome
        if not destino.is_file() or destino.stat().st_size == 0:
            morrer(f"modelo ausente ou vazio depois da copia: {pasta}/{nome}")
    log(f"6 modelos no SSD local em {MODELOS_LOCAIS} (nenhum symlink)")


# =============================================================================
# 4. ComfyUI no ar
# =============================================================================

def subir_comfyui() -> subprocess.Popen:
    # Os modelos LOCAIS: nada aponta para o Drive.
    extra = ["--listen", "127.0.0.1", "--port", str(PORTAL)]
    proc = subprocess.Popen(
        [sys.executable, "main.py", *extra],
        cwd=str(COMFY),
        stdout=open("/content/comfyui.log", "ab"),
        stderr=subprocess.STDOUT,
        env={**os.environ, "AUREA_MODELS_DIR": str(MODELOS_LOCAIS)},
    )
    log(f"ComfyUI subindo (pid {proc.pid}), log em /content/comfyui.log")
    return proc


def esperar_system_stats(url: str, tentativas: int, rotulo: str) -> bool:
    """HTTP 200 E JSON com 'system'. 200 de portal de wifi nao serve."""
    for i in range(tentativas):
        try:
            pedido = urllib.request.Request(
                url.rstrip("/") + "/system_stats",
                headers={"User-Agent": "Aurea-Discovery/1.0 (+https://aurea.app)"})
            with urllib.request.urlopen(pedido, timeout=20) as r:
                if r.status == 200:
                    corpo = json.loads(r.read().decode("utf-8", "replace"))
                    if isinstance(corpo, dict) and "system" in corpo:
                        log(f"/system_stats {rotulo} ok ({i + 1}a tentativa)")
                        return True
                    log(f"{rotulo}: respondeu 200 mas nao e o ComfyUI")
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            if i % 5 == 0:
                log(f"{rotulo}: ainda nao ({e})")
        except json.JSONDecodeError:
            log(f"{rotulo}: respondeu 200 mas o corpo nao e JSON")
        time.sleep(4)
    return False


# =============================================================================
# 5. Cloudflare Tunnel
# =============================================================================

def garantir_cloudflared() -> str:
    exe = Path("/usr/local/bin/cloudflared")
    if not exe.is_file():
        log("baixando o cloudflared…")
        subprocess.run(
            ["wget", "-q", "-O", str(exe),
             "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64"],
            check=True,
        )
        exe.chmod(0o755)
    return str(exe)


def abrir_tunel() -> tuple[subprocess.Popen, str]:
    exe = garantir_cloudflared()
    log("abrindo o Cloudflare Tunnel…")
    proc = subprocess.Popen(
        [exe, "tunnel", "--url", f"http://127.0.0.1:{PORTAL}", "--no-autoupdate"],
        stdout=open("/content/cloudflared.log", "wb"),
        stderr=subprocess.STDOUT,
    )
    # O cloudflared imprime a URL no log; ela nasce nesta sessao, nunca e chumbada.
    for _ in range(60):
        time.sleep(2)
        try:
            texto = Path("/content/cloudflared.log").read_text(errors="replace")
        except OSError:
            continue
        for pedaco in texto.split():
            if pedaco.startswith("https://") and "trycloudflare.com" in pedaco:
                url = pedaco.strip().rstrip(".,)")
                if url.count("://") == 1 and len(url) > 30:
                    log(f"tunel: {url}")
                    return proc, url
    proc.terminate()
    morrer("o cloudflared nao publicou uma URL a tempo (veja /content/cloudflared.log)")


# =============================================================================
# 6. Discovery
# =============================================================================

def ler_secret() -> str:
    """Le o Secret do Colab. Nunca imprime, nunca grava em arquivo."""
    valor = os.environ.get(SECRET_NO_COLAB, "")
    if not valor:
        try:
            from google.colab import userdata  # type: ignore

            valor = userdata.get(SECRET_NO_COLAB) or ""
        except Exception:  # noqa: BLE001
            valor = ""
    if not valor:
        morrer(
            f"falta o Secret {SECRET_NO_COLAB} no Colab "
            "(barra lateral > chave > Adicionar novo secret, com acesso ao notebook)"
        )
    log(f"secret {SECRET_NO_COLAB} lido (nao vou imprimir)")
    return valor


def conferir_no_worker(url_atual: str, tentativas: int = 15) -> bool:
    """O Worker tem que devolver EXATAMENTE esta URL antes de dizermos ONLINE."""
    for i in range(tentativas):
        try:
            pedido = urllib.request.Request(
                WORKER + "/server",
                headers={"User-Agent": "Aurea-Discovery/1.0 (+https://aurea.app)"})
            with urllib.request.urlopen(pedido, timeout=20) as r:
                doc = json.loads(r.read().decode("utf-8", "replace"))
        except Exception as e:  # noqa: BLE001
            log(f"/server ainda nao respondeu ({e})")
            time.sleep(3)
            continue
        if doc.get("online") is True and doc.get("endpoint") == url_atual:
            log("/server confirma esta URL")
            return True
        log(f"/server devolveu online={doc.get('online')} endpoint={doc.get('endpoint')} — esperando bater")
        time.sleep(3)
    return False


# =============================================================================
# O START
# =============================================================================

def main() -> None:
    log("=== Aurea AI — START ===")

    conferir_gpu()
    montar_drive()

    if not Path(DRIVE_RAIZ).is_dir():
        morrer("o Drive precisa estar montado: e de la que vem os modelos")

    restaurar_comfyui()
    copiar_modelos()

    comfy = subir_comfyui()
    if not esperar_system_stats(f"http://127.0.0.1:{PORTAL}", 60, "local"):
        comfy.terminate()
        morrer("o ComfyUI nao subiu. Veja /content/comfyui.log")

    tunel, url = abrir_tunel()
    if not esperar_system_stats(url, 45, "externo"):
        tunel.terminate()
        comfy.terminate()
        morrer("o endereco do tunel nao respondeu /system_stats. Nada foi publicado.")

    # Publica SÓ depois de o túnel estar saudável.
    sys.path.insert(0, "/content/aurea/colab")
    from publicar_discovery import publicar  # type: ignore

    if not publicar(url, gpu="NVIDIA A100-SXM4-80GB", segredo=ler_secret()):
        morrer("nao consegui publicar no discovery")

    if not conferir_no_worker(url):
        morrer("/server nao devolveu esta URL — o app nao vai encontrar o servidor")

    print("\n" + "=" * 60)
    print("🔥 AUREA AI ONLINE")
    print(f"   endereco: {url}")
    print(f"   publicado em {WORKER}/server")
    print("   O app conecta sozinho — nao precisa de APK novo.")
    print("=" * 60 + "\n")


main()
