// POLIMENTO DA BARRA DE FERRAMENTAS: com oito ferramentas espremidas
// (~50 dp) o rotulo saia "Transfor…", e no video "Borda e som…". A barra
// nao espreme abaixo de 64 (rola), os rotulos sao curtos e inteiros, e no
// maximo em duas linhas — com fonte REAL (a de teste tem 1 em por letra e
// cortaria tudo).
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/barra_contextual.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/barra_do_projeto.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderParagraph;
import 'package:flutter_test/flutter_test.dart';

import '../../apoio/print_da_ui.dart';

const _d = Duration(seconds: 2);

/// Uma camada de CADA tipo que a barra conhece.
final _camadas = <String, Layer Function()>{
  'video': () => VideoLayer(
    name: 'v',
    startTime: Duration.zero,
    duration: _d,
    sourcePath: 'v.mp4',
  ),
  'imagem': () => ImageLayer(
    name: 'i',
    startTime: Duration.zero,
    duration: _d,
    sourcePath: 'i.png',
  ),
  'texto': () =>
      TextLayer(name: 't', startTime: Duration.zero, duration: _d, text: 't'),
  'texto 3D': () => Scene3DLayer(
    name: 'T3D',
    startTime: Duration.zero,
    duration: _d,
    scene: Scene3D(nodes: [SceneNode(texto3d: const Texto3D())]),
  ),
  'cena 3D': () => Scene3DLayer(
    name: 'M3D',
    startTime: Duration.zero,
    duration: _d,
    scene: Scene3D(nodes: [SceneNode()]),
  ),
  'solido 3D': () =>
      Element3DLayer(name: 's', startTime: Duration.zero, duration: _d),
  'forma': () => ShapeLayer(name: 'f', startTime: Duration.zero, duration: _d),
  'audio': () => AudioLayer(
    name: 'a',
    startTime: Duration.zero,
    duration: _d,
    sourcePath: 'a.m4a',
  ),
  'camera': () =>
      CameraLayer(name: 'c', startTime: Duration.zero, duration: _d),
  'nulo': () => NullLayer(name: 'n', startTime: Duration.zero, duration: _d),
  'grupo': () => GroupLayer(name: 'g', startTime: Duration.zero, duration: _d),
  'ajuste': () =>
      AdjustmentLayer(name: 'aj', startTime: Duration.zero, duration: _d),
  'legenda': () =>
      CaptionLayer(name: 'l', startTime: Duration.zero, duration: _d),
  'particulas': () =>
      ParticulasLayer(name: 'p', startTime: Duration.zero, duration: _d),
};

/// Monta a barra em [largura] e confere CADA botao, rolando a fileira
/// ate ter visto todos: largura >= 64, rotulo sem reticencias, no maximo
/// duas linhas e nenhuma palavra partida ao meio.
Future<void> _conferir(
  WidgetTester tester,
  String nome,
  List<Ferramenta> ferramentas,
  double largura,
) async {
  tester.view.physicalSize = Size(largura, 300);
  tester.view.devicePixelRatio = 1;
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          // Uma barra por tipo (como na casca): a rolagem nao passa de uma
          // para a outra.
          child: BarraContextual(
            key: ValueKey('barra-$nome-$largura'),
            ferramentas: ferramentas,
            aoTocar: (_) {},
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  final vistos = <String>{};
  for (var volta = 0; volta < 20 && vistos.length < ferramentas.length; volta++) {
    for (final f in ferramentas) {
      final botao = find.byKey(ValueKey('ferramenta-${f.id}'));
      if (botao.evaluate().isEmpty) continue;
      final r = tester.getRect(botao);
      // So conta quem esta INTEIRO na tela.
      if (r.left < -.5 || r.right > largura + .5) continue;
      final onde = '$nome a ${largura.toInt()}: "${f.rotulo}"';
      expect(
        r.width,
        greaterThanOrEqualTo(AureaDims.botaoDeFerramenta),
        reason: '$onde espremido em ${r.width}',
      );
      // O icone tambem e um RichText: o rotulo e o de baixo (o ultimo).
      final p = tester.renderObject<RenderParagraph>(
        find.descendant(of: botao, matching: find.byType(RichText)).last,
      );
      expect(p.didExceedMaxLines, isFalse, reason: '$onde com reticencias');
      final estilo = (p.text as TextSpan).style;
      final texto = p.text.toPlainText();
      for (final palavra in texto.split(' ')) {
        final medida = TextPainter(
          text: TextSpan(text: palavra, style: estilo),
          textDirection: TextDirection.ltr,
          textScaler: p.textScaler,
        )..layout();
        expect(
          medida.width,
          lessThanOrEqualTo(p.size.width + .5),
          reason: '$onde: "$palavra" partida (${medida.width} > ${p.size.width})',
        );
        medida.dispose();
      }
      final umaLinha = TextPainter(
        text: TextSpan(text: 'A', style: estilo),
        textDirection: TextDirection.ltr,
        textScaler: p.textScaler,
      )..layout();
      expect(
        p.size.height,
        lessThanOrEqualTo(umaLinha.height * 2 + .5),
        reason: '$onde passou de duas linhas',
      );
      umaLinha.dispose();
      vistos.add(f.id);
    }
    if (vistos.length < ferramentas.length) {
      await tester.drag(
        find.byKey(const ValueKey('barra-contextual')),
        Offset(-largura / 2, 0),
      );
      await tester.pumpAndSettle();
    }
  }
  expect(
    vistos,
    {for (final f in ferramentas) f.id},
    reason: '$nome a ${largura.toInt()}: nem todos apareceram',
  );
  expect(tester.takeException(), isNull);
}

void main() {
  setUpAll(carregarFontesReais);

  for (final largura in [360.0, 411.0]) {
    testWidgets('todo tipo de camada a ${largura.toInt()}: nenhum rotulo '
        'cortado, nenhum botao abaixo de 64', (tester) async {
      addTearDown(tester.view.reset);
      for (final MapEntry(key: nome, value: fazer) in _camadas.entries) {
        await _conferir(tester, nome, ferramentasDa(fazer()), largura);
      }
      await _conferir(tester, 'lote', ferramentasDoLote(), largura);
      await _conferir(
        tester,
        'projeto',
        ferramentasDoProjeto(
          camadas: 3,
          temSom: true,
          temCamadaCopiada: true,
        ),
        largura,
      );
    });
  }

  test('o rotulo da borda e curto e inteiro', () {
    final video = _camadas['video']!();
    final borda = ferramentasDa(
      video,
    ).firstWhere((f) => f.abre == PainelId.bordaSombra);
    expect(borda.rotulo, 'Borda');
  });
}
