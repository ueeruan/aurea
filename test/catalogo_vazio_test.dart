// O CATALOGO DE EFEITOS ESTA VAZIO (16/09, ordem do dono: apagar todos
// os efeitos do app, da aba e do codigo, para recomecar do zero).
//
// O que este teste protege e o unico ponto que nao pode ceder no meio
// do corte: um projeto salvo com efeitos tem de continuar ABRINDO. O
// efeito se perde; o projeto, nunca.
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('a aba nao tem o que oferecer', () {
    expect(effectSpecs, isEmpty);
  });

  test('id de efeito que nao existe mais devolve nulo, sem estourar', () {
    expect(effectTypeFromId('glow'), isNull);
    expect(effectTypeFromId('shake'), isNull);
    expect(effectTypeFromId('nunca_existiu'), isNull);
  });

  test('projeto salvo COM efeito ainda abre — sem o efeito', () {
    // O ARQUIVO E DE VERDADE: serializa um projeto bom e injeta nele os
    // efeitos que um aparelho com a versao antiga teria gravado.
    final p = VideoProject(
      name: 'antigo',
      createdAt: DateTime(2026, 9, 1),
      layers: [
        ShapeLayer(
          id: 'f',
          name: 'Forma',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          position: AnimatedOffset(const Offset(10, 20)),
          contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
        ),
      ],
    );
    final json = projectToJson(p);
    final camada = (json['layers'] as List).first as Map<String, dynamic>;
    camada['effects'] = [
      {'kind': 'glow', 'params': <String, dynamic>{}},
      {'kind': 'shake', 'params': <String, dynamic>{}},
    ];

    final volta = projectFromJson(json);
    expect(volta.name, 'antigo');
    expect(volta.layers, hasLength(1), reason: 'a camada nao pode se perder');
    expect(volta.layers.single.name, 'Forma');
    expect(
      volta.layers.single.effects,
      isEmpty,
      reason: 'o efeito some; o projeto fica',
    );
  });

  test('salvar e reabrir um projeto sem efeitos continua redondo', () {
    final p = VideoProject(
      name: 'novo',
      createdAt: DateTime(2026, 9, 16),
      layers: [
        ShapeLayer(
          id: 'f',
          name: 'Forma',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          position: AnimatedOffset(const Offset(10, 20)),
          contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
        ),
      ],
    );
    final v = projectFromJson(projectToJson(p));
    expect(v.layers.single.name, 'Forma');
    expect(v.layers.single.effects, isEmpty);
  });
}
