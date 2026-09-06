import 'package:file_picker/file_picker.dart';
import 'package:image_picker/image_picker.dart';

import '../../../../core/ui/snack.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/panorama_cache.dart';
import '../../application/scene3d_gpu.dart';
import '../../domain/camera3d.dart';
import '../../domain/element3d.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/panorama3d.dart';
import '../../domain/scene3d.dart';
import '../widgets/element3d_painter.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';
import 'scene3d_studio.dart';
import 'model_import_button.dart';
import 'model_animation_screen.dart';

/// SHEET DA CENA 3D: estrutura da cena, materiais, luzes, camera com os
/// tres jeitos de ver a mesma grandeza (focal, angulo, zoom), a
/// profundidade de campo completa, os rigs e as ajudas.
///
/// Tudo aqui e parametro real do modelo — nada de caixa-preta.
Future<void> showScene3DSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  var tab = 0;
  String? selectedNode;

  await showParamSheet(
    context,
    title: 'Cena 3D',
    heightFactor: 0.55,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final layer = ref.read(editorControllerProvider).layerById(layerId);
        if (layer is! Scene3DLayer) return const SizedBox.shrink();
        final controller = ref.read(editorControllerProvider.notifier);
        final compWidth = ref
            .read(editorControllerProvider)
            .outputWidth
            .toDouble();

        void redraw() => setSheetState(() {});

        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(18, 12, 12, 4),
                child: Row(
                  children: [
                    const Text(
                      'Cena 3D',
                      style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text,
                      ),
                    ),
                    const SizedBox(width: 10),
                    Expanded(child: _BudgetBadge(layer: layer)),
                    const SizedBox(width: 8),
                    CupertinoButton(
                      padding: EdgeInsets.zero,
                      onPressed: () {
                        closeParamSheet(sheetContext);
                        Future.microtask(() {
                          if (context.mounted) {
                            openScene3DStudio(context, ref, layerId);
                          }
                        });
                      },
                      child: const Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            CupertinoIcons.viewfinder,
                            size: 18,
                            color: AmColors.accent,
                          ),
                          SizedBox(width: 5),
                          Text(
                            'Estudio',
                            style: TextStyle(
                              fontSize: 13,
                              color: AmColors.accent,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
              const _AvisoDoMotor(),
              _Tabs(
                labels: const [
                  'Objetos',
                  'Luzes',
                  'Ambiente',
                  'Camera',
                  'Foco',
                  'Ajudas',
                ],
                index: tab,
                onChanged: (i) => setSheetState(() => tab = i),
              ),
              Flexible(
                child: SingleChildScrollView(
                  padding: EdgeInsets.fromLTRB(
                    18,
                    10,
                    18,
                    16 + MediaQuery.of(sheetContext).viewInsets.bottom,
                  ),
                  child: switch (tab) {
                    0 => _ObjectsTab(
                      layer: layer,
                      controller: controller,
                      selected: selectedNode,
                      onSelect: (id) => setSheetState(() => selectedNode = id),
                      onChanged: redraw,
                    ),
                    1 => _LightsTab(
                      layer: layer,
                      controller: controller,
                      onChanged: redraw,
                    ),
                    2 => _EnvironmentTab(
                      layer: layer,
                      controller: controller,
                      onChanged: redraw,
                    ),
                    3 => _CameraTab(
                      layer: layer,
                      controller: controller,
                      compWidth: compWidth,
                      onChanged: redraw,
                    ),
                    4 => _DofTab(
                      layer: layer,
                      controller: controller,
                      onChanged: redraw,
                    ),
                    _ => _HelpersTab(
                      layer: layer,
                      controller: controller,
                      onChanged: redraw,
                    ),
                  },
                ),
              ),
            ],
          ),
        );
      },
    ),
  );
}

// ------------------------------------------------------------ objetos

class _ObjectsTab extends StatelessWidget {
  const _ObjectsTab({
    required this.layer,
    required this.controller,
    required this.selected,
    required this.onSelect,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final String? selected;
  final ValueChanged<String?> onSelect;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final scene = layer.scene;
    final node = scene.nodes.where((n) => n.id == selected).firstOrNull;
    final project = ProviderScope.containerOf(context)
        .read(editorControllerProvider);
    final textureLayers = project.layers
        .whereType<ImageLayer>()
        .where((candidate) => candidate.id != layer.id)
        .toList(growable: false);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _Hint(
          'Estrutura da cena. Cada objeto e uma malha de verdade — dois '
          'que se cruzam mostram a intersecao correta.',
        ),
        for (final n in scene.nodes)
          _NodeRow(
            node: n,
            selected: n.id == selected,
            onTap: () => onSelect(n.id == selected ? null : n.id),
            onVisible: () {
              controller.updateSceneNode(
                layer.id,
                n.id,
                (x) => x.copyWith(visible: !x.visible),
              );
              onChanged();
            },
            onLock: () {
              controller.setSceneNodeLocked(layer.id, n.id, !n.locked);
              onChanged();
            },
            onIsolate: () {
              controller.isolateSceneNode(layer.id, n.id);
              onChanged();
            },
            onDelete: () {
              if (n.locked) return;
              controller.removeSceneNode(layer.id, n.id);
              if (selected == n.id) onSelect(null);
              onChanged();
            },
          ),
        const SizedBox(height: 8),

        // EXTRUDAR: a forma plana do projeto vira volume. E o caminho de
        // logo chapado para logo girando, sem modelar nada.
        Builder(
          builder: (context) {
            final project = ProviderScope.containerOf(context)
                .read(editorControllerProvider);
            final formas = project.layers.whereType<ShapeLayer>().toList();
            if (formas.isEmpty) {
              return const _Hint(
                'Desenhe uma camada de forma para poder extrudar ela em '
                '3D.',
              );
            }
            return Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const _SectionTitle('Extrudar uma forma'),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final f in formas)
                      GestureDetector(
                        onTap: () {
                          final id = controller.extrudeShapeIntoScene(
                            layer.id,
                            f.id,
                          );
                          if (id != null) onSelect(id);
                          onChanged();
                        },
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 11,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const Icon(
                                CupertinoIcons.cube,
                                size: 13,
                                color: AmColors.accent,
                              ),
                              const SizedBox(width: 6),
                              Text(
                                f.name,
                                style: const TextStyle(
                                  fontSize: 11,
                                  color: AmColors.text,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                  ],
                ),
                if (node?.outline != null) ...[
                  const SizedBox(height: 6),
                  _Num(
                    label: 'Espessura',
                    track: AnimatedDouble(node!.extrudeDepth),
                    min: 2,
                    max: 300,
                    onChanged: (v) {
                      controller.setExtrudeDepth(layer.id, node.id, v);
                      onChanged();
                    },
                  ),
                ],
                const SizedBox(height: 10),
              ],
            );
          },
        ),

        // NULO 3D e o rig que ele destrava. Sem nulo dentro da cena nao
        // ha rigging la dentro: nao da para girar um conjunto junto,
        // nem orbitar a camera interna.
        Row(
          children: [
            Expanded(
              child: _AcaoLarga(
                rotulo: 'Nulo 3D',
                onTap: () {
                  final id = controller.addSceneNull(layer.id);
                  if (id.isNotEmpty) onSelect(id);
                  onChanged();
                },
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: _AcaoLarga(
                rotulo: 'Rig de orbita',
                onTap: () {
                  controller.addOrbitRig(layer.id);
                  onChanged();
                },
              ),
            ),
          ],
        ),
        const SizedBox(height: 6),

        // PAI de cada no, e de quem a camera interna segue.
        if (node != null) ...[
          _Chips(
            label: 'Pai de "${node.name}"',
            options: [
              'Nenhum',
              for (final n in scene.nodes)
                if (n.id != node.id) n.name,
            ],
            index: node.parentId == null
                ? 0
                : (() {
                    final outros = [
                      for (final n in scene.nodes)
                        if (n.id != node.id) n,
                    ];
                    final i = outros.indexWhere((n) => n.id == node.parentId);
                    return i < 0 ? 0 : i + 1;
                  })(),
            onChanged: (i) {
              final outros = [
                for (final n in scene.nodes)
                  if (n.id != node.id) n,
              ];
              controller.setSceneNodeParent(
                layer.id,
                node.id,
                i == 0 ? null : outros[i - 1].id,
              );
              onChanged();
            },
          ),
          const _Hint(
            'Girar o pai orbita o filho em torno do pivo dele. Um ciclo '
            '(A pai de B e B pai de A) e recusado.',
          ),
          const SizedBox(height: 6),
        ],

        _Chips(
          label: 'Camera segue',
          options: ['Nada', for (final n in scene.nodes) n.name],
          index: scene.cameraParentId == null
              ? 0
              : (() {
                  final i = scene.nodes.indexWhere(
                    (n) => n.id == scene.cameraParentId,
                  );
                  return i < 0 ? 0 : i + 1;
                })(),
          onChanged: (i) {
            controller.setSceneCameraParent(
              layer.id,
              i == 0 ? null : scene.nodes[i - 1].id,
            );
            onChanged();
          },
        ),
        const _Hint(
          'A camera herda posicao e rotacao do pai — nunca escala. '
          'Camera nao tem escala, e herdar e o que faz o enquadramento '
          'explodir.',
        ),
        const SizedBox(height: 10),

        ModelImportButton(
          layerId: layer.id,
          onImported: (id) {
            onSelect(id);
            onChanged();
          },
        ),
        const SizedBox(height: 10),

        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (final kind in Element3DKind.values)
              GestureDetector(
                onTap: () {
                  controller.addSceneNode(layer.id, kind);
                  onChanged();
                },
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 7,
                  ),
                  decoration: BoxDecoration(
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(9),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(
                        CupertinoIcons.plus,
                        size: 12,
                        color: AmColors.accent,
                      ),
                      const SizedBox(width: 5),
                      Text(
                        element3DLabel(kind),
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.accent,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
          ],
        ),
        if (node != null) ...[
          const SizedBox(height: 16),
          _SectionTitle(node.name),
          _ColorRow(
            color: node.colorTag,
            onColor: (color) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(colorTag: color),
              );
              onChanged();
            },
          ),
          if (node.locked)
            const _Hint(
              'Objeto bloqueado: o gizmo e a exclusao ficam protegidos.',
            ),
          _Num(
            label: 'Posicao X',
            track: node.x,
            min: -1500,
            max: 1500,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(x: n.x.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Posicao Y',
            track: node.y,
            min: -1500,
            max: 1500,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(y: n.y.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Posicao Z',
            track: node.z,
            min: -1500,
            max: 1500,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(z: n.z.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Girar X',
            track: node.rotX,
            min: -360,
            max: 360,
            suffix: '°',
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(rotX: n.rotX.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Girar Y',
            track: node.rotY,
            min: -360,
            max: 360,
            suffix: '°',
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(rotY: n.rotY.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Girar Z',
            track: node.rotZ,
            min: -360,
            max: 360,
            suffix: '°',
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(rotZ: n.rotZ.withBase(v)),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Tamanho',
            value: node.size,
            min: 10,
            max: 600,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(size: v),
              );
              onChanged();
            },
          ),
          _Chips(
            label: 'LOD',
            options: const ['Auto', 'Alto', 'Medio', 'Baixo'],
            index: node.lod.index,
            onChanged: (i) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(lod: MeshLod3D.values[i]),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Subdivisoes',
            value: node.subdivisions.toDouble(),
            min: 0,
            max: 4,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(subdivisions: v.round()),
              );
              onChanged();
            },
          ),
          if (node.modelAsset != null) ...[
            _AcaoLarga(
              rotulo: 'Animar modelo / Rig',
              onTap: () => Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (_) =>
                      ModelAnimationScreen(layerId: layer.id, nodeId: node.id),
                ),
              ),
            ),
            SwitchListTile.adaptive(
              contentPadding: EdgeInsets.zero,
              title: const Text('Materiais originais do modelo'),
              value: node.useModelMaterials,
              onChanged: (v) {
                controller.updateSceneNode(
                  layer.id,
                  node.id,
                  (n) => n.copyWith(useModelMaterials: v),
                );
                onChanged();
              },
            ),
            const _Hint(
              'Desligue para aplicar o material abaixo ao modelo inteiro.',
            ),
          ],
          if (node.modelAsset == null &&
              (node.modelSource?.animationNames.isNotEmpty ?? false))
            _Chips(
              label: 'Clipe',
              options: ['Nenhum', ...node.modelSource!.animationNames],
              index: node.animationClip == null
                  ? 0
                  : node.modelSource!.animationNames.indexOf(
                          node.animationClip!,
                        ) +
                        1,
              onChanged: (i) {
                controller.updateSceneNode(
                  layer.id,
                  node.id,
                  (n) => i == 0
                      ? n.copyWith(clearAnimationClip: true)
                      : n.copyWith(
                          animationClip: n.modelSource!.animationNames[i - 1],
                        ),
                );
                onChanged();
              },
            ),
          if (node.modelSource?.nodeNames.isNotEmpty ?? false)
            _Hint(
              'Hierarquia importada: '
              '${node.modelSource!.nodeNames.take(6).join(' › ')}',
            ),
          const SizedBox(height: 10),
          _SectionTitle('Material'),
          _Chips(
            label: 'Pronto',
            options: [
              for (final preset in MaterialPreset3D.values)
                materialPresetLabel(preset),
            ],
            index: -1,
            onChanged: (i) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(
                  material: materialFromPreset(MaterialPreset3D.values[i]),
                ),
              );
              onChanged();
            },
          ),
          _Chips(
            label: 'Tipo',
            options: const ['PBR', 'Sem luz', 'Vidro', 'Recorte'],
            index: node.material.kind.index,
            onChanged: (i) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(
                  material: n.material.copyWith(kind: MaterialKind.values[i]),
                ),
              );
              onChanged();
            },
          ),
          _ColorRow(
            color: node.material.baseColor,
            onColor: (c) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(material: n.material.copyWith(baseColor: c)),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Metalico',
            value: node.material.metallic,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(material: n.material.copyWith(metallic: v)),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Rugosidade',
            value: node.material.roughness,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(material: n.material.copyWith(roughness: v)),
              );
              onChanged();
            },
          ),
          // REFLEXO DO AMBIENTE: quanto da cena ao redor o material
          // devolve. E o controle que separa plastico de metal polido.
          _Plain(
            label: 'Reflexo',
            value: node.material.reflectivity,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) =>
                    n.copyWith(material: n.material.copyWith(reflectivity: v)),
              );
              onChanged();
            },
          ),
          // IMAGEM NO OBJETO: uma foto, um logo, uma tela. Projecao de
          // caixa — cada face recebe a imagem pelo eixo que mais encara.
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 4),
            child: Row(
              children: [
                const SizedBox(
                  width: 92,
                  child: Text(
                    'Imagem',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                ),
                Expanded(
                  child: Text(
                    node.material.imagePath == null
                        ? 'Nenhuma'
                        : node.material.imagePath!.split(RegExp(r'[\\/]')).last,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 12, color: AmColors.text),
                  ),
                ),
                GestureDetector(
                  onTap: () async {
                    final r = await FilePicker.platform.pickFiles(
                      type: FileType.image,
                    );
                    final caminho = r?.files.single.path;
                    if (caminho == null) return;
                    controller.updateSceneNode(
                      layer.id,
                      node.id,
                      (n) => n.copyWith(
                        material: n.material.copyWith(imagePath: caminho),
                      ),
                    );
                    onChanged();
                  },
                  child: const Padding(
                    padding: EdgeInsets.symmetric(horizontal: 8),
                    child: Icon(
                      CupertinoIcons.photo,
                      size: 20,
                      color: AmColors.accent,
                    ),
                  ),
                ),
                if (node.material.imagePath != null)
                  GestureDetector(
                    onTap: () {
                      controller.updateSceneNode(
                        layer.id,
                        node.id,
                        (n) => n.copyWith(
                          material: n.material.copyWith(clearImage: true),
                        ),
                      );
                      onChanged();
                    },
                    child: const Padding(
                      padding: EdgeInsets.only(left: 4),
                      child: Icon(
                        CupertinoIcons.xmark_circle,
                        size: 20,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
              ],
            ),
          ),
          if (textureLayers.isNotEmpty)
            _Chips(
              label: 'Da camada',
              options: [
                'Nenhuma',
                for (final candidate in textureLayers) candidate.name,
              ],
              index: node.material.textureLayerId == null
                  ? 0
                  : textureLayers.indexWhere(
                          (candidate) =>
                              candidate.id == node.material.textureLayerId,
                        ) +
                        1,
              onChanged: (i) {
                controller.updateSceneNode(layer.id, node.id, (n) {
                  if (i == 0) {
                    return n.copyWith(
                      material: n.material.copyWith(
                        clearTextureLayer: true,
                        clearImage: n.material.textureLayerId != null,
                      ),
                    );
                  }
                  final source = textureLayers[i - 1];
                  return n.copyWith(
                    material: n.material.copyWith(
                      textureLayerId: source.id,
                      imagePath: source.sourcePath,
                    ),
                  );
                });
                onChanged();
              },
            ),
          const _Hint(
            'Camadas de imagem e arquivos fixos usam projecao por caixa.',
          ),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var face = 0; face < 6; face++)
                _Action(
                  icon: node.material.faceImagePaths.containsKey(face)
                      ? CupertinoIcons.photo_fill
                      : CupertinoIcons.photo,
                  label: 'Face ${face + 1}',
                  onTap: () async {
                    final result = await FilePicker.platform.pickFiles(
                      type: FileType.image,
                    );
                    final path = result?.files.single.path;
                    if (path == null) return;
                    final faces = <int, String>{
                      ...node.material.faceImagePaths,
                      face: path,
                    };
                    controller.updateSceneNode(
                      layer.id,
                      node.id,
                      (n) => n.copyWith(
                        material: n.material.copyWith(faceImagePaths: faces),
                      ),
                    );
                    onChanged();
                  },
                ),
            ],
          ),
          _Plain(
            label: 'Emissivo',
            value: node.material.emissive,
            min: 0,
            max: 2,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(material: n.material.copyWith(emissive: v)),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Opacidade',
            value: node.material.opacity,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(material: n.material.copyWith(opacity: v)),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Normal',
            value: node.material.normalStrength,
            min: 0,
            max: 2,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(
                  material: n.material.copyWith(normalStrength: v),
                ),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Oclusao',
            value: node.material.occlusionStrength,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(
                  material: n.material.copyWith(occlusionStrength: v),
                ),
              );
              onChanged();
            },
          ),
          if (node.material.kind == MaterialKind.cutout)
            _Plain(
              label: 'Recorte',
              value: node.material.alphaCutoff,
              min: 0,
              max: 1,
              decimals: 2,
              onChanged: (v) {
                controller.updateSceneNode(
                  layer.id,
                  node.id,
                  (n) =>
                      n.copyWith(material: n.material.copyWith(alphaCutoff: v)),
                );
                onChanged();
              },
            ),
          _Toggle(
            label: 'Dupla face',
            value: node.material.doubleSided,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) =>
                    n.copyWith(material: n.material.copyWith(doubleSided: v)),
              );
              onChanged();
            },
          ),
          _Toggle(
            label: 'Canais empacotados',
            value: node.material.packedChannels,
            onChanged: (v) {
              controller.updateSceneNode(
                layer.id,
                node.id,
                (n) => n.copyWith(
                  material: n.material.copyWith(packedChannels: v),
                ),
              );
              onChanged();
            },
          ),
          const SizedBox(height: 12),
          _SectionTitle('Duplicar em array (Grade 3D)'),
          const _Hint(
            'Todas as copias sao INSTANCIAS da mesma malha: 200 objetos '
            'continuam sendo uma chamada de desenho.',
          ),
          _ArrayControls(
            node: node,
            onApply: (x, y, z, spacing) {
              controller.arrayNodeInstances(
                layer.id,
                node.id,
                countX: x,
                countY: y,
                countZ: z,
                spacing: spacing,
              );
              onChanged();
            },
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              _Action(
                icon: CupertinoIcons.viewfinder,
                label: 'Enquadrar',
                onTap: () {
                  controller.frameSceneNode(layer.id, node.id);
                  onChanged();
                },
              ),
              _Action(
                icon: CupertinoIcons.circle_lefthalf_fill,
                label: 'Focar aqui',
                onTap: () {
                  controller.focusCameraOnNode(layer.id, node.id);
                  onChanged();
                },
              ),
              _Action(
                icon: CupertinoIcons.doc_on_doc,
                label: 'Duplicar',
                onTap: () {
                  final copy = node.duplicate();
                  controller.updateScene3D(
                    layer.id,
                    (s) => s.copyWith(nodes: [...s.nodes, copy]),
                  );
                  onSelect(copy.id);
                  onChanged();
                },
              ),
            ],
          ),
          if (!node.credit.isEmpty) ...[
            const SizedBox(height: 8),
            Text(
              'Modelo · ${node.credit.badge}',
              style: const TextStyle(fontSize: 10, color: AmColors.muted),
            ),
          ],
        ],
      ],
    );
  }
}

class _AcaoLarga extends StatelessWidget {
  const _AcaoLarga({required this.rotulo, required this.onTap});

  final String rotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(vertical: 10),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Text(
        rotulo,
        style: const TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w600,
          color: AmColors.accent,
        ),
      ),
    ),
  );
}

class _NodeRow extends StatelessWidget {
  const _NodeRow({
    required this.node,
    required this.selected,
    required this.onTap,
    required this.onVisible,
    required this.onLock,
    required this.onIsolate,
    required this.onDelete,
  });

  final SceneNode node;
  final bool selected;
  final VoidCallback onTap;
  final VoidCallback onVisible;
  final VoidCallback onLock;
  final VoidCallback onIsolate;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        margin: EdgeInsets.fromLTRB(node.parentId == null ? 0 : 16, 3, 0, 3),
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
        decoration: BoxDecoration(
          color: selected ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(10),
          border: Border(left: BorderSide(color: node.colorTag, width: 3)),
        ),
        child: Row(
          children: [
            SizedBox(
              width: 26,
              height: 26,
              child: CustomPaint(
                painter: Element3DPainter(
                  layer: Element3DLayer(
                    name: '',
                    startTime: Duration.zero,
                    duration: const Duration(seconds: 1),
                    kind: node.kind,
                    size: 9,
                    color: node.material.baseColor,
                  ),
                  rotXDeg: -20,
                  rotYDeg: 32,
                ),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                node.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 13,
                  color: node.visible ? AmColors.text : AmColors.muted,
                ),
              ),
            ),
            if (node.instances.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(right: 6),
                child: Text(
                  '${node.instances.length}x',
                  style: const TextStyle(fontSize: 11, color: AmColors.accent),
                ),
              ),
            CupertinoButton(
              padding: EdgeInsets.zero,
              minimumSize: const Size(30, 30),
              onPressed: onVisible,
              child: Icon(
                node.visible ? CupertinoIcons.eye : CupertinoIcons.eye_slash,
                size: 17,
                color: node.visible ? AmColors.text : AmColors.muted,
              ),
            ),
            CupertinoButton(
              padding: EdgeInsets.zero,
              minimumSize: const Size(30, 30),
              onPressed: onLock,
              child: Icon(
                node.locked
                    ? CupertinoIcons.lock_fill
                    : CupertinoIcons.lock_open,
                size: 15,
                color: node.locked ? AmColors.accent : AmColors.muted,
              ),
            ),
            CupertinoButton(
              padding: EdgeInsets.zero,
              minimumSize: const Size(30, 30),
              onPressed: onIsolate,
              child: const Icon(
                CupertinoIcons.rectangle_dock,
                size: 16,
                color: AmColors.muted,
              ),
            ),
            CupertinoButton(
              padding: EdgeInsets.zero,
              minimumSize: const Size(30, 30),
              onPressed: onDelete,
              child: const Icon(
                CupertinoIcons.trash,
                size: 15,
                color: AmColors.muted,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ArrayControls extends StatefulWidget {
  const _ArrayControls({required this.node, required this.onApply});

  final SceneNode node;
  final void Function(int x, int y, int z, double spacing) onApply;

  @override
  State<_ArrayControls> createState() => _ArrayControlsState();
}

class _ArrayControlsState extends State<_ArrayControls> {
  int _x = 4;
  int _y = 1;
  int _z = 4;
  double _spacing = 160;

  @override
  Widget build(BuildContext context) {
    final total = _x * _y * _z;
    return Column(
      children: [
        _Plain(
          label: 'Colunas X',
          value: _x.toDouble(),
          min: 1,
          max: 20,
          decimals: 0,
          onChanged: (v) => setState(() => _x = v.round()),
        ),
        _Plain(
          label: 'Linhas Y',
          value: _y.toDouble(),
          min: 1,
          max: 20,
          decimals: 0,
          onChanged: (v) => setState(() => _y = v.round()),
        ),
        _Plain(
          label: 'Camadas Z',
          value: _z.toDouble(),
          min: 1,
          max: 20,
          decimals: 0,
          onChanged: (v) => setState(() => _z = v.round()),
        ),
        _Plain(
          label: 'Espaco',
          value: _spacing,
          min: 20,
          max: 600,
          onChanged: (v) => setState(() => _spacing = v),
        ),
        Row(
          children: [
            _Action(
              icon: CupertinoIcons.square_grid_3x2,
              label: 'Gerar $total',
              onTap: () => widget.onApply(_x, _y, _z, _spacing),
            ),
            const SizedBox(width: 8),
            _Action(
              icon: CupertinoIcons.clear,
              label: 'Limpar',
              onTap: () => widget.onApply(1, 1, 1, _spacing),
            ),
          ],
        ),
      ],
    );
  }
}

// ------------------------------------------------------------ ambiente

class _EnvironmentTab extends StatelessWidget {
  const _EnvironmentTab({
    required this.layer,
    required this.controller,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final scene = layer.scene;
    final panorama = scene.panorama;
    final probe = scene.reflectionProbe;
    final project = ProviderScope.containerOf(context)
        .read(editorControllerProvider);
    final sourceLayers = project.layers
        .whereType<ImageLayer>()
        .where((candidate) => candidate.id != layer.id)
        .toList(growable: false);

    void setPanorama(Panorama3D next, {EnvironmentKind? environment}) {
      if (next.hasImage) PanoramaCache.instance.samplerFor(next);
      controller.updateScene3D(
        layer.id,
        (s) => s.copyWith(
          panorama: next,
          environment: environment ?? s.environment,
        ),
      );
      onChanged();
    }

    void setProbe(ReflectionProbe3D next) {
      controller.updateScene3D(
        layer.id,
        (s) => s.copyWith(reflectionProbe: next),
      );
      onChanged();
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _Hint(
          'O panorama ilumina e aparece nos materiais. A mesma rotacao '
          'gira luz e reflexo para os dois continuarem coerentes.',
        ),
        _SectionTitle('Panorama estatico'),
        _Chips(
          label: 'Preset',
          options: [
            for (final preset in PanoramaPreset.values)
              panoramaPresetLabel(preset),
          ],
          index: panorama.preset.index,
          onChanged: (index) {
            final preset = PanoramaPreset.values[index];
            setPanorama(
              panorama.copyWith(
                preset: preset,
                source: PanoramaSource.preset,
                clearSource: true,
                clearLayer: true,
                approximate: false,
                coverageDegrees: 360,
                mirrorTo360: false,
                seamSoftness: 0,
                fillZenithNadir: false,
                convertedAtImport: false,
              ),
              environment: environmentForPanorama(preset),
            );
          },
        ),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            _Action(
              icon: CupertinoIcons.photo,
              label: 'Importar panorama',
              onTap: () async {
                final result = await FilePicker.platform.pickFiles(
                  type: FileType.image,
                );
                final path = result?.files.single.path;
                if (path == null) return;
                setPanorama(preparePanorama(path: path));
              },
            ),
            _Action(
              icon: CupertinoIcons.camera,
              label: 'Fotografar ambiente',
              onTap: () async {
                final file = await ImagePicker().pickImage(
                  source: ImageSource.camera,
                  imageQuality: 100,
                );
                if (file == null) return;
                setPanorama(
                  preparePanorama(
                    path: file.path,
                    coverageDegrees: 150,
                    capturedWithPhone: true,
                  ),
                );
              },
            ),
          ],
        ),
        if (panorama.hasImage) ...[
          const SizedBox(height: 6),
          Text(
            panorama.sourcePath!.split(RegExp(r'[\\/]')).last,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 11, color: AmColors.text),
          ),
          _Plain(
            label: 'Cobertura',
            value: panorama.coverageDegrees,
            min: 120,
            max: 360,
            suffix: '°',
            onChanged: (value) {
              final incomplete = value < 300;
              setPanorama(
                panorama.copyWith(
                  coverageDegrees: value,
                  mirrorTo360: incomplete,
                  seamSoftness: incomplete ? 0.16 : 0.02,
                  fillZenithNadir: incomplete,
                  approximate:
                      panorama.source == PanoramaSource.camera || incomplete,
                ),
              );
            },
          ),
        ],
        if (sourceLayers.isNotEmpty)
          _Chips(
            label: 'Da camada',
            options: [
              'Nenhuma',
              for (final source in sourceLayers) source.name,
            ],
            index: panorama.sourceLayerId == null
                ? 0
                : sourceLayers.indexWhere(
                        (source) => source.id == panorama.sourceLayerId,
                      ) +
                      1,
            onChanged: (index) {
              if (index == 0) {
                setPanorama(
                  panorama.copyWith(
                    source: PanoramaSource.preset,
                    clearLayer: true,
                  ),
                );
              } else {
                final source = sourceLayers[index - 1];
                setPanorama(
                  panorama.copyWith(
                    source: PanoramaSource.layer,
                    sourceLayerId: source.id,
                    sourcePath: source.sourcePath,
                    approximate: false,
                  ),
                );
              }
            },
          ),
        if (panorama.approximate)
          const _Hint(
            'Aproximado · espelhado para 360°, costura suavizada, teto e '
            'chao preenchidos e realce pseudo-HDR.',
          ),
        _Plain(
          label: 'Rotacao',
          value: panorama.rotationDegrees,
          min: -180,
          max: 180,
          suffix: '°',
          onChanged: (value) =>
              setPanorama(panorama.copyWith(rotationDegrees: value)),
        ),
        _Plain(
          label: 'Intensidade',
          value: panorama.intensity,
          min: 0,
          max: 2,
          decimals: 2,
          onChanged: (value) =>
              setPanorama(panorama.copyWith(intensity: value)),
        ),
        _Plain(
          label: 'Desfocar fundo',
          value: panorama.backgroundBlur,
          min: 0,
          max: 30,
          onChanged: (value) =>
              setPanorama(panorama.copyWith(backgroundBlur: value)),
        ),
        _Toggle(
          label: 'Mostrar panorama no fundo',
          value: panorama.showBackground,
          onChanged: (value) =>
              setPanorama(panorama.copyWith(showBackground: value)),
        ),
        _Plain(
          label: 'Altas luzes',
          value: panorama.highlightBoost,
          min: 0,
          max: 2,
          decimals: 2,
          onChanged: (value) =>
              setPanorama(panorama.copyWith(highlightBoost: value)),
        ),
        _Plain(
          label: 'Reflexo global',
          value: scene.envReflect,
          min: 0,
          max: 1,
          decimals: 2,
          onChanged: (value) {
            controller.updateScene3D(
              layer.id,
              (s) => s.copyWith(envReflect: value),
            );
            onChanged();
          },
        ),
        const SizedBox(height: 12),
        _SectionTitle('Atmosfera / nevoa'),
        _Toggle(
          label: 'Nevoa por distancia',
          value: scene.fogDensity > 0,
          onChanged: (value) {
            controller.updateScene3D(
              layer.id,
              (s) => s.copyWith(fogDensity: value ? .001 : 0),
            );
            onChanged();
          },
        ),
        if (scene.fogDensity > 0) ...[
          _Plain(
            label: 'Densidade',
            value: scene.fogDensity * 1000,
            min: 0.1,
            max: 10,
            decimals: 2,
            onChanged: (v) {
              controller.updateScene3D(
                layer.id,
                (s) => s.copyWith(fogDensity: v / 1000),
              );
              onChanged();
            },
          ),
          _Plain(
            label: 'Inicio da nevoa',
            value: scene.fogStart,
            min: 0,
            max: 5000,
            onChanged: (v) {
              controller.updateScene3D(
                layer.id,
                (s) => s.copyWith(fogStart: v),
              );
              onChanged();
            },
          ),
          _ColorRow(
            color: scene.fogColor,
            onColor: (v) {
              controller.updateScene3D(
                layer.id,
                (s) => s.copyWith(fogColor: v),
              );
              onChanged();
            },
          ),
          const _Hint(
            'Perspectiva atmosferica aplicada tambem na exportacao. '
            'Nao simula luz volumetrica.',
          ),
        ],
        const SizedBox(height: 12),
        _SectionTitle('Reflexo em tempo real'),
        _Toggle(
          label: 'Refletir a cena',
          value: probe.enabled,
          onChanged: (value) => setProbe(probe.copyWith(enabled: value)),
        ),
        if (probe.enabled) ...[
          _Chips(
            label: 'Qualidade',
            options: [for (final q in ProbeQuality.values) q.label],
            index: probe.quality.index,
            onChanged: (index) =>
                setProbe(probe.copyWith(quality: ProbeQuality.values[index])),
          ),
          _Chips(
            label: 'Atualizar',
            options: [for (final mode in ProbeUpdateMode.values) mode.label],
            index: probe.updateMode.index,
            onChanged: (index) {
              final mode = ProbeUpdateMode.values[index];
              setProbe(probe.copyWith(updateMode: mode));
              if (mode == ProbeUpdateMode.continuous) {
                AureaSnack.show(
                  context,
                  'Continuo atualiza uma face por quadro e usa mais bateria.',
                );
              }
            },
          ),
          const _Hint(
            'Ao mover refaz uma face por quadro e para apos seis; com a '
            'cena parada o custo da sonda e zero.',
          ),
          _Toggle(
            label: 'Sonda por objeto',
            value: probe.perObject,
            onChanged: (value) => setProbe(probe.copyWith(perObject: value)),
          ),
          if (!probe.perObject) ...[
            _Plain(
              label: 'Sonda X',
              value: probe.position.x,
              min: -1500,
              max: 1500,
              onChanged: (value) => setProbe(
                probe.copyWith(
                  position: ProbePoint3D(
                    value,
                    probe.position.y,
                    probe.position.z,
                  ),
                ),
              ),
            ),
            _Plain(
              label: 'Sonda Y',
              value: probe.position.y,
              min: -1500,
              max: 1500,
              onChanged: (value) => setProbe(
                probe.copyWith(
                  position: ProbePoint3D(
                    probe.position.x,
                    value,
                    probe.position.z,
                  ),
                ),
              ),
            ),
            _Plain(
              label: 'Sonda Z',
              value: probe.position.z,
              min: -1500,
              max: 1500,
              onChanged: (value) => setProbe(
                probe.copyWith(
                  position: ProbePoint3D(
                    probe.position.x,
                    probe.position.y,
                    value,
                  ),
                ),
              ),
            ),
          ],
          const _SectionTitle('Entram no reflexo'),
          for (final node in scene.nodes)
            _Toggle(
              label: node.name,
              value: !probe.excludeNodeIds.contains(node.id),
              onChanged: (value) {
                final excluded = <String>{...probe.excludeNodeIds};
                if (value) {
                  excluded.remove(node.id);
                } else {
                  excluded.add(node.id);
                }
                setProbe(probe.copyWith(excludeNodeIds: excluded));
              },
            ),
        ],
        const SizedBox(height: 10),
        _SectionTitle('Piso'),
        _Toggle(
          label: 'Reflexo planar no chao',
          value: scene.planarFloorReflection,
          onChanged: (value) {
            controller.updateScene3D(
              layer.id,
              (s) => s.copyWith(planarFloorReflection: value),
            );
            onChanged();
          },
        ),
        if (scene.planarFloorReflection)
          _Plain(
            label: 'Rugosidade',
            value: scene.planarFloorRoughness,
            min: 0,
            max: 1,
            decimals: 2,
            onChanged: (value) {
              controller.updateScene3D(
                layer.id,
                (s) => s.copyWith(planarFloorRoughness: value),
              );
              onChanged();
            },
          ),
      ],
    );
  }
}

// -------------------------------------------------------------- luzes

class _LightsTab extends StatelessWidget {
  const _LightsTab({
    required this.layer,
    required this.controller,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final lights = layer.scene.lights;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _Hint(
          'Iluminacao direta com poucas luzes. Cada malha so recebe as '
          'luzes que a alcancam — luz fora do alcance nem entra na conta.',
        ),
        _Plain(
          label: 'Ambiente',
          value: layer.scene.ambient,
          min: 0,
          max: 1,
          decimals: 2,
          onChanged: (v) {
            controller.updateScene3D(layer.id, (s) => s.copyWith(ambient: v));
            onChanged();
          },
        ),
        const SizedBox(height: 8),
        for (final l in lights) ...[
          _SectionTitle(switch (l.kind) {
            Light3DKind.directional => 'Direcional',
            Light3DKind.point => 'Ponto',
            Light3DKind.ambient => 'Ambiente',
            Light3DKind.spot => 'Spot',
          }),
          _Num(
            label: 'Intensidade',
            track: l.intensity,
            min: 0,
            max: 4,
            decimals: 2,
            onChanged: (v) {
              controller.updateSceneLight(
                layer.id,
                l.id,
                (x) => x.copyWith(intensity: x.intensity.withBase(v)),
              );
              onChanged();
            },
          ),
          if (l.kind == Light3DKind.directional ||
              l.kind == Light3DKind.spot) ...[
            _Plain(
              label: 'Direcao X',
              value: l.direction.x,
              min: -1,
              max: 1,
              decimals: 2,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(
                    direction: Vec3(v, x.direction.y, x.direction.z),
                  ),
                );
                onChanged();
              },
            ),
            _Plain(
              label: 'Direcao Y',
              value: l.direction.y,
              min: -1,
              max: 1,
              decimals: 2,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(
                    direction: Vec3(x.direction.x, v, x.direction.z),
                  ),
                );
                onChanged();
              },
            ),
            _Plain(
              label: 'Direcao Z',
              value: l.direction.z,
              min: -1,
              max: 1,
              decimals: 2,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(
                    direction: Vec3(x.direction.x, x.direction.y, v),
                  ),
                );
                onChanged();
              },
            ),
          ],
          if (l.kind == Light3DKind.point || l.kind == Light3DKind.spot) ...[
            _Plain(
              label: 'Posicao X',
              value: l.position.x,
              min: -3000,
              max: 3000,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) =>
                      x.copyWith(position: Vec3(v, x.position.y, x.position.z)),
                );
                onChanged();
              },
            ),
            _Plain(
              label: 'Posicao Y',
              value: l.position.y,
              min: -3000,
              max: 3000,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) =>
                      x.copyWith(position: Vec3(x.position.x, v, x.position.z)),
                );
                onChanged();
              },
            ),
            _Plain(
              label: 'Posicao Z',
              value: l.position.z,
              min: -3000,
              max: 3000,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) =>
                      x.copyWith(position: Vec3(x.position.x, x.position.y, v)),
                );
                onChanged();
              },
            ),
          ],
          if (l.kind == Light3DKind.point || l.kind == Light3DKind.spot)
            _Plain(
              label: 'Alcance',
              value: l.range,
              min: 100,
              max: 4000,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(range: v),
                );
                onChanged();
              },
            ),
          if (l.kind == Light3DKind.spot) ...[
            _Plain(
              label: 'Cone',
              value: l.coneDegrees,
              min: 5,
              max: 170,
              suffix: '°',
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(coneDegrees: v),
                );
                onChanged();
              },
            ),
            _Plain(
              label: 'Suavidade',
              value: l.softness,
              min: 0,
              max: 1,
              decimals: 2,
              onChanged: (v) {
                controller.updateSceneLight(
                  layer.id,
                  l.id,
                  (x) => x.copyWith(softness: v),
                );
                onChanged();
              },
            ),
          ],
          _ColorRow(
            color: l.color,
            onColor: (c) {
              controller.updateSceneLight(
                layer.id,
                l.id,
                (x) => x.copyWith(color: c),
              );
              onChanged();
            },
          ),
          Row(
            children: [
              _Toggle(
                label: 'Sombra',
                value: l.castsShadow,
                onChanged: (v) {
                  controller.updateScene3D(
                    layer.id,
                    (scene) => scene.copyWith(
                      lights: [
                        for (final light in scene.lights)
                          light.copyWith(
                            castsShadow: light.id == l.id ? v : false,
                          ),
                      ],
                    ),
                  );
                  onChanged();
                },
              ),
              const Spacer(),
              _Action(
                icon: CupertinoIcons.trash,
                label: 'Remover',
                onTap: () {
                  controller.removeSceneLight(layer.id, l.id);
                  onChanged();
                },
              ),
            ],
          ),
          const SizedBox(height: 10),
        ],
        Wrap(
          spacing: 8,
          children: [
            for (final k in Light3DKind.values)
              _Action(
                icon: CupertinoIcons.lightbulb,
                label: switch (k) {
                  Light3DKind.directional => 'Direcional',
                  Light3DKind.point => 'Ponto',
                  Light3DKind.ambient => 'Ambiente',
                  Light3DKind.spot => 'Spot',
                },
                onTap: () {
                  controller.addSceneLight(layer.id, k);
                  onChanged();
                },
              ),
          ],
        ),
      ],
    );
  }
}

// ------------------------------------------------------------- camera

class _CameraTab extends StatelessWidget {
  const _CameraTab({
    required this.layer,
    required this.controller,
    required this.compWidth,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final double compWidth;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final cam = layer.camera;
    final focal = cam.focalLength.base;
    final fov = cam.fovAt(Duration.zero);
    final zoom = cam.zoomAt(Duration.zero, compWidth);

    void setFocal(double mm) {
      controller.updateScene3DCamera(
        layer.id,
        (c) => c.copyWith(focalLength: c.focalLength.withBase(mm)),
      );
      onChanged();
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _Chips(
          label: 'Tipo',
          options: const ['Dois nos', 'Um no'],
          index: cam.kind.index,
          onChanged: (i) {
            // Converter NUNCA pode fazer a cena pular: o enquadramento
            // atual vira ponto de interesse (ou orientacao) equivalente.
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.convertedTo(CameraKind.values[i], Duration.zero),
            );
            onChanged();
          },
        ),
        const _Hint(
          'Dois nos olha sempre para o ponto de interesse. Um no e livre. '
          'Trocar preserva o enquadramento.',
        ),
        const SizedBox(height: 6),
        _SectionTitle('Lente'),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final mm in lensPresets)
              GestureDetector(
                onTap: () => setFocal(mm),
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 6,
                  ),
                  decoration: BoxDecoration(
                    color: (focal - mm).abs() < 0.5
                        ? AmColors.accentDim
                        : AmColors.chip,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    '${mm.toInt()}mm',
                    style: const TextStyle(
                      fontSize: 12,
                      color: AmColors.accent,
                    ),
                  ),
                ),
              ),
          ],
        ),
        const SizedBox(height: 6),
        // OS TRES JEITOS DE VER A MESMA GRANDEZA, ao vivo. Mexer num
        // recalcula os outros dois — e o que faz a relacao ficar clara.
        _Plain(
          label: 'Focal (mm)',
          value: focal,
          min: 5,
          max: 400,
          decimals: 1,
          onChanged: setFocal,
        ),
        _Plain(
          label: 'Angulo (°)',
          value: fov,
          min: 4,
          max: 160,
          decimals: 1,
          onChanged: (v) =>
              setFocal(focalFromFov(v, filmWidth: layer.camera.filmWidth)),
        ),
        _Plain(
          label: 'Zoom (px)',
          value: zoom,
          min: 100,
          max: 20000,
          decimals: 0,
          onChanged: (v) => setFocal(
            focalFromZoom(v, compWidth, filmWidth: layer.camera.filmWidth),
          ),
        ),
        _Plain(
          label: 'Filme (mm)',
          value: cam.filmWidth,
          min: 8,
          max: 70,
          decimals: 1,
          onChanged: (v) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(filmWidth: v),
            );
            onChanged();
          },
        ),
        _Toggle(
          label: 'Ortografica',
          value: cam.orthographic,
          onChanged: (v) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(orthographic: v),
            );
            onChanged();
          },
        ),
        const SizedBox(height: 10),
        _SectionTitle('Posicao'),
        _Num(
          label: 'X',
          track: cam.posX,
          min: -3000,
          max: 3000,
          onChanged: (v) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(posX: c.posX.withBase(v)),
            );
            onChanged();
          },
        ),
        _Num(
          label: 'Y',
          track: cam.posY,
          min: -3000,
          max: 3000,
          onChanged: (v) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(posY: c.posY.withBase(v)),
            );
            onChanged();
          },
        ),
        _Num(
          label: 'Z',
          track: cam.posZ,
          min: -3000,
          max: 3000,
          onChanged: (v) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(posZ: c.posZ.withBase(v)),
            );
            onChanged();
          },
        ),
        if (cam.kind == CameraKind.twoNode) ...[
          const SizedBox(height: 8),
          _SectionTitle('Ponto de interesse'),
          _Num(
            label: 'X',
            track: cam.poiX,
            min: -3000,
            max: 3000,
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(poiX: c.poiX.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Y',
            track: cam.poiY,
            min: -3000,
            max: 3000,
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(poiY: c.poiY.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Z',
            track: cam.poiZ,
            min: -3000,
            max: 3000,
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(poiZ: c.poiZ.withBase(v)),
              );
              onChanged();
            },
          ),
        ] else ...[
          const SizedBox(height: 8),
          _SectionTitle('Orientacao (caminho curto)'),
          _Num(
            label: 'X',
            track: cam.orientX,
            min: -180,
            max: 180,
            suffix: '°',
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(orientX: c.orientX.withBase(v)),
              );
              onChanged();
            },
          ),
          _Num(
            label: 'Y',
            track: cam.orientY,
            min: -180,
            max: 180,
            suffix: '°',
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(orientY: c.orientY.withBase(v)),
              );
              onChanged();
            },
          ),
          _SectionTitle('Rotacao (aditiva, aceita varias voltas)'),
          _Num(
            label: 'Y',
            track: cam.rotY,
            min: -1080,
            max: 1080,
            suffix: '°',
            onChanged: (v) {
              controller.updateScene3DCamera(
                layer.id,
                (c) => c.copyWith(rotY: c.rotY.withBase(v)),
              );
              onChanged();
            },
          ),
        ],
        const SizedBox(height: 10),
        _Chips(
          label: 'Auto-orientar',
          options: const ['Desligado', 'Seguir caminho', 'Para o alvo'],
          index: cam.autoOrient.index,
          onChanged: (i) {
            controller.updateScene3DCamera(
              layer.id,
              (c) => c.copyWith(autoOrient: AutoOrient.values[i]),
            );
            onChanged();
          },
        ),
        const SizedBox(height: 12),
        _SectionTitle('Rigs em um toque'),
        const _Hint(
          'Cada rig gera KEYFRAMES REAIS na camera, editaveis depois. '
          'Nenhum e caixa-preta.',
        ),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (final rig in CameraRig.values)
              _Action(
                icon: switch (rig) {
                  CameraRig.orbit => CupertinoIcons.arrow_2_circlepath,
                  CameraRig.tripod => CupertinoIcons.arrow_left_right,
                  CameraRig.dolly => CupertinoIcons.arrow_up_right,
                  CameraRig.handheld => CupertinoIcons.hand_raised,
                  CameraRig.dollyZoom => CupertinoIcons.scope,
                },
                label: cameraRigLabel(rig),
                onTap: () {
                  controller.applyRigToScene(layer.id, rig);
                  onChanged();
                },
              ),
          ],
        ),
        const SizedBox(height: 12),
        _SectionTitle('Comandos'),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            _Action(
              icon: CupertinoIcons.fullscreen,
              label: 'Enquadrar tudo',
              onTap: () {
                controller.frameSceneAll(layer.id);
                onChanged();
              },
            ),
            _Action(
              icon: CupertinoIcons.camera_viewfinder,
              label: 'Alinhar a vista',
              onTap: () {
                controller.alignCameraToCurrentView(layer.id);
                onChanged();
              },
            ),
          ],
        ),
      ],
    );
  }
}

// --------------------------------------------------------------- foco

class _DofTab extends StatelessWidget {
  const _DofTab({
    required this.layer,
    required this.controller,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final VoidCallback onChanged;

  void _dof(DepthOfField Function(DepthOfField) fn) {
    controller.updateScene3DCamera(layer.id, (c) => c.copyWith(dof: fn(c.dof)));
    onChanged();
  }

  @override
  Widget build(BuildContext context) {
    final d = layer.camera.dof;
    final fStop = d.fStopFor(layer.camera.focalLength.base, Duration.zero);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _Toggle(
          label: 'Profundidade de campo',
          value: d.enabled,
          onChanged: (v) => _dof((x) => x.copyWith(enabled: v)),
        ),
        const _Hint(
          'Usa a PROFUNDIDADE que a cena exporta — por isso o desfoque '
          'respeita a distancia real de cada objeto.',
        ),
        _Num(
          label: 'Foco',
          track: d.focusDistance,
          min: 10,
          max: 5000,
          onChanged: (v) => _dof(
            (x) => x.copyWith(focusDistance: x.focusDistance.withBase(v)),
          ),
        ),
        _Toggle(
          label: 'Travar no zoom',
          value: d.lockToZoom,
          onChanged: (v) => _dof((x) => x.copyWith(lockToZoom: v)),
        ),
        _Num(
          label: 'Abertura',
          track: d.aperture,
          min: 1,
          max: 300,
          onChanged: (v) =>
              _dof((x) => x.copyWith(aperture: x.aperture.withBase(v))),
        ),
        Padding(
          padding: const EdgeInsets.only(left: 2, bottom: 6),
          child: Text(
            'Diafragma f/${fStop.toStringAsFixed(1)}',
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ),
        _Num(
          label: 'Desfoque',
          track: d.blurLevel,
          min: 0,
          max: 200,
          suffix: '%',
          onChanged: (v) =>
              _dof((x) => x.copyWith(blurLevel: x.blurLevel.withBase(v))),
        ),
        const SizedBox(height: 10),
        _SectionTitle('Iris — a forma do bokeh'),
        _Chips(
          label: 'Formato',
          options: [for (final s in IrisShape.values) irisLabel(s)],
          index: d.irisShape.index,
          onChanged: (i) =>
              _dof((x) => x.copyWith(irisShape: IrisShape.values[i])),
        ),
        _Num(
          label: 'Girar iris',
          track: d.irisRotation,
          min: -180,
          max: 180,
          suffix: '°',
          onChanged: (v) =>
              _dof((x) => x.copyWith(irisRotation: x.irisRotation.withBase(v))),
        ),
        _Num(
          label: 'Arredondar',
          track: d.irisRoundness,
          min: -100,
          max: 100,
          onChanged: (v) => _dof(
            (x) => x.copyWith(irisRoundness: x.irisRoundness.withBase(v)),
          ),
        ),
        _Num(
          label: 'Proporcao',
          track: d.irisAspect,
          min: 0.3,
          max: 3,
          decimals: 2,
          onChanged: (v) =>
              _dof((x) => x.copyWith(irisAspect: x.irisAspect.withBase(v))),
        ),
        _Num(
          label: 'Franja',
          track: d.diffractionFringe,
          min: 0,
          max: 100,
          onChanged: (v) => _dof(
            (x) =>
                x.copyWith(diffractionFringe: x.diffractionFringe.withBase(v)),
          ),
        ),
        const SizedBox(height: 10),
        _SectionTitle('Realce — o que separa lente de borrao'),
        const _Hint(
          'Sem ganho e limiar, luz fora de foco vira mancha cinza. Com '
          'eles, vira a bola brilhante que a gente reconhece como foto.',
        ),
        _Num(
          label: 'Ganho',
          track: d.highlightGain,
          min: 0,
          max: 100,
          onChanged: (v) => _dof(
            (x) => x.copyWith(highlightGain: x.highlightGain.withBase(v)),
          ),
        ),
        _Num(
          label: 'Limiar',
          track: d.highlightThreshold,
          min: 0,
          max: 1,
          decimals: 2,
          onChanged: (v) => _dof(
            (x) => x.copyWith(
              highlightThreshold: x.highlightThreshold.withBase(v),
            ),
          ),
        ),
        _Num(
          label: 'Saturacao',
          track: d.highlightSaturation,
          min: 0,
          max: 2,
          decimals: 2,
          onChanged: (v) => _dof(
            (x) => x.copyWith(
              highlightSaturation: x.highlightSaturation.withBase(v),
            ),
          ),
        ),
      ],
    );
  }
}

// ------------------------------------------------------------- ajudas

class _HelpersTab extends StatelessWidget {
  const _HelpersTab({
    required this.layer,
    required this.controller,
    required this.onChanged,
  });

  final Scene3DLayer layer;
  final EditorController controller;
  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final s = layer.scene;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _Hint(
          'Nenhuma ajuda aparece na exportacao — grade, frustum, eixos e '
          'plano de foco existem so no preview.',
        ),
        _Toggle(
          label: 'Ajudas de cena',
          value: layer.showHelpers,
          onChanged: (v) {
            controller.setScene3DHelpers(layer.id, v);
            onChanged();
          },
        ),
        _Toggle(
          label: 'Grade do chao',
          value: s.showFloorGrid,
          onChanged: (v) {
            controller.updateScene3D(
              layer.id,
              (x) => x.copyWith(showFloorGrid: v),
            );
            onChanged();
          },
        ),
        _Toggle(
          label: 'Suavizacao (MSAA)',
          value: s.msaa,
          onChanged: (v) {
            controller.updateScene3D(layer.id, (x) => x.copyWith(msaa: v));
            onChanged();
          },
        ),
        _Toggle(
          label: 'Modo rascunho 3D',
          value: s.draftMode,
          onChanged: (v) {
            controller.updateScene3D(layer.id, (x) => x.copyWith(draftMode: v));
            onChanged();
          },
        ),
        const _Hint(
          'Rascunho desliga sombra, profundidade de campo e ambiente por '
          'imagem SO no preview. Liga sozinho durante o gesto no estudio.',
        ),
        const SizedBox(height: 12),
        _SectionTitle('Vistas salvas'),
        if (s.savedViews.isEmpty)
          const _Hint('Nenhuma ainda. Salve enquadramentos no estudio.'),
        for (var i = 0; i < s.savedViews.length; i++)
          Container(
            margin: const EdgeInsets.symmetric(vertical: 3),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            decoration: BoxDecoration(
              color: AmColors.chip,
              borderRadius: BorderRadius.circular(9),
            ),
            child: Row(
              children: [
                Expanded(
                  child: Text(
                    s.savedViews[i].name,
                    style: const TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
                CupertinoButton(
                  padding: EdgeInsets.zero,
                  minimumSize: const Size(30, 30),
                  onPressed: () {
                    controller.applySavedView(layer.id, s.savedViews[i]);
                    onChanged();
                  },
                  child: const Icon(
                    CupertinoIcons.camera_viewfinder,
                    size: 17,
                    color: AmColors.accent,
                  ),
                ),
                CupertinoButton(
                  padding: EdgeInsets.zero,
                  minimumSize: const Size(30, 30),
                  onPressed: () {
                    controller.removeSceneView(layer.id, i);
                    onChanged();
                  },
                  child: const Icon(
                    CupertinoIcons.trash,
                    size: 15,
                    color: AmColors.muted,
                  ),
                ),
              ],
            ),
          ),
      ],
    );
  }
}

// ---------------------------------------------------------- controles

class _Tabs extends StatelessWidget {
  const _Tabs({
    required this.labels,
    required this.index,
    required this.onChanged,
  });

  final List<String> labels;
  final int index;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 38,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 16),
        itemCount: labels.length,
        separatorBuilder: (_, _) => const SizedBox(width: 8),
        itemBuilder: (_, i) => GestureDetector(
          onTap: () => onChanged(i),
          child: Container(
            alignment: Alignment.center,
            padding: const EdgeInsets.symmetric(horizontal: 14),
            decoration: BoxDecoration(
              color: i == index ? AmColors.accentDim : AmColors.chip,
              borderRadius: BorderRadius.circular(10),
            ),
            child: Text(
              labels[i],
              style: TextStyle(
                fontSize: 13,
                fontWeight: i == index ? FontWeight.w700 : FontWeight.w400,
                color: i == index ? AmColors.accent : AmColors.muted,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);

  final String text;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(top: 6, bottom: 4),
    child: Text(
      text,
      style: const TextStyle(
        fontSize: 13,
        fontWeight: FontWeight.w700,
        color: AmColors.text,
      ),
    ),
  );
}

class _Hint extends StatelessWidget {
  const _Hint(this.text);

  final String text;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Text(
      text,
      style: const TextStyle(fontSize: 11, height: 1.35, color: AmColors.muted),
    ),
  );
}

/// Linha de parametro ANIMAVEL (tem base e keyframes).
class _Num extends StatelessWidget {
  const _Num({
    required this.label,
    required this.track,
    required this.min,
    required this.max,
    required this.onChanged,
    this.decimals = 0,
    this.suffix = '',
  });

  final String label;
  final AnimatedDouble track;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final int decimals;
  final String suffix;

  @override
  Widget build(BuildContext context) => _Plain(
    label: label,
    value: track.base,
    min: min,
    max: max,
    decimals: decimals,
    suffix: suffix,
    animated: track.isAnimated,
    onChanged: onChanged,
  );
}

class _Plain extends StatelessWidget {
  const _Plain({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.decimals = 0,
    this.suffix = '',
    this.animated = false,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final int decimals;
  final String suffix;
  final bool animated;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(
            width: 92,
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 12,
                color: animated ? AmColors.accent : AmColors.muted,
              ),
            ),
          ),
          Expanded(
            child: AmTickRuler(
              value: value,
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 340,
              height: 40,
              onChanged: onChanged,
            ),
          ),
          SizedBox(
            width: 62,
            child: Text(
              '${value.toStringAsFixed(decimals)}$suffix',
              textAlign: TextAlign.right,
              style: const TextStyle(fontSize: 12, color: AmColors.text),
            ),
          ),
        ],
      ),
    );
  }
}

class _Chips extends StatelessWidget {
  const _Chips({
    required this.label,
    required this.options,
    required this.index,
    required this.onChanged,
  });

  final String label;
  final List<String> options;
  final int index;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 92,
            child: Text(
              label,
              style: const TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          ),
          Expanded(
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (var i = 0; i < options.length; i++)
                  GestureDetector(
                    onTap: () => onChanged(i),
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 10,
                        vertical: 5,
                      ),
                      decoration: BoxDecoration(
                        color: i == index ? AmColors.accentDim : AmColors.chip,
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: Text(
                        options[i],
                        style: const TextStyle(
                          fontSize: 11,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _Toggle extends StatelessWidget {
  const _Toggle({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          Expanded(
            child: Text(
              label,
              style: const TextStyle(fontSize: 13, color: AmColors.text),
            ),
          ),
          CupertinoSwitch(
            value: value,
            activeTrackColor: AmColors.accent,
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }
}

class _Action extends StatelessWidget {
  const _Action({required this.icon, required this.label, required this.onTap});

  final IconData icon;
  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 7),
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 14, color: AmColors.accent),
            const SizedBox(width: 6),
            Text(
              label,
              style: const TextStyle(fontSize: 12, color: AmColors.accent),
            ),
          ],
        ),
      ),
    );
  }
}

class _ColorRow extends StatelessWidget {
  const _ColorRow({required this.color, required this.onColor});

  final Color color;
  final ValueChanged<Color> onColor;

  static const _palette = <Color>[
    Color(0xFFB8FF3D),
    Color(0xFF7C62FF),
    Color(0xFF35C4E7),
    Color(0xFFFF6B6B),
    Color(0xFFFFB020),
    Color(0xFFE9EDF2),
    Color(0xFF2BE3A0),
    Color(0xFF8B94A3),
  ];

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          const SizedBox(
            width: 92,
            child: Text(
              'Cor',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          ),
          // Espectro completo — qualquer cor, nao so a paleta.
          ColorWell(color: color, onChanged: onColor, size: 28),
          const SizedBox(width: 10),
          Expanded(
            child: Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                for (final c in _palette)
                  GestureDetector(
                    onTap: () => onColor(c),
                    child: Container(
                      width: 24,
                      height: 24,
                      decoration: BoxDecoration(
                        color: c,
                        shape: BoxShape.circle,
                        border: Border.all(
                          color: c.toARGB32() == color.toARGB32()
                              ? AmColors.text
                              : Colors.transparent,
                          width: 2,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// SELO DISCRETO do orcamento (§10) — nunca um dialogo.
/// O AVISO DE QUE A CENA ESTA NO PINTOR EM CPU.
///
/// Sem motor em GPU nao ha profundidade real, sombra nem brilho — e o
/// desenho custa dezenas de vezes mais, o que aparece como engasgo na
/// reproducao. Isso precisa estar escrito onde a pessoa mexe na cena, e
/// nao so num log que ninguem le.
class _AvisoDoMotor extends StatelessWidget {
  const _AvisoDoMotor();

  @override
  Widget build(BuildContext context) {
    if (!Scene3DGpu.indisponivel) return const SizedBox.shrink();
    final motivo = Scene3DGpu.motivo;
    return Container(
      margin: const EdgeInsets.fromLTRB(18, 0, 18, 8),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        color: const Color(0x33FFB020),
        borderRadius: BorderRadius.circular(9),
      ),
      child: Text(
        'Este aparelho esta desenhando a cena no pintor em CPU: sem sombra '
        'nem brilho, e bem mais lento.'
        '${motivo.isEmpty ? '' : ' Motivo: $motivo'}',
        style: const TextStyle(
          fontSize: 11,
          height: 1.3,
          color: Color(0xFFFFC868),
        ),
      ),
    );
  }
}

class _BudgetBadge extends StatelessWidget {
  const _BudgetBadge({required this.layer});

  final Scene3DLayer layer;

  @override
  Widget build(BuildContext context) {
    final frame = renderScene(
      layer.scene,
      layer.camera.renderAt(Duration.zero),
      const Size(1080, 1920),
      Duration.zero,
    );
    final over =
        frame.drawCalls > lowProfileBudget.maxDrawCalls ||
        frame.triangles > lowProfileBudget.maxTriangles ||
        estimateSceneMemoryMb(layer.scene) > lowProfileBudget.maxMemoryMb;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: over ? const Color(0x33FF6B6B) : AmColors.chip,
        borderRadius: BorderRadius.circular(7),
      ),
      child: Text(
        '${Scene3DGpu.comoDesenha} · ${frame.drawCalls} chamadas · '
        '${frame.triangles} tri · '
        '${estimateSceneMemoryMb(layer.scene).toStringAsFixed(1)} MB',
        style: TextStyle(
          fontSize: 10,
          color: over ? AmColors.pink : AmColors.muted,
        ),
      ),
    );
  }
}
