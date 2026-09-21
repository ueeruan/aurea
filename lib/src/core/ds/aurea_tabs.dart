import 'package:flutter/widgets.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// AS SUB-ABAS DE UM PAINEL (Posicao · Escala · Rotacao...).
///
/// Texto so, sem caixa: a aba ativa acende no destaque e ganha um traco de
/// 2 embaixo; as outras ficam no texto secundario. A fileira rola de lado
/// quando nao cabe — no app de referencia quase sempre ha mais aba do que
/// tela, e cortar uma seria esconder uma propriedade.
///
/// Chaves: `<chave>-<indice>`.
class AureaTabs extends StatelessWidget {
  const AureaTabs({
    super.key,
    required this.abas,
    required this.ativa,
    required this.aoTrocar,
    this.chave = 'aba',
  });

  /// Rotulos (texto de UI).
  final List<String> abas;
  final int ativa;
  final ValueChanged<int> aoTrocar;
  final String chave;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: AureaDims.abas,
      child: ListView.builder(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(
          horizontal: AureaDims.margemDoPainel - AureaDims.e10,
        ),
        itemCount: abas.length,
        itemBuilder: (context, i) {
          final ligada = i == ativa;
          return Tocavel(
            key: ValueKey('$chave-$i'),
            onTap: ligada ? null : () => aoTrocar(i),
            encolhe: 1,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: AureaDims.e10),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const SizedBox(height: 4),
                  AppText(
                    abas[i],
                    maxLines: 1,
                    style: TextStyle(
                      fontSize: AureaDims.textoDePropriedade,
                      fontWeight: ligada ? FontWeight.w600 : FontWeight.w500,
                      color: ligada
                          ? AureaCores.destaque
                          : AureaCores.textoSecundario,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Container(
                    width: 16,
                    height: 2,
                    decoration: BoxDecoration(
                      color: AureaCores.destaque.withValues(
                        alpha: ligada ? 1 : 0,
                      ),
                      borderRadius: BorderRadius.circular(1),
                    ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}
