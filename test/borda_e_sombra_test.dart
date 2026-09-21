// BORDA E SOMBRA (v1.1.1): o traco da forma ganha pontas nos contornos
// abertos; qualquer camada empilha ate quatro bordas (fora, dentro ou
// centro) e tem sombra, sombra interna e brilho numa folha so.
import 'dart:ui'
    show Color, Offset;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/borda_sombra.dart'
    show nomesDasTerminacoes, novaBorda;
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Color, Offset, Size;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';


class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

ShapeLayer _forma(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.first as ShapeLayer;

ShapeStroke _traco(TerminacaoDoTraco inicio, TerminacaoDoTraco fim) =>
    ShapeStroke(
      color: const Color(0xFFFFFFFF),
      width: AnimatedDouble(4),
      inicio: inicio,
      fim: fim,
    );

/// Rola a folha ate [alvo] existir (a lista e preguicosa) e o mostra.
void main() {

  test('a lista grava a primeira borda no contorno e as outras nas extras', () {
    final a = StrokeStyle(color: const Color(0xFFFFFFFF));
    final b = StrokeStyle(
      color: const Color(0xFF000000),
      posicao: PosicaoDaBorda.dentro,
    );
    var s = comBordas(const LayerStyles(), [a, b]);
    expect(s.stroke, same(a));
    expect(s.bordasExtras, [b]);
    expect(s.bordas, [a, b]);
    expect(s.isEmpty, isFalse);
    s = comBordas(s, const []);
    expect(s.bordas, isEmpty);
    expect(s.isEmpty, isTrue);
  });

  test('a borda nova nasce por fora da ultima', () {
    final primeira = novaBorda(const [], Duration.zero);
    expect(primeira.width.base, 6);
    final segunda = novaBorda([primeira], Duration.zero);
    expect(segunda.width.base, 12);
    expect(segunda.color, isNot(primeira.color));
    final larga = novaBorda([
      StrokeStyle(width: AnimatedDouble(98), posicao: PosicaoDaBorda.centro),
    ], Duration.zero);
    expect(larga.width.base, 100, reason: 'o teto da dilatacao');
    expect(larga.posicao, PosicaoDaBorda.centro);
  });

  test('bordas, posicoes, pontas e sombra interna voltam do arquivo', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F');
    final id = _forma(c).id;
    e.ensureShapeStroke(id);
    e.updateShapeStroke(
      id,
      (s) => s.copyWith(
        inicio: TerminacaoDoTraco.setaCheia,
        fim: TerminacaoDoTraco.circuloVazado,
        tamanhoDaTerminacao: 4.5,
      ),
    );
    e.updateLayerStyles(
      id,
      (s) => comBordas(s, [
        StrokeStyle(
          color: const Color(0xFFFF0000),
          width: AnimatedDouble(5),
          posicao: PosicaoDaBorda.dentro,
        ),
        StrokeStyle(
          color: const Color(0xFF00FF00),
          width: AnimatedDouble(9),
          posicao: PosicaoDaBorda.centro,
        ),
        StrokeStyle(color: const Color(0xFF0000FF), width: AnimatedDouble(14)),
      ]).copyWith(innerShadow: ShadowStyle(spread: AnimatedDouble(3))),
    );

    final volta = projectFromJson(
      projectToJson(c.read(editorControllerProvider)),
    );
    final forma = volta.layers.whereType<ShapeLayer>().single;
    final estilos = volta.metaOf(forma.id).styles;
    expect(estilos.bordas.map((b) => b.posicao), [
      PosicaoDaBorda.dentro,
      PosicaoDaBorda.centro,
      PosicaoDaBorda.fora,
    ]);
    expect(estilos.bordas.map((b) => b.width.base), [5, 9, 14]);
    expect(estilos.bordas[1].color, const Color(0xFF00FF00));
    expect(estilos.innerShadow?.spread.base, 3);
    final traco = forma.contents.whereType<ShapeStroke>().single;
    expect(traco.inicio, TerminacaoDoTraco.setaCheia);
    expect(traco.fim, TerminacaoDoTraco.circuloVazado);
    expect(traco.tamanhoDaTerminacao, 4.5);
  });

  test('pontas so nos contornos abertos, cada uma no seu lado', () {
    final linha = ShapeBezier(
      path: AnimatedPath(
        BezierPath(
          closed: false,
          vertices: const [
            PathVertex(p: Offset(-100, 0)),
            PathVertex(p: Offset(100, 0)),
          ],
        ),
      ),
    );
    final sem = evaluateShape([
      linha,
      _traco(TerminacaoDoTraco.nenhuma, TerminacaoDoTraco.nenhuma),
    ], Duration.zero);
    final com = evaluateShape([
      linha,
      _traco(TerminacaoDoTraco.setaCheia, TerminacaoDoTraco.circuloVazado),
    ], Duration.zero);
    expect(com, hasLength(sem.length + 2));
    final inicio = com[sem.length];
    final fim = com.last;
    expect(inicio.paint.style, PaintingStyle.fill, reason: 'seta cheia');
    expect(fim.paint.style, PaintingStyle.stroke, reason: 'circulo vazado');
    expect(inicio.path.getBounds().center.dx, lessThan(-50));
    expect(fim.path.getBounds().center.dx, greaterThan(50));

    final fechado = evaluateShape([
      ...ShapePresets.paramRect().where((i) => i is! ShapeFill),
      _traco(TerminacaoDoTraco.seta, TerminacaoDoTraco.seta),
    ], Duration.zero);
    expect(fechado, hasLength(1), reason: 'retangulo nao tem ponta');

    for (final tipo in TerminacaoDoTraco.values) {
      final d = caminhoDaTerminacao(tipo, Offset.zero, const Offset(1, 0), 12);
      expect(d == null, tipo == TerminacaoDoTraco.nenhuma, reason: tipo.name);
      if (d != null) {
        expect(d.$1.getBounds().longestSide, greaterThan(0), reason: tipo.name);
      }
      expect(nomesDasTerminacoes[tipo], isNotNull, reason: tipo.name);
    }
  });

}
