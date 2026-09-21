import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../features/editor/domain/expr.dart';
import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

// O TECLADO DO APP — o unico que digita um numero exato. A caixa de valor
// ([AureaValueField]), o seletor de cor e os paineis abrem este; nao existe
// um segundo teclado. Veio de `context/parameter_row.dart` (a linha de
// parametro antiga) quando a UI antiga foi apagada: o comportamento e o
// mesmo (contas, "50%", "1:30", "=" para ver a conta), a cor e a do DS.

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
    backgroundColor: AureaCores.painel,
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
///
/// SO APARA DEPOIS DA VIRGULA. A regra antiga (`\.?0+$` no texto inteiro)
/// tambem comia os zeros da parte INTEIRA quando nao havia casa decimal:
/// com `decimals` 0, 100 virava "1" e 120 virava "12" — o teclado abria
/// com o numero errado e a caixa de valor mostrava outro.
String formatarValorDigitado(double v, int decimals) {
  final texto = v.toStringAsFixed(decimals.clamp(0, 6));
  if (!texto.contains('.')) return texto == '-0' ? '0' : texto;
  final aparado = texto
      .replaceAll(RegExp(r'0+$'), '')
      .replaceAll(RegExp(r'\.$'), '');
  return aparado == '-0' ? '0' : aparado;
}

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
  late final TextEditingController _campo =
      TextEditingController(text: widget.inicial)
        ..selection = TextSelection(
          baseOffset: 0,
          extentOffset: widget.inicial.length,
        );

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
              style: TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
                color: AureaCores.texto,
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
              style: TextStyle(
                fontSize: 26,
                fontWeight: FontWeight.w700,
                color: AureaCores.texto,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
              suffix: widget.unidade.isEmpty
                  ? null
                  : Padding(
                      padding: const EdgeInsets.only(right: 14),
                      child: Text(
                        widget.unidade,
                        style: TextStyle(
                          fontSize: 16,
                          color: AureaCores.textoSecundario,
                        ),
                      ),
                    ),
              decoration: BoxDecoration(
                color: AureaCores.palco,
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
                        style: TextStyle(
                          fontSize: 12,
                          color: AureaCores.textoSecundario,
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
                    color: AureaCores.campo,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: () => Navigator.pop(context),
                    child: AppText(
                      'Cancelar',
                      style: TextStyle(color: AureaCores.texto, fontSize: 15),
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: CupertinoButton(
                    color: AureaCores.destaque,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    onPressed: _resultado == null
                        ? null
                        : () => Navigator.pop(context, _campo.text),
                    child: AppText(
                      'OK',
                      style: TextStyle(
                        color: AureaCores.sobreAcao,
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
        color: destaque ? AureaCores.destaqueApagado : AureaCores.campo,
        borderRadius: BorderRadius.circular(10),
      ),
      child: rotulo == '⌫'
          ? Icon(CupertinoIcons.delete_left, size: 22, color: AureaCores.texto)
          : Text(
              rotulo,
              style: TextStyle(
                fontSize: 21,
                fontWeight: FontWeight.w600,
                color: destaque ? AureaCores.destaque : AureaCores.texto,
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
              placeholder: translate(
                context,
                'ex.: wiggle(2, 30) ou time * 90',
              ),
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
