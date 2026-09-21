import 'package:flutter/widgets.dart';

import '../l10n/app_language.dart';
import '../ui/tocavel.dart';
import 'tokens.dart';

/// O BOTAO DE BARRA E DE BLOCO: icone em cima, rotulo de 10 embaixo.
///
///  * padrao: icone 24, sem fundo — a barra contextual da camada;
///  * [bloco]: icone 32 sobre um bloco de 57 no tom de campo — a grade de
///    uma folha (Adicionar) ou de um painel.
///
/// [ativo] acende icone e rotulo no destaque; e o UNICO sinal de estado,
/// nada de caixa em volta.
class AureaToolbarButton extends StatelessWidget {
  const AureaToolbarButton({
    super.key,
    required this.icone,
    required this.rotulo,
    this.aoTocar,
    this.aoSegurar,
    this.ativo = false,
    this.bloco = false,
    this.largura,
  });

  final IconData icone;

  /// Rotulo (texto de UI).
  final String rotulo;
  final VoidCallback? aoTocar;
  final VoidCallback? aoSegurar;
  final bool ativo;
  final bool bloco;

  /// Nulo: 64 no padrao; o que o pai der no bloco.
  final double? largura;

  @override
  Widget build(BuildContext context) {
    final habilitado = aoTocar != null || aoSegurar != null;
    final cor = !habilitado
        ? AureaCores.textoSecundario.withValues(alpha: .45)
        : ativo
        ? AureaCores.destaque
        : AureaCores.texto;
    final conteudo = Column(
      mainAxisAlignment: MainAxisAlignment.center,
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(
          icone,
          size: bloco ? AureaDims.iconeXl : AureaDims.iconeLg,
          color: cor,
        ),
        const SizedBox(height: AureaDims.e4),
        // NA BARRA, NO MAXIMO DUAS LINHAS: 24 + 4 + 2 x 12 cabem nos 57. A
        // barra nao espreme um botao abaixo de 64, entao rotulo de uma
        // palavra cabe numa linha so. No bloco (icone 32) cabe uma.
        AppText(
          rotulo,
          maxLines: bloco ? 1 : 2,
          overflow: TextOverflow.ellipsis,
          textAlign: TextAlign.center,
          style: AureaEstilos.rotulo.copyWith(
            color: ativo ? AureaCores.destaque : AureaCores.textoSecundario,
          ),
        ),
      ],
    );
    return Tocavel(
      onTap: aoTocar,
      onLongPress: aoSegurar,
      child: bloco
          ? Container(
              width: largura,
              height: AureaDims.blocoDePainel,
              padding: const EdgeInsets.symmetric(horizontal: AureaDims.e4),
              decoration: BoxDecoration(
                color: ativo ? AureaCores.destaqueApagado : AureaCores.campo,
                borderRadius: BorderRadius.circular(AureaDims.raioLg),
              ),
              child: conteudo,
            )
          : SizedBox(
              width: largura ?? AureaDims.botaoDeFerramenta,
              height: AureaDims.barraDeFerramentas,
              child: conteudo,
            ),
    );
  }
}
