// DEPOIS DE MEXER NUM VALOR, O EDITOR TEM DE ASSENTAR.
//
// ====================== O DEFEITO QUE ESTE ARQUIVO PRENDE ==============
//
// O relato era este: a bancada media 358 quadros em 6 s de tela PARADA
// logo depois de 90 passos de giro (ui_p50 0,8 ms, raster_p50 15 ms,
// 80% de uma CPU), e so depois de um gesto — o repouso de antes dava
// zero. Numero de laco de repintura: alguem marcando repintura ou
// pedindo quadro por vsync sem reconstruir nada.
//
// A CONTA DAQUELA MEDIDA ESTAVA NO INSTRUMENTO (ver
// test/bancada_ao_vivo_test.dart e o cabecalho da bancada): o binding de
// teste ao vivo reagenda quadro sozinho depois do primeiro quadro fora
// de um `pump`. Mas a pergunta continua valendo, e e ela que este
// arquivo responde com o relogio do app: depois de editar um valor,
// NENHUM quadro pode ficar agendado com a tela parada.
//
// A sequencia e a da bancada, passo a passo: editor montado com doze
// camadas cheias de marcas, uma selecionada, 90 edicoes de valor uma por
// quadro e entao seis segundos de repouso. `hasScheduledFrame` antes de
// cada pump e o sinal: se o app quer outro quadro com nada acontecendo,
// o laco voltou.
//
// A EDICAO PENDENTE FAZ PARTE DO CENARIO, e nao e acidente: com o
// keyframe automatico desligado (o padrao), editar fora de uma marca nao
// grava — vira pendencia, e a previa passa a desenhar um projeto
// DIFERENTE do que o controlador guarda. O teste confere que a pendencia
// existe mesmo, senao ele estaria medindo outra coisa.
//
// Rodar:  flutter test test/repouso_apos_editar_test.dart
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/interacao.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
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

/// O projeto da bancada: doze camadas, com posicao, escala e giro
/// marcados a cada 300 ms. Sem marcas nao ha pendencia, e sem pendencia
/// o cenario do relato nao existe.
Future<ProviderContainer> _montar(WidgetTester tester) async {
  tester.view.physicalSize = const Size(430, 932);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);

  final container = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
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
  for (final l in container.read(editorControllerProvider).layers) {
    for (var k = 0; k < 10; k++) {
      final t = Duration(milliseconds: 300 * k);
      for (final p in const [
        LayerProp.position,
        LayerProp.scale,
        LayerProp.rotation,
      ]) {
        c.toggleKeyframe(l.id, l.startTime + t, p);
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
  await assentar(tester);
  return container;
}

/// Deixa o editor parar de pedir quadro, e devolve com ele parado.
Future<void> assentar(WidgetTester tester) => tester.pumpAndSettle(
  const Duration(milliseconds: 16),
  EnginePhase.paint,
  const Duration(seconds: 10),
);

/// Seis segundos de tela parada. Devolve quantos quadros o app pediu —
/// o numero certo e zero.
Future<int> repouso(WidgetTester tester) async {
  var pedidos = 0;
  for (var i = 0; i < 375; i++) {
    if (SchedulerBinding.instance.hasScheduledFrame) pedidos++;
    await tester.pump(const Duration(milliseconds: 16));
  }
  return pedidos;
}

void main() {
  setUp(() {
    // NOS TESTES O SINAL DA INTERACAO NASCE DESLIGADO, e com ele
    // desligado o rastro do gesto — que e exatamente o suspeito — nao
    // acontece. Aqui ele fica ligado de proposito, e `zerar` no fim
    // evita o "A Timer is still pending".
    Interacao.ligada = true;
  });
  tearDown(() {
    Interacao.zerar();
    Interacao.ligada = false;
  });

  /// Os valores que um dedo arrasta. Todos passam pela mesma porta
  /// (`_replace` -> `_mutate`), entao se o rastro voltar por um, volta
  /// por todos — e e por isso que eles estao todos aqui.
  final edicoes = <String, void Function(EditorController c, String id)>{
    'giro': (c, id) => c.editRotation(id, Duration.zero, 40),
    'posicao': (c, id) => c.editPosition(id, Duration.zero, const Offset(9, 9)),
    'escala': (c, id) => c.editScaleUniform(id, Duration.zero, 1.4),
    'opacidade': (c, id) => c.editOpacity(id, Duration.zero, .6),
  };

  for (final entrada in edicoes.entries) {
    testWidgets('90 passos de ${entrada.key}: o editor assenta depois', (
      tester,
    ) async {
      final container = await _montar(tester);
      final c = container.read(editorControllerProvider.notifier);
      final primeira = container.read(editorControllerProvider).layers.first.id;
      container.read(selectedLayerProvider.notifier).state = primeira;
      await assentar(tester);
      expect(
        SchedulerBinding.instance.hasScheduledFrame,
        isFalse,
        reason: 'selecionar uma camada ja deixa o editor pedindo quadro',
      );

      for (var i = 0; i < 90; i++) {
        entrada.value(c, primeira);
        await tester.pump(const Duration(milliseconds: 16));
      }
      // O dedo soltou: o sinal da interacao cai e sai o quadro final em
      // qualidade cheia. Dali em diante, silencio.
      for (var i = 0; i < 40; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }

      expect(
        await repouso(tester),
        0,
        reason:
            'o editor continua pedindo quadro com a tela parada depois de '
            'mexer em ${entrada.key} — o laco de repintura voltou',
      );
    });
  }

  testWidgets('com brilho na cena, mexer num valor tambem assenta', (
    tester,
  ) async {
    final container = await _montar(tester);
    final c = container.read(editorControllerProvider.notifier);
    final primeira = container.read(editorControllerProvider).layers.first.id;
    container.read(selectedLayerProvider.notifier).state = primeira;
    c.addEffect(primeira, EffectType.brilho);
    await assentar(tester);

    final efeito = container
        .read(editorControllerProvider)
        .layerById(primeira)!
        .effects
        .first;
    for (var i = 0; i < 90; i++) {
      c.editEffectParam(primeira, efeito.id, 'intensidade', Duration.zero, 1.0 + i);
      await tester.pump(const Duration(milliseconds: 16));
    }
    for (var i = 0; i < 40; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }

    expect(
      await repouso(tester),
      0,
      reason: 'o brilho + um gesto deixam o editor pedindo quadro parado',
    );
  });

  // ==================== E A EDICAO PENDENTE CONTINUA LA =================
  //
  // O cenario so vale se a edicao for mesmo recusada e virar pendencia:
  // sem isso o teste mediria uma edicao comum e o laco do relato nunca
  // passaria por aqui.
  testWidgets('o cenario e mesmo o da edicao pendente', (tester) async {
    final container = await _montar(tester);
    final c = container.read(editorControllerProvider.notifier);
    final primeira = container.read(editorControllerProvider).layers.first;
    container.read(selectedLayerProvider.notifier).state = primeira.id;
    await assentar(tester);

    expect(container.read(autoKeyframeProvider), isFalse);
    final antes = container.read(editorControllerProvider);
    c.editRotation(primeira.id, Duration.zero, 40);
    await tester.pump(const Duration(milliseconds: 16));

    expect(
      container.read(edicaoPendenteProvider),
      isNotNull,
      reason: 'a edicao fora de uma marca tem de ficar pendente',
    );
    expect(
      identical(container.read(editorControllerProvider), antes),
      isTrue,
      reason: 'a pendencia nao pode ter ido para o projeto gravado',
    );
    expect(
      identical(container.read(projetoVisivelProvider), antes),
      isFalse,
      reason: 'a previa tem de mostrar o valor que a pessoa acabou de por',
    );
    // A folga da interacao (260 ms) nao pode ficar pendente no fim.
    await tester.pump(const Duration(milliseconds: 300));
  });
}
