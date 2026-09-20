import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';

import '../../../../core/ui/am_colors.dart';

/// A FAIXA QUE APARECE NO LUGAR DOS CONTROLES DE UMA CAMADA BLOQUEADA.
///
/// Ela existe porque o cadeado e um estado invisivel: a camada continua
/// desenhada igual, a barra continua na timeline e o painel continua
/// abrindo. Sem a faixa, "nao acontece nada quando eu arrasto" e a unica
/// leitura possivel — e ela esta errada.
///
/// O botao fica DENTRO da faixa de proposito: quem descobriu que a camada
/// esta travada esta com o dedo nela, e nao deveria precisar procurar o
/// cadeado na timeline para continuar.
class FaixaDeBloqueio extends StatelessWidget {
  const FaixaDeBloqueio({
    super.key,
    required this.camadaId,
    required this.aoDesbloquear,
    this.compacta = false,
  });

  final String camadaId;
  final VoidCallback aoDesbloquear;

  /// Versao curta, para o palco (onde a faixa fica sobre a imagem).
  final bool compacta;

  @override
  Widget build(BuildContext context) {
    return Container(
      key: ValueKey('faixa-bloqueio-$camadaId'),
      margin: EdgeInsets.fromLTRB(8, compacta ? 0 : 6, 8, compacta ? 0 : 4),
      padding: EdgeInsets.symmetric(
        horizontal: 10,
        vertical: compacta ? 5 : 7,
      ),
      decoration: BoxDecoration(
        color: AmColors.panelHigh,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: AmColors.accent.withValues(alpha: 0.5)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(
            CupertinoIcons.lock_fill,
            size: 13,
            color: AmColors.accent,
          ),
          const SizedBox(width: 7),
          const Flexible(
            child: AppText(
              'Camada bloqueada',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: AmColors.text,
              ),
            ),
          ),
          const SizedBox(width: 8),
          GestureDetector(
            key: ValueKey('desbloquear-$camadaId'),
            behavior: HitTestBehavior.opaque,
            onTap: aoDesbloquear,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
              decoration: BoxDecoration(
                color: AmColors.accent,
                borderRadius: BorderRadius.circular(999),
              ),
              child: AppText(
                'Desbloquear',
                style: TextStyle(
                  fontSize: 11.5,
                  fontWeight: FontWeight.w700,
                  color: AmColors.onAction,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
