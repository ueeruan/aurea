// OS DEZ TESTES DO RELATORIO, um por um.
//
// O pedido veio numerado, com o criterio de aceite escrito em cada
// item. Este arquivo repete a numeracao de proposito: quando um deles
// quebrar, da para voltar ao relatorio e ler o que a pessoa esperava,
// com as palavras dela.
//
// A regra que todos servem esta em `docs/keyframe-explicito.md`:
//
//   SEM BOTAO DE KEYFRAME -> SEM NOVO KEYFRAME
//   COM BOTAO DE KEYFRAME -> CRIA OU ATUALIZA KEYFRAME
import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const t0 = Duration.zero;
const t1 = Duration(seconds: 1);
const t2 = Duration(seconds: 2);

({ProviderContainer c, EditorController e, String id}) _bancada() {
  final container = ProviderContainer();
  final e = container.read(editorControllerProvider.notifier);
  e.addShapeLayer(t0);
  final id = container.read(editorControllerProvider).layers.single.id;
  return (c: container, e: e, id: id);
}

Layer _l(ProviderContainer c, String id) =>
    c.read(editorControllerProvider).layerById(id)!;

/// Opacidade animada de 100% a 0% em dois segundos, do jeito explicito.
void _animarOpacidade(EditorController e, String id) {
  e.toggleKeyframe(id, t0, LayerProp.opacity);
  e.editOpacity(id, t0, 1);
  e.toggleKeyframe(id, t2, LayerProp.opacity);
  e.editOpacity(id, t2, 0);
}

void main() {
  test('TESTE 1 — alterar valor sem tocar no keyframe: nenhum keyframe', () {
    final b = _bancada();
    addTearDown(b.c.dispose);

    b.e.editOpacity(b.id, t1, .4);
    b.e.editRotation(b.id, t1, 45);
    b.e.editScaleUniform(b.id, t1, 2);
    b.e.editPosition(b.id, t1, const Offset(30, 40));

    final l = _l(b.c, b.id);
    for (final trilha in <AnimatedDouble>[
      l.opacity,
      l.rotation,
      l.rotationX,
      l.rotationY,
      l.scaleX,
      l.scaleY,
      l.positionZ,
      l.skewX,
      l.skewY,
    ]) {
      expect(trilha.keyframes, isEmpty);
    }
    expect(l.position.keyframes, isEmpty);
    expect(l.pivot.keyframes, isEmpty);
    // E os valores estao todos la, na base.
    expect(l.opacity.base, .4);
    expect(l.rotation.base, 45);
    expect(l.scaleX.base, 2);
    expect(l.position.base, const Offset(30, 40));
  });

  test('TESTE 2 — tocar no losango cria a marca com o valor de agora', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    b.e.editOpacity(b.id, t1, .4);

    b.e.toggleKeyframe(b.id, t1, LayerProp.opacity);

    final o = _l(b.c, b.id).opacity;
    expect(o.keyframes, hasLength(1));
    expect(o.keyframes.single.time, t1);
    expect(o.keyframes.single.value, .4);
  });

  test('TESTE 3 — tocar de novo SOBRE a marca atualiza, e nao duplica', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    _animarOpacidade(b.e, b.id);
    expect(_l(b.c, b.id).opacity.keyframes, hasLength(2));

    // Editar sobre a marca: atualiza aquela marca.
    b.e.editOpacity(b.id, t2, .3);
    final o = _l(b.c, b.id).opacity;
    expect(o.keyframes, hasLength(2), reason: 'nada duplicou');
    expect(o.valueAt(t2), .3);
    // E os instantes continuam sendo os mesmos dois.
    expect(o.keyframes.map((k) => k.time).toList(), [t0, t2]);
  });

  test('TESTE 4 — tocar no indicador cheio REMOVE a marca', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    _animarOpacidade(b.e, b.id);

    b.e.toggleKeyframe(b.id, t2, LayerProp.opacity);

    final o = _l(b.c, b.id).opacity;
    expect(o.keyframes, hasLength(1));
    expect(o.keyframes.single.time, t0);
  });

  test('TESTE 5 — mexer na camada (canvas) nunca cria marca sozinho', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    // Estatica: arrastar so muda a base.
    b.e.editPosition(b.id, t1, const Offset(100, 100));
    expect(_l(b.c, b.id).position.keyframes, isEmpty);

    // Animada e FORA de marca: a linha do tempo nao muda; o valor fica
    // pendente ate o losango.
    b.e.toggleKeyframe(b.id, t0, LayerProp.position);
    b.e.toggleKeyframe(b.id, t2, LayerProp.position);
    final antes = _l(b.c, b.id).position.keyframes.length;

    b.e.editPosition(b.id, t1, const Offset(999, 999));

    expect(_l(b.c, b.id).position.keyframes, hasLength(antes));
    expect(b.c.read(edicaoPendenteProvider), isNotNull);
  });

  test('TESTE 6 — mover uma marca muda o TEMPO, nunca o valor', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    _animarOpacidade(b.e, b.id);
    final valorAntes = _l(b.c, b.id).opacity.valueAt(t2);

    b.e.moverKeyframeDeTransformacao(b.id, t2, t1);

    final o = _l(b.c, b.id).opacity;
    expect(o.keyframes, hasLength(2));
    expect(o.hasKeyframeAt(t1), isTrue);
    expect(o.hasKeyframeAt(t2), isFalse);
    expect(
      o.valueAt(t1),
      valorAntes,
      reason: 'o valor viajou junto com a marca, intacto',
    );
  });

  test('TESTE 7 — o valor, a curva e as alcas sobrevivem ao movimento', () {
    // O relatorio pede COPIAR E COLAR preservando valor, relacao de
    // tempo, interpolacao e alcas. Copiar e colar keyframe nao existe
    // no app (nao ha metodo, nem gesto, nem menu) — o que existe e
    // arrastar, e e sobre ele que da para cobrar a mesma promessa.
    final b = _bancada();
    addTearDown(b.c.dispose);
    _animarOpacidade(b.e, b.id);
    b.e.setSegmentEase(b.id, LayerProp.opacity, t0, Easing.easeInOut);
    final curvaAntes = _l(b.c, b.id).opacity.easeAt(t0);

    b.e.moverKeyframeDeTransformacao(b.id, t2, t1);

    final o = _l(b.c, b.id).opacity;
    expect(o.easeAt(t0), curvaAntes, reason: 'a curva do trecho ficou');
    expect(o.valueAt(t1), 0);
    expect(o.valueAt(t0), 1);
  });

  test('TESTE 8 — aplicar curva nao cria keyframe nenhum', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    _animarOpacidade(b.e, b.id);
    final antes = _l(b.c, b.id).opacity.keyframes.map((k) => k.time).toList();

    for (final e in [
      ...Easing.bezierPresets,
      Easing.bounce,
      Easing.elastic,
      Easing.softSpring,
    ]) {
      b.e.setSegmentEase(b.id, LayerProp.opacity, t0, e);
      b.e.applyEaseToAllSegments(b.id, LayerProp.opacity, e);
    }

    final o = _l(b.c, b.id).opacity;
    expect(o.keyframes.map((k) => k.time).toList(), antes);
    expect(o.valueAt(t0), 1);
    expect(o.valueAt(t2), 0);
  });

  test('TESTE 9 — o editor de curva so edita curva de marca que existe', () {
    final b = _bancada();
    addTearDown(b.c.dispose);
    // Sem nenhuma marca, nao ha trecho: aplicar curva nao inventa um.
    b.e.setSegmentEase(b.id, LayerProp.opacity, t0, Easing.softSpring);
    expect(_l(b.c, b.id).opacity.keyframes, isEmpty);

    // Com marcas, a curva entra no TRECHO, e a contagem nao muda.
    _animarOpacidade(b.e, b.id);
    b.e.setSegmentEase(b.id, LayerProp.opacity, t0, Easing.softSpring);
    expect(_l(b.c, b.id).opacity.keyframes, hasLength(2));
    expect(_l(b.c, b.id).opacity.easeAt(t0), Easing.softSpring);
  });

  // TESTE 10 — o interruptor VOLTOU como recurso, e o proprio doc diz
  // com que condicoes: "nasce desligado, e anunciado enquanto ligado, sai
  // com um toque". Exigir a ausencia do nome nao protegia mais nada — o
  // que protege e o padrao e o anuncio, e e isso que se cobra aqui.
  group('TESTE 10 — "Auto Keyframe" so existe sob tres condicoes', () {
    test('nasce desligado', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      expect(c.read(autoKeyframeProvider), isFalse);
      // E no codigo, nao so em tempo de execucao: um `=> true` de volta
      // por descuido ja aconteceu uma vez (4cf6177).
      final fonte = File(
        'lib/src/features/editor/application/editor_controller.dart',
      ).readAsStringSync();
      expect(
        fonte,
        contains('final autoKeyframeProvider = StateProvider<bool>((ref) => false)'),
      );
    });

    test('desligado, editar nunca cria marca', () {
      final b = _bancada();
      addTearDown(b.c.dispose);
      _animarOpacidade(b.e, b.id);
      final antes = _l(b.c, b.id).opacity.keyframes.length;
      b.e.editOpacity(b.id, t1, .5);
      expect(_l(b.c, b.id).opacity.keyframes, hasLength(antes));
      expect(b.c.read(edicaoPendenteProvider), isNotNull);
    });

    test('ligado, e anunciado na tela e sai com um toque', () {
      // O anuncio mora no cabecalho do painel Transformar (a UI nova): o
      // botao fica ACESO enquanto o modo esta ligado e um toque o desliga.
      final painel = File(
        'lib/src/features/editor/presentation/ui/paineis/transformar.dart',
      ).readAsStringSync();
      expect(painel, contains("ValueKey('transformar-auto-keyframe')"));
      expect(painel, contains('ativo: ref.watch(autoKeyframeProvider)'));
      expect(painel, contains('n.state = !n.state'));
    });
  });
}
