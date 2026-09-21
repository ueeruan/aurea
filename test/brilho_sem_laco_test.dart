// O EDITOR PARADO NAO PODE AGENDAR QUADRO — NEM COM BRILHO NA CENA.
//
// ====================== O DEFEITO QUE ESTE ARQUIVO PRENDE ==============
//
// A bancada mediu, num build de perfil no emulador, o cenario
// "2d-com-brilho-repouso" (editor parado, nada tocando, uma camada com
// brilho): 360 quadros em 6 s, 60 fps, cpu 79,6%, ui_p50 0,8 ms e
// raster_p50 15 ms. O certo e ZERO quadros.
//
// O laco era uma REALIMENTACAO: o quadro produzido era medido pelo
// gerente de desempenho, a medicao mexia na politica, a politica era
// lida pela previa (`escalaDaPrevia`, `tetoDasFotosPx`, `niveisDoBrilho`),
// a previa reconstruia e repintava — e o quadro que saia dali era medido
// de novo. Com brilho na cena o quadro custa o bastante para a escada
// balancar, e a oscilacao se sustenta sozinha com a tela parada.
//
// Rodar:  flutter test test/brilho_sem_laco_test.dart
import 'package:aurea/src/features/editor/application/desempenho/aurea_performance_manager.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
  @override
  void upsert(VideoProject project) => state = [project];
}

VideoProject _projeto() => VideoProject(
  name: 'laco',
  createdAt: DateTime(2026, 9, 20),
  layers: [
    for (var i = 0; i < 3; i++)
      ShapeLayer(
        id: 'c$i',
        name: 'Camada $i',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        position: AnimatedOffset(Offset(140.0 + 30 * i, 220)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle),
          ShapeFill(color: const Color(0xFF3DDC97)),
        ],
      ),
  ],
);

Future<ProviderContainer> _montar(
  WidgetTester tester, {
  EffectType? efeito,
}) async {
  tester.view.physicalSize = const Size(430, 932);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  final editor = c.read(editorControllerProvider.notifier);
  editor.openProject(_projeto());
  if (efeito != null) editor.addEffect('c0', efeito);
  c.read(selectedLayerProvider.notifier).state = 'c0';
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pumpAndSettle(
    const Duration(milliseconds: 16),
    EnginePhase.paint,
    const Duration(seconds: 5),
  );
  return c;
}

void main() {
  final gerente = AureaPerformanceManager.instancia;

  tearDown(() {
    PlaybackController.tocandoAgora.value = false;
    gerente.zerarMedicaoDoQuadro();
    gerente.recalcular();
  });

  /// Os efeitos com FOTO que o palco simplifica pela politica. Se um
  /// deles voltar a fechar o laco, e aqui que aparece.
  const comFoto = <EffectType>[
    EffectType.brilho, // o brilho (glow) de hoje
    EffectType.sGlowAura, // glow volumetrico
    EffectType.sGlowRings, // bloom em aneis
    EffectType.unsharpMask, // passe de nitidez (usa foto)
    EffectType.motionTile, // ladrilho (foto da camada por quadro)
  ];

  for (final efeito in [null, ...comFoto]) {
    testWidgets(
      'editor parado ${efeito == null ? 'sem efeito' : 'com ${efeito.name}'}: '
      'a medicao do quadro nao agenda outro quadro',
      (tester) async {
        await _montar(tester, efeito: efeito);
        expect(
          SchedulerBinding.instance.hasScheduledFrame,
          isFalse,
          reason: 'o editor parado ja nasce agendando quadro',
        );

        // OS QUADROS QUE A BANCADA MEDIU NO APARELHO, um a um. No
        // aparelho eles chegam sozinhos pelo `addTimingsCallback`; aqui
        // entram pela porta de teste do gerente. Nenhum deles pode
        // mexer no que a previa desenha — senao o proprio quadro
        // encomenda o proximo, e o laco esta fechado.
        final politicaAntes = gerente.politica.value;
        for (var i = 0; i < 400; i++) {
          gerente.amostraDeQuadro(40.0);
          expect(
            SchedulerBinding.instance.hasScheduledFrame,
            isFalse,
            reason:
                'medir o quadro $i agendou outro quadro com o editor parado: '
                'a politica realimenta a previa',
          );
        }
        expect(
          gerente.politica.value,
          politicaAntes,
          reason: 'a politica mudou so porque quadros foram medidos',
        );
      },
    );
  }

  // ===================== E CONTINUA MEDINDO DE VERDADE =================
  //
  // O portao de repouso nao pode virar "a escada foi desligada": com o
  // relogio andando (ou com o dedo no comando) ha motivo externo para o
  // quadro existir, e ai medir e o certo.
  testWidgets('tocando, doze quadros lentos ainda descem um degrau', (
    tester,
  ) async {
    await _montar(tester, efeito: EffectType.brilho);
    final antes = gerente.politica.value.escalaDaPrevia;
    PlaybackController.tocandoAgora.value = true;
    // Doze contam, e os oito primeiros sao a acomodacao do proprio
    // "comecou a tocar" (a politica mudou ali).
    for (var i = 0; i < 20; i++) {
      gerente.amostraDeQuadro(60);
    }
    expect(
      gerente.politica.value.escalaDaPrevia,
      lessThan(antes),
      reason: 'com motivo para o quadro existir, a escada tem de reagir',
    );
    PlaybackController.tocandoAgora.value = false;
  });

  // A ACOMODACAO: os quadros logo depois de uma politica nova sao efeito
  // dela e nao podem decidir a proxima.
  testWidgets('os quadros logo apos a politica nova nao votam', (tester) async {
    await _montar(tester, efeito: EffectType.brilho);
    PlaybackController.tocandoAgora.value = true;
    for (var i = 0; i < 20; i++) {
      gerente.amostraDeQuadro(60);
    }
    final depoisDoPrimeiro = gerente.politica.value;
    expect(depoisDoPrimeiro.escalaDaPrevia, lessThan(1.0));
    // Mais quarenta lentos SEGUIDOS: sem a acomodacao e sem a carencia
    // isto desceria outro degrau no mesmo instante.
    for (var i = 0; i < 40; i++) {
      gerente.amostraDeQuadro(60);
    }
    expect(
      gerente.politica.value,
      depoisDoPrimeiro,
      reason: 'dois degraus no mesmo instante sao o oscilador de volta',
    );
    PlaybackController.tocandoAgora.value = false;
  });
}
