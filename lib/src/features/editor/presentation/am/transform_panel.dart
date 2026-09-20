import 'package:aurea/src/core/l10n/app_language.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'animador_sheet.dart';
import 'am_widgets.dart';
import '../context/parameter_row.dart';
import '../../application/ui/pro_mode.dart';
import 'property_keyframe_context.dart';
import '../widgets/painel_de_transformacao.dart';
import '../widgets/rails_do_painel.dart';

export '../../application/ui/editor_session.dart'
    show TransformTool, propOfTool;

/// Painel "Movimentacao e transformacao": trilho esquerdo (voltar,
/// keyframe, curva), controle central com navegacao de keyframes
/// e sub-ferramentas a direita (mover/girar/escalar/inclinar/pivo).
/// O losango vive no trilho esquerdo, montado por [AlvoDoRail].
///
/// O CORPO E O `PainelDeTransformacao`, com uma superficie propria por
/// face. O corpo antigo — uma linha de parametro por propriedade dentro
/// de uma aba rolavel — ficou neste arquivo por meses DEPOIS de ter
/// saido da tela, e foi a origem de tres defeitos: dois testes de gesto
/// procuravam o dial por uma chave que so existia la, o menu oferecia
/// "Editar pivo" e "Opacidade" apontando para controles que ninguem
/// montava, e o animador automatico e a expressao ficaram sem porta
/// nenhuma. Tudo isso saiu junto com ele.

/// O toque longo no valor (Pro) abre a expressao da propriedade.
/// ANIMAR SOZINHO: o toque longo no nome da propriedade oferece o
/// animador automatico — a propriedade balança sem keyframe nenhum.
VoidCallback _animador(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  String nome, {
  String unidade = '',
}) =>
    () => showAnimadorSheet(
      context,
      ref,
      layer.id,
      prop,
      nome: nome,
      unidade: unidade,
    );

VoidCallback? _expressao(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  String nome,
) {
  // `read`, e nao `watch`: esta funcao e chamada do TOQUE, nunca do
  // build. Um `watch` fora do build ou mente (nao reconstroi) ou
  // estoura a assercao do Riverpod — e o menu ja nasce lendo o valor
  // do momento em que o dedo encosta.
  if (!ref.read(proModeProvider)) return null;
  return () async {
    final controller = ref.read(editorControllerProvider.notifier);
    final atual = controller.propExpression(layer, prop);
    final erro = switch (prop) {
      LayerProp.opacity => layer.opacity.expressionError?.mensagem,
      LayerProp.rotation => layer.rotation.expressionError?.mensagem,
      LayerProp.scale => layer.scaleX.expressionError?.mensagem,
      LayerProp.skew => layer.skewX.expressionError?.mensagem,
      _ => null,
    };
    final r = await showExpressionEditor(
      context,
      atual: atual,
      erro: erro,
      nome: nome,
    );
    if (r == null) return;
    controller.setPropExpression(layer.id, prop, r);
  };
}

/// O TOQUE LONGO NO NUMERO DO PAINEL NOVO.
///
/// No painel antigo eram DOIS alvos: o nome abria "Animar sozinho" e o
/// valor abria a expressao (Pro). O corpo novo tem um alvo so — o
/// numero —, entao os dois viram um menu.
///
/// Isto nao e enfeite: enquanto o corpo novo nao oferecia nenhuma das
/// duas, o ANIMADOR AUTOMATICO e o EDITOR DE EXPRESSAO ficaram
/// inalcancaveis no aplicativo inteiro. As duas portas pendiam dos
/// controles que este painel deixou de montar, e um recurso que existe
/// e nao se acha e o mesmo que nao existir.
Future<void> menuDoCampo(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  String nome,
  String unidade,
) async {
  final expressao = _expressao(context, ref, layer, prop, nome);
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    builder: (sheet) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (expressao != null)
            ListTile(
              leading: const Icon(
                CupertinoIcons.function,
                size: 20,
                color: AmColors.text,
              ),
              title: const AppText('Expressão'),
              subtitle: const AppText(
                'O valor calculado por uma conta',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              onTap: () {
                Navigator.of(sheet).pop();
                expressao();
              },
            ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.wand_stars,
              size: 20,
              color: AmColors.text,
            ),
            title: const AppText('Animar sozinho'),
            subtitle: const AppText(
              'Balança sem keyframe nenhum',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
            onTap: () {
              Navigator.of(sheet).pop();
              _animador(context, ref, layer, prop, nome, unidade: unidade)();
            },
          ),
          const SizedBox(height: 8),
        ],
      ),
    ),
  );
}

class TransformPanel extends ConsumerStatefulWidget {
  const TransformPanel({
    super.key,
    required this.playback,
    required this.tool,
    required this.onToolChanged,
    required this.onBack,
    required this.onOpenCurve,
  });

  final PlaybackController playback;
  final TransformTool tool;
  final ValueChanged<TransformTool> onToolChanged;
  final VoidCallback onBack;
  final void Function(LayerProp prop) onOpenCurve;

  @override
  ConsumerState<TransformPanel> createState() => _TransformPanelState();
}

/// QUEM A POSICAO DEVE SEGUIR.
///
/// Era um metodo privado do controle de posicao, chamado pelo menu de
/// opcoes por `GlobalKey.currentState?._pickLinkSource(...)`. O menu
/// nasceu ligado a um controle que o painel deixou de montar, entao o
/// `currentState` ficou sempre nulo e o item "Vincular posicao" — que
/// continua VISIVEL no menu — nao fazia absolutamente nada. Um controle
/// visivel e inerte em silencio e o defeito que este projeto nao aceita.
///
/// Aqui a escolha nao depende de quem esta montado: a funcao e chamada
/// direto pelo menu, com o id da camada.
Future<void> escolherOrigemDoVinculo(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  Duration t,
) async {
  final project = ref.read(editorControllerProvider);
  final controller = ref.read(editorControllerProvider.notifier);
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    builder: (sheetContext) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Padding(
            padding: EdgeInsets.all(14),
            child: AppText(
              'Seguir a posicao de...',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          for (final other in project.layers)
            if (other.id != layerId)
              ListTile(
                title: AppText(
                  other.name,
                  style: const TextStyle(color: AmColors.text),
                ),
                onTap: () {
                  controller.linkProperty(
                    layerId,
                    LayerProp.position,
                    other.id,
                    t,
                  );
                  Navigator.of(sheetContext).pop();
                },
              ),
          const SizedBox(height: 8),
        ],
      ),
    ),
  );
}

class _TransformPanelState extends ConsumerState<TransformPanel> {
  /// A PROPRIEDADE VIGENTE VEM DO TRILHO, e nao de `widget.tool`.
  ///
  /// O trilho direito e quem escolhe a face na tela; `widget.tool` so
  /// diz por qual porta o painel foi aberto. Enquanto os dois podiam
  /// discordar, trocar para Escalar deixava o losango, o "reset" e a
  /// navegacao de keyframe do menu trabalhando em Posicao.
  LayerProp get _prop => propDoModo(ref.read(modoDeTransformacaoProvider));

  Widget _options(EditorController controller, String id, Layer layer) {
    final autoKey = ref.watch(autoKeyframeProvider);
    // So o vinculo, nao o projeto: o painel fica aberto durante o arrasto.
    final linked = ref.watch(
      editorControllerProvider.select(
        (p) => p.linkFor(id, LayerProp.position) != null,
      ),
    );
    return PopupMenuButton<String>(
      tooltip: autoKey
          ? 'Opções de transformação · auto-key ligado'
          : 'Opções de transformação',
      icon: AmMenuIcon(
        ativo:
            autoKey ||
            layer.is3D ||
            linked ||
            widget.tool == TransformTool.pivot,
      ),
      color: AmColors.panelHigh,
      itemBuilder: (_) => [
        CheckedPopupMenuItem(
          value: '3d',
          checked: layer.is3D,
          child: const AppText('Transformação 3D'),
        ),
        if (widget.tool == TransformTool.position)
          PopupMenuItem(
            value: 'link',
            child: AppText(linked ? 'Desvincular posição' : 'Vincular posição'),
          ),
        CheckedPopupMenuItem(
          value: 'auto',
          checked: ref.read(autoKeyframeProvider),
          child: const AppText('Auto-key'),
        ),
        const PopupMenuItem(
          value: 'previous',
          child: AppText('Keyframe anterior'),
        ),
        const PopupMenuItem(value: 'next', child: AppText('Próximo keyframe')),
        const PopupMenuItem(
          value: 'reset',
          child: AppText('Resetar propriedade'),
        ),
        CheckedPopupMenuItem(
          value: 'pivot',
          checked: widget.tool == TransformTool.pivot,
          child: const AppText('Editar pivô'),
        ),
        CheckedPopupMenuItem(
          value: 'opacity',
          checked: widget.tool == TransformTool.opacity,
          child: const AppText('Opacidade'),
        ),
      ],
      onSelected: (value) {
        if (value == 'auto') {
          final setting = ref.read(autoKeyframeProvider.notifier);
          setting.state = !setting.state;
        } else if (value == '3d') {
          controller.toggle3D(id);
        } else if (value == 'link') {
          if (linked) {
            controller.unlinkProperty(id, LayerProp.position);
          } else {
            escolherOrigemDoVinculo(
              context,
              ref,
              id,
              widget.playback.time.value,
            );
          }
        } else if (value == 'pivot') {
          widget.onToolChanged(TransformTool.pivot);
        } else if (value == 'opacity') {
          widget.onToolChanged(TransformTool.opacity);
        } else if (value == 'reset') {
          controller.resetProp(id, _prop);
        } else {
          final times = keyframeTimesForProp(layer, _prop).toList()..sort();
          final local = layer
              .localTime(widget.playback.time.value)
              .inMicroseconds;
          final target = value == 'previous'
              ? times.where((us) => us < local - 8000).lastOrNull
              : times.where((us) => us > local + 8000).firstOrNull;
          if (target != null) {
            widget.playback.pause();
            widget.playback.seek(
              layer.startTime + Duration(microseconds: target),
            );
          }
        }
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(autoKeyframeProvider);
    final id = ref.watch(selectedLayerProvider);
    // A CAMADA, NAO O PROJETO. Este painel e o que mais fica aberto
    // enquanto o dedo arrasta: observar o projeto inteiro o refazia a cada
    // mutacao, inclusive as de outra camada.
    final layer = id == null
        ? null
        : ref.watch(projetoVisivelProvider.select((p) => p.layerById(id)));
    if (layer == null || id == null) {
      return ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);

    // O painel ESCUTA o relogio: sem isso, o `t` capturado no build fica
    // velho apos scrub na timeline e o diamante marcava keyframe no tempo
    // de quando o painel abriu (o bug do "keyframe fora do playhead").
    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, t, _) {
        return PainelDeTransformacao(
          camada: layer,
          tempo: t,
          playback: widget.playback,
          aoVoltar: widget.onBack,
          mais: _options(controller, id, layer),
          aoTrocarModo: (modo) => widget.onToolChanged(toolDoModo(modo)),
          aoSegurarCampo: (prop, nome, unidade) => menuDoCampo(
            context,
            ref,
            ref.read(editorControllerProvider).layerById(id) ?? layer,
            prop,
            nome,
            unidade,
          ),
          alvoDoRail: (modo) {
            final prop = propDoModo(modo);
            final realLayer =
                ref.watch(
                  editorControllerProvider.select((p) => p.layerById(id)),
                ) ??
                layer;
            final local = realLayer.localTime(t);
            final times = keyframeTimesForProp(realLayer, prop);
            final hasKfHere = times.any(
              (us) => (us - local.inMicroseconds).abs() < 8000,
            );
            return AlvoDoRail(
              temKeyframeAqui: hasKfHere,
              animado: times.isNotEmpty,
              aoAlternarKeyframe: () => controller.toggleKeyframe(id, t, prop),
              aoAbrirCurva: times.length >= 2
                  ? () => widget.onOpenCurve(prop)
                  : null,
            );
          },
        );
      },
    );
  }

}
