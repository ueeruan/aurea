import 'package:flutter/widgets.dart';

import '../../../../../core/ds/ds.dart';
import '../shell/contrato.dart';

/// A BARRA CONTEXTUAL: as [Ferramenta]s de `ferramentasDa` (camada), de
/// `ferramentasDoProjeto` (nada escolhido) ou de `ferramentasDoLote`
/// (varias escolhidas) — a mesma barra nos tres niveis, so muda a lista.
///
/// Mora na base da area da timeline (57 de altura), no tom de painel,
/// separada da timeline pelo tom, sem linha. A do painel aberto fica
/// acesa: o destaque e o UNICO sinal de estado, nada de caixa em volta.
///
/// QUANDO CABE, os botoes dividem a largura (uma camera tem cinco
/// ferramentas: espremidas a esquerda elas deixariam meia barra vazia e
/// o polegar teria de ir buscar). QUANDO NAO CABE, cada um fica com 64 e
/// a fileira rola de lado — cortar uma ferramenta seria esconder uma
/// funcao. NUNCA ESPREME abaixo de 64: a 50 o rotulo virava "Transfor…".
/// So o "Soltar" do lote nao rola: fica preso no fim.
///
/// Chaves: [chave] (padrao `barra-contextual`) e `ferramenta-<id>`.
class BarraContextual extends StatelessWidget {
  const BarraContextual({
    super.key,
    required this.ferramentas,
    required this.aoTocar,
    this.ativo,
    this.chave = 'barra-contextual',
    this.inicio,
    this.fim,
    this.recuoFinal = AureaDims.e6,
  });

  final List<Ferramenta> ferramentas;
  final ValueChanged<Ferramenta> aoTocar;
  final PainelId? ativo;
  final String chave;

  /// Fixo a esquerda, fora da rolagem (a contagem do lote).
  final Widget? inicio;

  /// Fixo a direita, fora da rolagem.
  final Widget? fim;

  /// O vao depois do ultimo botao. A barra do projeto guarda aqui o lugar
  /// do "+", que flutua por cima do canto direito dela.
  final double recuoFinal;

  /// A QUE NAO ROLA: o "Soltar" do lote fica preso no fim — e a saida do
  /// modo, e uma saida que some rolando e uma armadilha. O "Mais" da camada
  /// rola junto (o toque longo no clipe abre o mesmo menu), para nao roubar
  /// o lugar de uma ferramenta num aparelho de 360.
  static const _presas = {AcaoDoLote.soltar};

  /// ATE ONDE UM BOTAO ENCOLHE para a fileira caber sem rolar: os 64 do
  /// botao. Encolher mais cortava o rotulo ("Transfor…", "Borda e som…")
  /// — rolar e melhor que adivinhar.
  static const _larguraMinima = AureaDims.botaoDeFerramenta;

  @override
  Widget build(BuildContext context) {
    final rolam = [
      for (final f in ferramentas)
        if (!_presas.contains(f.id)) f,
    ];
    final presas = [
      for (final f in ferramentas)
        if (_presas.contains(f.id)) f,
    ];
    return Container(
      key: ValueKey(chave),
      height: AureaDims.barraDeFerramentas,
      color: AureaCores.painel,
      child: Row(
        children: [
          ?inicio,
          Expanded(
            child: LayoutBuilder(
              builder: (context, c) {
                final util =
                    c.maxWidth -
                    AureaDims.e6 -
                    (presas.isEmpty ? recuoFinal : 0);
                final cabe = rolam.length * _larguraMinima <= util;
                final largura = cabe && rolam.isNotEmpty
                    ? util / rolam.length
                    : AureaDims.botaoDeFerramenta;
                return ListView.builder(
                  scrollDirection: Axis.horizontal,
                  physics: cabe
                      ? const NeverScrollableScrollPhysics()
                      : const BouncingScrollPhysics(),
                  padding: EdgeInsets.only(
                    left: AureaDims.e6,
                    right: presas.isEmpty ? recuoFinal : 0,
                  ),
                  itemCount: rolam.length,
                  itemExtent: largura,
                  itemBuilder: (context, i) => _botao(rolam[i], largura),
                );
              },
            ),
          ),
          for (final f in presas) _botao(f, AureaDims.botaoDeFerramenta),
          if (presas.isNotEmpty) SizedBox(width: recuoFinal),
          ?fim,
        ],
      ),
    );
  }

  Widget _botao(Ferramenta f, double largura) => AureaToolbarButton(
    key: ValueKey('ferramenta-${f.id}'),
    icone: f.icone,
    rotulo: f.rotulo,
    largura: largura,
    ativo: f.abre != null && f.abre == ativo,
    aoTocar: f.abre == null && f.acao == null ? null : () => aoTocar(f),
  );
}
