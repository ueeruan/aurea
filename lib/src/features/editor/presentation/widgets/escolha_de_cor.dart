import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import '../../../../core/ui/am_colors.dart';
import 'linha_de_parametro.dart';

/// A ESCOLHA DE UMA COR, na mesma lingua do resto do painel.
///
/// A ficha de efeito MOSTRAVA a cor e nao deixava troca-la: `Cor` era
/// uma linha de leitura, com os tres numeros e o quadradinho. Metade da
/// pergunta respondida — dava para ver o que estava valendo e nao para
/// mudar. `setEffectColor` e `setEffectExtraColor` existiam no motor
/// sem um unico chamador.
///
/// Nada de roda de cor nem de deslizante: os tres canais sao numeros de
/// 0 a 255, e numero neste app se ajusta com fita e campo
/// (`docs/painel-de-transformacao-alight.md`). Quem sabe o valor digita;
/// quem nao sabe arrasta e ve a amostra mudar. A fileira de cores
/// prontas em cima resolve o caso comum sem nenhum arrasto.
class EscolhaDeCor extends StatelessWidget {
  const EscolhaDeCor({
    super.key,
    required this.rotulo,
    required this.cor,
    required this.aoMudar,
    this.aoComecar,
    this.aoTerminar,
  });

  final String rotulo;
  final Color cor;
  final void Function(Color) aoMudar;
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  /// AS CORES PRONTAS.
  ///
  /// Doze, e escolhidas para caber em duas fileiras de seis num celular
  /// estreito. Comecam no branco e no preto porque brilho, sombra e
  /// contorno — os efeitos que mais pedem cor — quase sempre querem um
  /// dos dois.
  static const prontas = <Color>[
    Color(0xFFFFFFFF),
    Color(0xFF000000),
    Color(0xFFFF3B30),
    Color(0xFFFF9500),
    Color(0xFFFFD60A),
    AureaColors.accent,
    Color(0xFF34C759),
    Color(0xFF35C4E7),
    Color(0xFF0A84FF),
    AureaColors.selectionText,
    Color(0xFFFF2D95),
    Color(0xFF8E8E93),
  ];

  static String _nomeDoCanal(int i) =>
      switch (i) { 0 => 'Vermelho', 1 => 'Verde', _ => 'Azul' };

  @override
  Widget build(BuildContext context) {
    final r = (cor.r * 255).round();
    final g = (cor.g * 255).round();
    final b = (cor.b * 255).round();

    void trocar(int canal, double valor) {
      final v = valor.clamp(0, 255).round();
      aoMudar(
        Color.fromARGB(
          (cor.a * 255).round(),
          canal == 0 ? v : r,
          canal == 1 ? v : g,
          canal == 2 ? v : b,
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _Cabecalho(rotulo: rotulo, cor: cor, r: r, g: g, b: b),
        Padding(
          padding: const EdgeInsets.only(bottom: 4),
          child: Wrap(
            spacing: 7,
            runSpacing: 7,
            children: [
              for (final p in prontas)
                _Pronta(
                  cor: p,
                  // O DONO NO NOME. Um gradiente tem tres seletores na
                  // mesma ficha, e sem o prefixo quem ouve a tela
                  // escuta "Cor 30 214 177" tres vezes sem saber qual
                  // delas esta tocando. Com `rotulo` igual a 'Cor', o
                  // nome fica identico ao de antes.
                  dono: rotulo,
                  aceso: p.toARGB32() == cor.withValues(alpha: 1).toARGB32(),
                  aoTocar: () => aoMudar(p.withValues(alpha: cor.a)),
                ),
            ],
          ),
        ),
        for (var i = 0; i < 3; i++)
          LinhaDeParametro(
            rotulo: _nomeDoCanal(i),
            nome: '$rotulo ${_nomeDoCanal(i)}',
            valor: [r, g, b][i].toDouble(),
            casas: 0,
            // A FITA COBRE OS 255 NUMA PASSADA da largura do painel: um
            // canal de cor tem intervalo de verdade, ao contrario de
            // quase todo parametro daqui, e atravessar a fita uma vez
            // tem de ir de zero ao cheio.
            porPixel: 255 / 300,
            escolhida: true,
            aoComecar: aoComecar,
            aoMudar: (v) => trocar(i, v),
            aoTerminar: aoTerminar,
            aoDigitar: (v) => trocar(i, v),
          ),
      ],
    );
  }
}

class _Cabecalho extends StatelessWidget {
  const _Cabecalho({
    required this.rotulo,
    required this.cor,
    required this.r,
    required this.g,
    required this.b,
  });

  final String rotulo;
  final Color cor;
  final int r, g, b;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    label: rotulo,
    value: '$r $g $b',
    child: SizedBox(
      height: LinhaDeParametro.altura,
      child: Row(
        children: [
          SizedBox(
            width: 94,
            child: AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: AmColors.muted,
              ),
            ),
          ),
          const Spacer(),
          AppText(
            '$r $g $b',
            style: const TextStyle(
              fontSize: 12,
              color: AmColors.accent,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
          const SizedBox(width: 10),
          Container(
            width: 26,
            height: 26,
            decoration: BoxDecoration(
              color: cor,
              borderRadius: BorderRadius.circular(7),
              border: Border.all(color: AmColors.hairline),
            ),
          ),
        ],
      ),
    ),
  );
}

class _Pronta extends StatelessWidget {
  const _Pronta({
    required this.cor,
    required this.dono,
    required this.aceso,
    required this.aoTocar,
  });

  final Color cor;
  final String dono;
  final bool aceso;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    selected: aceso,
    label:
        '$dono '
        '${(cor.r * 255).round()} '
        '${(cor.g * 255).round()} '
        '${(cor.b * 255).round()}',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        width: 30,
        height: 30,
        decoration: BoxDecoration(
          color: cor,
          borderRadius: BorderRadius.circular(8),
          // A ESCOLHIDA GANHA UM ANEL, e nao uma marca por cima: um
          // tique branco some no branco e no amarelo, que sao duas das
          // cores mais pedidas.
          border: Border.all(
            color: aceso ? AmColors.accent : AmColors.hairline,
            width: aceso ? 2.5 : 1,
          ),
        ),
      ),
    ),
  );
}
