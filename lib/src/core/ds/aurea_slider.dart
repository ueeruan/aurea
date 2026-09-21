import 'dart:math' as math;

import 'package:flutter/widgets.dart';

import '../ui/am_tick_ruler.dart';
import 'tokens.dart';

/// O DESLIZANTE DA UI NOVA.
///
/// NAO TEM CONTA PROPRIA. O gesto e o [AmArrastoDeValor] e o desenho usa
/// [paraCadaRisco] e [leituraDePosicao] — as tres pecas de
/// `core/ui/am_tick_ruler.dart` que concordam sobre a regra do dono:
/// arrastar para a DIREITA AUMENTA. Um deslizante com conta propria seria
/// o quarto lugar onde o sinal pode inverter.
///
/// DOIS DESENHOS, pelo tipo do numero:
///
///  * COM FAIXA ([min] e [max] finitos): trilho de 2,5, preenchimento de
///    POSICAO (cresce a partir do zero quando a faixa o cruza) e a alca no
///    valor. A sensibilidade padrao e a faixa inteira na largura: a alca
///    anda exatamente com o dedo.
///  * SEM FAIXA: riscos que deslizam com o valor e o indicador fixo no
///    centro — o numero nao tem "onde", so "quanto andou".
///
/// [arrastavel] FALSO quando quem arrasta e a linha inteira
/// ([AureaPropertyRow]): dois arrastos horizontais encaixados brigam na
/// arena e ganharia o de dentro, que e o mais estreito.
class AureaSlider extends StatelessWidget {
  const AureaSlider({
    super.key,
    required this.valor,
    required this.aoMudar,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.sensibilidade,
    this.aoComecar,
    this.aoTerminar,
    this.arrastavel = true,
    this.habilitado = true,
  });

  final double valor;
  final ValueChanged<double> aoMudar;
  final double min;
  final double max;

  /// Unidades por pixel de dedo. Nulo: com faixa, a faixa na largura
  /// util; sem faixa, 0,5.
  final double? sensibilidade;

  /// Comeco e fim do gesto (um arrasto = um passo de desfazer).
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  final bool arrastavel;
  final bool habilitado;

  bool get temFaixa => min.isFinite && max.isFinite && max > min;

  /// A sensibilidade que o deslizante usa numa largura [largura]. Publica
  /// porque a linha de propriedade, que arrasta pelo deslizante, precisa
  /// da MESMA conta para a alca continuar debaixo do dedo.
  static double sensibilidadePara({
    required double min,
    required double max,
    required double largura,
    double? pedida,
  }) {
    if (pedida != null && pedida > 0) return pedida;
    if (min.isFinite && max.isFinite && max > min) {
      final util = math.max(largura - AureaDims.alcaDoDeslizante, 1.0);
      return (max - min) / util;
    }
    return .5;
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        final largura = c.maxWidth.isFinite ? c.maxWidth : 200.0;
        final visual = SizedBox(
          height: AureaDims.toqueDoDeslizante,
          width: largura,
          child: CustomPaint(
            painter: PintorDoAureaSlider(
              valor: valor,
              min: min,
              max: max,
              porPixel: sensibilidadePara(
                min: min,
                max: max,
                largura: largura,
                pedida: sensibilidade,
              ),
              habilitado: habilitado,
              trilho: AureaCores.campoAlto,
              cheio: AureaCores.destaque,
              alca: AureaCores.texto,
              risco: AureaCores.textoSecundario,
            ),
          ),
        );
        if (!arrastavel || !habilitado) return visual;
        return AmArrastoDeValor(
          value: valor,
          min: min,
          max: max,
          unitsPerPixel: sensibilidadePara(
            min: min,
            max: max,
            largura: largura,
            pedida: sensibilidade,
          ),
          onStart: aoComecar,
          onEnd: aoTerminar,
          onChanged: aoMudar,
          child: visual,
        );
      },
    );
  }
}

/// O PINTOR — publico para os testes lerem o que ele desenharia.
///
/// Cores entram por parametro (o tema e lido no `build`, uma vez), e o
/// desenho e so `drawRRect`, `drawCircle` e `drawLine`: nada de camada,
/// sombra ou mascara. No Impeller cada `saveLayer` e um passe inteiro, e
/// este e o controle que fica sob o dedo.
class PintorDoAureaSlider extends CustomPainter {
  const PintorDoAureaSlider({
    required this.valor,
    required this.min,
    required this.max,
    required this.porPixel,
    required this.habilitado,
    required this.trilho,
    required this.cheio,
    required this.alca,
    required this.risco,
  });

  final double valor;
  final double min;
  final double max;
  final double porPixel;
  final bool habilitado;
  final Color trilho;
  final Color cheio;
  final Color alca;
  final Color risco;

  bool get _temFaixa => min.isFinite && max.isFinite && max > min;

  /// O trecho cheio do trilho, em pixels do deslizante — nulo sem faixa.
  /// A alca tem meia largura de cada lado: o trilho util e o miolo.
  ({double de, double ate, double alcaX})? leitura(Size size) {
    if (!_temFaixa) return null;
    final meia = AureaDims.alcaDoDeslizante / 2;
    final util = math.max(size.width - 2 * meia, 1.0);
    final l = leituraDePosicao(valor: valor, min: min, max: max, largura: util);
    if (l == null) return null;
    final v = valor.isFinite ? valor.clamp(min, max) : min;
    final x = meia + (v - min) / (max - min) * util;
    return (de: meia + l.de, ate: meia + l.ate, alcaX: x);
  }

  @override
  void paint(Canvas canvas, Size size) {
    final meio = size.height / 2;
    final opac = habilitado ? 1.0 : .4;
    final l = leitura(size);
    if (l != null) {
      const t = AureaDims.trilhoDoDeslizante;
      const raio = Radius.circular(t / 2);
      final meia = AureaDims.alcaDoDeslizante / 2;
      canvas.drawRRect(
        RRect.fromLTRBR(
          meia,
          meio - t / 2,
          size.width - meia,
          meio + t / 2,
          raio,
        ),
        Paint()..color = trilho.withValues(alpha: opac),
      );
      if (l.ate - l.de > .5) {
        canvas.drawRRect(
          RRect.fromLTRBR(l.de, meio - t / 2, l.ate, meio + t / 2, raio),
          Paint()..color = cheio.withValues(alpha: opac),
        );
      }
      // A alca desenhada e menor que a caixa de 25: a caixa e o alvo, o
      // circulo e o que o olho procura.
      canvas.drawCircle(
        Offset(l.alcaX, meio),
        AureaDims.alcaDoDeslizante * .3,
        Paint()..color = alca.withValues(alpha: opac),
      );
      return;
    }
    // SEM FAIXA: os riscos acompanham o dedo, o indicador fica no centro.
    final centro = size.width / 2;
    final fraco = Paint()
      ..color = risco.withValues(alpha: .35 * opac)
      ..strokeWidth = 1;
    final forte = Paint()
      ..color = risco.withValues(alpha: .7 * opac)
      ..strokeWidth = 1.4;
    paraCadaRisco(
      valor: valor,
      porPixel: porPixel,
      largura: size.width,
      origem: centro,
      desenhar: (x, eForte) {
        final h = eForte ? 7.0 : 4.0;
        canvas.drawLine(
          Offset(x, meio - h),
          Offset(x, meio + h),
          eForte ? forte : fraco,
        );
      },
    );
    canvas.drawLine(
      Offset(centro, meio - 10),
      Offset(centro, meio + 10),
      Paint()
        ..color = cheio.withValues(alpha: opac)
        ..strokeWidth = 2,
    );
  }

  @override
  bool shouldRepaint(PintorDoAureaSlider old) =>
      old.valor != valor ||
      old.min != min ||
      old.max != max ||
      old.porPixel != porPixel ||
      old.habilitado != habilitado ||
      old.trilho != trilho ||
      old.cheio != cheio ||
      old.alca != alca ||
      old.risco != risco;
}
