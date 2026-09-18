// QA 1.0 — CAMPANHA 5: A LINHA DO TEMPO SOB CARGA, E AS CAMADAS.
//
// Um projeto de teste tem tres camadas. Um projeto de verdade tem
// cinquenta, e e nele que as coisas quebram: id repetido depois de
// duplicar em massa, ordem que se embaralha ao reordenar, grupo que nao
// desfaz o que fez, camada travada que ainda assim se mexe, olho fechado
// que continua aparecendo no video exportado.
//
// Esta campanha faz o que o usuario faz quando o projeto cresce, e cobra
// invariantes que nao podem falhar em nenhum tamanho:
//
//   - ID UNICO: duas camadas com o mesmo id significam uma camada que
//     some ao salvar.
//   - ORDEM PRESERVADA: reordenar move UMA camada, nao embaralha as
//     outras.
//   - COPIA INDEPENDENTE: mexer na copia nao pode mexer no original.
//   - IDA E VOLTA do grupo: agrupar e desagrupar devolve o que havia.
//   - OLHO E SOLO valem TAMBEM na exportacao, nao so no preview.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late ProviderContainer container;
  EditorController ctrl() => container.read(editorControllerProvider.notifier);
  VideoProject projeto() => container.read(editorControllerProvider);

  setUp(() {
    container = ProviderContainer();
    ctrl().openProject(
      VideoProject(
        name: 'qa',
        createdAt: DateTime(2026, 9, 8),
        aspectRatio: 1,
        resolutionHeight: 128,
        layers: const [],
      ),
    );
  });
  tearDown(() => container.dispose());

  /// Enche a linha do tempo com [n] camadas, uma a cada meio segundo.
  List<String> encher(int n) {
    final ids = <String>[];
    ctrl().runAsOneUndo(() {
      for (var i = 0; i < n; i++) {
        ctrl().addShapeLayer(Duration(milliseconds: 500 * i), name: 'Forma $i');
      }
    });
    for (final l in projeto().layers) {
      ids.add(l.id);
    }
    return ids;
  }

  void semIdRepetido(VideoProject p, String quando) {
    final vistos = <String>{};
    final repetidos = <String>[];
    void varrer(Iterable<Layer> camadas) {
      for (final l in camadas) {
        if (!vistos.add(l.id)) repetidos.add('${l.name} (${l.id})');
        if (l is GroupLayer) varrer(l.children);
      }
    }

    varrer(p.layers);
    expect(repetidos, isEmpty, reason: 'id repetido $quando: $repetidos');
  }

  group('a linha do tempo cheia', () {
    for (final n in const [10, 50, 100]) {
      test('$n camadas: criar, salvar, reabrir e conferir uma a uma', () {
        encher(n);
        expect(projeto().layers, hasLength(n));
        semIdRepetido(projeto(), 'ao criar $n');

        final volta = projectFromJson(projectToJson(projeto()));
        expect(volta.layers, hasLength(n), reason: 'sumiu camada na volta');
        semIdRepetido(volta, 'depois de reabrir $n');
        for (var i = 0; i < n; i++) {
          expect(
            volta.layers[i].id,
            projeto().layers[i].id,
            reason: 'a ordem mudou na posicao $i',
          );
          expect(
            volta.layers[i].startTime,
            projeto().layers[i].startTime,
            reason: 'o inicio mudou na posicao $i',
          );
        }
      });
    }

    test('100 camadas: reordenar move UMA e nao embaralha o resto', () {
      encher(100);
      final antes = [for (final l in projeto().layers) l.id];
      final alvo = antes[42];
      ctrl().reorderLayer(alvo, 7);
      final depois = [for (final l in projeto().layers) l.id];

      expect(depois, hasLength(100));
      expect(depois.toSet(), antes.toSet(), reason: 'alguem sumiu ou nasceu');
      expect(depois.indexOf(alvo), 49, reason: 'a camada nao foi ao lugar');
      // Tirando a que se moveu, a ordem relativa das outras 99 e a mesma.
      expect(
        depois.where((id) => id != alvo).toList(),
        antes.where((id) => id != alvo).toList(),
        reason: 'reordenar embaralhou as vizinhas',
      );
    });

    test('100 camadas: apagar em massa nao deixa sobra nem buraco de id', () {
      final ids = encher(100);
      ctrl().removeLayers(ids.take(30));
      expect(projeto().layers, hasLength(70));
      for (final id in ids.take(30)) {
        expect(
          projeto().layerById(id),
          isNull,
          reason: 'a camada $id ficou depois de apagada',
        );
      }
      semIdRepetido(projeto(), 'depois de apagar 30');
      // E o desfazer devolve as trinta de uma vez.
      ctrl().undo();
      expect(projeto().layers, hasLength(100));
    });

    test('cortar uma camada em 20 pedacos: um undo, sem sobreposicao', () {
      ctrl().addShapeLayer(Duration.zero, name: 'Longa');
      final id = projeto().layers.single.id;
      final longa = projeto().layers.single;
      final tempos = [
        for (var i = 1; i <= 20; i++)
          longa.startTime + longa.duration * (i / 21),
      ];
      final pedacos = ctrl().splitLayerAtTimes(id, tempos);
      expect(pedacos.length, greaterThan(1), reason: 'nao cortou');
      semIdRepetido(projeto(), 'depois de cortar');

      // Os pedacos cobrem o mesmo tempo do original, sem se pisarem.
      final ordenados = [...projeto().layers]
        ..sort((a, b) => a.startTime.compareTo(b.startTime));
      for (var i = 1; i < ordenados.length; i++) {
        expect(
          ordenados[i].startTime.inMicroseconds,
          greaterThanOrEqualTo(ordenados[i - 1].endTime.inMicroseconds - 1),
          reason: 'o pedaco $i comeca antes de o anterior acabar',
        );
      }
      expect(
        ordenados.last.endTime.inMicroseconds,
        closeTo(longa.endTime.inMicroseconds, 2000),
        reason: 'o fim do ultimo pedaco nao e o fim do clipe',
      );

      ctrl().undo();
      expect(
        projeto().layers,
        hasLength(1),
        reason: 'um comando de corte tem de ser UM passo de desfazer',
      );
    });
  });

  group('duplicar e copiar', () {
    test('duplicar cada tipo de camada da uma copia INDEPENDENTE', () {
      final falhas = <String>[];
      void conferir(String tipo, void Function() criar) {
        ctrl().openProject(
          VideoProject(name: 'q', createdAt: DateTime(2026), layers: const []),
        );
        criar();
        if (projeto().layers.isEmpty) {
          falhas.add('$tipo: nem criou');
          return;
        }
        final original = projeto().layers.first;
        ctrl().duplicateLayer(original.id);
        if (projeto().layers.length != 2) {
          falhas.add('$tipo: nao duplicou');
          return;
        }
        final copia = projeto().layers.firstWhere((l) => l.id != original.id);
        if (copia.id == original.id) falhas.add('$tipo: id igual ao original');
        if (copia.runtimeType != original.runtimeType) {
          falhas.add('$tipo: a copia virou ${copia.runtimeType}');
        }
        // Mexer na copia nao pode mexer no original: se as duas
        // apontarem para a mesma lista de efeitos ou para a mesma cena,
        // editar uma muda a outra e a pessoa nao entende o que houve.
        ctrl().editOpacity(copia.id, Duration.zero, 0.123);
        ctrl().renameLayer(copia.id, 'COPIA');
        final origDepois = projeto().layerById(original.id)!;
        if ((origDepois.opacity.valueAt(Duration.zero) - 0.123).abs() < 1e-6) {
          falhas.add('$tipo: editar a copia mudou o ORIGINAL');
        }
        if (origDepois.name == 'COPIA') {
          falhas.add('$tipo: renomear a copia renomeou o original');
        }
        semIdRepetido(projeto(), 'ao duplicar $tipo');
      }

      conferir('texto', () => ctrl().addTextLayer(Duration.zero, text: 'oi'));
      conferir('forma', () => ctrl().addShapeLayer(Duration.zero));
      conferir('nulo', () => ctrl().addNullLayer(Duration.zero));
      conferir('ajuste', () => ctrl().addAdjustmentLayer(Duration.zero));
      conferir('particulas', () => ctrl().addParticulasLayer(Duration.zero));
      conferir('cena3d', () => ctrl().addScene3DLayer(Duration.zero));
      conferir(
        'elemento3d',
        () => ctrl().addElement3DLayer(Duration.zero, Element3DKind.cube),
      );
      conferir('icone', () {
        ctrl().addIconLayer(Duration.zero, 'M0 0 L10 0 L10 10 Z', 'Icone');
      });
      expect(falhas, isEmpty, reason: falhas.join('\n'));
    });

    test('duplicar uma cena 3D nao compartilha os objetos', () {
      ctrl().addScene3DLayer(Duration.zero);
      final cena = projeto().layers.whereType<Scene3DLayer>().single;
      ctrl().addSceneNode(cena.id, Element3DKind.sphere);
      ctrl().duplicateLayer(cena.id);
      final copia = projeto().layers.whereType<Scene3DLayer>().firstWhere(
        (l) => l.id != cena.id,
      );
      expect(copia.scene.nodes, hasLength(1));
      ctrl().addSceneNode(copia.id, Element3DKind.cube);
      expect(
        projeto().layerById(cena.id),
        isA<Scene3DLayer>().having(
          (l) => l.scene.nodes.length,
          'objetos da cena original',
          1,
        ),
        reason: 'por um objeto na copia e ele apareceu no original',
      );
    });
  });

  group('agrupar, travar, esconder e solo', () {
    test('agrupar e desagrupar devolve as mesmas camadas, no lugar', () {
      final ids = encher(4);
      final antes = {
        for (final l in projeto().layers)
          l.name: (l.startTime, l.duration, l.runtimeType),
      };
      ctrl().groupLayers(ids.take(3).toList());
      final grupos = projeto().layers.whereType<GroupLayer>().toList();
      expect(grupos, hasLength(1), reason: 'nao virou um grupo');
      expect(grupos.single.children, hasLength(3));
      expect(projeto().layers, hasLength(2), reason: 'sobrou camada solta');

      ctrl().ungroupLayer(grupos.single.id);
      expect(
        projeto().layers.whereType<GroupLayer>(),
        isEmpty,
        reason: 'o grupo continua la depois de desagrupar',
      );
      expect(projeto().layers, hasLength(4));
      for (final l in projeto().layers) {
        final velho = antes[l.name];
        expect(velho, isNotNull, reason: 'a camada ${l.name} nao existia');
        expect(
          (l.startTime, l.duration, l.runtimeType),
          velho,
          reason: 'a camada ${l.name} voltou diferente do grupo',
        );
      }
      semIdRepetido(projeto(), 'depois de desagrupar');
    });

    test('o estado de travada/escondida/solo sobrevive ao arquivo', () {
      final ids = encher(3);
      ctrl().toggleLocked(ids[0]);
      ctrl().toggleHidden(ids[1]);
      ctrl().toggleSolo(ids[2]);
      final volta = projectFromJson(projectToJson(projeto()));
      expect(volta.metaOf(ids[0]).locked, isTrue, reason: 'destravou sozinha');
      expect(volta.metaOf(ids[1]).hidden, isTrue, reason: 'reapareceu sozinha');
      expect(volta.metaOf(ids[2]).solo, isTrue, reason: 'perdeu o solo');
      expect(volta.hasSolo, isTrue);
      expect(
        volta.rendersInPreview(ids[0]),
        isFalse,
        reason: 'havendo solo, so o solo desenha',
      );
    });

    test('apagar a camada em solo tira o solo do projeto', () {
      final ids = encher(3);
      ctrl().toggleSolo(ids[0]);
      expect(projeto().hasSolo, isTrue);
      ctrl().removeLayer(ids[0]);
      // Um solo orfao esconderia TODAS as camadas restantes: a tela fica
      // preta e nao ha botao para desfazer isso, porque a camada do solo
      // nao existe mais.
      final visiveis = [
        for (final l in projeto().layers)
          if (projeto().rendersInPreview(l.id)) l.id,
      ];
      expect(
        visiveis,
        hasLength(2),
        reason: 'apagar o solo deixou o projeto invisivel',
      );
    });
  });

  group('o que esta escondido nao sai no video', () {
    Future<int> pixelsVerdes(WidgetTester tester, VideoProject p) async {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      c.read(editorControllerProvider.notifier).openProject(p);
      final tempo = ValueNotifier(const Duration(milliseconds: 200));
      addTearDown(tempo.dispose);
      final videos = VideoLayerManager();
      addTearDown(videos.dispose);
      final chave = GlobalKey();
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Center(
              child: RepaintBoundary(
                key: chave,
                child: SizedBox(
                  width: 128,
                  height: 128,
                  child: ColoredBox(
                    color: const Color(0xFF000000),
                    child: CompositionView(
                      time: tempo,
                      videos: videos,
                      selectedId: null,
                      exporting: true,
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 16));
      final boundary =
          chave.currentContext!.findRenderObject() as RenderRepaintBoundary;
      // Ler os PIXELS e trabalho de verdade, fora do relogio falso do
      // teste: sem runAsync, o `await` nunca volta e o teste fica
      // pendurado ate o limite de dez minutos.
      late ByteData bytes;
      await tester.runAsync(() async {
        final imagem = await boundary.toImage();
        bytes = (await imagem.toByteData(format: ui.ImageByteFormat.rawRgba))!;
        imagem.dispose();
      });
      var verdes = 0;
      for (var i = 0; i + 3 < bytes.lengthInBytes; i += 4) {
        final r = bytes.getUint8(i);
        final g = bytes.getUint8(i + 1);
        final b = bytes.getUint8(i + 2);
        if (g > 120 && g > r + 40 && g > b + 40) verdes++;
      }
      return verdes;
    }

    VideoProject comOlho({required bool escondida}) {
      final forma = ShapeLayer(
        id: 'verde',
        name: 'Verde',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        position: AnimatedOffset(const Offset(64, 64)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle),
          ShapeFill(color: const Color(0xFF22DD55)),
        ],
      );
      return VideoProject(
        name: 'olho',
        createdAt: DateTime(2026, 9, 8),
        aspectRatio: 1,
        resolutionHeight: 128,
        backgroundColor: const Color(0xFF000000),
        layers: [forma],
        meta: escondida
            ? const {'verde': LayerMeta(hidden: true)}
            : const <String, LayerMeta>{},
      );
    }

    testWidgets('o olho aberto desenha e o olho fechado nao', (tester) async {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final visivel = await pixelsVerdes(tester, comOlho(escondida: false));
      expect(visivel, greaterThan(500), reason: 'a camada visivel sumiu');
      final oculta = await pixelsVerdes(tester, comOlho(escondida: true));
      expect(
        oculta,
        0,
        reason: 'a camada de olho fechado apareceu no video exportado',
      );
    });
  });
}
