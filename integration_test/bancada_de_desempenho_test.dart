// A BANCADA DE DESEMPENHO DO EDITOR — o numero do ANTES e do DEPOIS.
//
// Monta o editor de verdade no aparelho, com um projeto montado por codigo,
// e mede por cenario: quantos quadros o Flutter produziu, quanto custou cada
// um (montagem + rasterizacao), quanto de CPU o processo gastou e a memoria.
//
// O CENARIO QUE MAIS IMPORTA E O REPOUSO: com nada mudando na tela, o numero
// certo de PEDIDOS e ZERO. Quadro pedido em repouso e bateria e calor
// jogados fora — e o que os testadores sentem como "o celular esquenta".
//
// ================== CONTAR QUADROS EM REPOUSO MEDE O INSTRUMENTO =========
//
// `quadros=` NAO serve para julgar repouso, e isto ja custou uma cacada
// inteira. O binding de teste AO VIVO reagenda um quadro por conta propria
// toda vez que desenha um quadro que nao veio de um `pump`:
// `LiveTestWidgetsFlutterBinding.handleDrawFrame` termina em
// `platformDispatcher.scheduleFrame()` quando `_expectingFrame` e falso, e
// com `framePolicy = fullyLive` esse quadro e sempre desenhado. Basta UM
// quadro do app fora de um pump — o quadro final em qualidade cheia que sai
// 260 ms depois de um gesto, por exemplo — para a bancada ficar em 60 fps
// ATE O FIM DO TESTE, com ui_p50 ~1 ms (nada reconstroi) e raster cheio.
// Medido com `Text('a')` na tela, sem app nenhum: 0 quadros em 1,5 s depois
// de um pump, e 168 quadros na MESMA janela depois de um unico
// `scheduleFrame()` — com UM pedido do app no total
// (test/bancada_ao_vivo_test.dart prende esses numeros).
//
// Por isso o numero honesto e `pedidos=`: o app pede quadro por
// `SchedulerBinding.scheduleFrame()`, que levanta `hasScheduledFrame`; o
// reagendamento do binding vai direto ao `platformDispatcher` e NAO levanta.
// Contar a borda de subida desse sinal conta o que o app quis, e nao o que o
// instrumento produziu sozinho.
//
// Rodar (a partir do drive A:):
//   flutter drive --driver=test_driver/integration_test.dart \
//     --target=integration_test/bancada_de_desempenho_test.dart \
//     --profile -d emulator-5554
// ou, em depuracao: flutter test integration_test/bancada_de_desempenho_test.dart
import 'dart:async';
import 'dart:io';
import 'dart:ui';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/freehand_session.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

class _ProjetosNaMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject project) => state = [project];
}

/// utime + stime do processo, em tiques de relogio (100 por segundo).
int _tiquesDeCpu() {
  try {
    final s = File('/proc/self/stat').readAsStringSync();
    final campos = s.substring(s.lastIndexOf(')') + 2).split(' ');
    return int.parse(campos[11]) + int.parse(campos[12]);
  } catch (_) {
    return 0;
  }
}

/// QUANTAS VEZES O APP PEDIU UM QUADRO — o numero que julga o repouso.
///
/// O pedido do app passa por `SchedulerBinding.scheduleFrame()` e levanta
/// `hasScheduledFrame`; o reagendamento do proprio binding de teste ao vivo
/// vai direto ao `platformDispatcher` e nao levanta nada (ver o cabecalho).
/// Contar a BORDA DE SUBIDA desse sinal separa um do outro.
///
/// Duas sondas, porque um pedido nasce em dois lugares: dentro do quadro
/// (alguem se marcou sujo enquanto pintava — a assinatura do laco, o quadro
/// que encomenda o proximo) e fora dele (um Timer, uma resposta de disco).
abstract final class _PedidosDeQuadro {
  static int contados = 0;
  static bool _aberto = false;
  static Timer? _relogio;

  static void observar() {
    if (_relogio != null) return;
    SchedulerBinding.instance.addPersistentFrameCallback((_) => _olhar());
    // 4 ms: um pedido fica de pe ate o proximo vsync (16 ms), entao quatro
    // olhadas por quadro nao deixam passar — e 250 despertares por segundo
    // nao mexem na conta de CPU.
    _relogio = Timer.periodic(const Duration(milliseconds: 4), (_) => _olhar());
  }

  static void _olhar() {
    final agora = SchedulerBinding.instance.hasScheduledFrame;
    if (agora && !_aberto) contados++;
    _aberto = agora;
  }
}

class _Medida {
  _Medida(
    this.nome,
    this.segundos,
    this.quadros,
    this.pedidos,
    this.cpu,
    this.rssMb,
  );
  final String nome;
  final double segundos;
  final List<FrameTiming> quadros;

  /// Quadros que O APP pediu na janela. Em repouso tem de ser ZERO.
  final int pedidos;
  final double cpu;
  final double rssMb;

  double _pct(List<double> v, double p) {
    if (v.isEmpty) return 0;
    final o = [...v]..sort();
    return o[((o.length - 1) * p).round()];
  }

  @override
  String toString() {
    final total = [
      for (final q in quadros) q.totalSpan.inMicroseconds / 1000.0,
    ];
    final ui = [for (final q in quadros) q.buildDuration.inMicroseconds / 1000.0];
    final gpu = [
      for (final q in quadros) q.rasterDuration.inMicroseconds / 1000.0,
    ];
    final fps = quadros.length / segundos;
    String f(double x) => x.toStringAsFixed(1);
    return 'BANCADA[$nome] pedidos=$pedidos quadros=${quadros.length} '
        'em ${f(segundos)}s '
        'fps=${f(fps)} total_p50=${f(_pct(total, .5))}ms '
        'total_p90=${f(_pct(total, .9))}ms total_p99=${f(_pct(total, .99))}ms '
        'ui_p50=${f(_pct(ui, .5))}ms ui_p90=${f(_pct(ui, .9))}ms '
        'raster_p50=${f(_pct(gpu, .5))}ms raster_p90=${f(_pct(gpu, .9))}ms '
        'cpu=${f(cpu)}% rss=${f(rssMb)}MB';
  }
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  binding.framePolicy = LiveTestWidgetsFlutterBindingFramePolicy.fullyLive;

  testWidgets('bancada de desempenho do editor', (tester) async {
    _PedidosDeQuadro.observar();
    final recolhidos = <FrameTiming>[];
    void aoMedir(List<FrameTiming> lote) => recolhidos.addAll(lote);
    SchedulerBinding.instance.addTimingsCallback(aoMedir);
    addTearDown(() => SchedulerBinding.instance.removeTimingsCallback(aoMedir));

    final resultados = <_Medida>[];

    /// OS TEMPOS CHEGAM EM LOTE (ate 1 s de atraso): esvazia antes, mede, e
    /// espera o lote final. So entram os quadros cujo vsync caiu DENTRO da
    /// janela da acao, contada a partir do primeiro quadro dela.
    Future<void> medir(String nome, Future<void> Function() acao) async {
      await Future<void>.delayed(const Duration(milliseconds: 1300));
      recolhidos.clear();
      final pedidos0 = _PedidosDeQuadro.contados;
      final cpu0 = _tiquesDeCpu();
      final relogio = Stopwatch()..start();
      await acao();
      relogio.stop();
      final cpu1 = _tiquesDeCpu();
      final pedidos1 = _PedidosDeQuadro.contados;
      await Future<void>.delayed(const Duration(milliseconds: 1300));
      final seg = relogio.elapsedMicroseconds / 1e6;
      var dentro = <FrameTiming>[];
      if (recolhidos.isNotEmpty) {
        final t0 = recolhidos.first.timestampInMicroseconds(
          FramePhase.vsyncStart,
        );
        dentro = [
          for (final q in recolhidos)
            if (q.timestampInMicroseconds(FramePhase.vsyncStart) - t0 <=
                relogio.elapsedMicroseconds)
              q,
        ];
      }
      final m = _Medida(
        nome,
        seg,
        dentro,
        pedidos1 - pedidos0,
        (cpu1 - cpu0) / 100.0 / seg * 100.0,
        ProcessInfo.currentRss / (1024 * 1024),
      );
      resultados.add(m);
      // ignore: avoid_print
      print(m);
    }

    Future<void> tentar(String nome, Future<void> Function() acao) async {
      try {
        await medir(nome, acao);
      } catch (e) {
        // ignore: avoid_print
        print('BANCADA[$nome] NAO MEDIU: $e');
      }
    }

    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_ProjetosNaMemoria.new),
      ],
    );
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('bancada'));
    for (var i = 0; i < 8; i++) {
      c.addShapeLayer(Duration(milliseconds: 400 * i), name: 'Forma $i');
    }
    for (var i = 0; i < 4; i++) {
      c.addTextLayer(Duration(milliseconds: 700 * i), text: 'Legenda $i');
    }
    // MUITOS KEYFRAMES: posicao, escala e giro em cada camada, a cada 300 ms.
    for (final l in container.read(editorControllerProvider).layers) {
      for (var k = 0; k < 10; k++) {
        final t = Duration(milliseconds: 300 * k);
        for (final p in const [
          LayerProp.position,
          LayerProp.scale,
          LayerProp.rotation,
        ]) {
          try {
            c.toggleKeyframe(l.id, l.startTime + t, p);
          } catch (_) {}
        }
      }
    }
    container.read(selectedLayerProvider.notifier).state = null;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          theme: ThemeData(
            platform: TargetPlatform.iOS,
            fontFamily: 'Aurea Motion Sans',
          ),
          home: const EditorScreen(),
        ),
      ),
    );
    await tester.pump(const Duration(seconds: 2));

    Future<void> repouso() =>
        Future<void>.delayed(const Duration(seconds: 6));

    Future<void> tocar() async {
      await tester.tap(find.byKey(const ValueKey('transport-play')).first);
      await Future<void>.delayed(const Duration(seconds: 6));
      await tester.tap(find.byKey(const ValueKey('transport-play')).first);
    }

    Future<void> rolarTimeline() async {
      final alvo = find.byKey(const ValueKey('timeline-fundo'));
      for (var i = 0; i < 3; i++) {
        await tester.timedDrag(
          alvo,
          const Offset(-260, 0),
          const Duration(milliseconds: 900),
        );
        await tester.timedDrag(
          alvo,
          const Offset(260, 0),
          const Duration(milliseconds: 900),
        );
      }
    }

    await tentar('2d-repouso', repouso);
    await tentar('2d-reproducao', tocar);
    await tentar('2d-rolar-timeline', rolarTimeline);

    // COM UMA CAMADA SELECIONADA (o painel e as alcas entram em cena).
    final primeira = container.read(editorControllerProvider).layers.first.id;
    container.read(selectedLayerProvider.notifier).state = primeira;
    await tester.pump(const Duration(milliseconds: 600));
    await tentar('2d-selecionado-repouso', repouso);

    // MEXER NUM VALOR COM O RELOGIO PARADO — o que o dedo faz num slider: 90
    // passos de giro, um por quadro, numa camada COM brilho (o efeito caro).
    // E aqui que "interagir custa mais que tocar" aparece ou some.
    Future<void> mexerNoValor() async {
      for (var i = 0; i < 90; i++) {
        c.editRotation(primeira, Duration.zero, i * 2.0);
        await tester.pump(const Duration(milliseconds: 16));
      }
    }

    // ISOLAR O CULPADO EM TRES PASSOS. Mexer num valor SEM efeito e medir
    // o repouso logo depois: se sujar aqui, a culpa e do rastro do gesto.
    // Depois por o brilho e medir o repouso SEM ter tocado em nada: se
    // sujar so aqui, a culpa e do efeito. So entao repetir o gesto.
    //
    // OLHAR `pedidos=`, NUNCA `quadros=`: depois de um gesto sai um quadro
    // fora do pump (o quadro final em qualidade cheia, 260 ms depois do
    // ultimo passo) e o binding ao vivo passa a reagendar sozinho ate o fim
    // do teste — `quadros=` fica em 60 fps com o app parado. Ver o cabecalho.
    await tentar('2d-mexer-valor-sem-efeito', mexerNoValor);
    await tentar('2d-repouso-apos-mexer', repouso);

    try {
      // O BRILHO DE VERDADE, e nao o primeiro nome com "glow": o
      // `lightGlow` saiu do catalogo em 16/09 e nasce inerte, entao o
      // cenario media uma cena sem efeito nenhum.
      c.addEffect(primeira, EffectType.brilho);
    } catch (e) {
      // ignore: avoid_print
      print('BANCADA sem brilho: $e');
    }
    await tester.pump(const Duration(milliseconds: 600));
    await tentar('2d-brilho-recem-posto-repouso', repouso);
    await tentar('2d-mexer-valor-com-brilho', mexerNoValor);
    await tentar('2d-com-brilho-repouso', repouso);

    // AGORA COM TEXTO 3D (o motor nativo entra no quadro).
    Motor3D.preparar();
    if (Motor3D.pronto) {
      await c.addTexto3D(Duration.zero, 'AUREA', EstiloDoTexto3D.ouro);
      container.read(selectedLayerProvider.notifier).state = null;
      await tester.pump(const Duration(seconds: 3));
      await tentar('3d-repouso', repouso);
      await tentar('3d-reproducao', tocar);
      await tentar('3d-rolar-timeline', rolarTimeline);
      final cena = container
          .read(editorControllerProvider)
          .layers
          .whereType<Scene3DLayer>()
          .first
          .id;
      container.read(selectedLayerProvider.notifier).state = cena;
      await tester.pump(const Duration(milliseconds: 600));
      await tentar('3d-selecionado-repouso', repouso);

      // MEXER NO OBJETO 3D: o giro da camada de cena, um passo por quadro.
      Future<void> girarACena() async {
        for (var i = 0; i < 90; i++) {
          c.editRotationY(cena, Duration.zero, i * 2.0);
          await tester.pump(const Duration(milliseconds: 16));
        }
      }

      await tentar('3d-girar-a-cena', girarACena);

      // A CASCA DE CEBOLA SOBRE A CENA 3D: cinco chaves de quadro vivas ao
      // mesmo tempo. Em repouso o certo continua sendo zero quadros.
      container.read(onionSkinProvider.notifier).state = 2;
      await tester.pump(const Duration(seconds: 2));
      await tentar('3d-casca-de-cebola-repouso', repouso);
      container.read(onionSkinProvider.notifier).state = 0;
    } else {
      // ignore: avoid_print
      print('BANCADA[3d] motor indisponivel: ${Motor3D.motivo}');
    }

    // ignore: avoid_print
    print('BANCADA-FIM ${resultados.length} cenarios');
    expect(resultados, isNotEmpty);
  });
}
