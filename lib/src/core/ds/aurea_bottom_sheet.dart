import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show showModalBottomSheet;

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// A FOLHA QUE SOBE POR CIMA do editor: cantos de 13,5, fundo de painel,
/// titulo opcional com fechar.
///
/// Entra em 100 ms (folha pequena) ou 200 ms ([grande]) desacelerando, e
/// sai em 100 ms acelerando — as duracoes de folha da referencia. Sem
/// sombra e sem borda.
class AureaBottomSheet extends StatelessWidget {
  const AureaBottomSheet({
    super.key,
    required this.child,
    this.titulo,
    this.acoes = const [],
    this.aoFechar,
    this.altura,
  });

  final Widget child;
  final String? titulo;
  final List<Widget> acoes;

  /// Nulo: fecha a rota da folha.
  final VoidCallback? aoFechar;

  /// Altura fixa do conteudo; nula = o que o conteudo pedir.
  final double? altura;

  @override
  Widget build(BuildContext context) {
    final corpo = altura == null
        ? child
        : SizedBox(height: altura, child: child);
    return SafeArea(
      top: false,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (titulo != null)
            SizedBox(
              height: AureaDims.cabecalhoDoPainel + AureaDims.e6,
              child: Row(
                children: [
                  const SizedBox(width: AureaDims.margemDoPainel),
                  Expanded(
                    child: AppText(
                      titulo!,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.titulo,
                    ),
                  ),
                  ...acoes,
                  Tocavel(
                    key: const ValueKey('folha-fechar'),
                    onTap: aoFechar ?? () => Navigator.of(context).maybePop(),
                    child: SizedBox(
                      width: AureaDims.toqueConfortavel,
                      height: AureaDims.cabecalhoDoPainel,
                      child: Icon(
                        CupertinoIcons.xmark,
                        size: AureaDims.iconeMd,
                        color: AureaCores.textoSecundario,
                      ),
                    ),
                  ),
                  const SizedBox(width: AureaDims.e6),
                ],
              ),
            )
          else
            const SizedBox(height: AureaDims.e10),
          Flexible(child: corpo),
        ],
      ),
    );
  }
}

/// ABRE UMA [AureaBottomSheet] com [construtor] dentro.
///
/// [modal] falso deixa o editor atras tocavel (sem veu): e a folha que
/// acompanha o trabalho. Arrastar a folha para baixo NAO a fecha — dentro
/// dela quase tudo se ajusta arrastando, e o arrasto que um controle nao
/// pegasse fecharia a folha no meio do ajuste (licao do editor antigo).
Future<T?> mostrarAureaFolha<T>(
  BuildContext context, {
  required WidgetBuilder construtor,
  String? titulo,
  List<Widget> acoes = const [],
  double? altura,
  bool grande = false,
  bool modal = true,
}) {
  return showModalBottomSheet<T>(
    context: context,
    isScrollControlled: true,
    enableDrag: false,
    useSafeArea: true,
    backgroundColor: AureaCores.painel,
    barrierColor: modal
        ? AureaCores.palco.withValues(alpha: .35)
        : AureaCores.palco.withValues(alpha: 0),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(
        top: Radius.circular(AureaDims.raioDaFolha),
      ),
    ),
    sheetAnimationStyle: AnimationStyle(
      duration: grande ? AureaMotion.normal : AureaMotion.rapido,
      reverseDuration: AureaMotion.rapido,
      curve: AureaMotion.entrada,
      reverseCurve: AureaMotion.saida,
    ),
    builder: (ctx) => AureaBottomSheet(
      titulo: titulo,
      acoes: acoes,
      altura: altura,
      child: Builder(builder: construtor),
    ),
  );
}
