# O LADO DO PC do teste integration_test/fluxo_completo_test.dart.
#
# O teste imprime "FLUXO-HOST <cmd> <nome> [arg]" (vai para o logcat, tag
# flutter); aqui se executa o pedido pelo adb e se responde criando
# files/fluxo/ok_<nome> dentro do app (run-as: o APK do teste e debug).
#
#   captura <nome>          screencap -> <saida>/<nome>.png
#   video <nome>            copia o mp4 de prova para files/fluxo/entrada.mp4
#   digitar <nome> <texto>  apaga o campo focado e digita pelo teclado real
#   puxar <nome> <caminho>  copia um arquivo do app para <saida>/<nome>.mp4
#   teclas <nome> <codigos> input keyevent
#   fim <nome>              encerra
#
# Uso (ligar ANTES do flutter test; ele termina sozinho no fim do teste):
#   python integration_test/fluxo_completo_vigia.py <pasta-das-fotos> [video.mp4]
# Sem video, gera um testsrc2 1080x1920 30 fps de 5 s com o ffmpeg do
# imageio_ffmpeg.
#
# Uma thread a parte vigia o dialogo de permissao do sistema: se ele tomar
# a frente, fotografa e NEGA (BACK) — a escolha que preserva a privacidade.
import os
import re
import subprocess
import sys
import threading
import time

ADB = r'C:\Users\SnyX\AppData\Local\Android\Sdk\platform-tools\adb.exe'
SER = 'emulator-5554'
PKG = 'com.aurea.aurea'
SAIDA = sys.argv[1]
os.makedirs(SAIDA, exist_ok=True)
VIDEO = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SAIDA, 'entrada.mp4')
if not os.path.exists(VIDEO):
    import imageio_ffmpeg
    subprocess.run([
        imageio_ffmpeg.get_ffmpeg_exe(), '-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'lavfi', '-i', 'testsrc2=size=1080x1920:rate=30:duration=5',
        '-f', 'lavfi', '-i', 'sine=frequency=440:duration=5',
        '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-preset', 'veryfast',
        '-c:a', 'aac', '-shortest', '-movflags', '+faststart', VIDEO,
    ], check=True)
LOG = open(os.path.join(SAIDA, 'vigia.log'), 'a', encoding='utf-8')
parar = threading.Event()


def log(msg):
    linha = f'{time.strftime("%H:%M:%S")} {msg}'
    print(linha, flush=True)
    LOG.write(linha + '\n')
    LOG.flush()


def adb(*a, entrada=None, timeout=60):
    return subprocess.run([ADB, '-s', SER, *a], capture_output=True,
                          input=entrada, timeout=timeout)


def captura(nome):
    png = adb('exec-out', 'screencap', '-p').stdout
    with open(os.path.join(SAIDA, nome + '.png'), 'wb') as f:
        f.write(png)
    return len(png)


def ack(nome):
    r = adb('shell', f'run-as {PKG} sh -c "mkdir -p files/fluxo && touch files/fluxo/ok_{nome}"')
    if r.returncode != 0:
        log(f'ack {nome} falhou: {r.stderr!r}')


def foco():
    r = adb('shell', 'dumpsys window | grep -m1 mCurrentFocus', timeout=20)
    return r.stdout.decode('utf-8', 'replace')


def vigiar_permissao():
    n = 0
    while not parar.is_set():
        try:
            f = foco()
            if 'permissioncontroller' in f or 'GrantPermissions' in f:
                n += 1
                tam = captura(f'permissao-{n}')
                log(f'DIALOGO DE PERMISSAO na frente ({f.strip()}); foto {tam} bytes; negando (BACK)')
                adb('shell', 'input keyevent KEYCODE_BACK')
                time.sleep(1.5)
        except Exception as e:  # noqa: BLE001
            log(f'vigia de permissao: {e}')
        parar.wait(1.0)


def main():
    adb('logcat', '-c')
    r = adb('push', VIDEO, '/data/local/tmp/fluxo_entrada.mp4', timeout=120)
    log(f'push do video: {r.returncode} {r.stdout[-200:]!r}')
    threading.Thread(target=vigiar_permissao, daemon=True).start()
    p = subprocess.Popen([ADB, '-s', SER, 'logcat', '-v', 'raw', 'flutter:I', '*:S'],
                         stdout=subprocess.PIPE, text=True, encoding='utf-8',
                         errors='replace', bufsize=1)
    log('vigiando o logcat')
    for linha in p.stdout:
        m = re.search(r'FLUXO-HOST (\w+) (\S+)(?: (.*))?', linha)
        if not m:
            if 'FLUXO-' in linha:
                LOG.write(linha)
                LOG.flush()
            continue
        cmd, nome, arg = m.group(1), m.group(2), (m.group(3) or '').strip()
        try:
            if cmd == 'captura':
                tam = captura(nome)
                log(f'captura {nome}: {tam} bytes')
            elif cmd == 'video':
                r = adb('shell', f"cat /data/local/tmp/fluxo_entrada.mp4 | run-as {PKG} sh -c 'mkdir -p files/fluxo && cat > files/fluxo/entrada.mp4'", timeout=120)
                log(f'video -> app: {r.returncode} {r.stderr!r}')
            elif cmd == 'digitar':
                # Fim do campo, apaga o que houver e digita pelo teclado.
                adb('shell', 'input keyevent KEYCODE_MOVE_END')
                adb('shell', 'input keyevent ' + ' '.join(['KEYCODE_DEL'] * 24))
                if arg:
                    adb('shell', 'input text ' + arg.replace(' ', '%s'))
                log(f'digitar {nome}: {arg!r}')
            elif cmd == 'puxar':
                r = adb('exec-out', f'run-as {PKG} cat {arg}', timeout=120)
                with open(os.path.join(SAIDA, nome + '.mp4'), 'wb') as f:
                    f.write(r.stdout)
                log(f'puxar {arg}: {len(r.stdout)} bytes')
            elif cmd == 'teclas':
                adb('shell', 'input keyevent ' + arg)
                log(f'teclas {nome}: {arg}')
            elif cmd == 'fim':
                log('fim pedido pelo teste')
                ack(nome)
                break
            ack(nome)
        except Exception as e:  # noqa: BLE001
            log(f'{cmd} {nome} falhou: {e}')
    parar.set()
    p.kill()


if __name__ == '__main__':
    main()
