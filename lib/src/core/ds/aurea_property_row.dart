import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import '../ui/am_tick_ruler.dart';
import '../ui/tocavel.dart';
import 'aurea_keyframe_button.dart';
import 'aurea_slider.dart';
import 'aurea_value_field.dart';
import 'tokens.dart';

enum _TipoDaLinha { numero, ponto, cor, personalizada }

/// A LINHA DE PROPRIEDADE — a peca de todo painel da UI nova.
///
///   [rotulo 75] [—— deslizante ——] [valor 56] [‹ ◆ ›]      altura 51
///
/// Variantes: [AureaPropertyRow.ponto] (X/Y, sem deslizante: arrastar a
/// caixa do eixo muda o eixo), [AureaPropertyRow.cor] (amostra + codigo,
/// toque abre o seletor) e [AureaPropertyRow.personalizada] (rotulo +
/// qualquer controle: escolha, interruptor).
///
/// NA LINHA NUMERICA A LINHA INTEIRA E A SUPERFICIE DE ARRASTO. Foi a
/// licao do beta: num aparelho de 375 o deslizante sobrava com vinte e
/// poucos pixels e "nao dava pra mexer". O rotulo e os vaos puxam junto;
/// a caixa e o losango continuam recebendo o TOQUE (toque e arrasto sao
/// gestos diferentes na arena). Com faixa, a sensibilidade e a do
/// deslizante — a alca fica debaixo do dedo.
///
/// Toque longo no rotulo: [aoResetar].
class AureaPropertyRow extends StatelessWidget {
  const AureaPropertyRow({
    super.key,
    required this.rotulo,
    required double this.valor,
    required ValueChanged<double> this.aoMudar,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.sensibilidade,
    this.casas = 1,
    this.unidade = '',
    this.keyframe,
    this.aoAnterior,
    this.aoProximo,
    this.aoResetar,
    this.aoComecarGesto,
    this.aoTerminarGesto,
    this.aoSegurarValor,
    this.chave,
    this.habilitado = true,
  }) : _tipo = _TipoDaLinha.numero,
       x = null,
       y = null,
       aoMudarX = null,
       aoMudarY = null,
       cor = null,
       aoTocar = null,
       filho = null;

  /// X e Y numa linha: duas caixas arrastaveis.
  const AureaPropertyRow.ponto({
    super.key,
    required this.rotulo,
    required double this.x,
    required double this.y,
    required ValueChanged<double> this.aoMudarX,
    required ValueChanged<double> this.aoMudarY,
    this.sensibilidade,
    this.casas = 1,
    this.unidade = '',
    this.keyframe,
    this.aoAnterior,
    this.aoProximo,
    this.aoResetar,
    this.aoComecarGesto,
    this.aoTerminarGesto,
    this.chave,
    this.habilitado = true,
  }) : _tipo = _TipoDaLinha.ponto,
       valor = null,
       aoMudar = null,
       min = double.negativeInfinity,
       max = double.infinity,
       aoSegurarValor = null,
       cor = null,
       aoTocar = null,
       filho = null;

  /// Amostra de cor; o toque abre o seletor de quem chama.
  const AureaPropertyRow.cor({
    super.key,
    required this.rotulo,
    required Color this.cor,
    required VoidCallback this.aoTocar,
    this.keyframe,
    this.aoAnterior,
    this.aoProximo,
    this.aoResetar,
    this.chave,
    this.habilitado = true,
  }) : _tipo = _TipoDaLinha.cor,
       valor = null,
       aoMudar = null,
       x = null,
       y = null,
       aoMudarX = null,
       aoMudarY = null,
       min = double.negativeInfinity,
       max = double.infinity,
       sensibilidade = null,
       casas = 0,
       unidade = '',
       aoComecarGesto = null,
       aoTerminarGesto = null,
       aoSegurarValor = null,
       filho = null;

  /// Rotulo + qualquer controle (escolha, interruptor, botoes).
  const AureaPropertyRow.personalizada({
    super.key,
    required this.rotulo,
    required Widget this.filho,
    this.keyframe,
    this.aoAnterior,
    this.aoProximo,
    this.aoResetar,
    this.chave,
    this.habilitado = true,
  }) : _tipo = _TipoDaLinha.personalizada,
       valor = null,
       aoMudar = null,
       x = null,
       y = null,
       aoMudarX = null,
       aoMudarY = null,
       min = double.negativeInfinity,
       max = double.infinity,
       sensibilidade = null,
       casas = 0,
       unidade = '',
       aoComecarGesto = null,
       aoTerminarGesto = null,
       aoSegurarValor = null,
       cor = null,
       aoTocar = null;

  final _TipoDaLinha _tipo;

  /// Nome da propriedade (texto de UI: vai ao catalogo de traducao).
  final String rotulo;

  final double? valor;
  final ValueChanged<double>? aoMudar;
  final double? x;
  final double? y;
  final ValueChanged<double>? aoMudarX;
  final ValueChanged<double>? aoMudarY;
  final Color? cor;
  final VoidCallback? aoTocar;
  final Widget? filho;

  final double min;
  final double max;

  /// Unidades por pixel. Nulo: a do deslizante (faixa na largura; 0,5).
  final double? sensibilidade;
  final int casas;
  final String unidade;

  /// O losango. Nulo = propriedade que nao anima (sem coluna de losango).
  final KeyframeState? keyframe;
  final VoidCallback? aoAnterior;
  final VoidCallback? aoProximo;
  final VoidCallback? aoResetar;

  /// Um arrasto = um passo de desfazer (`beginGesture`/`endGesture`).
  final VoidCallback? aoComecarGesto;
  final VoidCallback? aoTerminarGesto;

  /// Toque longo na caixa de valor (expressao, animar sozinho).
  final VoidCallback? aoSegurarValor;

  /// Base das chaves de teste. Nulo: o rotulo sem acento nem espaco.
  final String? chave;
  final bool habilitado;

  bool get _temFaixa => min.isFinite && max.isFinite && max > min;

  /// A chave estavel da linha: `prop-<chave>` na linha, `kf-<chave>` no
  /// losango, `valor-<chave>` na caixa.
  String get chaveBase => chave ?? slugDoRotulo(rotulo);

  static String slugDoRotulo(String s) {
    const acentos = {
      'á': 'a', 'à': 'a', 'â': 'a', 'ã': 'a', 'é': 'e', 'ê': 'e', //
      'í': 'i', 'ó': 'o', 'ô': 'o', 'õ': 'o', 'ú': 'u', 'ç': 'c',
    };
    final minusculo = s.toLowerCase().split('').map((c) => acentos[c] ?? c);
    return minusculo
        .join()
        .replaceAll(RegExp(r'[^a-z0-9]+'), '-')
        .replaceAll(RegExp(r'^-|-$'), '');
  }

  Widget _rotulo(BuildContext context) => Tocavel(
    onLongPress: habilitado ? aoResetar : null,
    encolhe: 1,
    child: SizedBox(
      width: AureaDims.rotuloDaPropriedade,
      height: AureaDims.linhaDePropriedade,
      child: Align(
        alignment: Alignment.centerLeft,
        child: AppText(
          rotulo,
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
          style: AureaEstilos.propriedade,
        ),
      ),
    ),
  );

  Widget? _losango() {
    final kf = keyframe;
    if (kf == null) return null;
    return Padding(
      padding: const EdgeInsets.only(left: AureaDims.e2),
      child: AureaKeyframeButton(
        estado: kf,
        aoAnterior: aoAnterior,
        aoProximo: aoProximo,
        chave: 'kf-$chaveBase',
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final losango = _losango();
    final Widget linha = switch (_tipo) {
      _TipoDaLinha.numero => _numero(context, losango),
      _TipoDaLinha.ponto => _ponto(context, losango),
      _TipoDaLinha.cor => _linhaSimples(context, _amostra(), losango),
      _TipoDaLinha.personalizada => _linhaSimples(context, filho!, losango),
    };
    return SizedBox(
      key: ValueKey('prop-$chaveBase'),
      height: AureaDims.linhaDePropriedade,
      child: linha,
    );
  }

  Widget _numero(BuildContext context, Widget? losango) {
    final v = valor!;
    final mudar = aoMudar!;
    return LayoutBuilder(
      builder: (context, c) {
        final largura = c.maxWidth.isFinite ? c.maxWidth : 320.0;
        final larguraDoDeslizante =
            largura -
            AureaDims.rotuloDaPropriedade -
            AureaDims.vaoDoPainel -
            AureaDims.caixaDeValor -
            (losango == null ? 0 : AureaDims.botaoDeKeyframe + AureaDims.e2);
        final porPixel = AureaSlider.sensibilidadePara(
          min: min,
          max: max,
          largura: larguraDoDeslizante,
          pedida: sensibilidade,
        );
        final conteudo = Row(
          children: [
            _rotulo(context),
            Expanded(
              child: AureaSlider(
                key: ValueKey('deslizante-$chaveBase'),
                valor: v,
                aoMudar: mudar,
                min: min,
                max: max,
                sensibilidade: porPixel,
                arrastavel: false,
                habilitado: habilitado,
              ),
            ),
            const SizedBox(width: AureaDims.vaoDoPainel),
            AureaValueField(
              key: ValueKey('valor-$chaveBase'),
              valor: v,
              aoMudar: mudar,
              casas: casas,
              unidade: unidade,
              min: _temFaixa ? min : double.negativeInfinity,
              max: _temFaixa ? max : double.infinity,
              titulo: translate(context, rotulo),
              aoSegurar: aoSegurarValor,
              habilitado: habilitado,
            ),
            ?losango,
          ],
        );
        if (!habilitado) return conteudo;
        return AmArrastoDeValor(
          value: v,
          min: min,
          max: max,
          unitsPerPixel: porPixel,
          onStart: aoComecarGesto,
          onEnd: aoTerminarGesto,
          onChanged: mudar,
          child: conteudo,
        );
      },
    );
  }

  Widget _ponto(BuildContext context, Widget? losango) {
    Widget eixo(String nome, double v, ValueChanged<double> mudar) => Expanded(
      child: AureaValueField(
        key: ValueKey('valor-$chaveBase-${nome.toLowerCase()}'),
        valor: v,
        aoMudar: mudar,
        casas: casas,
        unidade: unidade,
        prefixo: nome,
        largura: double.infinity,
        titulo: '${translate(context, rotulo)} $nome',
        arrastavel: true,
        sensibilidade: sensibilidade ?? .5,
        aoComecarGesto: aoComecarGesto,
        aoTerminarGesto: aoTerminarGesto,
        habilitado: habilitado,
      ),
    );
    return Row(
      children: [
        _rotulo(context),
        eixo('X', x!, aoMudarX!),
        const SizedBox(width: AureaDims.vaoDoPainel),
        eixo('Y', y!, aoMudarY!),
        ?losango,
      ],
    );
  }

  Widget _amostra() {
    final c = cor!;
    final hex = c
        .toARGB32()
        .toRadixString(16)
        .padLeft(8, '0')
        .substring(2)
        .toUpperCase();
    return Tocavel(
      key: ValueKey('cor-$chaveBase'),
      onTap: habilitado ? aoTocar : null,
      child: SizedBox(
        height: AureaDims.linhaDePropriedade,
        child: Row(
          children: [
            Container(
              width: 34,
              height: 22,
              decoration: BoxDecoration(
                color: c,
                borderRadius: BorderRadius.circular(AureaDims.raioMd),
              ),
            ),
            const SizedBox(width: AureaDims.e8),
            Expanded(
              // O CODIGO DA COR e conteudo, nao rotulo: Text, nao AppText.
              child: Text('#$hex', style: AureaEstilos.valor),
            ),
            Icon(
              CupertinoIcons.chevron_right,
              size: 13,
              color: AureaCores.textoSecundario,
            ),
          ],
        ),
      ),
    );
  }

  Widget _linhaSimples(BuildContext context, Widget controle, Widget? losango) =>
      Row(
        children: [
          _rotulo(context),
          Expanded(
            child: Align(alignment: Alignment.centerLeft, child: controle),
          ),
          ?losango,
        ],
      );
}
