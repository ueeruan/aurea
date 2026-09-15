import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'tracker2d.dart';

/// MUITOS PONTOS SEGUIDOS AO MESMO TEMPO — a entrada do rastreio de
/// camera.
///
/// O rastreador de um ponto ([trackSequence]) resolve "grudar um texto
/// no objeto". Para descobrir COMO A CAMERA SE MOVEU nao basta um ponto:
/// um ponto so nao distingue a camera andando para o lado de a camera
/// girando. Sao precisos dezenas de pontos espalhados, e a diferenca
/// entre como cada um se move (a paralaxe) e o que revela a profundidade.
///
/// Duas decisoes que definem a qualidade do resto:
///
///   1. ONDE POR OS PONTOS. Nao em qualquer lugar: so em CANTOS. Um
///      ponto no meio de uma parede lisa pode deslizar para qualquer
///      lado sem que a semelhanca mude, e um ponto que desliza envenena
///      o solver inteiro — ele acredita no que recebe.
///   2. QUANDO DESISTIR. Um ponto que sai do quadro, e coberto ou muda
///      de aparencia tem de MORRER, nao ser arrastado. Morte cedo custa
///      um ponto; ponto errado custa a cena.

/// Um ponto acompanhado ao longo do clipe.
///
/// As observacoes sao por quadro e podem ter buracos no fim (o ponto
/// morreu). Nao ha buraco no meio de proposito: quando a semelhanca cai,
/// o ponto acaba — recuperar depois seria adivinhar.
class PontoSeguido {
  PontoSeguido(this.id, this.primeiroQuadro, this.observacoes);

  final int id;
  final int primeiroQuadro;

  /// Quadro -> posicao em pixels do quadro ANALISADO.
  final Map<int, Offset> observacoes;

  int get ultimoQuadro =>
      observacoes.keys.fold(primeiroQuadro, (a, b) => a > b ? a : b);

  int get duracao => observacoes.length;

  Offset? em(int quadro) => observacoes[quadro];

  /// O quanto o ponto andou entre o primeiro e o ultimo quadro.
  double get deslocamento {
    final a = observacoes[primeiroQuadro];
    final b = observacoes[ultimoQuadro];
    if (a == null || b == null) return 0;
    return (b - a).distance;
  }
}

/// A FORCA DE CANTO de cada pixel (Shi-Tomasi).
///
/// A conta e a matriz de estrutura de uma janela: quanto o brilho varia
/// em x, em y, e o quanto essas variacoes andam juntas. O MENOR
/// autovalor dessa matriz 2x2 e a resposta — menor, e nao maior, porque
/// e ele que diz "varia nas DUAS direcoes". Uma borda reta tem um
/// autovalor grande e outro perto de zero: da para escorregar ao longo
/// dela, e por isso borda nao serve como ponto de rastreio.
Float32List forcaDeCanto(GrayFrame f, {int janela = 3}) {
  final w = f.width, h = f.height;
  final saida = Float32List(w * h);
  if (w < 2 * janela + 3 || h < 2 * janela + 3) return saida;

  // Gradientes por diferenca central.
  final gx = Float32List(w * h);
  final gy = Float32List(w * h);
  for (var y = 1; y < h - 1; y++) {
    for (var x = 1; x < w - 1; x++) {
      final i = y * w + x;
      gx[i] = (f.pixels[i + 1] - f.pixels[i - 1]) / 2;
      gy[i] = (f.pixels[i + w] - f.pixels[i - w]) / 2;
    }
  }

  for (var y = janela + 1; y < h - janela - 1; y++) {
    for (var x = janela + 1; x < w - janela - 1; x++) {
      var sxx = 0.0, syy = 0.0, sxy = 0.0;
      for (var dy = -janela; dy <= janela; dy++) {
        for (var dx = -janela; dx <= janela; dx++) {
          final i = (y + dy) * w + (x + dx);
          sxx += gx[i] * gx[i];
          syy += gy[i] * gy[i];
          sxy += gx[i] * gy[i];
        }
      }
      // Menor autovalor de [[sxx, sxy], [sxy, syy]].
      final meio = (sxx + syy) / 2;
      final raiz = math.sqrt(
        math.max(0, meio * meio - (sxx * syy - sxy * sxy)),
      );
      saida[y * w + x] = meio - raiz;
    }
  }
  return saida;
}

/// OS MELHORES CANTOS, espalhados.
///
/// Espalhar importa tanto quanto a forca: cinquenta cantos todos na
/// mesma quina da imagem dizem menos sobre a camera do que dez
/// distribuidos pelo quadro. Por isso, depois de ordenar pela forca,
/// cada candidato so entra se estiver a pelo menos [distanciaMinima] de
/// todos os que ja entraram (supressao de nao-maximos por distancia).
///
/// [evitar] sao pontos que ja estao sendo seguidos: ao repor os que
/// morreram, nao adianta nascer de novo em cima de quem ja existe.
List<Offset> detectarCantos(
  GrayFrame f, {
  int maximo = 120,
  double distanciaMinima = 8,
  double qualidade = 0.02,
  List<Offset> evitar = const [],
  int margem = 14,
}) {
  final forca = forcaDeCanto(f);
  if (forca.isEmpty) return const [];

  var maiorForca = 0.0;
  for (final v in forca) {
    if (v > maiorForca) maiorForca = v;
  }
  if (maiorForca <= 0) return const [];
  // O corte e RELATIVO ao melhor canto da imagem: um limiar absoluto
  // acharia tudo num quadro contrastado e nada num quadro de neblina.
  final corte = maiorForca * qualidade.clamp(0.0001, 1.0);

  final candidatos = <(double, int, int)>[];
  for (var y = margem; y < f.height - margem; y++) {
    for (var x = margem; x < f.width - margem; x++) {
      final v = forca[y * f.width + x];
      if (v >= corte) candidatos.add((v, x, y));
    }
  }
  candidatos.sort((a, b) => b.$1.compareTo(a.$1));

  final escolhidos = <Offset>[];
  final dm2 = distanciaMinima * distanciaMinima;
  for (final (_, x, y) in candidatos) {
    if (escolhidos.length >= maximo) break;
    final p = Offset(x.toDouble(), y.toDouble());
    var perto = false;
    for (final o in escolhidos) {
      final dx = o.dx - p.dx, dy = o.dy - p.dy;
      if (dx * dx + dy * dy < dm2) {
        perto = true;
        break;
      }
    }
    if (!perto) {
      for (final o in evitar) {
        final dx = o.dx - p.dx, dy = o.dy - p.dy;
        if (dx * dx + dy * dy < dm2) {
          perto = true;
          break;
        }
      }
    }
    if (!perto) escolhidos.add(p);
  }
  return escolhidos;
}

/// Ajuste SUBPIXEL do casamento.
///
/// A busca inteira devolve o pixel vencedor; o pico verdadeiro quase
/// nunca cai no centro de um pixel. Encaixar uma parabola nos tres
/// valores em volta (o vencedor e os vizinhos) e o jeito classico e
/// barato de achar o topo — e a diferenca entre um solver que converge e
/// um que fica preso no ruido de meio pixel.
Offset refinarSubpixel(
  GrayFrame molde,
  Offset alvo,
  GrayFrame quadro,
  Offset achado,
  int patch,
) {
  double s(double dx, double dy) =>
      ncc(molde, alvo, quadro, Offset(achado.dx + dx, achado.dy + dy), patch);
  final centro = s(0, 0);
  final ex = s(-1, 0), dx = s(1, 0);
  final ey = s(0, -1), dy = s(0, 1);
  final denX = ex - 2 * centro + dx;
  final denY = ey - 2 * centro + dy;
  final ax = denX.abs() < 1e-9 ? 0.0 : (ex - dx) / (2 * denX);
  final ay = denY.abs() < 1e-9 ? 0.0 : (ey - dy) / (2 * denY);
  return Offset(
    achado.dx + ax.clamp(-1.0, 1.0),
    achado.dy + ay.clamp(-1.0, 1.0),
  );
}

/// SEGUE MUITOS PONTOS pela sequencia inteira.
///
/// A cada quadro: leva adiante quem ainda casa bem, mata quem nao casa,
/// e — quando o time fica pequeno demais — semeia cantos novos nas
/// regioes que ficaram vazias. Sem a reposicao, um travelling longo
/// termina sem nenhum ponto: os do comeco sairam todos pela borda.
///
/// O molde de cada ponto vem do quadro em que ele NASCEU, nunca do
/// anterior. Atualizar o molde parece melhor e e pior: o erro de cada
/// passo se acumula e em cem quadros o ponto esta seguindo outra coisa.
List<PontoSeguido> seguirPontos(
  List<GrayFrame> frames, {
  int maximoDePontos = 120,
  double distanciaMinima = 8,
  int patch = 7,
  int busca = 16,
  double semelhancaMinima = 0.72,
  double reporAbaixoDe = 0.6,
  int duracaoMinima = 6,
}) {
  if (frames.length < 2) return const [];

  final saida = <PontoSeguido>[];
  // Vivos: id -> (quadro do molde, posicao do molde, posicao atual)
  final vivos = <int, ({int quadro, Offset molde, Offset atual})>{};
  var proximoId = 1;

  void semear(int quadro) {
    final ocupados = [for (final v in vivos.values) v.atual];
    final novos = detectarCantos(
      frames[quadro],
      maximo: maximoDePontos - vivos.length,
      distanciaMinima: distanciaMinima,
      evitar: ocupados,
    );
    for (final p in novos) {
      final id = proximoId++;
      vivos[id] = (quadro: quadro, molde: p, atual: p);
      saida.add(PontoSeguido(id, quadro, {quadro: p}));
    }
  }

  semear(0);
  final porId = {for (final p in saida) p.id: p};

  for (var i = 1; i < frames.length; i++) {
    final mortos = <int>[];
    for (final entry in vivos.entries.toList()) {
      final id = entry.key;
      final v = entry.value;
      final (achado, score) = matchPatch(
        frames[v.quadro],
        v.molde,
        frames[i],
        patch: patch,
        busca: busca,
        seed: v.atual,
      );
      if (score < semelhancaMinima) {
        mortos.add(id);
        continue;
      }
      // Fora da area util o molde ja esta metade fora da imagem: o que
      // sobra casa com qualquer coisa.
      if (achado.dx < patch + 1 ||
          achado.dy < patch + 1 ||
          achado.dx > frames[i].width - patch - 2 ||
          achado.dy > frames[i].height - patch - 2) {
        mortos.add(id);
        continue;
      }
      final fino = refinarSubpixel(
        frames[v.quadro],
        v.molde,
        frames[i],
        achado,
        patch,
      );
      vivos[id] = (quadro: v.quadro, molde: v.molde, atual: fino);
      porId[id]!.observacoes[i] = fino;
    }
    for (final id in mortos) {
      vivos.remove(id);
    }

    // Repor: so quando o time encolheu de verdade, e nunca no ultimo
    // quadro (um ponto que nasce no fim nao serve para nada).
    if (i < frames.length - duracaoMinima &&
        vivos.length < maximoDePontos * reporAbaixoDe) {
      final antes = saida.length;
      semear(i);
      for (var k = antes; k < saida.length; k++) {
        porId[saida[k].id] = saida[k];
      }
    }
  }

  // Um ponto visto em tres quadros nao diz nada sobre a camera e ainda
  // atrapalha: entra na conta com peso igual ao de quem foi seguido o
  // clipe inteiro.
  return [
    for (final p in saida)
      if (p.duracao >= duracaoMinima) p,
  ];
}
