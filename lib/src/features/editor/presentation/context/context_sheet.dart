import 'package:flutter/material.dart';

import '../../../../core/theme/tokens.dart';
import '../../application/ui/editor_layout.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// ZONA E — A CASCA DO PAINEL CONTEXTUAL.
///
/// Uma superficie so, sempre no mesmo lugar (a base da tela), com uma
/// alca em cima. O CONTEUDO muda com a selecao (E1–E5); a POSICAO nunca.
/// A alca arrasta a altura entre espiada, metade e cheia; o preview nao
/// se mexe — quem cede espaco e a timeline.
class ContextSheet extends StatelessWidget {
  const ContextSheet({
    super.key,
    required this.height,
    required this.child,
    this.title,
    this.subtitle,
    this.onBack,
    this.trailing,
  });

  /// Altura resolvida pelas metricas (ja inclui a alca).
  final double height;
  final Widget child;

  /// Cabecalho opcional: `‹ titulo` (categoria aberta) e a trilha.
  final String? title;
  final String? subtitle;
  final VoidCallback? onBack;
  final Widget? trailing;

  static const double handleHeight = 12;
  static const double titleHeight = 44;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return Container(
      key: const ValueKey('context-sheet'),
      height: height,
      decoration: BoxDecoration(
        color: t.surface,
        border: Border(top: BorderSide(color: t.hairline)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            key: const ValueKey('context-sheet-handle'),
            height: handleHeight,
            // SEM PUXADOR quando nao ha titulo. A pilula cinza convidava
            // a arrastar; agora nada se arrasta, e um convite que nao
            // leva a lugar nenhum e pior do que nao convidar.
            child: title == null
                ? const SizedBox.shrink()
                : Row(
                    children: [
                      if (onBack != null)
                        IconButton(
                          key: const ValueKey('painel-voltar'),
                          tooltip: 'Voltar às ferramentas da camada',
                          onPressed: onBack,
                          icon: Icon(
                            Icons.chevron_left,
                            size: 26,
                            color: t.text,
                          ),
                        ),
                      Expanded(
                        child: Tooltip(
                          message: subtitle ?? title!,
                          child: AppText(
                            title!,
                            key: const ValueKey('editor-context'),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              fontSize: 14,
                              fontWeight: FontWeight.w600,
                              color: t.text,
                            ),
                          ),
                        ),
                      ),
                      ?trailing,
                      const SizedBox(width: 12),
                    ],
                  ),
          ),
          Expanded(child: child),
        ],
      ),
    );
  }
}

/// A ALCA ENTRE O PREVIEW E O TRANSPORTE: arrasta a altura do preview.
/// Duplo toque volta ao padrao.
class PreviewResizeHandle extends StatelessWidget {
  const PreviewResizeHandle({
    super.key,
    required this.onExpand,
    required this.expanded,
  });

  final VoidCallback onExpand;
  final bool expanded;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    // A FAIXA NAO ARRASTA MAIS. A altura do preview sai da proporcao da
    // composicao, e o unico botao aqui e o de tela cheia. O que sobrou
    // e a linha que separa o preview do transporte.
    return SizedBox(
      key: const ValueKey('preview-resize-handle'),
      child: Container(
        height: EditorLayoutMetrics.handleHeight,
        color: t.surface,
        child: Stack(
          alignment: Alignment.center,
          children: [
            Positioned(
              right: 4,
              child: Tooltip(
                message: expanded ? 'Voltar ao editor' : 'Expandir preview',
                child: GestureDetector(
                  key: const ValueKey('preview-expand'),
                  behavior: HitTestBehavior.opaque,
                  onTap: onExpand,
                  child: SizedBox(
                    width: 40,
                    height: EditorLayoutMetrics.handleHeight,
                    child: Icon(
                      expanded ? Icons.fullscreen_exit : Icons.fullscreen,
                      size: 10,
                      color: t.muted,
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
