"""Cliente do ComfyUI.

O ComfyUI e o backend de execucao. Nada aqui e exposto ao app: o app fala com
o `/api/v1` do Aurea AI e o Aurea AI fala com o ComfyUI em 127.0.0.1.
"""
from __future__ import annotations

import asyncio
import json
import logging
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, AsyncIterator, Callable
from urllib.parse import urlparse

import aiohttp

log = logging.getLogger("aurea_ai.comfy")


class ComfyIndisponivel(RuntimeError):
    pass


class ComfyErro(RuntimeError):
    def __init__(self, mensagem: str, node: str = "", detalhe: str = "") -> None:
        super().__init__(mensagem)
        self.node = node
        self.detalhe = detalhe


@dataclass
class Progresso:
    """O que o ComfyUI conta, ja traduzido para as etapas do contrato."""
    step: int = 0
    total: int = 0
    node: str = ""
    executando: bool = False
    saidas: dict[str, Any] = field(default_factory=dict)

    @property
    def fracao(self) -> float:
        if self.total <= 0:
            return 0.0
        return max(0.0, min(1.0, self.step / self.total))


# Etapas do contrato, na ordem. O indice no ComfyUI nao diz nada ao usuario;
# a posicao do node dentro deste percurso, sim.
ETAPAS = (
    ("loading_model", "Carregando modelo"),
    ("encoding_prompt", "Lendo o prompt"),
    ("generating", "Gerando"),
    ("decoding", "Decodificando"),
    ("encoding_video", "Gravando o video"),
)


class Comfy:
    def __init__(self, base: str, timeout_s: int = 30) -> None:
        self.base = base.rstrip("/")
        self.timeout_s = timeout_s
        self.client_id = uuid.uuid4().hex
        self._sessao: aiohttp.ClientSession | None = None

    # -- ciclo de vida ----------------------------------------------------
    async def sessao(self) -> aiohttp.ClientSession:
        if self._sessao is None or self._sessao.closed:
            self._sessao = aiohttp.ClientSession(
                timeout=aiohttp.ClientTimeout(total=None, sock_connect=10, sock_read=self.timeout_s)
            )
        return self._sessao

    async def fechar(self) -> None:
        if self._sessao and not self._sessao.closed:
            await self._sessao.close()
        self._sessao = None

    def ws_url(self) -> str:
        p = urlparse(self.base)
        esquema = "wss" if p.scheme == "https" else "ws"
        return f"{esquema}://{p.netloc}/ws?clientId={self.client_id}"

    # -- consultas --------------------------------------------------------
    async def vivo(self) -> bool:
        try:
            s = await self.sessao()
            async with s.get(f"{self.base}/system_stats", timeout=aiohttp.ClientTimeout(total=5)) as r:
                return r.status == 200
        except Exception:
            return False

    async def estatisticas(self) -> dict:
        s = await self.sessao()
        try:
            async with s.get(f"{self.base}/system_stats") as r:
                if r.status != 200:
                    raise ComfyIndisponivel(f"system_stats devolveu {r.status}")
                return await r.json()
        except aiohttp.ClientError as e:
            raise ComfyIndisponivel(f"ComfyUI nao respondeu: {e}") from e

    async def object_info(self) -> dict:
        s = await self.sessao()
        try:
            async with s.get(f"{self.base}/object_info",
                             timeout=aiohttp.ClientTimeout(total=60)) as r:
                if r.status != 200:
                    raise ComfyIndisponivel(f"object_info devolveu {r.status}")
                return await r.json()
        except aiohttp.ClientError as e:
            raise ComfyIndisponivel(f"ComfyUI nao respondeu: {e}") from e

    async def tamanho_fila(self) -> int:
        s = await self.sessao()
        try:
            async with s.get(f"{self.base}/queue") as r:
                if r.status != 200:
                    return 0
                d = await r.json()
                return len(d.get("queue_running", [])) + len(d.get("queue_pending", []))
        except Exception:
            return 0

    # -- envio ------------------------------------------------------------
    async def enviar(self, grafo: dict) -> str:
        s = await self.sessao()
        corpo = {"prompt": grafo, "client_id": self.client_id}
        try:
            async with s.post(f"{self.base}/prompt", json=corpo) as r:
                texto = await r.text()
                if r.status != 200:
                    raise ComfyErro("o ComfyUI recusou o workflow", detalhe=texto[:2000])
                d = json.loads(texto)
        except aiohttp.ClientError as e:
            raise ComfyIndisponivel(f"falha ao enviar ao ComfyUI: {e}") from e

        if d.get("node_errors"):
            # Erro de validacao: o no culpado vem nomeado.
            primeiro = next(iter(d["node_errors"].items()))
            raise ComfyErro("workflow invalido para este ComfyUI",
                            node=str(primeiro[0]),
                            detalhe=json.dumps(primeiro[1])[:2000])
        pid = d.get("prompt_id")
        if not pid:
            raise ComfyErro("ComfyUI nao devolveu prompt_id", detalhe=texto_curto(d))
        return str(pid)

    async def interromper(self) -> None:
        s = await self.sessao()
        try:
            async with s.post(f"{self.base}/interrupt") as r:
                await r.read()
        except Exception as e:
            log.warning("interrupt falhou: %s", e)

    async def apagar_da_fila(self, prompt_id: str) -> None:
        s = await self.sessao()
        try:
            async with s.post(f"{self.base}/queue", json={"delete": [prompt_id]}) as r:
                await r.read()
        except Exception as e:
            log.warning("delete da fila falhou: %s", e)

    async def subir_imagem(self, nome: str, dados: bytes) -> str:
        """Devolve o nome com que o ComfyUI guardou. O nome vem do servidor
        (UUID + extensao), nunca do cliente."""
        s = await self.sessao()
        forma = aiohttp.FormData()
        forma.add_field("image", dados, filename=nome, content_type="application/octet-stream")
        forma.add_field("overwrite", "true")
        forma.add_field("type", "input")
        try:
            async with s.post(f"{self.base}/upload/image", data=forma) as r:
                if r.status != 200:
                    raise ComfyErro("upload recusado pelo ComfyUI", detalhe=(await r.text())[:1000])
                d = await r.json()
        except aiohttp.ClientError as e:
            raise ComfyIndisponivel(f"falha no upload ao ComfyUI: {e}") from e
        nome = d.get("name") or nome
        sub = d.get("subfolder") or ""
        return f"{sub}/{nome}" if sub else nome

    async def historico(self, prompt_id: str) -> dict:
        s = await self.sessao()
        async with s.get(f"{self.base}/history/{prompt_id}") as r:
            if r.status != 200:
                return {}
            return await r.json()

    async def baixar(self, nome: str, sub: str, tipo: str) -> bytes:
        """Pega um arquivo gerado. `nome`/`sub` vem do historico do ComfyUI,
        nao do cliente — mesmo assim, barra e `..` sao recusados."""
        for campo in (nome, sub):
            if ".." in campo or campo.startswith("/") or "\\" in campo:
                raise ComfyErro("caminho recusado", detalhe=campo[:200])
        s = await self.sessao()
        params = {"filename": nome, "subfolder": sub, "type": tipo}
        async with s.get(f"{self.base}/view", params=params) as r:
            if r.status != 200:
                raise ComfyErro("arquivo nao encontrado no ComfyUI", detalhe=nome)
            return await r.read()

    # -- progresso --------------------------------------------------------
    async def acompanhar(
        self,
        prompt_id: str,
        ao_progresso: Callable[[Progresso, str], None],
        cancelado: Callable[[], bool],
        timeout_s: int,
    ) -> dict:
        """Segue o WebSocket do ComfyUI ate o prompt terminar.

        Devolve o dicionario de saidas do historico. Levanta ComfyErro em falha,
        asyncio.CancelledError em cancelamento, TimeoutError no teto de tempo.
        """
        s = await self.sessao()
        prog = Progresso()
        etapa_atual = "queued"
        limite = asyncio.get_event_loop().time() + timeout_s

        while True:
            if cancelado():
                await self.interromper()
                raise asyncio.CancelledError()
            if asyncio.get_event_loop().time() > limite:
                await self.interromper()
                raise TimeoutError("tempo maximo do job estourado")

            try:
                async with s.ws_connect(self.ws_url(), heartbeat=20, max_msg_size=0) as ws:
                    while True:
                        if cancelado():
                            await self.interromper()
                            raise asyncio.CancelledError()
                        if asyncio.get_event_loop().time() > limite:
                            await self.interromper()
                            raise TimeoutError("tempo maximo do job estourado")

                        try:
                            msg = await ws.receive(timeout=10)
                        except asyncio.TimeoutError:
                            continue
                        if msg.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                            break
                        if msg.type != aiohttp.WSMsgType.TEXT:
                            continue

                        try:
                            d = json.loads(msg.data)
                        except json.JSONDecodeError:
                            continue

                        tipo = d.get("type")
                        dados = d.get("data") or {}
                        # O WS e de todos os clientes: filtra o nosso prompt.
                        pid = dados.get("prompt_id")
                        if pid and pid != prompt_id:
                            continue

                        if tipo == "progress":
                            prog.step = int(dados.get("value") or 0)
                            prog.total = int(dados.get("max") or 0)
                            prog.node = str(dados.get("node") or "")
                            prog.executando = True
                            etapa_atual = "generating"
                            ao_progresso(prog, etapa_atual)

                        elif tipo == "executing":
                            node = dados.get("node")
                            if node is None:
                                # Fim da execucao: busca o historico e sai.
                                return await self._saidas(prompt_id)
                            etapa_atual = _etapa_do_node(node)
                            prog.node = str(node)
                            ao_progresso(prog, etapa_atual)

                        elif tipo == "executed":
                            node = str(dados.get("node") or "")
                            for v in (dados.get("output") or {}).values():
                                if isinstance(v, list):
                                    prog.saidas.setdefault(node, []).extend(v)
                            ao_progresso(prog, etapa_atual)

                        elif tipo == "execution_error":
                            raise ComfyErro(
                                str(dados.get("exception_message") or "erro no ComfyUI"),
                                node=str(dados.get("node_id") or ""),
                                detalhe=str(dados.get("exception_type") or "")[:500],
                            )

                        elif tipo == "execution_cached":
                            ao_progresso(prog, etapa_atual)

            except aiohttp.ClientError as e:
                # Queda do WS nao mata o job: o ComfyUI segue executando. Tenta
                # reconectar, mas primeiro ve se ja terminou.
                historico = await self.historico(prompt_id)
                if prompt_id in historico:
                    return _saidas_do_historico(historico[prompt_id])
                log.warning("WS do ComfyUI caiu (%s); reconectando", e)
                await asyncio.sleep(2)
                continue

    async def _saidas(self, prompt_id: str) -> dict:
        h = await self.historico(prompt_id)
        registro = h.get(prompt_id)
        if not registro:
            raise ComfyErro("o ComfyUI terminou sem deixar historico")
        return _saidas_do_historico(registro)


# ---------------------------------------------------------------------------

def _etapa_do_node(node: str) -> str:
    # O id do node vem do workflow: 1..5 = carga, 6/7 = prompt, 11 = amostra,
    # 13/14 = decode, 15/16 = gravacao.
    try:
        n = int(node)
    except (TypeError, ValueError):
        return "generating"
    if n <= 5:
        return "loading_model"
    if n in (6, 7, 8):
        return "encoding_prompt"
    if n <= 12:
        return "generating"
    if n <= 14:
        return "decoding"
    return "encoding_video"


def _saidas_do_historico(registro: dict) -> dict:
    status = registro.get("status") or {}
    if status.get("status_str") == "error":
        msgs = status.get("messages") or []
        detalhe = ""
        for m in msgs:
            if isinstance(m, list) and len(m) > 1 and m[0] == "execution_error":
                info = m[1] or {}
                detalhe = str(info.get("exception_message") or "")
                node = str(info.get("node_id") or "")
                raise ComfyErro(detalhe or "erro no ComfyUI", node=node)
        raise ComfyErro("erro no ComfyUI", detalhe=detalhe)
    saidas: dict[str, Any] = {}
    for node_id, saida in (registro.get("outputs") or {}).items():
        saidas[node_id] = saida
    return saidas


def texto_curto(d: Any, limite: int = 500) -> str:
    try:
        return json.dumps(d)[:limite]
    except (TypeError, ValueError):
        return str(d)[:limite]


def primeiro_video(saidas: dict) -> dict | None:
    """Acha o arquivo gravado pelo SaveVideo, seja qual for o id do node."""
    for saida in saidas.values():
        for chave in ("videos", "gifs", "images"):
            for item in saida.get(chave) or []:
                if isinstance(item, dict) and item.get("filename"):
                    return item
    return None


def primeiro_audio(saidas: dict) -> dict | None:
    for saida in saidas.values():
        for item in saida.get("audio") or []:
            if isinstance(item, dict) and item.get("filename"):
                return item
    return None
