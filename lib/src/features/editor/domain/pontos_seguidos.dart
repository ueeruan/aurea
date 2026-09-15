import 'dart:ui';

/// O QUE O SEGUIDOR DEVOLVE — a entrada do rastreio de camera.
///
/// Quem SEGUE os pontos e o motor nativo; este arquivo e so o formato
/// em que os rastros chegam ao resto do app (nuvem, planos, tela).
///
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
