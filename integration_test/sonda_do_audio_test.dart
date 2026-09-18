// SONDA: COMO O ANDROID PUBLICA A POSICAO DO AUDIO.
//
// O relogio da composicao e ANCORADO na posicao que o tocador publica
// (`PlaybackController.anchorToMedia`), e a politica de ancoragem tem um
// numero escrito para isso: "o plugin publica posicao a cada 100 ms (dez
// amostras por segundo)". Esse numero foi medido no iOS e esta no
// comentario do codigo.
//
// Esta sonda mede o MESMO numero no Android, e mais duas coisas que
// decidem o resto da conta:
//
//   * quantas amostras NOVAS de posicao chegam por segundo;
//   * de quanto em quanto a posicao ANDA (o tamanho do degrau) — e ele
//     que diz se a amostra serve para ancorar um relogio de 30 fps;
//   * quanto tempo cada leitura leva (a chamada de plataforma), porque
//     ela acontece a cada 100 ms no mesmo isolate que desenha.
//
// Nao e teste de desempenho: o emulador usa SwiftShader e um numero de
// tempo de quadro daqui nao diz nada sobre um celular. O que se le aqui e
// o COMPORTAMENTO do plugin, que e o mesmo no aparelho.
//
// Rodar:
//   flutter test integration_test/sonda_do_audio_test.dart -d emulator-5554
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path_provider/path_provider.dart';
import 'package:video_player/video_player.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('como o Android publica a posicao do audio', (tester) async {
    // UM ARQUIVO DE VERDADE: o m4a que ja viaja no bundle dos templates.
    final destino = await getTemporaryDirectory();
    final arquivo = File('${destino.path}/sonda-audio.m4a');
    if (!arquivo.existsSync()) {
      final bytes = await rootBundle.load(
        'assets/templates/reference-rebuild-audio.m4a',
      );
      await arquivo.writeAsBytes(
        bytes.buffer.asUint8List(bytes.offsetInBytes, bytes.lengthInBytes),
        flush: true,
      );
    }
    // ignore: avoid_print
    print('SONDA arquivo=${arquivo.path} bytes=${arquivo.lengthSync()}');

    final c = VideoPlayerController.file(arquivo);
    final t0 = DateTime.now();
    await c.initialize();
    final tInit = DateTime.now().difference(t0).inMilliseconds;
    // ignore: avoid_print
    print('SONDA init=${tInit}ms duracao=${c.value.duration.inMilliseconds}ms');

    c.play();
    final amostras = <int>[];
    final carimbos = <int>[];
    final leiturasUs = <int>[];

    final inicio = DateTime.now();
    var anterior = -1;
    // 4 s de janela, lendo a cada 10 ms: mais fino que o timer do plugin
    // (100 ms), entao a CADENCIA que aparecer e a dele, e nao a minha.
    while (DateTime.now().difference(inicio) < const Duration(seconds: 4)) {
      final a = DateTime.now();
      final pos = await c.position;
      leiturasUs.add(DateTime.now().difference(a).inMicroseconds);
      final us = pos?.inMicroseconds ?? -1;
      if (us != anterior) {
        anterior = us;
        amostras.add(us);
        carimbos.add(DateTime.now().difference(inicio).inMilliseconds);
      }
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }

    // ignore: avoid_print
    print('SONDA amostras_novas=${amostras.length} em 4s');
    if (amostras.length > 1) {
      final passos = <int>[];
      for (var i = 1; i < amostras.length; i++) {
        passos.add(amostras[i] - amostras[i - 1]);
      }
      passos.sort();
      final mediana = passos[passos.length ~/ 2] ~/ 1000;
      final menor = passos.first ~/ 1000;
      final maior = passos.last ~/ 1000;
      // ignore: avoid_print
      print('SONDA passo_da_posicao_ms mediana=$mediana min=$menor max=$maior');
      final intervalos = <int>[];
      for (var i = 1; i < carimbos.length; i++) {
        intervalos.add(carimbos[i] - carimbos[i - 1]);
      }
      intervalos.sort();
      // ignore: avoid_print
      print('SONDA intervalo_entre_amostras_ms '
          'mediana=${intervalos[intervalos.length ~/ 2]} '
          'min=${intervalos.first} max=${intervalos.last}');
    }
    leiturasUs.sort();
    // ignore: avoid_print
    print('SONDA leitura_da_posicao_us '
        'mediana=${leiturasUs[leiturasUs.length ~/ 2]} '
        'p95=${leiturasUs[(leiturasUs.length * 0.95).floor()]} '
        'max=${leiturasUs.last}');
    // ignore: avoid_print
    print('SONDA amostra_bruta=${amostras.take(12).map((e) => e ~/ 1000).toList()}');

    // ================================================================
    // A LARGADA: quanto tempo o Android leva do play ate o som ANDAR.
    //
    // E a pergunta que decide tudo: o relogio da composicao comeca a
    // contar no instante do toque. Se o som so comeca a andar 300 ms
    // depois, a composicao ja esta 300 ms adiantada — e a ancora passa a
    // puxar o relogio PARA TRAS, que e o que a pessoa sente como "o audio
    // ficou atrasado".
    // ================================================================
    for (final rotulo in ['quente', 'frio']) {
      if (rotulo == 'frio') {
        await c.seekTo(Duration.zero);
        await c.pause();
        await Future<void>.delayed(const Duration(milliseconds: 600));
      }
      final antes = await c.position ?? Duration.zero;
      final t = DateTime.now();
      c.play();
      var andou = -1;
      while (DateTime.now().difference(t) < const Duration(seconds: 3)) {
        final agora = await c.position ?? Duration.zero;
        if (agora > antes) {
          andou = DateTime.now().difference(t).inMilliseconds;
          break;
        }
        await Future<void>.delayed(const Duration(milliseconds: 5));
      }
      // ignore: avoid_print
      print('SONDA largada_$rotulo=${andou}ms '
          'buffering=${c.value.isBuffering} tocando=${c.value.isPlaying}');
    }

    await c.pause();
    await c.dispose();
  });
}
