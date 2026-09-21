import 'package:flutter/widgets.dart';

import '../ui/am_tick_ruler.dart';
import '../ui/tocavel.dart';
import 'aurea_teclado_numerico.dart';
import 'tokens.dart';

/// O NUMERO EXATO: a caixa de 56 com o valor. O toque abre o teclado do
/// app ([showNumberInput] -> `TecladoNumerico`, o MESMO do editor antigo:
/// contas, "50%", "1:30"). Nao existe um segundo teclado.
///
/// Com [arrastavel], arrastar a propria caixa tambem muda o valor (direita
/// aumenta) — e o que a linha de ponto X/Y usa, ja que nela nao ha
/// deslizante.
class AureaValueField extends StatelessWidget {
  const AureaValueField({
    super.key,
    required this.valor,
    required this.aoMudar,
    this.casas = 1,
    this.unidade = '',
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.titulo,
    this.prefixo,
    this.largura = AureaDims.caixaDeValor,
    this.aoSegurar,
    this.arrastavel = false,
    this.sensibilidade = .5,
    this.aoComecarGesto,
    this.aoTerminarGesto,
    this.habilitado = true,
  });

  final double valor;
  final ValueChanged<double> aoMudar;
  final int casas;

  /// Sufixo mostrado e aceito pelo teclado ('%', '°', 'px').
  final String unidade;
  final double min;
  final double max;

  /// Titulo do teclado. Nulo: "Valor exato".
  final String? titulo;

  /// Letra antes do numero (o eixo, na linha de ponto).
  final String? prefixo;

  /// Largura da caixa. `double.infinity` para ocupar o que sobrar.
  final double largura;

  /// Toque longo (expressao, menu do campo).
  final VoidCallback? aoSegurar;

  final bool arrastavel;
  final double sensibilidade;
  final VoidCallback? aoComecarGesto;
  final VoidCallback? aoTerminarGesto;
  final bool habilitado;

  /// O texto da caixa: sem zeros sobrando, com a unidade.
  static String texto(double v, int casas, String unidade) {
    if (!v.isFinite) return '—';
    return '${formatarValorDigitado(v, casas)}$unidade';
  }

  Future<void> _abrirTeclado(BuildContext context) async {
    final v = await showNumberInput(
      context,
      value: valor,
      unit: unidade,
      min: min,
      max: max,
      decimals: casas,
      title: titulo,
    );
    if (v != null) aoMudar(v);
  }

  @override
  Widget build(BuildContext context) {
    final caixa = Container(
      width: largura,
      height: AureaDims.alturaDaCaixaDeValor,
      padding: const EdgeInsets.symmetric(horizontal: AureaDims.e4),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AureaCores.campo,
        borderRadius: BorderRadius.circular(AureaDims.raioMd),
      ),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (prefixo != null) ...[
              Text(prefixo!, style: AureaEstilos.rotulo),
              const SizedBox(width: AureaDims.e4),
            ],
            Text(
              texto(valor, casas, unidade),
              maxLines: 1,
              style: AureaEstilos.valor.copyWith(
                color: habilitado
                    ? AureaCores.texto
                    : AureaCores.textoSecundario,
              ),
            ),
          ],
        ),
      ),
    );
    final tocavel = Tocavel(
      onTap: habilitado ? () => _abrirTeclado(context) : null,
      onLongPress: habilitado ? aoSegurar : null,
      child: caixa,
    );
    if (!arrastavel || !habilitado) return tocavel;
    return AmArrastoDeValor(
      value: valor,
      min: min,
      max: max,
      unitsPerPixel: sensibilidade,
      onStart: aoComecarGesto,
      onEnd: aoTerminarGesto,
      onChanged: aoMudar,
      child: tocavel,
    );
  }
}
