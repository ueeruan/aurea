// A BANCADA NAO PODE MEDIR O PROPRIO INSTRUMENTO.
//
// ====================== O QUE ESTE ARQUIVO PRENDE ======================
//
// A bancada de desempenho roda no binding de teste AO VIVO com
// `framePolicy = fullyLive`. Nessa combinacao o binding REAGENDA um
// quadro por conta propria toda vez que desenha um quadro que nao veio
// de um `pump`:
//
//   LiveTestWidgetsFlutterBinding.handleDrawFrame() {
//     ...
//     if (_expectingFrame) { completa o pump }
//     else if (framePolicy != benchmark) platformDispatcher.scheduleFrame();
//   }
//
// Ou seja: UM unico quadro do app fora de um pump — o quadro final em
// qualidade cheia que sai 260 ms depois de um gesto, por exemplo — poe a
// bancada em 60 fps ate o fim do teste, com o app inteiramente parado.
// Foi assim que "2d-repouso-apos-mexer: 358 quadros em 6 s, ui_p50
// 0,8 ms, raster_p50 15 ms, cpu 80%" virou um laco de repintura que nao
// existia: ui quase zero porque nada reconstruia mesmo, e raster cheio
// porque o binding mandava recompor a mesma arvore a cada vsync.
//
// Este teste mede isso com um `Text` na tela — sem editor, sem efeito,
// sem projeto — e guarda o criterio que separa um do outro:
//
//   * `quadros` (FrameTiming) conta o que o INSTRUMENTO produziu;
//   * `hasScheduledFrame` so sobe quando o APP pede, porque o
//     reagendamento do binding vai direto ao `platformDispatcher`.
//
// A bancada imprime os dois, e quem julga repouso e `pedidos=`.
//
// Rodar:  flutter test test/bancada_ao_vivo_test.dart
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final binding = LiveTestWidgetsFlutterBinding.ensureInitialized();
  binding.framePolicy = LiveTestWidgetsFlutterBindingFramePolicy.fullyLive;

  testWidgets('um quadro fora do pump poe a bancada em fps cheio sozinha', (
    tester,
  ) async {
    final quadros = <FrameTiming>[];
    void aoMedir(List<FrameTiming> lote) => quadros.addAll(lote);
    SchedulerBinding.instance.addTimingsCallback(aoMedir);
    addTearDown(() => SchedulerBinding.instance.removeTimingsCallback(aoMedir));

    // A MESMA SONDA DA BANCADA: borda de subida de `hasScheduledFrame`.
    var pedidos = 0;
    var aberto = false;
    void olhar() {
      final agora = SchedulerBinding.instance.hasScheduledFrame;
      if (agora && !aberto) pedidos++;
      aberto = agora;
    }

    final relogio = Timer.periodic(const Duration(milliseconds: 4), (_) {
      olhar();
    });
    SchedulerBinding.instance.addPersistentFrameCallback((_) => olhar());
    addTearDown(relogio.cancel);

    await tester.pumpWidget(const MaterialApp(home: Text('a')));
    await tester.pump(const Duration(milliseconds: 300));

    /// Os tempos chegam em lote (ate 1 s de atraso), como na bancada.
    Future<void> janela(Duration d) async {
      await Future<void>.delayed(const Duration(milliseconds: 1300));
      quadros.clear();
      pedidos = 0;
      await Future<void>.delayed(d);
      await Future<void>.delayed(const Duration(milliseconds: 1300));
    }

    // 1) DEPOIS DE UM PUMP o instrumento fica quieto: e por isso que os
    //    cenarios de repouso que vem logo depois de um `pump` davam zero,
    //    e nao porque aqueles estados fossem mais limpos que os outros.
    await janela(const Duration(milliseconds: 1500));
    // ignore: avoid_print
    print('AO-VIVO depois de um pump: ${quadros.length} quadros em 1,5 s');
    expect(
      quadros.length,
      lessThan(5),
      reason: 'com o ultimo evento sendo um pump, o instrumento se cala',
    );

    // 2) UM UNICO quadro pedido fora do pump — e o instrumento nao para
    //    mais. O app continua sendo um `Text`.
    final relogioDaJanela = Stopwatch()..start();
    unawaited(
      Future<void>.delayed(const Duration(milliseconds: 1300)).then((_) {
        SchedulerBinding.instance.scheduleFrame();
      }),
    );
    await janela(const Duration(milliseconds: 1500));
    relogioDaJanela.stop();
    // ignore: avoid_print
    print(
      'AO-VIVO depois de UM quadro fora do pump: ${quadros.length} quadros '
      'em 1,5 s, pedidos do app: $pedidos',
    );
    expect(
      quadros.length,
      greaterThan(60),
      reason:
          'o binding ao vivo tem de estar reagendando sozinho — se este '
          'numero caiu, a bancada mudou de comportamento e o cabecalho '
          'dela precisa ser revisto',
    );
    // E O NUMERO HONESTO NAO SE MEXE: um `Text` parado pediu um quadro,
    // e um so — os outros 250 sao do instrumento.
    expect(
      pedidos,
      lessThan(5),
      reason:
          '`hasScheduledFrame` tem de distinguir o pedido do app do '
          'reagendamento do binding; sem isso a bancada nao tem medida',
    );
  });
}
