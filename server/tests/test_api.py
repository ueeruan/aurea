"""Exercita a API pelo soquete, contra um ComfyUI falso.

O que NAO esta provado aqui: a inferencia do MiniMax H3. Nenhum teste deste
arquivo gera video de verdade — eles provam o contrato, a fila, o
cancelamento, o isolamento entre testers e as recusas.
"""
from __future__ import annotations

import asyncio
import json
import time

import aiohttp
import httpx
import pytest

from conftest import (TOKEN_ADMIN, TOKEN_CLIENTE, TOKEN_OUTRO, esperar_job,
                      esperar_ocioso, fake)

JPEG = b"\xff\xd8\xff\xe0" + b"corpo-de-teste" * 30 + b"\xff\xd9"
PNG = b"\x89PNG\r\n\x1a\n" + b"corpo-de-teste" * 30


def _pedido(**troca) -> dict:
    base = {"mode": "text_to_video", "prompt": "um farol no meio da neblina",
            "duration": 5, "aspectRatio": "16:9", "resolution": "standard",
            "fps": 24, "audio": True, "seed": -1, "turbo": True}
    base.update(troca)
    return base


# ---------------------------------------------------------------------------
# Identidade do servidor
# ---------------------------------------------------------------------------

async def test_health_e_publico_e_se_identifica(base):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/health")
    assert r.status_code == 200
    d = r.json()
    assert d["service"] == "aurea-ai"      # e o que o app exige
    assert d["engine"] == "minimax-h3"
    assert d["version"] == 1
    assert d["ready"] is True
    assert d["gpu"] == "NVIDIA A100-SXM4-80GB"
    assert d["vram_total_mb"] > 80_000


async def test_health_nao_vaza_token_nem_caminho(base):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/health")
    texto = r.text
    for proibido in (TOKEN_CLIENTE, TOKEN_ADMIN, "/content", "127.0.0.1"):
        assert proibido not in texto


async def test_sem_token_da_401_com_formato_de_erro(base):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/capabilities")
    assert r.status_code == 401
    assert set(r.json()) == {"error", "detail"}


async def test_token_errado_da_401(base):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/capabilities",
                        headers={"Authorization": "Bearer nao-e-o-token"})
    assert r.status_code == 401


async def test_cabecalho_malformado_da_401(base):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/capabilities",
                        headers={"Authorization": TOKEN_CLIENTE})
    assert r.status_code == 401
    assert "Bearer" in r.json()["detail"]


# ---------------------------------------------------------------------------
# Capacidades
# ---------------------------------------------------------------------------

async def test_capacidades_montam_a_ui(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/capabilities", headers=cabecalho)
    assert r.status_code == 200
    d = r.json()
    assert set(d["modes"]) == {"text_to_video", "image_to_video"}
    assert d["durations"] == [5, 10, 15]
    assert d["aspectRatios"] == ["9:16", "16:9", "1:1", "4:5"]
    assert d["resolutions"] == ["preview", "standard", "high"]
    assert d["audio"] is True
    assert d["maxConcurrentJobs"] == 1


# ---------------------------------------------------------------------------
# Validacao — nada de valor arbitrario (§23)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("troca", [
    {"duration": 7},
    {"aspectRatio": "21:9"},
    {"resolution": "ultra"},
    {"fps": 60},
    {"mode": "video_to_video"},
    {"prompt": ""},
    {"prompt": "x" * 2001},
])
async def test_valores_fora_da_tabela_sao_recusados(base, cabecalho, troca):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(**troca))
    assert r.status_code == 422, r.text


async def test_image_to_video_exige_asset(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(mode="image_to_video"))
    assert r.status_code == 422


async def test_text_to_video_recusa_asset(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(imageAssetId="11111111-2222-3333-4444-555555555555"))
    assert r.status_code == 422


@pytest.mark.parametrize("alvo", [
    "../../etc/passwd",
    "/etc/passwd",
    "..\\..\\windows\\system32",
    "11111111-2222-3333-4444-555555555555/../../x",
    "nao-e-uuid",
])
async def test_asset_id_nunca_vira_caminho(base, cabecalho, alvo):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(mode="image_to_video", imageAssetId=alvo))
    assert r.status_code == 422, r.text


async def test_i2v_com_asset_inexistente_da_404(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(mode="image_to_video",
                                      imageAssetId="11111111-2222-3333-4444-555555555555"))
    assert r.status_code == 404


# ---------------------------------------------------------------------------
# Upload
# ---------------------------------------------------------------------------

async def test_upload_devolve_uuid_e_ignora_o_nome_do_cliente(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                         files={"file": ("../../../etc/passwd.png", PNG, "image/png")})
    assert r.status_code == 201
    aid = r.json()["assetId"]
    assert len(aid) == 36 and aid.count("-") == 4
    assert "passwd" not in aid


async def test_upload_recusa_o_que_nao_e_imagem(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                         files={"file": ("x.png", b"<html>nao sou imagem</html>", "image/png")})
    assert r.status_code == 415


async def test_upload_recusa_tipo_fora_da_lista(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                         files={"file": ("x.gif", b"GIF89a", "image/gif")})
    assert r.status_code == 415


async def test_upload_recusa_arquivo_grande(base, cabecalho):
    grande = b"\x89PNG\r\n\x1a\n" + b"\x00" * (1024 * 1024 + 10)
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                         files={"file": ("x.png", grande, "image/png")})
    assert r.status_code == 413


async def test_upload_vazio_e_recusado(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                         files={"file": ("x.png", b"", "image/png")})
    assert r.status_code in (413, 422)


# ---------------------------------------------------------------------------
# Caminho feliz — texto para video
# ---------------------------------------------------------------------------

async def test_t2v_do_pedido_ao_arquivo(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        assert r.status_code == 202, r.text
        job_id = r.json()["jobId"]
        assert r.json()["status"] == "queued"

        d = await esperar_job(base, cabecalho, job_id)
        assert d["status"] == "completed", d
        assert d["progress"] == 1.0
        assert d["error"] is None

        res = d["result"]
        assert res["width"] == 1344 and res["height"] == 768      # 16:9 em standard
        assert res["duration"] == 5.0 and res["fps"] == 24
        assert res["videoUrl"] == f"/api/v1/generations/{job_id}/video"
        assert res["videoUrl"].startswith("/api/v1/")             # relativo, nao caminho do Colab
        assert res["hasAudio"] is True


async def test_o_video_volta_inteiro(base, cabecalho, comfy):
    from fake_comfy import VIDEO_FALSO
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        job_id = r.json()["jobId"]
        await esperar_job(base, cabecalho, job_id)

        v = await c.get(f"{base}/api/v1/generations/{job_id}/video", headers=cabecalho)
        assert v.status_code == 200
        assert v.headers["content-type"] == "video/mp4"
        assert v.content == VIDEO_FALSO


async def test_o_video_aceita_range(base, cabecalho):
    """Sem Range o player do app nao consegue buscar o meio do arquivo."""
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        job_id = r.json()["jobId"]
        await esperar_job(base, cabecalho, job_id)

        v = await c.get(f"{base}/api/v1/generations/{job_id}/video",
                        headers={**cabecalho, "Range": "bytes=0-9"})
    assert v.status_code in (200, 206), v.text
    if v.status_code == 206:
        assert len(v.content) == 10
        assert v.headers.get("content-range", "").startswith("bytes 0-9/")


async def test_sem_video_pronto_da_404(base, cabecalho):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/generations/nao-existe/video", headers=cabecalho)
    assert r.status_code == 404
    assert r.json()["error"] == "not_found"


async def test_o_workflow_recebe_o_que_o_app_pediu(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(prompt="um dragao de papel", seed=4242,
                                      aspectRatio="9:16", duration=10))
        job_id = r.json()["jobId"]
        await esperar_job(base, cabecalho, job_id)

    grafo = next(iter(comfy.grafos.values()))
    assert grafo["6"]["inputs"]["text"] == "um dragao de papel"
    assert grafo["11"]["inputs"]["seed"] == 4242
    assert grafo["11"]["inputs"]["width"] == 768      # 9:16 em standard
    assert grafo["11"]["inputs"]["height"] == 1344
    assert grafo["11"]["inputs"]["length"] == 240     # 10 s a 24 fps
    assert grafo["16"]["inputs"]["filename_prefix"].startswith("aurea/")
    assert "_aurea" not in grafo                      # o bloco interno nao vaza


async def test_nenhum_campo_do_cliente_vira_comando(base, cabecalho, comfy):
    """O grafo enviado tem que ser o do arquivo, so com os inputs trocados."""
    from aurea_ai.workflows import Workflow
    from pathlib import Path
    import conftest
    wf = Workflow.carregar(Path(conftest.RAIZ) / "workflows" / "h3_t2v.json")

    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        await esperar_job(base, cabecalho, r.json()["jobId"])

    grafo = next(iter(comfy.grafos.values()))
    assert set(grafo) == set(wf.grafo)                       # mesmos nodes
    for nid, node in grafo.items():
        assert node["class_type"] == wf.grafo[nid]["class_type"]
        assert set(node["inputs"]) == set(wf.grafo[nid]["inputs"])   # mesmos campos


# ---------------------------------------------------------------------------
# Imagem para video
# ---------------------------------------------------------------------------

async def test_i2v_sobe_a_imagem_e_gera(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        up = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                          files={"file": ("foto.jpeg", JPEG, "image/jpeg")})
        aid = up.json()["assetId"]

        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(mode="image_to_video", imageAssetId=aid))
        assert r.status_code == 202, r.text
        d = await esperar_job(base, cabecalho, r.json()["jobId"])

    assert d["status"] == "completed", d
    # A imagem chegou ao ComfyUI com o UUID, nunca com o nome do cliente.
    assert list(comfy.uploads) == [f"{aid}.png"]
    assert comfy.uploads[f"{aid}.png"] == JPEG
    grafo = next(iter(comfy.grafos.values()))
    assert grafo["8"]["inputs"]["image"] == f"{aid}.png"


async def test_i2v_nao_deixa_o_nome_do_cliente_chegar_ao_comfy(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        up = await c.post(f"{base}/api/v1/assets", headers=cabecalho,
                          files={"file": ("../../../segredo.png", PNG, "image/png")})
        aid = up.json()["assetId"]
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                         json=_pedido(mode="image_to_video", imageAssetId=aid))
        await esperar_job(base, cabecalho, r.json()["jobId"])

    assert all("segredo" not in n and ".." not in n for n in comfy.uploads)


# ---------------------------------------------------------------------------
# Fila
# ---------------------------------------------------------------------------

async def test_o_segundo_job_espera_a_vez(base, cabecalho, comfy):
    comfy.duracao_s = 1.2
    async with httpx.AsyncClient(timeout=40) as c:
        a = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        b = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        ja, jb = a.json()["jobId"], b.json()["jobId"]

        await asyncio.sleep(0.4)
        da = (await c.get(f"{base}/api/v1/generations/{ja}", headers=cabecalho)).json()
        db = (await c.get(f"{base}/api/v1/generations/{jb}", headers=cabecalho)).json()

        assert da["status"] in ("loading_model", "generating", "encoding_prompt")
        assert db["status"] == "queued"
        assert db["queuePosition"] == 0      # e o proximo da fila

        # Um terceiro fica atras dele.
        terceiro = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jc = terceiro.json()["jobId"]
        dc = (await c.get(f"{base}/api/v1/generations/{jc}", headers=cabecalho)).json()
        assert dc["queuePosition"] == 1

        # Com MAX_GPU_JOBS=1, o ComfyUI nunca recebeu dois pedidos ao mesmo tempo.
        assert comfy.execucoes == 1
        await esperar_job(base, cabecalho, ja)
        await esperar_job(base, cabecalho, jb)
        await esperar_job(base, cabecalho, jc)
    assert comfy.execucoes == 3


async def test_fila_cheia_da_429(base, cabecalho, comfy):
    from aurea_ai.app import estado
    comfy.duracao_s = 2.0
    original = estado.cfg.max_queue
    object.__setattr__(estado.cfg, "max_queue", 1)
    try:
        async with httpx.AsyncClient(timeout=40) as c:
            await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
            await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
            r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        assert r.status_code == 429
        assert r.json()["error"] == "rate_limited" or r.status_code == 429
    finally:
        object.__setattr__(estado.cfg, "max_queue", original)
        await esperar_ocioso()


async def test_teto_de_tres_jobs_por_tester(base, cabecalho, comfy):
    comfy.duracao_s = 2.0
    async with httpx.AsyncClient(timeout=40) as c:
        for _ in range(3):
            await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
    assert r.status_code == 429
    assert "3 jobs" in r.json()["detail"]
    await esperar_ocioso()


# ---------------------------------------------------------------------------
# Cancelamento
# ---------------------------------------------------------------------------

async def test_cancelar_na_fila(base, cabecalho, comfy):
    comfy.duracao_s = 3.0
    async with httpx.AsyncClient(timeout=40) as c:
        a = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        b = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jb = b.json()["jobId"]

        r = await c.delete(f"{base}/api/v1/generations/{jb}", headers=cabecalho)
        assert r.status_code == 200
        assert r.json()["status"] == "cancelled"

        d = (await c.get(f"{base}/api/v1/generations/{jb}", headers=cabecalho)).json()
        assert d["status"] == "cancelled"
        assert comfy.execucoes == 1     # o cancelado nunca chegou ao ComfyUI
        await c.delete(f"{base}/api/v1/generations/{a.json()['jobId']}", headers=cabecalho)
    await esperar_ocioso()


async def test_cancelar_no_meio_da_geracao(base, cabecalho, comfy):
    comfy.duracao_s = 3.0
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        job_id = r.json()["jobId"]
        await asyncio.sleep(0.6)
        meio = (await c.get(f"{base}/api/v1/generations/{job_id}", headers=cabecalho)).json()
        assert meio["status"] == "generating", meio

        await c.delete(f"{base}/api/v1/generations/{job_id}", headers=cabecalho)
        await asyncio.sleep(0.4)
        d = (await c.get(f"{base}/api/v1/generations/{job_id}", headers=cabecalho)).json()
    assert d["status"] == "cancelled"
    # Interrompeu no ComfyUI de verdade, nao so marcou o estado.
    assert comfy.interrompido is True
    await esperar_ocioso()


async def test_cancelar_duas_vezes_nao_quebra(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        await esperar_job(base, cabecalho, jid)
        r = await c.delete(f"{base}/api/v1/generations/{jid}", headers=cabecalho)
    assert r.status_code == 200
    assert r.json()["status"] == "completed"    # job concluido nao vira cancelado


async def test_cancelar_job_alheio_da_404(base, cabecalho, cabecalho_outro, comfy):
    comfy.duracao_s = 3.0
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        outro = await c.delete(f"{base}/api/v1/generations/{jid}", headers=cabecalho_outro)
        assert outro.status_code == 404
        await c.delete(f"{base}/api/v1/generations/{jid}", headers=cabecalho)
    await esperar_ocioso()


# ---------------------------------------------------------------------------
# Isolamento entre testers
# ---------------------------------------------------------------------------

async def test_um_tester_nao_ve_o_job_do_outro(base, cabecalho, cabecalho_outro):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        await esperar_job(base, cabecalho, jid)

        r = await c.get(f"{base}/api/v1/generations/{jid}", headers=cabecalho_outro)
        assert r.status_code == 404
        r = await c.get(f"{base}/api/v1/generations/{jid}/video", headers=cabecalho_outro)
        assert r.status_code == 404


async def test_historico_so_do_dono(base, cabecalho, cabecalho_outro):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        await esperar_job(base, cabecalho, jid)

        meu = (await c.get(f"{base}/api/v1/generations", headers=cabecalho)).json()
        dele = (await c.get(f"{base}/api/v1/generations", headers=cabecalho_outro)).json()
    assert any(j["jobId"] == jid for j in meu)
    assert not any(j["jobId"] == jid for j in dele)


async def test_admin_ve_tudo(base, cabecalho, cabecalho_admin):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        await esperar_job(base, cabecalho, jid)
        d = await c.get(f"{base}/api/v1/generations/{jid}", headers=cabecalho_admin)
    assert d.status_code == 200


async def test_admin_status_so_com_token_admin(base, cabecalho, cabecalho_admin):
    async with httpx.AsyncClient() as c:
        r = await c.get(f"{base}/api/v1/admin/status", headers=cabecalho)
        assert r.status_code == 403
        r = await c.get(f"{base}/api/v1/admin/status", headers=cabecalho_admin)
    assert r.status_code == 200
    assert r.json()["pronto"] is True


# ---------------------------------------------------------------------------
# Eventos
# ---------------------------------------------------------------------------

async def test_websocket_conta_a_historia_do_job(base, cabecalho, comfy):
    comfy.duracao_s = 0.8
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]

    url = base.replace("http://", "ws://") + f"/api/v1/generations/{jid}/events"
    tipos, ultimo = [], None
    async with aiohttp.ClientSession() as s:
        async with s.ws_connect(url, headers={"Authorization": f"Bearer {TOKEN_CLIENTE}"}) as ws:
            async for msg in ws:
                if msg.type != aiohttp.WSMsgType.TEXT:
                    break
                d = json.loads(msg.data)
                tipos.append(d["type"])
                if d["type"] == "completed":
                    ultimo = d
                    break
                if time.time() > 0 and len(tipos) > 200:
                    break

    assert "progress" in tipos, tipos
    assert "status" in tipos
    assert tipos[-1] == "completed"
    assert ultimo["result"]["duration"] == 5.0


async def test_websocket_sem_token_recusa(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
    url = base.replace("http://", "ws://") + f"/api/v1/generations/{jid}/events"
    async with aiohttp.ClientSession() as s:
        with pytest.raises(aiohttp.WSServerHandshakeError) as e:
            await s.ws_connect(url)
    assert e.value.status in (401, 403, 4401)


async def test_websocket_de_job_alheio_recusa(base, cabecalho, comfy):
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
    url = base.replace("http://", "ws://") + f"/api/v1/generations/{jid}/events"
    async with aiohttp.ClientSession() as s:
        with pytest.raises(aiohttp.WSServerHandshakeError):
            await s.ws_connect(url, headers={"Authorization": f"Bearer {TOKEN_OUTRO}"})


# ---------------------------------------------------------------------------
# Falhas do motor
# ---------------------------------------------------------------------------

async def test_workflow_recusado_vira_erro_claro(base, cabecalho, comfy):
    comfy.recusar = True
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        d = await esperar_job(base, cabecalho, r.json()["jobId"])
    assert d["status"] == "failed"
    assert d["error"] == "comfy_rejected"
    assert d["result"] is None


async def test_erro_na_execucao_vira_erro_claro(base, cabecalho, comfy):
    comfy.explodir = True
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        d = await esperar_job(base, cabecalho, r.json()["jobId"])
    assert d["status"] == "failed"
    assert d["error"] == "generation_failed"


async def test_sem_video_gravado_vira_erro(base, cabecalho, comfy):
    comfy.sem_video = True
    async with httpx.AsyncClient(timeout=40) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        d = await esperar_job(base, cabecalho, r.json()["jobId"])
    assert d["status"] == "failed"
    assert d["error"] == "result_missing"


async def test_queda_do_websocket_nao_perde_o_job(base, cabecalho, comfy):
    """O WS pode cair; o ComfyUI segue trabalhando. O job nao pode morrer."""
    comfy.duracao_s = 1.2
    comfy.cair_ws = True
    async with httpx.AsyncClient(timeout=60) as c:
        r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
        jid = r.json()["jobId"]
        # Depois da primeira queda o duble passa a atender normalmente.
        await asyncio.sleep(1.0)
        comfy.cair_ws = False
        comfy._concluir(comfy.prompt_id)

        limite = time.time() + 25
        d = None
        while time.time() < limite:
            d = (await c.get(f"{base}/api/v1/generations/{jid}", headers=cabecalho)).json()
            if d["status"] in ("completed", "failed"):
                break
            await asyncio.sleep(0.2)
    assert d and d["status"] == "completed", d


# ---------------------------------------------------------------------------
# Limite de taxa
# ---------------------------------------------------------------------------

async def test_limite_de_taxa_da_429(base, cabecalho):
    from aurea_ai import auth
    auth.reset_limite_for_tests(3)
    try:
        async with httpx.AsyncClient(timeout=40) as c:
            codigos = []
            for _ in range(6):
                r = await c.post(f"{base}/api/v1/generations", headers=cabecalho,
                                 json=_pedido(prompt="x"))
                codigos.append(r.status_code)
            ultima = r
        assert 429 in codigos
        assert ultima.json()["error"] == "rate_limited"
        assert ultima.headers.get("retry-after") == "60"
    finally:
        auth.reset_limite_for_tests(200)
        await esperar_ocioso()


# ---------------------------------------------------------------------------
# Sem servidor de geracao
# ---------------------------------------------------------------------------

async def test_engine_fora_do_ar_da_503(base, cabecalho, comfy, monkeypatch):
    from aurea_ai.app import estado
    original = estado.comfy.base
    estado.comfy.base = "http://127.0.0.1:1"
    try:
        async with httpx.AsyncClient(timeout=40) as c:
            r = await c.post(f"{base}/api/v1/generations", headers=cabecalho, json=_pedido())
            assert r.status_code == 503
            h = await c.get(f"{base}/api/v1/health")
            assert h.json()["ready"] is False
            assert h.json()["status"] == "degraded"
    finally:
        estado.comfy.base = original
