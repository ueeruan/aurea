import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// COMO SE FECHA UM PAINEL DE PARAMETRO.
///
/// Um painel de parametro normalmente e uma folha persistente do
/// Scaffold do editor (showBottomSheet). Usar Navigator.pop pode retirar
/// o EDITOR quando a entrada de historico da folha ja foi consumida.
/// Este escopo carrega o fechar certo, e
/// [closeParamSheet] serve os dois casos: dentro do escopo fecha a
/// folha; fora dele (um modal de verdade) fecha a rota.
class ParamSheetScope extends InheritedWidget {
  const ParamSheetScope({super.key, required this.close, required super.child});

  final VoidCallback close;

  static VoidCallback? maybeOf(BuildContext context) =>
      context.getInheritedWidgetOfExactType<ParamSheetScope>()?.close;

  @override
  bool updateShouldNotify(ParamSheetScope old) => close != old.close;
}

/// Fecha o painel em que [context] esta — folha persistente ou rota.
void closeParamSheet(BuildContext context) {
  if (!context.mounted) return;
  final fechar = ParamSheetScope.maybeOf(context);
  if (fechar != null) {
    fechar();
    return;
  }
  // Um contexto externo ao painel nunca deve retirar a pagina do editor.
  // O fallback serve somente um popup que ainda seja a rota atual.
  final route = ModalRoute.of(context);
  if (route is PopupRoute && route.isCurrent) {
    Navigator.of(context).pop();
  }
}

/// OS ULTIMOS PAINEIS QUE A PESSOA ABRIU.
///
/// Em motion se fica alternando entre dois ou tres parametros — raio do
/// glow, opacidade, posicao — e reabrir o caminho inteiro toda vez e o
/// que cansa. Esta fileira e o atalho: um toque, direto no painel.
class RecentSheets {
  RecentSheets._();
  static final instance = RecentSheets._();

  static const max = 4;

  final List<({String label, VoidCallback reopen})> _itens = [];

  List<({String label, VoidCallback reopen})> get items =>
      List.unmodifiable(_itens);

  void push(String label, VoidCallback reopen) {
    _itens.removeWhere((e) => e.label == label);
    _itens.insert(0, (label: label, reopen: reopen));
    while (_itens.length > max) {
      _itens.removeLast();
    }
  }

  void clear() => _itens.clear();
}

/// A CASCA DE TODO PAINEL DE PARAMETRO.
///
/// Tres coisas que o polegar cobra num celular:
///
///   TRES ALTURAS   espiada, metade e cheia, pela alca. Espiar um valor
///                  sem perder o preview e o caso comum.
///   VOLTAR EMBAIXO o polegar alcanca o terco inferior; botao de voltar
///                  no topo obriga a reposicionar a mao.
///   DESLIZAR       para a direita fecha. Gesto, nao so botao.
///
/// E a fileira dos ultimos paineis, que resolve sozinha boa parte da
/// dor: alternar entre dois parametros deixa de custar o caminho
/// inteiro.
class ParamSheetShell extends StatefulWidget {
  const ParamSheetShell({
    super.key,
    required this.child,
    required this.maxHeight,
    this.title,
    this.onBack,
  });

  final Widget child;

  /// A altura CHEIA (a maior das tres). As outras sao fracoes dela.
  final double maxHeight;

  /// Trilha de navegacao: "Camada › Efeitos › Glow".
  final String? title;

  /// Voltar um nivel. Nulo = fecha a folha.
  final VoidCallback? onBack;

  @override
  State<ParamSheetShell> createState() => _ParamSheetShellState();
}

class _ParamSheetShellState extends State<ParamSheetShell> {
  /// 0 = espiada, 1 = metade, 2 = cheia.
  ///
  /// Abre CHEIA: quem abre um parametro quer mexer nele agora, e na
  /// metade o grafico de curva e as reguas ficavam fora da vista.
  /// Espiar continua a um arrasto da alca.
  int _nivel = 2;

  static const _fracoes = [0.42, 0.72, 1.0];

  double get _altura => widget.maxHeight * _fracoes[_nivel];

  void _sobe() {
    if (_nivel < 2) setState(() => _nivel++);
  }

  void _desce() {
    if (_nivel > 0) {
      setState(() => _nivel--);
    } else {
      _fecha();
    }
  }

  void _fecha() {
    final voltar = widget.onBack;
    if (voltar != null) {
      voltar();
    } else {
      closeParamSheet(context);
    }
  }

  @override
  Widget build(BuildContext context) => SizedBox(
    height: _altura,
    child: Column(
      children: [
        GestureDetector(
          behavior: HitTestBehavior.opaque,
          onVerticalDragEnd: (d) {
            final v = d.primaryVelocity ?? 0;
            if (v > 260) {
              _desce();
            } else if (v < -260) {
              _sobe();
            }
          },
          child: SizedBox(
            height: 44,
            child: Row(
              children: [
                IconButton(
                  tooltip: 'Fechar painel',
                  onPressed: _fecha,
                  icon: const Icon(
                    CupertinoIcons.chevron_back,
                    size: 23,
                    color: AmColors.text,
                  ),
                ),
                Expanded(
                  child: AppText(
                    widget.title ?? 'Ajustar',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 14, color: AmColors.text),
                  ),
                ),
                IconButton(
                  tooltip: 'Expandir painel',
                  onPressed: _sobe,
                  icon: const Icon(
                    CupertinoIcons.chevron_up,
                    size: 16,
                    color: AmColors.muted,
                  ),
                ),
              ],
            ),
          ),
        ),
        Expanded(child: widget.child),
      ],
    ),
  );
}
