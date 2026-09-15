import 'dart:convert';
import 'dart:math' as math;

import 'algebra_numerica.dart';

/// RASTREIO DE CAMERA 3D — descobrir por onde a camera passou, a partir
/// do video e de nada mais.
///
/// O problema: uma imagem e a sombra de um mundo de tres dimensoes num
/// plano de duas. Uma imagem so nao tem volta. Mas DUAS imagens do mesmo
/// mundo, tiradas de lugares diferentes, tem: o quanto cada ponto se
/// desloca entre elas depende de quao longe ele esta (paralaxe), e dessa
/// diferenca sai ao mesmo tempo a forma da cena e o caminho da camera.
///
/// A CONTA EM SI NAO MORA MAIS AQUI: quem resolve e o motor nativo
/// (packages/aurea_tracker2). Este arquivo guarda o RESULTADO — a pose
/// de cada quadro, a nuvem, a ficha de qualidade — e as operacoes de
/// MUNDO que a pessoa faz depois: por em pe, definir chao, origem e
/// escala real. Sao transformacoes de semelhanca: nenhuma muda um
/// pixel da reprojecao, e os testes provam isso.
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

}

class RastreioException implements Exception {
  const RastreioException(this.falha, this.mensagem);
  final FalhaDoRastreio falha;
  final String mensagem;
  @override
  String toString() => mensagem;
}

/// Onde o ponto CAI na imagem, em coordenadas normalizadas. Null se
/// estiver atras da camera.
///
/// E a REGUA do contrato inteiro: os testes de origem, escala e chao
/// provam que a reprojecao nao muda um pixel usando exatamente esta
/// conta, e qualquer motor que produza uma [SolucaoCamera3D] tem de
/// concordar com ela.
List<double>? projetar(Mat3 r, List<double> t, List<double> x) {
  final c = r.aplicar(x);
  final z = c[2] + t[2];
  if (z < 1e-6) return null;
  return [(c[0] + t[0]) / z, (c[1] + t[1]) / z];
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
