import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../domain/layer.dart';

/// O RAIL LE O PROJETO DE VERDADE.
///
/// Os controles mostram a EDICAO PENDENTE — e o losango tem de dizer o
/// contrario: que ali ainda NAO ha marca. Se ele lesse a mesma camada
/// que a linha do numero, ficaria cheio antes de a pessoa gravar, diria
/// "tirar o keyframe daqui" e o toque apagaria uma marca que nunca
/// existiu (`docs/keyframe-explicito.md`).
Layer camadaReal(WidgetRef ref, Layer camada) =>
    ref.read(editorControllerProvider).layerById(camada.id) ?? camada;

/// A PROPRIEDADE QUE O RAIL ESQUERDO ESTA MIRANDO.
///
/// O rail e o mesmo em toda ferramenta — voltar, marcar keyframe, abrir
/// a curva —, mas a propriedade muda: na transformacao e o modo vigente
/// (posicao, rotacao...), nos efeitos e o parametro escolhido. Em vez de
/// o rail conhecer camada, modo e efeito, ele recebe isto pronto e nao
/// sabe de onde veio.
@immutable
class AlvoDoRail {
  const AlvoDoRail({
    this.temKeyframeAqui = false,
    this.animado = false,
    this.aoAlternarKeyframe,
    this.aoAbrirCurva,
  });

  /// Ha marca EXATAMENTE no cabecote?
  final bool temKeyframeAqui;

  /// A propriedade tem alguma marca, em qualquer instante?
  final bool animado;

  final VoidCallback? aoAlternarKeyframe;

  /// Nulo quando o cabecote nao esta dentro de um trecho entre duas
  /// marcas — nao ha caminho para curvar.
  final VoidCallback? aoAbrirCurva;
}

/// O RAIL ESQUERDO: voltar, keyframe, curva.
///
/// Sempre nesta ordem, em toda ferramenta, porque a mao aprende posicao
/// antes de aprender icone. Largura de 46 px, medida na referencia
/// (`docs/painel-de-transformacao-alight.md`).
///
/// O `‹` DAQUI VOLTA UM NIVEL — para a grade de categorias. O `‹` do
/// cabecalho da tela fecha a ferramenta inteira. Sao duas perguntas
/// diferentes, e por isso dois botoes.
class RailEsquerdo extends StatelessWidget {
  const RailEsquerdo({
    super.key,
    required this.aoVoltar,
    required this.alvo,
    this.mais,
  });

  final VoidCallback aoVoltar;
  final AlvoDoRail alvo;
  final Widget? mais;

  static const largura = 46.0;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: largura,
    child: Column(
      children: [
        _BotaoDoRail(
          key: const ValueKey('painel-voltar'),
          icone: Icons.chevron_left_rounded,
          rotulo: 'Voltar as ferramentas',
          tamanho: 24,
          aoTocar: aoVoltar,
        ),
        _BotaoCustomDoRail(
          rotulo: alvo.temKeyframeAqui
              ? 'Tirar o keyframe daqui'
              : 'Marcar keyframe aqui',
          aoTocar: alvo.aoAlternarKeyframe,
          child: CustomPaint(
            size: const Size(22, 22),
            painter: _DiamondKeyframePainter(
              hasKeyframe: alvo.temKeyframeAqui,
              isAnimated: alvo.animado,
              ativo: alvo.aoAlternarKeyframe != null,
            ),
          ),
        ),
        _BotaoCustomDoRail(
          rotulo: 'Editar curva da propriedade',
          aoTocar: alvo.aoAbrirCurva,
          child: Tooltip(
            message: 'Editar curva da propriedade',
            child: CustomPaint(
              size: const Size(20, 20),
              painter: _CurveIconPainter(
                ativo: alvo.aoAbrirCurva != null,
                isAnimated: alvo.animado,
              ),
            ),
          ),
        ),
        if (mais != null)
          Expanded(
            child: Center(child: mais!),
          ),
      ],
    ),
  );
}

/// O RAIL DIREITO: os quatro modos de transformacao.
class RailDireito extends StatelessWidget {
  const RailDireito({
    super.key,
    required this.modos,
    required this.vigente,
    required this.aoEscolher,
  });

  /// Cada modo: o icone e o rotulo de acessibilidade.
  final List<(IconData, String)> modos;
  final int vigente;
  final void Function(int) aoEscolher;

  static const largura = 40.0;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: largura,
    // O BOTAO ENCOLHE QUANDO A COLUNA NAO CABE.
    //
    // Sao cinco modos e um painel baixo (um 320x568 com o painel no
    // minimo): cinco botoes de 36 nao cabem, e a coluna estourava por
    // 8,8 px. O trilho e uma fileira de icones — encolher mantem os
    // cinco ALCANCAVEIS, e um trilho que rola para mostrar o quinto
    // esconde justamente a face que ele veio oferecer.
    child: LayoutBuilder(
      builder: (context, restricoes) {
        final altura = restricoes.maxHeight.isFinite && modos.isNotEmpty
            ? math.min(36.0, restricoes.maxHeight / modos.length)
            : 36.0;
        return Column(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: [
            for (var i = 0; i < modos.length; i++)
              Semantics(
                container: true,
                excludeSemantics: true,
                button: true,
                selected: i == vigente,
                label: modos[i].$2,
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: () => aoEscolher(i),
                  child: Container(
                    width: math.min(36.0, altura),
                    height: altura,
                    decoration: BoxDecoration(
                      color: i == vigente
                          ? const Color(0xFF1E222D)
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(8),
                      border: i == vigente
                          ? Border.all(color: AmColors.accent, width: 1.5)
                          : null,
                    ),
                    child: Icon(
                      modos[i].$1,
                      size: math.min(20.0, altura * .6),
                      color: i == vigente
                          ? AmColors.accent
                          : AureaColors.muted,
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    ),
  );
}

class _BotaoDoRail extends StatelessWidget {
  const _BotaoDoRail({
    super.key,
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
    this.tamanho = 20,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback? aoTocar;
  final double tamanho;

  @override
  Widget build(BuildContext context) {
    final ativo = aoTocar != null;
    return Expanded(
      child: Semantics(
        container: true,
        excludeSemantics: true,
        button: ativo,
        enabled: ativo,
        label: rotulo,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: aoTocar,
          child: SizedBox(
            width: RailEsquerdo.largura,
            child: Center(
              child: Icon(
                icone,
                size: tamanho,
                color: !ativo
                    ? AmColors.muted.withValues(alpha: .28)
                    : AmColors.text,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _BotaoCustomDoRail extends StatelessWidget {
  const _BotaoCustomDoRail({
    required this.rotulo,
    required this.aoTocar,
    required this.child,
  });

  final String rotulo;
  final VoidCallback? aoTocar;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final ativo = aoTocar != null;
    return Expanded(
      child: Semantics(
        container: true,
        excludeSemantics: true,
        button: ativo,
        enabled: ativo,
        label: rotulo,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: aoTocar,
          child: SizedBox(
            width: RailEsquerdo.largura,
            child: Center(child: child),
          ),
        ),
      ),
    );
  }
}

/// Losango de keyframe com sinal de '+' no centro (conforme UI oficial AM).
class _DiamondKeyframePainter extends CustomPainter {
  const _DiamondKeyframePainter({
    required this.hasKeyframe,
    required this.isAnimated,
    required this.ativo,
  });

  final bool hasKeyframe;
  final bool isAnimated;
  final bool ativo;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final color = !ativo
        ? const Color(0xFF434956)
        : hasKeyframe
        ? AmColors.accent
        : const Color(0xFFFFFFFF);

    final path = Path()
      ..moveTo(center.dx, 2)
      ..lineTo(size.width - 2, center.dy)
      ..lineTo(center.dx, size.height - 2)
      ..lineTo(2, center.dy)
      ..close();

    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.6
      ..color = color;

    canvas.drawPath(path, paint);

    if (hasKeyframe) {
      // Sinal de menos '-' quando já existe keyframe aqui
      canvas.drawLine(
        Offset(center.dx - 3.5, center.dy),
        Offset(center.dx + 3.5, center.dy),
        paint..strokeWidth = 1.5,
      );
    } else {
      // Sinal de mais '+' quando não há keyframe aqui
      canvas.drawLine(
        Offset(center.dx - 3.5, center.dy),
        Offset(center.dx + 3.5, center.dy),
        paint..strokeWidth = 1.5,
      );
      canvas.drawLine(
        Offset(center.dx, center.dy - 3.5),
        Offset(center.dx, center.dy + 3.5),
        paint..strokeWidth = 1.5,
      );
    }
  }

  @override
  bool shouldRepaint(_DiamondKeyframePainter old) =>
      old.hasKeyframe != hasKeyframe ||
      old.isAnimated != isAnimated ||
      old.ativo != ativo;
}

/// Ícone de curva em caixa arredondada com curva S Bezier.
class _CurveIconPainter extends CustomPainter {
  const _CurveIconPainter({required this.ativo, required this.isAnimated});

  final bool ativo;
  final bool isAnimated;

  @override
  void paint(Canvas canvas, Size size) {
    final color = !ativo
        ? const Color(0xFF434956)
        : (isAnimated ? AmColors.accent : AureaColors.muted);

    final rrect = RRect.fromRectAndRadius(
      Rect.fromLTWH(1, 1, size.width - 2, size.height - 2),
      const Radius.circular(4),
    );

    final boxPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.3
      ..color = color.withValues(alpha: 0.5);

    canvas.drawRRect(rrect, boxPaint);

    // Curva S suave no interior
    final path = Path()
      ..moveTo(4, size.height - 5)
      ..cubicTo(
        size.width * 0.45,
        size.height - 5,
        size.width * 0.55,
        5,
        size.width - 4,
        5,
      );

    final curvePaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5
      ..strokeCap = StrokeCap.round
      ..color = color;

    canvas.drawPath(path, curvePaint);
  }

  @override
  bool shouldRepaint(_CurveIconPainter old) =>
      old.ativo != ativo || old.isAnimated != isAnimated;
}
