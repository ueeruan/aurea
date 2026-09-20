import 'package:flutter/foundation.dart';

/// O PEDACO DA LINHA DO TEMPO QUE ESTA NA TELA, em pixels do CONTEUDO.
///
/// ============================ POR QUE EXISTE ==========================
///
/// O eixo do tempo e um `SingleChildScrollView` com o conteudo INTEIRO la
/// dentro: uma hora a 400 px/s sao 1,44 milhao de pixels. Nada ali sabia
/// o que estava visivel — a regua riscava a duracao toda, a onda montava
/// um caminho da largura do clipe, e cada pedaco de um video decupado
/// virava uma barra com alcas, previa e losangos mesmo a dez telas do
/// cabecote.
///
/// Esta e a janela: `[iniPx, fimPx]` no sistema de coordenadas do conteudo
/// (zero = instante zero do projeto, ja descontada a meia tela de recuo).
/// Quem desenha ou constroi olha para ela e ignora o resto.
///
/// QUANTIZADA DE PROPOSITO. Se a janela mudasse a cada pixel de rolagem,
/// quem a escuta reconstruiria/repintaria a cada quadro — o contrario do
/// que se quer. Ela anda em baldes de [balde] px: entre uma troca e outra,
/// rolar e so transladar camadas prontas.
@immutable
class JanelaDaTimeline {
  const JanelaDaTimeline(this.iniPx, this.fimPx);

  /// Sem recorte: tudo e visivel. E o valor de quem ainda nao mediu a
  /// tela, e o que os pintores assumem quando ninguem lhes da janela.
  static const JanelaDaTimeline tudo = JanelaDaTimeline(
    double.negativeInfinity,
    double.infinity,
  );

  /// O passo da quantizacao, em pixels.
  static const double balde = 256;

  /// A folga minima para cada lado. Pequena: a janela e recalculada no
  /// MESMO quadro em que a rolagem anda (o aviso do controlador chega
  /// antes da montagem), entao a folga so cobre o arredondamento do balde
  /// e a inercia de um quadro.
  static const double folga = 96;

  final double iniPx;
  final double fimPx;

  /// A janela para uma rolagem em [offset], numa tela de [viewport] px
  /// cujo conteudo comeca recuado de [recuo] px (a meia tela que poe o
  /// instante zero sob o cabecote central).
  factory JanelaDaTimeline.de({
    required double offset,
    required double viewport,
    required double recuo,
  }) {
    final ini = offset - recuo - folga;
    final fim = offset - recuo + viewport + folga;
    return JanelaDaTimeline(
      (ini / balde).floorToDouble() * balde,
      (fim / balde).ceilToDouble() * balde,
    );
  }

  /// O trecho `[a, b]` aparece (nem que seja uma ponta)?
  bool cruza(double a, double b) => b >= iniPx && a <= fimPx;

  /// O ponto [x], com [margem] px de tolerancia para cada lado.
  bool contem(double x, {double margem = 0}) =>
      x >= iniPx - margem && x <= fimPx + margem;

  @override
  bool operator ==(Object other) =>
      other is JanelaDaTimeline && other.iniPx == iniPx && other.fimPx == fimPx;

  @override
  int get hashCode => Object.hash(iniPx, fimPx);

  @override
  String toString() => 'JanelaDaTimeline($iniPx..$fimPx)';
}

/// O aviso da janela. Um `ValueNotifier` que sabe dizer se alguem o
/// escuta: a linha do tempo troca o valor direto enquanto ninguem ouve (a
/// primeira montagem) e adia para depois do quadro quando ha ouvintes —
/// avisar no meio da montagem e proibido.
class AvisoDaJanela extends ValueNotifier<JanelaDaTimeline> {
  AvisoDaJanela() : super(JanelaDaTimeline.tudo);

  bool get temOuvintes => hasListeners;
}
