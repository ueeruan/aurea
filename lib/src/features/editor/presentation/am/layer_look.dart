import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../domain/layer.dart';

/// COR E ICONE POR TIPO DE CAMADA.
///
/// Numa linha do tempo com dez faixas todas do mesmo violeta, achar o
/// audio no meio dos videos custa ler dez rotulos. Cor e icone resolvem
/// isso antes da leitura: a pessoa reconhece a faixa pela mancha, e so
/// le o nome quando precisa distinguir duas do mesmo tipo.
///
/// As cores sao ESCURAS de proposito — a barra e fundo para o nome, a
/// forma de onda e as miniaturas. Cor saturada aqui disputaria com o
/// conteudo que ela deveria emoldurar.
Color layerTypeColor(Layer l) => switch (l) {
      VideoLayer() => const Color(0xFF6A52E0),
      ImageLayer() => const Color(0xFF3D6FD9),
      AudioLayer() => const Color(0xFF1F8C93),
      TextLayer() => const Color(0xFFB07A16),
      CaptionLayer() => const Color(0xFF8A6A1E),
      ShapeLayer() => const Color(0xFF2E9459),
      ParticlesLayer() => const Color(0xFFB0417A),
      Element3DLayer() => const Color(0xFFC06A24),
      Scene3DLayer() => const Color(0xFFA85520),
      GroupLayer() => const Color(0xFF4C5566),
      AdjustmentLayer() => const Color(0xFF5A4A7A),
      NullLayer() => const Color(0xFF444C5C),
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
      ParticlesLayer() => CupertinoIcons.sparkles,
      Element3DLayer() => CupertinoIcons.cube_fill,
      Scene3DLayer() => CupertinoIcons.cube_box_fill,
      GroupLayer() => CupertinoIcons.folder_fill,
      AdjustmentLayer() => CupertinoIcons.slider_horizontal_3,
      NullLayer() => CupertinoIcons.smallcircle_circle,
    };
