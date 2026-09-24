"""Fila de jobs e o executor.

`MAX_GPU_JOBS = 1` por padrao: uma A100 roda um H3 por vez. O resto espera com
`queuePosition` visivel no app.

Cada job tem um conjunto de assinantes (WebSocket). Quem publica evento nao
espera ninguem: se um assinante sumiu, o evento vai para o vazio.
"""
from __future__ import annotations

import asyncio
import logging
import shutil
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path

from .comfy import Comfy, ComfyErro, Progresso, primeiro_audio, primeiro_video
from .config import Config
from .contract import (GenerationRequest, Resultado, StatusJob)
from .workflows import Biblioteca, Workflow

log = logging.getLogger("aurea_ai.jobs")


@dataclass
class Job:
    id: str
    dono: str
    req: GenerationRequest
    criado: float = field(default_factory=time.time)
    iniciado: float | None = None
    terminado: float | None = None
    status: StatusJob = StatusJob.queued
    progresso: float = 0.0
    etapa: str = ""
    etapa_num: int = 0
    passo: int = 0
    passos: int = 0
    erro: str | None = None
    resultado: Resultado | None = None
    comfy_prompt_id: str | None = None
    cancelar: bool = False
    video: Path | None = None
    thumb: Path | None = None
    inscritos: set[asyncio.Queue] = field(default_factory=set)

    # -- publicacao -------------------------------------------------------
    def publicar(self, evento: dict) -> None:
        for q in list(self.inscritos):
            try:
                q.put_nowait(evento)
            except asyncio.QueueFull:
                # Assinante lento: derruba o mais antigo em vez de travar o job.
                try:
                    q.get_nowait()
                    q.put_nowait(evento)
                except (asyncio.QueueEmpty, asyncio.QueueFull):
                    pass

    def evento_estado(self) -> dict:
        return {"type": "status", "status": self.status.value,
                "queuePosition": 0, "progress": self.progresso, "stage": self.etapa}

    # -- leitura ----------------------------------------------------------
    def elapsed(self) -> float:
        fim = self.terminado or time.time()
        return max(0.0, fim - self.criado)

    def vencido(self, ttl_s: int) -> bool:
        return self.terminado is not None and (time.time() - self.terminado) > ttl_s


class Fila:
    def __init__(self, cfg: Config, comfy: Comfy, biblioteca: Biblioteca) -> None:
        self.cfg = cfg
        self.comfy = comfy
        self.biblioteca = biblioteca
        self.jobs: dict[str, Job] = {}
        self.pendentes: asyncio.Queue[str] = asyncio.Queue()
        self._trabalhadores: list[asyncio.Task] = []
        self._limpador: asyncio.Task | None = None
        self._rodando = False

    # -- ciclo de vida ----------------------------------------------------
    async def iniciar(self) -> None:
        if self._rodando:
            return
        self._rodando = True
        for i in range(self.cfg.max_gpu_jobs):
            self._trabalhadores.append(asyncio.create_task(self._trabalhador(i), name=f"gpu-{i}"))
        self._limpador = asyncio.create_task(self._limpeza(), name="limpeza")

    async def parar(self) -> None:
        self._rodando = False
        for t in self._trabalhadores:
            t.cancel()
        if self._limpador:
            self._limpador.cancel()
        await asyncio.gather(*self._trabalhadores, self._limpador, return_exceptions=True)
        self._trabalhadores.clear()
        self._limpador = None

    # -- API --------------------------------------------------------------
    def enfileirar(self, dono: str, req: GenerationRequest) -> Job:
        job = Job(id=str(uuid.uuid4()), dono=dono, req=req)
        self.jobs[job.id] = job
        self.pendentes.put_nowait(job.id)
        return job

    def posicao(self, job: Job) -> int:
        if job.status not in (StatusJob.queued,):
            return 0
        fila = list(self.pendentes._queue)  # noqa: SLF001 — so leitura, para exibir
        try:
            return fila.index(job.id)
        except ValueError:
            return 0

    def do_dono(self, job_id: str, dono: str, admin: bool) -> Job | None:
        job = self.jobs.get(job_id)
        if job is None:
            return None
        if admin or job.dono == dono:
            return job
        return None

    def cancelar(self, job_id: str, dono: str, admin: bool) -> Job | None:
        job = self.do_dono(job_id, dono, admin)
        if job is None:
            return None
        if job.status in (StatusJob.completed, StatusJob.failed, StatusJob.cancelled):
            return job
        job.cancelar = True
        job.status = StatusJob.cancelled
        job.terminado = time.time()
        job.etapa = "Cancelado"
        job.publicar({"type": "cancelled"})
        return job

    def usados_por(self, dono: str) -> int:
        return sum(1 for j in self.jobs.values()
                   if j.dono == dono and j.status not in
                   (StatusJob.completed, StatusJob.failed, StatusJob.cancelled))

    # -- trabalhador ------------------------------------------------------
    async def _trabalhador(self, indice: int) -> None:
        log.info("trabalhador GPU %d pronto", indice)
        while self._rodando:
            try:
                job_id = await self.pendentes.get()
            except asyncio.CancelledError:
                return
            job = self.jobs.get(job_id)
            if job is None or job.cancelar:
                continue
            try:
                await self._executar(job)
            except asyncio.CancelledError:
                raise
            except Exception as e:  # noqa: BLE001 — o job nao pode derrubar a fila
                log.exception("job %s explodiu", job.id)
                self._falhar(job, "internal_error", str(e)[:300])

    async def _executar(self, job: Job) -> None:
        job.iniciado = time.time()
        job.status = StatusJob.loading_model
        job.etapa = "Carregando modelo"
        job.publicar(job.evento_estado())

        try:
            grafo = await self._preparar(job)
        except ComfyErro as e:
            self._falhar(job, "workflow_invalid", e.detalhe or str(e))
            return
        except Exception as e:  # noqa: BLE001
            self._falhar(job, "prepare_failed", str(e)[:300])
            return

        if job.cancelar:
            return

        try:
            job.comfy_prompt_id = await self.comfy.enviar(grafo)
        except ComfyErro as e:
            self._falhar(job, "comfy_rejected", e.detalhe or str(e), node=e.node)
            return
        except Exception as e:  # noqa: BLE001
            self._falhar(job, "comfy_offline", str(e)[:300])
            return

        job.status = StatusJob.generating
        job.etapa = "Gerando"
        job.publicar(job.evento_estado())

        def ao_progresso(p: Progresso, etapa: str) -> None:
            if job.cancelar:
                return
            job.etapa_num = max(job.etapa_num, _indice_etapa(etapa))
            job.etapa = _rotulo(etapa)
            job.passo, job.passos = p.step, p.total
            job.status = StatusJob(etapa) if etapa in StatusJob._value2member_map_ else job.status
            # A geracao e a etapa mais longa: 5% no inicio, 95% no fim do job.
            faixa = _faixa(etapa)
            dentro = p.fracao if etapa == "generating" else 0.5
            job.progresso = max(job.progresso, faixa[0] + (faixa[1] - faixa[0]) * dentro)
            job.publicar({
                "type": "progress", "progress": round(job.progresso, 4),
                "step": p.step, "totalSteps": p.total, "stage": job.etapa,
            })

        try:
            saidas = await self.comfy.acompanhar(
                job.comfy_prompt_id, ao_progresso, lambda: job.cancelar, self.cfg.job_timeout_s)
        except asyncio.CancelledError:
            job.status = StatusJob.cancelled
            job.etapa = "Cancelado"
            job.terminado = time.time()
            job.publicar({"type": "cancelled"})
            return
        except TimeoutError:
            self._falhar(job, "timeout", "o job passou do tempo maximo")
            return
        except ComfyErro as e:
            self._falhar(job, "generation_failed", (e.detalhe or str(e))[:300], node=e.node)
            return
        except Exception as e:  # noqa: BLE001
            self._falhar(job, "generation_failed", str(e)[:300])
            return

        if job.cancelar:
            return

        job.status = StatusJob.encoding_video
        job.etapa = "Preparando o arquivo"
        job.progresso = 0.97
        job.publicar(job.evento_estado())

        try:
            await self._recolher(job, saidas)
        except Exception as e:  # noqa: BLE001
            log.exception("falha ao recolher o resultado do job %s", job.id)
            self._falhar(job, "result_missing", str(e)[:300])
            return

        job.progresso = 1.0
        job.status = StatusJob.completed
        job.etapa = "Concluido"
        job.terminado = time.time()
        job.publicar({"type": "completed", "result": job.resultado.model_dump()})

    # -- montagem ---------------------------------------------------------
    async def _preparar(self, job: Job) -> dict:
        wf: Workflow = self.biblioteca.do_modo(job.req.mode)
        largura, altura = job.req.dimensoes()
        frames = int(round(job.req.duration * job.req.fps))

        valores: dict = {
            "prompt": job.req.prompt,
            "negative": job.req.negativePrompt or "",
            "seed": job.req.seed if job.req.seed >= 0 else _semente(),
            "largura": largura,
            "altura": altura,
            "frames": frames,
            "fps": job.req.fps,
            "steps": 8 if job.req.turbo else 30,
            "cfg": 1.0 if job.req.turbo else 4.0,
            "turbo": 1.0 if job.req.turbo else 0.0,
            "prefixo": f"aurea/{job.id}",
        }

        if job.req.mode == "image_to_video":
            origem = Path(self.cfg.upload_dir) / f"{job.req.imageAssetId}.bin"
            if not origem.is_file():
                raise ComfyErro("imagem de partida nao encontrada",
                                detalhe=job.req.imageAssetId or "")
            nome = await self.comfy.subir_imagem(f"{job.req.imageAssetId}.png", origem.read_bytes())
            valores["imagem"] = nome

        return wf.montar(valores)

    async def _recolher(self, job: Job, saidas: dict) -> None:
        item = primeiro_video(saidas)
        if item is None:
            raise ComfyErro("o ComfyUI nao gravou video")

        dados = await self.comfy.baixar(
            item["filename"], item.get("subfolder") or "", item.get("type") or "output")

        destino = Path(self.cfg.upload_dir) / "saidas" / f"{job.id}.mp4"
        destino.parent.mkdir(parents=True, exist_ok=True)
        destino.write_bytes(dados)
        job.video = destino

        # Miniatura: o preview do proprio ComfyUI, se veio; senao, ffmpeg.
        job.thumb = await self._miniatura(job, saidas, destino)

        largura, altura = job.req.dimensoes()
        job.resultado = Resultado(
            videoUrl=f"/api/v1/generations/{job.id}/video",
            thumbnailUrl=f"/api/v1/generations/{job.id}/thumbnail" if job.thumb else "",
            duration=float(job.req.duration),
            width=largura,
            height=altura,
            fps=job.req.fps,
            hasAudio=job.req.audio and primeiro_audio(saidas) is not None,
        )

    async def _miniatura(self, job: Job, saidas: dict, video: Path) -> Path | None:
        destino = video.with_suffix(".jpg")

        preview = primeiro_video(saidas)
        if preview and preview.get("filename", "").lower().endswith((".jpg", ".png", ".webp")):
            try:
                dados = await self.comfy.baixar(preview["filename"],
                                                preview.get("subfolder") or "",
                                                preview.get("type") or "output")
                destino.write_bytes(dados)
                return destino
            except Exception:  # noqa: BLE001
                pass

        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            log.warning("sem ffmpeg: job %s fica sem miniatura", job.id)
            return None
        try:
            proc = await asyncio.create_subprocess_exec(
                ffmpeg, "-y", "-v", "error", "-i", str(video),
                "-frames:v", "1", "-vf", "scale=640:-2", str(destino),
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
            await asyncio.wait_for(proc.wait(), timeout=30)
        except (asyncio.TimeoutError, OSError) as e:
            log.warning("ffmpeg falhou no job %s: %s", job.id, e)
            return None
        return destino if destino.is_file() and destino.stat().st_size else None

    # -- auxiliares -------------------------------------------------------
    def _falhar(self, job: Job, codigo: str, detalhe: str, node: str = "") -> None:
        if job.cancelar or job.status == StatusJob.cancelled:
            return
        job.status = StatusJob.failed
        job.erro = codigo
        job.etapa = "Falhou"
        job.terminado = time.time()
        if node:
            log.error("job %s falhou em %s: %s (%s)", job.id, codigo, detalhe, node)
        else:
            log.error("job %s falhou em %s: %s", job.id, codigo, detalhe)
        job.publicar({"type": "failed", "error": codigo, "detail": detalhe[:300]})

    async def _limpeza(self) -> None:
        while self._rodando:
            await asyncio.sleep(60)
            for job_id, job in list(self.jobs.items()):
                if not job.vencido(self.cfg.job_ttl_s):
                    continue
                for p in (job.video, job.thumb):
                    if p and p.is_file():
                        p.unlink(missing_ok=True)
                self.jobs.pop(job_id, None)


# ---------------------------------------------------------------------------

def _semente() -> int:
    return int.from_bytes(uuid.uuid4().bytes[:4], "big") & 0x7FFFFFFF


def _indice_etapa(etapa: str) -> int:
    ordem = ["loading_model", "encoding_prompt", "generating", "decoding", "encoding_video"]
    return ordem.index(etapa) if etapa in ordem else 0


def _rotulo(etapa: str) -> str:
    return {
        "queued": "Na fila",
        "loading_model": "Carregando modelo",
        "encoding_prompt": "Lendo o prompt",
        "generating": "Gerando",
        "decoding": "Decodificando",
        "encoding_video": "Gravando o video",
    }.get(etapa, etapa)


def _faixa(etapa: str) -> tuple[float, float]:
    """Fatia da barra de progresso de cada etapa."""
    return {
        "loading_model": (0.00, 0.15),
        "encoding_prompt": (0.15, 0.20),
        "generating": (0.20, 0.90),
        "decoding": (0.90, 0.95),
        "encoding_video": (0.95, 1.00),
    }.get(etapa, (0.0, 1.0))
