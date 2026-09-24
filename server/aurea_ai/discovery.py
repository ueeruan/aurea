"""Publica o documento de discovery e mantem o heartbeat.

O app nao guarda endereco nenhum: ele le este documento. O endereco do tunel
muda a cada reinicio do Colab e o app descobre sozinho.

Escrita autenticada (token do GitHub, so no Colab). Leitura publica.
"""
from __future__ import annotations

import asyncio
import base64
import json
import logging
import time

import aiohttp

from .config import Config
from .contract import DiscoveryDoc

log = logging.getLogger("aurea_ai.discovery")

API = "https://api.github.com"


class DiscoveryErro(RuntimeError):
    pass


class Publicador:
    """Escreve `discovery/aurea-h3.json` no repositorio via API do GitHub."""

    def __init__(self, cfg: Config, sessao: aiohttp.ClientSession | None = None) -> None:
        self.cfg = cfg
        self._sessao = sessao
        self._sha: str | None = None
        self._propria = sessao is None

    async def sessao(self) -> aiohttp.ClientSession:
        if self._sessao is None or self._sessao.closed:
            self._sessao = aiohttp.ClientSession(
                timeout=aiohttp.ClientTimeout(total=20),
                headers={
                    "Authorization": f"Bearer {self.cfg.discovery_token}",
                    "Accept": "application/vnd.github+json",
                    "X-GitHub-Api-Version": "2022-11-28",
                    "User-Agent": "aurea-ai-discovery",
                },
            )
        return self._sessao

    async def fechar(self) -> None:
        if self._propria and self._sessao and not self._sessao.closed:
            await self._sessao.close()

    def _url(self) -> str:
        return (f"{API}/repos/{self.cfg.discovery_repo}/contents/"
                f"{self.cfg.discovery_path}")

    def configurado(self) -> bool:
        return bool(self.cfg.discovery_repo and self.cfg.discovery_token)

    async def _sha_atual(self) -> str | None:
        if self._sha is not None:
            return self._sha
        s = await self.sessao()
        params = {"ref": self.cfg.discovery_branch}
        async with s.get(self._url(), params=params) as r:
            if r.status == 404:
                return None
            if r.status != 200:
                raise DiscoveryErro(f"GET do documento devolveu {r.status}: {(await r.text())[:300]}")
            d = await r.json()
        self._sha = d.get("sha")
        return self._sha

    async def publicar(self, doc: DiscoveryDoc) -> None:
        if not self.configurado():
            raise DiscoveryErro("discovery nao configurado (AUREA_DISCOVERY_REPO / _TOKEN)")

        corpo = json.dumps(doc.como_dict(), indent=2, sort_keys=True)
        conteudo = base64.b64encode(corpo.encode("utf-8")).decode("ascii")

        for tentativa in (1, 2):
            sha = await self._sha_atual()
            payload: dict = {
                "message": f"aurea-ai: heartbeat {doc.endpoint}",
                "content": conteudo,
                "branch": self.cfg.discovery_branch,
            }
            if sha:
                payload["sha"] = sha

            s = await self.sessao()
            async with s.put(self._url(), json=payload) as r:
                if r.status in (200, 201):
                    d = await r.json()
                    self._sha = (d.get("content") or {}).get("sha") or self._sha
                    return
                texto = (await r.text())[:300]
                # 409 = alguem escreveu entre o GET e o PUT: refaz com o sha novo.
                if r.status == 409 and tentativa == 1:
                    self._sha = None
                    continue
                raise DiscoveryErro(f"PUT do documento devolveu {r.status}: {texto}")


class Batedor:
    """Reescreve o documento a cada N segundos enquanto o servidor estiver de pe."""

    def __init__(self, cfg: Config, endpoint: str, gpu: str, capacidades: list[str]) -> None:
        self.cfg = cfg
        self.endpoint = endpoint
        self.gpu = gpu
        self.capacidades = capacidades
        self.publicador = Publicador(cfg)
        self._tarefa: asyncio.Task | None = None
        self._parar = False
        self.ultimo_erro: str = ""
        self.batidas = 0

    def documento(self, online: bool) -> DiscoveryDoc:
        """O que vai para o repositorio — o app le exatamente isto."""
        return DiscoveryDoc(
            endpoint=self.endpoint, online=online, gpu=self.gpu,
            capabilities=self.capacidades, updatedAt=int(time.time()),
            appToken=self.cfg.app_token)

    def iniciar(self) -> None:
        if not self.publicador.configurado():
            log.warning("discovery nao configurado: o app nao vai encontrar este servidor sozinho")
            return
        if not self.cfg.app_token:
            log.warning("AUREA_SERVER_TOKENS vazio: o app vai achar o servidor e nao vai conseguir autenticar")
        self._tarefa = asyncio.create_task(self._laco(), name="heartbeat")

    async def parar(self) -> None:
        self._parar = True
        if self._tarefa:
            self._tarefa.cancel()
            await asyncio.gather(self._tarefa, return_exceptions=True)
            self._tarefa = None
        await self.publicador.fechar()

    async def publicar_offline(self) -> None:
        """Marca o servidor como fora do ar antes de desligar."""
        if not self.publicador.configurado():
            return
        try:
            await self.publicador.publicar(self.documento(online=False))
        except Exception as e:  # noqa: BLE001
            log.warning("nao consegui marcar offline: %s", e)

    async def _laco(self) -> None:
        intervalo = max(10, self.cfg.heartbeat_seconds)
        while not self._parar:
            try:
                await self.publicador.publicar(self.documento(online=True))
                self.batidas += 1
                self.ultimo_erro = ""
                log.info("discovery publicado (%d): %s", self.batidas, self.endpoint)
            except asyncio.CancelledError:
                return
            except Exception as e:  # noqa: BLE001
                self.ultimo_erro = str(e)[:200]
                log.warning("falha ao publicar discovery: %s", e)
            try:
                await asyncio.sleep(intervalo)
            except asyncio.CancelledError:
                return
