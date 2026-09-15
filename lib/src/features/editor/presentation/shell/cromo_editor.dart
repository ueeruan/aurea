import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/ui/opcoes_de_visualizacao.dart';
import '../../domain/layer.dart';
import '../../domain/layout_ops.dart';
import '../am/align_sheet.dart';
import '../am/am_colors.dart';
import '../am/beats_sheet.dart';
import '../am/cameras_sheet.dart';
import '../am/export_sheet.dart';
import 'layer_actions.dart';
import 'project_settings_sheet.dart';
import 'transport_bar.dart' show parseTimecodeInput;

/// O CROMO DO EDITOR (v1.1.1): as barras que cercam palco e timeline.
///
/// Barra do projeto de 44 no topo (sair · titulo · tempo · projeto ·
/// exportar), barra do LOTE no lugar dela quando ha multi-selecao,
/// barra de reproducao de 46 sobre a timeline (desfazer/refazer ·
/// quadro-play-quadro · colar · marcas · tela cheia), trilho vertical a
/// direita do palco (solo · camera · zoom do palco) e a barra flutuante
/// de aparar/dividir quando ha camada selecionada.
///
/// AS CORES SAO AS DA AUREA ([AmColors]): o lima e acao e "ligado", o
/// teal e keyframe, o violeta e selecao e grupo. Estrutura de editor de
/// motion se aprende de qualquer referencia; paleta, icones e textos sao
/// nossos.
abstract final class CromoEditor {
  static const Color fundo = AmColors.topBar;
  static const Color palco = AmColors.bg;
  static const Color trilho = AmColors.chip;

  /// Acao e estado ligado (o lima da logo).
  static const Color acao = AmColors.action;
  static const Color sobreAcao = AmColors.onAction;

  /// Keyframe no cabecote (teal).
  static const Color keyframe = AmColors.accent;

  /// Selecao e grupo (violeta da logo): a barra do lote e o chip de grupo.
  static const Color selecao = AmColors.selection;
  static const Color branco = AmColors.text;
  static const Color apagado = Color(0x66FFFFFF);

  static const double navbar = 44;
  static const double playbar = 46;
}

/// O ZOOM DO PALCO (trilho da direita): 1.0 = ajustado a janela.
final zoomDoPalcoProvider = StateProvider<double>((ref) => 1.0);

/// Um botao de icone do cromo: alvo de 40, sem enfeite proprio.
class _BotaoDoCromo extends StatelessWidget {
  const _BotaoDoCromo({
    super.key,
    required this.icone,
    required this.dica,
    required this.onTap,
    this.onLongPress,
    this.cor,
    this.tamanho = 21,
    this.largura = 40,
    this.altura = 44,
  });

  final IconData icone;
  final String dica;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final Color? cor;
  final double tamanho;
  final double largura;
  final double altura;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: dica,
      child: Tocavel(
        onTap: onTap == null
            ? null
            : () {
                HapticFeedback.lightImpact();
                onTap!();
              },
        onLongPress: onLongPress == null
            ? null
            : () {
                HapticFeedback.mediumImpact();
                onLongPress!();
              },
        child: SizedBox(
          width: largura,
          height: altura,
          child: Icon(
            icone,
            size: tamanho,
            color: cor ??
                (onTap == null
                    ? CromoEditor.apagado.withValues(alpha: .25)
                    : CromoEditor.branco),
          ),
        ),
      ),
    );
  }
}

String _tempoCurto(Duration d) {
  final cs = (d.inMilliseconds % 1000) ~/ 10;
  final s = d.inSeconds % 60;
  final m = d.inMinutes;
  return '$m:${s.toString().padLeft(2, '0')}.${cs.toString().padLeft(2, '0')}';
}

/// A BARRA DO PROJETO: sair · titulo (toque renomeia; dentro de grupo, o
/// chip sai do grupo) · tempo corrente (toque digita) · projeto ·
/// exportar.
class BarraDoProjeto extends ConsumerWidget {
  const BarraDoProjeto({
    super.key,
    required this.onBack,
    required this.playback,
  });

  final VoidCallback onBack;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final dentroDeGrupo = controller.dentroDeGrupo;
    return Container(
      height: CromoEditor.navbar,
      color: CromoEditor.fundo,
      padding: const EdgeInsets.only(right: 6),
      child: Row(
        children: [
          Tooltip(
            message: dentroDeGrupo ? 'Voltar' : 'Projetos',
            child: Tocavel(
              key: const ValueKey('editor-back'),
              onTap: onBack,
              child: SizedBox(
                width: 44,
                height: 44,
                // A porta com a seta: sair do projeto.
                child: Transform.flip(
                  flipX: true,
                  child: const Icon(
                    Icons.logout,
                    size: 20,
                    color: CromoEditor.branco,
                  ),
                ),
              ),
            ),
          ),
          if (dentroDeGrupo)
            Tooltip(
              message: 'Sair do grupo',
              child: Tocavel(
                key: const ValueKey('navbar-sair-grupo'),
                onTap: controller.exitGroup,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  height: 26,
                  margin: const EdgeInsets.only(right: 6),
                  decoration: BoxDecoration(
                    color: CromoEditor.trilho,
                    borderRadius: BorderRadius.circular(13),
                  ),
                  child: const Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        CupertinoIcons.chevron_left,
                        size: 12,
                        color: CromoEditor.selecao,
                      ),
                      SizedBox(width: 2),
                      Icon(
                        CupertinoIcons.rectangle_stack,
                        size: 14,
                        color: CromoEditor.selecao,
                      ),
                    ],
                  ),
                ),
              ),
            ),
          Expanded(
            child: GestureDetector(
              key: const ValueKey('editor-project-name'),
              behavior: HitTestBehavior.opaque,
              onTap: () => renomearProjeto(context, ref),
              child: AppText(
                project.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: CromoEditor.branco,
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
          ),
          // O relogio mora na barra do projeto: tocar digita o tempo.
          ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (context, t, _) => Tooltip(
              message: 'Ir para o tempo',
              child: Tocavel(
                key: const ValueKey('navbar-tempo'),
                onTap: () => digitarTempo(context, ref, playback),
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 12,
                  ),
                  child: AppText(
                    _tempoCurto(t),
                    style: const TextStyle(
                      color: CromoEditor.apagado,
                      fontSize: 12.5,
                      fontFeatures: [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
            ),
          ),
          _BotaoDoCromo(
            key: const ValueKey('editor-settings'),
            icone: CupertinoIcons.gear_alt_fill,
            dica: 'Projeto',
            tamanho: 19,
            onTap: () => showProjectSettingsSheet(context, ref),
          ),
          _BotaoDoCromo(
            key: const ValueKey('editor-export'),
            icone: CupertinoIcons.square_arrow_up,
            dica: 'Exportar',
            cor: CromoEditor.acao,
            onTap: () {
              // EXPORTAR DE DENTRO DE UM GRUPO exportava so o grupo: o
              // palco da exportacao le o estado do editor. Sai de todos
              // antes — o projeto inteiro e o que se exporta.
              controller.exitAllGroups();
              showExportSheet(context, ref);
            },
          ),
        ],
      ),
    );
  }
}

/// A BARRA DO LOTE: quando ha multi-selecao, o topo inteiro vira
/// violeta (a cor da selecao) com as acoes do lote — agrupar, alinhar na
/// tela, excluir. Toque longo num alinhamento abre a folha completa.
class BarraDoLote extends ConsumerWidget {
  const BarraDoLote({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final multi = ref.watch(multiSelectProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final ids = multi.toList();
    final t = playback.time.value;

    Widget alinhar(Key key, IconData icone, String dica, AlignEdge edge) =>
        _BotaoDoCromo(
          key: key,
          icone: icone,
          dica: dica,
          cor: CromoEditor.branco,
          tamanho: 18,
          largura: 30,
          onTap: () => controller.alignSelection(ids, edge, t),
          onLongPress: () => showAlignSheet(context, ref, ids, t),
        );

    return Container(
      height: CromoEditor.navbar,
      color: CromoEditor.selecao,
      child: Row(
        children: [
          _BotaoDoCromo(
            key: const ValueKey('selectbar-cancelar'),
            icone: CupertinoIcons.xmark,
            dica: 'Cancelar seleção',
            cor: CromoEditor.branco,
            onTap: () =>
                ref.read(multiSelectProvider.notifier).state = const {},
          ),
          Expanded(
            child: AppText(
              '${multi.length} selecionadas',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                color: CromoEditor.branco,
                fontSize: 13,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-esquerda'),
            CupertinoIcons.arrow_left_to_line,
            'Alinhar à esquerda · segure para mais',
            AlignEdge.left,
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-centro'),
            CupertinoIcons.arrow_left_right,
            'Centralizar na horizontal · segure para mais',
            AlignEdge.centerH,
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-direita'),
            CupertinoIcons.arrow_right_to_line,
            'Alinhar à direita · segure para mais',
            AlignEdge.right,
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-topo'),
            CupertinoIcons.arrow_up_to_line,
            'Alinhar ao topo · segure para mais',
            AlignEdge.top,
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-meio'),
            CupertinoIcons.arrow_up_arrow_down,
            'Centralizar na vertical · segure para mais',
            AlignEdge.centerV,
          ),
          alinhar(
            const ValueKey('selectbar-alinhar-base'),
            CupertinoIcons.arrow_down_to_line,
            'Alinhar à base · segure para mais',
            AlignEdge.bottom,
          ),
          const SizedBox(width: 2),
          _BotaoDoCromo(
            key: const ValueKey('selectbar-agrupar'),
            icone: CupertinoIcons.rectangle_stack,
            dica: 'Agrupar seleção',
            cor: CromoEditor.branco,
            largura: 36,
            onTap: multi.length >= 2
                ? () => agruparSelecao(ref, multi)
                : null,
          ),
          _BotaoDoCromo(
            key: const ValueKey('selectbar-excluir'),
            icone: CupertinoIcons.trash,
            dica: 'Excluir seleção',
            cor: CromoEditor.branco,
            largura: 36,
            onTap: () => excluirCamadas(context, ref, multi),
          ),
          const SizedBox(width: 4),
        ],
      ),
    );
  }
}

/// A BARRA DE REPRODUCAO (46, sobre a timeline): desfazer/refazer a
/// esquerda, quadro-play-quadro no centro; a direita copiar e colar,
/// marcador no cabecote, opcoes de visualizacao e tela cheia.
///
/// Os saltos: toque em |◀ ▶| anda por KEYFRAME quando a camada
/// selecionada tem marcas (o pedido dos testadores), senao UM QUADRO;
/// segurar vai ao inicio/fim.
///
/// Enquanto o dedo manipula alguma coisa ([infobarProvider]), a barra
/// vira a barra de informacoes: o numero que esta mudando fica onde o
/// olho ja esta. Tocando, um medidor de nivel do som corre por tras
/// dos botoes.
class BarraDeReproducao extends ConsumerWidget {
  const BarraDeReproducao({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final info = ref.watch(infobarProvider);
    if (info != null) {
      return Container(
        key: const ValueKey('barra-de-informacoes'),
        height: CromoEditor.playbar,
        color: CromoEditor.fundo,
        child: _ConteudoDaInfobar(info: info),
      );
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.watch(editorControllerProvider);
    final selected = ref.watch(selectedLayerProvider);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final duration = project.duration;
    final fps = project.fps <= 0 ? 30 : project.fps;
    final quadro = Duration(microseconds: 1000000 ~/ fps);

    final camada = selected == null ? null : project.layerById(selected);
    final marcas = camada == null
        ? const <Duration>[]
        : [for (final k in camada.keyframeTimes) camada.startTime + k];
    Duration? anterior(Duration agora) {
      Duration? achada;
      for (final m in marcas) {
        if (m < agora - const Duration(milliseconds: 1)) achada = m;
      }
      return achada;
    }

    Duration? proxima(Duration agora) {
      for (final m in marcas) {
        if (m > agora + const Duration(milliseconds: 1)) return m;
      }
      return null;
    }

    Duration limitar(Duration t) =>
        t < Duration.zero ? Duration.zero : (t > duration ? duration : t);

    final expandido = ref.watch(
      editorSessionProvider.select((s) => s.previewExpanded),
    );

    return Container(
      height: CromoEditor.playbar,
      color: CromoEditor.fundo,
      child: Stack(
        children: [
          Positioned.fill(
            child: _MedidorDeNivel(playback: playback),
          ),
          LayoutBuilder(
            builder: (context, c) {
              // NUM 320 os seis botoes laterais (2 + 4) em 40 estouravam
              // a fileira: o trio do centro e fixo (132), os lados dividem
              // o que sobra, nunca abaixo de 30.
              final lado = c.maxWidth.isFinite
                  ? ((c.maxWidth - 132) / 6).clamp(30.0, 40.0).toDouble()
                  : 40.0;
              return Row(
                children: [
                  _BotaoDoCromo(
                    key: const ValueKey('editor-undo'),
                    icone: CupertinoIcons.arrow_uturn_left,
                    dica: 'Desfazer',
                    largura: lado,
                    onTap: controller.canUndo ? controller.undo : null,
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('editor-redo'),
                    icone: CupertinoIcons.arrow_uturn_right,
                    dica: 'Refazer',
                    largura: lado,
                    onTap: controller.canRedo ? controller.redo : null,
                  ),
                  Expanded(
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        _BotaoDoCromo(
                          key: const ValueKey('transport-start'),
                          icone: CupertinoIcons.backward_end,
                          dica: marcas.isEmpty
                              ? 'Um quadro atrás · segure para o início'
                              : 'Keyframe anterior · segure para o início',
                          onTap: () => playback.seek(
                            anterior(playback.time.value) ??
                                limitar(playback.time.value - quadro),
                          ),
                          onLongPress: () => playback.seek(Duration.zero),
                        ),
                        ListenableBuilder(
                          listenable: Listenable.merge([
                            playback.playing,
                            playback.loop,
                          ]),
                          builder: (context, _) => Stack(
                            alignment: Alignment.center,
                            children: [
                              _BotaoDoCromo(
                                key: const ValueKey('transport-play'),
                                icone: playback.playing.value
                                    ? CupertinoIcons.pause_fill
                                    : CupertinoIcons.play_fill,
                                dica: playback.loop.value
                                    ? 'Repetição ligada · segure para desligar'
                                    : (playback.playing.value
                                          ? 'Pausar'
                                          : 'Reproduzir · segure para repetir'),
                                cor: playback.loop.value
                                    ? CromoEditor.acao
                                    : CromoEditor.branco,
                                tamanho: 26,
                                largura: 52,
                                onTap: playback.toggle,
                                onLongPress: () =>
                                    playback.loop.value = !playback.loop.value,
                              ),
                              // Repetindo, o play ganha a setinha do laco.
                              if (playback.loop.value)
                                const Positioned(
                                  right: 8,
                                  bottom: 8,
                                  child: IgnorePointer(
                                    child: Icon(
                                      CupertinoIcons.repeat,
                                      key: ValueKey('transport-play-laco'),
                                      size: 11,
                                      color: CromoEditor.acao,
                                    ),
                                  ),
                                ),
                            ],
                          ),
                        ),
                        _BotaoDoCromo(
                          key: const ValueKey('transport-end'),
                          icone: CupertinoIcons.forward_end,
                          dica: marcas.isEmpty
                              ? 'Um quadro à frente · segure para o fim'
                              : 'Próximo keyframe · segure para o fim',
                          onTap: () => playback.seek(
                            proxima(playback.time.value) ??
                                limitar(playback.time.value + quadro),
                          ),
                          onLongPress: () => playback.seek(duration),
                        ),
                      ],
                    ),
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('playbar-colar'),
                    icone: CupertinoIcons.doc_on_clipboard,
                    dica: 'Copiar e colar',
                    largura: lado,
                    onTap: selected == null && !controller.temEfeitosCopiados
                        ? null
                        : () => _menuDeColar(context, ref, selected),
                  ),
                  ValueListenableBuilder<Duration>(
                    valueListenable: playback.time,
                    builder: (context, t, _) {
                      final aqui = project.markerNear(
                        t,
                        Duration(microseconds: quadro.inMicroseconds ~/ 2),
                      );
                      return _BotaoDoCromo(
                        key: const ValueKey('playbar-marcador'),
                        icone: aqui == null
                            ? CupertinoIcons.bookmark
                            : CupertinoIcons.bookmark_fill,
                        dica: aqui == null
                            ? 'Marcar este instante · segure para as marcas'
                            : 'Tirar a marca · segure para as marcas',
                        cor: aqui?.color,
                        largura: lado,
                        // O TEMPO SAI NA HORA DO TOQUE, como na barra da
                        // selecao: o construtor so decide o desenho.
                        onTap: () =>
                            controller.toggleMarker(playback.time.value),
                        onLongPress: () =>
                            menuDasMarcas(context, ref, playback),
                      );
                    },
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('playbar-visualizacao'),
                    icone: !opcoes.visaoDaCamera
                        ? CupertinoIcons.videocam
                        : (opcoes.aberta
                              ? CupertinoIcons.eye_fill
                              : CupertinoIcons.eye),
                    dica: opcoes.aberta
                        ? 'Fechar as opções de visualização'
                        : 'Opções de visualização',
                    cor: opcoes.aberta ? CromoEditor.acao : null,
                    largura: lado,
                    onTap: () => ref
                        .read(opcoesDeVisualizacaoProvider.notifier)
                        .alternarColuna(),
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('transport-expand'),
                    icone: expandido
                        ? CupertinoIcons.fullscreen_exit
                        : CupertinoIcons.fullscreen,
                    dica: expandido ? 'Sair da tela cheia' : 'Tela cheia',
                    largura: lado,
                    onTap: () => ref
                        .read(editorSessionProvider.notifier)
                        .togglePreviewExpanded(),
                  ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }

  Future<void> _menuDeColar(
    BuildContext context,
    WidgetRef ref,
    String? selected,
  ) async {
    final controller = ref.read(editorControllerProvider.notifier);
    await showCupertinoModalPopup<void>(
      context: context,
      builder: (ctx) => CupertinoActionSheet(
        actions: [
          if (selected != null)
            CupertinoActionSheetAction(
              key: const ValueKey('colar-duplicar'),
              onPressed: () {
                Navigator.pop(ctx);
                controller.duplicateLayer(selected);
              },
              child: const AppText('Duplicar camada'),
            ),
          if (selected != null)
            CupertinoActionSheetAction(
              key: const ValueKey('colar-copiar-efeitos'),
              onPressed: () {
                Navigator.pop(ctx);
                final n = controller.copyEffects(selected);
                AureaSnack.show(
                  context,
                  n == 0
                      ? 'Esta camada não tem efeitos para copiar'
                      : 'Efeitos copiados: $n',
                );
              },
              child: const AppText('Copiar efeitos'),
            ),
          if (selected != null && controller.temEfeitosCopiados)
            CupertinoActionSheetAction(
              key: const ValueKey('colar-colar-efeitos'),
              onPressed: () {
                Navigator.pop(ctx);
                final n = controller.pasteEffects(selected);
                AureaSnack.show(context, 'Efeitos colados: $n');
              },
              child: const AppText('Colar efeitos'),
            ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
      ),
    );
  }
}

/// O CONTEUDO DA BARRA DE INFORMACOES: tempo e deslocamento quando o
/// arrasto e no tempo; ate seis pares rotulo/valor quando e no palco.
class _ConteudoDaInfobar extends StatelessWidget {
  const _ConteudoDaInfobar({required this.info});

  final DadosDaInfobar info;

  @override
  Widget build(BuildContext context) {
    const estiloValor = TextStyle(
      color: CromoEditor.branco,
      fontSize: 13,
      fontWeight: FontWeight.w600,
      fontFeatures: [FontFeature.tabularFigures()],
    );
    const estiloRotulo = TextStyle(color: CromoEditor.apagado, fontSize: 10.5);
    final tempo = info.tempo;
    if (tempo != null) {
      final desloc = info.deslocamento ?? Duration.zero;
      return Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(
            CupertinoIcons.rhombus,
            size: 14,
            color: CromoEditor.keyframe,
          ),
          const SizedBox(width: 6),
          AppText(
            tempoDaInfobar(tempo),
            key: const ValueKey('infobar-tempo'),
            style: estiloValor,
          ),
          const SizedBox(width: 22),
          const Icon(
            CupertinoIcons.arrow_right_arrow_left,
            size: 14,
            color: CromoEditor.apagado,
          ),
          const SizedBox(width: 6),
          AppText(
            '${desloc.isNegative ? '' : '+'}${tempoDaInfobar(desloc)}',
            key: const ValueKey('infobar-deslocamento'),
            style: estiloValor,
          ),
        ],
      );
    }
    final pares = info.pares.take(6).toList();
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          for (final (rotulo, valor) in pares)
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  AppText(rotulo, maxLines: 1, style: estiloRotulo),
                  AppText(
                    valor,
                    maxLines: 1,
                    overflow: TextOverflow.fade,
                    style: estiloValor,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

/// O MEDIDOR DE NIVEL: uma faixa que respira do centro para as bordas
/// no volume do som que esta tocando. Parado, nao desenha nada.
class _MedidorDeNivel extends ConsumerWidget {
  const _MedidorDeNivel({required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return IgnorePointer(
      child: ValueListenableBuilder<bool>(
        valueListenable: playback.playing,
        builder: (context, tocando, _) {
          if (!tocando) return const SizedBox.shrink();
          return ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (context, t, _) {
              final nivel = nivelDeAudioEm(
                ref.read(editorControllerProvider),
                t,
              );
              return CustomPaint(
                key: const ValueKey('medidor-de-nivel'),
                painter: _PintorDoMedidor(nivel),
              );
            },
          );
        },
      ),
    );
  }
}

class _PintorDoMedidor extends CustomPainter {
  const _PintorDoMedidor(this.nivel);

  final double nivel;

  @override
  void paint(Canvas canvas, Size size) {
    if (nivel <= 0.01) return;
    final meia = size.width / 2 * nivel;
    final centro = size.width / 2;
    final faixa = Rect.fromLTRB(centro - meia, 0, centro + meia, size.height);
    canvas.drawRect(
      faixa,
      Paint()
        ..shader = LinearGradient(
          colors: [
            CromoEditor.acao.withValues(alpha: 0),
            CromoEditor.acao.withValues(alpha: .16),
            CromoEditor.acao.withValues(alpha: 0),
          ],
        ).createShader(faixa),
    );
  }

  @override
  bool shouldRepaint(_PintorDoMedidor old) => old.nivel != nivel;
}

/// A BARRA FLUTUANTE DA SELECAO: aparar, dividir, keyframe, duplicar e
/// excluir da camada selecionada, num cartao sobre a timeline.
class BarraDaSelecao extends ConsumerWidget {
  const BarraDaSelecao({
    super.key,
    required this.playback,
    required this.layerId,
    required this.keyframeHere,
    required this.onKeyframe,
  });

  final PlaybackController playback;
  final String layerId;
  final bool keyframeHere;
  final VoidCallback onKeyframe;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    // O TEMPO SAI NA HORA DO TOQUE: lido no build, a barra cortava no
    // cabecote de QUANDO ELA APARECEU (a selecao), nao no de agora.
    Duration t() => playback.time.value;
    return Container(
      height: 46,
      padding: const EdgeInsets.symmetric(horizontal: 4),
      decoration: BoxDecoration(
        color: CromoEditor.trilho.withValues(alpha: .97),
        borderRadius: BorderRadius.circular(12),
        boxShadow: const [
          BoxShadow(color: Colors.black45, blurRadius: 10, offset: Offset(0, 3)),
        ],
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _BotaoDoCromo(
            key: const ValueKey('transport-keyframe'),
            icone: keyframeHere
                ? CupertinoIcons.rhombus_fill
                : CupertinoIcons.rhombus,
            dica: keyframeHere
                ? 'Remover keyframe no cabeçote'
                : 'Adicionar keyframe no cabeçote',
            cor: keyframeHere ? CromoEditor.keyframe : CromoEditor.branco,
            onTap: onKeyframe,
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-aparar-esq'),
            icone: CupertinoIcons.arrow_right_to_line,
            dica: 'Aparar o início até o cabeçote',
            onTap: () => controller.trimLayerStart(layerId, t()),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-dividir'),
            icone: CupertinoIcons.scissors,
            dica: 'Dividir a camada no cabeçote',
            onTap: () => controller.splitLayer(layerId, t()),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-aparar-dir'),
            icone: CupertinoIcons.arrow_left_to_line,
            dica: 'Aparar o fim até o cabeçote',
            onTap: () => controller.trimLayerEnd(layerId, t()),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-duplicar'),
            icone: CupertinoIcons.plus_square_on_square,
            dica: 'Duplicar camada',
            onTap: () => controller.duplicateLayer(layerId),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-excluir'),
            icone: CupertinoIcons.trash,
            dica: 'Excluir camada',
            onTap: () => excluirCamadas(context, ref, {layerId}),
          ),
        ],
      ),
    );
  }
}

/// A COLUNA DE OPCOES DE VISUALIZACAO (borda direita do palco, aberta
/// pelo olho da barra de reproducao): pixels, grade, solo, visao da
/// camera e o zoom do palco (+ · 100% · −; tocar no numero volta ao
/// ajustado). Segurar a camera abre a lista de cameras.
class ColunaDeVisualizacao extends ConsumerWidget {
  const ColunaDeVisualizacao({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final selected = ref.watch(selectedLayerProvider);
    final project = ref.watch(editorControllerProvider);
    final soloAtivo = selected != null && project.metaOf(selected).solo;
    final zoom = ref.watch(zoomDoPalcoProvider);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final notifier = ref.read(opcoesDeVisualizacaoProvider.notifier);

    void mudarZoom(double fator) {
      final novo = (zoom * fator).clamp(0.25, 4.0);
      ref.read(zoomDoPalcoProvider.notifier).state = novo;
    }

    CameraLayer? primeiraCamera(List<Layer> ls) {
      for (final l in ls) {
        if (l is CameraLayer) return l;
        if (l is GroupLayer) {
          final achada = primeiraCamera(l.children);
          if (achada != null) return achada;
        }
      }
      return null;
    }

    final camera = primeiraCamera(project.layers);

    // A COLUNA CABE NO PALCO: num celular o palco tem pouco mais de 200
    // pontos de altura, e sete alvos de 44 passavam por cima da barra de
    // reproducao — o toque ia parar nela. Os botoes dividem a altura que
    // ha (quatro chaves, dois de zoom e o numero valendo meio).
    return LayoutBuilder(
      builder: (context, c) {
        final h = c.maxHeight.isFinite
            ? ((c.maxHeight - 18) / 6.6).floorToDouble().clamp(18.0, 44.0)
            : 44.0;
        return _colunaDeVisualizacao(
          context,
          ref,
          h: h,
          camera: camera,
          soloAtivo: soloAtivo,
          selected: selected,
          zoom: zoom,
          opcoes: opcoes,
          notifier: notifier,
          mudarZoom: mudarZoom,
        );
      },
    );
  }

  Widget _colunaDeVisualizacao(
    BuildContext context,
    WidgetRef ref, {
    required double h,
    required CameraLayer? camera,
    required bool soloAtivo,
    required String? selected,
    required double zoom,
    required OpcoesDeVisualizacao opcoes,
    required OpcoesDeVisualizacaoNotifier notifier,
    required void Function(double) mudarZoom,
  }) {
    return Container(
      key: const ValueKey('coluna-de-visualizacao'),
      width: 40,
      padding: const EdgeInsets.symmetric(vertical: 4),
      decoration: const BoxDecoration(
        color: CromoEditor.trilho,
        borderRadius: BorderRadius.only(
          topLeft: Radius.circular(12),
          bottomLeft: Radius.circular(12),
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          _BotaoDoCromo(
            key: const ValueKey('visao-pixels'),
            altura: h,
            icone: CupertinoIcons.square_grid_3x2,
            dica: opcoes.pixels
                ? 'Pixels reais ligados'
                : 'Pixels reais (resolução cheia e grade de pixels no zoom)',
            cor: opcoes.pixels ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 18,
            onTap: notifier.alternarPixels,
          ),
          _BotaoDoCromo(
            key: const ValueKey('visao-grade'),
            altura: h,
            icone: CupertinoIcons.grid,
            dica: opcoes.grade ? 'Esconder a grade' : 'Mostrar a grade',
            cor: opcoes.grade ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 18,
            onTap: notifier.alternarGrade,
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-solo'),
            altura: h,
            icone: Icons.center_focus_strong,
            dica: soloAtivo ? 'Tirar do solo' : 'Solo da camada selecionada',
            cor: soloAtivo ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 19,
            onTap: selected == null
                ? null
                : () => ref
                      .read(editorControllerProvider.notifier)
                      .toggleSolo(selected),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-camera'),
            altura: h,
            icone: opcoes.visaoDaCamera
                ? CupertinoIcons.videocam_fill
                : CupertinoIcons.videocam,
            dica: camera == null
                ? 'Visão da câmera (adicione uma em + › Objeto)'
                : (opcoes.visaoDaCamera
                      ? 'Vendo pela câmera · toque para a vista livre, '
                            'segure para as câmeras'
                      : 'Vista livre · toque para ver pela câmera'),
            cor: camera != null && opcoes.visaoDaCamera
                ? CromoEditor.acao
                : CromoEditor.branco,
            tamanho: 18,
            onTap: () {
              if (camera == null) {
                AureaSnack.show(
                  context,
                  'Adicione uma Câmera (+ › Objeto) primeiro',
                );
                return;
              }
              notifier.alternarVisaoDaCamera();
            },
            onLongPress: camera == null
                ? null
                : () => showCamerasSheet(context, ref, camera.id, playback),
          ),
          Container(
            width: 22,
            height: 1,
            margin: const EdgeInsets.symmetric(vertical: 3),
            color: CromoEditor.apagado.withValues(alpha: .2),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-mais'),
            altura: h,
            icone: Icons.zoom_in,
            dica: 'Aproximar o palco',
            tamanho: 19,
            onTap: zoom >= 4.0 ? null : () => mudarZoom(1.25),
          ),
          Tooltip(
            message: 'Voltar ao ajustado',
            child: Tocavel(
              key: const ValueKey('rail-zoom-texto'),
              onTap: zoom == 1.0
                  ? null
                  : () => ref.read(zoomDoPalcoProvider.notifier).state = 1.0,
              child: SizedBox(
                width: 40,
                height: h * .6,
                child: Center(
                  child: AppText(
                    '${(zoom * 100).round()}%',
                    style: TextStyle(
                      fontSize: 9.5,
                      color: zoom == 1.0
                          ? CromoEditor.apagado
                          : CromoEditor.acao,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
            ),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-menos'),
            altura: h,
            icone: Icons.zoom_out,
            dica: 'Afastar o palco',
            tamanho: 19,
            onTap: zoom <= 0.25 ? null : () => mudarZoom(0.8),
          ),
        ],
      ),
    );
  }
}

/// O ZOOM SOBRE O PALCO: com o palco fora do ajustado, o numero fica no
/// canto de cima a esquerda; tocar volta ao ajustado.
class IndicadorDeZoomDoPalco extends ConsumerWidget {
  const IndicadorDeZoomDoPalco({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final zoom = ref.watch(zoomDoPalcoProvider);
    if (zoom == 1.0) return const SizedBox.shrink();
    return Tooltip(
      message: 'Voltar ao ajustado',
      child: Tocavel(
        key: const ValueKey('preview-zoom-indicador'),
        onTap: () => ref.read(zoomDoPalcoProvider.notifier).state = 1.0,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
          decoration: BoxDecoration(
            color: Colors.black.withValues(alpha: .55),
            borderRadius: BorderRadius.circular(999),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.zoom_in, size: 14, color: CromoEditor.branco),
              const SizedBox(width: 4),
              AppText(
                '${(zoom * 100).round()}%',
                style: const TextStyle(
                  color: CromoEditor.branco,
                  fontSize: 11.5,
                  fontWeight: FontWeight.w700,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// O ⋮ DA TIMELINE (canto inferior esquerdo): marcas, batidas, agrupar
/// e o guia.
Future<void> menuDaTimeline(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback, {
  required VoidCallback onAgrupar,
  required VoidCallback onGuia,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    builder: (ctx) => CupertinoActionSheet(
      actions: [
        CupertinoActionSheetAction(
          key: const ValueKey('timeline-menu-marcas'),
          onPressed: () async {
            Navigator.pop(ctx);
            await menuDasMarcas(context, ref, playback);
          },
          child: const AppText('Marcas na timeline'),
        ),
        CupertinoActionSheetAction(
          key: const ValueKey('timeline-menu-batidas'),
          onPressed: () async {
            Navigator.pop(ctx);
            final som = ref
                .read(editorControllerProvider)
                .layers
                .where((l) => l is AudioLayer || l is VideoLayer)
                .firstOrNull;
            if (som == null) {
              AureaSnack.show(
                context,
                'Adicione um áudio ou um vídeo primeiro',
              );
              return;
            }
            playback.pause();
            await showBeatsSheet(context, ref, som.id);
          },
          child: const AppText('Batidas da música'),
        ),
        CupertinoActionSheetAction(
          key: const ValueKey('timeline-menu-agrupar'),
          onPressed: () {
            Navigator.pop(ctx);
            onAgrupar();
          },
          child: const AppText('Agrupar camadas…'),
        ),
        CupertinoActionSheetAction(
          key: const ValueKey('timeline-menu-guia'),
          onPressed: () {
            Navigator.pop(ctx);
            onGuia();
          },
          child: const AppText('Guia rápido'),
        ),
      ],
      cancelButton: CupertinoActionSheetAction(
        onPressed: () => Navigator.pop(ctx),
        child: const AppText('Cancelar'),
      ),
    ),
  );
}

/// IR PARA O TEMPO — o relogio da navbar aceita "12.5", "1:02.5" etc.
Future<void> digitarTempo(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
) async {
  playback.pause();
  final project = ref.read(editorControllerProvider);
  final total = project.duration;
  final fps = project.fps;
  final ctrl = TextEditingController(
    text: (playback.time.value.inMilliseconds / 1000).toStringAsFixed(2),
  );
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: const AppText('Ir para o tempo'),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('transport-timecode-campo'),
          controller: ctrl,
          autofocus: true,
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          placeholder: translate(context, 'segundos, ou mm:ss.ms'),
          onSubmitted: (v) => Navigator.pop(ctx, v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('Ir'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  final t = parseTimecodeInput(r ?? '', fps);
  if (t == null) return;
  playback.seek(t < Duration.zero ? Duration.zero : (t > total ? total : t));
}
