"""ComfyUI de mentira, para os testes.

Fala o mesmo HTTP/WebSocket que o ComfyUI de verdade (/prompt, /ws, /history,
/view, /upload/image, /queue, /interrupt, /object_info, /system_stats). Nao e um
endpoint simulado do produto: e o duble da dependencia externa, que existe para
o servidor real poder ser exercitado sem uma A100.

Tambem simula os defeitos que interessam: workflow recusado, execucao que
explode, execucao que demora, e queda do WebSocket no meio.
"""
from __future__ import annotations

import asyncio
import json
import uuid
from typing import Any

from aiohttp import WSMsgType, web

# Bytes que fingem ser o MP4. O teste confere que voltam inteiros pelo /view.
VIDEO_FALSO = b"\x00\x00\x00\x18ftypmp42" + b"aurea-teste-" * 40
IMAGEM_FALSA = b"\xff\xd8\xff\xe0" + b"jpeg-de-teste" * 20 + b"\xff\xd9"


class FakeComfy:
    def __init__(self) -> None:
        self.prompt_id: str | None = None
        self.interrompido = False
        self.uploads: dict[str, bytes] = {}
        self.grafos: dict[str, dict] = {}
        self.historico: dict[str, dict] = {}
        self.execucoes = 0
        self.duracao_s = 0.0
        self.passos = 8
        self.recusar = False
        self.explodir = False
        self.cair_ws = False
        self.sem_video = False
        self.arquivos: dict[str, bytes] = {}
        self.nome_do_video = "aurea_h3_00001.mp4"
        self.views_pedidas: list[dict] = []
        self.app = web.Application()
        self.app.add_routes([
            web.get("/system_stats", self._stats),
            web.get("/object_info", self._info),
            web.post("/prompt", self._prompt),
            web.get("/queue", self._fila),
            web.post("/queue", self._fila_post),
            web.post("/interrupt", self._interromper),
            web.post("/upload/image", self._upload),
            web.get("/history/{pid}", self._historico),
            web.get("/view", self._view),
            web.get("/ws", self._ws),
        ])

    # -- rotas ------------------------------------------------------------
    async def _stats(self, req: web.Request) -> web.Response:
        return web.json_response({
            "system": {"comfyui_version": "0.3.fake"},
            "devices": [{"name": "NVIDIA A100-SXM4-80GB",
                         "vram_total": 85_000_000_000,
                         "vram_free": 70_000_000_000}],
        })

    async def _info(self, req: web.Request) -> web.Response:
        nomes = set()
        for g in self.grafos.values():
            for n in g.values():
                if isinstance(n, dict) and n.get("class_type"):
                    nomes.add(n["class_type"])
        return web.json_response({n: {} for n in
                                  nomes | {"UNETLoader", "CLIPLoader", "VAELoader",
                                           "LoraLoaderModelOnly", "CLIPTextEncode",
                                           "VAEDecode", "CreateVideo", "SaveVideo",
                                           "LoadImage", "MiniMaxH3Sampler",
                                           "MiniMaxH3AudioDecode"}})

    async def _prompt(self, req: web.Request) -> web.Response:
        d = await req.json()
        grafo = d.get("prompt") or {}
        self.execucoes += 1
        if not grafo:
            return web.json_response({"error": "prompt vazio"}, status=400)
        if self.recusar:
            return web.json_response({
                "error": {"type": "prompt_outputs_failed_validation"},
                "node_errors": {"11": {"errors": [{"message": "valor invalido"}]}},
            })
        pid = uuid.uuid4().hex
        self.prompt_id = pid
        self.grafos[pid] = grafo
        self.interrompido = False
        if self.duracao_s > 0:
            asyncio.create_task(self._executar_devagar(pid))
        return web.json_response({"prompt_id": pid, "number": self.execucoes})

    async def _executar_devagar(self, pid: str) -> None:
        await asyncio.sleep(self.duracao_s)
        self._concluir(pid)

    async def _fila(self, req: web.Request) -> web.Response:
        rodando = 1 if (self.prompt_id and self.prompt_id not in self.historico
                        and self.duracao_s > 0) else 0
        return web.json_response({"queue_running": [[0, self.prompt_id, {}, {}, []]] * rodando,
                                  "queue_pending": []})

    async def _fila_post(self, req: web.Request) -> web.Response:
        d = await req.json()
        for pid in d.get("delete") or []:
            self.interrompido = True
            self.historico.setdefault(pid, {"status": {"status_str": "error",
                                                       "messages": []}, "outputs": {}})
        return web.json_response({})

    async def _interromper(self, req: web.Request) -> web.Response:
        self.interrompido = True
        return web.json_response({})

    async def _upload(self, req: web.Request) -> web.Response:
        leitor = await req.multipart()
        nome = "recebido.png"
        dados = b""
        async for parte in leitor:
            if parte.name == "image":
                nome = parte.filename or nome
                dados = await parte.read()
        self.uploads[nome] = dados
        self.arquivos[nome] = dados
        return web.json_response({"name": nome, "subfolder": "", "type": "input"})

    async def _historico(self, req: web.Request) -> web.Response:
        pid = req.match_info["pid"]
        reg = self.historico.get(pid)
        return web.json_response({pid: reg} if reg else {})

    async def _view(self, req: web.Request) -> web.Response:
        nome = req.query.get("filename", "")
        sub = req.query.get("subfolder", "")
        self.views_pedidas.append({"filename": nome, "subfolder": sub,
                                   "type": req.query.get("type", "")})
        if ".." in nome or nome.startswith("/"):
            return web.Response(status=400, text="caminho recusado")
        dados = self.arquivos.get(nome)
        if dados is None:
            return web.Response(status=404, text="nao achei")
        return web.Response(body=dados, content_type="video/mp4")

    # -- websocket --------------------------------------------------------
    async def _ws(self, req: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(req)
        pid = self.prompt_id
        try:
            if self.cair_ws:
                await ws.close()
                return ws
            if not pid:
                await ws.close()
                return ws

            # Com duracao_s, o progresso sai espacado e leva o tempo combinado:
            # e o que permite testar fila, posicao e cancelamento no meio.
            intervalo = (self.duracao_s / self.passos) if self.duracao_s > 0 else 0.01
            for i in range(self.passos):
                if self.interrompido:
                    await ws.close()
                    return ws
                await ws.send_str(json.dumps({"type": "executing",
                                              "data": {"node": "11", "prompt_id": pid}}))
                await ws.send_str(json.dumps({"type": "progress",
                                              "data": {"value": i + 1, "max": self.passos,
                                                       "node": "11", "prompt_id": pid}}))
                await asyncio.sleep(intervalo)
                if self.interrompido:
                    await ws.close()
                    return ws

            if self.explodir:
                await ws.send_str(json.dumps({
                    "type": "execution_error",
                    "data": {"prompt_id": pid, "node_id": "11",
                             "exception_message": "CUDA out of memory",
                             "exception_type": "torch.OutOfMemoryError"}}))
                await ws.close()
                return ws

            for node in ("13", "14", "15", "16"):
                await ws.send_str(json.dumps({"type": "executing",
                                              "data": {"node": node, "prompt_id": pid}}))
            # Grava o historico ANTES de avisar o fim: o cliente busca o
            # historico assim que ve `executing: null`.
            self._concluir(pid)
            await ws.send_str(json.dumps({"type": "executing", "data": {"node": None,
                                                                       "prompt_id": pid}}))
            await asyncio.sleep(0.05)
        except (ConnectionResetError, asyncio.CancelledError):
            pass
        await ws.close()
        return ws

    def _concluir(self, pid: str) -> None:
        if pid in self.historico:
            return
        saidas: dict[str, Any] = {"14": {"audio": [{"filename": "aurea_audio.wav",
                                                    "subfolder": "", "type": "output"}]}}
        if not self.sem_video:
            saidas["16"] = {"videos": [{"filename": self.nome_do_video,
                                        "subfolder": "aurea", "type": "output"}]}
        self.historico[pid] = {"status": {"status_str": "success", "completed": True,
                                          "messages": []},
                               "outputs": saidas}
        self.arquivos[self.nome_do_video] = VIDEO_FALSO
