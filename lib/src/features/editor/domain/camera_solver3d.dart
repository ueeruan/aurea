import 'dart:convert';
import 'dart:math' as math;
import 'dart:ui';

import 'algebra_numerica.dart';
import 'pontos_seguidos.dart';

/// RASTREIO DE CAMERA 3D — descobrir por onde a camera passou, a partir
/// do video e de nada mais.
///
/// O problema: uma imagem e a sombra de um mundo de tres dimensoes num
/// plano de duas. Uma imagem so nao tem volta. Mas DUAS imagens do mesmo
/// mundo, tiradas de lugares diferentes, tem: o quanto cada ponto se
/// desloca entre elas depende de quao longe ele esta (paralaxe), e dessa
/// diferenca sai ao mesmo tempo a forma da cena e o caminho da camera.
///
/// O caminho, na ordem em que acontece aqui:
///
///   1. PAR INICIAL. Escolhe dois quadros com pontos em comum e
///      deslocamento suficiente. Pouca paralaxe e o unico caso em que
///      isto realmente nao tem solucao — e melhor dizer isso do que
///      devolver uma cena inventada.
///   2. MATRIZ ESSENCIAL entre eles (8 pontos + RANSAC), que carrega a
///      rotacao e a DIRECAO da translacao entre as duas cameras.
///   3. QUATRO POSSIBILIDADES saem da decomposicao; so uma poe os pontos
///      na frente das duas cameras. E esse teste (cheiralidade) que
///      escolhe.
///   4. TRIANGULA os pontos vistos pelas duas — a primeira nuvem.
///   5. RESSECAO: com a nuvem na mao, cada outro quadro descobre sua
///      propria pose por onde os pontos conhecidos caem nele.
///   6. INTERSECAO: com todas as poses, retriangula os pontos usando
///      TODOS os quadros que os viram. Repetir 5 e 6 e um ajuste de
///      feixes pobre, e converge bem o bastante.
///
/// O QUE NAO DA PARA SABER, por princípio: a ESCALA. Duas fotos de uma
/// maquete e de um predio de verdade sao identicas. Entao a escala aqui
/// e uma convencao (a nuvem cabe num raio conhecido) e a orientacao vem
/// de "a camera estava em pe", que e verdade quase sempre.
///
/// Convencao interna: `Xcam = R * Xmundo + t`, com a camera olhando para
/// +Z, X para a direita e Y para BAIXO (a mesma do pixel na imagem).
/// Esse trio e destro, entao R e uma rotacao de verdade — se Y fosse
/// para cima o determinante daria -1 e a decomposicao da essencial
/// devolveria reflexoes em vez de rotacoes.

/// A pose da camera num quadro.
class PoseCamera {
  const PoseCamera(this.quadro, this.rotacao, this.translacao);

  final int quadro;

  /// Leva o mundo para a camera.
  final Mat3 rotacao;
  final List<double> translacao;

  /// Onde a camera esta, no mundo: `-R^T t`.
  List<double> get posicao {
    final rt = rotacao.transposta.aplicar(translacao);
    return [-rt[0], -rt[1], -rt[2]];
  }

  /// Para onde ela olha (terceira linha de R, no mundo).
  List<double> get frente => [
    rotacao.at(2, 0),
    rotacao.at(2, 1),
    rotacao.at(2, 2),
  ];

  /// O topo da imagem, no mundo. A segunda linha de R aponta para BAIXO
  /// na imagem, entao "cima" e o negativo dela.
  List<double> get cima => [
    -rotacao.at(1, 0),
    -rotacao.at(1, 1),
    -rotacao.at(1, 2),
  ];

  /// A direita da imagem, no mundo.
  List<double> get direita => [
    rotacao.at(0, 0),
    rotacao.at(0, 1),
    rotacao.at(0, 2),
  ];
}

/// O RESULTADO: por onde a camera passou e onde estao os pontos.
class SolucaoCamera3D {
  const SolucaoCamera3D({
    required this.largura,
    required this.altura,
    required this.focalPx,
    required this.poses,
    required this.nuvem,
    required this.erroPixels,
    required this.quadros,
    required this.fps,
    this.errosPorPonto = const {},
    this.vistasPorPonto = const {},
    this.pontosSeguidos = 0,
    this.tipoDeTomada = TipoDeTomada.auto,
    this.inicioDaFonteUs,
  });

  /// Tamanho do quadro ANALISADO (nao o do video).
  final int largura;
  final int altura;

  /// Distancia focal em pixels desse quadro analisado. Vira milimetros
  /// dividindo pela largura: focal_mm = 36 * focalPx / largura.
  final double focalPx;

  /// Uma pose por quadro resolvido, em ordem.
  final List<PoseCamera> poses;

  /// Id do ponto seguido -> posicao no mundo.
  final Map<int, List<double>> nuvem;

  /// Erro medio de reprojecao, em pixels do quadro analisado. Abaixo de
  /// ~1 px o rastreio gruda; acima de ~3 px escorrega e da para ver.
  final double erroPixels;

  /// Quantos quadros a analise tinha, e a que taxa.
  final int quadros;
  final int fps;

  /// O ERRO DE CADA PONTO, em pixels do quadro analisado.
  ///
  /// O erro medio da cena esconde o caso que interessa: uma solucao com
  /// 0,9 px de media pode ter dez pontos com 6 px cada, e sao esses que
  /// fazem o objeto tremer. Guardado por ponto, da para MOSTRAR quais
  /// sao ruins e deixar apaga-los.
  final Map<int, double> errosPorPonto;

  /// Em quantos quadros cada ponto foi visto. Um ponto com erro baixo
  /// visto em tres quadros e sorte, nao qualidade.
  final Map<int, int> vistasPorPonto;

  /// Quantos pontos a etapa de seguimento achou, antes de a
  /// reconstrucao descartar os que nao fecharam.
  final int pontosSeguidos;

  /// O tipo de tomada com que a analise foi feita.
  final TipoDeTomada tipoDeTomada;

  /// DE QUE INSTANTE DO ARQUIVO saiu o quadro 0 da analise (microssegundos,
  /// tempo absoluto da fonte). Com ele a camera segue o quadro que o clipe
  /// MOSTRA — velocidade, reverso e Time Remap incluidos. Nulo em solucao
  /// antiga: o quadro q vale q/fps do tempo da camada, como era.
  final int? inicioDaFonteUs;

  /// A QUALIDADE DE UM PONTO, do jeito que a tela mostra.
  ///
  /// Erro e permanencia contam juntos porque um sozinho engana: um ponto
  /// visto em tres quadros quase sempre fecha (ha poucas observacoes
  /// para contraria-lo), e um ponto visto no video inteiro com dois
  /// pixels de erro ainda sustenta a cena.
  QualidadeDoPonto qualidadeDoPonto(int id) {
    final e = errosPorPonto[id];
    final v = vistasPorPonto[id] ?? 0;
    if (e == null) return QualidadeDoPonto.ruim;
    final poucasVistas = v < math.max(4, quadros ~/ 6);
    if (e > 4) return QualidadeDoPonto.ruim;
    if (e > 2 || poucasVistas) return QualidadeDoPonto.fraco;
    if (e > 1) return QualidadeDoPonto.bom;
    return QualidadeDoPonto.excelente;
  }

  List<int> pontosDaQualidade(Set<QualidadeDoPonto> quais) => [
    for (final id in nuvem.keys)
      if (quais.contains(qualidadeDoPonto(id))) id,
  ];

  /// De uma a cinco estrelas, o que a ficha do solve mostra.
  ///
  /// Nao e so o erro: uma cena com erro baixo e vinte pontos e fragil, e
  /// uma com erro medio e seiscentos pontos sustenta. As duas coisas
  /// entram, e a menor manda.
  int get estrelas {
    final porErro = switch (erroPixels) {
      < 0.5 => 5,
      < 1.0 => 4,
      < 2.0 => 3,
      < 4.0 => 2,
      _ => 1,
    };
    final porPontos = switch (nuvem.length) {
      >= 300 => 5,
      >= 150 => 4,
      >= 60 => 3,
      >= 25 => 2,
      _ => 1,
    };
    return math.min(porErro, porPontos);
  }

  /// Quantos pontos aguentam segurar um objeto.
  int get pontosBons =>
      pontosDaQualidade({QualidadeDoPonto.excelente, QualidadeDoPonto.bom})
          .length;

  SolucaoCamera3D copiarCom({
    List<PoseCamera>? poses,
    Map<int, List<double>>? nuvem,
    Map<int, double>? errosPorPonto,
    Map<int, int>? vistasPorPonto,
    double? erroPixels,
    TipoDeTomada? tipoDeTomada,
    int? inicioDaFonteUs,
  }) => SolucaoCamera3D(
    largura: largura,
    altura: altura,
    focalPx: focalPx,
    poses: poses ?? this.poses,
    nuvem: nuvem ?? this.nuvem,
    erroPixels: erroPixels ?? this.erroPixels,
    quadros: quadros,
    fps: fps,
    errosPorPonto: errosPorPonto ?? this.errosPorPonto,
    vistasPorPonto: vistasPorPonto ?? this.vistasPorPonto,
    pontosSeguidos: pontosSeguidos,
    tipoDeTomada: tipoDeTomada ?? this.tipoDeTomada,
    inicioDaFonteUs: inicioDaFonteUs ?? this.inicioDaFonteUs,
  );

  bool get isEmpty => poses.isEmpty || nuvem.isEmpty;

  /// A qualidade em palavras — e o que a interface mostra.
  String get qualidade => switch (erroPixels) {
    < 1.0 => 'Ótimo',
    < 2.0 => 'Bom',
    < 4.0 => 'Aceitável',
    _ => 'Ruim',
  };

  PoseCamera? poseDoQuadro(int q) {
    for (final p in poses) {
      if (p.quadro == q) return p;
    }
    return null;
  }

  /// GRAVAR A SOLUCAO.
  ///
  /// Resolver uma camera custa segundos; reabrir o projeto nao pode
  /// custar de novo. E a solucao tem de ser a MESMA de antes, senao o
  /// objeto que a pessoa colou no plano muda de lugar sozinho entre uma
  /// sessao e outra.
  Map<String, dynamic> toJson() => {
    'w': largura,
    'h': altura,
    'f': focalPx,
    'e': erroPixels,
    'q': quadros,
    'fps': fps,
    'p': [
      for (final p in poses) [p.quadro, ...p.rotacao.m, ...p.translacao],
    ],
    'n': {for (final e in nuvem.entries) '${e.key}': e.value},
    'ep': {for (final e in errosPorPonto.entries) '${e.key}': e.value},
    'vp': {for (final e in vistasPorPonto.entries) '${e.key}': e.value},
    'ps': pontosSeguidos,
    'tt': tipoDeTomada.name,
    'src0': ?inicioDaFonteUs,
  };

  static SolucaoCamera3D? decode(String fonte) {
    try {
      final m = (jsonDecode(fonte) as Map).cast<String, dynamic>();
      return SolucaoCamera3D(
        largura: (m['w'] as num).toInt(),
        altura: (m['h'] as num).toInt(),
        focalPx: (m['f'] as num).toDouble(),
        erroPixels: (m['e'] as num).toDouble(),
        quadros: (m['q'] as num).toInt(),
        fps: (m['fps'] as num).toInt(),
        poses: [
          for (final p in (m['p'] as List))
            PoseCamera(
              ((p as List)[0] as num).toInt(),
              Mat3([for (var i = 1; i <= 9; i++) (p[i] as num).toDouble()]),
              [for (var i = 10; i <= 12; i++) (p[i] as num).toDouble()],
            ),
        ],
        nuvem: {
          for (final e in (m['n'] as Map).entries)
            int.parse(e.key as String): [
              for (final v in (e.value as List)) (v as num).toDouble(),
            ],
        },
        // Solucao gravada por uma versao antiga nao tem estes campos. Ela
        // continua valendo: sem eles a cena e a camera sao as mesmas, e o
        // que se perde e so a ficha de qualidade.
        errosPorPonto: {
          for (final e in ((m['ep'] as Map?) ?? const {}).entries)
            int.parse(e.key as String): (e.value as num).toDouble(),
        },
        vistasPorPonto: {
          for (final e in ((m['vp'] as Map?) ?? const {}).entries)
            int.parse(e.key as String): (e.value as num).toInt(),
        },
        pontosSeguidos: ((m['ps'] as num?) ?? 0).toInt(),
        tipoDeTomada: TipoDeTomada.values.firstWhere(
          (t) => t.name == m['tt'],
          orElse: () => TipoDeTomada.auto,
        ),
        inicioDaFonteUs: (m['src0'] as num?)?.toInt(),
      );
    } catch (_) {
      // Solucao antiga ou estragada nao pode impedir de rastrear de novo.
      return null;
    }
  }
}

/// Por que uma analise nao deu certo. Cada caso pede uma acao diferente
/// de quem filmou, e por isso sao mensagens separadas em vez de "erro".
enum FalhaDoRastreio { poucosPontos, semParalaxe, naoConvergiu }

/// QUANTO SE PODE CONFIAR NUM PONTO.
///
/// Um ponto ruim nao estraga a media da cena — estraga o objeto colado
/// nele. Por isso a qualidade e por ponto, e nao so do solve.
enum QualidadeDoPonto {
  excelente,
  bom,
  fraco,
  ruim;

  String get emPalavras => switch (this) {
    QualidadeDoPonto.excelente => 'Excelente',
    QualidadeDoPonto.bom => 'Bom',
    QualidadeDoPonto.fraco => 'Fraco',
    QualidadeDoPonto.ruim => 'Ruim',
  };
}

/// COMO A TOMADA FOI FEITA.
///
/// Saber isto ANTES muda a conta. Uma lente fixa deixa a focal ser
/// resolvida uma vez para o clipe inteiro (mais estavel); um zoom pede
/// que ela varie; um tripe nao tem paralaxe nenhuma e nao da rastreio
/// 3D — nesse caso o certo e dizer isso, e nao devolver uma cena
/// inventada. Quem nao sabe deixa em [auto], que e o padrao.
enum TipoDeTomada {
  auto,
  lenteFixa,
  zoomVariavel,
  tripe;

  String get emPalavras => switch (this) {
    TipoDeTomada.auto => 'Detectar sozinho',
    TipoDeTomada.lenteFixa => 'Lente fixa',
    TipoDeTomada.zoomVariavel => 'Zoom durante a tomada',
    TipoDeTomada.tripe => 'Tripé / panorâmica',
  };
}

/// O QUE A ANALISE VE DA FILMAGEM, antes de tentar resolver.
///
/// E o "Auto Detect" da referencia: olhar o movimento dos pontos e dizer
/// que tipo de cena e aquela. O numero que decide e o residuo da
/// homografia — quanto de todo o deslocamento uma unica transformacao
/// plana ja explica. Perto de zero: ou a camera girou no lugar, ou tudo
/// esta na mesma distancia. Longe: ha profundidade de verdade.
enum LeituraDaCena {
  /// Camera girando no lugar. Nao da rastreio 3D.
  tripeOuGiro,

  /// Ha movimento, mas tudo quase no mesmo plano: da para resolver, e a
  /// profundidade sai fraca.
  quaseChata,

  /// O caso bom: coisas perto e longe, camera andando.
  profundidadeBoa;

  String get emPalavras => switch (this) {
    LeituraDaCena.tripeOuGiro => 'Tripé ou giro no lugar',
    LeituraDaCena.quaseChata => 'Cena quase plana',
    LeituraDaCena.profundidadeBoa => 'Cena com profundidade',
  };
}

/// O ACERTO QUE SE PEDE, e o tempo que se aceita esperar por ele.
///
/// Sao DOIS PASSES, e nao tres algoritmos: o rapido serve para ver na
/// hora se a filmagem da rastreio; o preciso e para fechar o projeto. O
/// que muda entre eles e a quantidade de quadros lidos, de pontos
/// seguidos e de rodadas de refinamento — nao a matematica.
enum ModoDoSolve {
  rapido,
  equilibrado,
  preciso;

  String get emPalavras => switch (this) {
    ModoDoSolve.rapido => 'Rápido',
    ModoDoSolve.equilibrado => 'Equilibrado',
    ModoDoSolve.preciso => 'Preciso',
  };

  String get explicacao => switch (this) {
    ModoDoSolve.rapido =>
      'Poucos segundos. Serve para ver se a filmagem dá rastreio.',
    ModoDoSolve.equilibrado => 'O padrão: bom resultado sem esperar demais.',
    ModoDoSolve.preciso => 'Mais quadros e mais pontos. Demora, e gruda.',
  };

  /// Quadros por segundo lidos do video.
  int get fps => switch (this) {
    ModoDoSolve.rapido => 5,
    ModoDoSolve.equilibrado => 8,
    ModoDoSolve.preciso => 12,
  };

  /// Quantos pontos seguir.
  int get pontos => switch (this) {
    ModoDoSolve.rapido => 70,
    ModoDoSolve.equilibrado => 110,
    ModoDoSolve.preciso => 220,
  };

  /// Teto de quadros lidos. Um video longo nao pode virar RAM: mil
  /// quadros em tons de cinza a 240 px ja sao setenta megabytes, e num
  /// iPhone 13 isso e o app fechado.
  int get maximoDeQuadros => switch (this) {
    ModoDoSolve.rapido => 120,
    ModoDoSolve.equilibrado => 240,
    ModoDoSolve.preciso => 400,
  };

  /// Rodadas de refinamento alternado (pose e pontos).
  int get refinos => switch (this) {
    ModoDoSolve.rapido => 1,
    ModoDoSolve.equilibrado => 2,
    ModoDoSolve.preciso => 4,
  };
}

class RastreioException implements Exception {
  const RastreioException(this.falha, this.mensagem);
  final FalhaDoRastreio falha;
  final String mensagem;
  @override
  String toString() => mensagem;
}

// ------------------------------------------------------------- helpers

/// Coordenada normalizada: o raio que sai da camera por aquele pixel.
List<double> _raio(Offset p, double cx, double cy, double f) => [
  (p.dx - cx) / f,
  (p.dy - cy) / f,
  1,
];

Mat3 _deColunas(List<double> a, List<double> b, List<double> c) =>
    Mat3([a[0], b[0], c[0], a[1], b[1], c[1], a[2], b[2], c[2]]);

/// A MATRIZ ESSENCIAL de oito ou mais pares (algoritmo de 8 pontos).
///
/// Os pontos sao recentrados e reescalados antes: com dados crus a
/// matriz do sistema fica mal condicionada e a solucao sai do ruido, nao
/// dos dados. Depois a transformacao e desfeita.
Mat3? essencialDePares(List<(List<double>, List<double>)> pares) {
  if (pares.length < 8) return null;

  ({double cx, double cy, double s}) medida(bool primeiro) {
    var cx = 0.0, cy = 0.0;
    for (final par in pares) {
      final p = primeiro ? par.$1 : par.$2;
      cx += p[0];
      cy += p[1];
    }
    cx /= pares.length;
    cy /= pares.length;
    var d = 0.0;
    for (final par in pares) {
      final p = primeiro ? par.$1 : par.$2;
      d += math.sqrt((p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy));
    }
    d /= pares.length;
    return (cx: cx, cy: cy, s: d < 1e-12 ? 1.0 : math.sqrt(2) / d);
  }

  final m1 = medida(true), m2 = medida(false);
  final linhas = <List<double>>[];
  for (final (a, b) in pares) {
    final x1 = (a[0] - m1.cx) * m1.s, y1 = (a[1] - m1.cy) * m1.s;
    final x2 = (b[0] - m2.cx) * m2.s, y2 = (b[1] - m2.cy) * m2.s;
    linhas.add([x2 * x1, x2 * y1, x2, y2 * x1, y2 * y1, y2, x1, y1, 1]);
  }
  final v = nucleo(linhas);
  if (v.length != 9) return null;
  final f = Mat3(v);

  // Desfaz a normalizacao: E = T2^T F T1.
  final t1 = Mat3([m1.s, 0, -m1.s * m1.cx, 0, m1.s, -m1.s * m1.cy, 0, 0, 1]);
  final t2 = Mat3([m2.s, 0, -m2.s * m2.cx, 0, m2.s, -m2.s * m2.cy, 0, 0, 1]);
  return projetarNaEssencial(t2.transposta * f * t1);
}

/// A ESSENCIAL VALIDA mais proxima de [e].
///
/// Uma matriz essencial de verdade tem dois valores singulares iguais e
/// um zero. O que sai do sistema linear nao tem — e uma matriz qualquer
/// que chega perto. Forcar os valores singulares e o que transforma a
/// solucao aproximada numa geometria possivel; sem isso a decomposicao
/// devolve rotacoes que nao fecham.
Mat3 projetarNaEssencial(Mat3 e) {
  final d = decomporEmValoresSingulares(e);
  // diag(1, 1, 0) no lugar dos valores singulares.
  final u = d.u, v = d.v;
  final r = List<double>.filled(9, 0.0);
  for (var i = 0; i < 3; i++) {
    for (var j = 0; j < 3; j++) {
      r[i * 3 + j] = u.at(i, 0) * v.at(j, 0) + u.at(i, 1) * v.at(j, 1);
    }
  }
  return Mat3(r);
}

/// DECOMPOSICAO EM VALORES SINGULARES de uma 3x3, por autovetores.
///
/// Nao ha SVD pronta no projeto e escrever uma completa seria muito
/// codigo para o que se usa. Para 3x3 da para chegar la pelo caminho
/// curto: V e a base propria de E^T E, os valores singulares sao as
/// raizes dos autovalores, e cada coluna de U sai de `E v / sigma`. A
/// terceira coluna vem do produto vetorial das outras duas, o que ja
/// garante determinante +1 — que e o que a decomposicao da essencial
/// precisa para devolver rotacoes e nao reflexoes.
({Mat3 u, List<double> sigma, Mat3 v}) decomporEmValoresSingulares(Mat3 e) {
  final eTe = e.transposta * e;
  final auto = autovaloresSimetrica([
    [eTe.at(0, 0), eTe.at(0, 1), eTe.at(0, 2)],
    [eTe.at(1, 0), eTe.at(1, 1), eTe.at(1, 2)],
    [eTe.at(2, 0), eTe.at(2, 1), eTe.at(2, 2)],
  ]);
  // autovaloresSimetrica devolve do menor para o maior; aqui e o
  // contrario que interessa.
  final v0 = normalizar(auto.vetores[2]);
  final v1 = normalizar(auto.vetores[1]);
  var v2 = produtoVetorial(v0, v1);
  final s0 = math.sqrt(math.max(0, auto.valores[2]));
  final s1 = math.sqrt(math.max(0, auto.valores[1]));
  final s2 = math.sqrt(math.max(0, auto.valores[0]));

  List<double> coluna(List<double> vi, double si, List<double> reserva) =>
      si < 1e-12 ? reserva : normalizar(e.aplicar(vi));

  final u0 = coluna(v0, s0, [1, 0, 0]);
  final u1 = coluna(v1, s1, [0, 1, 0]);
  final u2 = produtoVetorial(u0, u1);
  // V tambem precisa de determinante +1.
  final v = _deColunas(v0, v1, v2);
  if (v.determinante < 0) {
    v2 = [-v2[0], -v2[1], -v2[2]];
  }
  return (
    u: _deColunas(u0, u1, u2),
    sigma: [s0, s1, s2],
    v: _deColunas(v0, v1, v2),
  );
}

/// As QUATRO poses possiveis escondidas numa matriz essencial.
///
/// A essencial nao distingue "a camera andou para a direita" de "andou
/// para a esquerda e o mundo esta atras". Sao quatro combinacoes de
/// rotacao e sentido; so uma poe a cena na frente das duas cameras, e e
/// so isso que resolve a ambiguidade.
List<(Mat3, List<double>)> posesDaEssencial(Mat3 e) {
  final d = decomporEmValoresSingulares(e);
  const w = Mat3([0, -1, 0, 1, 0, 0, 0, 0, 1]);
  var ra = d.u * w * d.v.transposta;
  var rb = d.u * w.transposta * d.v.transposta;
  if (ra.determinante < 0) ra = ra.escalada(-1);
  if (rb.determinante < 0) rb = rb.escalada(-1);
  final t = [d.u.at(0, 2), d.u.at(1, 2), d.u.at(2, 2)];
  final tn = [-t[0], -t[1], -t[2]];
  return [(ra, t), (ra, tn), (rb, t), (rb, tn)];
}

/// A HOMOGRAFIA que leva os pontos de uma imagem na outra (4 pontos,
/// DLT). Como no caso da essencial, os dados sao normalizados antes: sem
/// isso a matriz do sistema mistura numeros na casa de 1 com numeros na
/// casa de 50 mil e a solucao vem do arredondamento.
Mat3? homografiaDePares(List<(Offset, Offset)> pares) {
  if (pares.length < 4) return null;

  ({double cx, double cy, double s}) medida(bool primeiro) {
    var cx = 0.0, cy = 0.0;
    for (final par in pares) {
      final p = primeiro ? par.$1 : par.$2;
      cx += p.dx;
      cy += p.dy;
    }
    cx /= pares.length;
    cy /= pares.length;
    var d = 0.0;
    for (final par in pares) {
      final p = primeiro ? par.$1 : par.$2;
      d += (p - Offset(cx, cy)).distance;
    }
    d /= pares.length;
    return (cx: cx, cy: cy, s: d < 1e-12 ? 1.0 : math.sqrt(2) / d);
  }

  final m1 = medida(true), m2 = medida(false);
  final linhas = <List<double>>[];
  for (final (a, b) in pares) {
    final x = (a.dx - m1.cx) * m1.s, y = (a.dy - m1.cy) * m1.s;
    final u = (b.dx - m2.cx) * m2.s, v = (b.dy - m2.cy) * m2.s;
    linhas.add([-x, -y, -1, 0, 0, 0, u * x, u * y, u]);
    linhas.add([0, 0, 0, -x, -y, -1, v * x, v * y, v]);
  }
  final h = nucleo(linhas);
  if (h.length != 9) return null;

  final t1 = Mat3([m1.s, 0, -m1.s * m1.cx, 0, m1.s, -m1.s * m1.cy, 0, 0, 1]);
  final t2inv = Mat3([1 / m2.s, 0, m2.cx, 0, 1 / m2.s, m2.cy, 0, 0, 1]);
  return t2inv * Mat3(h) * t1;
}

/// O QUANTO UMA IMAGEM SO E EXPLICADA POR UMA HOMOGRAFIA — em pixels.
///
/// Este e o teste que separa "da para rastrear em 3D" de "nao da", e ele
/// e independente da distancia focal, o que importa porque a focal ainda
/// nao e conhecida quando ele roda.
///
/// A ideia: uma homografia (uma transformacao plana) explica
/// perfeitamente dois casos, e so dois — a camera que GIROU NO LUGAR, e
/// a cena que e um PLANO. Nos dois, profundidade nao existe ou nao e
/// observavel, e qualquer "cena 3D" que o solver devolva e invencao. Num
/// plano de verdade, com camera que anda e objetos a distancias
/// diferentes, nenhuma homografia da conta: o que esta perto se desloca
/// mais que o que esta longe, e essa diferenca vira residuo grande.
double residuoDeHomografia(List<(Offset, Offset)> pares) {
  final h = homografiaDePares(pares);
  if (h == null) return double.infinity;
  final erros = <double>[];
  for (final (a, b) in pares) {
    final p = h.aplicar([a.dx, a.dy, 1]);
    if (p[2].abs() < 1e-9) continue;
    erros.add((Offset(p[0] / p[2], p[1] / p[2]) - b).distance);
  }
  return erros.isEmpty ? double.infinity : mediana(erros);
}

/// TRIANGULA um ponto a partir de duas ou mais vistas.
///
/// Cada vista da duas equacoes ("o ponto esta nesta reta"); o ponto e
/// onde as retas mais se aproximam. Com so duas vistas as retas quase
/// nunca se cruzam de verdade — o ruido de meio pixel ja separa elas —,
/// e por isso a solucao e o vetor que minimiza o erro, nao uma
/// intersecao exata.
List<double>? triangular(List<(Mat3, List<double>, List<double>)> vistas) {
  if (vistas.length < 2) return null;
  final linhas = <List<double>>[];
  for (final (r, t, obs) in vistas) {
    final x = obs[0], y = obs[1];
    final p0 = [r.at(0, 0), r.at(0, 1), r.at(0, 2), t[0]];
    final p1 = [r.at(1, 0), r.at(1, 1), r.at(1, 2), t[1]];
    final p2 = [r.at(2, 0), r.at(2, 1), r.at(2, 2), t[2]];
    linhas.add([for (var i = 0; i < 4; i++) x * p2[i] - p0[i]]);
    linhas.add([for (var i = 0; i < 4; i++) y * p2[i] - p1[i]]);
  }
  final v = nucleo(linhas);
  if (v.length != 4 || v[3].abs() < 1e-12) return null;
  return [v[0] / v[3], v[1] / v[3], v[2] / v[3]];
}

/// A profundidade do ponto na camera. Negativa = atras da lente.
double profundidade(Mat3 r, List<double> t, List<double> x) =>
    r.at(2, 0) * x[0] + r.at(2, 1) * x[1] + r.at(2, 2) * x[2] + t[2];

/// Onde o ponto CAI na imagem, em coordenadas normalizadas. Null se
/// estiver atras da camera.
List<double>? projetar(Mat3 r, List<double> t, List<double> x) {
  final c = r.aplicar(x);
  final z = c[2] + t[2];
  if (z < 1e-6) return null;
  return [(c[0] + t[0]) / z, (c[1] + t[1]) / z];
}

/// Distancia de Sampson: o quanto o par viola a geometria da essencial,
/// medido em unidades de imagem (e nao "erro algebrico", que nao tem
/// significado geometrico e por isso nao serve de limiar).
double _sampson(Mat3 e, List<double> a, List<double> b) {
  final ea = e.aplicar(a);
  final etb = e.transposta.aplicar(b);
  final num = produtoInterno(b, ea);
  final den = ea[0] * ea[0] + ea[1] * ea[1] + etb[0] * etb[0] + etb[1] * etb[1];
  if (den < 1e-18) return double.infinity;
  return num.abs() / math.sqrt(den);
}

/// A ESSENCIAL ROBUSTA: RANSAC por cima do 8 pontos.
///
/// Basta um par errado — um ponto que escorregou para o fundo — para o
/// ajuste por minimos quadrados inteiro entortar. RANSAC nao tenta
/// acomodar o erro: sorteia oito pares, ve quantos concordam, e no fim
/// fica com o consenso maior. Um ponto ruim simplesmente nao entra.
({Mat3 e, List<int> inliers})? essencialRobusta(
  List<(List<double>, List<double>)> pares, {
  required double limiar,
  int tentativas = 400,
  int? semente,
}) {
  if (pares.length < 8) return null;
  final rng = math.Random(semente ?? 20260907);
  var melhorInliers = <int>[];
  Mat3? melhorE;

  for (var it = 0; it < tentativas; it++) {
    final indices = <int>{};
    var guarda = 0;
    while (indices.length < 8 && guarda++ < 200) {
      indices.add(rng.nextInt(pares.length));
    }
    if (indices.length < 8) continue;
    final e = essencialDePares([for (final i in indices) pares[i]]);
    if (e == null) continue;

    final inliers = <int>[];
    for (var i = 0; i < pares.length; i++) {
      if (_sampson(e, pares[i].$1, pares[i].$2) < limiar) inliers.add(i);
    }
    if (inliers.length > melhorInliers.length) {
      melhorInliers = inliers;
      melhorE = e;
    }
    // Parar cedo quando quase tudo ja concorda: continuar sorteando so
    // gasta tempo.
    if (melhorInliers.length > pares.length * .9 && it > 40) break;
  }
  if (melhorE == null || melhorInliers.length < 8) return null;

  // Refina com TODOS os inliers: os oito do sorteio deram a hipotese, o
  // consenso inteiro da a precisao.
  final refinada = essencialDePares([for (final i in melhorInliers) pares[i]]);
  if (refinada != null) {
    final novos = <int>[];
    for (var i = 0; i < pares.length; i++) {
      if (_sampson(refinada, pares[i].$1, pares[i].$2) < limiar) novos.add(i);
    }
    if (novos.length >= melhorInliers.length) {
      return (e: refinada, inliers: novos);
    }
  }
  return (e: melhorE, inliers: melhorInliers);
}

/// A pose que poe mais pontos na FRENTE das duas cameras.
({Mat3 r, List<double> t, Map<int, List<double>> pontos})?
escolherPorCheiralidade(
  Mat3 e,
  List<(List<double>, List<double>)> pares,
  List<int> inliers,
) {
  ({Mat3 r, List<double> t, Map<int, List<double>> pontos})? melhor;
  var melhorContagem = -1;

  for (final (r, t) in posesDaEssencial(e)) {
    final pontos = <int, List<double>>{};
    for (final i in inliers) {
      final x = triangular([
        (Mat3.identidade, const [0.0, 0.0, 0.0], pares[i].$1),
        (r, t, pares[i].$2),
      ]);
      if (x == null) continue;
      if (x[2] <= 1e-6) continue;
      if (profundidade(r, t, x) <= 1e-6) continue;
      pontos[i] = x;
    }
    if (pontos.length > melhorContagem) {
      melhorContagem = pontos.length;
      melhor = (r: r, t: t, pontos: pontos);
    }
  }
  if (melhor == null || melhor.pontos.length < 6) return null;
  return melhor;
}

/// RESSECAO: a pose de UM quadro, sabendo onde os pontos estao no mundo.
///
/// Comeca do palpite (a pose do quadro vizinho, que num video esta
/// sempre perto) e desce pelo erro de reprojecao com Gauss-Newton
/// amortecido. O peso de Huber e o que impede um unico ponto perdido de
/// arrastar a camera inteira: erro grande deixa de crescer ao quadrado e
/// passa a crescer devagar.
({Mat3 r, List<double> t, double erro})? resolverPose(
  Mat3 r0,
  List<double> t0,
  List<(List<double>, List<double>)> correspondencias, {
  int iteracoes = 12,
  double huber = 0.01,
}) {
  if (correspondencias.length < 4) return null;
  var r = r0;
  var t = List<double>.from(t0);
  var amortecimento = 1e-4;
  var erroAnterior = double.infinity;

  for (var it = 0; it < iteracoes; it++) {
    final h = [for (var i = 0; i < 6; i++) List<double>.filled(6, 0.0)];
    final g = List<double>.filled(6, 0.0);
    var soma = 0.0;
    var usados = 0;

    for (final (x, obs) in correspondencias) {
      final c = r.aplicar(x);
      final xc = c[0] + t[0], yc = c[1] + t[1], zc = c[2] + t[2];
      if (zc < 1e-6) continue;
      final px = xc / zc, py = yc / zc;
      final rx = px - obs[0], ry = py - obs[1];
      final norm = math.sqrt(rx * rx + ry * ry);
      // Peso de Huber.
      final peso = norm <= huber ? 1.0 : huber / norm;
      soma += norm * norm * peso;
      usados++;

      // d(projecao)/d(ponto na camera).
      final iz = 1 / zc;
      final dpx = [iz, 0.0, -xc * iz * iz];
      final dpy = [0.0, iz, -yc * iz * iz];
      // d(ponto na camera)/d(incremento) = [-[Xc]x , I].
      final cruz = Mat3.cruzada([xc, yc, zc]);
      final jx = <double>[
        -(dpx[0] * cruz.at(0, 0) +
            dpx[1] * cruz.at(1, 0) +
            dpx[2] * cruz.at(2, 0)),
        -(dpx[0] * cruz.at(0, 1) +
            dpx[1] * cruz.at(1, 1) +
            dpx[2] * cruz.at(2, 1)),
        -(dpx[0] * cruz.at(0, 2) +
            dpx[1] * cruz.at(1, 2) +
            dpx[2] * cruz.at(2, 2)),
        dpx[0],
        dpx[1],
        dpx[2],
      ];
      final jy = <double>[
        -(dpy[0] * cruz.at(0, 0) +
            dpy[1] * cruz.at(1, 0) +
            dpy[2] * cruz.at(2, 0)),
        -(dpy[0] * cruz.at(0, 1) +
            dpy[1] * cruz.at(1, 1) +
            dpy[2] * cruz.at(2, 1)),
        -(dpy[0] * cruz.at(0, 2) +
            dpy[1] * cruz.at(1, 2) +
            dpy[2] * cruz.at(2, 2)),
        dpy[0],
        dpy[1],
        dpy[2],
      ];

      for (var a = 0; a < 6; a++) {
        g[a] -= peso * (jx[a] * rx + jy[a] * ry);
        for (var b = 0; b < 6; b++) {
          h[a][b] += peso * (jx[a] * jx[b] + jy[a] * jy[b]);
        }
      }
    }
    if (usados < 4) return null;

    final erro = math.sqrt(soma / usados);
    if ((erroAnterior - erro).abs() < 1e-9 && it > 2) break;
    erroAnterior = erro;

    for (var a = 0; a < 6; a++) {
      h[a][a] *= 1 + amortecimento;
    }
    final passo = resolverSistema(h, g);
    if (passo == null) {
      amortecimento *= 10;
      if (amortecimento > 1e3) break;
      continue;
    }
    final dw = [passo[0], passo[1], passo[2]];
    final dt = [passo[3], passo[4], passo[5]];
    if (norma(dw) > 1 || norma(dt) > 100) {
      amortecimento *= 10;
      if (amortecimento > 1e3) break;
      continue;
    }
    final rot = rotacaoDeVetor(dw);
    r = rotacaoMaisProxima(rot * r);
    final tr = rot.aplicar(t);
    t = [tr[0] + dt[0], tr[1] + dt[1], tr[2] + dt[2]];
    amortecimento = math.max(1e-6, amortecimento * .5);
  }

  // Erro final honesto: sem peso de Huber, so a media quadratica.
  var soma = 0.0;
  var n = 0;
  for (final (x, obs) in correspondencias) {
    final p = projetar(r, t, x);
    if (p == null) continue;
    final dx = p[0] - obs[0], dy = p[1] - obs[1];
    soma += dx * dx + dy * dy;
    n++;
  }
  if (n < 4) return null;
  return (r: r, t: t, erro: math.sqrt(soma / n));
}

// ------------------------------------------------------------ pipeline

class _Preparado {
  _Preparado(this.pontos, this.cx, this.cy, this.focal);
  final List<PontoSeguido> pontos;
  final double cx, cy, focal;

  List<double>? raio(int id, int quadro) {
    for (final p in pontos) {
      if (p.id != id) continue;
      final o = p.em(quadro);
      return o == null ? null : _raio(o, cx, cy, focal);
    }
    return null;
  }
}

/// A NOTA DE UM PAR DE QUADROS como semente da reconstrucao.
///
/// O criterio nao e "o mais longe": longe demais e nao ha pontos em
/// comum. E o que tem muitos pontos em comum E deslocamento suficiente —
/// sem deslocamento a essencial e indeterminada, e a cena sai plana.
/// Paralaxe pesa mais do que quantidade: dobrar o deslocamento melhora a
/// triangulacao mais do que dobrar o numero de pontos.
double _notaDoPar(
  List<PontoSeguido> pontos,
  int a,
  int b, {
  required double paralaxeMinima,
}) {
  final desloc = <double>[];
  for (final p in pontos) {
    final pa = p.em(a), pb = p.em(b);
    if (pa != null && pb != null) desloc.add((pb - pa).distance);
  }
  if (desloc.length < 12) return 0;
  final par = mediana(desloc);
  if (par < paralaxeMinima) return 0;
  return desloc.length * math.sqrt(par);
}

/// ESCOLHE O PAR INICIAL — os dois quadros de onde a cena nasce.
///
/// O PRIMEIRO QUADRO NAO E SAGRADO, e essa foi a licao cara. Enquanto a
/// base era sempre o quadro zero, tres filmagens comuns nao fechavam:
///
///   - o chicote, em que a camera fica quase parada no comeco e so
///     entao dispara: o par (0, k) nao tem deslocamento nenhum ate o
///     movimento comecar, e quando comeca os pontos do quadro zero ja
///     sairam do enquadramento;
///   - a filmagem escura, em que o ruido do primeiro quadro define a
///     geometria de tudo o que vem depois;
///   - a que comeca com borrao e so estabiliza depois de um segundo.
///
/// Em todas, ha um par excelente no meio do clipe. Procurar a base entre
/// varios candidatos custa algumas contas de mediana e resolve as tres —
/// e e o que qualquer reconstrucao seria faz: a semente escolhe a si
/// mesma, pela qualidade, e nao pela posicao na fila.
({int base, int parceiro})? _melhorPar(
  List<PontoSeguido> pontos,
  List<int> quadros, {
  required double paralaxeMinima,
}) {
  if (quadros.length < 2) return null;
  // Candidatos a base espalhados pela primeira metade: a base precisa
  // de quadros DEPOIS dela para a reconstrucao crescer para os dois
  // lados, entao nao adianta procurar perto do fim.
  final candidatos = <int>{quadros.first};
  final ate = math.max(1, quadros.length ~/ 2);
  for (var i = 0; i < 6; i++) {
    candidatos.add(quadros[(i * ate / 6).floor().clamp(0, quadros.length - 1)]);
  }

  ({int base, int parceiro})? melhor;
  var melhorNota = 0.0;
  for (final base in candidatos) {
    for (final q in quadros) {
      if (q <= base) continue;
      final nota = _notaDoPar(pontos, base, q, paralaxeMinima: paralaxeMinima);
      if (nota > melhorNota) {
        melhorNota = nota;
        melhor = (base: base, parceiro: q);
      }
    }
  }
  return melhor;
}

/// O SOLVER, com a distancia focal ja conhecida.
SolucaoCamera3D? _resolverComFocal(
  List<PontoSeguido> pontos,
  List<int> quadros, {
  required int largura,
  required int altura,
  required double focalPx,
  required int totalDeQuadros,
  required int fps,
  int rodadasDeRefino = 3,
  int tentativasRansac = 400,
  int? semente,
}) {
  if (pontos.length < 12 || quadros.length < 2) return null;
  final cx = largura / 2, cy = altura / 2;
  final prep = _Preparado(pontos, cx, cy, focalPx);

  final par = _melhorPar(pontos, quadros, paralaxeMinima: largura * 0.012);
  if (par == null) return null;
  final base = par.base;
  final parceiro = par.parceiro;

  // --- 1. par inicial
  final ids = <int>[];
  final pares = <(List<double>, List<double>)>[];
  for (final p in pontos) {
    final a = p.em(base), b = p.em(parceiro);
    if (a == null || b == null) continue;
    ids.add(p.id);
    pares.add((_raio(a, cx, cy, focalPx), _raio(b, cx, cy, focalPx)));
  }
  if (pares.length < 12) return null;

  final robusta = essencialRobusta(
    pares,
    limiar: 2.0 / focalPx,
    tentativas: tentativasRansac,
    semente: semente,
  );
  if (robusta == null) return null;

  final escolhida = escolherPorCheiralidade(robusta.e, pares, robusta.inliers);
  if (escolhida == null) return null;

  // DEGENERESCENCIA: a camera girou no lugar?
  //
  // Este e o caso que engana o solver de verdade. Uma essencial sai
  // igual, a cheiralidade escolhe uma pose, os numeros fecham — e a cena
  // e ficcao: sem translacao nao existe profundidade, e os "pontos 3D"
  // vao parar onde o ruido mandar. O sintoma no video e cruel, porque o
  // rastreio parece funcionar ate o objeto colado comecar a nadar.
  //
  // O teste honesto e o ANGULO DE PARALAXE: de quanto o mesmo ponto e
  // visto pelas duas cameras. Girar no lugar da zero por definicao,
  // ande a camera o quanto andar em rotacao.
  final centro2 = () {
    final rt = escolhida.r.transposta.aplicar(escolhida.t);
    return [-rt[0], -rt[1], -rt[2]];
  }();
  final angulos = <double>[];
  for (final x in escolhida.pontos.values) {
    final r1 = normalizar(x);
    final r2 = normalizar([
      x[0] - centro2[0],
      x[1] - centro2[1],
      x[2] - centro2[2],
    ]);
    angulos.add(math.acos(produtoInterno(r1, r2).clamp(-1.0, 1.0)));
  }
  if (angulos.isEmpty || mediana(angulos) < 0.006) {
    throw const RastreioException(
      FalhaDoRastreio.semParalaxe,
      'Esse plano nao tem paralaxe: a camera girou no lugar ou ficou '
      'parada. Sem deslocamento lateral nao da para medir profundidade — '
      'ande alguns passos enquanto filma e tente de novo.',
    );
  }

  // --- 2. primeira nuvem e primeiras duas poses
  final nuvem = <int, List<double>>{
    for (final e in escolhida.pontos.entries) ids[e.key]: e.value,
  };
  final poses = <int, ({Mat3 r, List<double> t})>{
    base: (r: Mat3.identidade, t: const [0.0, 0.0, 0.0]),
    parceiro: (r: escolhida.r, t: escolhida.t),
  };

  // --- 3. ressecao dos demais quadros, saindo do par para os dois lados
  final ordem = [...quadros]
    ..sort((a, b) {
      final da = (a - base).abs(), db = (b - base).abs();
      return da.compareTo(db);
    });

  ({Mat3 r, List<double> t})? vizinhaDe(int q) {
    ({Mat3 r, List<double> t})? melhor;
    var melhorD = 1 << 30;
    for (final e in poses.entries) {
      final d = (e.key - q).abs();
      if (d < melhorD) {
        melhorD = d;
        melhor = e.value;
      }
    }
    return melhor;
  }

  for (var rodada = 0; rodada < rodadasDeRefino; rodada++) {
    // --- ressecao
    for (final q in ordem) {
      final correspondencias = <(List<double>, List<double>)>[];
      for (final entrada in nuvem.entries) {
        final obs = prep.raio(entrada.key, q);
        if (obs == null) continue;
        correspondencias.add((entrada.value, obs));
      }
      if (correspondencias.length < 6) continue;
      final palpite = poses[q] ?? vizinhaDe(q);
      if (palpite == null) continue;
      final pose = resolverPose(palpite.r, palpite.t, correspondencias);
      if (pose == null) continue;
      // Erro absurdo = a ressecao fugiu; melhor ficar sem a pose desse
      // quadro do que colocar a camera do outro lado do mundo.
      if (pose.erro > 20 / focalPx) {
        if (rodada == 0) poses.remove(q);
        continue;
      }
      poses[q] = (r: pose.r, t: pose.t);
    }
    if (poses.length < 2) return null;

    // --- intersecao: retriangula com TODAS as vistas
    final novaNuvem = <int, List<double>>{};
    for (final p in pontos) {
      final vistas = <(Mat3, List<double>, List<double>)>[];
      for (final e in poses.entries) {
        final o = p.em(e.key);
        if (o == null) continue;
        vistas.add((e.value.r, e.value.t, _raio(o, cx, cy, focalPx)));
      }
      if (vistas.length < 2) continue;
      final x = triangular(vistas);
      if (x == null) continue;
      // Um ponto atras de qualquer camera que o viu e um ponto errado.
      var valido = true;
      var erro = 0.0;
      for (final (r, t, obs) in vistas) {
        final proj = projetar(r, t, x);
        if (proj == null) {
          valido = false;
          break;
        }
        final dx = proj[0] - obs[0], dy = proj[1] - obs[1];
        erro = math.max(erro, math.sqrt(dx * dx + dy * dy));
      }
      if (!valido || erro > 6 / focalPx) continue;
      novaNuvem[p.id] = x;
    }
    if (novaNuvem.length < 8) return null;
    nuvem
      ..clear()
      ..addAll(novaNuvem);
  }

  // --- 4. erro final, em pixels - no total E POR PONTO
  var soma = 0.0;
  var n = 0;
  final errosPorPonto = <int, double>{};
  final vistasPorPonto = <int, int>{};
  for (final p in pontos) {
    final x = nuvem[p.id];
    if (x == null) continue;
    var somaDoPonto = 0.0;
    var vistas = 0;
    for (final e in poses.entries) {
      final o = p.em(e.key);
      if (o == null) continue;
      final proj = projetar(e.value.r, e.value.t, x);
      if (proj == null) continue;
      final dx = (proj[0] - (o.dx - cx) / focalPx) * focalPx;
      final dy = (proj[1] - (o.dy - cy) / focalPx) * focalPx;
      somaDoPonto += dx * dx + dy * dy;
      vistas++;
    }
    if (vistas == 0) continue;
    soma += somaDoPonto;
    n += vistas;
    errosPorPonto[p.id] = math.sqrt(somaDoPonto / vistas);
    vistasPorPonto[p.id] = vistas;
  }
  if (n == 0) return null;

  // A CAMERA CHEGOU A SAIR DO LUGAR?
  //
  // Este e o teste que realmente pega a filmagem de tripe. Uma
  // panoramica pura tem solucao perfeita e MENTIROSA: cameras todas no
  // mesmo ponto, pontos a qualquer profundidade, erro de reprojecao
  // zero. Nada nas contas reclama — so a geometria final denuncia, e o
  // sinal e o caminho da camera ser desprezivel perto do tamanho da
  // cena. Um objeto colado numa cena assim parece grudado ate a camera
  // mexer, e ai nada.
  final centros = [
    for (final e in poses.entries)
      () {
        final rt = e.value.r.transposta.aplicar(e.value.t);
        return [-rt[0], -rt[1], -rt[2]];
      }(),
  ];
  var percurso = 0.0;
  for (var i = 0; i < centros.length; i++) {
    for (var j = i + 1; j < centros.length; j++) {
      percurso = math.max(
        percurso,
        norma([
          centros[i][0] - centros[j][0],
          centros[i][1] - centros[j][1],
          centros[i][2] - centros[j][2],
        ]),
      );
    }
  }
  var meio = [0.0, 0.0, 0.0];
  for (final v in nuvem.values) {
    meio = [meio[0] + v[0], meio[1] + v[1], meio[2] + v[2]];
  }
  meio = [
    meio[0] / nuvem.length,
    meio[1] / nuvem.length,
    meio[2] / nuvem.length,
  ];
  final raioDaCena = mediana([
    for (final v in nuvem.values)
      norma([v[0] - meio[0], v[1] - meio[1], v[2] - meio[2]]),
  ]);
  if (raioDaCena > 1e-9 && percurso / raioDaCena < 0.01) {
    throw const RastreioException(
      FalhaDoRastreio.semParalaxe,
      'Esse plano nao tem paralaxe: a camera girou no lugar ou ficou '
      'parada. Sem deslocamento lateral nao da para medir profundidade — '
      'ande alguns passos enquanto filma e tente de novo.',
    );
  }

  final lista = poses.keys.toList()..sort();
  return SolucaoCamera3D(
    largura: largura,
    altura: altura,
    focalPx: focalPx,
    poses: [for (final q in lista) PoseCamera(q, poses[q]!.r, poses[q]!.t)],
    nuvem: nuvem,
    erroPixels: math.sqrt(soma / n),
    quadros: totalDeQuadros,
    fps: fps,
    errosPorPonto: errosPorPonto,
    vistasPorPonto: vistasPorPonto,
    pontosSeguidos: pontos.length,
  );
}

/// RESOLVE A CAMERA a partir dos pontos seguidos.
///
/// Quando [focalPx] nao e informada, a distancia focal e descoberta por
/// varredura: resolve uma versao curta da cena com varias focais
/// candidatas e fica com a que explica melhor as observacoes. Duas
/// vistas sozinhas nao decidem a focal (uma focal errada e absorvida
/// pela geometria); TRES ou mais decidem, e por isso a varredura usa um
/// punhado de quadros espalhados e nao so o par inicial.
/// O QUANTO UMA TRANSFORMACAO PLANA JA EXPLICA O MOVIMENTO.
///
/// E a medida que separa uma filmagem que da rastreio 3D de uma que nao
/// da, e ela se tira ANTES de qualquer conta de camera — sem sequer
/// saber a distancia focal. Perto de zero significa que uma homografia
/// (giro puro, ou tudo na mesma distancia) descreve o que os pontos
/// fizeram; e ai a profundidade simplesmente nao esta na imagem.
///
/// Devolve o residuo em pixels do quadro analisado, ou null quando nao
/// ha pares suficientes para medir.
double? residuoPlanoDaCena(List<PontoSeguido> pontos, int quadros) {
  final marcos = <int>{
    0,
    quadros ~/ 4,
    quadros ~/ 2,
    (quadros * 3) ~/ 4,
    quadros - 1,
  }.toList()..sort();
  final residuos = <double>[];
  for (var i = 0; i < marcos.length; i++) {
    for (var j = i + 1; j < marcos.length; j++) {
      final pares = <(Offset, Offset)>[];
      for (final p in pontos) {
        final a = p.em(marcos[i]), b = p.em(marcos[j]);
        if (a != null && b != null) pares.add((a, b));
      }
      if (pares.length < 8) continue;
      final r = residuoDeHomografia(pares);
      if (r.isFinite) residuos.add(r);
    }
  }
  return residuos.isEmpty ? null : mediana(residuos);
}

/// O "AUTO DETECT": que tipo de cena e esta, em uma palavra.
///
/// Os dois limiares sao a mesma medida em escalas diferentes: abaixo de
/// 0,33% da largura o movimento e plano demais para haver profundidade;
/// entre isso e 1% ha profundidade, mas pouca, e o solve sai fraco.
LeituraDaCena lerCena(
  List<PontoSeguido> pontos, {
  required int largura,
  required int quadros,
}) {
  final r = residuoPlanoDaCena(pontos, quadros);
  if (r == null) return LeituraDaCena.quaseChata;
  if (r < largura * 0.0033) return LeituraDaCena.tripeOuGiro;
  if (r < largura * 0.01) return LeituraDaCena.quaseChata;
  return LeituraDaCena.profundidadeBoa;
}

SolucaoCamera3D resolverCamera3D(
  List<PontoSeguido> pontos, {
  required int largura,
  required int altura,
  required int quadros,
  int fps = 12,
  double? focalPx,
  int? semente,
  TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
  int rodadasDeRefino = 3,
}) {
  if (pontos.length < 12) {
    throw const RastreioException(
      FalhaDoRastreio.poucosPontos,
      'Poucos pontos para rastrear. O plano precisa de textura: '
      'parede lisa, ceu limpo ou desfoque forte nao dao onde agarrar.',
    );
  }

  final todos = [for (var i = 0; i < quadros; i++) i];

  // ANTES DE QUALQUER CONTA: da para rastrear em 3D neste plano?
  //
  // A pergunta se responde com uma homografia, e sem saber a distancia
  // focal. Se uma transformacao plana ja explica como os pontos se
  // deslocam, entao ou a camera girou no lugar, ou o que esta no quadro
  // e chato — e nos dois casos a profundidade nao esta na imagem. O
  // solver ainda assim devolveria uma cena: sem translacao a conta fecha
  // com as cameras todas no mesmo ponto e erro de reprojecao zero. E
  // exatamente por parecer certa que essa solucao e perigosa, e por isso
  // a recusa vem antes, e nao depois.
  // QUEM DIZ QUE FILMOU NO TRIPE JA SABE A RESPOSTA. Nao ha o que
  // resolver: sem deslocamento nao existe profundidade a medir, e
  // devolver uma cena seria devolver uma invencao.
  if (tipoDeTomada == TipoDeTomada.tripe) {
    throw const RastreioException(
      FalhaDoRastreio.semParalaxe,
      'Tomada de tripe nao tem paralaxe: a camera gira, mas nao anda. '
      'Para prender um objeto 3D no chao e preciso que a camera se '
      'desloque. Para uma panoramica, use o rastreio 2D de objeto.',
    );
  }

  final residuo = residuoPlanoDaCena(pontos, quadros);
  if (residuo != null && residuo < largura * 0.0033) {
    throw const RastreioException(
      FalhaDoRastreio.semParalaxe,
      'Esse plano nao da rastreio 3D: uma unica transformacao plana ja '
      'explica todo o movimento. Ou a camera girou no lugar (tripe), ou '
      'o que aparece esta todo na mesma distancia. Filme andando alguns '
      'passos, com coisas perto e longe no quadro.',
    );
  }

  if (focalPx != null) {
    final s = _resolverComFocal(
      pontos,
      todos,
      largura: largura,
      altura: altura,
      focalPx: focalPx,
      totalDeQuadros: quadros,
      fps: fps,
      semente: semente,
      rodadasDeRefino: rodadasDeRefino,
    );
    if (s == null) {
      throw const RastreioException(
        FalhaDoRastreio.naoConvergiu,
        'Nao consegui fechar a conta da camera nesse plano.',
      );
    }
    return _arrumarMundo(s).copiarCom(tipoDeTomada: tipoDeTomada);
  }

  // Varredura de focal num subconjunto de quadros — o suficiente para
  // decidir, barato o bastante para tentar quatorze vezes.
  final amostra = <int>[];
  const alvo = 7;
  for (var i = 0; i < alvo; i++) {
    final q = (i * (quadros - 1) / (alvo - 1)).round();
    if (!amostra.contains(q)) amostra.add(q);
  }

  double? melhorFocal;
  var melhorErro = double.infinity;
  for (var k = 0; k < 14; k++) {
    final f = largura * (0.5 + k * 0.2);
    // Uma focal candidata que nao fecha nao e erro: e so uma candidata
    // descartada. Se NENHUMA fechar, a mensagem sai la embaixo.
    SolucaoCamera3D? s;
    try {
      s = _resolverComFocal(
        pontos,
        amostra,
        largura: largura,
        altura: altura,
        focalPx: f,
        totalDeQuadros: quadros,
        fps: fps,
        rodadasDeRefino: 2,
        tentativasRansac: 150,
        semente: semente,
      );
    } on RastreioException {
      continue;
    }
    if (s == null) continue;
    // O erro em pixels e comparavel entre focais: as observacoes sao as
    // mesmas, so o modelo muda.
    if (s.erroPixels < melhorErro) {
      melhorErro = s.erroPixels;
      melhorFocal = f;
    }
  }

  if (melhorFocal == null) {
    throw const RastreioException(
      FalhaDoRastreio.semParalaxe,
      'Esse plano nao tem paralaxe: a camera girou no lugar ou nao saiu '
      'do lugar. Sem deslocamento nao da para saber a profundidade.',
    );
  }

  final s = _resolverComFocal(
    pontos,
    todos,
    largura: largura,
    altura: altura,
    focalPx: melhorFocal,
    totalDeQuadros: quadros,
    fps: fps,
    semente: semente,
  );
  if (s == null) {
    throw const RastreioException(
      FalhaDoRastreio.naoConvergiu,
      'Nao consegui fechar a conta da camera nesse plano.',
    );
  }
  return _arrumarMundo(s).copiarCom(tipoDeTomada: tipoDeTomada);
}

/// PONHA O MUNDO EM PE E NUM TAMANHO UTIL.
///
/// O solver devolve um mundo com origem, orientacao e escala
/// arbitrarias — nao ha como saber "para cima" nem "um metro" so
/// olhando. Duas convencoes resolvem isso na pratica:
///
///   CIMA: o topo da imagem, em media, e para cima no mundo. Ninguem
///   filma de lado sem querer. Ajustar por um plano de chao seria mais
///   preciso quando ha chao, e desastroso quando o que domina o quadro e
///   uma parede.
///
///   TAMANHO: a nuvem cabe num raio de 350 unidades, que e a ordem de
///   grandeza do resto da cena 3D do app. Sem isso o rastreio de um
///   plano largo devolve numeros na casa dos milhoes e nada do que a
///   pessoa adicionar depois aparece.
/// A arrumacao do mundo (+Y para cima, centro na nuvem, raio 350) para
/// quem resolve a camera por fora deste arquivo (o motor em C++).
SolucaoCamera3D arrumarMundo(SolucaoCamera3D s) => _arrumarMundo(s);

SolucaoCamera3D _arrumarMundo(SolucaoCamera3D s) {
  if (s.poses.isEmpty || s.nuvem.isEmpty) return s;

  // 1. a media do "cima" das cameras.
  var cima = [0.0, 0.0, 0.0];
  for (final p in s.poses) {
    final c = p.cima;
    cima = [cima[0] + c[0], cima[1] + c[1], cima[2] + c[2]];
  }
  cima = normalizar(cima);
  if (norma(cima) < .5) cima = [0, 1, 0];

  // Rotacao que leva `cima` para +Y.
  const alvo = [0.0, 1.0, 0.0];
  final eixo = produtoVetorial(cima, alvo);
  final seno = norma(eixo);
  final cosseno = produtoInterno(cima, alvo).clamp(-1.0, 1.0);
  final giro = seno < 1e-9
      ? (cosseno > 0
            ? Mat3.identidade
            : const Mat3([1, 0, 0, 0, -1, 0, 0, 0, -1]))
      : rotacaoDeVetor([
          eixo[0] / seno * math.atan2(seno, cosseno),
          eixo[1] / seno * math.atan2(seno, cosseno),
          eixo[2] / seno * math.atan2(seno, cosseno),
        ]);

  // 2. centro e escala da nuvem, ja no mundo girado.
  final girados = <int, List<double>>{
    for (final e in s.nuvem.entries) e.key: giro.aplicar(e.value),
  };
  var centro = [0.0, 0.0, 0.0];
  for (final v in girados.values) {
    centro = [centro[0] + v[0], centro[1] + v[1], centro[2] + v[2]];
  }
  centro = [
    centro[0] / girados.length,
    centro[1] / girados.length,
    centro[2] / girados.length,
  ];
  final distancias = [
    for (final v in girados.values)
      norma([v[0] - centro[0], v[1] - centro[1], v[2] - centro[2]]),
  ];
  final raio = mediana(distancias);
  final escala = raio < 1e-9 ? 1.0 : 350 / raio;

  List<double> mover(List<double> v) => [
    (v[0] - centro[0]) * escala,
    (v[1] - centro[1]) * escala,
    (v[2] - centro[2]) * escala,
  ];

  // 3. as poses seguem o mesmo caminho, ao contrario: R' = R * G^T e
  // t' = escala * (t + R * (G^T * ... )). Mais simples e seguro:
  // recompor a partir da POSICAO e da orientacao, que e o que o resto do
  // app consome.
  final novasPoses = <PoseCamera>[];
  final gT = giro.transposta;
  for (final p in s.poses) {
    final novaR = p.rotacao * gT;
    final posMundo = mover(giro.aplicar(p.posicao));
    final rt = novaR.aplicar(posMundo);
    novasPoses.add(PoseCamera(p.quadro, novaR, [-rt[0], -rt[1], -rt[2]]));
  }

  // O GIRO NAO MEXE NO ERRO. Rodar e mover o mundo inteiro nao muda onde
  // cada ponto cai na imagem - e por isso a ficha de qualidade atravessa
  // esta funcao intacta.
  return s.copiarCom(
    poses: novasPoses,
    nuvem: {for (final e in girados.entries) e.key: mover(e.value)},
  );
}

/// MOVE A ORIGEM DO MUNDO para [origem] (um ponto da nuvem, o centro
/// de um plano). So translacao: nada gira nem muda de tamanho, e a
/// reprojecao fica IDENTICA — o que muda e onde o (0, 0, 0) mora.
SolucaoCamera3D definirOrigem(SolucaoCamera3D s, List<double> origem) {
  List<double> mover(List<double> v) => [
    v[0] - origem[0],
    v[1] - origem[1],
    v[2] - origem[2],
  ];
  final novasPoses = <PoseCamera>[];
  for (final p in s.poses) {
    final pos = mover(p.posicao);
    final rt = p.rotacao.aplicar(pos);
    novasPoses.add(PoseCamera(p.quadro, p.rotacao, [-rt[0], -rt[1], -rt[2]]));
  }
  return s.copiarCom(
    poses: novasPoses,
    nuvem: {for (final e in s.nuvem.entries) e.key: mover(e.value)},
  );
}

/// A CONVENCAO METRICA DO MUNDO 3D: 1 metro = 100 unidades. Rastreio de
/// uma camera so nao sabe tamanho (a mesma filmagem serve para uma
/// maquete e para um predio); Definir escala ancora essa ambiguidade
/// numa medida que a pessoa conhece.
const unidadesPorMetro = 100.0;

/// ESCALA DO MUNDO: multiplica tudo por [fator]. A reprojecao nao muda
/// um pixel — muda o que "1 unidade" quer dizer.
SolucaoCamera3D escalarMundo(SolucaoCamera3D s, double fator) {
  if (!fator.isFinite || fator <= 0) return s;
  List<double> mover(List<double> v) => [
    v[0] * fator,
    v[1] * fator,
    v[2] * fator,
  ];
  final novasPoses = <PoseCamera>[];
  for (final p in s.poses) {
    final pos = mover(p.posicao);
    final rt = p.rotacao.aplicar(pos);
    novasPoses.add(PoseCamera(p.quadro, p.rotacao, [-rt[0], -rt[1], -rt[2]]));
  }
  return s.copiarCom(
    poses: novasPoses,
    nuvem: {for (final e in s.nuvem.entries) e.key: mover(e.value)},
  );
}

/// Unidades que a pessoa pode usar ao medir uma distancia da cena.
enum UnidadeReal {
  mm,
  cm,
  m,
  pol,
  pe;

  double get metros => switch (this) {
    UnidadeReal.mm => 0.001,
    UnidadeReal.cm => 0.01,
    UnidadeReal.m => 1,
    UnidadeReal.pol => 0.0254,
    UnidadeReal.pe => 0.3048,
  };

  String get emPalavras => switch (this) {
    UnidadeReal.mm => 'mm',
    UnidadeReal.cm => 'cm',
    UnidadeReal.m => 'm',
    UnidadeReal.pol => 'pol',
    UnidadeReal.pe => 'pés',
  };
}

/// O fator que faz a distancia entre os pontos [a] e [b] valer [metros]
/// de verdade (na convencao de [unidadesPorMetro]). Nulo quando os
/// pontos nao existem ou estao praticamente juntos.
double? fatorDeEscalaReal(SolucaoCamera3D s, int a, int b, double metros) {
  final va = s.nuvem[a], vb = s.nuvem[b];
  if (va == null || vb == null || !metros.isFinite || metros <= 0) return null;
  final d = norma([va[0] - vb[0], va[1] - vb[1], va[2] - vb[2]]);
  if (d < 1e-9) return null;
  return metros * unidadesPorMetro / d;
}

/// O PLANO DO CHAO a partir de pontos escolhidos — o "definir plano do
/// chao e origem" do After Effects.
///
/// Sem isto, o mundo fica com a orientacao que o solver achou, e um
/// texto posto no chao entra na areia torto. Com tres pontos ou mais, o
/// plano medio deles vira o y = 0 do mundo e a origem vai para o centro
/// deles.
SolucaoCamera3D definirChao(SolucaoCamera3D s, List<int> idsDoChao) {
  final escolhidos = [
    for (final id in idsDoChao)
      if (s.nuvem[id] != null) s.nuvem[id]!,
  ];
  if (escolhidos.length < 3) return s;

  var centro = [0.0, 0.0, 0.0];
  for (final v in escolhidos) {
    centro = [centro[0] + v[0], centro[1] + v[1], centro[2] + v[2]];
  }
  centro = [
    centro[0] / escolhidos.length,
    centro[1] / escolhidos.length,
    centro[2] / escolhidos.length,
  ];

  // A normal do plano e a direcao de MENOR espalhamento dos pontos — o
  // autovetor do menor autovalor da matriz de covariancia.
  final cov = [for (var i = 0; i < 3; i++) List<double>.filled(3, 0.0)];
  for (final v in escolhidos) {
    final d = [v[0] - centro[0], v[1] - centro[1], v[2] - centro[2]];
    for (var i = 0; i < 3; i++) {
      for (var j = 0; j < 3; j++) {
        cov[i][j] += d[i] * d[j];
      }
    }
  }
  var normal = normalizar(autovaloresSimetrica(cov).vetores.first);

  // A normal aponta para o lado das cameras: o chao fica ABAIXO delas.
  var acima = 0.0;
  for (final p in s.poses) {
    final pos = p.posicao;
    acima += produtoInterno(normal, [
      pos[0] - centro[0],
      pos[1] - centro[1],
      pos[2] - centro[2],
    ]);
  }
  if (acima < 0) normal = [-normal[0], -normal[1], -normal[2]];

  const alvo = [0.0, 1.0, 0.0];
  final eixo = produtoVetorial(normal, alvo);
  final seno = norma(eixo);
  final cosseno = produtoInterno(normal, alvo).clamp(-1.0, 1.0);
  final angulo = math.atan2(seno, cosseno);
  final giro = seno < 1e-9
      ? (cosseno > 0
            ? Mat3.identidade
            : const Mat3([1, 0, 0, 0, -1, 0, 0, 0, -1]))
      : rotacaoDeVetor([
          eixo[0] / seno * angulo,
          eixo[1] / seno * angulo,
          eixo[2] / seno * angulo,
        ]);

  List<double> mover(List<double> v) {
    final g = giro.aplicar([
      v[0] - centro[0],
      v[1] - centro[1],
      v[2] - centro[2],
    ]);
    return g;
  }

  final gT = giro.transposta;
  final novasPoses = <PoseCamera>[];
  for (final p in s.poses) {
    final novaR = p.rotacao * gT;
    final pos = mover(p.posicao);
    final rt = novaR.aplicar(pos);
    novasPoses.add(PoseCamera(p.quadro, novaR, [-rt[0], -rt[1], -rt[2]]));
  }

  return s.copiarCom(
    poses: novasPoses,
    nuvem: {for (final e in s.nuvem.entries) e.key: mover(e.value)},
  );
}
