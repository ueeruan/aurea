import 'package:flutter/material.dart';
import 'package:flutter/cupertino.dart';

import '../../../../core/ui/am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// AS PECAS DE FOLHA QUE SOBRARAM DO ESTUDIO 3D.
///
/// Elas nasceram dentro do estudio (`scene3d_studio_ux.dart`) e nao tem
/// nada de 3D: sao a casca de uma folha de ajustes, uma linha de ajuste e
/// um cabecalho de secao. Quem as usa de verdade e o ⚙ Projeto, que
/// ficaria sem tela nenhuma quando o estudio fosse apagado.
///
/// Ficam aqui, e nao no estudio: o estudio saiu inteiro (o motor foi
/// trocado), e o que era da casa veio junto.

/// A folha padrao do Estudio: painel escuro, canto redondo, sem borda.
Future<T?> folhaDoEstudio<T>(
  BuildContext context, {
  required Widget Function(BuildContext, StateSetter) builder,
  String? titulo,
  double alturaFator = 0.55,
}) {
  return showModalBottomSheet<T>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    barrierColor: Colors.black38,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setSheet) => SafeArea(
        top: false,
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxHeight: MediaQuery.of(ctx).size.height * alturaFator,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const SizedBox(height: 8),
              Center(
                child: Container(
                  width: 36,
                  height: 4,
                  decoration: BoxDecoration(
                    color: AmColors.muted.withValues(alpha: .5),
                    borderRadius: BorderRadius.circular(2),
                  ),
                ),
              ),
              if (titulo != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(18, 12, 18, 4),
                  child: AppText(titulo,
                    style: const TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
              Flexible(child: builder(ctx, setSheet)),
            ],
          ),
        ),
      ),
    ),
  );
}

/// Uma linha de menu: icone, titulo, subtitulo opcional, e ou um
/// interruptor, ou um chevron, ou nada.
class LinhaDoEstudio extends StatelessWidget {
  const LinhaDoEstudio({
    super.key,
    required this.titulo,
    this.icone,
    this.subtitulo,
    this.ligado,
    this.ativo = false,
    this.perigo = false,
    this.chevron = false,
    this.onTap,
    this.trailing,
  });

  final String titulo;
  final IconData? icone;
  final String? subtitulo;

  /// Com valor, a linha vira um interruptor.
  final bool? ligado;
  final bool ativo;
  final bool perigo;
  final bool chevron;
  final VoidCallback? onTap;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final cor = perigo
        ? AmColors.pink
        : ativo
        ? AmColors.accent
        : onTap == null && ligado == null
        ? AmColors.muted
        : AmColors.text;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 11),
        child: Row(
          children: [
            if (icone != null) ...[
              Icon(icone, size: 20, color: ativo ? AmColors.accent : cor),
              const SizedBox(width: 12),
            ],
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(titulo,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 15,
                      color: cor,
                      fontWeight: ativo ? FontWeight.w600 : FontWeight.w400,
                    ),
                  ),
                  if (subtitulo != null)
                    AppText(
                      subtitulo!,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 11.5,
                        height: 1.3,
                        color: AmColors.muted,
                      ),
                    ),
                ],
              ),
            ),
            ?trailing,
            if (ligado != null)
              CupertinoSwitch(
                value: ligado!,
                activeTrackColor: AmColors.accent,
                onChanged: onTap == null ? null : (_) => onTap!(),
              )
            else if (ativo)
              Icon(
                CupertinoIcons.checkmark_alt,
                size: 18,
                color: AmColors.accent,
              )
            else if (chevron)
              const Icon(
                CupertinoIcons.chevron_right,
                size: 15,
                color: AmColors.muted,
              ),
          ],
        ),
      ),
    );
  }
}

class SecaoDoEstudio extends StatelessWidget {
  const SecaoDoEstudio(this.titulo, {super.key});
  final String titulo;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(18, 14, 18, 4),
    child: AppText(
      titulo.toUpperCase(),
      style: const TextStyle(
        fontSize: 11,
        letterSpacing: .6,
        fontWeight: FontWeight.w600,
        color: AmColors.muted,
      ),
    ),
  );
}
