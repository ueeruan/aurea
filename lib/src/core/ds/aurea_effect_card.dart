import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// UM EFEITO NA PILHA DA CAMADA.
///
///   [› ] Nome do efeito ............ [olho] [≡] [⋯]     cabecalho 37
///   [ linhas de propriedade ]                          (aberto)
///
/// * seta / toque no nome: abre e recolhe;
/// * olho: liga e desliga SEM apagar;
/// * ≡: alca de reordenar — com [indiceNaLista], ela e o gatilho de
///   arrasto de uma `ReorderableListView` (sem precisar de toque longo);
/// * ⋯: o menu do efeito (duplicar, apagar...), de quem chama.
///
/// Fundo no tom elevado sobre o painel, cantos de 5, sem borda. Desligado,
/// o nome apaga — a pilha mostra de relance o que esta valendo.
///
/// Chaves: `<chave>-cabecalho`, `-seta`, `-olho`, `-alca`, `-menu`,
/// `-corpo`.
class AureaEffectCard extends StatefulWidget {
  const AureaEffectCard({
    super.key,
    required this.nome,
    required this.filhos,
    this.ligado = true,
    this.aoAlternarLigado,
    this.aoMenu,
    this.inicialmenteAberto = false,
    this.aberto,
    this.aoMudarAberto,
    this.indiceNaLista,
    this.chave = 'efeito',
    this.traduzirNome = true,
  });

  /// Nome do efeito (do catalogo: vai ao catalogo de traducao).
  final String nome;
  final bool traduzirNome;

  /// O corpo: `AureaPropertyRow`s.
  final List<Widget> filhos;

  final bool ligado;
  final VoidCallback? aoAlternarLigado;

  /// O ⋯ ([context] do botao, para o menu abrir junto dele).
  final void Function(BuildContext context)? aoMenu;

  /// Estado inicial quando o cartao controla a si mesmo.
  final bool inicialmenteAberto;

  /// CONTROLADO: com [aberto] nao nulo, quem manda e o pai (e ele recebe
  /// [aoMudarAberto]).
  final bool? aberto;
  final ValueChanged<bool>? aoMudarAberto;

  /// Posicao numa `ReorderableListView`: liga a alca de reordenar.
  final int? indiceNaLista;
  final String chave;

  @override
  State<AureaEffectCard> createState() => _AureaEffectCardState();
}

class _AureaEffectCardState extends State<AureaEffectCard> {
  late bool _abertoLocal = widget.inicialmenteAberto;

  bool get _aberto => widget.aberto ?? _abertoLocal;

  void _alternar() {
    final novo = !_aberto;
    if (widget.aberto == null) setState(() => _abertoLocal = novo);
    widget.aoMudarAberto?.call(novo);
  }

  @override
  Widget build(BuildContext context) {
    final k = widget.chave;
    final corNome = widget.ligado
        ? AureaCores.texto
        : AureaCores.textoSecundario.withValues(alpha: .6);
    Widget alca = SizedBox(
      key: ValueKey('$k-alca'),
      width: AureaDims.alcaDeReordenar,
      height: AureaDims.cabecalhoDoCartao,
      child: Icon(
        CupertinoIcons.line_horizontal_3,
        size: AureaDims.iconeSm,
        color: AureaCores.textoSecundario,
      ),
    );
    final indice = widget.indiceNaLista;
    if (indice != null) {
      alca = ReorderableDragStartListener(index: indice, child: alca);
    }
    final estiloNome = AureaEstilos.corpo.copyWith(
      color: corNome,
      fontWeight: FontWeight.w600,
    );
    return Container(
      margin: const EdgeInsets.only(bottom: AureaDims.vaoDoPainel),
      decoration: BoxDecoration(
        color: AureaCores.elevado,
        borderRadius: BorderRadius.circular(AureaDims.raioLg),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            key: ValueKey('$k-cabecalho'),
            height: AureaDims.cabecalhoDoCartao,
            child: Row(
              children: [
                Expanded(
                  child: Tocavel(
                    key: ValueKey('$k-seta'),
                    onTap: _alternar,
                    encolhe: 1,
                    child: Row(
                      children: [
                        SizedBox(
                          width: 32,
                          child: AnimatedRotation(
                            turns: _aberto ? .25 : 0,
                            duration: AureaMotion.rapido,
                            curve: AureaMotion.entrada,
                            child: Icon(
                              CupertinoIcons.chevron_right,
                              size: 13,
                              color: AureaCores.textoSecundario,
                            ),
                          ),
                        ),
                        Expanded(
                          child: widget.traduzirNome
                              ? AppText(
                                  widget.nome,
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                  style: estiloNome,
                                )
                              : Text(
                                  widget.nome,
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                  style: estiloNome,
                                ),
                        ),
                      ],
                    ),
                  ),
                ),
                if (widget.aoAlternarLigado != null)
                  Tocavel(
                    key: ValueKey('$k-olho'),
                    onTap: widget.aoAlternarLigado,
                    child: SizedBox(
                      width: 36,
                      height: AureaDims.cabecalhoDoCartao,
                      child: Icon(
                        widget.ligado
                            ? CupertinoIcons.eye
                            : CupertinoIcons.eye_slash,
                        size: AureaDims.iconeSm + 1,
                        color: widget.ligado
                            ? AureaCores.textoSecundario
                            : AureaCores.textoSecundario.withValues(alpha: .5),
                      ),
                    ),
                  ),
                alca,
                if (widget.aoMenu != null)
                  Builder(
                    builder: (botao) => Tocavel(
                      key: ValueKey('$k-menu'),
                      onTap: () => widget.aoMenu!(botao),
                      child: SizedBox(
                        width: 36,
                        height: AureaDims.cabecalhoDoCartao,
                        child: Icon(
                          CupertinoIcons.ellipsis,
                          size: AureaDims.iconeSm + 1,
                          color: AureaCores.textoSecundario,
                        ),
                      ),
                    ),
                  ),
                const SizedBox(width: AureaDims.e4),
              ],
            ),
          ),
          // Recolher e instantaneo de proposito: o cartao mede o corpo, e
          // animar a altura de uma lista de linhas dentro de uma lista que
          // reordena e reconstruir tudo a cada quadro por enfeite.
          if (_aberto)
            Padding(
              key: ValueKey('$k-corpo'),
              padding: const EdgeInsets.fromLTRB(
                AureaDims.e10,
                0,
                AureaDims.e4,
                AureaDims.e4,
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                mainAxisSize: MainAxisSize.min,
                children: widget.filhos,
              ),
            ),
        ],
      ),
    );
  }
}
