// O CODIFICADOR DO NUCLEO C++ NO ANDROID DE VERDADE (emulador ou aparelho).
//
// Mesmos 90 quadros 1280x720 pelo caminho novo (FFI + MediaCodec no C++) e
// pelo antigo (canal de plataforma + Kotlin). O arquivo de cada um tem de
// abrir com a duracao certa; o tempo dos dois vai para o log.
//
// Rodar:  flutter test integration_test/nucleo_codificador_test.dart -d emulator-5554
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/features/export/application/platform_encoder.dart';
import 'package:aurea_core/aurea_core.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path_provider/path_provider.dart';
import 'package:video_player/video_player.dart';

const _w = 1280, _h = 720, _fps = 30, _n = 90;

Uint8List _quadro(int k) {
  final b = Uint8List(_w * _h * 4);
  for (var y = 0; y < _h; y++) {
    for (var x = 0; x < _w; x++) {
      final i = (y * _w + x) * 4;
      b[i] = (x + k * 8) & 0xFF;
      b[i + 1] = (y + k * 4) & 0xFF;
      b[i + 2] = ((x ^ y) + k) & 0xFF;
      b[i + 3] = 255;
    }
  }
  return b;
}

Future<Duration> _codificar(String caminho, {required bool nucleo}) async {
  final quadros = [for (var k = 0; k < 6; k++) _quadro(k)];
  final relogio = Stopwatch()..start();
  PlatformEncoder.nucleoLigado = nucleo;
  await PlatformEncoder.start(
    path: caminho,
    width: _w,
    height: _h,
    fps: _fps,
    bitrate: 6000000,
    pelaMemoria: nucleo,
  );
  // Sem isto o teste passaria com o nucleo recusado e o Kotlin por tras.
  expect(PlatformEncoder.noNucleo, nucleo, reason: 'caminho do fluxo');
  for (var k = 0; k < _n; k++) {
    await PlatformEncoder.frameRgba(quadros[k % quadros.length], _w, _h);
  }
  expect(await PlatformEncoder.finish(), isTrue);
  relogio.stop();
  return relogio.elapsed;
}

Future<Duration> _duracao(String caminho) async {
  final c = VideoPlayerController.file(File(caminho));
  await c.initialize();
  final d = c.value.duration;
  await c.dispose();
  return d;
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('nucleo C++ e caminho antigo codificam o mesmo video', (t) async {
    expect(nucleoCarregado, isTrue, reason: 'libaurea_core.so no APK');
    expect(CodificadorNativo.disponivel, isTrue);
    final pasta = await getTemporaryDirectory();
    final novo = '${pasta.path}/nucleo.mp4',
        antigo = '${pasta.path}/antigo.mp4';

    final tNovo = await t.runAsync(() => _codificar(novo, nucleo: true));
    final tAntigo = await t.runAsync(() => _codificar(antigo, nucleo: false));

    final dNovo = await t.runAsync(() => _duracao(novo));
    final dAntigo = await t.runAsync(() => _duracao(antigo));
    // ignore: avoid_print
    print(
      'NUCLEO: $_n quadros ${_w}x$_h em ${tNovo!.inMilliseconds} ms '
      '(${File(novo).lengthSync()} bytes, ${dNovo!.inMilliseconds} ms de video) | '
      'ANTIGO: ${tAntigo!.inMilliseconds} ms '
      '(${File(antigo).lengthSync()} bytes, ${dAntigo!.inMilliseconds} ms de video)',
    );
    expect(File(novo).lengthSync(), greaterThan(10000));
    expect(dNovo.inMilliseconds, closeTo(3000, 150));
  });
}
