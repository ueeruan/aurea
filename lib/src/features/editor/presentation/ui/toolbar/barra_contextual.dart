import 'package:flutter/widgets.dart';

import '../../../../../core/ds/ds.dart';
import '../shell/contrato.dart';

/// A BARRA CONTEXTUAL DA CAMADA: as [Ferramenta]s de `ferramentasDa`, uma
/// fileira que rola de lado (quase sempre ha mais ferramenta do que tela, e
/// cortar uma seria esconder uma funcao). A do painel aberto fica acesa.
///
/// Mora na base da area da timeline (57 de altura), no tom de painel —
/// separada da timeline pelo tom, sem linha.
///
/// Chaves: `barra-contextual` e `ferramenta-<id>`.
class BarraContextual extends StatelessWidget {
  const BarraContextual({
    super.key,
    required this.ferramentas,
    required this.aoTocar,
    this.ativo,
  });

  final List<Ferramenta> ferramentas;
  final ValueChanged<Ferramenta> aoTocar;
  final PainelId? ativo;

  @override
  Widget build(BuildContext context) {
    return Container(
      key: const ValueKey('barra-contextual'),
      height: AureaDims.barraDeFerramentas,
      color: AureaCores.painel,
      child: ListView.builder(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: AureaDims.e6),
        itemCount: ferramentas.length,
        itemBuilder: (context, i) {
          final f = ferramentas[i];
          return AureaToolbarButton(
            key: ValueKey('ferramenta-${f.id}'),
            icone: f.icone,
            rotulo: f.rotulo,
            ativo: f.abre != null && f.abre == ativo,
            aoTocar: f.abre == null && f.acao == null ? null : () => aoTocar(f),
          );
        },
      ),
    );
  }
}
