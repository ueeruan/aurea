// O PAINEL RASTREAR com uma solucao pronta, do lado de quem usa. O motor
// tem os proprios testes com verdade sintetica; o que se prova aqui e que
// a solucao vira COISA — assinatura do motor, escolha pela legenda,
// chao/origem/escala aplicados de verdade, cena criada em cima do clipe —
// e que uma analise sem assinatura (motor antigo, o fantasma do aparelho)
// e denunciada em vez de confiada.
import 'package:aurea/src/features/editor/application/camera_track_service.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/rastrear.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/paineis_3d/banco.dart';

void main() {
  /// Uma solucao com as qualidades SOB CONTROLE (quadros: 11, limiar de
  /// vistas: 4):
  ///   - id 1: EXCELENTE (o unico) — para "Origem aqui" com um toque;
  ///   - ids 2 e 3: BONS (os unicos, a 80 unidades um do outro) — para
  ///     "Escala real" com um toque;
  ///   - ids 4 e 5: fracos; ids 6 e 7: RUINS;
  ///   - ids 10..40: um chao dominante em y = 80 (fracos, de vistas
  ///     poucas) — para o "Chao automatico".
  SolucaoCamera3D solucaoDeTeste({String? motor, int? analiseMs}) {
    PoseCamera pose(int q, List<double> c, List<double> giroRad) {
      final r = rotacaoDeVetor(giroRad);
      final rc = r.aplicar(c);
      return PoseCamera(q, r, [-rc[0], -rc[1], -rc[2]]);
    }

    final nuvem = <int, List<double>>{
      1: [0, -40, 500],
      2: [-40, 0, 520],
      3: [40, 0, 520],
      4: [-60, -60, 480],
      5: [60, -60, 480],
      6: [-90, 30, 560],
      7: [90, 30, 560],
    };
    final erros = <int, double>{
      1: .5,
      2: 1.5,
      3: 1.5,
      4: 2.5,
      5: 2.5,
      6: 6,
      7: 6,
    };
    final vistas = <int, int>{for (var i = 1; i <= 7; i++) i: 8};
    final rndX = [for (var i = 0; i < 31; i++) (i * 37 % 200) - 100.0];
    final rndZ = [for (var i = 0; i < 31; i++) 400.0 + (i * 53 % 300)];
    for (var i = 0; i < 31; i++) {
      nuvem[10 + i] = [rndX[i], 80.0 + (i % 3) * .5, rndZ[i]];
      erros[10 + i] = 2.5;
      vistas[10 + i] = 3;
    }
    return SolucaoCamera3D(
      largura: 640,
      altura: 360,
      focalPx: 700,
      poses: [
        pose(0, [0, 0, 0], [0, 0, 0]),
        pose(10, [120, 8, -30], [0, -.12, 0]),
      ],
      nuvem: nuvem,
      erroPixels: 1.2,
      quadros: 11,
      fps: 24,
      errosPorPonto: erros,
      vistasPorPonto: vistas,
      pontosSeguidos: 60,
      motor: motor,
      analiseMs: analiseMs,
    );
  }

  const assinado = 'aurea_tracker2 2.0.0';

  /// O painel Rastrear aberto num clipe com [s] ja resolvida, na aba [aba]
  /// (0 Analisar, 1 Pontos).
  Future<(ProviderContainer, String)> montarPainel(
    WidgetTester tester,
    SolucaoCamera3D s, {
    int aba = 0,
  }) async {
    final c = containerNovo();
    final video = controladorDe(c).addVideoLayer(
      Duration.zero,
      'clipe.mp4',
      'Clipe',
      const Duration(seconds: 10),
    );
    CameraTrackService.instance.adotar(video, s);
    addTearDown(() => CameraTrackService.instance.clear(video));
    await montar(tester, c, (_) => PainelRastrear(layerId: video));
    if (aba > 0) await tocarNaAba(tester, 'rastrear', aba);
    return (c, video);
  }

  SolucaoCamera3D guardada(String video) =>
      CameraTrackService.instance.dataFor(video)!;

  Set<int> naMao(ProviderContainer c) => c.read(pontosDoRastreioProvider).ids;

  Future<void> tocar(WidgetTester tester, String chave) async {
    final alvo = find.byKey(ValueKey(chave));
    await tester.ensureVisible(alvo);
    await tester.pump();
    await tester.tap(alvo);
    await tester.pump();
  }

  group('analisar', () {
    testWidgets('a ficha mostra a assinatura do motor', (tester) async {
      await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado, analiseMs: 42000),
      );
      expect(find.byKey(const ValueKey('rastreio-resultado')), findsOneWidget);
      final assinatura = tester.widget<Text>(
        find.descendant(
          of: find.byKey(const ValueKey('rastreio-assinatura')),
          matching: find.byType(Text),
        ),
      );
      expect(assinatura.data, contains('Motor 2.0'));
      expect(assinatura.data, contains('42 s'));
      expect(
        find.byKey(const ValueKey('rastreio-analise-antiga')),
        findsNothing,
      );
      expect(find.byKey(const ValueKey('rastreio-criar-cena')), findsOneWidget);
    });

    testWidgets('a analise sem assinatura e denunciada', (tester) async {
      await montarPainel(tester, solucaoDeTeste());
      expect(
        find.byKey(const ValueKey('rastreio-analise-antiga')),
        findsOneWidget,
      );
      expect(find.byKey(const ValueKey('rastreio-assinatura')), findsNothing);
    });

    testWidgets('criar a cena poe a camada 3D em cima do clipe', (
      tester,
    ) async {
      final (c, video) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
      );
      await tocar(tester, 'rastreio-criar-cena');
      final projeto = c.read(editorControllerProvider);
      final cena = projeto.layers.whereType<Scene3DLayer>().single;
      final iCena = projeto.layers.indexWhere((l) => l.id == cena.id);
      final iVideo = projeto.layers.indexWhere((l) => l.id == video);
      expect(iCena, lessThan(iVideo), reason: 'a cena entra ACIMA do clipe');
      expect(cena.camera.posX.keyframes.length, 2);
      await tester.pump(const Duration(seconds: 6));
    });
  });

  group('pontos', () {
    testWidgets('tocar na legenda escolhe a qualidade inteira', (tester) async {
      final (c, _) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
        aba: 1,
      );
      expect(naMao(c), isEmpty);
      await tocar(tester, 'rastreio-qualidade-ruim');
      expect(naMao(c), {6, 7});
      // Tocar de novo desfaz.
      await tocar(tester, 'rastreio-qualidade-ruim');
      expect(naMao(c), isEmpty);
    });

    testWidgets('apagar os ruins tira fracos e ruins da solucao guardada', (
      tester,
    ) async {
      final (_, video) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
        aba: 1,
      );
      final antes = guardada(video).nuvem.length;
      await tocar(tester, 'rastreio-apagar-ruins');
      final depois = guardada(video);
      // Cairam os fracos (4, 5 e o chao de poucas vistas) e os ruins (6, 7).
      expect(depois.nuvem.length, lessThan(antes));
      expect(depois.nuvem.containsKey(6), isFalse);
      expect(depois.nuvem.containsKey(4), isFalse);
      expect(depois.nuvem.containsKey(1), isTrue);
      // A camera nao se mexe.
      expect(depois.poses.length, 2);
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('origem no ponto escolhido zera o ponto sem mexer na camera', (
      tester,
    ) async {
      final (c, video) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
        aba: 1,
      );
      // So o id 1 e excelente: um toque na legenda = um ponto na mao.
      await tocar(tester, 'rastreio-qualidade-excelente');
      expect(naMao(c), {1});
      await tocar(tester, 'rastreio-origem');
      final s = guardada(video);
      expect(s.nuvem[1]![0], closeTo(0, 1e-9));
      expect(s.nuvem[1]![1], closeTo(0, 1e-9));
      expect(s.nuvem[1]![2], closeTo(0, 1e-9));
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('escala real: dois pontos + distancia = 100 u por metro', (
      tester,
    ) async {
      final (c, video) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
        aba: 1,
      );
      // Ids 2 e 3 sao os unicos bons, a 80 unidades um do outro.
      await tocar(tester, 'rastreio-qualidade-bom');
      expect(naMao(c), {2, 3});
      await tocar(tester, 'rastreio-escala');
      await tester.pump(const Duration(milliseconds: 400));
      // O teclado do app: o campo do valor, confirmado pelo "concluir".
      await tester.enterText(find.byKey(const ValueKey('valor-campo')), '2');
      await tester.testTextInput.receiveAction(TextInputAction.done);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      final s = guardada(video);
      final a = s.nuvem[2]!, b = s.nuvem[3]!;
      expect(
        norma([a[0] - b[0], a[1] - b[1], a[2] - b[2]]),
        closeTo(2 * unidadesPorMetro, 1e-6),
        reason: 'dois metros viram 200 unidades',
      );
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('chao automatico acha o plano dominante e deita o mundo', (
      tester,
    ) async {
      final (_, video) = await montarPainel(
        tester,
        solucaoDeTeste(motor: assinado),
        aba: 1,
      );
      await tocar(tester, 'rastreio-chao-auto');
      final s = guardada(video);
      // Os pontos do chao (10..40) foram para perto de y = 0.
      var somaY = 0.0;
      var n = 0;
      for (var i = 10; i <= 40; i++) {
        final p = s.nuvem[i];
        if (p == null) continue;
        somaY += p[1];
        n++;
      }
      expect(n, greaterThan(20));
      expect((somaY / n).abs(), lessThan(2), reason: 'o chao vira y = 0');
      await tester.pump(const Duration(seconds: 6));
    });
  });
}
