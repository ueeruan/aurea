import 'dart:math' as math;

import 'package:flutter/foundation.dart';

import '../../domain/orcamento_render.dart';

/// O PERFIL que a pessoa escolhe em Ajustes › Desempenho.
///
/// Automatico e o padrao e, com o aparelho frio, e exatamente o app de
/// antes: nada muda ate um sinal pedir. Os outros cravam um ponto de
/// partida; a escada termica continua valendo por cima deles, so para
/// BAIXO (ver [politicaPara]).
enum PerfilDeDesempenho {
  automatico,
  economia,
  equilibrado,
  desempenho,
  maximaQualidade,
}

String perfilDeDesempenhoRotulo(PerfilDeDesempenho p) => switch (p) {
  PerfilDeDesempenho.automatico => 'Automático',
  PerfilDeDesempenho.economia => 'Economia',
  PerfilDeDesempenho.equilibrado => 'Equilibrado',
  PerfilDeDesempenho.desempenho => 'Desempenho',
  PerfilDeDesempenho.maximaQualidade => 'Máxima qualidade',
};

/// Uma linha que explica o perfil, para os Ajustes.
String perfilDeDesempenhoExplicacao(PerfilDeDesempenho p) => switch (p) {
  PerfilDeDesempenho.automatico =>
    'Qualidade cheia com o aparelho frio; quando ele esquenta ou o quadro '
        'atrasa, só a prévia fica mais leve. A exportação nunca muda.',
  PerfilDeDesempenho.economia =>
    'Prévia em meia resolução e 30 quadros por segundo, para poupar '
        'bateria e calor. A exportação nunca muda.',
  PerfilDeDesempenho.equilibrado =>
    'Prévia em 75% e efeitos um pouco mais leves. A exportação nunca muda.',
  PerfilDeDesempenho.desempenho =>
    'Prévia em meia resolução e sem suavização 3D, para a edição ficar '
        'fluida. A exportação nunca muda.',
  PerfilDeDesempenho.maximaQualidade =>
    'Prévia sempre completa, mesmo tocando. Só cede com o aparelho em '
        'temperatura crítica. A exportação nunca muda.',
};

/// O que o aparelho diz da propria temperatura (o `termico` do
/// `SistemaNativo`: 0 normal, 1 morno, 2 serio, 3 critico).
enum EstadoTermico { frio, subindo, alto, critico }

EstadoTermico estadoTermicoDe(int termico) => switch (termico) {
  <= 0 => EstadoTermico.frio,
  1 => EstadoTermico.subindo,
  2 => EstadoTermico.alto,
  _ => EstadoTermico.critico,
};

/// Quanto a previa decodifica do video. A altura e a do quadro guardado
/// pelo leitor de quadros da previa (360 e o valor de sempre).
enum DecodificacaoDaPrevia {
  completa(360),
  reduzida(270),
  minima(180);

  const DecodificacaoDaPrevia(this.alturaPx);

  final int alturaPx;
}

/// A POLITICA: uma fotografia imutavel do que a PREVIA pode gastar agora.
///
/// ======================= A EXPORTACAO NAO LE ISTO ======================
///
/// Tudo aqui e sobre o que se VE enquanto se edita. O arquivo exportado
/// sai sempre por [PoliticaDeDesempenho.exportacao] — a politica completa,
/// constante, que nenhum perfil, temperatura, memoria ou gesto altera.
/// Um video nao pode sair pior porque o celular estava quente na hora.
@immutable
class PoliticaDeDesempenho {
  const PoliticaDeDesempenho({
    required this.escalaDaPrevia,
    required this.tetoDasFotosPx,
    required this.niveisDoBrilho,
    required this.amostras3D,
    required this.sombra3D,
    required this.teto3D,
    required this.fpsAlvoDaPrevia,
    required this.miniaturasPorLote,
    required this.decodificacao,
    required this.rascunho,
    this.motivo = '',
  });

  /// TETO sobre a resolucao da previa: 1, 0.75, 0.5 ou 0.25. E um teto —
  /// quem ja escolheu menos no seletor de resolucao continua com menos.
  final double escalaDaPrevia;

  /// Maior lado, em pixels, de uma foto decodificada para a previa.
  final int tetoDasFotosPx;

  /// Niveis da piramide do brilho/bloom (5 = completo, 2 = rascunho).
  final int niveisDoBrilho;

  /// Amostras de suavizacao da cena 3D (4 = MSAA, 1 = sem).
  final int amostras3D;

  /// A cena 3D projeta sombra na previa?
  final bool sombra3D;

  /// O teto que a politica pede ao controlador de qualidade 3D.
  final Qualidade3D teto3D;

  /// Quadros por segundo da previa; 0 = a taxa do projeto.
  final int fpsAlvoDaPrevia;

  /// Quantas miniaturas da linha do tempo podem ser pedidas de uma vez
  /// (0 = nenhuma agora; ficam para quando houver folga).
  final int miniaturasPorLote;

  final DecodificacaoDaPrevia decodificacao;

  /// Tocando ou com o dedo mexendo: vale trocar acabamento por resposta.
  final bool rascunho;

  /// Por que a politica e esta — para o overlay de diagnostico.
  final String motivo;

  /// A POLITICA DA EXPORTACAO: sempre a completa. Constante de proposito —
  /// nao ha caminho de codigo que a faca depender de um sinal.
  static const exportacao = PoliticaDeDesempenho(
    escalaDaPrevia: 1,
    tetoDasFotosPx: 8192,
    niveisDoBrilho: 5,
    amostras3D: 4,
    sombra3D: true,
    teto3D: Qualidade3D.ultra,
    fpsAlvoDaPrevia: 0,
    miniaturasPorLote: 0,
    decodificacao: DecodificacaoDaPrevia.completa,
    rascunho: false,
    motivo: 'exportacao',
  );

  PoliticaDeDesempenho copyWith({
    double? escalaDaPrevia,
    int? tetoDasFotosPx,
    int? niveisDoBrilho,
    int? amostras3D,
    bool? sombra3D,
    Qualidade3D? teto3D,
    int? fpsAlvoDaPrevia,
    int? miniaturasPorLote,
    DecodificacaoDaPrevia? decodificacao,
    bool? rascunho,
    String? motivo,
  }) => PoliticaDeDesempenho(
    escalaDaPrevia: escalaDaPrevia ?? this.escalaDaPrevia,
    tetoDasFotosPx: tetoDasFotosPx ?? this.tetoDasFotosPx,
    niveisDoBrilho: niveisDoBrilho ?? this.niveisDoBrilho,
    amostras3D: amostras3D ?? this.amostras3D,
    sombra3D: sombra3D ?? this.sombra3D,
    teto3D: teto3D ?? this.teto3D,
    fpsAlvoDaPrevia: fpsAlvoDaPrevia ?? this.fpsAlvoDaPrevia,
    miniaturasPorLote: miniaturasPorLote ?? this.miniaturasPorLote,
    decodificacao: decodificacao ?? this.decodificacao,
    rascunho: rascunho ?? this.rascunho,
    motivo: motivo ?? this.motivo,
  );

  // Igualdade por VALOR: o ValueNotifier so avisa quando a politica muda
  // de verdade, e o palco nao refaz nada a cada leitura da sonda.
  @override
  bool operator ==(Object other) =>
      other is PoliticaDeDesempenho &&
      other.escalaDaPrevia == escalaDaPrevia &&
      other.tetoDasFotosPx == tetoDasFotosPx &&
      other.niveisDoBrilho == niveisDoBrilho &&
      other.amostras3D == amostras3D &&
      other.sombra3D == sombra3D &&
      other.teto3D == teto3D &&
      other.fpsAlvoDaPrevia == fpsAlvoDaPrevia &&
      other.miniaturasPorLote == miniaturasPorLote &&
      other.decodificacao == decodificacao &&
      other.rascunho == rascunho &&
      other.motivo == motivo;

  @override
  int get hashCode => Object.hash(
    escalaDaPrevia,
    tetoDasFotosPx,
    niveisDoBrilho,
    amostras3D,
    sombra3D,
    teto3D,
    fpsAlvoDaPrevia,
    miniaturasPorLote,
    decodificacao,
    rascunho,
    motivo,
  );

  @override
  String toString() =>
      'previa ${(escalaDaPrevia * 100).round()}% · fotos $tetoDasFotosPx px · '
      'brilho $niveisDoBrilho · 3D ${qualidade3dRotulo(teto3D)} '
      '${amostras3D}x${sombra3D ? ' sombra' : ''} · '
      '${fpsAlvoDaPrevia == 0 ? 'fps do projeto' : '$fpsAlvoDaPrevia fps'} · '
      'miniaturas $miniaturasPorLote · decode ${decodificacao.alturaPx}p'
      '${rascunho ? ' · rascunho' : ''} ($motivo)';
}

/// Os sinais que entram na conta. Tudo com valor padrao de "aparelho
/// frio, parado": e o que um teste (e uma plataforma sem sonda) ve.
@immutable
class SinaisDeDesempenho {
  const SinaisDeDesempenho({
    this.termico = EstadoTermico.frio,
    this.memoriaApertada = false,
    this.memoriaEmEmergencia = false,
    this.degrausPorTempo = 0,
    this.interagindo = false,
    this.tocando = false,
    this.receita3D = ReceitaDeQualidade.ultra,
  });

  final EstadoTermico termico;

  /// Menos de ~300 MB disponiveis, ou o sistema em "pouca memoria".
  final bool memoriaApertada;

  /// O aviso de memoria do sistema (o que precede o jetsam) esta valendo.
  final bool memoriaEmEmergencia;

  /// 0..2: o que o tempo de quadro pediu (so conta no Automatico).
  final int degrausPorTempo;

  final bool interagindo;
  final bool tocando;

  /// A receita em vigor no controlador de qualidade 3D: a politica nunca
  /// promete mais 3D do que ele ja concedeu.
  final ReceitaDeQualidade receita3D;
}

/// O ponto de partida de cada perfil, com o aparelho frio e parado.
PoliticaDeDesempenho baseDoPerfil(PerfilDeDesempenho perfil) =>
    switch (perfil) {
      // AUTOMATICO FRIO = O APP DE SEMPRE. Nenhum numero daqui e menor do
      // que o que o app ja fazia antes de existir politica.
      PerfilDeDesempenho.automatico ||
      PerfilDeDesempenho.maximaQualidade => const PoliticaDeDesempenho(
        escalaDaPrevia: 1,
        tetoDasFotosPx: 4096,
        niveisDoBrilho: 5,
        amostras3D: 4,
        sombra3D: true,
        teto3D: Qualidade3D.ultra,
        fpsAlvoDaPrevia: 0,
        miniaturasPorLote: 6,
        decodificacao: DecodificacaoDaPrevia.completa,
        rascunho: false,
      ),
      PerfilDeDesempenho.equilibrado => const PoliticaDeDesempenho(
        escalaDaPrevia: .75,
        tetoDasFotosPx: 2048,
        niveisDoBrilho: 4,
        amostras3D: 4,
        sombra3D: true,
        teto3D: Qualidade3D.alta,
        fpsAlvoDaPrevia: 0,
        miniaturasPorLote: 6,
        decodificacao: DecodificacaoDaPrevia.completa,
        rascunho: false,
      ),
      PerfilDeDesempenho.desempenho => const PoliticaDeDesempenho(
        escalaDaPrevia: .5,
        tetoDasFotosPx: 2048,
        niveisDoBrilho: 3,
        amostras3D: 1,
        sombra3D: true,
        teto3D: Qualidade3D.media,
        fpsAlvoDaPrevia: 0,
        miniaturasPorLote: 4,
        decodificacao: DecodificacaoDaPrevia.completa,
        rascunho: false,
      ),
      PerfilDeDesempenho.economia => const PoliticaDeDesempenho(
        escalaDaPrevia: .5,
        tetoDasFotosPx: 1536,
        niveisDoBrilho: 2,
        amostras3D: 1,
        sombra3D: false,
        teto3D: Qualidade3D.baixa,
        fpsAlvoDaPrevia: 30,
        miniaturasPorLote: 2,
        decodificacao: DecodificacaoDaPrevia.reduzida,
        rascunho: false,
      ),
    };

/// OS DEGRAUS DA ESCADA. Cada um e um TETO aplicado por cima do perfil:
/// nunca melhora nada, so limita.
///
/// 1 · esquentando: alivia as cargas da previa que se veem menos.
/// 2 · quente: previa em 1/2 e sai o que e caro so para visualizar.
/// 3 · critico: previa em 1/4, o minimo para continuar editando.
PoliticaDeDesempenho _limitar(PoliticaDeDesempenho p, int degraus) {
  if (degraus <= 0) return p;
  final (
    double escala,
    int fotos,
    int brilho,
    int amostras,
    bool sombra,
    Qualidade3D teto,
    int fps,
    int miniaturas,
    DecodificacaoDaPrevia decode,
  ) = switch (degraus) {
    1 => (
      .75,
      2048,
      3,
      1,
      true,
      Qualidade3D.alta,
      0,
      4,
      DecodificacaoDaPrevia.completa,
    ),
    2 => (
      .5,
      1536,
      2,
      1,
      false,
      Qualidade3D.media,
      30,
      2,
      DecodificacaoDaPrevia.reduzida,
    ),
    _ => (
      .25,
      1024,
      2,
      1,
      false,
      Qualidade3D.baixa,
      24,
      0,
      DecodificacaoDaPrevia.minima,
    ),
  };
  return p.copyWith(
    escalaDaPrevia: math.min(p.escalaDaPrevia, escala),
    tetoDasFotosPx: math.min(p.tetoDasFotosPx, fotos),
    niveisDoBrilho: math.min(p.niveisDoBrilho, brilho),
    amostras3D: math.min(p.amostras3D, amostras),
    sombra3D: p.sombra3D && sombra,
    teto3D: Qualidade3D.values[math.max(p.teto3D.index, teto.index)],
    // 0 quer dizer "o do projeto", que e o MAIOR valor possivel.
    fpsAlvoDaPrevia: fps == 0
        ? p.fpsAlvoDaPrevia
        : (p.fpsAlvoDaPrevia == 0 ? fps : math.min(p.fpsAlvoDaPrevia, fps)),
    miniaturasPorLote: math.min(p.miniaturasPorLote, miniaturas),
    decodificacao: DecodificacaoDaPrevia
        .values[math.max(p.decodificacao.index, decode.index)],
  );
}

/// Quantos degraus os sinais pedem a este perfil.
int degrausPara(PerfilDeDesempenho perfil, SinaisDeDesempenho s) {
  // MAXIMA QUALIDADE so cede quando continuar custaria o aparelho: a
  // temperatura critica (o sistema ja esta estrangulando a GPU) e o aviso
  // de memoria que precede a morte do processo.
  if (perfil == PerfilDeDesempenho.maximaQualidade) {
    if (s.memoriaEmEmergencia) return 3;
    return s.termico == EstadoTermico.critico ? 2 : 0;
  }
  var d = switch (s.termico) {
    EstadoTermico.frio => 0,
    EstadoTermico.subindo => 1,
    EstadoTermico.alto => 2,
    EstadoTermico.critico => 3,
  };
  if (s.memoriaApertada) d = math.max(d, 1);
  if (s.memoriaEmEmergencia) d = math.max(d, 3);
  // O TEMPO DE QUADRO so manda no Automatico: nos perfis fixos a pessoa
  // escolheu o ponto de partida, e uma previa que muda de resolucao
  // sozinha seria o contrario do que ela pediu.
  if (perfil == PerfilDeDesempenho.automatico) {
    d += s.degrausPorTempo.clamp(0, 2);
  }
  return math.min(3, d);
}

/// A CONTA INTEIRA, pura: perfil + sinais -> politica da PREVIA.
///
/// A exportacao nao passa por aqui (ver [PoliticaDeDesempenho.exportacao]).
PoliticaDeDesempenho politicaPara(
  PerfilDeDesempenho perfil,
  SinaisDeDesempenho s,
) {
  final degraus = degrausPara(perfil, s);
  var p = _limitar(baseDoPerfil(perfil), degraus);
  // O 3D nunca promete mais do que o controlador de qualidade ja deu: e
  // ele quem conhece o orcamento de GPU da cena.
  final r = s.receita3D;
  p = p.copyWith(
    amostras3D: r.msaa ? p.amostras3D : 1,
    sombra3D: p.sombra3D && r.sombras,
    // Em Maxima qualidade nem tocar baixa o acabamento.
    rascunho:
        (s.tocando || s.interagindo) &&
        (perfil != PerfilDeDesempenho.maximaQualidade || degraus > 0),
    motivo: _motivo(perfil, s, degraus),
  );
  return p;
}

String _motivo(PerfilDeDesempenho perfil, SinaisDeDesempenho s, int degraus) {
  final nome = perfilDeDesempenhoRotulo(perfil);
  if (degraus == 0) return nome;
  final causa = s.memoriaEmEmergencia
      ? 'memoria no limite'
      : switch (s.termico) {
          EstadoTermico.critico => 'temperatura critica',
          EstadoTermico.alto => 'aparelho quente',
          EstadoTermico.subindo => 'aparelho esquentando',
          EstadoTermico.frio =>
            s.memoriaApertada ? 'pouca memoria' : 'quadros atrasando',
        };
  return '$nome · $causa (-$degraus)';
}
