// A CAMADA ESCOLHIDA (v1.1.1): barra da camada no topo, menu ⋯ com
// etiquetas, recorte, grupo, encaixe, espelho, midia e tempo; copiar e
// colar camada e estilo por categoria; e o parentesco sem ciclo.
import 'dart:io';
import 'dart:ui' show BlendMode, Offset;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/info_da_midia.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/layout_ops.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Offset;
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

Future<ProviderContainer> _editor(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final c = _container();
  final e = c.read(editorControllerProvider.notifier);
  e.addShapeLayer(Duration.zero, name: 'A');
  e.addShapeLayer(Duration.zero, name: 'B');
  c.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return c;
}

void main() {
  testWidgets('com camada escolhida a barra e dela; voltar devolve a do projeto', (
    tester,
  ) async {
    final c = await _editor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('barra-da-camada')), findsOneWidget);
    for (final k in ['camada-nome', 'camada-duplicar', 'camada-lixeira', 'camada-menu']) {
      expect(find.byKey(ValueKey(k)), findsOneWidget, reason: k);
    }
    expect(find.byKey(const ValueKey('editor-project-name')), findsNothing);
    expect(
      find.byKey(const ValueKey('camada-parentesco')),
      findsNothing,
      reason: 'sem pai, o elo nao ocupa lugar na barra',
    );

    // Renomear ali mesmo.
    await tester.tap(find.byKey(const ValueKey('camada-nome')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('camada-nome-campo')), 'Logo');
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layerById(id)!.name, 'Logo');

    await tester.tap(find.byKey(const ValueKey('editor-back')));
    await tester.pumpAndSettle();
    expect(c.read(selectedLayerProvider), isNull);
    expect(find.byKey(const ValueKey('editor-project-name')), findsOneWidget);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('o ⋯ da camada: etiqueta e espelho pelo menu real', (
    tester,
  ) async {
    final c = await _editor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('camada-menu')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('camada-menu-folha')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('camada-etiqueta-6')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).metaOf(id).label?.color,
      LayerLabel.palette[6].color,
    );
    // A lista ja construiu o item (fora da tela): rolar ate ele de verdade.
    await tester.ensureVisible(
      find.byKey(const ValueKey('camada-menu-espelhar-h')),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('camada-menu-espelhar-h')));
    await tester.pumpAndSettle();
    final l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.scaleX.valueAt(Duration.zero), lessThan(0));
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  test('doze etiquetas, as seis antigas na mesma ordem', () {
    expect(LayerLabel.palette, hasLength(12));
    expect(LayerLabel.palette.first.name, 'Rosa');
    expect(LayerLabel.palette[5].name, 'Violeta');
  });

  test('recortar pela de baixo: a base continua a vista', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'Base'); // vai para baixo
    e.addShapeLayer(Duration.zero, name: 'Cima');
    final cima = c.read(editorControllerProvider).layers.first;
    final base = c.read(editorControllerProvider).layers[1];
    expect(e.recortarPelaDeBaixo(cima.id), isTrue);
    final l = c.read(editorControllerProvider).layerById(cima.id)!;
    expect(l.matteMode, MatteMode.recorte);
    expect(l.matteSourceId, base.id);
    expect(matteEscondeAFonte(MatteMode.recorte), isFalse);
    expect(matteEscondeAFonte(MatteMode.alpha), isTrue);
    // A de baixo nao tem base: nada acontece.
    expect(e.recortarPelaDeBaixo(base.id), isFalse);
  });

  test('forma do grupo: o filho de cima vira mascara, recorte ou normal', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    e.addShapeLayer(Duration.zero, name: 'B');
    e.groupLayers([for (final l in c.read(editorControllerProvider).layers) l.id]);
    final g = c.read(editorControllerProvider).layers.single.id;
    e.definirFormaDoGrupo(g, BlendMode.dstIn);
    expect(
      (c.read(editorControllerProvider).layerById(g)! as GroupLayer).children.first.blendMode,
      BlendMode.dstIn,
    );
    e.definirFormaDoGrupo(g, null);
    expect(
      (c.read(editorControllerProvider).layerById(g)! as GroupLayer).children.first.blendMode,
      BlendMode.srcOver,
    );
  });

  test('caber, preencher e esticar na composicao', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    final layer = c.read(editorControllerProvider).layers.first;
    final p = c.read(editorControllerProvider);
    final caixa = e.layerBoxSize(layer, Duration.zero, scaled: false);
    final w = p.outputWidth / caixa.width;
    final h = p.outputHeight / caixa.height;
    e.encaixarNaComposicao(layer.id, EncaixeNaComposicao.esticar, Duration.zero);
    var l = c.read(editorControllerProvider).layerById(layer.id)!;
    expect(l.scaleX.valueAt(Duration.zero), closeTo(w, 1e-6));
    expect(l.scaleY.valueAt(Duration.zero), closeTo(h, 1e-6));
    expect(
      l.position.valueAt(Duration.zero),
      Offset(p.outputWidth / 2, p.outputHeight / 2),
    );
    e.encaixarNaComposicao(layer.id, EncaixeNaComposicao.caber, Duration.zero);
    l = c.read(editorControllerProvider).layerById(layer.id)!;
    expect(l.scaleX.valueAt(Duration.zero), closeTo(w < h ? w : h, 1e-6));
    e.encaixarNaComposicao(layer.id, EncaixeNaComposicao.preencher, Duration.zero);
    l = c.read(editorControllerProvider).layerById(layer.id)!;
    expect(l.scaleY.valueAt(Duration.zero), closeTo(w > h ? w : h, 1e-6));
  });

  test('espelhar inverte base e keyframes', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    final video = ShapeLayer(
      name: 'S',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      scaleY: AnimatedDouble(1, [
        const Keyframe(time: Duration.zero, value: 1),
        const Keyframe(time: Duration(seconds: 1), value: 2),
      ]),
    );
    e.openProject(c.read(editorControllerProvider).copyWith(layers: [video]));
    e.espelharCamada(video.id, horizontal: false);
    final l = c.read(editorControllerProvider).layerById(video.id)!;
    expect(l.scaleY.valueAt(const Duration(seconds: 1)), -2);
    expect(l.scaleY.valueAt(Duration.zero), -1);
  });

  test('extrair audio: camada de som no mesmo trecho e video mudo, um desfazer', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    final video = VideoLayer(
      name: 'Clipe',
      startTime: const Duration(seconds: 2),
      duration: const Duration(seconds: 3),
      sourcePath: 'clipe.mp4',
      sourceOffset: const Duration(milliseconds: 500),
    );
    e.openProject(c.read(editorControllerProvider).copyWith(layers: [video]));
    final id = e.extrairAudioDaCamada(video.id, 'clipe.m4a');
    expect(id, isNotNull);
    final audio = c.read(editorControllerProvider).layerById(id!)! as AudioLayer;
    expect(audio.startTime, const Duration(seconds: 2));
    expect(audio.duration, const Duration(seconds: 3));
    expect(audio.sourceOffset, const Duration(milliseconds: 500));
    expect(
      (c.read(editorControllerProvider).layerById(video.id)! as VideoLayer).audio.muted,
      isTrue,
    );
    e.undo();
    expect(c.read(editorControllerProvider).layers, hasLength(1));
    expect(
      (c.read(editorControllerProvider).layers.single as VideoLayer).audio.muted,
      isFalse,
    );
  });

  test('copiar e colar camada: nova identidade, acima da escolhida, no cabecote', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    e.addShapeLayer(Duration.zero, name: 'B');
    final b = c.read(editorControllerProvider).layers[1];
    e.setLayerLabel(b.id, LayerLabel.palette[2]);
    e.copiarCamada(b.id);
    expect(e.temCamadaCopiada, isTrue);
    final nova = e.colarCamada(const Duration(seconds: 4), acimaDe: b.id)!;
    final p = c.read(editorControllerProvider);
    expect(nova, isNot(b.id));
    expect(p.layers[1].id, nova);
    expect(p.layerById(nova)!.startTime, const Duration(seconds: 4));
    expect(p.metaOf(nova).label?.color, LayerLabel.palette[2].color);
    expect(c.read(selectedLayerProvider), nova);
  });

  test('colar estilo: so as categorias escolhidas, e so as que cabem', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'Fonte');
    e.addShapeLayer(Duration.zero, name: 'Alvo');
    final alvoId = c.read(editorControllerProvider).layers.first.id;
    final fonteId = c.read(editorControllerProvider).layers[1].id;
    e.editRotation(fonteId, Duration.zero, 45);
    e.setBlendMode(fonteId, BlendMode.screen);
    e.addEffect(fonteId, EffectType.gaussianBlur);
    e.copiarEstilo(fonteId);
    final possiveis = e.categoriasColaveis(alvoId);
    expect(possiveis, contains(CategoriaDeEstilo.moverETransformar));
    expect(possiveis, contains(CategoriaDeEstilo.efeitos));
    expect(possiveis, isNot(contains(CategoriaDeEstilo.volume)));
    expect(possiveis, isNot(contains(CategoriaDeEstilo.estiloDeTexto)));
    final n = e.colarEstilo(alvoId, {
      CategoriaDeEstilo.mesclagemEOpacidade,
      CategoriaDeEstilo.efeitos,
      CategoriaDeEstilo.volume, // nao cabe: ignorada
    });
    expect(n, 2);
    final alvo = c.read(editorControllerProvider).layerById(alvoId)!;
    expect(alvo.blendMode, BlendMode.screen);
    expect(alvo.effects.single.type, EffectType.gaussianBlur);
    expect(alvo.rotation.valueAt(Duration.zero), 0, reason: 'transformar nao foi');
    e.undo();
    final desfeito = c.read(editorControllerProvider).layerById(alvoId)!;
    expect(desfeito.blendMode, BlendMode.srcOver);
    expect(desfeito.effects, isEmpty);
  });

  test('parentesco: quem ja segue a camada nao pode ser pai dela', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addNullLayer(Duration.zero);
    e.addShapeLayer(Duration.zero, name: 'Filho');
    e.addShapeLayer(Duration.zero, name: 'Neto');
    final layers = c.read(editorControllerProvider).layers;
    final neto = layers[0].id;
    final filho = layers[1].id;
    final nulo = layers[2].id;
    e.linkProperty(filho, LayerProp.parent, nulo, Duration.zero);
    e.linkProperty(neto, LayerProp.parent, filho, Duration.zero);
    final p = c.read(editorControllerProvider);
    expect(descendentesPorParentesco(p, nulo), {filho, neto});
    expect(descendentesPorParentesco(p, filho), {neto});
    expect(descendentesPorParentesco(p, neto), isEmpty);
  });

  test('ficha da midia: fracao de quadros, tamanho legivel e arquivo sem ffprobe', () async {
    expect(quadrosDaFracao('30000/1001'), closeTo(29.97, 0.01));
    expect(quadrosDaFracao('25'), 25);
    expect(quadrosDaFracao('0/0'), isNull);
    expect(quadrosDaFracao('lixo'), isNull);
    expect(tamanhoLegivel(512), '512 B');
    expect(tamanhoLegivel(1536), '1,5 KB');
    expect(tamanhoLegivel(12 * 1024 * 1024), '12,0 MB');
    final dir = Directory.systemTemp.createTempSync('aurea-info-');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = File('${dir.path}/foto.png')..writeAsBytesSync(List.filled(2048, 0));
    final info = await lerInfoDaMidia(f.path);
    expect(info.nome, 'foto.png');
    expect(info.formato, 'PNG');
    expect(info.bytes, 2048);
    expect(info.linhas.first, ('Nome', 'foto.png'));
    expect(info.linhas.map((l) => l.$1), contains('Taxa de amostragem'));
  });
}
