import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'aurea_tabs.dart';
import 'tokens.dart';

/// O PAINEL — a unica casca de painel do editor novo.
///
///   [titulo ............ acoes  ✓]      cabecalho 38
///   [ aba · aba · aba ]                  (opcional) 38
///   [ corpo rolavel, margem 22, topo 15 ]
///
/// Fundo de superficie ([AureaCores.painel]) e NENHUMA borda: o painel se
/// separa da timeline pelo tom. Todo painel e `AureaPanel` com
/// `AureaPropertyRow` dentro — nenhuma area inventa casca propria, e por
/// isso a mesma pessoa acha o mesmo controle no mesmo lugar em todos.
///
/// Corpo: [filhos] (lista rolavel com os respiros do painel) OU [corpo]
/// (quem precisa de rolagem propria, como a pilha de efeitos que
/// reordena).
class AureaPanel extends StatelessWidget {
  const AureaPanel({
    super.key,
    required this.titulo,
    this.filhos = const [],
    this.corpo,
    this.acoes = const [],
    this.abas,
    this.abaAtiva = 0,
    this.aoTrocarAba,
    this.aoFechar,
    this.chave = 'painel',
  });

  /// Titulo (texto de UI).
  final String titulo;
  final List<Widget> filhos;
  final Widget? corpo;

  /// Botoes do cabecalho, a esquerda do fechar (icones 20).
  final List<Widget> acoes;

  /// Sub-abas; nulo = painel sem abas.
  final List<String>? abas;
  final int abaAtiva;
  final ValueChanged<int>? aoTrocarAba;

  /// O ✓ do cabecalho. Nulo = sem botao de fechar.
  final VoidCallback? aoFechar;

  /// Base das chaves: `<chave>` no painel, `<chave>-fechar`,
  /// `<chave>-corpo`, `<chave>-aba-<i>`.
  final String chave;

  @override
  Widget build(BuildContext context) {
    final abas = this.abas;
    return ColoredBox(
      key: ValueKey(chave),
      color: AureaCores.painel,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: AureaDims.cabecalhoDoPainel,
            child: Row(
              children: [
                const SizedBox(width: AureaDims.margemDoPainel),
                Expanded(
                  child: AppText(
                    titulo,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.titulo,
                  ),
                ),
                ...acoes,
                if (aoFechar != null)
                  Tocavel(
                    key: ValueKey('$chave-fechar'),
                    onTap: aoFechar,
                    child: SizedBox(
                      width: AureaDims.toqueConfortavel,
                      height: AureaDims.cabecalhoDoPainel,
                      child: Icon(
                        CupertinoIcons.checkmark_alt,
                        size: AureaDims.iconeMd,
                        color: AureaCores.destaque,
                      ),
                    ),
                  ),
                const SizedBox(width: AureaDims.e6),
              ],
            ),
          ),
          if (abas != null && abas.isNotEmpty)
            AureaTabs(
              abas: abas,
              ativa: abaAtiva.clamp(0, abas.length - 1),
              aoTrocar: aoTrocarAba ?? (_) {},
              chave: '$chave-aba',
            ),
          Expanded(
            child: KeyedSubtree(
              key: ValueKey('$chave-corpo'),
              child:
                  corpo ??
                  ListView(
                    padding: const EdgeInsets.fromLTRB(
                      AureaDims.margemDoPainel,
                      AureaDims.e4,
                      AureaDims.margemDoPainel,
                      AureaDims.topoDoPainel,
                    ),
                    children: filhos,
                  ),
            ),
          ),
        ],
      ),
    );
  }
}

/// O AVISO CURTO de um painel que ainda nao tem o controle: texto e,
/// quando existe, a porta para o editor que ja faz aquilo. Nunca um
/// painel vazio — um vazio parece quebrado.
class AureaAvisoDoPainel extends StatelessWidget {
  const AureaAvisoDoPainel({super.key, required this.texto});

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: AureaDims.e10),
    child: AppText(texto, style: AureaEstilos.propriedade),
  );
}
