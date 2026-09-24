"""START da Aurea AI no Colab (A100) — uma celula, do zero ao ONLINE.

Otimizado para comecar rapido. O gargalo e copiar ~41 GB do Drive para o SSD
local, entao e o que mais mudou:

  * copia PARALELA, com a concorrencia MEDIDA antes de comecar (nao chutada):
    uma sonda copia 48 MB com 1, 2 e 4 fluxos e fica com a que deu mais MB/s
    agregado. Drive montado por FUSE nao escala linear — 4 fluxos costumam ser
    mais lentos que 2 ou 3;
  * os dois modelos grandes entram primeiro, para nao sobrar um deles sozinho
    no fim;
  * arquivo local com o tamanho certo e pulado na hora (rodar o START de novo
    na MESMA sessao fica quase instantaneo);
  * enquanto os modelos copiam, OUTRA thread cuida do que nao depende deles:
    cloudflared, dependencias do ComfyUI e o import do publicador;
  * o ComfyUI so e clonado/instalado se faltar — ha um carimbo das dependencias,
    e o `pip install` e pulado quando o carimbo bate.

O que NAO mudou, de proposito:

  * a copia e de VERDADE, para o SSD local. Nunca symlink para o Drive: foi o
    symlink que travava o CLIPLoader/VAELoader. Os originais do Drive sao so
    lidos;
  * o endereco publicado vem da sessao atual e e conferido no /server antes de
    dizer ONLINE;
  * falhar num passo PARA o START. Nao existe "ONLINE" falso.

Rode esta celula inteira e espere `AUREA AI ONLINE`.
"""
from __future__ import annotations

import concurrent.futures as futuros
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

# =============================================================================
# Configuracao
# =============================================================================

DRIVE_RAIZ = "/content/drive/MyDrive"
MODELOS_LOCAIS = Path("/content/models")          # SSD da instancia, nao o Drive
COMFY = Path("/content/ComfyUI")
CARIMBO_DEPS = Path("/content/.aurea_deps_ok")
AGENTE = "Aurea-Discovery/1.0 (+https://aurea.app)"
PORTAL = 8188
WORKER = "https://aurea-ai-discovery.aureaapp.workers.dev"
SECRET_NO_COLAB = "AUREA_DISCOVERY_SECRET"

BLOCO = 16 << 20                # 16 MB por leitura: sobre FUSE o bloco decide a velocidade
CONCORRENCIA_MAX = 4
SONDA_MB = 48                   # quanto se copia em cada teste de concorrencia

MODELOS = {
    "minimax_h3_fl2v_turbo_4step_v1.0_768p_comfyui_bf16.safetensors": "loras",
    "minimax_h3_fl2v_turbo_8step_v1.0_comfyui_bf16.safetensors": "loras",
    "minimax_h3_fl2va_pruned_int8_convrot.safetensors": "diffusion_models",
    "qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors": "text_encoders",
    "minimax_h3_audio_vae_fp32.safetensors": "vae",
    "minimax_h3_video_vae_int8_convrot.safetensors": "vae",
}

# Rotulos curtos, so para o progresso ficar legivel.
APELIDO = {
    "minimax_h3_fl2v_turbo_4step_v1.0_768p_comfyui_bf16.safetensors": "LoRA 4step",
    "minimax_h3_fl2v_turbo_8step_v1.0_comfyui_bf16.safetensors": "LoRA 8step",
    "minimax_h3_fl2va_pruned_int8_convrot.safetensors": "diffusion model",
    "qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors": "Qwen text encoder",
    "minimax_h3_audio_vae_fp32.safetensors": "audio VAE",
    "minimax_h3_video_vae_int8_convrot.safetensors": "video VAE",
}

_inicio = time.time()


def log(msg: str) -> None:
    print(f"\r[aurea +{time.time() - _inicio:5.1f}s] {msg}", flush=True)


def morrer(msg: str) -> None:
    """Falha alto. Um START que segue depois de um erro mente para o usuario."""
    print(f"\n❌ {msg}", flush=True)
    sys.exit(1)


def humano(n: float) -> str:
    return f"{n / 1e9:.2f} GB" if n >= 1e9 else f"{n / 1e6:.0f} MB"


def relogio(s: float) -> str:
    s = int(max(0, s))
    return f"{s // 60}m{s % 60:02d}s" if s >= 60 else f"{s}s"


# =============================================================================
# 1. Ambiente
# =============================================================================

def conferir_gpu() -> None:
    try:
        saida = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=60,
        ).stdout.strip()
    except Exception as e:  # noqa: BLE001
        morrer(f"nvidia-smi nao respondeu ({e}). O runtime esta com GPU?")
    if "A100" not in saida:
        log(f"AVISO: a GPU e '{saida}', nao uma A100. Roda, mas mais devagar.")
    else:
        log(f"GPU: {saida}")


def montar_drive() -> None:
    from google.colab import drive  # type: ignore

    if not Path(DRIVE_RAIZ).is_dir():
        drive.mount("/content/drive")
    if not Path(DRIVE_RAIZ).is_dir():
        morrer("o Drive nao montou")
    log("Drive montado")


# =============================================================================
# 2. O que roda ENQUANTO os modelos copiam
# =============================================================================

def garantir_cloudflared() -> str:
    exe = Path("/usr/local/bin/cloudflared")
    if not exe.is_file():
        log("ambiente: baixando o cloudflared")
        subprocess.run(
            ["wget", "-q", "-O", str(exe),
             "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64"],
            check=True,
        )
        exe.chmod(0o755)
    return str(exe)


def hash_requirements() -> str:
    req = COMFY / "requirements.txt"
    if not req.is_file():
        return "sem-requirements"
    return hashlib.sha256(req.read_bytes()).hexdigest()[:16]


def restaurar_comfyui() -> None:
    """Reaproveita o ComfyUI que ja esta la. So clona/instala se faltar."""
    if (COMFY / "main.py").is_file():
        log(f"ComfyUI: reaproveitando {COMFY}")
    else:
        if COMFY.exists():
            log("ComfyUI: pasta existe mas sem main.py — clonando de novo")
            shutil.rmtree(COMFY, ignore_errors=True)
        log("ComfyUI: clonando")
        subprocess.run(
            ["git", "clone", "--depth", "1",
             "https://github.com/comfyanonymous/ComfyUI.git", str(COMFY)],
            check=True,
        )

    # O carimbo evita repetir um pip install de minutos quando nada mudou.
    marca = hash_requirements()
    if CARIMBO_DEPS.is_file() and CARIMBO_DEPS.read_text().strip() == marca:
        log("ComfyUI: dependencias ja instaladas (carimbo bate)")
        return
    req = COMFY / "requirements.txt"
    if req.is_file():
        log("ComfyUI: instalando dependencias (pip)")
        subprocess.run([sys.executable, "-m", "pip", "install", "-q", "-r", str(req)], check=True)
    CARIMBO_DEPS.write_text(marca)
    log("ComfyUI: dependencias prontas")


def preparar_ambiente() -> str:
    """Tudo que NAO depende dos modelos. Roda em paralelo com a copia."""
    restaurar_comfyui()
    exe = garantir_cloudflared()
    # O publicador tem que importar: melhor descobrir isso antes da copia de 41 GB.
    sys.path.insert(0, "/content/aurea/colab")
    import publicar_discovery  # type: ignore  # noqa: F401

    log("ambiente: cloudflared, dependencias e publicador prontos (em paralelo)")
    return exe


# =============================================================================
# 3. Drive -> SSD local, em paralelo
# =============================================================================

class Contador:
    """Progresso compartilhado entre as threads de copia."""

    def __init__(self, total: int) -> None:
        self.total = total
        self.feito = 0
        self.prontos: list[str] = []
        self.trava = threading.Lock()

    def somar(self, n: int) -> None:
        with self.trava:
            self.feito += n

    def concluir(self, rotulo: str) -> None:
        with self.trava:
            self.prontos.append(rotulo)
        log(f"[{len(self.prontos)}] {rotulo} ✓")

    def instantaneo(self) -> tuple[int, int]:
        with self.trava:
            return self.feito, len(self.prontos)


def achar_no_drive(nome: str) -> Path | None:
    """Procura o arquivo no Drive pelo nome. Nao adivinha caminho."""
    for raiz, dirs, arquivos in os.walk(DRIVE_RAIZ):
        dirs[:] = [d for d in dirs if not d.startswith(".") and d not in
                   ("Colab Notebooks", "ComfyUI", "outputs", "temp", "node_modules")]
        if nome in arquivos:
            return Path(raiz) / nome
    return None


def copiar(origem: Path, destino: Path, contador: Contador | None = None,
           limite: int | None = None) -> int:
    """Copia binaria em blocos grandes. Devolve os bytes copiados.

    Blocos de 16 MB e nao o `shutil.copyfile` padrao: sobre FUSE o tamanho do
    bloco e o que decide a velocidade, e o default (64 KB) desperdica a maior
    parte da banda do Drive.
    """
    destino.parent.mkdir(parents=True, exist_ok=True)
    tmp = destino.with_suffix(destino.suffix + ".parcial")
    total = 0
    with open(origem, "rb", buffering=0) as e, open(tmp, "wb", buffering=0) as s:
        while True:
            bloco = e.read(BLOCO)
            if not bloco:
                break
            s.write(bloco)
            total += len(bloco)
            if contador is not None:
                contador.somar(len(bloco))
            if limite is not None and total >= limite:
                break
    if limite is not None:
        tmp.unlink(missing_ok=True)     # sondagem: nao deixa lixo
    else:
        tmp.replace(destino)
    return total


def medir_concorrencia(sonda: Path) -> int:
    """Copia a mesma fatia com 1, 2 e 4 fluxos e fica com a de maior MB/s.

    Drive montado por FUSE nao escala linear: cada fluxo extra custa overhead no
    FUSE e, a partir de certo ponto, o agregado CAI. Medir custa ~30 s; escolher
    errado custa minutos nos 41 GB.
    """
    melhores: list[tuple[float, int]] = []
    for n in (1, 2, CONCORRENCIA_MAX):
        pasta = Path("/content/.aurea_sonda")
        shutil.rmtree(pasta, ignore_errors=True)
        pasta.mkdir(parents=True, exist_ok=True)
        inicio = time.time()
        with futuros.ThreadPoolExecutor(max_workers=n) as pool:
            tarefas = [pool.submit(copiar, sonda, pasta / f"p{n}_{i}.bin", None, SONDA_MB << 20)
                       for i in range(n)]
            movido = sum(t.result() for t in tarefas)
        gasto = max(time.time() - inicio, 0.001)
        mbps = movido / gasto / 1e6
        melhores.append((mbps, n))
        log(f"modelos: sonda com {n} fluxo(s) -> {mbps:.0f} MB/s")
        shutil.rmtree(pasta, ignore_errors=True)

    mbps, n = max(melhores)
    return n


def relatar(contador: Contador, quantos: int, parar: threading.Event) -> None:
    """Uma linha de progresso a cada 2 s. Nada de milhares de linhas."""
    anterior, quando = 0, time.time()
    while not parar.wait(2):
        feito, prontos = contador.instantaneo()
        agora = time.time()
        janela = max(agora - quando, 0.001)
        mbps = (feito - anterior) / janela / 1e6
        anterior, quando = feito, agora
        faltam = contador.total - feito
        eta = faltam / (mbps * 1e6) if mbps > 1 else 0
        linha = ("Modelos SSD  %s/%s  %.0f MB/s  falta %s  [%d/%d]"
                 % (humano(feito), humano(contador.total), mbps, relogio(eta), prontos, quantos))
        print(f"\r{linha:<74}", end="", flush=True)
    print()


def preparar_modelos(concorrencia: int | None = None) -> None:
    MODELOS_LOCAIS.mkdir(parents=True, exist_ok=True)

    # 1) Acha tudo e separa o que ja esta pronto (segunda execucao: quase tudo).
    origens: dict[str, Path] = {}
    faltando: list[str] = []
    for nome in MODELOS:
        o = achar_no_drive(nome)
        if o is None:
            faltando.append(nome)
        else:
            origens[nome] = o
    if faltando:
        morrer("nao achei no Drive: " + ", ".join(faltando))

    pendentes: list[tuple[str, Path, Path, int]] = []
    for nome, pasta in MODELOS.items():
        destino = MODELOS_LOCAIS / pasta / nome
        tamanho = origens[nome].stat().st_size
        if destino.is_file() and destino.stat().st_size == tamanho:
            log(f"modelos: {APELIDO[nome]} ja no SSD, pulando")
            continue
        pendentes.append((nome, origens[nome], destino, tamanho))

    if not pendentes:
        log("modelos: os 6 ja estao no SSD local")
        return

    total = sum(p[3] for p in pendentes)
    log(f"modelos: {len(pendentes)} para copiar, {humano(total)}")

    # 2) Concorrencia medida (so quando ha mais de um arquivo).
    if concorrencia is None:
        if len(pendentes) == 1:
            concorrencia = 1
        else:
            maior = max(origens.values(), key=lambda p: p.stat().st_size)
            concorrencia = medir_concorrencia(maior)
    log(f"modelos: copiando com {concorrencia} fluxo(s)")

    # 3) Os grandes primeiro: com pool, comecar pelo maior evita o fim triste de
    #    um arquivo de 19 GB rodando sozinho.
    pendentes.sort(key=lambda p: p[3], reverse=True)

    contador = Contador(total)
    parar = threading.Event()
    reporter = threading.Thread(target=relatar, args=(contador, len(pendentes), parar), daemon=True)
    reporter.start()

    falhas: list[str] = []

    def tarefa(item: tuple[str, Path, Path, int]) -> None:
        nome, origem, destino, tamanho = item
        for tentativa in (1, 2):
            destino.with_suffix(destino.suffix + ".parcial").unlink(missing_ok=True)
            try:
                copiar(origem, destino, contador)
                if destino.stat().st_size != tamanho:
                    raise IOError(f"copiou {destino.stat().st_size}, esperava {tamanho}")
                contador.concluir(APELIDO[nome])
                return
            except Exception as e:  # noqa: BLE001
                if tentativa == 2:
                    falhas.append(f"{APELIDO[nome]}: {e}")
                    return
                log(f"modelos: {APELIDO[nome]} falhou ({e}); tentando de novo")

    with futuros.ThreadPoolExecutor(max_workers=concorrencia) as pool:
        list(pool.map(tarefa, pendentes))

    parar.set()
    reporter.join(timeout=3)

    if falhas:
        morrer("copia incompleta — " + "; ".join(falhas))

    for nome, pasta in MODELOS.items():
        destino = MODELOS_LOCAIS / pasta / nome
        if not destino.is_file() or destino.stat().st_size == 0:
            morrer(f"modelo ausente ou vazio depois da copia: {pasta}/{nome}")
    log(f"modelos: 6 no SSD local em {MODELOS_LOCAIS} (nenhum symlink)")


# =============================================================================
# 4. ComfyUI no ar
# =============================================================================

def subir_comfyui() -> subprocess.Popen:
    proc = subprocess.Popen(
        [sys.executable, "main.py", "--listen", "127.0.0.1", "--port", str(PORTAL)],
        cwd=str(COMFY),
        stdout=open("/content/comfyui.log", "ab"),
        stderr=subprocess.STDOUT,
        env={**os.environ, "AUREA_MODELS_DIR": str(MODELOS_LOCAIS)},
    )
    log(f"ComfyUI: subindo (pid {proc.pid}), log em /content/comfyui.log")
    return proc


def esperar_system_stats(base: str, tentativas: int, rotulo: str) -> bool:
    """HTTP 200 E JSON com 'system'. 200 de portal de wifi nao serve."""
    pedido = urllib.request.Request(
        base.rstrip("/") + "/system_stats", headers={"User-Agent": AGENTE})
    for i in range(tentativas):
        try:
            with urllib.request.urlopen(pedido, timeout=20) as r:
                if r.status == 200:
                    corpo = json.loads(r.read().decode("utf-8", "replace"))
                    if isinstance(corpo, dict) and "system" in corpo:
                        log(f"/system_stats {rotulo} ok ({i + 1}a tentativa)")
                        return True
                    log(f"{rotulo}: respondeu 200 mas nao e o ComfyUI")
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            if i % 5 == 0:
                log(f"{rotulo}: ainda nao ({e})")
        except json.JSONDecodeError:
            log(f"{rotulo}: respondeu 200 mas o corpo nao e JSON")
        time.sleep(4)
    return False


# =============================================================================
# 5. Cloudflare Tunnel
# =============================================================================

def abrir_tunel(exe: str) -> tuple[subprocess.Popen, str]:
    log("tunel: abrindo")
    proc = subprocess.Popen(
        [exe, "tunnel", "--url", f"http://127.0.0.1:{PORTAL}", "--no-autoupdate"],
        stdout=open("/content/cloudflared.log", "wb"),
        stderr=subprocess.STDOUT,
    )
    for _ in range(60):
        time.sleep(2)
        try:
            texto = Path("/content/cloudflared.log").read_text(errors="replace")
        except OSError:
            continue
        for pedaco in texto.split():
            if pedaco.startswith("https://") and "trycloudflare.com" in pedaco:
                url = pedaco.strip().rstrip(".,)")
                if url.count("://") == 1 and len(url) > 30:
                    log(f"tunel: {url}")
                    return proc, url
    proc.terminate()
    morrer("o cloudflared nao publicou uma URL a tempo (veja /content/cloudflared.log)")


# =============================================================================
# 6. Discovery
# =============================================================================

def ler_secret() -> str:
    """Le o Secret do Colab. Nunca imprime, nunca grava em arquivo."""
    valor = os.environ.get(SECRET_NO_COLAB, "")
    if not valor:
        try:
            from google.colab import userdata  # type: ignore

            valor = userdata.get(SECRET_NO_COLAB) or ""
        except Exception:  # noqa: BLE001
            valor = ""
    if not valor:
        morrer(f"falta o Secret {SECRET_NO_COLAB} no Colab "
               "(barra lateral > chave > Adicionar novo secret, com acesso ao notebook)")
    log(f"discovery: secret {SECRET_NO_COLAB} lido (nao vou imprimir)")
    return valor


def conferir_no_worker(url_atual: str, tentativas: int = 15) -> bool:
    """O Worker tem que devolver EXATAMENTE esta URL antes de dizermos ONLINE."""
    for _ in range(tentativas):
        pedido = urllib.request.Request(WORKER + "/server", headers={"User-Agent": AGENTE})
        try:
            with urllib.request.urlopen(pedido, timeout=20) as r:
                doc = json.loads(r.read().decode("utf-8", "replace"))
        except Exception as e:  # noqa: BLE001
            log(f"/server ainda nao respondeu ({e})")
            time.sleep(3)
            continue
        if doc.get("online") is True and doc.get("endpoint") == url_atual:
            log("discovery: /server confirma esta URL")
            return True
        log(f"/server devolveu online={doc.get('online')} endpoint={doc.get('endpoint')} — esperando bater")
        time.sleep(3)
    return False


# =============================================================================
# O START
# =============================================================================

def main() -> None:
    log("=== Aurea AI — START ===")

    conferir_gpu()

    # Ambiente em uma thread: cloudflared + dependencias + publicador correm
    # ENQUANTO os 41 GB copiam. Antes, isso tudo esperava a copia terminar.
    ambiente = futuros.ThreadPoolExecutor(max_workers=1)
    tarefa_ambiente = ambiente.submit(preparar_ambiente)

    montar_drive()
    preparar_modelos()

    # So agora o resultado do ambiente e necessario.
    try:
        exe_cloudflared = tarefa_ambiente.result()
    except Exception as e:  # noqa: BLE001
        morrer(f"o preparo do ambiente falhou: {e}")
    finally:
        ambiente.shutdown(wait=False)

    comfy = subir_comfyui()
    if not esperar_system_stats(f"http://127.0.0.1:{PORTAL}", 60, "local"):
        comfy.terminate()
        morrer("o ComfyUI nao subiu. Veja /content/comfyui.log")

    tunel, url = abrir_tunel(exe_cloudflared)
    if not esperar_system_stats(url, 45, "externo"):
        tunel.terminate()
        comfy.terminate()
        morrer("o endereco do tunel nao respondeu /system_stats. Nada foi publicado.")

    sys.path.insert(0, "/content/aurea/colab")
    from publicar_discovery import publicar  # type: ignore

    if not publicar(url, gpu="NVIDIA A100-SXM4-80GB", segredo=ler_secret()):
        morrer("nao consegui publicar no discovery")

    if not conferir_no_worker(url):
        morrer("/server nao devolveu esta URL — o app nao vai encontrar o servidor")

    print("\n" + "=" * 60)
    print(f"🔥 AUREA AI ONLINE   (em {relogio(time.time() - _inicio)})")
    print(f"   endereco: {url}")
    print(f"   publicado em {WORKER}/server")
    print("   O app conecta sozinho — nao precisa de APK novo.")
    print("=" * 60 + "\n")


main()
