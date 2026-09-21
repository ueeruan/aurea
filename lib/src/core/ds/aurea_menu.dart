import 'dart:math' as math;

import 'package:flutter/cupertino.dart';

import '../l10n/app_language.dart';
import 'tokens.dart';

/// UM ITEM DE MENU: rotulo, icone opcional e o valor que ele devolve.
class AureaMenuItem<T> {
  const AureaMenuItem({
    required this.valor,
    required this.rotulo,
    this.icone,
    this.marcado = false,
    this.destrutivo = false,
    this.habilitado = true,
    this.traduzir = true,
    this.chave,
  });

  final T valor;

  /// Texto do item. [traduzir] falso para conteudo (nome de fonte, de
  /// camada), que nao vai ao catalogo.
  final String rotulo;
  final IconData? icone;

  /// O item em vigor: barra de marca de 4 a esquerda, texto no destaque.
  final bool marcado;
  final bool destrutivo;
  final bool habilitado;
  final bool traduzir;

  /// Chave de teste: `menu-<chave>` (padrao: o indice).
  final String? chave;
}

/// A LISTA DO MENU — 250 de largura, itens de 40, sem borda.
///
/// Publica para quem quiser o menu embutido (uma folha, um painel); o
/// caminho comum e [mostrarAureaMenu], que a poe flutuando junto do
/// botao que a abriu.
class AureaMenu<T> extends StatelessWidget {
  const AureaMenu({
    super.key,
    required this.itens,
    required this.aoEscolher,
    this.titulo,
    this.alturaMaxima = AureaDims.alturaMaximaDoMenu,
  });

  final List<AureaMenuItem<T>> itens;
  final ValueChanged<T> aoEscolher;
  final String? titulo;
  final double alturaMaxima;

  @override
  Widget build(BuildContext context) {
    // A ROTA DO MENU NAO TEM `Material` por cima: sem um estilo de texto
    // proprio, o texto herdaria o estilo de erro (sublinhado amarelo).
    return DefaultTextStyle(
      style: AureaEstilos.corpo,
      child: _lista(),
    );
  }

  Widget _lista() {
    return ConstrainedBox(
      constraints: BoxConstraints(
        maxWidth: AureaDims.larguraDoMenu,
        minWidth: AureaDims.larguraDoMenu,
        maxHeight: alturaMaxima,
      ),
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: AureaCores.elevado,
          borderRadius: BorderRadius.circular(AureaDims.raioXl),
        ),
        child: ClipRRect(
          borderRadius: BorderRadius.circular(AureaDims.raioXl),
          child: ListView(
            shrinkWrap: true,
            padding: const EdgeInsets.symmetric(vertical: AureaDims.e4),
            children: [
              if (titulo != null)
                Padding(
                  padding: const EdgeInsets.fromLTRB(
                    AureaDims.e15,
                    AureaDims.e6,
                    AureaDims.e15,
                    AureaDims.e4,
                  ),
                  child: AppText(titulo!, style: AureaEstilos.secao),
                ),
              for (var i = 0; i < itens.length; i++)
                _ItemDoMenu<T>(
                  key: ValueKey('menu-${itens[i].chave ?? i}'),
                  item: itens[i],
                  aoEscolher: aoEscolher,
                ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ItemDoMenu<T> extends StatefulWidget {
  const _ItemDoMenu({super.key, required this.item, required this.aoEscolher});

  final AureaMenuItem<T> item;
  final ValueChanged<T> aoEscolher;

  @override
  State<_ItemDoMenu<T>> createState() => _ItemDoMenuState<T>();
}

class _ItemDoMenuState<T> extends State<_ItemDoMenu<T>> {
  bool _apertado = false;

  @override
  Widget build(BuildContext context) {
    final item = widget.item;
    final cor = !item.habilitado
        ? AureaCores.textoSecundario.withValues(alpha: .5)
        : item.destrutivo
        ? AureaCores.perigo
        : item.marcado
        ? AureaCores.destaque
        : AureaCores.texto;
    final estilo = AureaEstilos.corpo.copyWith(color: cor);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTapDown: item.habilitado ? (_) => setState(() => _apertado = true) : null,
      onTapCancel: () => setState(() => _apertado = false),
      onTapUp: (_) => setState(() => _apertado = false),
      onTap: item.habilitado ? () => widget.aoEscolher(item.valor) : null,
      child: Container(
        height: AureaDims.itemDeMenu,
        color: _apertado
            ? AureaCores.campoAlto
            : AureaCores.elevado.withValues(alpha: 0),
        child: Row(
          children: [
            Container(
              width: AureaDims.barraDeMarcaDoMenu,
              height: AureaDims.itemDeMenu - 16,
              color: AureaCores.destaque.withValues(
                alpha: item.marcado ? 1 : 0,
              ),
            ),
            const SizedBox(width: AureaDims.e10),
            if (item.icone != null) ...[
              Icon(item.icone, size: AureaDims.iconeMd, color: cor),
              const SizedBox(width: AureaDims.e10),
            ],
            Expanded(
              child: item.traduzir
                  ? AppText(
                      item.rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: estilo,
                    )
                  : Text(
                      item.rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: estilo,
                    ),
            ),
            const SizedBox(width: AureaDims.e10),
          ],
        ),
      ),
    );
  }
}

/// ABRE O MENU junto de quem chamou ([context] e o botao) e devolve o
/// valor escolhido, ou nulo.
///
/// Abaixo do botao quando cabe, acima quando nao; sempre dentro da tela
/// com 8 de folga. Entra em 100 ms desacelerando, sai acelerando — o
/// tempo de submenu da referencia. Sem sombra: o menu se separa pelo tom
/// ([AureaCores.elevado]) e por um veu leve atras.
Future<T?> mostrarAureaMenu<T>(
  BuildContext context, {
  required List<AureaMenuItem<T>> itens,
  String? titulo,
  Rect? ancora,
}) {
  final caixa = context.findRenderObject();
  final rect =
      ancora ??
      (caixa is RenderBox && caixa.hasSize
          ? caixa.localToGlobal(Offset.zero) & caixa.size
          : Rect.zero);
  return showGeneralDialog<T>(
    context: context,
    barrierDismissible: true,
    barrierLabel: 'menu',
    barrierColor: AureaCores.palco.withValues(alpha: .25),
    transitionDuration: AureaMotion.rapido,
    pageBuilder: (ctx, _, _) {
      final tela = MediaQuery.sizeOf(ctx);
      final alturaMax = math.min(
        AureaDims.alturaMaximaDoMenu,
        tela.height - 16,
      );
      final estimada = math.min(
        alturaMax,
        itens.length * AureaDims.itemDeMenu + (titulo == null ? 8 : 38),
      );
      final cabeAbaixo = rect.bottom + estimada + 8 <= tela.height;
      final topo = cabeAbaixo
          ? rect.bottom + 4
          : math.max(8.0, rect.top - estimada - 4);
      final esquerda = (rect.right - AureaDims.larguraDoMenu)
          .clamp(8.0, math.max(8.0, tela.width - AureaDims.larguraDoMenu - 8))
          .toDouble();
      return Stack(
        children: [
          Positioned(
            left: esquerda,
            top: topo,
            child: AureaMenu<T>(
              itens: itens,
              titulo: titulo,
              alturaMaxima: alturaMax,
              aoEscolher: (v) => Navigator.of(ctx).pop(v),
            ),
          ),
        ],
      );
    },
    transitionBuilder: (ctx, anim, _, filho) {
      final curva = CurvedAnimation(
        parent: anim,
        curve: AureaMotion.entrada,
        reverseCurve: AureaMotion.saida,
      );
      return FadeTransition(
        opacity: curva,
        child: ScaleTransition(
          scale: Tween(begin: .96, end: 1.0).animate(curva),
          alignment: Alignment.topRight,
          child: filho,
        ),
      );
    },
  );
}
