// A UI NOVA DO RASTREIO 3D, do lado de quem usa: a porta (folha Cena 3D)
// e o estudio. O motor tem os proprios testes com verdade sintetica; o
// que se prova aqui e que a solucao vira COISA — ficha com assinatura,
// selecao pela legenda, chao/origem/escala aplicados de verdade, cena
// criada em cima do clipe — e que uma analise sem assinatura (motor
// antigo, o fantasma do aparelho) e denunciada em vez de confiada.
import 'package:aurea/src/features/editor/application/camera_track_service.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/estudio_do_rastreio.dart';
import 'package:aurea/src/features/editor/presentation/am/rastreio_sheet.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

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

  Future<ProviderContainer> montarEstudio(
    WidgetTester tester,
    SolucaoCamera3D s,
  ) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(VideoProject(name: 'p', createdAt: DateTime(2026)));
    e.addVideoLayer(
      Duration.zero,
      'clipe.mp4',
      'Clipe',
      const Duration(seconds: 10),
    );
    final video =
        c.read(editorControllerProvider).layers.whereType<VideoLayer>().first;
    CameraTrackService.instance.adotar(video.id, s);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: EstudioDoRastreio(layerId: video.id, solucao: s),
        ),
      ),
    );
    await tester.pump(const Duration(milliseconds: 50));
    return c;
  }

  String idDoVideo(ProviderContainer c) =>
      c.read(editorControllerProvider).layers.whereType<VideoLayer>().first.id;

  group('estudio do rastreio', () {
    testWidgets('a ficha mostra a assinatura do motor', (tester) async {
      await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0', analiseMs: 42000),
      );
      final ficha = tester.widget<Text>(
        find.byKey(const ValueKey('estudio-rastreio-ficha')),
      );
      expect(ficha.data, contains('42 s'));
      expect(ficha.data, isNot(contains('análise antiga')));
    });

    testWidgets('a analise sem assinatura e denunciada', (tester) async {
      await montarEstudio(tester, solucaoDeTeste());
      final ficha = tester.widget<Text>(
        find.byKey(const ValueKey('estudio-rastreio-ficha')),
      );
      expect(ficha.data, contains('análise antiga'));
    });

    testWidgets('tocar na legenda escolhe a qualidade inteira', (tester) async {
      await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      expect(find.byKey(const ValueKey('estudio-rastreio-selecao')),
          findsNothing);
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-ruim')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-ruim')),
      );
      await tester.pump();
      expect(find.text('2 na mão'), findsOneWidget);
      // Tocar de novo desfaz.
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-ruim')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-ruim')),
      );
      await tester.pump();
      expect(find.byKey(const ValueKey('estudio-rastreio-selecao')),
          findsNothing);
    });

    testWidgets('apagar os ruins tira fracos e ruins da solucao guardada', (
      tester,
    ) async {
      final c = await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      final antes =
          CameraTrackService.instance.dataFor(idDoVideo(c))!.nuvem.length;
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-apagar-ruins')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-apagar-ruins')),
      );
      await tester.pump();
      final depois = CameraTrackService.instance.dataFor(idDoVideo(c))!;
      // Cairam os fracos (4, 5 e o chao de poucas vistas) e os ruins (6, 7).
      expect(depois.nuvem.length, lessThan(antes));
      expect(depois.nuvem.containsKey(6), isFalse);
      expect(depois.nuvem.containsKey(4), isFalse);
      expect(depois.nuvem.containsKey(1), isTrue);
      // A camera nao se mexe.
      expect(depois.poses.length, 2);
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('criar a cena poe a camada 3D em cima do clipe', (
      tester,
    ) async {
      final c = await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-criar-cena')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-criar-cena')),
      );
      await tester.pump();
      final projeto = c.read(editorControllerProvider);
      final cena = projeto.layers.whereType<Scene3DLayer>().single;
      final iCena = projeto.layers.indexWhere((l) => l.id == cena.id);
      final iVideo = projeto.layers.indexWhere((l) => l.id == idDoVideo(c));
      expect(iCena, lessThan(iVideo), reason: 'a cena entra ACIMA do clipe');
      expect(cena.camera.posX.keyframes.length, 2);
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('origem no ponto escolhido zera o ponto sem mexer na camera', (
      tester,
    ) async {
      final c = await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      // So o id 1 e excelente: um toque na legenda = um ponto na mao.
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-excelente')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-excelente')),
      );
      await tester.pump();
      expect(find.text('1 na mão'), findsOneWidget);
      await tester.ensureVisible(find.byKey(const ValueKey('estudio-rastreio-origem')));
      await tester.pump();
      await tester.tap(find.byKey(const ValueKey('estudio-rastreio-origem')));
      await tester.pump();
      final s = CameraTrackService.instance.dataFor(idDoVideo(c))!;
      expect(s.nuvem[1]![0], closeTo(0, 1e-9));
      expect(s.nuvem[1]![1], closeTo(0, 1e-9));
      expect(s.nuvem[1]![2], closeTo(0, 1e-9));
      await tester.pump(const Duration(seconds: 6));
    });

    testWidgets('escala real: dois pontos + distancia = 100 u por metro', (
      tester,
    ) async {
      final c = await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      // Ids 2 e 3 sao os unicos bons, a 80 unidades um do outro.
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-bom')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-qualidade-bom')),
      );
      await tester.pump();
      expect(find.text('2 na mão'), findsOneWidget);
      await tester.ensureVisible(find.byKey(const ValueKey('estudio-rastreio-escala')));
      await tester.pump();
      await tester.tap(find.byKey(const ValueKey('estudio-rastreio-escala')));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      await tester.enterText(find.byType(CupertinoTextField), '2');
      await tester.tap(find.text('Aplicar'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 400));
      final s = CameraTrackService.instance.dataFor(idDoVideo(c))!;
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
      final c = await montarEstudio(
        tester,
        solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      await tester.ensureVisible(
        find.byKey(const ValueKey('estudio-rastreio-chao-auto')),
      );
      await tester.pump();
      await tester.tap(
        find.byKey(const ValueKey('estudio-rastreio-chao-auto')),
      );
      await tester.pump();
      final s = CameraTrackService.instance.dataFor(idDoVideo(c))!;
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

  group('a porta (folha Cena 3D)', () {
    Future<ProviderContainer> montarPorta(
      WidgetTester tester, {
      SolucaoCamera3D? solucao,
    }) async {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.openProject(VideoProject(name: 'p', createdAt: DateTime(2026)));
      e.addVideoLayer(
        Duration.zero,
        'clipe.mp4',
        'Clipe',
        const Duration(seconds: 10),
      );
      final video = c
          .read(editorControllerProvider)
          .layers
          .whereType<VideoLayer>()
          .first;
      if (solucao != null) {
        CameraTrackService.instance.adotar(video.id, solucao);
      }
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Consumer(
              builder: (context, ref, _) => Scaffold(
                body: Center(
                  child: GestureDetector(
                    key: const ValueKey('abrir-porta'),
                    onTap: () => showRastreioSheet(context, ref, video.id),
                    child: const Text('abrir'),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.tap(find.byKey(const ValueKey('abrir-porta')));
      // O load() da porta faz IO DE VERDADE quando nao ha cache; o
      // relogio falso do teste nao espera IO real — o runAsync espera.
      for (var i = 0; i < 20; i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 25)),
        );
        await tester.pump(const Duration(milliseconds: 60));
        if (tester.any(find.byKey(const ValueKey('rastreio-camera')))) break;
      }
      return c;
    }

    testWidgets('sem solucao: botao de rastrear e a escolha da tomada', (
      tester,
    ) async {
      await montarPorta(tester);
      expect(find.byKey(const ValueKey('rastreio-camera')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('rastreio-tomada-auto')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('rastreio-tomada-tripe')),
        findsOneWidget,
      );
      expect(find.byKey(const ValueKey('rastreio-resultado')), findsNothing);
      // A metade 2D tambem esta na porta.
      expect(find.byKey(const ValueKey('rastreio-objetos')), findsOneWidget);
      await tester.pump(const Duration(seconds: 1));
    });

    testWidgets('com solucao assinada: ficha, estudio e criar a cena', (
      tester,
    ) async {
      await montarPorta(
        tester,
        solucao: solucaoDeTeste(motor: 'aurea_tracker2 2.0.0', analiseMs: 42000),
      );
      expect(find.byKey(const ValueKey('rastreio-resultado')), findsOneWidget);
      final assinatura = tester.widget<Text>(
        find.byKey(const ValueKey('rastreio-assinatura')),
      );
      expect(assinatura.data, contains('Motor 2.0'));
      expect(assinatura.data, contains('42 s'));
      expect(
        find.byKey(const ValueKey('rastreio-abrir-estudio')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('rastreio-criar-cena')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('rastreio-analise-antiga')),
        findsNothing,
      );
      await tester.pump(const Duration(seconds: 1));
    });

    testWidgets('solucao de versao antiga e denunciada na porta', (
      tester,
    ) async {
      await montarPorta(tester, solucao: solucaoDeTeste());
      expect(
        find.byKey(const ValueKey('rastreio-analise-antiga')),
        findsOneWidget,
      );
      await tester.pump(const Duration(seconds: 1));
    });

    testWidgets('abrir o estudio pela porta chega na tela nova', (
      tester,
    ) async {
      await montarPorta(
        tester,
        solucao: solucaoDeTeste(motor: 'aurea_tracker2 2.0.0'),
      );
      // O alvo mora fundo na folha e a casca so muda de altura pela
      // alca — em teste, o que se prova aqui e a FIACAO do botao (existe
      // e empurra a tela nova); o toque fisico na casca ja e coberto
      // pelas outras interacoes desta suite.
      expect(
        find.byKey(const ValueKey('rastreio-abrir-estudio')),
        findsOneWidget,
      );
      tester
          .widget<GestureDetector>(
            find.byKey(const ValueKey('rastreio-abrir-estudio')),
          )
          .onTap!();
      for (var i = 0; i < 20; i++) {
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 25)),
        );
        await tester.pump(const Duration(milliseconds: 60));
        if (tester.any(find.byType(EstudioDoRastreio))) break;
      }
      expect(find.byType(EstudioDoRastreio), findsOneWidget);
      expect(
        find.byKey(const ValueKey('estudio-rastreio-criar-cena')),
        findsOneWidget,
      );
      await tester.pump(const Duration(seconds: 1));
    });
  });
}
