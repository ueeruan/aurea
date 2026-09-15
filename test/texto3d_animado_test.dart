import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart'
    show cenaDependeDoTempo;
import 'package:aurea/src/features/editor/domain/camera3d.dart';

/// TEXTO 3D ANIMADO — o esqueleto por letra e a pilha de animadores do
/// texto normal valendo na malha extrudada, com a fonte REAL do app.

const _arquivoDaFonte = 'assets/templates/dnyx/AureaMotionSans.ttf';

FonteTrueType _fonte() =>
    FonteTrueType.ler(File(_arquivoDaFonte).readAsBytesSync());

({double minX, double maxX, double minY, double maxY}) _caixa(
  ModelFrame3D f,
) {
  var minX = double.infinity, maxX = -double.infinity;
  var minY = double.infinity, maxY = -double.infinity;
  for (final v in f.mesh.verts) {
    if (v[0] < minX) minX = v[0];
    if (v[0] > maxX) maxX = v[0];
    if (v[1] < minY) minY = v[1];
    if (v[1] > maxY) maxY = v[1];
  }
  return (minX: minX, maxX: maxX, minY: minY, maxY: maxY);
}

void main() {
  late FonteTrueType fonte;
  setUpAll(() => fonte = _fonte());

  ModelAsset3D montar(String texto) {
    const params = Texto3D();
    final comTexto = params.copyWith(texto: texto);
    return modeloDoTexto3DPorLetra(
      disporTexto3D(comTexto, fonte),
      comTexto,
      fonte.unidadesPorEm,
      texto,
      EstiloDoTexto3D.cromo,
    );
  }

  group('esqueleto por letra', () {
    test('um no por letra, pendurado na raiz, com o mapa de unidades', () {
      final m = montar('AVA');
      expect(m.nodes.length, 4); // raiz + 3 letras
      for (var i = 1; i < 4; i++) {
        expect(m.nodes[i]['parent'], 0);
      }
      final texto = m.data['texto'] as Map;
      expect(texto['u'], [0, 1, 2]);
      expect(m.temAnimacaoDeTexto, isFalse);
      expect(m.triangleCount, greaterThan(0));
    });

    test('espaco nao vira letra mas conta como unidade', () {
      final m = montar('Ola mundo');
      final texto = m.data['texto'] as Map;
      // 8 letras que desenham; o espaco e a unidade 3, pulada.
      expect(texto['u'], [0, 1, 2, 4, 5, 6, 7, 8]);
    });

    test('parado, a geometria e identica a do bloco unico', () {
      const params = Texto3D(texto: 'AVA');
      final porLetra = montar('AVA').evaluate(
        Duration.zero,
        const ModelMotion3D(clip: -1),
      );
      final bloco =
          modeloDoTexto3D(
            malhaDoTexto3D(
              disporTexto3D(params, fonte),
              params,
              fonte.unidadesPorEm,
            ),
            'AVA',
            EstiloDoTexto3D.cromo,
          ).evaluate(Duration.zero, const ModelMotion3D(clip: -1));
      expect(porLetra.mesh.verts.length, bloco.mesh.verts.length);
      final a = _caixa(porLetra), b = _caixa(bloco);
      expect(a.minX, closeTo(b.minX, 1e-9));
      expect(a.maxX, closeTo(b.maxX, 1e-9));
      expect(a.minY, closeTo(b.minY, 1e-9));
      expect(a.maxY, closeTo(b.maxY, 1e-9));
    });

    test('letra repetida compartilha os buffers da malha', () {
      final m = montar('AAA');
      final primeiras = [
        for (final p in m.primitives)
          if (p['node'] == 1) p['positions'],
      ];
      final terceiras = [
        for (final p in m.primitives)
          if (p['node'] == 3) p['positions'],
      ];
      expect(primeiras.isNotEmpty, isTrue);
      for (var i = 0; i < primeiras.length; i++) {
        expect(identical(primeiras[i], terceiras[i]), isTrue);
      }
    });
  });

  group('animacao', () {
    test('entrada "Aparecer": colapsada no zero, neutra depois', () {
      final base = montar('AVA');
      final animado = texto3DComAnims(
        base,
        [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
        fimDaCamada: const Duration(seconds: 5),
      );
      expect(animado.temAnimacaoDeTexto, isTrue);
      // Opacidade zero vira escala zero: cada letra e um ponto, a caixa
      // perde a altura.
      final t0 = _caixa(animado.evaluate(Duration.zero, const ModelMotion3D()));
      expect(t0.maxY - t0.minY, lessThan(0.05));
      // Bem depois do total (600ms + 2x55ms), tudo neutro: igual ao
      // modelo parado, vertice a vertice.
      final depois = animado.evaluate(
        const Duration(seconds: 3),
        const ModelMotion3D(),
      );
      final parado = base.evaluate(Duration.zero, const ModelMotion3D());
      expect(depois.mesh.verts.length, parado.mesh.verts.length);
      for (var i = 0; i < depois.mesh.verts.length; i += 97) {
        for (var k = 0; k < 3; k++) {
          expect(depois.mesh.verts[i][k], closeTo(parado.mesh.verts[i][k], 1e-6));
        }
      }
    });

    test('entrada "Subir" desloca as letras para baixo no comeco', () {
      final base = montar('AVA');
      final animado = texto3DComAnims(
        base,
        [TextAnim(specId: 'slideUp', slot: TextAnimSlot.entrada)],
        fimDaCamada: const Duration(seconds: 5),
      );
      final t0 = _caixa(animado.evaluate(Duration.zero, const ModelMotion3D()));
      final parado = _caixa(base.evaluate(Duration.zero, const ModelMotion3D()));
      // 90px do preset viram 75 unidades (tamanho 100 / corpo 120), que
      // a normalizacao divide pela meia-extensao — mas o sinal e o que
      // importa: composicao Y para baixo = cena Y para menos.
      expect(t0.minY, lessThan(parado.minY - 0.1));
    });

    test('saida ancora no fim gravado e no fim entregue pelo render', () {
      final base = montar('AVA');
      final animado = texto3DComAnims(
        base,
        [TextAnim(specId: 'fade', slot: TextAnimSlot.saida)],
        fimDaCamada: const Duration(seconds: 10),
      );
      // No meio da camada: neutro.
      final meio = _caixa(
        animado.evaluate(const Duration(seconds: 5), const ModelMotion3D()),
      );
      expect(meio.maxY - meio.minY, greaterThan(0.5));
      // No fim gravado: sumido.
      final fim = _caixa(
        animado.evaluate(const Duration(seconds: 10), const ModelMotion3D()),
      );
      expect(fim.maxY - fim.minY, lessThan(0.05));
      // O render sabe mais: a camada foi ENCURTADA para 6s e a saida
      // acompanha sem re-aplicar nada.
      final cortado = _caixa(
        animado.evaluate(
          const Duration(seconds: 6),
          const ModelMotion3D(),
          fimDaCamada: const Duration(seconds: 6),
        ),
      );
      expect(cortado.maxY - cortado.minY, lessThan(0.05));
      final antes = _caixa(
        animado.evaluate(
          const Duration(seconds: 3),
          const ModelMotion3D(),
          fimDaCamada: const Duration(seconds: 6),
        ),
      );
      expect(antes.maxY - antes.minY, greaterThan(0.5));
    });

    test('o cache do evaluate distingue tempo e fim da camada', () {
      final animado = texto3DComAnims(
        montar('AVA'),
        [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
        fimDaCamada: const Duration(seconds: 5),
      );
      final a = animado.evaluate(Duration.zero, const ModelMotion3D());
      final b = animado.evaluate(Duration.zero, const ModelMotion3D());
      expect(identical(a, b), isTrue);
      final c = animado.evaluate(
        const Duration(milliseconds: 400),
        const ModelMotion3D(),
      );
      expect(identical(a, c), isFalse);
    });

    test('a pose neutra sob demanda ignora a animacao', () {
      final base = montar('AVA');
      final animado = texto3DComAnims(
        base,
        [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
        fimDaCamada: const Duration(seconds: 5),
      );
      final neutro = _caixa(
        animado.evaluate(
          Duration.zero,
          const ModelMotion3D(),
          comAnimacaoDeTexto: false,
        ),
      );
      expect(neutro.maxY - neutro.minY, greaterThan(0.5));
    });
  });

  group('persistencia e cena', () {
    test('as animacoes viajam no data do modelo em JSON puro', () {
      final animado = texto3DComAnims(
        montar('AVA'),
        [
          TextAnim(specId: 'bounceLetter', slot: TextAnimSlot.entrada),
          TextAnim(specId: 'wave', slot: TextAnimSlot.enfase),
        ],
        fimDaCamada: const Duration(seconds: 7),
      );
      final ida = jsonEncode(animado.data);
      final volta = ModelAsset3D(
        (jsonDecode(ida) as Map).cast<String, dynamic>(),
      );
      expect(volta.temAnimacaoDeTexto, isTrue);
      final anims = animsDoTexto3D(volta);
      expect(anims.length, 2);
      expect(anims.first.specId, 'bounceLetter');
      expect(anims.last.slot, TextAnimSlot.enfase);
      // E a caixa anda igual a do modelo original.
      final a = _caixa(volta.evaluate(Duration.zero, const ModelMotion3D()));
      expect(a.maxY - a.minY, lessThan(0.6)); // entrada mexendo no zero
    });

    test('anim gravada com spec desconhecida e ignorada sem quebrar', () {
      expect(textAnimDeJson({'spec': 'naoExiste'}), isNull);
      expect(textAnimDeJson({'spec': 42}), isNull);
    });

    test('texto animado faz a cena depender do tempo', () {
      final parado = Scene3D(
        nodes: [SceneNode(name: 'T', modelAsset: montar('AVA'))],
      );
      expect(cenaDependeDoTempo(parado, Camera3D()), isFalse);
      final vivo = Scene3D(
        nodes: [
          SceneNode(
            name: 'T',
            modelAsset: texto3DComAnims(
              montar('AVA'),
              [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
              fimDaCamada: const Duration(seconds: 5),
            ),
          ),
        ],
      );
      expect(cenaDependeDoTempo(vivo, Camera3D()), isTrue);
    });
  });
}
