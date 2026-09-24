"""Modelos da API. Espelham docs/ai/CONTRATO.md.

Regra do contrato: o cliente so manda o que esta aqui. Nenhum campo aceita
caminho de arquivo, nome de workflow, id de node ou comando.
"""
from __future__ import annotations

import time
from enum import Enum
from typing import Literal, Optional

from pydantic import BaseModel, Field, field_validator, model_validator

# ---------------------------------------------------------------------------
# Valores aceitos
# ---------------------------------------------------------------------------

# O que o app escolhe mostra na tela. Valor fora daqui = 422, nunca "ajusta e
# segue": adivinhar duracao faria o app mostrar 8 s e gerar 5 s.
DURACOES = (5, 10, 15)
ASPECTOS = ("9:16", "16:9", "1:1", "4:5")
RESOLUCOES = ("preview", "standard", "high")
FPS = (24,)

# Lado menor de cada resolucao por aspecto. H3 quer multiplos de 32.
_LADO = {
    "preview": 512,
    "standard": 768,
    "high": 1024,
}


def dimensoes(aspecto: str, resolucao: str) -> tuple[int, int]:
    """(largura, altura) arredondados para multiplo de 32."""
    base = _LADO[resolucao]
    a, b = (int(x) for x in aspecto.split(":"))
    if a >= b:
        largura = base * a // b
        altura = base
    else:
        largura = base
        altura = base * b // a
    return (largura // 32) * 32, (altura // 32) * 32


# ---------------------------------------------------------------------------
# Erros
# ---------------------------------------------------------------------------

class Erro(BaseModel):
    error: str
    detail: str


# ---------------------------------------------------------------------------
# Health / capabilities
# ---------------------------------------------------------------------------

class Capabilities(BaseModel):
    t2v: bool = True
    i2v: bool = True
    audio: bool = True


class Health(BaseModel):
    status: Literal["ok", "degraded"] = "ok"
    service: str = "aurea-ai"
    engine: str = "minimax-h3"
    version: int = 1
    gpu: str = ""
    vram_total_mb: int = 0
    ready: bool = True
    queue: int = 0
    # Nomes dos pesos que faltam no disco. Vazio = tudo no lugar.
    missingModels: list[str] = []
    capabilities: Capabilities = Capabilities()


class CapabilitiesResponse(BaseModel):
    engine: str = "minimax-h3"
    modes: list[str] = ["text_to_video", "image_to_video"]
    durations: list[int] = list(DURACOES)
    aspectRatios: list[str] = list(ASPECTOS)
    resolutions: list[str] = list(RESOLUCOES)
    fps: list[int] = list(FPS)
    audio: bool = True
    maxConcurrentJobs: int = 1
    queueLength: int = 0


# ---------------------------------------------------------------------------
# Geracao
# ---------------------------------------------------------------------------

class GenerationRequest(BaseModel):
    mode: Literal["text_to_video", "image_to_video"]
    prompt: str = Field(min_length=1, max_length=2000)
    negativePrompt: str = Field(default="", max_length=2000)
    duration: int = 5
    aspectRatio: str = "16:9"
    resolution: str = "standard"
    fps: int = 24
    audio: bool = True
    seed: int = -1
    turbo: bool = True
    imageAssetId: Optional[str] = None

    @field_validator("prompt")
    @classmethod
    def _prompt_util(cls, v: str) -> str:
        v = v.strip()
        if not v:
            raise ValueError("prompt vazio")
        return v

    @field_validator("duration")
    @classmethod
    def _duracao_valida(cls, v: int) -> int:
        if v not in DURACOES:
            raise ValueError(f"duracao {v} nao suportada; use uma de {list(DURACOES)}")
        return v

    @field_validator("aspectRatio")
    @classmethod
    def _aspecto_valido(cls, v: str) -> str:
        if v not in ASPECTOS:
            raise ValueError(f"aspecto {v} nao suportado; use um de {list(ASPECTOS)}")
        return v

    @field_validator("resolution")
    @classmethod
    def _resolucao_valida(cls, v: str) -> str:
        if v not in RESOLUCOES:
            raise ValueError(f"resolucao {v} nao suportada; use uma de {list(RESOLUCOES)}")
        return v

    @field_validator("fps")
    @classmethod
    def _fps_valido(cls, v: int) -> int:
        if v not in FPS:
            raise ValueError(f"fps {v} nao suportado")
        return v

    @field_validator("imageAssetId")
    @classmethod
    def _asset_uuid(cls, v: Optional[str]) -> Optional[str]:
        if v is None:
            return None
        v = v.strip()
        # UUID canonico apenas. Sem barras, sem pontos, sem caminho.
        if len(v) != 36 or v.count("-") != 4:
            raise ValueError("imageAssetId invalido")
        if not all(c in "0123456789abcdef-" for c in v.lower()):
            raise ValueError("imageAssetId invalido")
        return v.lower()

    @model_validator(mode="after")
    def _asset_combina_com_modo(self) -> "GenerationRequest":
        if self.mode == "image_to_video" and not self.imageAssetId:
            raise ValueError("image_to_video exige imageAssetId")
        if self.mode == "text_to_video" and self.imageAssetId:
            raise ValueError("text_to_video nao aceita imageAssetId")
        return self

    def dimensoes(self) -> tuple[int, int]:
        return dimensoes(self.aspectRatio, self.resolution)


class Resultado(BaseModel):
    videoUrl: str
    # Vazio quando nao foi possivel extrair um quadro: o app cai no proprio
    # player em vez de mostrar uma imagem quebrada.
    thumbnailUrl: str
    duration: float
    width: int
    height: int
    fps: int
    hasAudio: bool


class StatusJob(str, Enum):
    queued = "queued"
    loading_model = "loading_model"
    encoding_prompt = "encoding_prompt"
    generating = "generating"
    decoding = "decoding"
    encoding_video = "encoding_video"
    completed = "completed"
    failed = "failed"
    cancelled = "cancelled"


class JobResponse(BaseModel):
    jobId: str
    status: StatusJob
    progress: float = 0.0
    stage: str = ""
    queuePosition: int = 0
    elapsedSeconds: float = 0.0
    error: Optional[str] = None
    result: Optional[Resultado] = None


class GenerateResponse(BaseModel):
    jobId: str
    status: StatusJob
    queuePosition: int = 0


class CancelResponse(BaseModel):
    status: StatusJob


class AssetResponse(BaseModel):
    assetId: str


# ---------------------------------------------------------------------------
# Eventos (WebSocket)
# ---------------------------------------------------------------------------

class EventoProgresso(BaseModel):
    type: Literal["progress"] = "progress"
    progress: float
    step: int
    totalSteps: int
    stage: str


class EventoEstado(BaseModel):
    type: Literal["status"] = "status"
    status: StatusJob
    queuePosition: int = 0


class EventoConcluido(BaseModel):
    type: Literal["completed"] = "completed"
    result: Resultado


class EventoFalha(BaseModel):
    type: Literal["failed"] = "failed"
    error: str


class EventoCancelado(BaseModel):
    type: Literal["cancelled"] = "cancelled"


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

class DiscoveryDoc(BaseModel):
    service: Literal["aurea-h3"] = "aurea-h3"
    version: int = 1
    endpoint: str
    online: bool = True
    gpu: str = ""
    model: str = "MiniMax-H3"
    capabilities: list[str] = ["text_to_video", "image_to_video", "audio"]
    updatedAt: int = Field(default_factory=lambda: int(time.time()))

    def valido(self, janela_s: int = 90) -> bool:
        if self.service != "aurea-h3" or self.version != 1:
            return False
        if not self.endpoint.startswith("https://"):
            return False
        if not self.online:
            return False
        return abs(int(time.time()) - self.updatedAt) <= janela_s

    def como_dict(self) -> dict:
        return self.model_dump()
