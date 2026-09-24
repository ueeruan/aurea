"""Aureia AI API — a superficie que o app conhece.

O ComfyUI fica atras. O app nunca fala com ele, nunca ve o Colab, nunca ve um
caminho de arquivo.
"""
from __future__ import annotations

import asyncio
import json
import logging
import time
import uuid
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import (Depends, FastAPI, File, HTTPException, Request, UploadFile,
                     WebSocket, WebSocketDisconnect, status)
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse

from . import __version__
from .auth import (Papel, exige_admin, exige_token, limita, limite)
from .comfy import Comfy
from .config import Config, config
from .contract import (ASPECTOS, DURACOES, FPS, RESOLUCOES, AssetResponse,
                       CancelResponse, CapabilitiesResponse, Erro, GenerateResponse,
                       GenerationRequest, Health, JobResponse, Resultado,
                       StatusJob)
from .discovery import Batedor
from .jobs import Fila
from .models import conferir, exigidos_pela_biblioteca, faltando
from .tunnel import TunelErro, escolher
from .workflows import Biblioteca, WorkflowInvalido

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("aurea_ai")

# Starlette renomeou estas tres em 2025. Os dois nomes convivem para o servidor
# subir em qualquer versao da faixa suportada.
S_413 = getattr(status, "HTTP_413_CONTENT_TOO_LARGE", 413)
S_415 = getattr(status, "HTTP_415_UNSUPPORTED_MEDIA_TYPE", 415)
S_422 = getattr(status, "HTTP_422_UNPROCESSABLE_CONTENT", 422)

TIPOS_ACEITOS = {"image/png": ".png", "image/jpeg": ".jpg", "image/webp": ".webp"}


# ---------------------------------------------------------------------------
# Estado do processo
# ---------------------------------------------------------------------------

class Estado:
    def __init__(self) -> None:
        self.cfg: Config = config()
        self.biblioteca = Biblioteca(Path(self.cfg.workflows_dir))
        self.comfy: Comfy | None = None
        self.fila: Fila | None = None
        self.batedor: Batedor | None = None
        self.tunel = None
        self.endpoint = ""
        self.gpu = ""
        self.vram_mb = 0
        self._stats_cache: tuple[float, dict] = (0.0, {})
        self.pronto = False
        self.motivo = "iniciando"
        self.faltando_modelos: list[str] = []


estado = Estado()


@asynccontextmanager
async def lifespan(app: FastAPI):
    cfg = estado.cfg

    if cfg.mode == "producao":
        falta = cfg.exigir_tokens()
        if falta:
            raise RuntimeError(f"modo producao sem {', '.join(falta)}")
    elif cfg.exigir_tokens():
        log.warning("modo dev: %s nao configurado — qualquer token sera aceito",
                    ", ".join(cfg.exigir_tokens()))

    Path(cfg.upload_dir).mkdir(parents=True, exist_ok=True)

    try:
        estado.biblioteca.carregar()
        log.info("workflows: %s", ", ".join(estado.biblioteca.modos()))
    except WorkflowInvalido as e:
        estado.motivo = f"workflow invalido: {e}"
        log.error("%s", estado.motivo)

    estado.comfy = Comfy(cfg.comfy_url, cfg.comfy_timeout_s)
    estado.fila = Fila(cfg, estado.comfy, estado.biblioteca)
    await estado.fila.iniciar()

    asyncio.create_task(_vigiar())

    yield

    if estado.batedor:
        await estado.batedor.publicar_offline()
        await estado.batedor.parar()
    if estado.fila:
        await estado.fila.parar()
    if estado.comfy:
        await estado.comfy.fechar()
    if estado.tunel:
        await estado.tunel.fechar()


async def _vigiar() -> None:
    """Descobre o GPU, abre o tunel e comeca o heartbeat. Nada disso derruba o
    servidor: sem tunel ele ainda responde em 127.0.0.1."""
    cfg = estado.cfg
    try:
        stats = await estado.comfy.estatisticas()
        for d in stats.get("devices", []):
            estado.gpu = d.get("name") or estado.gpu
            estado.vram_mb = int((d.get("vram_total") or 0) // (1024 * 1024))
    except Exception as e:  # noqa: BLE001
        log.warning("ComfyUI ainda nao respondeu: %s", e)

    if cfg.public_url and not cfg.discovery_repo:
        estado.endpoint = cfg.public_url
    else:
        try:
            estado.tunel = escolher(cfg)
            estado.endpoint = await estado.tunel.abrir(8000)
        except TunelErro as e:
            estado.motivo = f"sem tunel: {e}"
            log.error("%s", estado.motivo)

    if estado.endpoint and estado.biblioteca.modos():
        estado.batedor = Batedor(cfg, estado.endpoint, estado.gpu,
                                 ["text_to_video", "image_to_video", "audio"])
        estado.batedor.iniciar()

    # Sem os pesos no disco o servidor responde, mas nao aceita gerar: melhor
    # recusar na hora do que enfileirar um job que vai falhar em 10 minutos.
    try:
        exigidos = exigidos_pela_biblioteca(estado.biblioteca)
        estado.faltando_modelos = faltando(conferir(Path(cfg.models_dir), exigidos))
        if estado.faltando_modelos:
            estado.motivo = f"faltam pesos: {', '.join(estado.faltando_modelos)}"
            log.error("%s", estado.motivo)
    except Exception as e:  # noqa: BLE001
        log.warning("nao consegui conferir os pesos: %s", e)

    estado.pronto = bool(estado.biblioteca.modos()) and not estado.faltando_modelos
    if estado.pronto:
        estado.motivo = "pronto"
    log.info("Aurea AI no ar em %s (gpu=%s)", estado.endpoint or "local", estado.gpu or "?")


# ---------------------------------------------------------------------------
# App
# ---------------------------------------------------------------------------

app = FastAPI(title="Aurea AI API", version=__version__, lifespan=lifespan,
              docs_url="/api/v1/docs", openapi_url="/api/v1/openapi.json")

_cfg_cors = config().cors_origins.strip()
if _cfg_cors:
    # Em producao, lista fechada. Sem a variavel, nenhuma origem de navegador —
    # o app e nativo e nao manda Origin.
    app.add_middleware(CORSMiddleware, allow_origins=[o.strip() for o in _cfg_cors.split(",")],
                       allow_methods=["GET", "POST", "DELETE"], allow_headers=["Authorization",
                                                                               "Content-Type"],
                       allow_credentials=False, max_age=600)


@app.exception_handler(HTTPException)
async def _erro_http(request: Request, exc: HTTPException) -> JSONResponse:
    return JSONResponse(status_code=exc.status_code,
                        content={"error": _codigo(exc.status_code), "detail": str(exc.detail)},
                        headers=getattr(exc, "headers", None))


@app.exception_handler(RequestValidationError)
async def _erro_validacao(request: Request, exc: RequestValidationError) -> JSONResponse:
    """Traduz a validacao do FastAPI para o formato do contrato.

    O corpo cru do FastAPI tem `detail` como lista de dicionarios — o app teria
    que entender a estrutura interna do framework. Aqui vira uma frase.
    """
    return JSONResponse(status_code=422,
                        content={"error": "invalid_request", "detail": _frase(exc)})


def _frase(exc: RequestValidationError) -> str:
    for erro in exc.errors():
        campo = ".".join(str(p) for p in erro.get("loc", ()) if p not in ("body", "query"))
        msg = erro.get("msg") or "valor invalido"
        msg = msg.replace("Value error, ", "")
        return f"{campo}: {msg}" if campo else msg
    return "pedido invalido"


@app.exception_handler(Exception)
async def _erro_500(request: Request, exc: Exception) -> JSONResponse:
    log.exception("erro nao tratado em %s", request.url.path)
    return JSONResponse(status_code=500,
                        content={"error": "internal_error", "detail": "erro interno"})


def _codigo(http: int) -> str:
    return {400: "bad_request", 401: "unauthorized", 403: "forbidden",
            404: "not_found", 409: "conflict", 413: "too_large",
            415: "unsupported_type", 422: "invalid_request",
            429: "rate_limited"}.get(http, "error")


# ---------------------------------------------------------------------------
# Descoberta do servidor
# ---------------------------------------------------------------------------

@app.get("/api/v1/health", response_model=Health)
async def health() -> Health:
    """Publico de proposito: o app precisa conferir o servidor antes de ter
    qualquer token. Nao devolve nada sensivel."""
    cfg = estado.cfg
    vivo = await estado.comfy.vivo()
    fila_len = estado.fila.pendentes.qsize() if estado.fila else 0
    return Health(
        status="ok" if (vivo and estado.pronto) else "degraded",
        service=cfg.service, engine=cfg.engine, version=cfg.version,
        gpu=estado.gpu, vram_total_mb=estado.vram_mb,
        ready=bool(vivo and estado.pronto),
        queue=fila_len,
        missingModels=estado.faltando_modelos,
    )


@app.get("/api/v1/capabilities", response_model=CapabilitiesResponse)
async def capabilities(quem: tuple[str, str] = Depends(exige_token)) -> CapabilitiesResponse:
    cfg = estado.cfg
    return CapabilitiesResponse(
        engine=cfg.engine,
        modes=estado.biblioteca.modos() or ["text_to_video", "image_to_video"],
        durations=list(DURACOES), aspectRatios=list(ASPECTOS),
        resolutions=list(RESOLUCOES), fps=list(FPS),
        audio=any("AudioDecode" in " ".join(w.class_names()) for w in estado.biblioteca.todos()),
        maxConcurrentJobs=cfg.max_gpu_jobs,
        queueLength=(estado.fila.pendentes.qsize() if estado.fila else 0),
    )


# ---------------------------------------------------------------------------
# Geracoes
# ---------------------------------------------------------------------------

@app.post("/api/v1/generations", response_model=GenerateResponse,
          status_code=status.HTTP_202_ACCEPTED)
async def gerar(req: GenerationRequest, quem: tuple[str, str] = Depends(limita)) -> GenerateResponse:
    cfg, fila = estado.cfg, estado.fila
    if not fila or not estado.pronto:
        raise HTTPException(status.HTTP_503_SERVICE_UNAVAILABLE,
                            detail=estado.motivo or "servidor nao esta pronto")
    if not await estado.comfy.vivo():
        raise HTTPException(status.HTTP_503_SERVICE_UNAVAILABLE, detail="o motor de geracao nao responde")
    if req.mode not in estado.biblioteca.modos():
        raise HTTPException(S_422,
                            detail=f"modo '{req.mode}' indisponivel")
    if fila.pendentes.qsize() >= cfg.max_queue:
        raise HTTPException(status.HTTP_429_TOO_MANY_REQUESTS,
                            detail="fila cheia; tente daqui a pouco",
                            headers={"Retry-After": "30"})
    if fila.usados_por(quem[1]) >= 3:
        raise HTTPException(status.HTTP_429_TOO_MANY_REQUESTS,
                            detail="voce ja tem 3 jobs em andamento",
                            headers={"Retry-After": "30"})

    if req.mode == "image_to_video":
        origem = Path(cfg.upload_dir) / f"{req.imageAssetId}.bin"
        if not origem.is_file():
            raise HTTPException(status.HTTP_404_NOT_FOUND, detail="imagem de partida desconhecida")

    job = fila.enfileirar(quem[1], req)
    log.info("job %s enfileirado por %s (%s, %ds, %s)",
             job.id, quem[0], req.mode, req.duration, req.resolution)
    return GenerateResponse(jobId=job.id, status=job.status, queuePosition=fila.posicao(job))


@app.get("/api/v1/generations", response_model=list[JobResponse])
async def historico(quem: tuple[str, str] = Depends(exige_token)) -> list[JobResponse]:
    fila = estado.fila
    if not fila:
        return []
    admin = quem[0] == Papel.admin
    meus = [j for j in fila.jobs.values() if admin or j.dono == quem[1]]
    meus.sort(key=lambda j: j.criado, reverse=True)
    return [_resposta(j) for j in meus[:50]]


@app.get("/api/v1/generations/{job_id}", response_model=JobResponse)
async def consultar(job_id: str, quem: tuple[str, str] = Depends(exige_token)) -> JobResponse:
    job = _job(job_id, quem)
    return _resposta(job)


@app.delete("/api/v1/generations/{job_id}", response_model=CancelResponse)
async def cancelar(job_id: str, quem: tuple[str, str] = Depends(exige_token)) -> CancelResponse:
    fila = estado.fila
    if not fila:
        raise HTTPException(status.HTTP_404_NOT_FOUND, detail="sem fila")
    admin = quem[0] == Papel.admin
    job = fila.cancelar(job_id, quem[1], admin)
    if job is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, detail="job desconhecido")
    log.info("job %s cancelado por %s", job.id, quem[0])
    return CancelResponse(status=job.status)


@app.get("/api/v1/generations/{job_id}/video")
async def video(job_id: str, quem: tuple[str, str] = Depends(exige_token)) -> FileResponse:
    job = _job(job_id, quem)
    if not job.video or not job.video.is_file():
        raise HTTPException(status.HTTP_404_NOT_FOUND, detail="video ainda nao esta pronto")
    return FileResponse(job.video, media_type="video/mp4",
                        headers={"Cache-Control": "private, max-age=3600"})


@app.get("/api/v1/generations/{job_id}/thumbnail")
async def miniatura(job_id: str, quem: tuple[str, str] = Depends(exige_token)) -> FileResponse:
    job = _job(job_id, quem)
    if not job.thumb or not job.thumb.is_file():
        raise HTTPException(status.HTTP_404_NOT_FOUND, detail="sem miniatura")
    tipo = "image/jpeg" if job.thumb.suffix.lower() in (".jpg", ".jpeg") else "image/png"
    return FileResponse(job.thumb, media_type=tipo,
                        headers={"Cache-Control": "private, max-age=3600"})


@app.websocket("/api/v1/generations/{job_id}/events")
async def eventos(ws: WebSocket, job_id: str) -> None:
    cfg = estado.cfg
    token = _token_ws(ws)
    quem = _autentica_ws(cfg, token)
    if quem is None:
        await ws.close(code=4401)
        return

    fila = estado.fila
    job = fila.do_dono(job_id, quem[1], quem[0] == Papel.admin) if fila else None
    if job is None:
        await ws.close(code=4404)
        return

    await ws.accept()
    assinatura: asyncio.Queue = asyncio.Queue(maxsize=128)
    job.inscritos.add(assinatura)
    try:
        # Estado atual primeiro: o app pode ter conectado no meio do caminho.
        await ws.send_text(json.dumps(_instantaneo(job)))
        while True:
            try:
                evento = await asyncio.wait_for(assinatura.get(), timeout=20)
            except asyncio.TimeoutError:
                # Ping oco para manter viva a conexao atraves do tunel.
                await ws.send_text(json.dumps({"type": "ping"}))
                if job.status in (StatusJob.completed, StatusJob.failed, StatusJob.cancelled):
                    break
                continue
            await ws.send_text(json.dumps(evento))
            if evento.get("type") in ("completed", "failed", "cancelled"):
                break
    except WebSocketDisconnect:
        pass
    except Exception:  # noqa: BLE001
        pass
    finally:
        job.inscritos.discard(assinatura)
        try:
            await ws.close()
        except Exception:  # noqa: BLE001
            pass


def _instantaneo(job) -> dict:
    if job.status == StatusJob.completed and job.resultado:
        return {"type": "completed", "result": job.resultado.model_dump()}
    if job.status == StatusJob.failed:
        return {"type": "failed", "error": job.erro or "generation_failed"}
    if job.status == StatusJob.cancelled:
        return {"type": "cancelled"}
    return {"type": "status", "status": job.status.value,
            "queuePosition": estado.fila.posicao(job) if estado.fila else 0,
            "progress": job.progresso, "stage": job.etapa}


def _token_ws(ws: WebSocket) -> str:
    cabecalho = ws.headers.get("authorization") or ""
    partes = cabecalho.split(None, 1)
    if len(partes) == 2 and partes[0].lower() == "bearer":
        return partes[1].strip()
    # Alguns clientes nao conseguem mandar cabecalho no handshake. Aceito na
    # query, ciente de que a URL pode aparecer em log de proxy.
    return ws.query_params.get("token", "")


def _autentica_ws(cfg: Config, token: str) -> tuple[str, str] | None:
    import hmac
    from .auth import _dono
    if not token:
        return None
    if cfg.admin_token and hmac.compare_digest(token, cfg.admin_token):
        return Papel.admin, "admin"
    for valido in cfg.server_tokens:
        if hmac.compare_digest(token, valido):
            return Papel.cliente, _dono(token)
    if cfg.mode == "dev" and not cfg.server_tokens:
        return Papel.cliente, _dono(token)
    return None


# ---------------------------------------------------------------------------
# Assets
# ---------------------------------------------------------------------------

@app.post("/api/v1/assets", response_model=AssetResponse, status_code=status.HTTP_201_CREATED)
async def subir_asset(arquivo: UploadFile = File(..., alias="file"),
                      quem: tuple[str, str] = Depends(limita)) -> AssetResponse:
    cfg = estado.cfg
    tipo = (arquivo.content_type or "").split(";")[0].strip().lower()
    if tipo not in TIPOS_ACEITOS:
        raise HTTPException(S_415,
                            detail="so PNG, JPEG ou WebP")

    # Le em blocos e corta no limite: `await arquivo.read()` de um arquivo de
    # 2 GB encheria a RAM do Colab.
    pedacos: list[bytes] = []
    total = 0
    while True:
        bloco = await arquivo.read(64 * 1024)
        if not bloco:
            break
        total += len(bloco)
        if total > cfg.max_upload_bytes:
            raise HTTPException(S_413,
                                detail=f"imagem maior que {cfg.max_upload_bytes // (1024 * 1024)} MB")
        pedacos.append(bloco)

    dados = b"".join(pedacos)
    if not dados:
        raise HTTPException(S_422, detail="arquivo vazio")
    if not _parece_imagem(dados, tipo):
        raise HTTPException(S_415,
                            detail="o conteudo nao e a imagem que diz ser")

    # O nome do arquivo do cliente e descartado: vira UUID. Sem isso, um nome
    # com "../" ou com barras viraria caminho.
    asset_id = str(uuid.uuid4())
    destino = (Path(cfg.upload_dir) / f"{asset_id}.bin").resolve()
    if not str(destino).startswith(str(Path(cfg.upload_dir).resolve())):
        raise HTTPException(status.HTTP_400_BAD_REQUEST, detail="destino invalido")
    destino.write_bytes(dados)
    log.info("asset %s (%d bytes, %s) de %s", asset_id, len(dados), tipo, quem[0])
    return AssetResponse(assetId=asset_id)


def _parece_imagem(dados: bytes, tipo: str) -> bool:
    """Confere a assinatura do arquivo. `content_type` vem do cliente e nao vale
    como prova."""
    if tipo == "image/png":
        return dados.startswith(b"\x89PNG\r\n\x1a\n")
    if tipo == "image/jpeg":
        return dados.startswith(b"\xff\xd8\xff")
    if tipo == "image/webp":
        return len(dados) > 12 and dados[:4] == b"RIFF" and dados[8:12] == b"WEBP"
    return False


# ---------------------------------------------------------------------------
# Admin (so no Colab, com AUREA_ADMIN_TOKEN)
# ---------------------------------------------------------------------------

@app.post("/api/v1/admin/queue/reset")
async def admin_reset(quem: str = Depends(exige_admin)) -> dict:
    fila = estado.fila
    if not fila:
        return {"removidos": 0}
    n = 0
    while not fila.pendentes.empty():
        try:
            job_id = fila.pendentes.get_nowait()
        except asyncio.QueueEmpty:
            break
        job = fila.jobs.get(job_id)
        if job and job.status == StatusJob.queued:
            job.status = StatusJob.cancelled
            job.terminado = time.time()
            job.publicar({"type": "cancelled"})
            n += 1
    return {"removidos": n}


@app.get("/api/v1/admin/status")
async def admin_status(quem: str = Depends(exige_admin)) -> dict:
    fila = estado.fila
    return {
        "endpoint": estado.endpoint,
        "gpu": estado.gpu,
        "pronto": estado.pronto,
        "motivo": estado.motivo,
        "faltandoModelos": estado.faltando_modelos,
        "workflows": estado.biblioteca.modos(),
        "jobs": len(fila.jobs) if fila else 0,
        "fila": fila.pendentes.qsize() if fila else 0,
        "heartbeats": estado.batedor.batidas if estado.batedor else 0,
        "heartbeat_erro": estado.batedor.ultimo_erro if estado.batedor else "",
        "tunel": estado.tunel.nome if estado.tunel else "",
        "comfy": estado.cfg.comfy_url,
    }


# ---------------------------------------------------------------------------
# Auxiliares
# ---------------------------------------------------------------------------

def _job(job_id: str, quem: tuple[str, str]):
    fila = estado.fila
    job = fila.do_dono(job_id, quem[1], quem[0] == Papel.admin) if fila else None
    if job is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, detail="job desconhecido")
    return job


def _resposta(job) -> JobResponse:
    return JobResponse(
        jobId=job.id, status=job.status, progress=round(job.progresso, 4),
        stage=job.etapa, queuePosition=estado.fila.posicao(job) if estado.fila else 0,
        elapsedSeconds=round(job.elapsed(), 2), error=job.erro, result=job.resultado)


@app.get("/")
async def raiz() -> dict:
    return {"service": estado.cfg.service, "api": "/api/v1",
            "health": "/api/v1/health"}
