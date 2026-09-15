import 'dart:math' as math;

import 'algebra_numerica.dart';
import 'camera_solver3d.dart';
import 'cena_do_rastreio.dart';

/// O PLANO ACHADO NA NUVEM — o alvo que aparece em cima do vídeo.
///
/// Depois de resolver a câmera, o que a pessoa quer não é a nuvem: é
/// UMA SUPERFÍCIE onde pôr o texto. Escolher três pontos e digitar
/// posição e rotação é o caminho que ninguém percorre num celular. Aqui
/// os pontos escolhidos viram um plano, e o plano vira um lugar.
class PlanoDoRastreio implements PlanoLike {
  const PlanoDoRastreio({
    required this.origem,
    required this.normal,
    required this.eixoX,
    required this.eixoZ,
    required this.tamanho,
    required this.espessura,
    required this.ids,
  });

  /// O centro dos pontos que formaram o plano. É onde o objeto nasce.
  @override
  final List<double> origem;

  /// A direção perpendicular à superfície, apontando para fora dela.
  @override
  final List<double> normal;

  /// Dois eixos dentro do plano. Com a normal, formam a orientação
  /// completa — é o que faz o texto deitar na mesa em vez de flutuar
  /// virado para um lado qualquer.
  final List<double> eixoX;
  final List<double> eixoZ;

  /// Metade da largura do retângulo que cobre os pontos. Serve para
  /// desenhar o alvo do tamanho da superfície, e não sempre igual.
  @override
  final double tamanho;

  /// O quanto os pontos fogem do plano, em unidades do mundo. Um valor
  /// alto quer dizer que aquilo não era uma superfície.
  final double espessura;

  /// Quais pontos entraram.
  final List<int> ids;

  /// Se os pontos realmente descrevem uma superfície. Uma nuvem em forma
  /// de nuvem também tem um "plano médio" — e ele não quer dizer nada.
  bool get ehSuperficie => ids.length >= 3 && espessura < tamanho * 0.25;

  /// CHÃO, PAREDE OU MESA — pela direção da normal.
  ///
  /// O mundo já vem em pé (o solver alinha +Y para cima), então a
  /// pergunta é só quanto a normal se afasta da vertical. Nomear ajuda
  /// mais do que parece: "Chão" diz à pessoa que o texto vai deitar, e
  /// ela reconhece na hora se escolheu a superfície errada.
  TipoDeSuperficie get tipo {
    final vertical = normal[1].abs();
    if (vertical > 0.80) return TipoDeSuperficie.chao;
    if (vertical < 0.35) return TipoDeSuperficie.parede;
    return TipoDeSuperficie.inclinada;
  }

  /// A matriz que leva do plano para o mundo, em colunas (X, Y, Z), com
  /// Y na normal — é a orientação que um objeto colado aqui recebe.
  Mat3 get orientacao => Mat3([
    eixoX[0],
    normal[0],
    eixoZ[0],
    eixoX[1],
    normal[1],
    eixoZ[1],
    eixoX[2],
    normal[2],
    eixoZ[2],
  ]);

  /// Os três ângulos de Euler que o motor 3D usa, em graus (X, Y, Z).
  ///
  /// A cena 3D do app aplica as rotações nessa ordem; converter aqui é o
  /// que faz o objeto entrar deitado na superfície sem ninguém mexer em
  /// rotação nenhuma.
  @override
  (double, double, double) get anglesEmGraus {
    final m = orientacao;
    const g = 180 / math.pi;
    final sy = -m.at(2, 0);
    if (sy.abs() > 0.99999) {
      // Olhando reto para cima ou para baixo: X e Z viram o mesmo eixo
      // e a decomposição perde um grau de liberdade. Fixar Z em zero é a
      // escolha que não faz o objeto pular ao cruzar essa posição.
      return (
        math.atan2(-m.at(1, 2), m.at(1, 1)) * g,
        (sy > 0 ? math.pi / 2 : -math.pi / 2) * g,
        0.0,
      );
    }
    return (
      math.atan2(m.at(2, 1), m.at(2, 2)) * g,
      math.asin(sy.clamp(-1.0, 1.0)) * g,
      math.atan2(m.at(1, 0), m.at(0, 0)) * g,
    );
  }
}

enum TipoDeSuperficie {
  chao,
  parede,
  inclinada;

  String get emPalavras => switch (this) {
    TipoDeSuperficie.chao => 'Chão',
    TipoDeSuperficie.parede => 'Parede',
    TipoDeSuperficie.inclinada => 'Superfície inclinada',
  };
}

/// AJUSTA UM PLANO AOS PONTOS ESCOLHIDOS.
///
/// A normal é a direção de MENOR espalhamento — o autovetor do menor
/// autovalor da covariância. É a mesma conta que define o chão, e aqui
/// ela serve para qualquer superfície.
///
/// Devolve null com menos de três pontos: dois pontos definem uma reta,
/// e uma reta tem infinitos planos passando por ela.
PlanoDoRastreio? planoDosPontos(
  Map<int, List<double>> nuvem,
  List<int> ids, {
  List<double>? ladoDeFora,
}) {
  final pontos = [
    for (final id in ids)
      if (nuvem[id] != null) nuvem[id]!,
  ];
  final usados = [
    for (final id in ids)
      if (nuvem[id] != null) id,
  ];
  if (pontos.length < 3) return null;

  var centro = [0.0, 0.0, 0.0];
  for (final v in pontos) {
    centro = [centro[0] + v[0], centro[1] + v[1], centro[2] + v[2]];
  }
  centro = [
    centro[0] / pontos.length,
    centro[1] / pontos.length,
    centro[2] / pontos.length,
  ];

  final cov = [for (var i = 0; i < 3; i++) List<double>.filled(3, 0.0)];
  for (final v in pontos) {
    final d = [v[0] - centro[0], v[1] - centro[1], v[2] - centro[2]];
    for (var i = 0; i < 3; i++) {
      for (var j = 0; j < 3; j++) {
        cov[i][j] += d[i] * d[j];
      }
    }
  }
  final auto = autovaloresSimetrica(cov);
  // Os autovalores saem em ordem crescente: o primeiro vetor é a
  // direção que menos espalha (a normal), e os dois últimos são os
  // eixos dentro do plano.
  var normal = normalizar(auto.vetores.first);
  if (norma(normal) < .5) return null;

  // A NORMAL APONTA PARA FORA. "Fora" é o lado de onde se olha — a
  // câmera. Sem isto, metade das superfícies nasceria com o objeto
  // enterrado nela, dependendo só do sinal que a decomposição devolveu.
  final referencia = ladoDeFora ?? const [0.0, 1.0, 0.0];
  if (produtoInterno(normal, [
        referencia[0] - centro[0],
        referencia[1] - centro[1],
        referencia[2] - centro[2],
      ]) <
      0) {
    normal = [-normal[0], -normal[1], -normal[2]];
  }

  var eixoX = normalizar(auto.vetores[2]);
  if (norma(eixoX) < .5) {
    eixoX = normalizar(produtoVetorial(normal, [0, 0, 1]));
    if (norma(eixoX) < .5) {
      eixoX = normalizar(produtoVetorial(normal, [1, 0, 0]));
    }
  }
  // Ortogonaliza contra a normal: a decomposição já devolve eixos
  // ortogonais, mas o desempate acima pode ter trazido um vetor torto.
  final proj = produtoInterno(eixoX, normal);
  eixoX = normalizar([
    eixoX[0] - proj * normal[0],
    eixoX[1] - proj * normal[1],
    eixoX[2] - proj * normal[2],
  ]);
  // X CRUZ NORMAL, e nao normal cruz X. A ordem trocada devolve um trio
  // de MAO ESQUERDA (determinante -1): a matriz parece uma rotacao, mas
  // e uma reflexao, e o objeto colado no chao nascia de cabeca para
  // baixo — cento e oitenta graus em X, sem nada na tela explicando.
  final eixoZ = normalizar(produtoVetorial(eixoX, normal));

  var espessura = 0.0;
  var extensao = 0.0;
  for (final v in pontos) {
    final d = [v[0] - centro[0], v[1] - centro[1], v[2] - centro[2]];
    espessura += math.pow(produtoInterno(d, normal), 2).toDouble();
    extensao = math.max(
      extensao,
      math.sqrt(
        math.pow(produtoInterno(d, eixoX), 2) +
            math.pow(produtoInterno(d, eixoZ), 2),
      ),
    );
  }

  return PlanoDoRastreio(
    origem: centro,
    normal: normal,
    eixoX: eixoX,
    eixoZ: eixoZ,
    tamanho: extensao,
    espessura: math.sqrt(espessura / pontos.length),
    ids: usados,
  );
}

/// ACHA A MAIOR SUPERFÍCIE DA NUVEM SOZINHO.
///
/// É o que faz "selecionei uns pontos e apareceu o alvo" funcionar sem
/// que a pessoa saiba o que é um plano. RANSAC: sorteia três pontos,
/// mede quantos outros caem perto do plano deles, e fica com o melhor.
///
/// A tolerância é uma FRAÇÃO DO TAMANHO DA CENA, e não um número em
/// unidades: a escala do mundo reconstruído é arbitrária, então
/// "5 unidades" não quer dizer nada — mas "1% do raio da nuvem" quer.
PlanoDoRastreio? maiorPlano(
  Map<int, List<double>> nuvem, {
  List<int>? entre,
  double tolerancia = 0.03,
  int tentativas = 300,
  int? semente,
}) {
  final ids = (entre ?? nuvem.keys.toList())
      .where((id) => nuvem[id] != null)
      .toList();
  if (ids.length < 3) return null;
  if (ids.length <= 6) return planoDosPontos(nuvem, ids);

  var centro = [0.0, 0.0, 0.0];
  for (final id in ids) {
    final v = nuvem[id]!;
    centro = [centro[0] + v[0], centro[1] + v[1], centro[2] + v[2]];
  }
  centro = [
    centro[0] / ids.length,
    centro[1] / ids.length,
    centro[2] / ids.length,
  ];
  final raio = mediana([
    for (final id in ids)
      norma([
        nuvem[id]![0] - centro[0],
        nuvem[id]![1] - centro[1],
        nuvem[id]![2] - centro[2],
      ]),
  ]);
  if (raio < 1e-9) return null;
  final limite = raio * tolerancia;

  // SEMENTE FIXA por padrão: o mesmo vídeo tem de dar o mesmo plano em
  // duas aberturas do projeto, senão o objeto colado nele muda de lugar
  // sozinho entre uma sessão e outra.
  final sorte = math.Random(semente ?? 20260908);
  List<int>? melhor;
  var melhorContagem = 0;
  for (var t = 0; t < tentativas; t++) {
    final a = nuvem[ids[sorte.nextInt(ids.length)]]!;
    final b = nuvem[ids[sorte.nextInt(ids.length)]]!;
    final c = nuvem[ids[sorte.nextInt(ids.length)]]!;
    final n = produtoVetorial(
      [b[0] - a[0], b[1] - a[1], b[2] - a[2]],
      [c[0] - a[0], c[1] - a[1], c[2] - a[2]],
    );
    if (norma(n) < 1e-9) continue;
    final u = normalizar(n);
    final dentro = <int>[];
    for (final id in ids) {
      final v = nuvem[id]!;
      final d = produtoInterno(u, [v[0] - a[0], v[1] - a[1], v[2] - a[2]]);
      if (d.abs() <= limite) dentro.add(id);
    }
    if (dentro.length > melhorContagem) {
      melhorContagem = dentro.length;
      melhor = dentro;
    }
  }
  // Menos de um quarto da nuvem não é uma superfície: é um acaso de três
  // pontos. Dizer "não achei" é melhor do que colar um alvo no vazio.
  if (melhor == null || melhor.length < math.max(3, ids.length ~/ 4)) {
    return null;
  }
  return planoDosPontos(nuvem, melhor);
}

/// TIRA PONTOS DA SOLUÇÃO e recalcula o erro sem eles.
///
/// É o "delete bad tracks" da referência. O erro médio muda porque ele é
/// a média sobre os pontos que ficaram — e é justamente essa mudança que
/// a pessoa quer ver: apagar os ruins melhora a ficha, e o número prova.
///
/// As POSES NÃO SE MEXEM aqui de propósito. Recalcular a câmera é outra
/// operação (custa segundos e precisa dos rastros 2D, que a solução não
/// guarda); esta é instantânea e reversível.
SolucaoCamera3D semPontos(SolucaoCamera3D s, Set<int> ids) {
  if (ids.isEmpty) return s;
  final nuvem = {
    for (final e in s.nuvem.entries)
      if (!ids.contains(e.key)) e.key: e.value,
  };
  if (nuvem.isEmpty) return s;
  final erros = {
    for (final e in s.errosPorPonto.entries)
      if (nuvem.containsKey(e.key)) e.key: e.value,
  };
  final vistas = {
    for (final e in s.vistasPorPonto.entries)
      if (nuvem.containsKey(e.key)) e.key: e.value,
  };
  // A média volta a ser ponderada pelas observações de cada ponto, que é
  // como ela foi calculada na primeira vez.
  var soma = 0.0;
  var n = 0;
  for (final e in erros.entries) {
    final v = vistas[e.key] ?? 1;
    soma += e.value * e.value * v;
    n += v;
  }
  return s.copiarCom(
    nuvem: nuvem,
    errosPorPonto: erros,
    vistasPorPonto: vistas,
    erroPixels: n == 0 ? s.erroPixels : math.sqrt(soma / n),
  );
}
