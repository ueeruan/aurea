import 'package:flutter/cupertino.dart';

import '../ui/tocavel.dart';
import 'tokens.dart';

/// UMA CAMADA NUMA LISTA (vincular, agrupar, escolher fonte de mascara):
///
///   [faixa 10][miniatura/icone 32] nome ................ [olho 44]
///
/// Item de lista de 37 — a mesma altura de qualquer lista de painel.
/// Selecionada = fundo no tom de selecao; nada de contorno.
///
/// O NOME E CONTEUDO do usuario: vai em `Text`, nunca em `AppText`.
class AureaLayerRow extends StatelessWidget {
  const AureaLayerRow({
    super.key,
    required this.nome,
    required this.icone,
    this.corDaFaixa,
    this.miniatura,
    this.selecionada = false,
    this.visivel = true,
    this.aoTocar,
    this.aoSegurar,
    this.aoAlternarVisivel,
    this.recuo = 0,
  });

  final String nome;
  final IconData icone;

  /// A cor do tipo da camada (a mesma da barra na timeline).
  final Color? corDaFaixa;

  /// Substitui o icone quando ha miniatura de verdade.
  final Widget? miniatura;
  final bool selecionada;
  final bool visivel;
  final VoidCallback? aoTocar;
  final VoidCallback? aoSegurar;

  /// Nulo = sem olho (lista de escolha, nao de controle).
  final VoidCallback? aoAlternarVisivel;

  /// Recuo de filho de grupo (em niveis de 12).
  final int recuo;

  @override
  Widget build(BuildContext context) {
    final corTexto = visivel
        ? AureaCores.texto
        : AureaCores.textoSecundario.withValues(alpha: .6);
    return Tocavel(
      onTap: aoTocar,
      onLongPress: aoSegurar,
      encolhe: 1,
      child: Container(
        height: AureaDims.itemDeLista,
        color: selecionada
            ? AureaCores.selecao
            : AureaCores.painel.withValues(alpha: 0),
        child: Row(
          children: [
            SizedBox(width: recuo * 12.0),
            Container(
              width: AureaDims.faixaDeCor / 2.5,
              height: AureaDims.itemDeLista - 12,
              decoration: BoxDecoration(
                color: corDaFaixa ?? AureaCores.campoAlto,
                borderRadius: BorderRadius.circular(AureaDims.raioXs),
              ),
            ),
            const SizedBox(width: AureaDims.e8),
            SizedBox(
              width: AureaDims.miniaturaDoCabecalho,
              height: AureaDims.itemDeLista - 8,
              child:
                  miniatura ??
                  Icon(icone, size: AureaDims.iconeMd, color: corTexto),
            ),
            const SizedBox(width: AureaDims.e8),
            Expanded(
              child: Text(
                nome,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.corpo.copyWith(color: corTexto),
              ),
            ),
            if (aoAlternarVisivel != null)
              Tocavel(
                onTap: aoAlternarVisivel,
                child: SizedBox(
                  width: AureaDims.olhoDoCabecalho,
                  height: AureaDims.itemDeLista,
                  child: Icon(
                    visivel ? CupertinoIcons.eye : CupertinoIcons.eye_slash,
                    size: AureaDims.iconeSm + 2,
                    color: visivel
                        ? AureaCores.textoSecundario
                        : AureaCores.textoSecundario.withValues(alpha: .5),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
