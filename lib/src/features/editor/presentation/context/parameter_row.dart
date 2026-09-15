import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/tokens.dart';
import '../../../../core/ui/tocavel.dart';
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
    this.onExpose,
    this.rulerKey,
    this.valueKey,
    this.accentCenter = false,
    this.onExpression,
    this.expression,
    this.height = AureaTokens.minTap,
  });

  /// EXPOR NO PROJETO (Pro): o toque longo no nome oferece por a
  /// propriedade na lista de ⚙ Propriedades expostas — a lista que ate
  /// aqui so removia. Com [onReset] junto, o toque longo vira um menu
  /// com as duas acoes.
  final VoidCallback? onExpose;

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
        onExpose: onExpose,
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
    this.onExpose,
    this.height = AureaTokens.minTap,
    this.labelWidth = 76,
  });

  final String label;
  final Widget child;
  final KeyframeState? keyframe;
  final VoidCallback? onReset;
  final VoidCallback? onExpose;
  final double height;
  final double labelWidth;

  /// O TOQUE LONGO NO NOME: so resetar (como sempre) quando e a unica
  /// acao; com "expor" junto, um menu curto — resetar continua a um
  /// toque de distancia, e a lista de ⚙ deixa de ser so-remover.
  Future<void> _menuDoNome(BuildContext context) async {
    if (onExpose == null) {
      onReset?.call();
      return;
    }
    if (onReset == null) {
      onExpose!.call();
      return;
    }
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: AppText(label),
        actions: [
          CupertinoActionSheetAction(
            onPressed: () => Navigator.of(c).pop('reset'),
            child: const AppText('Resetar propriedade'),
          ),
          CupertinoActionSheetAction(
            key: const ValueKey('expor-propriedade'),
            onPressed: () => Navigator.of(c).pop('expor'),
            child: const AppText('Expor no projeto'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (acao == 'reset') onReset!.call();
    if (acao == 'expor') onExpose!.call();
  }

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
            message: onReset == null && onExpose == null
                ? label
                : '$label (toque longo: acoes)',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onLongPress: onReset == null && onExpose == null
                  ? null
                  : () => _menuDoNome(context),
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

/// TECLADO NUMERICO: digita o valor exato num teclado do proprio app —
/// digitos, apagar, sinal, as quatro contas, dois-pontos para tempo
/// ("1:30"), porcentagem ("50%", da faixa ou de 100 quando a unidade e %)
/// e "=" para ver a conta resolvida antes de confirmar.
Future<double?> showNumberInput(
  BuildContext context, {
  required double value,
  String unit = '',
  double min = double.negativeInfinity,
  double max = double.infinity,
  int decimals = 1,
  String? title,
}) async {
  final percentOf = unit == '%' ? 100.0 : (max.isFinite ? max : 100.0);
  final r = await showModalBottomSheet<String>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AmColors.panel,
    barrierColor: Colors.black38,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (ctx) => TecladoNumerico(
      titulo: title ?? 'Valor exato${unit.isEmpty ? '' : ' ($unit)'}',
      inicial: formatarValorDigitado(value, decimals),
      unidade: unit,
      decimais: decimals,
      min: min,
      max: max,
      percentOf: percentOf,
    ),
  );
  if (r == null) return null;
  final v = lerValorDigitado(r, percentOf: percentOf);
  if (v == null || v.isNaN) return null;
  return v.clamp(min, max).toDouble();
}

/// O numero sem zeros sobrando: 12.50 -> "12.5", 3.00 -> "3".
String formatarValorDigitado(double v, int decimals) =>
    v.toStringAsFixed(decimals.clamp(0, 6)).replaceAll(RegExp(r'\.?0+$'), '');

class TecladoNumerico extends StatefulWidget {
  const TecladoNumerico({
    super.key,
    required this.titulo,
    required this.inicial,
    this.unidade = '',
    this.decimais = 1,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.percentOf,
  });

  final String titulo;
  final String inicial;
  final String unidade;
  final int decimais;
  final double min;
  final double max;
  final double? percentOf;

  @override
  State<TecladoNumerico> createState() => _TecladoNumericoState();
}

class _TecladoNumericoState extends State<TecladoNumerico> {
  late final TextEditingController _campo = TextEditingController(
    text: widget.inicial,
  )..selection = TextSelection(baseOffset: 0, extentOffset: widget.inicial.length);

  @override
  void initState() {
    super.initState();
    _campo.addListener(_mudou);
  }

  @override
  void dispose() {
    _campo
      ..removeListener(_mudou)
      ..dispose();
    super.dispose();
  }

  void _mudou() => setState(() {});

  /// Escreve [s] no lugar da selecao (ou no cursor).
  void _escrever(String s) {
    final v = _campo.value;
    final sel = v.selection.isValid
        ? v.selection
        : TextSelection.collapsed(offset: v.text.length);
    final texto = v.text.replaceRange(sel.start, sel.end, s);
    _campo.value = TextEditingValue(
      text: texto,
      selection: TextSelection.collapsed(offset: sel.start + s.length),
    );
  }

  void _apagar() {
    final v = _campo.value;
    final sel = v.selection.isValid
        ? v.selection
        : TextSelection.collapsed(offset: v.text.length);
    if (!sel.isCollapsed) {
      _escrever('');
      return;
    }
    if (sel.start == 0) return;
    _campo.value = TextEditingValue(
      text: v.text.replaceRange(sel.start - 1, sel.start, ''),
      selection: TextSelection.collapsed(offset: sel.start - 1),
    );
  }

  void _trocarSinal() {
    final texto = _campo.text.trim();
    final novo = texto.startsWith('-') || texto.startsWith('−')
        ? texto.substring(1)
        : '-$texto';
    _campo.value = TextEditingValue(
      text: novo,
      selection: TextSelection.collapsed(offset: novo.length),
    );
  }

  double? get _resultado =>
      lerValorDigitado(_campo.text, percentOf: widget.percentOf);

  void _resolver() {
    final v = _resultado;
    if (v == null) return;
    final texto = formatarValorDigitado(v, widget.decimais);
    _campo.value = TextEditingValue(
      text: texto,
      selection: TextSelection.collapsed(offset: texto.length),
    );
  }

  /// A linha de baixo do visor: a conta resolvida, ou o aviso de que o
  /// valor sai da faixa e sera preso nela.
  String? get _dica {
    final v = _resultado;
    final texto = _campo.text.trim();
    if (texto.isEmpty) return null;
    if (v == null) return translate(context, 'Conta incompleta');
    final preso = v.clamp(widget.min, widget.max).toDouble();
    final numeroSimples = double.tryParse(texto.replaceAll(',', '.')) != null;
    if (preso != v) {
      return '${translate(context, 'Fica em')} '
          '${formatarValorDigitado(preso, widget.decimais)}${widget.unidade}';
    }
    if (numeroSimples) return null;
    return '= ${formatarValorDigitado(v, widget.decimais)}${widget.unidade}';
  }

  @override
  Widget build(BuildContext context) {
    const teclas = [
      ['7', '8', '9', '⌫'],
      ['4', '5', '6', '÷'],
      ['1', '2', '3', '×'],
      [',', '0', '±', '−'],
      [':', '%', '=', '+'],
    ];
    const nomes = {
      '⌫': 'apagar',
      '÷': 'dividir',
      '×': 'vezes',
      ',': 'virgula',
      '±': 'sinal',
      '−': 'menos',
      ':': 'dois-pontos',
      '%': 'porcento',
      '=': 'igual',
      '+': 'mais',
    };
    final dica = _dica;
    return SafeArea(
      child: Padding(
        padding: EdgeInsets.fromLTRB(
          16,
          14,
          16,
          12 + MediaQuery.viewInsetsOf(context).bottom,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            AppText(
              widget.titulo,
              style: const TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 10),
            CupertinoTextField(
              key: const ValueKey('valor-campo'),
              controller: _campo,
              autofocus: true,
              // O teclado e o de baixo: o do sistema nao sobe por cima.
              keyboardType: TextInputType.none,
              textAlign: TextAlign.right,
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
              style: const TextStyle(
                fontSize: 26,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
              suffix: widget.unidade.isEmpty
                  ? null
                  : Padding(
                      padding: const EdgeInsets.only(right: 14),
                      child: Text(
                        widget.unidade,
                        style: const TextStyle(
                          fontSize: 16,
                          color: AmColors.muted,
                        ),
                      ),
                    ),
              decoration: BoxDecoration(
                color: AmColors.bg,
                borderRadius: BorderRadius.circular(12),
              ),
              onSubmitted: (v) => Navigator.pop(context, v),
            ),
            SizedBox(
              height: 22,
              child: dica == null
                  ? null
                  : Align(
                      alignment: Alignment.centerRight,
                      child: Text(
                        dica,
                        key: const ValueKey('valor-dica'),
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.muted,
                        ),
                      ),
                    ),
            ),
            for (final linha in teclas)
              Padding(
                padding: const EdgeInsets.only(bottom: 6),
                child: Row(
                  children: [
                    for (final tecla in linha)
                      Expanded(
                        child: Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 3),
                          child: _Tecla(
                            key: ValueKey('tecla-${nomes[tecla] ?? tecla}'),
                            rotulo: tecla,
                            destaque: '÷×−+='.contains(tecla),
                            onTap: switch (tecla) {
                              '⌫' => _apagar,
                              '±' => _trocarSinal,
                              '=' => _resolver,
                              _ => () => _escrever(tecla),
                            },
                            onLongPress: tecla == '⌫'
                                ? () => _campo.clear()
                                : null,
                          ),
                        ),
                      ),
                  ],
                ),
              ),
            const SizedBox(height: 4),
            Row(
              children: [
                Expanded(
                  child: CupertinoButton(
                    color: AmColors.chip,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: () => Navigator.pop(context),
                    child: const AppText(
                      'Cancelar',
                      style: TextStyle(color: AmColors.text, fontSize: 15),
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: CupertinoButton(
                    color: AmColors.accent,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: _resultado == null
                        ? null
                        : () => Navigator.pop(context, _campo.text),
                    child: const AppText(
                      'OK',
                      style: TextStyle(
                        color: AmColors.onAction,
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _Tecla extends StatelessWidget {
  const _Tecla({
    super.key,
    required this.rotulo,
    required this.onTap,
    this.onLongPress,
    this.destaque = false,
  });

  final String rotulo;
  final VoidCallback onTap;
  final VoidCallback? onLongPress;
  final bool destaque;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: onTap,
    onLongPress: onLongPress,
    haptico: true,
    child: Container(
      height: 48,
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: destaque ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: rotulo == '⌫'
          ? const Icon(
              CupertinoIcons.delete_left,
              size: 22,
              color: AmColors.text,
            )
          : Text(
              rotulo,
              style: TextStyle(
                fontSize: 21,
                fontWeight: FontWeight.w600,
                color: destaque ? AmColors.accent : AmColors.text,
              ),
            ),
    ),
  );
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
