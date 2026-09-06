import 'dart:math' as math;
import 'dart:ui';

import 'caption.dart';

/// LEGENDA COM DESTAQUE — a frase inteira na tela, e a palavra que esta
/// sendo dita inflando no lugar dela.
///
/// Nao e motor novo: e a receita de texto com unidade PALAVRA, dirigida
/// pelos tempos por palavra do Whisper, animando escala e cor em vez de
/// so cor. O mesmo caminho do karaoke.
///
/// A regra que manda em tudo: A FRASE E DIAGRAMADA UMA VEZ. Se o layout
/// recalculasse a cada palavra, as vizinhas pulariam de lugar e o texto
/// ficaria ilegivel — que e o defeito classico dessa animacao.

/// Como a frase se arruma na tela.
enum HighlightLayout {
  /// Palavra gigante centralizada, contexto numa linha cruzando ela.
  atravessada,

  /// Minusculas num canto, varias linhas, entrelinha apertada.
  empilhada,

  /// Duas palavras grandes lado a lado.
  dupla,

  /// Tela dividida, palavras assentadas na divisa.
  costura,

  /// So a palavra, centralizada.
  sozinha,
}

extension HighlightLayoutX on HighlightLayout {
  String get rotulo => switch (this) {
        HighlightLayout.atravessada => 'Atravessada',
        HighlightLayout.empilhada => 'Empilhada',
        HighlightLayout.dupla => 'Dupla',
        HighlightLayout.costura => 'Costura',
        HighlightLayout.sozinha => 'Sozinha',
      };

  /// Quantas palavras de contexto o arranjo comporta de cada lado.
  int get contextoPorLado => switch (this) {
        HighlightLayout.sozinha => 0,
        HighlightLayout.dupla => 1,
        _ => 2,
      };
}

/// O TETO DO CONTEXTO. A frase inteira em volta polui; duas de cada lado
/// bastam para dar sentido sem virar paragrafo.
const int kMaxContextoPorLado = 2;

/// UMA FRASE DIAGRAMADA — o grupo de palavras que fica na tela junto.
///
/// O grupo e FIXO enquanto qualquer palavra dele esta sendo dita. E isso
/// que garante que as vizinhas nao se movem: elas nao entram e saem, o
/// grupo inteiro e que troca.
class CaptionPhrase {
  const CaptionPhrase(this.palavras);

  final List<Cue> palavras;

  Duration get start => palavras.first.start;
  Duration get end => palavras.last.end;

  bool contem(Duration t) => t >= start && t < end;

  /// O indice da palavra que esta sendo dita, ou null entre palavras.
  ///
  /// Entre uma palavra e a proxima o destaque SEGURA na ultima dita, em
  /// vez de apagar: piscar a cada respiracao seria pior que atrasar.
  int? ativaEm(Duration t) {
    if (palavras.isEmpty) return null;
    int? ultima;
    for (var i = 0; i < palavras.length; i++) {
      final p = palavras[i];
      if (t >= p.start && t < p.end) return i;
      if (t >= p.end) ultima = i;
    }
    return ultima;
  }
}

/// AGRUPA AS PALAVRAS EM FRASES.
///
/// Corta o grupo em tres situacoes: encheu ([maxPalavras]), houve pausa
/// de [pausaMinima], ou a palavra terminou com pontuacao forte. Pontuacao
/// e o sinal mais confiavel dos tres — o Whisper a coloca onde a pessoa
/// realmente parou.
List<CaptionPhrase> agruparEmFrases(
  List<Cue> palavras, {
  int maxPalavras = 1 + kMaxContextoPorLado * 2,
  Duration pausaMinima = const Duration(milliseconds: 420),
}) {
  if (palavras.isEmpty) return const [];
  final ordenadas = [...palavras]..sort((a, b) => a.start.compareTo(b.start));

  final out = <CaptionPhrase>[];
  var atual = <Cue>[ordenadas.first];
  for (var i = 1; i < ordenadas.length; i++) {
    final anterior = ordenadas[i - 1];
    final palavra = ordenadas[i];
    final pausou = palavra.start - anterior.end >= pausaMinima;
    final pontuou = RegExp(r'[.!?…]\s*$').hasMatch(anterior.text);
    if (atual.length >= maxPalavras || pausou || pontuou) {
      out.add(CaptionPhrase(atual));
      atual = [palavra];
    } else {
      atual.add(palavra);
    }
  }
  out.add(CaptionPhrase(atual));
  return out;
}

/// A frase que esta no ar em [t], ou null.
CaptionPhrase? fraseEm(List<CaptionPhrase> frases, Duration t) {
  for (final f in frases) {
    if (f.contem(t)) return f;
  }
  return null;
}

// ------------------------------------------------------------- a escala

/// Quanto tempo a palavra leva para inflar.
const Duration kDuracaoDoInflar = Duration(milliseconds: 220);

/// A CURVA DO INFLAR — mola criticamente amortecida.
///
/// `1 - e^(-kt)(1 + kt)` sobe rapido e encosta no alvo SEM PASSAR. Uma
/// mola comum daria overshoot, e overshoot em legenda vira ruido: o olho
/// le o tranco, nao a palavra.
double inflar(double progresso) {
  if (progresso <= 0) return 0;
  if (progresso >= 1) return 1;
  const k = 6.0;
  final x = k * progresso;
  return 1 - math.exp(-x) * (1 + x);
}

/// A ESCALA DE UMA PALAVRA neste instante.
///
/// [destaque] em 1,0 devolve 1,0 sempre — destaque do tamanho do contexto
/// e legenda comum, e neutro tem de ser neutro.
double escalaDaPalavra({
  required int indice,
  required int? ativa,
  required CaptionPhrase frase,
  required Duration t,
  required double destaque,
  Duration duracao = kDuracaoDoInflar,
}) {
  if (destaque == 1.0 || ativa == null || indice != ativa) return 1;
  final inicio = frase.palavras[indice].start;
  final decorrido = t - inicio;
  if (decorrido <= Duration.zero) return 1;
  final p = decorrido.inMicroseconds / duracao.inMicroseconds;
  return 1 + (destaque - 1) * inflar(p.clamp(0.0, 1.0));
}

/// A COR DA PALAVRA neste instante — entra junto com a escala.
Color corDaPalavra({
  required int indice,
  required int? ativa,
  required CaptionPhrase frase,
  required Duration t,
  required Color contexto,
  required Color destaque,
  Duration duracao = kDuracaoDoInflar,
}) {
  if (ativa == null || indice != ativa) return contexto;
  final decorrido = t - frase.palavras[indice].start;
  if (decorrido <= Duration.zero) return contexto;
  final p = (decorrido.inMicroseconds / duracao.inMicroseconds).clamp(0.0, 1.0);
  return Color.lerp(contexto, destaque, inflar(p)) ?? destaque;
}

// -------------------------------------------------------------- caber

/// O QUANTO A PALAVRA PRECISA ENCOLHER para caber na margem segura.
///
/// Palavra longa estourando a tela e o bug classico desta animacao —
/// "INFINITAS POSSIBILIDADES" em corpo 120 nao cabe em celular nenhum.
/// Devolve o fator (<= 1) que aplica em cima do tamanho pedido.
double fatorParaCaber({
  required double larguraDoTexto,
  required double larguraDisponivel,
}) {
  if (larguraDoTexto <= 0 || larguraDisponivel <= 0) return 1;
  if (larguraDoTexto <= larguraDisponivel) return 1;
  return larguraDisponivel / larguraDoTexto;
}

// ------------------------------------------------------------- estilo

/// O ESTILO DESTAQUE de uma camada de legenda.
///
/// E da CAMADA, nao da fala: trocar o preset muda as 47 falas de uma vez.
class CaptionHighlightStyle {
  const CaptionHighlightStyle({
    this.ativo = false,
    this.layout = HighlightLayout.atravessada,
    this.destaque = 1.9,
    this.corDestaque = const Color(0xFFE23B3B),
    this.corContexto = const Color(0xFFFFFFFF),
    this.fonteDestaque,
    this.fonteContexto,
    this.maiusculas = true,
    this.tracking = 0,
    this.entrelinha = 1.05,
    this.duracaoInflar = kDuracaoDoInflar,
    this.contextoPorLado = kMaxContextoPorLado,
    this.atrasDaPessoa = false,
  });

  final bool ativo;
  final HighlightLayout layout;

  /// Quanto a palavra dita cresce. 1,0 = do tamanho do contexto.
  final double destaque;

  final Color corDestaque;
  final Color corContexto;

  /// Nulo usa a fonte generica do sistema. Quem importou um `.ttf` pelo
  /// nivel 2 poe o nome da familia aqui.
  final String? fonteDestaque;
  final String? fonteContexto;

  final bool maiusculas;
  final double tracking;
  final double entrelinha;
  final Duration duracaoInflar;

  /// No maximo [kMaxContextoPorLado].
  final int contextoPorLado;

  /// Usa a mascara de segmentacao para a palavra passar ATRAS do sujeito.
  final bool atrasDaPessoa;

  bool get isNeutro => !ativo || destaque == 1.0;

  CaptionHighlightStyle copyWith({
    bool? ativo,
    HighlightLayout? layout,
    double? destaque,
    Color? corDestaque,
    Color? corContexto,
    String? fonteDestaque,
    String? fonteContexto,
    bool? maiusculas,
    double? tracking,
    double? entrelinha,
    Duration? duracaoInflar,
    int? contextoPorLado,
    bool? atrasDaPessoa,
  }) => CaptionHighlightStyle(
        ativo: ativo ?? this.ativo,
        layout: layout ?? this.layout,
        destaque: destaque ?? this.destaque,
        corDestaque: corDestaque ?? this.corDestaque,
        corContexto: corContexto ?? this.corContexto,
        fonteDestaque: fonteDestaque ?? this.fonteDestaque,
        fonteContexto: fonteContexto ?? this.fonteContexto,
        maiusculas: maiusculas ?? this.maiusculas,
        tracking: tracking ?? this.tracking,
        entrelinha: entrelinha ?? this.entrelinha,
        duracaoInflar: duracaoInflar ?? this.duracaoInflar,
        contextoPorLado: contextoPorLado ?? this.contextoPorLado,
        atrasDaPessoa: atrasDaPessoa ?? this.atrasDaPessoa,
      );
}

/// OS CINCO PRESETS — a profundidade Pronto do estilo Destaque.
///
/// Cada um e uma combinacao de fonte, cor, layout e caixa. Nenhum e
/// codigo novo.
abstract final class HighlightPresets {
  /// Serifada, maiuscula, atravessada — o visual editorial.
  static const editorial = CaptionHighlightStyle(
    ativo: true,
    layout: HighlightLayout.atravessada,
    destaque: 2.1,
    corDestaque: Color(0xFFE23B3B),
    fonteDestaque: 'serif',
    maiusculas: true,
  );

  /// Grotesca, minuscula, empilhada, entrelinha apertada.
  static const manifesto = CaptionHighlightStyle(
    ativo: true,
    layout: HighlightLayout.empilhada,
    destaque: 1.7,
    corDestaque: Color(0xFFB8FF3D),
    maiusculas: false,
    tracking: -1.2,
    entrelinha: 0.92,
  );

  static const impacto = CaptionHighlightStyle(
    ativo: true,
    layout: HighlightLayout.sozinha,
    destaque: 2.4,
    corDestaque: Color(0xFFFFFFFF),
    corContexto: Color(0x66FFFFFF),
    maiusculas: true,
    tracking: -0.5,
  );

  static const sussurro = CaptionHighlightStyle(
    ativo: true,
    layout: HighlightLayout.empilhada,
    destaque: 1.35,
    corDestaque: Color(0xFFFFFFFF),
    corContexto: Color(0x99FFFFFF),
    maiusculas: false,
    duracaoInflar: Duration(milliseconds: 340),
  );

  static const clipe = CaptionHighlightStyle(
    ativo: true,
    layout: HighlightLayout.dupla,
    destaque: 1.9,
    corDestaque: Color(0xFF7C62FF),
    maiusculas: true,
    duracaoInflar: Duration(milliseconds: 150),
  );

  static const todos = <(String, CaptionHighlightStyle)>[
    ('Editorial', editorial),
    ('Manifesto', manifesto),
    ('Impacto', impacto),
    ('Sussurro', sussurro),
    ('Clipe', clipe),
  ];
}
