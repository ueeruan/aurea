// MOVER LETRAS SOLTAS DENTRO DE UM TEXTO 3D.
//
// O giro por letra ja existia e valia para TODAS as letras de uma vez.
// Faltava o contrario, que e o que o dono pediu: empurrar SO o "C" de
// "ABCDE" no eixo Z, ou mexer so em "BCD".
//
// O QUE ESTES TESTES PRENDEM:
//
//   * um ajuste num caractere move AQUELE caractere — as outras quatro
//     letras ficam exatamente onde estavam (a prova e a matriz das cinco);
//   * um intervalo move as tres letras do intervalo, e so elas;
//   * o ajuste e uma trilha de verdade: com keyframe, a letra anda no
//     tempo pelo mesmo motor de qualquer outra propriedade do aplicativo;
//   * o ajuste sobrevive a ida e volta do arquivo, e um projeto SEM ele
//     continua abrindo;
//   * mexer numa letra NAO refaz a malha — a geometria e a mesma lista de
//     triangulos, so a pose muda.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:vector_math/vector_math_64.dart' as vm;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  final fonte = FonteTrueType.ler(
    File('assets/templates/dnyx/AureaMotionSans.ttf').readAsBytesSync(),
  );

  ModelAsset3D modelo(Texto3D t) => modeloDoTexto3DPorLetra(
    disporTexto3D(t, fonte),
    t,
    fonte.unidadesPorEm,
    t.texto,
    EstiloDoTexto3D.ouro,
  );

  /// O DESLOCAMENTO DE CADA LETRA em relacao ao lugar de layout dela.
  ///
  /// A matriz por letra ja carrega a translacao do layout dentro; o que
  /// interessa aqui e a DIFERENCA — zero quer dizer "esta letra nao saiu
  /// do lugar", que e a metade do que estes testes provam.
  List<vm.Vector3> deslocamentos(Texto3D t, [Duration q = Duration.zero]) {
    final m = modelo(t);
    final matrizes = matrizesDoTextoAnimado(m.data, q, null);
    final nodes = m.data['nodes'] as List;
    final out = <vm.Vector3>[];
    for (var i = 1; i < nodes.length; i++) {
      final tr = (nodes[i] as Map)['translation'] as List;
      final base = vm.Vector3(
        (tr[0] as num).toDouble(),
        (tr[1] as num).toDouble(),
        (tr[2] as num).toDouble(),
      );
      final mat = matrizes == null ? null : matrizes[i];
      out.add(mat == null ? vm.Vector3.zero() : mat.getTranslation() - base);
    }
    return out;
  }

  AjusteDeCaracteres ajuste(
    int inicio,
    int fim,
    MedidaDoCaractere m,
    double v,
  ) => AjusteDeCaracteres(
    inicio: inicio,
    fim: fim,
    trilhas: {m: AnimatedDouble(v)},
  );

  group('a faixa escolhida, e so ela', () {
    test('sem ajuste nenhum nao ha matriz por letra', () {
      const t = Texto3D(texto: 'ABCDE');
      expect(matrizesDoTextoAnimado(modelo(t).data, Duration.zero, null),
          isNull);
      expect(modelo(t).temAnimacaoDeTexto, isFalse);
    });

    test('empurrar SO o C no eixo Z move uma letra de cinco', () {
      final t = const Texto3D(texto: 'ABCDE').copyWith(
        ajustes: [ajuste(2, 2, MedidaDoCaractere.z, 60)],
      );
      final d = deslocamentos(t);
      expect(d.length, 5, reason: 'ABCDE tem cinco letras');
      // ignore: avoid_print
      print('DESLOCAMENTO Z POR LETRA: ${[for (final v in d) v.z]}');
      expect(d[2].z, closeTo(60, 1e-9));
      for (final i in [0, 1, 3, 4]) {
        expect(
          d[i].length,
          closeTo(0, 1e-9),
          reason: 'a letra $i nao foi escolhida e nao pode andar',
        );
      }
      // E o modelo tem de PEDIR as matrizes: sem a bandeira, a pose ficava
      // gravada e nunca era aplicada.
      expect(modelo(t).temAnimacaoDeTexto, isTrue);
    });

    test('o intervalo BCD move tres letras', () {
      final t = const Texto3D(texto: 'ABCDE').copyWith(
        ajustes: [ajuste(1, 3, MedidaDoCaractere.y, 25)],
      );
      final d = deslocamentos(t);
      final andaram = [
        for (var i = 0; i < d.length; i++)
          if (d[i].length > 1e-9) i,
      ];
      expect(andaram, [1, 2, 3]);
      for (final i in andaram) {
        expect(d[i].y, closeTo(25, 1e-9));
      }
    });

    test('todas as letras e a faixa aberta (fim negativo)', () {
      final t = const Texto3D(texto: 'ABCDE').copyWith(
        ajustes: [ajuste(0, -1, MedidaDoCaractere.x, 10)],
      );
      final d = deslocamentos(t);
      for (var i = 0; i < d.length; i++) {
        expect(d[i].x, closeTo(10, 1e-9), reason: 'letra $i');
      }
    });

    test('o espacamento abre a faixa letra a letra; o offset desliza junta', () {
      const corpo = 100.0;
      final espacado = deslocamentos(
        const Texto3D(texto: 'ABCDE', tamanho: corpo).copyWith(
          ajustes: [ajuste(1, 3, MedidaDoCaractere.espacamento, 0.1)],
        ),
      );
      // A primeira da faixa fica; as de dentro andam em passos iguais.
      expect(espacado[1].x, closeTo(0, 1e-9));
      expect(espacado[2].x, closeTo(0.1 * corpo, 1e-9));
      expect(espacado[3].x, closeTo(0.2 * corpo, 1e-9));
      expect(espacado[4].x, closeTo(0, 1e-9), reason: 'fora da faixa');

      final deslizado = deslocamentos(
        const Texto3D(texto: 'ABCDE', tamanho: corpo).copyWith(
          ajustes: [ajuste(1, 3, MedidaDoCaractere.offset, 0.25)],
        ),
      );
      for (final i in [1, 2, 3]) {
        expect(deslizado[i].x, closeTo(0.25 * corpo, 1e-9));
      }
      expect(deslizado[0].x, closeTo(0, 1e-9));
    });

    test('girar so uma letra muda a matriz dela e nenhuma outra', () {
      const parado = Texto3D(texto: 'ABCDE');
      final girado = parado.copyWith(
        ajustes: [ajuste(2, 2, MedidaDoCaractere.girY, 90)],
      );
      final antes = modelo(parado);
      final depois = matrizesDoTextoAnimado(
        modelo(girado).data,
        Duration.zero,
        null,
      )!;
      final nodes = antes.data['nodes'] as List;
      for (var i = 1; i < nodes.length; i++) {
        final tr = (nodes[i] as Map)['translation'] as List;
        final base = vm.Matrix4.identity()
          ..setTranslationRaw(
            (tr[0] as num).toDouble(),
            (tr[1] as num).toDouble(),
            (tr[2] as num).toDouble(),
          );
        final iguais = base.storage.indexed.every(
          (e) => (e.$2 - depois[i]!.storage[e.$1]).abs() < 1e-9,
        );
        expect(iguais, i != 3, reason: 'no $i (letra ${i - 1})');
      }
    });
  });

  group('o ajuste e uma trilha de verdade', () {
    test('com keyframe, a letra anda no tempo', () {
      final trilha = AnimatedDouble(0)
          .comMarcaInserida(Duration.zero, 0)
          .comMarcaInserida(const Duration(seconds: 1), 80);
      final t = const Texto3D(texto: 'ABCDE').copyWith(
        ajustes: [
          AjusteDeCaracteres(
            inicio: 2,
            fim: 2,
            trilhas: {MedidaDoCaractere.z: trilha},
          ),
        ],
      );
      expect(deslocamentos(t).elementAt(2).z, closeTo(0, 1e-9));
      expect(
        deslocamentos(t, const Duration(milliseconds: 500)).elementAt(2).z,
        closeTo(40, 1e-6),
      );
      expect(
        deslocamentos(t, const Duration(seconds: 1)).elementAt(2).z,
        closeTo(80, 1e-9),
      );
    });

    test('o ajuste sem nada gravado e inerte', () {
      expect(AjusteDeCaracteres(inicio: 0, fim: -1).inerte, isTrue);
      expect(
        AjusteDeCaracteres(
          inicio: 0,
          fim: -1,
          trilhas: {MedidaDoCaractere.escala: AnimatedDouble(1)},
        ).inerte,
        isTrue,
        reason: 'escala 1 e o repouso, nao um ajuste',
      );
      expect(ajuste(0, -1, MedidaDoCaractere.escala, 2).inerte, isFalse);
    });
  });

  group('ida e volta no arquivo', () {
    Scene3DLayer camadaCom(List<AjusteDeCaracteres> ajustes) => Scene3DLayer(
      name: 'Texto 3D',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      scene: Scene3D(
        nodes: [
          SceneNode(
            id: 'no',
            name: 'ABCDE',
            texto3d: Texto3D(texto: 'ABCDE', ajustes: ajustes),
            estiloTexto3d: EstiloDoTexto3D.cromo,
          ),
        ],
      ),
    );

    Texto3D roundTrip(Scene3DLayer l) {
      final json = jsonDecode(jsonEncode(layerToJson(l)));
      final lido = layerFromJson((json as Map).cast<String, dynamic>());
      return (lido as Scene3DLayer).scene.nodeById('no')!.texto3d!;
    }

    test('a faixa, os numeros e os keyframes voltam iguais', () {
      final trilha = AnimatedDouble(3)
          .comMarcaInserida(Duration.zero, 0)
          .comMarcaInserida(const Duration(milliseconds: 750), 42.5);
      final original = [
        AjusteDeCaracteres(
          inicio: 1,
          fim: 3,
          trilhas: {
            MedidaDoCaractere.z: trilha,
            MedidaDoCaractere.girY: AnimatedDouble(-45),
            MedidaDoCaractere.escala: AnimatedDouble(1.5),
          },
        ),
        ajuste(2, 2, MedidaDoCaractere.espacamento, 0.4),
      ];
      final lido = roundTrip(camadaCom(original));
      expect(lido.ajustes.length, 2);
      expect(lido.ajustes, original, reason: 'igualdade por valor');
      final z = lido.ajustes.first.trilha(MedidaDoCaractere.z);
      expect(z.keyframes.length, 2);
      expect(z.valueAt(const Duration(milliseconds: 750)), closeTo(42.5, 1e-9));
    });

    test('projeto SEM ajuste continua abrindo, e sem chave a toa', () {
      final json = layerToJson(camadaCom(const []));
      final bloco =
          (((json['scene'] as Map)['nodes'] as List).first
              as Map)['texto3d']
          as Map;
      expect(bloco.containsKey('ajustesDeCaracteres'), isFalse);
      expect(roundTrip(camadaCom(const [])).ajustes, isEmpty);
      // E um bloco com lixo no lugar da lista nao derruba a leitura.
      expect(ajustesDeJson('nao e lista'), isEmpty);
      expect(ajustesDeJson([1, 'x', <String, Object>{}]), isEmpty);
    });
  });

  group('o controlador nao refaz a malha para mover uma letra', () {
    late ProviderContainer container;
    late EditorController controller;

    setUp(() {
      container = ProviderContainer();
      addTearDown(container.dispose);
      controller = container.read(editorControllerProvider.notifier);
      controller.openProject(VideoProject.empty('caracteres'));
    });

    Scene3DLayer cena() => container
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .single;

    test('a geometria e a MESMA lista; so a pose muda', () async {
      final noId = (await controller.addTexto3D(
        Duration.zero,
        'ABCDE',
        EstiloDoTexto3D.ouro,
      ))!;
      final antes = cena().scene.nodeById(noId)!;
      final primitivasAntes = antes.modelAsset!.data['primitives'];

      controller.ajustarCaracteresDoTexto3D(cena().id, noId, [
        ajuste(2, 2, MedidaDoCaractere.z, 70),
      ]);

      final depois = cena().scene.nodeById(noId)!;
      expect(depois.texto3d!.ajustes.length, 1);
      expect(
        depois.modelAsset!.data['primitives'],
        same(primitivasAntes),
        reason: 'mover uma letra nao pode reconstruir um triangulo',
      );
      expect(depois.modelAsset!.temAnimacaoDeTexto, isTrue);
      final matrizes = matrizesDoTextoAnimado(
        depois.modelAsset!.data,
        Duration.zero,
        null,
      )!;
      final nodes = depois.modelAsset!.data['nodes'] as List;
      final tr = (nodes[3] as Map)['translation'] as List;
      expect(
        matrizes[3]!.getTranslation().z - (tr[2] as num).toDouble(),
        closeTo(70, 1e-9),
      );
    });

    test('zerar o ajuste devolve o projeto ao que era', () async {
      final noId = (await controller.addTexto3D(
        Duration.zero,
        'ABCDE',
        EstiloDoTexto3D.ouro,
      ))!;
      controller.ajustarCaracteresDoTexto3D(cena().id, noId, [
        ajuste(2, 2, MedidaDoCaractere.z, 70),
      ]);
      expect(cena().scene.nodeById(noId)!.texto3d!.ajustes, isNotEmpty);
      // Um ajuste no repouso nao fica gravado: nada de bloco de zeros
      // viajando no arquivo e ligando a matriz por letra a toa.
      controller.ajustarCaracteresDoTexto3D(cena().id, noId, [
        AjusteDeCaracteres(inicio: 2, fim: 2),
      ]);
      final no = cena().scene.nodeById(noId)!;
      expect(no.texto3d!.ajustes, isEmpty);
      expect(no.modelAsset!.temAnimacaoDeTexto, isFalse);
    });

    test('mudar o texto nao perde o ajuste por caractere', () async {
      final noId = (await controller.addTexto3D(
        Duration.zero,
        'ABCDE',
        EstiloDoTexto3D.ouro,
      ))!;
      controller.ajustarCaracteresDoTexto3D(cena().id, noId, [
        ajuste(2, 2, MedidaDoCaractere.z, 70),
      ]);
      final params = cena().scene.nodeById(noId)!.texto3d!;
      expect(
        await controller.editarTexto3D(
          cena().id,
          noId,
          params.copyWith(espessura: 40),
          EstiloDoTexto3D.ouro,
        ),
        isTrue,
      );
      final no = cena().scene.nodeById(noId)!;
      expect(no.texto3d!.espessura, 40);
      expect(no.texto3d!.ajustes.length, 1);
      expect(
        no.modelAsset!.temAnimacaoDeTexto,
        isTrue,
        reason: 'refazer a malha tem de levar a pose junto',
      );
    });
  });
}
