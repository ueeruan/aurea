"""Regressions for cancellation at await boundaries; no real GPU or network."""
import asyncio
import importlib.util
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest

from aurea_ai.config import Config
from aurea_ai.contract import GenerationRequest, StatusJob
from aurea_ai.jobs import Fila


def queue(tmp_path):
    comfy = SimpleNamespace(enviar=AsyncMock(return_value="prompt"),
                            acompanhar=AsyncMock(return_value={}))
    fila = Fila(Config(upload_dir=str(tmp_path)), comfy, None)
    fila._preparar = AsyncMock(return_value={})
    return fila


async def test_stop_before_start_and_repeated_stop(tmp_path):
    fila = queue(tmp_path)
    await fila.parar()
    await fila.iniciar()
    await fila.parar()
    await fila.parar()
    assert not fila._trabalhadores and fila._limpador is None


async def test_cancel_during_collection_never_publishes_completed(tmp_path):
    fila = queue(tmp_path)
    job = fila.enfileirar("owner", GenerationRequest(mode="text_to_video", prompt="test"))
    events = asyncio.Queue()
    job.inscritos.add(events)
    reached = asyncio.Event()
    resume = asyncio.Event()
    video = tmp_path / "result.mp4"

    async def collect(job, outputs):
        reached.set()
        await resume.wait()
        video.write_bytes(b"result")
        video.with_suffix(".jpg").write_bytes(b"partial thumbnail")
        job.video = video

    fila._recolher = collect
    task = asyncio.create_task(fila._executar(job))
    await asyncio.wait_for(reached.wait(), 1)
    fila.cancelar(job.id, job.dono, False)
    resume.set()
    await task
    assert job.status == StatusJob.cancelled
    assert job.resultado is None and not video.exists()
    assert not video.with_suffix(".jpg").exists()
    assert all(event["type"] != "completed" for event in list(events._queue))


async def test_cancel_during_submit_preserves_terminal_state(tmp_path):
    fila = queue(tmp_path)
    job = fila.enfileirar("owner", GenerationRequest(mode="text_to_video", prompt="test"))

    async def submit(graph):
        fila.cancelar(job.id, job.dono, False)
        return "prompt"

    async def watch(prompt, progress, cancelled, timeout):
        assert cancelled() and job.status == StatusJob.cancelled
        raise asyncio.CancelledError()

    fila.comfy.enviar = submit
    fila.comfy.acompanhar = watch
    await fila._executar(job)
    assert job.status == StatusJob.cancelled


@pytest.mark.parametrize("phase", ["prepare", "watch", "collect"])
async def test_shutdown_cancels_active_job_at_every_phase(tmp_path, phase):
    fila = queue(tmp_path)
    job = fila.enfileirar("owner", GenerationRequest(mode="text_to_video", prompt="test"))
    reached = asyncio.Event()

    async def blocked(*args):
        reached.set()
        await asyncio.Event().wait()

    setattr(fila.comfy if phase == "watch" else fila,
            {"prepare": "_preparar", "watch": "acompanhar", "collect": "_recolher"}[phase], blocked)
    await fila.iniciar()
    await asyncio.wait_for(reached.wait(), 1)
    await asyncio.wait_for(fila.parar(), 1)
    assert job.status == StatusJob.cancelled
    assert not fila._trabalhadores


@pytest.mark.parametrize("failure", ["timeout", "cancel"])
async def test_thumbnail_reaps_process_on_timeout_or_cancel(tmp_path, monkeypatch, failure):
    fila = queue(tmp_path)
    job = fila.enfileirar("owner", GenerationRequest(mode="text_to_video", prompt="test"))
    reached = asyncio.Event()

    class Process:
        returncode = None
        killed = False
        reaped = False

        async def wait(self):
            if self.killed:
                self.reaped = True
                return self.returncode
            reached.set()
            if failure == "timeout":
                raise TimeoutError()
            await asyncio.Event().wait()

        def kill(self):
            self.killed = True
            self.returncode = -9

    proc = Process()
    monkeypatch.setattr("aurea_ai.jobs.shutil.which", lambda _: "local-ffmpeg")
    monkeypatch.setattr("aurea_ai.jobs.asyncio.create_subprocess_exec", AsyncMock(return_value=proc))
    task = asyncio.create_task(fila._miniatura(job, {}, tmp_path / "video.mp4"))
    await asyncio.wait_for(reached.wait(), 1)
    if failure == "cancel":
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
    else:
        assert await task is None
    assert proc.killed and proc.reaped


@pytest.mark.parametrize("exceptional", [False, True])
async def test_shutdown_joins_startup_watcher_before_closing_resources(tmp_path, monkeypatch, exceptional):
    # Isolated module state: the API integration fixture owns the real module.
    from aurea_ai import app as running_module
    spec = importlib.util.spec_from_file_location("aurea_ai._lifecycle_fixture", running_module.__file__)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.estado.cfg = Config(upload_dir=str(tmp_path))
    module.estado.biblioteca = SimpleNamespace(carregar=lambda: None, modos=lambda: [])
    reached = asyncio.Event()
    stopped = asyncio.Event()

    async def watcher():
        reached.set()
        try:
            await asyncio.Event().wait()
        finally:
            stopped.set()

    async def close():
        assert stopped.is_set()

    comfy = SimpleNamespace(fechar=AsyncMock(side_effect=close))
    workers = SimpleNamespace(iniciar=AsyncMock(), parar=AsyncMock(side_effect=close))
    monkeypatch.setattr(module, "_vigiar", watcher)
    monkeypatch.setattr(module, "Comfy", lambda *args: comfy)
    monkeypatch.setattr(module, "Fila", lambda *args: workers)
    try:
        async with module.lifespan(module.app):
            await asyncio.wait_for(reached.wait(), 1)
            if exceptional:
                raise RuntimeError("fixture failure")
    except RuntimeError:
        assert exceptional
    assert stopped.is_set() and not module.estado.pronto
    comfy.fechar.assert_awaited_once()
    workers.parar.assert_awaited_once()
