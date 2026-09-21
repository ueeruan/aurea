import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// UMA SECAO DO PAINEL: titulo pequeno e apagado, e o conteudo embaixo.
///
/// Recolhivel por padrao — o titulo inteiro e o alvo, com a seta dizendo
/// o estado. Serve para painel com muitas linhas (efeito com trinta
/// parametros) abrir no que importa e deixar o resto a um toque.
///
/// Chaves: `secao-<chave>` no titulo.
class AureaSection extends StatefulWidget {
  const AureaSection({
    super.key,
    required this.titulo,
    required this.filhos,
    this.recolhivel = true,
    this.inicialmenteAberta = true,
    this.chave,
  });

  final String titulo;
  final List<Widget> filhos;
  final bool recolhivel;
  final bool inicialmenteAberta;
  final String? chave;

  @override
  State<AureaSection> createState() => _AureaSectionState();
}

class _AureaSectionState extends State<AureaSection> {
  late bool _aberta = widget.inicialmenteAberta;

  @override
  Widget build(BuildContext context) {
    final aberta = _aberta || !widget.recolhivel;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        Tocavel(
          key: ValueKey('secao-${widget.chave ?? widget.titulo}'),
          onTap: widget.recolhivel
              ? () => setState(() => _aberta = !_aberta)
              : null,
          encolhe: 1,
          child: SizedBox(
            height: 30,
            child: Row(
              children: [
                Expanded(
                  // Traduz ANTES de caixa-alta: o catalogo tem a frase
                  // como foi escrita, e "EFEITOS" nao casaria com nada.
                  child: Text(
                    translate(context, widget.titulo).toUpperCase(),
                    maxLines: 1,
                    style: AureaEstilos.secao,
                  ),
                ),
                if (widget.recolhivel)
                  Icon(
                    aberta
                        ? CupertinoIcons.chevron_up
                        : CupertinoIcons.chevron_down,
                    size: 12,
                    color: AureaCores.textoSecundario,
                  ),
              ],
            ),
          ),
        ),
        if (aberta) ...widget.filhos,
      ],
    );
  }
}
