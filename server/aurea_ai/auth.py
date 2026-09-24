"""Autenticacao e limite de taxa.

Dois papeis:
  - cliente: o app. Token `AUREA_SERVER_TOKEN`. So mexe nos proprios jobs.
  - admin:   so o Colab. Token `AUREA_ADMIN_TOKEN`. Ve e cancela qualquer job.

Comparacao em tempo constante: `==` de string vaza o tamanho do prefixo certo
pelo tempo de resposta.
"""
from __future__ import annotations

import hmac
import time
from collections import defaultdict, deque

from fastapi import Depends, Header, HTTPException, Request, status

from .config import Config, config


def _igual(a: str, b: str) -> bool:
    return hmac.compare_digest(a.encode("utf-8"), b.encode("utf-8"))


class Papel:
    cliente = "cliente"
    admin = "admin"


def _token_do_header(authorization: str | None) -> str:
    if not authorization:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, detail="token ausente")
    partes = authorization.split(None, 1)
    if len(partes) != 2 or partes[0].lower() != "bearer":
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, detail="Authorization precisa ser 'Bearer <token>'")
    return partes[1].strip()


async def exige_token(
    authorization: str | None = Header(default=None),
    cfg: Config = Depends(config),
) -> tuple[str, str]:
    """Devolve (papel, dono). O dono e o proprio token em hash curto — e o que
    separa o historico de um tester do de outro sem guardar o token."""
    token = _token_do_header(authorization)
    if cfg.admin_token and _igual(token, cfg.admin_token):
        return Papel.admin, "admin"
    for valido in cfg.server_tokens:
        if _igual(token, valido):
            return Papel.cliente, _dono(token)
    if cfg.mode == "dev" and not cfg.server_tokens:
        # Em dev, sem AUREA_SERVER_TOKEN configurado, qualquer token vale.
        return Papel.cliente, _dono(token)
    raise HTTPException(status.HTTP_401_UNAUTHORIZED, detail="token invalido")


async def exige_admin(
    authorization: str | None = Header(default=None),
    cfg: Config = Depends(config),
) -> str:
    token = _token_do_header(authorization)
    if cfg.admin_token and _igual(token, cfg.admin_token):
        return Papel.admin
    raise HTTPException(status.HTTP_403_FORBIDDEN, detail="somente admin")


def _dono(token: str) -> str:
    import hashlib
    return hashlib.sha256(token.encode("utf-8")).hexdigest()[:16]


# ---------------------------------------------------------------------------
# Limite de taxa
# ---------------------------------------------------------------------------

class LimiteTaxa:
    """Janela deslizante por dono. Em memoria: um Colab e um processo."""

    def __init__(self, por_minuto: int) -> None:
        self.por_minuto = por_minuto
        self._vistos: dict[str, deque[float]] = defaultdict(deque)

    def permite(self, chave: str) -> bool:
        agora = time.monotonic()
        janela = self._vistos[chave]
        while janela and agora - janela[0] > 60.0:
            janela.popleft()
        if len(janela) >= self.por_minuto:
            return False
        janela.append(agora)
        return True


_limite: LimiteTaxa | None = None


def limite(cfg: Config | None = None) -> LimiteTaxa:
    global _limite
    if _limite is None:
        _limite = LimiteTaxa((cfg or config()).rate_limit_per_min)
    return _limite


def reset_limite_for_tests(por_minuto: int) -> None:
    global _limite
    _limite = LimiteTaxa(por_minuto)


async def limita(request: Request, quem: tuple[str, str] = Depends(exige_token)) -> tuple[str, str]:
    """Dependencia para as rotas caras (gerar, subir asset)."""
    if not limite().permite(quem[1]):
        raise HTTPException(
            status.HTTP_429_TOO_MANY_REQUESTS,
            detail="muitas requisicoes; espere um minuto",
            headers={"Retry-After": "60"},
        )
    return quem
