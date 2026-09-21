import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../features/editor/domain/layer.dart';

/// COR E ICONE POR TIPO DE CAMADA (design system: a timeline, o menu de
/// adicionar e o escolher-pai pintam com a MESMA tabela).
///
/// Numa linha do tempo com dez faixas todas do mesmo violeta, achar o
/// audio no meio dos videos custa ler dez rotulos. Cor e icone resolvem
/// isso antes da leitura: a pessoa reconhece a faixa pela mancha, e so
/// le o nome quando precisa distinguir duas do mesmo tipo.
///
/// As cores sao ESCURAS de proposito — a barra e fundo para o nome, a
/// forma de onda e as miniaturas. Cor saturada aqui disputaria com o
/// conteudo que ela deveria emoldurar.
Color layerTypeColor(Layer l) => layerKindColor(kindOf(l));

/// OS DOZE TIPOS, sem precisar de uma instancia: e o que o menu de
/// adicionar (E1) e os cabecalhos usam para pintar com a MESMA cor que
/// a barra vai ter na timeline (Fase 2: cor por tipo em tudo).
enum LayerKind {
  video,
  image,
  audio,
  text,
  caption,
  shape,
  particles,
  element3d,
  scene3d,
  camera,
  group,
  adjustment,
  nullLayer,
}

LayerKind kindOf(Layer l) => switch (l) {
  VideoLayer() => LayerKind.video,
  ImageLayer() => LayerKind.image,
  AudioLayer() => LayerKind.audio,
  TextLayer() => LayerKind.text,
  CaptionLayer() => LayerKind.caption,
  ShapeLayer() => LayerKind.shape,
  ParticulasLayer() => LayerKind.particles,
  Element3DLayer() => LayerKind.element3d,
  Scene3DLayer() => LayerKind.scene3d,
  CameraLayer() => LayerKind.camera,
  GroupLayer() => LayerKind.group,
  AdjustmentLayer() => LayerKind.adjustment,
  NullLayer() => LayerKind.nullLayer,
};

Color layerKindColor(LayerKind k) => switch (k) {
  LayerKind.video => const Color(0xFF6A52E0),
  LayerKind.image => const Color(0xFF3D6FD9),
  LayerKind.audio => const Color(0xFF1F8C93),
  LayerKind.text => const Color(0xFFB07A16),
  LayerKind.caption => const Color(0xFF8A6A1E),
  LayerKind.shape => const Color(0xFF2E9459),
  LayerKind.particles => const Color(0xFFB0417A),
  LayerKind.element3d => const Color(0xFFC06A24),
  LayerKind.scene3d => const Color(0xFFA85520),
  LayerKind.camera => const Color(0xFF2A7B9B),
  LayerKind.group => const Color(0xFF4C5566),
  LayerKind.adjustment => const Color(0xFF5A4A7A),
  LayerKind.nullLayer => const Color(0xFF444C5C),
};

IconData layerKindIcon(LayerKind k) => switch (k) {
  LayerKind.video => CupertinoIcons.videocam_fill,
  LayerKind.image => CupertinoIcons.photo_fill,
  LayerKind.audio => CupertinoIcons.music_note,
  LayerKind.text => CupertinoIcons.textformat,
  LayerKind.caption => CupertinoIcons.captions_bubble_fill,
  LayerKind.shape => CupertinoIcons.circle_fill,
  LayerKind.particles => CupertinoIcons.sparkles,
  LayerKind.element3d => CupertinoIcons.cube_fill,
  LayerKind.scene3d => CupertinoIcons.cube_box_fill,
  LayerKind.camera => CupertinoIcons.camera_fill,
  LayerKind.group => CupertinoIcons.folder_fill,
  LayerKind.adjustment => CupertinoIcons.slider_horizontal_3,
  LayerKind.nullLayer => CupertinoIcons.smallcircle_circle,
};

/// A cor clara da mesma familia, para as listras da camada selecionada.
Color layerTypeStripe(Layer l) =>
    Color.lerp(layerTypeColor(l), Colors.white, 0.28)!;

IconData layerTypeIcon(Layer l) => switch (l) {
  VideoLayer() => CupertinoIcons.videocam_fill,
  ImageLayer() => CupertinoIcons.photo_fill,
  AudioLayer() => CupertinoIcons.music_note,
  TextLayer() => CupertinoIcons.textformat,
  CaptionLayer() => CupertinoIcons.captions_bubble_fill,
  ShapeLayer() => CupertinoIcons.circle_fill,
  ParticulasLayer() => CupertinoIcons.sparkles,
  Element3DLayer() => CupertinoIcons.cube_fill,
  Scene3DLayer() => CupertinoIcons.cube_box_fill,
  CameraLayer() => CupertinoIcons.camera_fill,
  GroupLayer() => CupertinoIcons.folder_fill,
  AdjustmentLayer() => CupertinoIcons.slider_horizontal_3,
  NullLayer() => CupertinoIcons.smallcircle_circle,
};
