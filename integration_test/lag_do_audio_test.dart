// O LAG DO AUDIO NO ANDROID — MEDIDO, E NAO ADIVINHADO.
//
// O relato do dono: com musica na linha do tempo, o play engasga NO
// ANDROID (no iPhone nao). A correcao anterior atacou a ONDA sendo
// reconstruida a cada quadro, e o relato continuou.
//
// "Engasga" nao diz ONDE o tempo vai. Esta bancada roda o EDITOR DE
// VERDADE (palco + timeline + onda + tocador) com uma faixa de audio e
// imprime, uma vez por segundo:
//
//   * quadros: quantos, o pior, e quantos passaram de 33 ms;
//   * a deriva entre o relogio da composicao e a posicao real da midia;
//   * os contadores do `DiagnosticoDoAudio` — seeks, plays, volume,
//     tocadores criados/descartados, cenas remontadas e amostras de
//     relogio. Cada um desses tem uma correcao diferente;
//   * o erro entre o cabecote e o audio, amostra a amostra.
//
// O EMULADOR NAO MEDE DESEMPENHO (SwiftShader, audio emulado): o numero
// absoluto de tempo de quadro daqui nao vale para um celular. O que vale
// e o COMPORTAMENTO — quantas chamadas por segundo, se a cena e remontada
// a cada tique, se a deriva e absorvida ou corrigida em bloco.
//
// Rodar:
//   flutter test integration_test/lag_do_audio_test.dart -d emulator-5554
import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/preview_stats.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:path_provider/path_provider.dart';

import '../test/editor_hierarchy_test.dart' show openEditor;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('com musica, o play no Android: quadros e chamadas por segundo',
      (tester) async {
    // O ARQUIVO: o m4a que ja viaja no bundle dos templates, extraido para
    // um caminho de verdade (o tocador le arquivo, nao asset).
    final destino = await getTemporaryDirectory();
    final arquivo = File('${destino.path}/musica-da-bancada.m4a');
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
    print('LAG arquivo=${arquivo.lengthSync()} bytes');

    final container = await openEditor(tester, size: const Size(430, 932));
    final editor = container.read(editorControllerProvider.notifier);

    // UMA FORMA DE FUNDO: o palco tem o que desenhar durante o play.
    editor.addShapeLayer(Duration.zero, name: 'Fundo');

    // A MUSICA, com a duracao do arquivo.
    final id = editor.addAudioLayer(
      Duration.zero,
      arquivo.path,
      'musica',
      const Duration(seconds: 9),
      fonte: const Duration(seconds: 9),
    );
    // ignore: avoid_print
    print('LAG camada=$id duracao=${container.read(editorControllerProvider).duration.inMilliseconds}ms');
    await tester.pump(const Duration(milliseconds: 400));

    // A FAIXA DE AUDIO PRECISA ESTAR NA TELA: e a onda dela que entra no
    // caminho do relato.
    expect(find.byType(EditorScreen), findsOneWidget);

    // QUADROS: o mesmo ponto de apresentacao que o HUD usa.
    final quadros = <double>[];
    var anterior = 0;
    void medir(List<FrameTiming> timings) {
      for (final t in timings) {
        final us = DateTime.now().microsecondsSinceEpoch;
        if (anterior != 0) quadros.add((us - anterior) / 1000.0);
        anterior = us;
        if (t.totalSpan.inMicroseconds > 33000) {
          quadros.add(-1);
        }
      }
    }

    SchedulerBinding.instance.addTimingsCallback(medir);
    DiagnosticoDoAudio.zerar();
    DiagnosticoDoAudio.trilhaLigada = true;
    FrameLog.reset();

    // PLAY: o mesmo botao que a pessoa toca.
    final play = find.byIcon(CupertinoIcons.play_fill);
    expect(play, findsWidgets, reason: 'o botao de tocar tem de estar na tela');
    await tester.tap(play.first);
    await tester.pump();

    final inicio = DateTime.now();
    var segundo = 0;
    while (DateTime.now().difference(inicio) < const Duration(seconds: 8)) {
      await tester.pump(const Duration(milliseconds: 250));
      final passado = DateTime.now().difference(inicio).inSeconds;
      if (passado <= segundo) continue;
      segundo = passado;
      final d = DiagnosticoDoAudio.ler();
      final rel = FrameLog.report.value;
      final comuns = quadros.where((q) => q >= 0).toList()..sort();
      final pior = comuns.isEmpty ? 0.0 : comuns.last;
      final mediana = comuns.isEmpty ? 0.0 : comuns[comuns.length ~/ 2];
      final perdidos = quadros.where((q) => q < 0).length;
      // ignore: avoid_print
      print(
        'LAG t=${segundo}s quadros=${comuns.length} mediana=${mediana.toStringAsFixed(1)}ms '
        'pior=${pior.toStringAsFixed(1)}ms perdidos=$perdidos '
        'drift=${rel?.driftMs.toStringAsFixed(1)}ms '
        'seeks=${d['seeks']} plays=${d['plays']} pauses=${d['pauses']} '
        'volumes=${d['volumes']} criados=${d['criados']} '
        'descartados=${d['descartados']} cenas=${d['cenas']} '
        'amostras=${d['amostras']}',
      );
    }
    SchedulerBinding.instance.removeTimingsCallback(medir);

    // A TRILHA CRUA da ancoragem: relogio;midia;alvo;erro;vies.
    // ignore: avoid_print
    print('LAG trilha_cabecalho=relogio_ms;midia_ms;alvo_ms;erro_ms;vies_ms');
    for (final linha in DiagnosticoDoAudio.trilha) {
      // ignore: avoid_print
      print('LAG trilha=$linha');
    }

    // A PAUSA tambem faz parte: o custo de parar aparece no relato como
    // "trava quando eu paro".
    await tester.tap(play.first);
    await tester.pump(const Duration(milliseconds: 200));
    // ignore: avoid_print
    print('LAG fim posicao=${FrameLog.report.value?.driftMs}');

    // UM ULTIMO NUMERO: a deriva entre o cabecote e o audio, no fim.
    final projeto = container.read(editorControllerProvider);
    // ignore: avoid_print
    print('LAG projeto=${projeto.layers.length} camadas');
    expect(find.byType(EditorScreen), findsOneWidget);
  });
}
