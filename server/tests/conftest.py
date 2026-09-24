"""Sobe o ComfyUI falso e o servidor de verdade, e aponta um para o outro.

Ordem importa: as variaveis de ambiente precisam estar no lugar antes de
`aurea_ai.app` ser importado, porque o modulo le a configuracao no import.
"""
from __future__ import annotations

import asyncio
import os
import socket
import sys
import tempfile
import threading
import time
from pathlib import Path

import pytest

RAIZ = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RAIZ))

TMP = Path(tempfile.mkdtemp(prefix="aurea-ai-testes-"))

TOKEN_CLIENTE = "token-do-cliente"
TOKEN_ADMIN = "token-do-admin"
TOKEN_OUTRO = "token-do-outro-tester"   # segundo tester: token proprio


def _porta_livre() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    porta = s.getsockname()[1]
    s.close()
    return porta


PORTA_COMFY = _porta_livre()
PORTA_API = _porta_livre()

# --- ambiente, antes de importar o app --------------------------------------
os.environ.update({
    "AUREA_MODE": "dev",
    "AUREA_SERVER_TOKENS": f"{TOKEN_CLIENTE},{TOKEN_OUTRO}",
    "AUREA_ADMIN_TOKEN": TOKEN_ADMIN,
    "AUREA_COMFY_URL": f"http://127.0.0.1:{PORTA_COMFY}",
    "AUREA_UPLOAD_DIR": str(TMP / "assets"),
    "AUREA_WORKFLOWS_DIR": str(RAIZ / "workflows"),
    "AUREA_MODELS_DIR": str(TMP / "models"),
    "AUREA_PUBLIC_URL": "https://exemplo.invalido",
    "AUREA_TUNNEL": "manual",
    "AUREA_MAX_GPU_JOBS": "1",
    "AUREA_MAX_QUEUE": "20",
    "AUREA_RATE_LIMIT_PER_MIN": "200",
    "AUREA_JOB_TIMEOUT_S": "60",
    "AUREA_MAX_UPLOAD_MB": "1",
    "AUREA_COMFY_TIMEOUT_S": "30",
})
os.environ.pop("AUREA_DISCOVERY_REPO", None)
os.environ.pop("AUREA_DISCOVERY_TOKEN", None)

# Pesos de mentira. O servidor exige que existam e nao estejam vazios, e sem
# manifesto e exatamente isso que ele verifica — o ComfyUI falso nao le arquivo.
_MODELOS = TMP / "models"
_MODELOS.mkdir(parents=True, exist_ok=True)
for _nome in ("minimax_h3_fl2va_pruned_int8_convrot.safetensors",
              "qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors",
              "minimax_h3_video_vae_int8_convrot.safetensors",
              "minimax_h3_audio_vae_fp32.safetensors",
              "minimax_h3_turbo_lora.safetensors"):
    (_MODELOS / _nome).write_bytes(b"peso-de-teste")

from aiohttp import web  # noqa: E402
from fake_comfy import FakeComfy  # noqa: E402

fake = FakeComfy()


def _rodar_comfy() -> None:
    async def _principal() -> None:
        runner = web.AppRunner(fake.app)
        await runner.setup()
        site = web.TCPSite(runner, "127.0.0.1", PORTA_COMFY)
        await site.start()
        await asyncio.Event().wait()

    asyncio.run(_principal())


def _rodar_api() -> None:
    import uvicorn
    from aurea_ai.app import app
    uvicorn.run(app, host="127.0.0.1", port=PORTA_API, log_level="warning",
                ws_ping_interval=20, ws_ping_timeout=20)


def _esperar(url: str, segundos: float = 30.0) -> None:
    import httpx
    limite = time.time() + segundos
    while time.time() < limite:
        try:
            if httpx.get(url, timeout=2).status_code < 500:
                return
        except Exception:  # noqa: BLE001
            time.sleep(0.15)
    raise RuntimeError(f"{url} nao subiu em {segundos}s")


def pytest_configure(config: pytest.Config) -> None:
    threading.Thread(target=_rodar_comfy, daemon=True, name="comfy-falso").start()
    _esperar(f"http://127.0.0.1:{PORTA_COMFY}/system_stats")
    threading.Thread(target=_rodar_api, daemon=True, name="aurea-ai").start()
    _esperar(f"http://127.0.0.1:{PORTA_API}/api/v1/health")


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def base() -> str:
    return f"http://127.0.0.1:{PORTA_API}"


@pytest.fixture
def comfy() -> FakeComfy:
    """Zera o duble entre testes: sem isso um job do teste anterior vaza."""
    fake.prompt_id = None
    fake.interrompido = False
    fake.historico.clear()
    fake.grafos.clear()
    fake.uploads.clear()
    fake.arquivos.clear()
    fake.views_pedidas.clear()
    fake.execucoes = 0
    fake.duracao_s = 0.0
    fake.recusar = False
    fake.explodir = False
    fake.cair_ws = False
    fake.sem_video = False
    fake.nome_do_video = "aurea_h3_00001.mp4"
    yield fake
    # Espera o servidor esvaziar a fila antes do proximo teste: um job do
    # teste anterior vazando mudaria a contagem de chamadas ao ComfyUI.
    from aurea_ai.app import estado
    limite = time.time() + 30
    while time.time() < limite:
        if estado.fila and estado.fila.pendentes.qsize() == 0:
            ativos = [j for j in estado.fila.jobs.values()
                      if j.status.value in ("queued", "loading_model", "encoding_prompt",
                                            "generating", "decoding", "encoding_video")]
            if not ativos:
                break
        time.sleep(0.05)


@pytest.fixture
def cabecalho() -> dict:
    return {"Authorization": f"Bearer {TOKEN_CLIENTE}"}


@pytest.fixture
def cabecalho_admin() -> dict:
    return {"Authorization": f"Bearer {TOKEN_ADMIN}"}


@pytest.fixture
def cabecalho_outro() -> dict:
    return {"Authorization": f"Bearer {TOKEN_OUTRO}"}


def esperar_ocioso_sincrono(segundos: float = 20.0) -> None:
    """Igual a `esperar_ocioso`, para uso fora de um laco assincrono."""
    limite = time.time() + segundos
    while time.time() < limite:
        time.sleep(0.05)
        from aurea_ai.app import estado
        if estado.fila and estado.fila.pendentes.qsize() == 0:
            ativos = [j for j in estado.fila.jobs.values()
                      if j.status.value in ("queued", "loading_model", "encoding_prompt",
                                            "generating", "decoding", "encoding_video")]
            if not ativos:
                return


async def esperar_ocioso(segundos: float = 20.0) -> None:
    from aurea_ai.app import estado
    limite = time.time() + segundos
    while time.time() < limite:
        if estado.fila and estado.fila.pendentes.qsize() == 0:
            ativos = [j for j in estado.fila.jobs.values()
                      if j.status.value in ("queued", "loading_model", "encoding_prompt",
                                            "generating", "decoding", "encoding_video")]
            if not ativos:
                return
        await asyncio.sleep(0.1)


async def esperar_job(base: str, cabecalho: dict, job_id: str, segundos: float = 30.0) -> dict:
    import httpx
    limite = time.time() + segundos
    async with httpx.AsyncClient() as c:
        while time.time() < limite:
            r = await c.get(f"{base}/api/v1/generations/{job_id}", headers=cabecalho)
            if r.status_code == 200:
                d = r.json()
                if d["status"] in ("completed", "failed", "cancelled"):
                    return d
            await asyncio.sleep(0.1)
    raise AssertionError(f"job {job_id} nao terminou em {segundos}s")


@pytest.fixture
def tmp_assets() -> Path:
    return TMP / "assets"
