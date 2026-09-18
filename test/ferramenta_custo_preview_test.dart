import 'dart:ui' as ui;
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/passe_de_cor.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// FERRAMENTA DE MESA: quanto custa UM QUADRO DE PREVIEW.
///
/// Nao afirma nada — mede. E mede a coisa certa: o tempo gasto na THREAD
/// DE UI para montar e pintar a composicao. E esse tempo, e nao o da
/// GPU, que decide se o aplicativo responde ao dedo enquanto se edita.
/// Um quadro de 8 ms deixa o editor fluido; um de 80 ms faz a timeline
/// parecer travada mesmo que a imagem apareca.
///
/// Os casos sao os que o usuario descreveu: video, video+texto+grafico,
/// muitas camadas, 3D, e a combinacao que ele diz travar — video + 3D +
/// motion graph + efeitos.
///
/// Os numeros sao de DESKTOP. Um celular custa mais.
void main() {
  Duration t(num s) => Duration(microseconds: (s * 1000000).round());

  /// Uma animacao com curva — e o que exercita a avaliacao de motion
  /// graph a cada quadro.
  AnimatedDouble comCurva(double a, double b) => AnimatedDouble(a, [
    Keyframe(
      time: Duration.zero,
      value: a,
      ease: const Easing(x1: .2, y1: .8, x2: .4, y2: 1),
    ),
    Keyframe(
      time: t(3),
      value: b,
      ease: const Easing(x1: .6, y1: 0, x2: .9, y2: .4),
    ),
  ]);

  Future<void> medir(
    WidgetTester tester,
    String nome,
    void Function(EditorController c) montar, {
    int quadros = 24,
    bool tocando = true,
  }) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    montar(container.read(editorControllerProvider.notifier));

    late PlaybackController playback;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: _Host(
            builder: (p) {
              playback = p;
              return PreviewStage(playback: p, videos: VideoLayerManager());
            },
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    // TOCANDO e o estado em que o usuario reclama de travar, e e onde o
    // rascunho do preview vale. PARADO e a qualidade cheia — os dois
    // precisam ser medidos, senao a comparacao mistura duas variaveis.
    if (tocando) playback.play();
    // Aquece: a primeira montagem paga caches que nao se repetem.
    for (var i = 0; i < 4; i++) {
      playback.seek(t(i / 30));
      await tester.pump();
    }

    final relogio = Stopwatch()..start();
    for (var i = 0; i < quadros; i++) {
      // Cada quadro num tempo diferente: e o que o play faz, e o que
      // obriga a arvore a ser remontada de verdade.
      playback.seek(t(0.2 + i / 30));
      await tester.pump();
    }
    relogio.stop();
    if (tocando) playback.pause();

    final camadas = container.read(editorControllerProvider).layers.length;
    final ms = relogio.elapsedMicroseconds / 1000 / quadros;
    // ignore: avoid_print
    print(
      '${nome.padRight(30)} ${tocando ? 'tocando' : ' parado'} | '
      '${camadas.toString().padLeft(3)} camadas | '
      '${ms.toStringAsFixed(1).padLeft(6)} ms/quadro na thread de UI | '
      '${(1000 / ms).toStringAsFixed(0).padLeft(4)} fps de teto',
    );

    // DESMONTA A ARVORE AQUI, e nao no proximo caso.
    //
    // Trocar a arvore por outra deixa o Riverpod com um temporizador de
    // descarte pendente, e o teste termina vermelho com um erro que nao
    // tem nada a ver com a medida. Uma ferramenta que vive vermelha para
    // de servir de sinal — entao cada caso limpa o que sujou.
    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pump(const Duration(milliseconds: 50));
  }

  EffectInstance efeito(EffectType tipo) =>
      EffectInstance(type: tipo, params: const {});

  /// Troca todas as camadas de uma vez — nao ha API de uma camada so, e
  /// aqui o que importa e montar o caso, nao a rota de edicao.
  void mapear(EditorController c, Layer Function(Layer) fn) {
    final p = c.state;
    c.openProject(p.copyWith(layers: [for (final l in p.layers) fn(l)]));
  }

  testWidgets('custo do quadro de preview, caso a caso', (tester) async {
    await medir(tester, 'A · so um texto', (c) {
      c.addTextLayer(Duration.zero);
    });

    await medir(tester, 'B · texto com motion graph', (c) {
      c.addTextLayer(Duration.zero);
      mapear(
        c,
        (l) => l.copyLayer(rotation: comCurva(0, 90), opacity: comCurva(.3, 1)),
      );
    });

    await medir(tester, 'C · 10 camadas 2D', (c) {
      for (var i = 0; i < 5; i++) {
        c.addTextLayer(Duration.zero);
        c.addShapeLayer(Duration.zero);
      }
    });

    await medir(tester, 'D · 10 camadas com efeitos', (c) {
      for (var i = 0; i < 5; i++) {
        c.addTextLayer(Duration.zero);
        c.addShapeLayer(Duration.zero);
      }
      mapear(
        c,
        (l) => l.copyLayer(
          effects: [efeito(EffectType.lightGlow), efeito(EffectType.tint)],
        ),
      );
    });

    await medir(tester, 'E · elemento 3D animado', (c) {
      c.addElement3DLayer(Duration.zero, Element3DKind.cube);
      mapear(
        c,
        (l) =>
            l.copyLayer(rotation: comCurva(0, 360), rotationX: comCurva(0, 180)),
      );
    });

    await medir(tester, 'F · cena 3D', (c) {
      c.addScene3DLayer(Duration.zero);
    });

    await medir(tester, 'G · 3D + 2D + graph + efeitos', (c) {
      c.addScene3DLayer(Duration.zero);
      c.addElement3DLayer(Duration.zero, Element3DKind.cube);
      for (var i = 0; i < 3; i++) {
        c.addTextLayer(Duration.zero);
        c.addShapeLayer(Duration.zero);
      }
      mapear(
        c,
        (l) => l.copyLayer(
          rotation: comCurva(0, 45),
          opacity: comCurva(.4, 1),
          effects: l is Scene3DLayer
              ? const []
              : [efeito(EffectType.lightGlow)],
        ),
      );
    });

    await medir(tester, 'H · projeto pesado (40 camadas)', (c) {
      for (var i = 0; i < 20; i++) {
        c.addTextLayer(Duration.zero);
        c.addShapeLayer(Duration.zero);
      }
      mapear(c, (l) => l.copyLayer(rotation: comCurva(0, 30)));
    }, quadros: 12);
  });

  testWidgets('os dois estados, lado a lado', (tester) async {
    void dezComEfeitos(EditorController c) {
      for (var i = 0; i < 5; i++) {
        c.addTextLayer(Duration.zero);
        c.addShapeLayer(Duration.zero);
      }
      mapear(
        c,
        (l) => l.copyLayer(
          effects: [efeito(EffectType.lightGlow), efeito(EffectType.tint)],
        ),
      );
    }

    await medir(tester, 'D · 10 com efeitos', dezComEfeitos, tocando: false);
    await medir(tester, 'D · 10 com efeitos', dezComEfeitos, tocando: true);

    for (final tipo in [
      EffectType.filmGrain,
      EffectType.lightGlow,
      EffectType.glowVol,
    ]) {
      void caso(EditorController c) {
        for (var i = 0; i < 4; i++) {
          c.addShapeLayer(Duration.zero);
        }
        mapear(c, (l) => l.copyLayer(effects: [efeito(tipo)]));
      }

      await medir(tester, '  ${tipo.name}', caso, tocando: false, quadros: 16);
      await medir(tester, '  ${tipo.name}', caso, tocando: true, quadros: 16);
    }
  });

  testWidgets('custo POR EFEITO, com quatro camadas iguais', (tester) async {
    // A medida acima diz que o custo esta nos efeitos. Esta diz em QUAL:
    // sem isso a correcao seria chute.
    for (final tipo in [
      EffectType.tint,
      EffectType.corrections,
      EffectType.vignette,
      EffectType.filmGrain,
      EffectType.lightGlow,
      EffectType.glowVol,
      EffectType.gaussianBlur,
      EffectType.directionalBlur,
      EffectType.rgbSplit,
      EffectType.lightRays,
    ]) {
      await medir(tester, '  ${tipo.name}', (c) {
        for (var i = 0; i < 4; i++) {
          c.addShapeLayer(Duration.zero);
        }
        mapear(c, (l) => l.copyLayer(effects: [efeito(tipo)]));
      }, quadros: 16);
    }
    await medir(tester, '  (sem efeito nenhum)', (c) {
      for (var i = 0; i < 4; i++) {
        c.addShapeLayer(Duration.zero);
      }
    }, quadros: 16);
  });

  testWidgets('custo dos GLOW NOVOS (shaders/luz.frag)', (tester) async {
    // Os de cima sao do catalogo antigo. Estes nasceram depois, todos no
    // mesmo shader (estilizar_lote2.dart: receitasSapphire -> luz.frag), e
    // nunca foram medidos: o caso por efeito acima nao os inclui.
    //
    // A ABI e a mesma para todos; o que muda e quantas amostras o shader
    // dispara por pixel. Por isso a comparacao entre eles e a medida util.
    //
    // ACORDAR O SHADER ANTES DE MEDIR. Sem isto o programa e nulo,
    // `PassadaSapphire` devolve o filho intacto e a medida sai igual ao
    // controle — foi o que aconteceu na primeira tentativa. `runAsync`
    // porque carregar asset e I/O de verdade, e o relogio do teste e falso.
    await tester.runAsync(
      () => MotorSapphire.carregar('shaders/luz.frag'),
    );
    final carregou = MotorSapphire.programa('shaders/luz.frag') != null;
    // ignore: avoid_print
    print(
      'shaders/luz.frag carregado: $carregou'
      '${MotorSapphire.falha == null ? '' : ' | falha: ${MotorSapphire.falha}'}'
      ' | ImageFilter.shader suportado: ${ui.ImageFilter.isShaderFilterSupported}',
    );

    void quatro(EditorController c, EffectType tipo) {
      for (var i = 0; i < 4; i++) {
        c.addShapeLayer(Duration.zero);
      }
      mapear(c, (l) => l.copyLayer(effects: [efeito(tipo)]));
    }

    await medir(tester, '  (controle: sem efeito)', (c) {
      for (var i = 0; i < 4; i++) {
        c.addShapeLayer(Duration.zero);
      }
    }, quadros: 16);

    for (final tipo in [
      EffectType.brilho,
      EffectType.deepGlow,
      EffectType.sGlowAura,
      EffectType.sGlowDarks,
      EffectType.sGlowRings,
      EffectType.sGlint,
      EffectType.sGlintRainbow,
      EffectType.sRays,
      EffectType.sEdgeRays,
      EffectType.sSpotLight,
    ]) {
      await medir(
        tester,
        '  ${tipo.name}',
        (c) => quatro(c, tipo),
        quadros: 16,
      );
      await medir(
        tester,
        '  ${tipo.name}',
        (c) => quatro(c, tipo),
        tocando: false,
        quadros: 16,
      );
    }
  });
}

class _Host extends StatefulWidget {
  const _Host({required this.builder});

  final Widget Function(PlaybackController) builder;

  @override
  State<_Host> createState() => _HostState();
}

class _HostState extends State<_Host> with SingleTickerProviderStateMixin {
  late final PlaybackController playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      Scaffold(body: widget.builder(playback));
}
