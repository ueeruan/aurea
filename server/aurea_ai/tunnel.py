"""Tunel: como o servidor do Colab fica alcancavel de fora.

Abstraido de proposito. O provedor padrao e o Cloudflare Quick Tunnel, mas trocar
por outro (ngrok, bore, um dominio proprio) e escrever uma classe com dois
metodos — o resto do servidor nao sabe qual esta em uso.
"""
from __future__ import annotations

import asyncio
import logging
import re
import shutil
from abc import ABC, abstractmethod

from .config import Config

log = logging.getLogger("aurea_ai.tunnel")

# O cloudflared anuncia o endereco no stderr, entre outras linhas.
_RE_TRYCLOUDFLARE = re.compile(r"https://[a-z0-9-]+\.trycloudflare\.com")


class TunelErro(RuntimeError):
    pass


class ProvedorDeTunel(ABC):
    nome = "?"

    @abstractmethod
    async def abrir(self, porta: int) -> str:
        """Sobe o tunel e devolve o endereco publico https."""

    @abstractmethod
    async def fechar(self) -> None:
        ...


class TunelManual(ProvedorDeTunel):
    """Sem tunel: usa `AUREA_PUBLIC_URL`. Serve para rodar local, atras de um
    proxy proprio ou quando o operador ja tem um endereco fixo."""

    nome = "manual"

    def __init__(self, url: str) -> None:
        self.url = url.rstrip("/")

    async def abrir(self, porta: int) -> str:
        if not self.url.startswith("https://"):
            raise TunelErro("AUREA_PUBLIC_URL precisa comecar com https://")
        return self.url

    async def fechar(self) -> None:
        return None


class QuickTunnelCloudflare(ProvedorDeTunel):
    """`cloudflared tunnel --url` — endereco aleatorio, sem conta, sem cartao.

    O endereco muda a cada reinicio. E exatamente por isso que o app nao guarda
    endereco: ele le o que este tunel publicar no discovery.
    """

    nome = "cloudflare-quick"

    def __init__(self, binario: str = "cloudflared") -> None:
        self.binario = binario
        self._proc: asyncio.subprocess.Process | None = None

    @staticmethod
    def disponivel(binario: str = "cloudflared") -> bool:
        return shutil.which(binario) is not None

    async def abrir(self, porta: int) -> str:
        if not self.disponivel(self.binario):
            raise TunelErro(f"'{self.binario}' nao esta no PATH")

        self._proc = await asyncio.create_subprocess_exec(
            self.binario, "tunnel", "--url", f"http://127.0.0.1:{porta}",
            "--no-autoupdate", "--loglevel", "info",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)

        limite = asyncio.get_event_loop().time() + 60
        while asyncio.get_event_loop().time() < limite:
            if self._proc.returncode is not None:
                raise TunelErro(f"cloudflared saiu com codigo {self._proc.returncode}")
            assert self._proc.stdout is not None
            try:
                linha = await asyncio.wait_for(self._proc.stdout.readline(), timeout=5)
            except asyncio.TimeoutError:
                continue
            if not linha:
                await asyncio.sleep(0.2)
                continue
            texto = linha.decode("utf-8", "replace")
            achado = _RE_TRYCLOUDFLARE.search(texto)
            if achado:
                url = achado.group(0).rstrip("/")
                log.info("tunel aberto: %s", url)
                return url

        await self.fechar()
        raise TunelErro("cloudflared nao anunciou um endereco em 60 s")

    async def fechar(self) -> None:
        if self._proc and self._proc.returncode is None:
            self._proc.terminate()
            try:
                await asyncio.wait_for(self._proc.wait(), timeout=10)
            except asyncio.TimeoutError:
                self._proc.kill()
        self._proc = None


def escolher(cfg: Config) -> ProvedorDeTunel:
    """AUREA_TUNNEL=cloudflare|manual|nenhum."""
    import os
    qual = (os.environ.get("AUREA_TUNNEL") or "").strip().lower()
    if not qual:
        qual = "manual" if cfg.public_url else "cloudflare"
    if qual == "nenhum":
        return TunelManual(cfg.public_url or "https://127.0.0.1")
    if qual == "manual":
        if not cfg.public_url:
            raise TunelErro("AUREA_TUNNEL=manual exige AUREA_PUBLIC_URL")
        return TunelManual(cfg.public_url)
    if qual == "cloudflare":
        return QuickTunnelCloudflare()
    raise TunelErro(f"provedor de tunel desconhecido: {qual}")
