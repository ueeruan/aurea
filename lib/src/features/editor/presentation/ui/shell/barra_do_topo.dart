import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';

/// A BARRA DE CIMA (42): voltar, o nome do projeto (toque renomeia), o ⋯
/// do projeto, as configuracoes e Exportar.
///
/// So le o NOME do projeto: um passo de slider nao a reconstroi.
///
/// Chaves: `topo-voltar`, `topo-nome`, `topo-menu`, `topo-projeto`,
/// `topo-exportar`.
class BarraDoTopo extends ConsumerWidget {
  const BarraDoTopo({
    super.key,
    required this.aoVoltar,
    required this.aoRenomear,
    required this.aoMenu,
    required this.aoConfigurar,
    required this.aoExportar,
  });

  final VoidCallback aoVoltar;
  final VoidCallback aoRenomear;
  final VoidCallback aoMenu;
  final VoidCallback aoConfigurar;
  final VoidCallback aoExportar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final nome = ref.watch(editorControllerProvider.select((p) => p.name));
    Widget botao(
      String chave,
      IconData icone,
      String dica,
      VoidCallback acao, {
      bool destaque = false,
    }) => Semantics(
      label: translate(context, dica),
      button: true,
      child: Tocavel(
        key: ValueKey(chave),
        onTap: acao,
        child: SizedBox(
          width: AureaDims.botaoDeBarra,
          height: AureaDims.barraDoTopo,
          child: Icon(
            icone,
            size: AureaDims.iconeMd + 2,
            color: destaque ? AureaCores.destaque : AureaCores.texto,
          ),
        ),
      ),
    );
    return Container(
      key: const ValueKey('barra-do-topo'),
      height: AureaDims.barraDoTopo,
      color: AureaCores.cromo,
      child: Row(
        children: [
          const SizedBox(width: AureaDims.e2),
          botao(
            'topo-voltar',
            CupertinoIcons.chevron_left,
            'Voltar',
            aoVoltar,
          ),
          Expanded(
            child: Tocavel(
              key: const ValueKey('topo-nome'),
              onTap: aoRenomear,
              encolhe: 1,
              child: SizedBox(
                height: AureaDims.barraDoTopo,
                child: Align(
                  alignment: Alignment.centerLeft,
                  // O NOME e do usuario: Text, nao AppText.
                  child: Text(
                    nome,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.titulo,
                  ),
                ),
              ),
            ),
          ),
          botao(
            'topo-menu',
            CupertinoIcons.ellipsis,
            'Mais do projeto',
            aoMenu,
          ),
          botao(
            'topo-projeto',
            CupertinoIcons.gear_alt,
            'Configurações do projeto',
            aoConfigurar,
          ),
          botao(
            'topo-exportar',
            CupertinoIcons.square_arrow_up,
            'Exportar',
            aoExportar,
            destaque: true,
          ),
          const SizedBox(width: AureaDims.e2),
        ],
      ),
    );
  }
}
