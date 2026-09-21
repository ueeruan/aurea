import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/shape_library.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

/// NIVEL 1 DO RESET (shapes no modelo AM): biblioteca, numeros
/// animaveis, traco com tracejado animavel, Drawing Progress, pontos
/// com keyframe (morph) e o menu de adicionar com abas e trilho.
void main() {
  group('biblioteca de formas', () {
    test('toda entrada desenha algo e tem nome', () {
      expect(shapeLibrary.length, greaterThanOrEqualTo(20));
      for (final e in shapeLibrary) {
        expect(e.nome, isNotEmpty);
        final itens = e.build();
        final b = shapeLibraryPreviewPath(itens).getBounds();
        // A linha tem altura zero: basta um dos lados.
        expect(b.width > 0 || b.height > 0, isTrue, reason: e.nome);
        // Toda forma tem como se pintar: preenchimento ou traco.
        expect(
            itens.any((i) =>
                i is ShapeFill || i is ShapeGradientFill || i is ShapeStroke),
            isTrue,
            reason: e.nome);
      }
      expect(shapeLibraryIsStrokeOnly(ShapeLibrary.line()), isTrue);
      expect(shapeLibraryIsStrokeOnly(ShapeLibrary.cross()), isFalse);
    });

    test('quadrado arredondado: o raio e em % e comeca em 25', () {
      final sp = ShapeLibrary.roundedSquare().whereType<ShapeParametric>().single;
      expect(sp.kind, ParamShapeKind.rect);
      expect(sp.roundnessPercent, isTrue);
      expect(sp.roundness.base, 25);
    });
  });

  group('desenho livre', () {
    test('simplifica o rabisco e devolve um caminho aberto e suave', () {
      final pts = [
        for (var i = 0; i <= 100; i++)
          Offset(i * 3.0, (i % 2 == 0 ? 1 : -1) * 0.8 + (i > 50 ? (i - 50) * 2.0 : 0)),
      ];
      final simples = simplifyPolyline(pts, 4);
      expect(simples.length, lessThan(10));
      expect(simples.first, pts.first);
      expect(simples.last, pts.last);
      final caminho = freehandToPath(pts);
      expect(caminho.closed, isFalse);
      expect(caminho.vertices.length, simples.length);
      expect(caminho.vertices.first.inT, Offset.zero);
      expect(caminho.vertices.last.outT, Offset.zero);
      expect(caminho.vertices[1].corner, isFalse);
    });
  });

  group('controller: traco, desenhar e pontos', () {
    late ProviderContainer c;
    late String id;

    setUp(() {
      c = ProviderContainer();
      final layer = ShapeLayer(
        name: 'quadrado',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        contents: ShapeLibrary.roundedSquare(),
      );
      c.read(editorControllerProvider.notifier).openProject(VideoProject(
        name: 'p',
        createdAt: DateTime(2026, 1, 1),
        layers: [layer],
      ));
      id = layer.id;
      // O KEYFRAME AUTOMATICO NASCE DESLIGADO (`docs/keyframe-explicito.md`):
      // editar um valor num instante sem marca nao grava nada — fica de
      // pendencia esperando o losango. Estes testes falam da ANIMACAO em si
      // (a curva do raio, a formiguinha, o Drawing Progress, o morph dos
      // pontos), e nao da politica de quando a marca nasce — dessa cuidam
      // `test/keyframe_explicito_test.dart` e `test/manual_auto_key_test.dart`.
      // Por isso o interruptor e ligado DE PROPOSITO, como em
      // `manual_auto_key_test.dart`: com ele ligado a edicao crava no instante
      // editado, e so em trilha que JA esta animada (o ◈ de cada teste).
      c.read(autoKeyframeProvider.notifier).state = true;
    });

    tearDown(() => c.dispose());

    ShapeLayer camada() =>
        c.read(editorControllerProvider).layerById(id) as ShapeLayer;

    test('raio de canto animado: dois keyframes e a curva', () {
      final ctl = c.read(editorControllerProvider.notifier);
      ctl.toggleShapeParamKeyframe(id, 'roundness', Duration.zero);
      ctl.editShapeParam(id, 'roundness', const Duration(seconds: 1), 100);
      final sp = camada().contents.whereType<ShapeParametric>().single;
      expect(sp.roundness.keyframes.length, 2);
      expect(sp.roundness.valueAt(Duration.zero), 25);
      expect(sp.roundness.valueAt(const Duration(seconds: 1)), 100);
      ctl.setShapeParamSegmentEase(id, 'roundness', Duration.zero, Easing.overshoot);
      final sp2 = camada().contents.whereType<ShapeParametric>().single;
      expect(sp2.roundness.keyframes.first.ease, Easing.overshoot);
    });

    test('traco: garante uma vez so, tracejado com deslocamento animavel', () {
      final ctl = c.read(editorControllerProvider.notifier);
      final s1 = ctl.ensureShapeStroke(id);
      final s2 = ctl.ensureShapeStroke(id);
      expect(s1, isNotNull);
      expect(s1, s2, reason: 'nao duplica o traco');
      ctl.updateShapeStroke(id, (s) => s.copyWith(dashLength: AnimatedDouble(20), gapLength: AnimatedDouble(12)));
      ctl.toggleShapeItemTrackKeyframe(id, s1!, 'dashOffset', Duration.zero);
      ctl.editShapeItemTrack(id, s1, 'dashOffset', const Duration(seconds: 2), 300);
      final stroke = camada().contents.whereType<ShapeStroke>().single;
      expect(stroke.dashLength.base, 20);
      expect(stroke.dashOffset.keyframes.length, 2, reason: 'formiguinha');
      expect(stroke.dashOffset.valueAt(const Duration(seconds: 1)), 150);
      ctl.removeShapeStroke(id);
      expect(camada().contents.whereType<ShapeStroke>(), isEmpty);
    });

    test('regra 6: todo numero do traco anima, e o projeto antigo abre', () {
      final ctl = c.read(editorControllerProvider.notifier);
      final sid = ctl.ensureShapeStroke(id)!;
      // Espessura com dois keyframes: o desenho usa o valor DO TEMPO.
      ctl.toggleShapeItemTrackKeyframe(id, sid, 'width', Duration.zero);
      ctl.editShapeItemTrack(id, sid, 'width', Duration.zero, 4);
      ctl.editShapeItemTrack(id, sid, 'width', const Duration(seconds: 1), 40);
      final stroke = camada().contents.whereType<ShapeStroke>().single;
      expect(stroke.width.keyframes.length, 2);
      final meio = evaluateShape(
          camada().contents, const Duration(milliseconds: 500));
      expect(meio.last.paint.strokeWidth, closeTo(22, 0.001));

      // Os quatro numeros tem trilha; nenhum ficou como double solto.
      for (final k in ['width', 'opacity', 'dashLength', 'gapLength']) {
        expect(EditorController.shapeItemTrack(stroke, k), isNotNull,
            reason: k);
      }
      // E viram diamante na barra da camada.
      expect(camada().moduleTimesUs, contains(0));

      // Projeto salvo com os numeros soltos (antes da regra 6) ainda abre.
      final antigo = <String, dynamic>{
        'kind': 'stroke',
        'id': 'x',
        'color': 0xFFFFFFFF,
        'w': 7.0,
        'cap': 1,
        'join': 1,
        'miter': 4.0,
        'op': 0.5,
        'dash': 12.0,
        'gap': 6.0,
      };
      final lido = shapeItemFromJson(antigo) as ShapeStroke;
      expect(lido.width.base, 7);
      expect(lido.opacity.base, 0.5);
      expect(lido.dashLength.base, 12);
      expect(lido.gapLength.base, 6);
    });

    test('Drawing Progress: entra antes da pintura e anima de 0 a 100', () {
      final ctl = c.read(editorControllerProvider.notifier);
      final t1 = ctl.ensureShapeTrim(id)!;
      expect(ctl.ensureShapeTrim(id), t1);
      final itens = camada().contents;
      final iTrim = itens.indexWhere((i) => i is TrimOperator);
      final iFill = itens.indexWhere((i) => i is ShapeFill);
      expect(iTrim, lessThan(iFill), reason: 'operador antes da pintura');
      ctl.toggleShapeItemTrackKeyframe(id, t1, 'end', Duration.zero);
      ctl.editShapeItemTrack(id, t1, 'end', Duration.zero, 0);
      ctl.editShapeItemTrack(id, t1, 'end', const Duration(seconds: 1), 1);
      final trim = camada().contents.whereType<TrimOperator>().single;
      expect(trim.end.valueAt(const Duration(milliseconds: 500)), closeTo(0.5, 1e-9));
      ctl.removeShapeTrim(id);
      expect(camada().contents.whereType<TrimOperator>(), isEmpty);
    });

    test('Edit Points: a parametrica vira caminho e os pontos fazem morph', () {
      final ctl = c.read(editorControllerProvider.notifier);
      final itemId = ctl.ensureShapeBezierGeometry(id, Duration.zero);
      expect(itemId, isNotNull);
      final bez = ctl.shapeBezierOf(id, itemId!);
      expect(bez, isNotNull);
      expect(camada().contents.whereType<ShapeParametric>(), isEmpty);
      final n = bez!.path.base.vertices.length;
      expect(n, greaterThanOrEqualTo(4));

      // ◈ no tempo 0, move um ponto no tempo 1: dois keyframes, e no
      // meio o ponto esta a meio caminho.
      ctl.toggleShapeBezierKeyframe(id, itemId, Duration.zero);
      final p0 = bez.path.base.vertices.first.p;
      ctl.editShapeBezier(id, itemId, const Duration(seconds: 1),
          (c0) => BezierPath(vertices: [
                PathVertex(p: p0 + const Offset(100, 0), corner: c0.vertices.first.corner,
                    inT: c0.vertices.first.inT, outT: c0.vertices.first.outT),
                ...c0.vertices.skip(1),
              ], closed: c0.closed));
      final depois = ctl.shapeBezierOf(id, itemId)!.path;
      expect(depois.keyframes.length, 2);
      final meio = depois.valueAt(const Duration(milliseconds: 500));
      expect(meio.vertices.first.p.dx, closeTo(p0.dx + 50, 1e-6));
      expect(meio.vertices.length, n, reason: 'contagem igual: ponto a ponto');
    });

    test('forma vazia (desenho vetorial) ganha um caminho aberto', () {
      final ctl = c.read(editorControllerProvider.notifier);
      ctl.addShapeLayer(Duration.zero,
          contents: [ShapeStroke(width: AnimatedDouble(10))], name: 'Desenho');
      final nova = c.read(editorControllerProvider).layers.firstWhere((l) => l.name.startsWith('Desenho'));
      final itemId = ctl.ensureShapeBezierGeometry(nova.id, Duration.zero)!;
      final bez = ctl.shapeBezierOf(nova.id, itemId)!;
      expect(bez.path.base.vertices, isEmpty);
      expect(bez.path.base.closed, isFalse);
    });
  });

}
