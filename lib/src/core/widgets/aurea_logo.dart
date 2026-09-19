import 'package:flutter/material.dart';

import '../theme/aurea_colors.dart';

/// A LOGO DO AUREA, em vetor (mesma geometria do SVG original, base 108).
///
/// ==========================================================================
/// A MESMA FORMA, AGORA COM VOLUME
/// ==========================================================================
///
/// O caminho e o de sempre — quem ja conhece a marca nao vai reaprender nada.
/// O que mudou em 18/09/2026 foi o DESENHO: a versao antiga era um traco
/// chapado de cor unica (lima) mais uma bola de outra cor (violeta). A nova e
/// um TUBO azul com luz em cima e sombra embaixo, e a bola virou esfera.
///
/// O VOLUME E FEITO COM QUATRO PASSADAS, e nao com um degrade.
///
/// Um `LinearGradient` num traco pinta ao LONGO do caminho: escurece da
/// esquerda para a direita, mas o tubo fica chato — nao ha luz nem sombra na
/// SECAO. O que da a sensacao de cilindro e a secao ter um lado claro e um
/// escuro, e isso se consegue desenhando o mesmo caminho varias vezes, cada
/// vez mais fino e mais claro, com um deslocamento pequeno:
///
///   1. a borda escura, um pouco mais grossa que o tubo
///   2. o tubo, com o degrade de comprimento
///   3. a faixa clara, deslocada para o lado da luz
///   4. o brilho, fino e quase branco, no mesmo lado
///
/// QUATRO `drawPath` E UMA BOLA. Sem `MaskFilter`, sem `saveLayer`, sem
/// imagem: a memoria diz que na GPU do iPhone cada `saveLayer` e um passe
/// inteiro, e esta logo aparece em quatro lugares do app (Ajustes, boas
/// vindas, barra de projeto e tela inicial) — inclusive dentro de listas.
///
/// O TAMANHO NAO MUDA O DESENHO. Tudo e proporcional a base 108, entao a
/// marca de 22 px tem a mesma proporcao da de 96.
class AureaLogo extends StatelessWidget {
  const AureaLogo({
    super.key,
    this.size = 48,
    this.withBackground = true,
    this.borderRadius,
  });

  final double size;
  final bool withBackground;
  final BorderRadius? borderRadius;

  @override
  Widget build(BuildContext context) {
    final logo = CustomPaint(
      size: Size.square(size),
      painter: _AureaLogoPainter(withBackground: withBackground),
    );
    if (!withBackground) return logo;
    return ClipRRect(
      borderRadius: borderRadius ?? BorderRadius.circular(size * 0.22),
      child: logo,
    );
  }
}

/// A GEOMETRIA, EM BASE 108. E a mesma do SVG original e a mesma que gerou
/// `assets/icon/app_icon.png` — se um dia a marca mudar, os dois mudam juntos.
const double _kBase = 108;

/// A ESPESSURA DO TUBO. 9 e a medida do arquivo original.
const double _kTraco = 9;

class _AureaLogoPainter extends CustomPainter {
  const _AureaLogoPainter({required this.withBackground});

  final bool withBackground;

  @override
  void paint(Canvas canvas, Size size) {
    final s = size.width / _kBase;
    canvas.scale(s, s);

    if (withBackground) {
      canvas.drawRect(
        const Rect.fromLTWH(0, 0, _kBase, _kBase),
        Paint()..color = AureaColors.bg,
      );
    }

    final traco = Path()
      ..moveTo(26, 77)
      ..cubicTo(26, 45, 39, 29, 62, 29)
      ..cubicTo(77, 29, 86, 37, 86, 49)
      ..cubicTo(86, 61, 76, 68, 59, 68)
      ..lineTo(44, 68);

    // O DEGRADE AO LONGO: do azul claro em cima a direita (onde bate a luz)
    // ao azul fundo em baixo a esquerda. Os pontos sao os cantos do desenho,
    // e nao do quadrado — o degrade inteiro acontece dentro da marca.
    final aoLongo = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = _kTraco
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..shader = const LinearGradient(
        begin: Alignment.topRight,
        end: Alignment.bottomLeft,
        colors: [Color(0xFF3E8BF0), Color(0xFF245D8C), Color(0xFF001A63)],
        stops: [0, .45, 1],
      ).createShader(const Rect.fromLTWH(21, 24, 74, 60));

    // 1. A BORDA. Um traco um pouco mais grosso, do azul mais fundo: e o que
    //    separa a marca do fundo mesmo quando o fundo e escuro.
    canvas.drawPath(
      traco,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = _kTraco + 0.9
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round
        ..color = const Color(0xFF00134F),
    );

    // 2. O TUBO.
    canvas.drawPath(traco, aoLongo);

    // 3. A FAIXA CLARA, para o lado de cima e da esquerda.
    canvas.save();
    canvas.translate(-0.75, -0.75);
    canvas.drawPath(
      traco,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = _kTraco * 0.46
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round
        ..color = const Color(0x8C4E9BF5),
    );

    // 4. O BRILHO.
    canvas.translate(-0.85, -0.85);
    canvas.drawPath(
      traco,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = _kTraco * 0.17
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round
        ..color = const Color(0x99DFF1FF),
    );
    canvas.restore();

    // A ESFERA. O centro e o raio sao os do original; o volume vem de um
    // radial FORA DO CENTRO, que e o que poe a luz em cima a esquerda e a
    // sombra embaixo a direita.
    const centro = Offset(87, 76);
    const raio = 8.0;
    canvas.drawCircle(
      centro,
      raio,
      Paint()
        ..shader = const RadialGradient(
          center: Alignment(-0.45, -0.5),
          radius: 1.05,
          colors: [Color(0xFF7FC0FF), Color(0xFF1F63C8), Color(0xFF001460)],
          stops: [0, .45, 1],
        ).createShader(
          Rect.fromCircle(center: centro, radius: raio),
        ),
    );
    // O PINGO DE LUZ da esfera, pequeno e no mesmo lado da faixa clara.
    canvas.drawCircle(
      const Offset(84.6, 73.2),
      1.6,
      Paint()..color = const Color(0xB3EAF6FF),
    );
  }

  @override
  bool shouldRepaint(_AureaLogoPainter oldDelegate) =>
      oldDelegate.withBackground != withBackground;
}
