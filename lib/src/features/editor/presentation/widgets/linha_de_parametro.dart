import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';

import '../../../../core/ui/am_colors.dart';
import 'campo_de_valor.dart';
import 'fita_de_ajuste.dart';

/// UMA LINHA DE PARAMETRO: chip, fita e campo.
///
/// E a forma medida na referencia para TODO numero que nao seja posicao
/// nem angulo (`docs/painel-de-transformacao-alight.md`, "O painel de
/// efeitos"). As tres partes respondem a tres perguntas diferentes, e
/// por isso as tres existem:
///
///   - o CHIP diz o que e, e diz quando este e o parametro que o rail
///     esquerdo esta mirando;
///   - a FITA e onde o dedo mexe, e ela e relativa: funciona igual para
///     um parametro de 0 a 1 e para um de 0 a 4000. DIREITA AUMENTA,
///     esquerda diminui — como em todo controle de arrasto do app;
///   - o CAMPO diz o valor exato, e abre o teclado para quem quer
///     digitar em vez de arrastar.
///
/// O que ela substituiu foi um `Slider`. O deslizante parece o controle
/// obvio e e o errado aqui: ele desenha "onde no intervalo", e a maior
/// parte destes parametros nao tem intervalo — o limite da tabela e uma
/// borda de seguranca, nao uma escala.
class LinhaDeParametro extends StatelessWidget {
  const LinhaDeParametro({
    super.key,
    required this.rotulo,
    required this.valor,
    required this.porPixel,
    required this.aoMudar,
    this.nome,
    this.casas = 2,
    this.sufixo = '',
    this.escolhida = false,
    this.aoEscolher,
    this.aoComecar,
    this.aoTerminar,
    this.aoDigitar,
    this.min,
    this.max,
  });

  final String rotulo;

  /// O QUE A LEITURA DE TELA ANUNCIA, quando difere do texto desenhado.
  ///
  /// Tres linhas chamadas "Vermelho" na mesma ficha — uma por cor de um
  /// gradiente — sao distinguiveis pelo olho, que ve a amostra logo
  /// acima de cada grupo, e indistinguiveis para quem ouve. O chip
  /// continua curto; o nome ganha o dono.
  final String? nome;

  final double valor;

  /// Quanto o valor anda por pixel de dedo na fita.
  final double porPixel;

  final void Function(double) aoMudar;
  final int casas;
  final String sufixo;

  /// Este e o parametro que o rail esquerdo esta mirando?
  final bool escolhida;

  /// Nulo quando a linha nao participa da escolha — o caso de uma
  /// ferramenta com um parametro so, onde nao ha o que escolher.
  final VoidCallback? aoEscolher;

  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  /// Nulo deixa o campo so de leitura, e sem sublinhado.
  final void Function(double)? aoDigitar;

  /// A FAIXA DO PARAMETRO, quando a tabela tem uma. So desenha: com as
  /// duas finitas a fita mostra o trilho de posicao, que enche para a
  /// DIREITA ([FitaDeAjuste.min]). Prender o valor continua com quem
  /// recebe [aoMudar].
  final double? min;
  final double? max;

  static const altura = 48.0;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: altura,
    child: Row(
      children: [
        _Chip(
          rotulo: rotulo,
          nome: nome,
          escolhida: escolhida,
          aoTocar: aoEscolher,
        ),
        const SizedBox(width: 8),
        Expanded(
          child: FitaDeAjuste(
            rotulo: nome ?? rotulo,
            valor: valor,
            porPixel: porPixel,
            min: min,
            max: max,
            ativa: escolhida || aoEscolher == null,
            altura: altura - 8,
            aoComecar: () {
              aoEscolher?.call();
              aoComecar?.call();
            },
            aoMudar: aoMudar,
            aoTerminar: aoTerminar,
          ),
        ),
        const SizedBox(width: 8),
        CampoDeValor(
          // ROTULO VAZIO: o nome ja esta no chip a esquerda, e repetir
          // embaixo da caixa custaria 12 px de altura em cada linha. O
          // nome vai por [CampoDeValor.nome] porque o chip da esquerda
          // nao existe para quem ouve a tela.
          rotulo: '',
          nome: nome ?? rotulo,
          valor: valor,
          casas: casas,
          sufixo: sufixo,
          largura: 68,
          aoDigitar: aoDigitar,
        ),
      ],
    ),
  );
}

class _Chip extends StatelessWidget {
  const _Chip({
    required this.rotulo,
    required this.nome,
    required this.escolhida,
    required this.aoTocar,
  });

  final String rotulo;
  final String? nome;
  final bool escolhida;
  final VoidCallback? aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: aoTocar != null,
    selected: escolhida,
    label: nome ?? rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        width: 94,
        height: 32,
        alignment: Alignment.center,
        padding: const EdgeInsets.symmetric(horizontal: 6),
        decoration: BoxDecoration(
          color: escolhida ? AmColors.chip : null,
          borderRadius: BorderRadius.circular(8),
        ),
        // DUAS LINHAS antes das reticencias: "Entrada branco" e "Saturacao
        // ao colorir" nao cabem em 94 px, e cortar o nome de um
        // parametro de cor deixava dois chips iguais na mesma ficha.
        child: AppText(
          rotulo,
          maxLines: 2,
          textAlign: TextAlign.center,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
            fontSize: 12,
            height: 1.05,
            fontWeight: FontWeight.w600,
            // ESCOLHIDA FICA VERDE E SUBLINHADA, como na referencia: e o
            // mesmo par de sinais que o campo de valor usa para dizer
            // "este numero e o que esta em jogo".
            color: escolhida ? AmColors.accent : AmColors.muted,
            decoration: escolhida
                ? TextDecoration.underline
                : TextDecoration.none,
            decorationColor: AmColors.accent,
          ),
        ),
      ),
    ),
  );
}
