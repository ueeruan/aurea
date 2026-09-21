import 'package:flutter/widgets.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// A PILULA: escolha curta, filtro, estado. Aceso = fundo do destaque
/// apagado e texto no destaque; apagado = tom de campo.
class AureaChip extends StatelessWidget {
  const AureaChip({
    super.key,
    required this.rotulo,
    this.aoTocar,
    this.ativo = false,
    this.icone,
    this.traduzir = true,
  });

  final String rotulo;
  final VoidCallback? aoTocar;
  final bool ativo;
  final IconData? icone;
  final bool traduzir;

  @override
  Widget build(BuildContext context) {
    final cor = ativo ? AureaCores.destaque : AureaCores.texto;
    final estilo = AureaEstilos.corpo.copyWith(fontSize: 12, color: cor);
    return Tocavel(
      onTap: aoTocar,
      child: Container(
        height: 28,
        padding: const EdgeInsets.symmetric(horizontal: AureaDims.e10 + 2),
        decoration: BoxDecoration(
          color: ativo ? AureaCores.destaqueApagado : AureaCores.campo,
          borderRadius: BorderRadius.circular(AureaDims.raioPilula),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icone != null) ...[
              Icon(icone, size: AureaDims.iconeSm, color: cor),
              const SizedBox(width: AureaDims.e4),
            ],
            traduzir
                ? AppText(rotulo, maxLines: 1, style: estilo)
                : Text(rotulo, maxLines: 1, style: estilo),
          ],
        ),
      ),
    );
  }
}
