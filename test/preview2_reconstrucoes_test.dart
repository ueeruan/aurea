// A REGUA DAS RECONSTRUCOES DO EDITOR (frente PREVIEW2).
//
// O relato do dono e "o preview continua lagando". Lag de preview quase
// nunca e o desenho: e a ARVORE INTEIRA sendo refeita a cada passo de
// dedo. Um slider de opacidade muta o projeto uma vez por evento de
// ponteiro; se cada mutacao reconstruir a raiz do editor (palco, timeline,
// barras, painel aberto), o gesto engasga — e engasga mais quanto maior o
// projeto.
//
// Este arquivo NAO mede tempo (tempo em host nao diz nada do celular).
// Ele conta RECONSTRUCOES, que e o numero que a correcao muda:
//
//   build.editor      a raiz do EditorScreen
//   build.palco       o PreviewStage
//   build.composicao  o CompositionView (quem desenha de verdade)
//   build.timeline    a AmTimeline
//   build.barra-lote  a BarraDoLoteNoTempo
//
// Rodar:  flutter test test/preview2_reconstrucoes_test.dart
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

VideoProject _projeto({int camadas = 6}) => VideoProject(
  name: 'regua',
  createdAt: DateTime(2026, 9, 20),
  layers: [
    for (var i = 0; i < camadas; i++)
      ShapeLayer(
        id: 'c$i',
        name: 'Camada $i',
        startTime: Duration(milliseconds: 200 * i),
        duration: const Duration(seconds: 4),
        position: AnimatedOffset(Offset(180.0 + i * 10, 220)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle),
          ShapeFill(color: const Color(0xFF3DDC97)),
        ],
      ),
  ],
);

Future<ProviderContainer> _montar(WidgetTester tester) async {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  c.read(editorControllerProvider.notifier).openProject(_projeto());
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 120));
  return c;
}

/// Conta as reconstrucoes de cada peca enquanto [acao] acontece.
Future<Map<String, int>> _durante(Future<void> Function() acao) async {
  Perfil3D.zerar();
  Perfil3D.ligado = true;
  try {
    await acao();
  } finally {
    Perfil3D.ligado = false;
  }
  final r = Perfil3D.relatorio();
  return {
    for (final k in const [
      'build.editor',
      'build.palco',
      'build.composicao',
      'build.timeline',
      'build.barra-lote',
    ])
      k: r.contas[k] ?? (r.fases[k]?.chamadas ?? 0),
  };
}

void main() {
  testWidgets('um slider nao reconstroi o editor inteiro', (tester) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final c = await _montar(tester);
    final e = c.read(editorControllerProvider.notifier);
    c.read(selectedLayerProvider.notifier).state = 'c2';
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    // 12 PASSOS DE SLIDER: e o que um dedo produz num arrasto curto.
    const passos = 12;
    final slider = await _durante(() async {
      e.beginGesture();
      for (var i = 0; i < passos; i++) {
        e.editOpacity('c2', Duration.zero, 1 - i * 0.05);
        await tester.pump(const Duration(milliseconds: 16));
      }
      e.endGesture();
      await tester.pump(const Duration(milliseconds: 16));
    });

    // O TIQUE DO RELOGIO: o cabecote anda, o projeto nao muda.
    final relogio = await _durante(() async {
      for (var i = 0; i < 12; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
    });

    // SELECIONAR OUTRA CAMADA.
    final selecao = await _durante(() async {
      c.read(selectedLayerProvider.notifier).state = 'c4';
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 16));
    });

    // ABRIR E FECHAR O PAINEL.
    final painel = await _durante(() async {
      c.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 50));
      c.read(editorSessionProvider.notifier).closePanel();
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 50));
    });

    // ARRASTAR UM CLIPE no tempo: 12 mutacoes de startTime.
    final arrasto = await _durante(() async {
      e.beginGesture();
      for (var i = 1; i <= 12; i++) {
        e.moveLayer('c2', Duration(milliseconds: 400 + i * 20));
        await tester.pump(const Duration(milliseconds: 16));
      }
      e.endGesture();
      await tester.pump(const Duration(milliseconds: 16));
    });

    // O CASO REAL: o painel ABERTO enquanto o slider anda. E assim que
    // se mexe num parametro de verdade — nunca com a folha fechada.
    c.read(selectedLayerProvider.notifier).state = 'c2';
    await tester.pump();
    c.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 80));
    final sliderComPainel = await _durante(() async {
      e.beginGesture();
      for (var i = 0; i < passos; i++) {
        e.editOpacity('c2', Duration.zero, 1 - i * 0.05);
        await tester.pump(const Duration(milliseconds: 16));
      }
      e.endGesture();
      await tester.pump(const Duration(milliseconds: 16));
    });

    final linhas = <String>[
      '',
      'RECONSTRUCOES POR CENARIO (6 camadas, 400x900)',
      '',
      'cenario                  editor   palco  composicao  timeline  lote',
      '---------------------------------------------------------------------',
    ];
    void linha(String nome, Map<String, int> m) => linhas.add(
      '${nome.padRight(24)}'
      '${m['build.editor'].toString().padLeft(6)}  '
      '${m['build.palco'].toString().padLeft(6)}  '
      '${m['build.composicao'].toString().padLeft(10)}  '
      '${m['build.timeline'].toString().padLeft(8)}  '
      '${m['build.barra-lote'].toString().padLeft(4)}',
    );
    linha('slider (12 passos)', slider);
    linha('slider + painel (12)', sliderComPainel);
    linha('arrastar clipe (12)', arrasto);
    linha('relogio (12 tiques)', relogio);
    linha('selecionar camada', selecao);
    linha('abrir/fechar painel', painel);
    // ignore: avoid_print
    print(linhas.join(String.fromCharCode(10)));

    // ===================== O QUE ESTA PRESO =========================
    //
    // A RAIZ DO EDITOR nao pode acompanhar o slider. Ela monta o palco,
    // a timeline, a barra do topo e a folha; uma reconstrucao por passo
    // de dedo e o lag inteiro do relato.
    expect(
      slider['build.editor'],
      lessThanOrEqualTo(2),
      reason: 'a raiz do editor esta seguindo cada passo do slider',
    );
    expect(
      slider['build.palco'],
      lessThanOrEqualTo(2),
      reason: 'o palco esta sendo refeito a cada passo do slider',
    );
    // O CompositionView PRECISA acompanhar: e ele quem mostra o valor
    // novo. O que nao pode e arrastar a arvore de cima junto.
    expect(
      slider['build.composicao'],
      greaterThan(0),
      reason: 'o preview parou de acompanhar o slider',
    );

    // COM O PAINEL ABERTO — o caso de verdade — a raiz tambem nao pode
    // seguir o dedo. Antes desta frente ela seguia: 12 passos davam 12
    // reconstrucoes do editor, do palco e da timeline.
    expect(
      sliderComPainel['build.editor'],
      lessThanOrEqualTo(2),
      reason: 'com o painel aberto a raiz voltou a seguir o slider',
    );
    expect(
      sliderComPainel['build.palco'],
      lessThanOrEqualTo(2),
      reason: 'com o painel aberto o palco voltou a seguir o slider',
    );
    expect(
      sliderComPainel['build.composicao'],
      greaterThan(0),
      reason: 'o preview parou de acompanhar o slider',
    );

    // ARRASTAR UM CLIPE muda o tempo, nao o desenho da raiz.
    expect(
      arrasto['build.editor'],
      lessThanOrEqualTo(2),
      reason: 'a raiz do editor esta seguindo cada passo do arrasto',
    );
    expect(
      arrasto['build.palco'],
      lessThanOrEqualTo(2),
      reason: 'o palco esta sendo refeito a cada passo do arrasto',
    );

    // O TIQUE DO RELOGIO nao muta o projeto: a raiz nao tem o que refazer.
    expect(
      relogio['build.editor'],
      lessThanOrEqualTo(1),
      reason: 'o relogio esta reconstruindo a raiz do editor',
    );

    // A BARRA DO LOTE nao existe sem lote: ela nao pode custar nada em
    // nenhum destes cenarios.
    for (final m in [slider, arrasto, relogio, selecao, painel]) {
      expect(m['build.barra-lote'], 0);
    }

    // A gravacao do projeto e adiada em 900 ms (ProjectsController):
    // sem esperar por ela o teste morre no invariante de timers.
    await tester.pump(const Duration(seconds: 2));
  });
}
