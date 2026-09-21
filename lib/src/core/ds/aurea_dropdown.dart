import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'aurea_menu.dart';
import 'tokens.dart';

/// ESCOLHA ENTRE OPCOES: a caixa mostra a escolhida e o toque abre o
/// [AureaMenu] junto dela, com a atual marcada.
///
/// Para poucas opcoes que cabem na linha, [AureaChip] lado a lado e mais
/// rapido; o dropdown e para lista longa (modo de mescla, fonte).
class AureaDropdown<T> extends StatelessWidget {
  const AureaDropdown({
    super.key,
    required this.valor,
    required this.opcoes,
    required this.rotuloDe,
    required this.aoMudar,
    this.traduzir = true,
    this.titulo,
    this.largura,
    this.habilitado = true,
  });

  final T valor;
  final List<T> opcoes;
  final String Function(T) rotuloDe;
  final ValueChanged<T> aoMudar;

  /// Falso quando as opcoes sao conteudo (nomes de fonte, de camada).
  final bool traduzir;
  final String? titulo;

  /// Nulo: ocupa a largura que tiver.
  final double? largura;
  final bool habilitado;

  Future<void> _abrir(BuildContext context) async {
    final escolhido = await mostrarAureaMenu<T>(
      context,
      titulo: titulo,
      itens: [
        for (final o in opcoes)
          AureaMenuItem<T>(
            valor: o,
            rotulo: rotuloDe(o),
            marcado: o == valor,
            traduzir: traduzir,
          ),
      ],
    );
    if (escolhido != null && escolhido != valor) aoMudar(escolhido);
  }

  @override
  Widget build(BuildContext context) {
    final rotulo = rotuloDe(valor);
    final estilo = AureaEstilos.corpo.copyWith(
      color: habilitado ? AureaCores.texto : AureaCores.textoSecundario,
    );
    return Tocavel(
      onTap: habilitado ? () => _abrir(context) : null,
      child: Container(
        width: largura,
        height: AureaDims.alturaDaCaixaDeValor,
        padding: const EdgeInsets.symmetric(horizontal: AureaDims.e10),
        decoration: BoxDecoration(
          color: AureaCores.campo,
          borderRadius: BorderRadius.circular(AureaDims.raioMd),
        ),
        child: Row(
          mainAxisSize: largura == null ? MainAxisSize.max : MainAxisSize.min,
          children: [
            Expanded(
              child: traduzir
                  ? AppText(
                      rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: estilo,
                    )
                  : Text(
                      rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: estilo,
                    ),
            ),
            Icon(
              CupertinoIcons.chevron_down,
              size: 12,
              color: AureaCores.textoSecundario,
            ),
          ],
        ),
      ),
    );
  }
}
