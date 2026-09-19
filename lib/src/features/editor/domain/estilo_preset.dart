import 'dart:ui';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'package:uuid/uuid.dart';

import 'keyframe.dart';
import 'layer_meta.dart';

/// PRESET DE ESTILO: o ACABAMENTO de uma camada — bordas, sombras,
/// brilho e sobreposicoes — guardado com nome para usar em qualquer
/// outra camada, de qualquer projeto.
///
/// Irmao do preset de efeito e diferente dele: efeito mexe nos PIXELS
/// que ja estao ali; estilo e o que se desenha EM VOLTA e POR CIMA da
/// camada. Quem salva "meu texto com contorno branco e sombra" quer
/// isto, e nao uma pilha de efeitos.
class EstiloPreset {
  EstiloPreset({
    String? id,
    required this.nome,
    required this.estilos,
    this.deFabrica = false,
    DateTime? criadoEm,
  }) : id = id ?? const Uuid().v4(),
       criadoEm = criadoEm ?? DateTime(2026);

  final String id;
  final String nome;
  final LayerStyles estilos;

  /// Estilo de fabrica: so leitura.
  final bool deFabrica;
  final DateTime criadoEm;

  EstiloPreset comNome(String novo) => EstiloPreset(
    id: id,
    nome: novo,
    estilos: estilos,
    deFabrica: deFabrica,
    criadoEm: criadoEm,
  );

  /// O QUE A PESSOA VE no cartao, em ordem de peso.
  List<String> get partes => [
    if (estilos.bordas.isNotEmpty)
      estilos.bordas.length == 1 ? 'borda' : '${estilos.bordas.length} bordas',
    if (estilos.dropShadow != null) 'sombra',
    if (estilos.innerShadow != null) 'sombra interna',
    if (estilos.outerGlow != null) 'brilho',
    if (estilos.colorOverlay != null) 'cor por cima',
    if (estilos.gradientOverlay != null) 'degradê por cima',
  ];
}

/// ESTILOS DE FABRICA: o ponto de partida de quem nunca salvou nenhum.
/// Sao da Aurea — cores e numeros nossos. O id e FIXO: a lista se
/// reconstroi a cada quadro, e um id sorteado faria cada cartao virar
/// outro widget toda vez.
List<EstiloPreset> estilosDeFabrica() => [
  EstiloPreset(
    id: 'fab-contorno',
    nome: 'Contorno branco',
    deFabrica: true,
    estilos: LayerStyles(
      stroke: StrokeStyle(
        color: const Color(0xFFFFFFFF),
        width: AnimatedDouble(10),
      ),
    ),
  ),
  EstiloPreset(
    id: 'fab-adesivo',
    nome: 'Adesivo',
    deFabrica: true,
    estilos: LayerStyles(
      stroke: StrokeStyle(
        color: const Color(0xFFFFFFFF),
        width: AnimatedDouble(14),
      ),
      dropShadow: ShadowStyle(
        color: const Color(0xFF000000),
        opacity: AnimatedDouble(.35),
        distance: AnimatedDouble(8),
        size: AnimatedDouble(12),
      ),
    ),
  ),
  EstiloPreset(
    id: 'fab-sombra',
    nome: 'Sombra macia',
    deFabrica: true,
    estilos: LayerStyles(
      dropShadow: ShadowStyle(
        color: const Color(0xFF05070A),
        opacity: AnimatedDouble(.45),
        angleDeg: AnimatedDouble(270),
        distance: AnimatedDouble(16),
        size: AnimatedDouble(28),
      ),
    ),
  ),
  EstiloPreset(
    id: 'fab-neon',
    nome: 'Néon',
    deFabrica: true,
    estilos: LayerStyles(
      outerGlow: GlowStyle(
        color: const Color(0xFF7BE3C9),
        opacity: AnimatedDouble(.9),
        size: AnimatedDouble(30),
      ),
      stroke: StrokeStyle(
        color: const Color(0xFFDFFFF6),
        width: AnimatedDouble(4),
      ),
    ),
  ),
  EstiloPreset(
    id: 'fab-recorte',
    nome: 'Recorte de revista',
    deFabrica: true,
    estilos: LayerStyles(
      stroke: StrokeStyle(
        color: const Color(0xFFFFFFFF),
        width: AnimatedDouble(16),
      ),
      bordasExtras: [
        StrokeStyle(color: AureaColors.bg, width: AnimatedDouble(22)),
      ],
      dropShadow: ShadowStyle(
        opacity: AnimatedDouble(.3),
        distance: AnimatedDouble(10),
        size: AnimatedDouble(8),
      ),
    ),
  ),
  EstiloPreset(
    id: 'fab-relevo',
    nome: 'Relevo',
    deFabrica: true,
    estilos: LayerStyles(
      innerShadow: ShadowStyle(
        color: const Color(0xFF000000),
        opacity: AnimatedDouble(.55),
        angleDeg: AnimatedDouble(120),
        distance: AnimatedDouble(6),
        size: AnimatedDouble(10),
      ),
      dropShadow: ShadowStyle(
        color: const Color(0xFFFFFFFF),
        opacity: AnimatedDouble(.25),
        angleDeg: AnimatedDouble(300),
        distance: AnimatedDouble(4),
        size: AnimatedDouble(2),
      ),
    ),
  ),
];
