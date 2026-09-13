import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/tokens.dart';
import '../../domain/expr.dart';
import '../am/am_colors.dart';
import '../am/am_widgets.dart';

/// O ESTADO DE KEYFRAME de uma linha de parametro: o losango.
class KeyframeState {
  const KeyframeState({
    required this.animated,
    required this.here,
    required this.onToggle,
    this.onCurve,
  });

  /// A propriedade tem keyframes / tem keyframe neste instante.
  final bool animated;
  final bool here;

  /// Toque no losango: poe ou tira o keyframe no cabecote.
  final VoidCallback onToggle;

  /// Toque longo no losango: abre a curva (quando ha keyframes).
  final VoidCallback? onCurve;
}

/// A LINHA DE PARAMETRO (secao 4E do prompt, componente `ParameterRow`):
///
///   [◆] [nome] [—— regua ——] [valor tocavel]
///
/// Arrastar a regua muda o valor; TOCAR O NUMERO abre o teclado para o
/// valor exato (aceita "1080/3" e "50%"). Toque longo no nome reseta.
/// Toque longo no valor (Pro) abre a expressao, quando [onExpression]
/// existe.
class ParameterRow extends StatelessWidget {
  const ParameterRow({
    super.key,
    required this.label,
    required this.value,
    required this.onChanged,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.unitsPerPixel = 0.5,
    this.decimals = 1,
    this.unit = '',
    this.keyframe,
    this.onReset,
    this.rulerKey,
    this.valueKey,
    this.accentCenter = false,
    this.onExpression,
    this.expression,
    this.height = AureaTokens.minTap,
  });

  /// A expressao em vigor (Pro): o chip mostra "fx" e o toque longo edita.
  final String? expression;

  final String label;
  final double value;
  final ValueChanged<double> onChanged;
  final double min;
  final double max;
  final double unitsPerPixel;
  final int decimals;
  final String unit;
  final KeyframeState? keyframe;
  final VoidCallback? onReset;
  final Key? rulerKey;
  final Key? valueKey;
  final bool accentCenter;
  final VoidCallback? onExpression;
  final double height;

  @override
  Widget build(BuildContext context) {
    // A LINHA INTEIRA E A SUPERFICIE DE ARRASTO.
    //
    // Antes so a faixa de riscos puxava o valor. Num aparelho de 375 px
    // sobravam vinte e tres pixels para ela na linha "Largura" — o
    // losango, o rotulo e o numero comiam o resto — e o beta relatou
    // exatamente isso: "nao da pra mexer no botao de largura, so no de
    // altura". A altura funcionava por acaso, por nao ter losango.
    //
    // Envolver a linha resolve sem mexer no desenho: o rotulo, os
    // espacos e a propria regua puxam. O losango e o numero continuam
    // recebendo o TOQUE, porque toque e arrasto sao gestos diferentes e
    // a arena entrega cada um a quem pediu.
    return AmArrastoDeValor(
      value: value,
      min: min,
      max: max,
      unitsPerPixel: unitsPerPixel,
      onChanged: onChanged,
      child: ParameterFrame(
        label: label,
        keyframe: keyframe,
        onReset: onReset,
        height: height,
        child: Row(
          children: [
            Expanded(
              child: AmTickRuler(
                key: rulerKey,
                value: value,
                min: min,
                max: max,
                unitsPerPixel: unitsPerPixel,
                accentCenter: accentCenter,
                height: height - 8,
                arrastavel: false,
                onChanged: onChanged,
              ),
            ),
            const SizedBox(width: 6),
            ParameterValue(
              key: valueKey,
              text: '${amNumber(value, decimals)}$unit',
              prefix: (expression ?? '').trim().isEmpty ? null : 'fx',
              onLongPress: onExpression,
              onTap: () async {
                final v = await showNumberInput(
                  context,
                  value: value,
                  unit: unit,
                  min: min,
                  max: max,
                  decimals: decimals,
                );
                if (v != null) onChanged(v);
              },
            ),
          ],
        ),
      ),
    );
  }
}

/// [◆] [nome] [ X valor ] [ Y valor ] — para posicao e pivo, onde a
/// regua e o proprio pad de arrasto.
class ParameterPointRow extends StatelessWidget {
  const ParameterPointRow({
    super.key,
    required this.label,
    required this.x,
    required this.y,
    required this.onX,
    required this.onY,
    this.compact = false,
    this.z,
    this.onZ,
    this.decimals = 1,
    this.keyframe,
    this.onReset,
    this.height = AureaTokens.minTap,
  });

  final bool compact;
  final String label;
  final double x;
  final double y;
  final double? z;
  final ValueChanged<double> onX;
  final ValueChanged<double> onY;
  final ValueChanged<double>? onZ;
  final int decimals;
  final KeyframeState? keyframe;
  final VoidCallback? onReset;
  final double height;

  @override
  Widget build(BuildContext context) {
    Widget eixo(String nome, double v, ValueChanged<double> onChanged) =>
        Expanded(
          child: ParameterValue(
            key: ValueKey('${label.toLowerCase()}-$nome'),
            prefix: nome,
            text: amNumber(v, decimals),
            width: double.infinity,
            onTap: () async {
              final novo = await showNumberInput(
                context,
                value: v,
                decimals: decimals,
              );
              if (novo != null) onChanged(novo);
            },
          ),
        );
    if (compact) {
      return SizedBox(
        height: height,
        child: Row(
          children: [
            eixo('X', x, onX),
            const SizedBox(width: 6),
            eixo('Y', y, onY),
            if (z != null && onZ != null) ...[
              const SizedBox(width: 6),
              eixo('Z', z!, onZ!),
            ],
          ],
        ),
      );
    }
    return ParameterFrame(
      label: label,
      keyframe: keyframe,
      onReset: onReset,
      height: height,
      child: Row(
        children: [
          eixo('X', x, onX),
          const SizedBox(width: 6),
          eixo('Y', y, onY),
          if (z != null && onZ != null) ...[
            const SizedBox(width: 6),
            eixo('Z', z!, onZ!),
          ],
        ],
      ),
    );
  }
}

/// [nome] [interruptor].
class ParameterToggleRow extends StatelessWidget {
  const ParameterToggleRow({
    super.key,
    required this.label,
    required this.value,
    required this.onChanged,
    this.valueKey,
    this.keyframe,
    this.height = AureaTokens.minTap,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;
  final Key? valueKey;
  final KeyframeState? keyframe;
  final double height;

  @override
  Widget build(BuildContext context) => ParameterFrame(
    label: label,
    keyframe: keyframe,
    height: height,
    child: Align(
      alignment: Alignment.centerRight,
      child: CupertinoSwitch(
        key: valueKey,
        value: value,
        activeTrackColor: AmColors.action,
        onChanged: onChanged,
      ),
    ),
  );
}

/// [nome] [amostra de cor ›] — toque abre o seletor.
class ParameterColorRow extends StatelessWidget {
  const ParameterColorRow({
    super.key,
    required this.label,
    required this.color,
    required this.onTap,
    this.valueKey,
    this.height = AureaTokens.minTap,
  });

  final String label;
  final Color color;
  final VoidCallback onTap;
  final Key? valueKey;
  final double height;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return ParameterFrame(
      label: label,
      height: height,
      child: GestureDetector(
        key: valueKey,
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Row(
          children: [
            Container(
              width: 30,
              height: 30,
              decoration: BoxDecoration(
                color: color,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.white24),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: AppText(
                '#${color.toARGB32().toRadixString(16).padLeft(8, '0').substring(2).toUpperCase()}',
                style: TextStyle(fontSize: 12.5, color: t.text),
              ),
            ),
            Icon(CupertinoIcons.chevron_right, size: 14, color: t.muted),
          ],
        ),
      ),
    );
  }
}

/// [nome] [qualquer coisa] — para chips, escolhas e campos.
class ParameterCustomRow extends StatelessWidget {
  const ParameterCustomRow({
    super.key,
    required this.label,
    required this.child,
    this.keyframe,
    this.onReset,
    this.height = AureaTokens.minTap,
  });

  final String label;
  final Widget child;
  final KeyframeState? keyframe;
  final VoidCallback? onReset;
  final double height;

  @override
  Widget build(BuildContext context) => ParameterFrame(
    label: label,
    keyframe: keyframe,
    onReset: onReset,
    height: height,
    child: child,
  );
}

/// A MOLDURA comum: losango (opcional), nome (toque longo = resetar) e o
/// conteudo a direita.
class ParameterFrame extends StatelessWidget {
  const ParameterFrame({
    super.key,
    required this.label,
    required this.child,
    this.keyframe,
    this.onReset,
    this.height = AureaTokens.minTap,
    this.labelWidth = 76,
  });

  final String label;
  final Widget child;
  final KeyframeState? keyframe;
  final VoidCallback? onReset;
  final double height;
  final double labelWidth;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    final kf = keyframe;
    return SizedBox(
      height: height,
      child: Row(
        children: [
          if (kf != null)
            Tooltip(
              message: kf.here
                  ? 'Remover keyframe deste parametro'
                  : 'Keyframe neste parametro',
              child: GestureDetector(
                key: ValueKey('kf-${_slug(label)}'),
                behavior: HitTestBehavior.opaque,
                onTap: kf.onToggle,
                onLongPress: kf.onCurve,
                child: SizedBox(
                  width: 30,
                  height: height,
                  child: Icon(
                    kf.here
                        ? CupertinoIcons.rhombus_fill
                        : CupertinoIcons.rhombus,
                    size: 15,
                    color: kf.animated
                        ? t.keyframe
                        : t.muted.withValues(alpha: .6),
                  ),
                ),
              ),
            )
          else
            const SizedBox(width: 6),
          Tooltip(
            message: onReset == null ? label : '$label (toque longo: resetar)',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onLongPress: onReset,
              child: SizedBox(
                width: labelWidth,
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.centerLeft,
                    child: AppText(
                      label,
                      maxLines: 1,
                      style: TextStyle(fontSize: 12.5, color: t.muted),
                    ),
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(width: 6),
          Expanded(child: child),
        ],
      ),
    );
  }

  static String _slug(String s) => s
      .toLowerCase()
      .replaceAll(RegExp(r'[^a-z0-9]+'), '-')
      .replaceAll(RegExp(r'^-|-$'), '');
}

/// O VALOR TOCAVEL: chip com o numero; toque abre o teclado.
class ParameterValue extends StatelessWidget {
  const ParameterValue({
    super.key,
    required this.text,
    required this.onTap,
    this.onLongPress,
    this.prefix,
    this.width = 74,
  });

  final String text;
  final VoidCallback onTap;
  final VoidCallback? onLongPress;
  final String? prefix;
  final double width;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return Tooltip(
      message: 'Toque para digitar o valor',
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        onLongPress: onLongPress,
        child: Container(
          width: width,
          height: 34,
          padding: const EdgeInsets.symmetric(horizontal: 6),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: t.chip,
            borderRadius: BorderRadius.circular(8),
          ),
          child: FittedBox(
            fit: BoxFit.scaleDown,
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (prefix != null) ...[
                  AppText(
                    prefix!,
                    style: TextStyle(fontSize: 10.5, color: t.muted),
                  ),
                  const SizedBox(width: 4),
                ],
                AppText(
                  text,
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: t.keyframe,
                    fontFeatures: const [FontFeature.tabularFigures()],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// TECLADO NUMERICO: digita o valor exato. Aceita conta ("1080/3") e
/// porcentagem ("50%", da faixa ou de 100 quando a unidade e %).
Future<double?> showNumberInput(
  BuildContext context, {
  required double value,
  String unit = '',
  double min = double.negativeInfinity,
  double max = double.infinity,
  int decimals = 1,
  String? title,
}) async {
  final ctrl = TextEditingController(
    text: value.toStringAsFixed(decimals).replaceAll(RegExp(r'\.?0+$'), ''),
  );
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: AppText(title ?? 'Valor exato${unit.isEmpty ? '' : ' ($unit)'}'),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('valor-campo'),
          controller: ctrl,
          autofocus: true,
          keyboardType: const TextInputType.numberWithOptions(
            decimal: true,
            signed: true,
          ),
          placeholder: translate(context, 'ex.: 120, 1080/3 ou 50%'),
          onSubmitted: (v) => Navigator.pop(ctx, v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  if (r == null) return null;
  final percentOf = unit == '%' ? 100.0 : (max.isFinite ? max : 100.0);
  final v = evalExpression(r.replaceAll(',', '.'), percentOf: percentOf);
  if (v == null || v.isNaN) return null;
  return v.clamp(min, max).toDouble();
}

/// EDITOR DE EXPRESSAO (Pro, Fase 5): um campo de texto, o erro do motor
/// quando ha, e Limpar. Devolve null ao cancelar; '' para tirar.
Future<String?> showExpressionEditor(
  BuildContext context, {
  String? atual,
  String? erro,
  String? nome,
}) async {
  final ctrl = TextEditingController(text: atual ?? '');
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: AppText('Expressão${nome == null ? '' : ' · $nome'}'),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            CupertinoTextField(
              key: const ValueKey('expressao-campo'),
              controller: ctrl,
              autofocus: true,
              maxLines: 3,
              minLines: 1,
              placeholder: translate(context, 'ex.: wiggle(2, 30) ou time * 90'),
              onSubmitted: (v) => Navigator.pop(ctx, v),
            ),
            if (erro != null) ...[
              const SizedBox(height: 8),
              AppText(
                erro,
                style: const TextStyle(fontSize: 12, color: Color(0xFFFF6B6B)),
              ),
            ],
          ],
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx, ''),
          child: const AppText('Limpar'),
        ),
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  return r;
}
