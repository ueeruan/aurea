import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show Tooltip;

import 'am_colors.dart';
import 'am_widgets.dart';

/// A CASCA DE TODO PAINEL DE PARAMETRO.
///
/// Motion e um ciclo apertado: rolar o tempo, olhar o preview, ajustar o
/// valor, cravar o keyframe, rolar de novo. As tres coisas que o ciclo
/// precisa — preview, controle do parametro e a linha de keyframes
/// DAQUELE parametro — tem de estar visiveis ao mesmo tempo. Se uma sai
/// da tela, a pessoa passa a navegar em vez de animar.
///
/// Dai as regras desta casca:
///
///   ALTURA FIXA      igual entre tipos de camada e entre secoes. Painel
///                    que muda de tamanho redimensiona o preview, e o
///                    enquadramento pula debaixo do dedo.
///   ABAS A DIREITA   trocar de parametro e UM toque lateral, nao
///                    voltar-e-entrar. Ficam na coluna da direita para o
///                    trilho da esquerda (voltar, keyframe, curva) nunca
///                    sair do lugar quando a aba muda.
///   TRILHO FIXO      voltar, keyframe, curva e mais num trilho vertical
///                    a esquerda, na mesma posicao em toda aba. Memoria
///                    muscular so existe se o botao nao anda.
class AmPanelChrome extends StatelessWidget {
  const AmPanelChrome({
    super.key,
    required this.onBack,
    required this.corpo,
    required this.animado,
    required this.temKfAqui,
    required this.onCravar,
    this.onCurva,
    this.abas = const [],
    this.abaAtiva,
    this.onAba,
    this.acoes = const [],
    this.compact = false,
    this.spacious = false,
    this.more,
  });

  final VoidCallback onBack;
  final Widget corpo;

  /// O parametro em edicao tem keyframes / tem keyframe neste instante.
  final bool animado;
  final bool temKfAqui;
  final VoidCallback onCravar;
  final VoidCallback? onCurva;

  final List<ParamTab> abas;
  final String? abaAtiva;
  final ValueChanged<String>? onAba;

  /// COMANDOS VISIVEIS do painel, fixos no fim da fileira de abas.
  ///
  /// E o lugar do que antes morava no `⋯`: resetar, apagar ponto, fechar
  /// caminho. Fica preso a direita e nao rola junto com as abas, para
  /// estar sempre no mesmo pixel.
  final List<Widget> acoes;
  final bool compact;
  final bool spacious;
  final Widget? more;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // TRILHO ESQUERDO FIXO. Troca-se de aba pela fileira e o
          // diamante nao sai do lugar — e o que faz memoria muscular.
          AmLeftRail(
            width: spacious ? 44 : 56,
            more: more,
            onBack: onBack,
            animado: animado,
            temKfAqui: temKfAqui,
            onCravar: onCravar,
            onCurva: onCurva,
          ),
          Expanded(
            child: Column(
              children: [
                if (acoes.isNotEmpty)
                  SizedBox(
                    height: compact ? 28 : 38,
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.end,
                      children: acoes,
                    ),
                  ),
                // CANTOS EM L: delimitam a area de arrasto sem desenhar
                // uma caixa. Sem eles nao se sabe onde o dedo vale, e a
                // pessoa arrasta fora e nada acontece.
                Expanded(
                  child: Padding(
                    padding: compact
                        ? const EdgeInsets.fromLTRB(6, 2, 4, 2)
                        : const EdgeInsets.fromLTRB(10, 6, 6, 10),
                    child: _Cantos(child: corpo),
                  ),
                ),
              ],
            ),
          ),
          // TRILHO DIREITO: as sub-abas do parametro, em coluna. A ativa
          // com fundo solido — trocar de aba pela direita nao move o
          // botao de cravar keyframe, que fica na esquerda.
          if (abas.isNotEmpty)
            AmRightTabs(
              abas: abas,
              ativa: abaAtiva,
              onAba: onAba ?? (_) {},
              width: spacious ? 46 : 58,
            ),
        ],
      ),
    );
  }
}

/// O TRILHO ESQUERDO: `← ◆ ⌇`, na mesma posicao em todo painel.
///
/// Tres itens, nao quatro: o `⋯` era um menu escondido, e o que ele abria
/// virou botao visivel do proprio painel.
///
/// Voltar, cravar keyframe, curva de easing, mais. Acao indisponivel
/// fica esmaecida, nunca some: botao que aparece e some troca o lugar
/// dos vizinhos, e a memoria muscular vira chute. Navegar entre
/// keyframes nao mora aqui: e tocar no diamante da barra da camada.
class AmLeftRail extends StatelessWidget {
  const AmLeftRail({
    super.key,
    required this.onBack,
    required this.animado,
    required this.temKfAqui,
    required this.onCravar,
    this.onCurva,
    this.width = 56,
    this.more,
  });

  final VoidCallback onBack;
  final bool animado;
  final bool temKfAqui;
  final VoidCallback onCravar;
  final VoidCallback? onCurva;
  final double width;
  final Widget? more;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: width,
      child: Column(
        children: [
          for (final button in <Widget>[
            Tooltip(
              message: 'Voltar às ferramentas da camada',
              child: AmRailButton(
                onTap: onBack,
                child: const Icon(
                  CupertinoIcons.chevron_back,
                  size: 24,
                  color: AmColors.text,
                ),
              ),
            ),
            Tooltip(
              message: temKfAqui
                  ? 'Remover keyframe neste instante'
                  : 'Adicionar keyframe neste instante',
              child: AmRailButton(
                onTap: onCravar,
                child: AmDiamondAdd(active: animado, filled: temKfAqui),
              ),
            ),
            Tooltip(
              message: animado
                  ? 'Editar curva da propriedade'
                  : 'Crie keyframes para editar a curva',
              child: AmRailButton(
                onTap: animado ? onCurva : null,
                child: Opacity(
                  opacity: animado && onCurva != null ? 1 : 0.32,
                  child: AmCurveIcon(
                    color: animado ? AmColors.text : AmColors.muted,
                  ),
                ),
              ),
            ),
          ])
            Flexible(child: button),
          if (more != null) Flexible(child: more!),
        ],
      ),
    );
  }
}

/// Uma aba da fileira de parametros.
class ParamTab {
  const ParamTab({
    required this.id,
    required this.label,
    this.icone,
    this.animated = false,
  });

  final String id;
  final String label;

  /// O icone da sub-aba no trilho direito. Sem ele a aba mostra so o
  /// rotulo, em duas linhas.
  final IconData? icone;

  /// Tem keyframes: a aba ganha um ponto, para se achar o que ja foi
  /// animado sem entrar em cada uma.
  final bool animated;
}

/// FILEIRA DE ABAS horizontal e rolavel, com indicador nas pontas.
///
/// O indicador nao e enfeite: fileira que corta a ultima aba no meio
/// avisa que ha mais; fileira que corta rente parece completa, e a
/// pessoa nunca descobre o que existe do lado.
class AmParamTabs extends StatefulWidget {
  const AmParamTabs({
    super.key,
    required this.abas,
    required this.ativa,
    required this.onAba,
  });

  final List<ParamTab> abas;
  final String? ativa;
  final ValueChanged<String> onAba;

  @override
  State<AmParamTabs> createState() => _AmParamTabsState();
}

class _AmParamTabsState extends State<AmParamTabs> {
  final _scroll = ScrollController();
  bool _temEsquerda = false;
  bool _temDireita = false;

  @override
  void initState() {
    super.initState();
    _scroll.addListener(_mede);
    WidgetsBinding.instance.addPostFrameCallback((_) => _mede());
  }

  @override
  void didUpdateWidget(AmParamTabs old) {
    super.didUpdateWidget(old);
    WidgetsBinding.instance.addPostFrameCallback((_) => _mede());
  }

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  void _mede() {
    if (!mounted || !_scroll.hasClients) return;
    final p = _scroll.position;
    final esq = p.pixels > 2;
    final dir = p.pixels < p.maxScrollExtent - 2;
    if (esq != _temEsquerda || dir != _temDireita) {
      setState(() {
        _temEsquerda = esq;
        _temDireita = dir;
      });
    }
  }

  /// Largura que uma aba pede: o texto mais o respiro do chip.
  static double _larguraDe(ParamTab aba) =>
      aba.label.length * 8.2 + 34 + (aba.animated ? 10 : 0);

  @override
  Widget build(BuildContext context) {
    // ABAS QUE CABEM DIVIDEM A LINHA POR IGUAL. Antes eram chips de
    // largura propria numa lista rolavel: a ultima saia cortada na borda
    // e as alturas variavam — a fileira parecia torta, e o dedo errava
    // o alvo. Se nao couberem, ai sim rolam, com as pontas indicando.
    return LayoutBuilder(
      builder: (context, c) {
        final pedido = widget.abas.fold<double>(
          16,
          (acc, a) => acc + _larguraDe(a) + 6,
        );
        final cabem = pedido <= c.maxWidth;
        return SizedBox(
          height: 48,
          child: cabem
              ? Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 8,
                    vertical: 6,
                  ),
                  child: Row(
                    children: [
                      for (final aba in widget.abas)
                        Expanded(
                          child: _Aba(
                            aba: aba,
                            selecionada: aba.id == widget.ativa,
                            onTap: () => widget.onAba(aba.id),
                          ),
                        ),
                    ],
                  ),
                )
              : Stack(
                  children: [
                    ListView(
                      controller: _scroll,
                      scrollDirection: Axis.horizontal,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 6,
                      ),
                      children: [
                        for (final aba in widget.abas)
                          _Aba(
                            aba: aba,
                            selecionada: aba.id == widget.ativa,
                            onTap: () => widget.onAba(aba.id),
                          ),
                      ],
                    ),
                    if (_temEsquerda) const _Ponta(esquerda: true),
                    if (_temDireita) const _Ponta(esquerda: false),
                  ],
                ),
        );
      },
    );
  }
}

class _Ponta extends StatelessWidget {
  const _Ponta({required this.esquerda});

  final bool esquerda;

  @override
  Widget build(BuildContext context) {
    return Positioned(
      left: esquerda ? 0 : null,
      right: esquerda ? null : 0,
      top: 0,
      bottom: 0,
      child: IgnorePointer(
        child: Container(
          width: 26,
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: esquerda ? Alignment.centerLeft : Alignment.centerRight,
              end: esquerda ? Alignment.centerRight : Alignment.centerLeft,
              colors: const [AmColors.panel, Color(0x00171C23)],
            ),
          ),
        ),
      ),
    );
  }
}

class _Aba extends StatelessWidget {
  const _Aba({
    required this.aba,
    required this.selecionada,
    required this.onTap,
  });

  final ParamTab aba;
  final bool selecionada;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        margin: const EdgeInsets.symmetric(horizontal: 3),
        padding: const EdgeInsets.symmetric(horizontal: 10),
        constraints: const BoxConstraints(minWidth: 64, minHeight: 36),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: selecionada ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              aba.label,
              style: TextStyle(
                fontSize: 12.5,
                fontWeight: selecionada ? FontWeight.w700 : FontWeight.w500,
                color: selecionada ? AmColors.accent : AmColors.text,
              ),
            ),
            if (aba.animated) ...[
              const SizedBox(width: 5),
              Container(
                width: 5,
                height: 5,
                decoration: const BoxDecoration(
                  color: AmColors.accent,
                  shape: BoxShape.circle,
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// UM COMANDO VISIVEL do painel, preso no fim da fileira de abas.
///
/// Chip preenchido, sem contorno. Desligado fica esmaecido em vez de
/// sumir: botao que some troca o lugar dos vizinhos.
class AmPanelAcao extends StatelessWidget {
  const AmPanelAcao({
    super.key,
    required this.rotulo,
    required this.icone,
    required this.onTap,
    this.ligado = false,
  });

  final String rotulo;
  final IconData icone;
  final VoidCallback? onTap;
  final bool ligado;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 6, right: 8),
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Opacity(
          opacity: onTap == null ? 0.35 : 1,
          child: Container(
            height: 30,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: BoxDecoration(
              color: ligado ? AmColors.accentDim : AmColors.chip,
              borderRadius: BorderRadius.circular(9),
            ),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  icone,
                  size: 14,
                  color: ligado ? AmColors.accent : AmColors.text,
                ),
                const SizedBox(width: 5),
                Text(
                  rotulo,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: ligado ? AmColors.accent : AmColors.text,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// AS SUB-ABAS DO PARAMETRO, na coluna da direita.
///
/// A ativa tem fundo solido, nao so texto colorido: numa tela de celular,
/// no sol, cor de texto sozinha nao diz qual esta ligada.
///
/// Divide a altura disponível entre as abas, mantendo todas visíveis.
class AmRightTabs extends StatelessWidget {
  const AmRightTabs({
    super.key,
    required this.abas,
    required this.ativa,
    required this.onAba,
    this.width = 58,
  });
  final List<ParamTab> abas;
  final String? ativa;
  final ValueChanged<String> onAba;
  final double width;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: width,
    child: LayoutBuilder(
      builder: (context, limits) {
        final slot = abas.isEmpty ? 0.0 : limits.maxHeight / abas.length;
        final iconSize = (slot - 16).clamp(8.0, 19.0);
        return Column(
          children: [
            for (final aba in abas)
              Expanded(
                child: Tooltip(
                  message: aba.label,
                  child: GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onTap: () => onAba(aba.id),
                    child: Container(
                      width: double.infinity,
                      margin: const EdgeInsets.fromLTRB(4, 1, 6, 1),
                      decoration: BoxDecoration(
                        color: ativa == aba.id ? AmColors.chip : AmColors.panel,
                        borderRadius: BorderRadius.circular(3),
                      ),
                      child: Stack(
                        alignment: Alignment.center,
                        children: [
                          Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              if (aba.icone != null) ...[
                                Icon(
                                  aba.icone,
                                  size: iconSize,
                                  color: ativa == aba.id
                                      ? AmColors.accent
                                      : AmColors.text,
                                ),
                                const SizedBox(height: 1),
                              ],
                              Text(
                                aba.label,
                                maxLines: 1,
                                textAlign: TextAlign.center,
                                overflow: TextOverflow.ellipsis,
                                style: TextStyle(
                                  fontSize: 9,
                                  height: 1.1,
                                  fontWeight: FontWeight.w600,
                                  color: ativa == aba.id
                                      ? AmColors.accent
                                      : AmColors.text,
                                ),
                              ),
                            ],
                          ),
                          if (aba.animated)
                            Positioned(
                              right: 3,
                              top: 3,
                              child: Container(
                                width: 4,
                                height: 4,
                                decoration: BoxDecoration(
                                  shape: BoxShape.circle,
                                  color: ativa == aba.id
                                      ? AmColors.accent
                                      : AmColors.accent,
                                ),
                              ),
                            ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    ),
  );
}

/// Os quatro cantos em L em volta do corpo do painel.
class _Cantos extends StatelessWidget {
  const _Cantos({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => CustomPaint(
    painter: _CantosPainter(),
    child: Padding(padding: const EdgeInsets.all(6), child: child),
  );
}

class _CantosPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final tinta = Paint()
      ..color = AmColors.muted.withValues(alpha: 0.45)
      ..strokeWidth = 1.5
      ..strokeCap = StrokeCap.round
      ..style = PaintingStyle.stroke;
    const b = 14.0;
    void canto(Offset o, double dx, double dy) {
      canvas.drawLine(o, o.translate(b * dx, 0), tinta);
      canvas.drawLine(o, o.translate(0, b * dy), tinta);
    }

    canto(Offset.zero, 1, 1);
    canto(Offset(size.width, 0), -1, 1);
    canto(Offset(0, size.height), 1, -1);
    canto(Offset(size.width, size.height), -1, -1);
  }

  @override
  bool shouldRepaint(covariant _CantosPainter oldDelegate) => false;
}
