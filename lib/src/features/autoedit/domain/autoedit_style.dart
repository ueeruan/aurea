import '../../editor/domain/caption.dart';

/// OS SEIS ESTILOS DO AUTOEDIT.
///
/// Cada um e um CONJUNTO DE PRESETS, nao codigo novo: escolher "Viral" so
/// diz quanto silencio cortar, como legendar, quanto zoom dar e o que
/// fazer com o som. Todo o resto ja existe no aplicativo.
///
/// E por isso que o resultado abre no editor como projeto comum — nao ha
/// motor secreto para desfazer depois.
enum AutoEditStyleId { limpo, podcast, viral, tutorial, entrevista, soLegendas }

/// Quanto o enquadramento fecha na troca de frase.
enum AutoEditZoom { nenhum, sutil, forte }

extension AutoEditZoomX on AutoEditZoom {
  /// A escala que o zoom alcanca. 1,0 e nao mexer.
  double get escala => switch (this) {
        AutoEditZoom.nenhum => 1.0,
        AutoEditZoom.sutil => 1.06,
        AutoEditZoom.forte => 1.18,
      };

  String get rotulo => switch (this) {
        AutoEditZoom.nenhum => 'Nenhum',
        AutoEditZoom.sutil => 'Sutil',
        AutoEditZoom.forte => 'Forte',
      };
}

class AutoEditStyle {
  const AutoEditStyle({
    required this.id,
    required this.nome,
    required this.descricao,
    required this.captionMode,
    required this.ritmo,
    required this.zoom,
    this.legendar = true,
    this.normalizar = true,
    this.melhorarVoz = false,
    this.ducking = false,
  });

  final AutoEditStyleId id;
  final String nome;
  final String descricao;

  /// Como a legenda e agrupada: palavra a palavra (karaoke) ou em frases.
  final CaptionMode captionMode;

  /// Quanto do silencio some, de 0 (nada) a 1 (tudo, menos a folga).
  final double ritmo;

  final AutoEditZoom zoom;
  final bool legendar;
  final bool normalizar;
  final bool melhorarVoz;

  /// Abaixar a musica sob a fala. So faz efeito se houver musica.
  final bool ducking;

  AutoEditStyle copyWith({
    double? ritmo,
    AutoEditZoom? zoom,
    CaptionMode? captionMode,
  }) => AutoEditStyle(
        id: id,
        nome: nome,
        descricao: descricao,
        captionMode: captionMode ?? this.captionMode,
        ritmo: ritmo ?? this.ritmo,
        zoom: zoom ?? this.zoom,
        legendar: legendar,
        normalizar: normalizar,
        melhorarVoz: melhorarVoz,
        ducking: ducking,
      );
}

abstract final class AutoEditStyles {
  static const limpo = AutoEditStyle(
    id: AutoEditStyleId.limpo,
    nome: 'Limpo',
    descricao: 'Legenda simples, sem zoom, audio normalizado.',
    captionMode: CaptionMode.frases,
    ritmo: 0.6,
    zoom: AutoEditZoom.nenhum,
  );

  static const podcast = AutoEditStyle(
    id: AutoEditStyleId.podcast,
    nome: 'Podcast',
    descricao: 'Legenda em caixa, zoom lento a cada frase, musica cedendo '
        'sob a fala.',
    captionMode: CaptionMode.frases,
    ritmo: 0.5,
    zoom: AutoEditZoom.sutil,
    ducking: true,
    melhorarVoz: true,
  );

  static const viral = AutoEditStyle(
    id: AutoEditStyleId.viral,
    nome: 'Viral',
    descricao: 'Karaoke palavra por palavra, zoom rapido, corte seco nos '
        'silencios.',
    captionMode: CaptionMode.palavra,
    ritmo: 1.0,
    zoom: AutoEditZoom.forte,
    melhorarVoz: true,
  );

  static const tutorial = AutoEditStyle(
    id: AutoEditStyleId.tutorial,
    nome: 'Tutorial',
    descricao: 'Legenda no rodape, zoom no que esta sendo dito, ritmo calmo.',
    captionMode: CaptionMode.frases,
    ritmo: 0.35,
    zoom: AutoEditZoom.sutil,
    melhorarVoz: true,
  );

  static const entrevista = AutoEditStyle(
    id: AutoEditStyleId.entrevista,
    nome: 'Entrevista',
    descricao: 'Corte das pausas longas, sem zoom.',
    captionMode: CaptionMode.frases,
    ritmo: 0.75,
    zoom: AutoEditZoom.nenhum,
  );

  /// O estilo NEUTRO: nada alem de legendar. E o que prova que o AutoEdit
  /// nao mexe no que nao foi pedido.
  static const soLegendas = AutoEditStyle(
    id: AutoEditStyleId.soLegendas,
    nome: 'So legendas',
    descricao: 'Nada alem de legendar. O video fica intacto.',
    captionMode: CaptionMode.frases,
    ritmo: 0,
    zoom: AutoEditZoom.nenhum,
    normalizar: false,
  );

  static const todos = <AutoEditStyle>[
    limpo,
    podcast,
    viral,
    tutorial,
    entrevista,
    soLegendas,
  ];

  static AutoEditStyle byId(AutoEditStyleId id) =>
      todos.firstWhere((e) => e.id == id);
}
