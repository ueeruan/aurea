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
import '../../domain/layer.dart';
import '../../domain/layout_ops.dart';
import '../am/align_sheet.dart';
import '../am/beats_sheet.dart';
import '../am/cameras_sheet.dart';
import '../am/export_sheet.dart';
import 'layer_actions.dart';
import 'project_settings_sheet.dart';
import 'transport_bar.dart' show parseTimecodeInput;

/// O CROMO DO EDITOR NA LINGUA DO ALIGHT MOTION 5 (v1.1.1).
///
/// A pedido do dono, a casca de edicao segue a planta do AM 5 lida do
/// proprio APK (base.apk, layouts decodificados): navbar de 44 no topo
/// (sair · titulo editavel · tempo · projeto · exportar), selectbar
/// VERDE no lugar dela quando ha multi-selecao, playbar de 46 sobre a
/// timeline (desfazer/refazer · quadro-play-quadro · colar · marcas ·
/// tela cheia), trilho vertical a direita do palco (solo · camera ·
/// zoom do palco) e a barra flutuante de aparar/dividir quando ha
/// camada selecionada. Os MOTORES por baixo sao todos nossos; so a
/// casca fala AM. Nenhum asset do APK foi copiado — icones sao os
/// nossos (Cupertino/Material) e as cores vem da paleta abaixo.
abstract final class CromoAM {
  // A paleta lida do resources.arsc do AM 5 (nomes deles entre aspas).
  static const Color fundo = Color(0xFF191B27); // "A10" — barras
  static const Color palco = Color(0xFF141620); // "A11" — atras do palco
  static const Color trilho = Color(0xFF333750); // "S9" — trilho/pilulas
  static const Color verde = Color(0xFF00FFA8); // "Y1" — acento/selecao
  static const Color sobreVerde = Color(0xFF0F1418); // "K2" — icone no verde
  static const Color branco = Color(0xFFFFFFFF); // "W1"
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
  });

  final IconData icone;
  final String dica;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final Color? cor;
  final double tamanho;
  final double largura;

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
          height: 44,
          child: Icon(
            icone,
            size: tamanho,
            color: cor ??
                (onTap == null
                    ? CromoAM.apagado.withValues(alpha: .25)
                    : CromoAM.branco),
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

/// A NAVBAR DO AM: sair · titulo (toque renomeia; dentro de grupo, o
/// chip sai do grupo) · tempo corrente/total (toque digita) · projeto ·
/// exportar.
class AmNavbar extends ConsumerWidget {
  const AmNavbar({super.key, required this.onBack, required this.playback});

  final VoidCallback onBack;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final dentroDeGrupo = controller.dentroDeGrupo;
    return Container(
      height: CromoAM.navbar,
      color: CromoAM.fundo,
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
                // A porta com a seta do AM: sair do projeto.
                child: Transform.flip(
                  flipX: true,
                  child: const Icon(
                    Icons.logout,
                    size: 20,
                    color: CromoAM.branco,
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
                    color: CromoAM.trilho,
                    borderRadius: BorderRadius.circular(13),
                  ),
                  child: const Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        CupertinoIcons.chevron_left,
                        size: 12,
                        color: CromoAM.verde,
                      ),
                      SizedBox(width: 2),
                      Icon(
                        CupertinoIcons.rectangle_stack,
                        size: 14,
                        color: CromoAM.verde,
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
                  color: CromoAM.branco,
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
          ),
          // O editTimeView do AM: o relogio mora na navbar.
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
                      color: CromoAM.apagado,
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
            cor: CromoAM.verde,
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

/// A SELECTBAR VERDE DO AM: quando ha multi-selecao, o topo inteiro
/// vira verde com as acoes do lote — agrupar, alinhar na tela, excluir.
/// Toque longo num alinhamento abre a folha completa (distribuir etc.).
class AmSelectbar extends ConsumerWidget {
  const AmSelectbar({super.key, required this.playback});

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
          cor: CromoAM.sobreVerde,
          tamanho: 18,
          largura: 30,
          onTap: () => controller.alignSelection(ids, edge, t),
          onLongPress: () => showAlignSheet(context, ref, ids, t),
        );

    return Container(
      height: CromoAM.navbar,
      color: CromoAM.verde,
      child: Row(
        children: [
          _BotaoDoCromo(
            key: const ValueKey('selectbar-cancelar'),
            icone: CupertinoIcons.xmark,
            dica: 'Cancelar seleção',
            cor: CromoAM.sobreVerde,
            onTap: () =>
                ref.read(multiSelectProvider.notifier).state = const {},
          ),
          Expanded(
            child: AppText(
              '${multi.length} selecionadas',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                color: CromoAM.sobreVerde,
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
            cor: CromoAM.sobreVerde,
            largura: 36,
            onTap: multi.length >= 2
                ? () => agruparSelecao(ref, multi)
                : null,
          ),
          _BotaoDoCromo(
            key: const ValueKey('selectbar-excluir'),
            icone: CupertinoIcons.trash,
            dica: 'Excluir seleção',
            cor: CromoAM.sobreVerde,
            largura: 36,
            onTap: () => excluirCamadas(context, ref, multi),
          ),
          const SizedBox(width: 4),
        ],
      ),
    );
  }
}

/// A PLAYBAR DO AM (46, sobre a timeline): desfazer/refazer a esquerda,
/// quadro-play-quadro no centro, colar/marcas/tela-cheia a direita.
///
/// Os saltos: toque em |◀ ▶| anda por KEYFRAME quando a camada
/// selecionada tem marcas (o pedido dos testadores), senao UM QUADRO
/// (como no AM); segurar vai ao inicio/fim.
class AmPlaybar extends ConsumerWidget {
  const AmPlaybar({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.watch(editorControllerProvider);
    final selected = ref.watch(selectedLayerProvider);
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
      height: CromoAM.playbar,
      color: CromoAM.fundo,
      child: Row(
        children: [
          _BotaoDoCromo(
            key: const ValueKey('editor-undo'),
            icone: CupertinoIcons.arrow_uturn_left,
            dica: 'Desfazer',
            onTap: controller.canUndo ? controller.undo : null,
          ),
          _BotaoDoCromo(
            key: const ValueKey('editor-redo'),
            icone: CupertinoIcons.arrow_uturn_right,
            dica: 'Refazer',
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
                  builder: (context, _) => _BotaoDoCromo(
                    key: const ValueKey('transport-play'),
                    icone: playback.playing.value
                        ? CupertinoIcons.pause_fill
                        : CupertinoIcons.play_fill,
                    dica: playback.loop.value
                        ? 'Repetição ligada · segure para desligar'
                        : (playback.playing.value
                              ? 'Pausar'
                              : 'Reproduzir · segure para repetir'),
                    cor: playback.loop.value ? CromoAM.verde : CromoAM.branco,
                    tamanho: 26,
                    largura: 52,
                    onTap: playback.toggle,
                    onLongPress: () =>
                        playback.loop.value = !playback.loop.value,
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
            onTap: selected == null && !controller.temEfeitosCopiados
                ? null
                : () => _menuDeColar(context, ref, selected),
          ),
          _BotaoDoCromo(
            key: const ValueKey('playbar-marcas'),
            icone: CupertinoIcons.bookmark,
            dica: 'Marcas na timeline',
            onTap: () => menuDasMarcas(context, ref, playback),
          ),
          _BotaoDoCromo(
            key: const ValueKey('transport-expand'),
            icone: expandido
                ? CupertinoIcons.fullscreen_exit
                : CupertinoIcons.fullscreen,
            dica: expandido ? 'Sair da tela cheia' : 'Tela cheia',
            onTap: () =>
                ref.read(editorSessionProvider.notifier).togglePreviewExpanded(),
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

/// A BARRA FLUTUANTE DA SELECAO (o select_bottomvar do AM): aparar,
/// dividir, keyframe, duplicar e excluir da camada selecionada, num
/// cartao sobre a timeline.
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
    final t = playback.time.value;
    return Container(
      height: 46,
      padding: const EdgeInsets.symmetric(horizontal: 4),
      decoration: BoxDecoration(
        color: CromoAM.trilho.withValues(alpha: .97),
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
            cor: keyframeHere ? CromoAM.verde : CromoAM.branco,
            onTap: onKeyframe,
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-aparar-esq'),
            icone: CupertinoIcons.arrow_right_to_line,
            dica: 'Aparar o início até o cabeçote',
            onTap: () => controller.trimLayerStart(layerId, t),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-dividir'),
            icone: CupertinoIcons.scissors,
            dica: 'Dividir a camada no cabeçote',
            onTap: () => controller.splitLayer(layerId, t),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-aparar-dir'),
            icone: CupertinoIcons.arrow_left_to_line,
            dica: 'Aparar o fim até o cabeçote',
            onTap: () => controller.trimLayerEnd(layerId, t),
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

/// O TRILHO DA DIREITA DO PALCO (o previewmode do AM): solo, cameras e
/// o zoom do palco (− · 100% · +; tocar no numero volta ao ajustado).
class TrilhoDoPalco extends ConsumerWidget {
  const TrilhoDoPalco({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final selected = ref.watch(selectedLayerProvider);
    final project = ref.watch(editorControllerProvider);
    final soloAtivo = selected != null && project.metaOf(selected).solo;
    final zoom = ref.watch(zoomDoPalcoProvider);

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

    return Container(
      width: 40,
      padding: const EdgeInsets.symmetric(vertical: 4),
      decoration: const BoxDecoration(
        color: CromoAM.trilho,
        borderRadius: BorderRadius.only(
          topLeft: Radius.circular(12),
          bottomLeft: Radius.circular(12),
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          _BotaoDoCromo(
            key: const ValueKey('rail-solo'),
            icone: Icons.center_focus_strong,
            dica: soloAtivo ? 'Tirar do solo' : 'Solo da camada selecionada',
            cor: soloAtivo ? CromoAM.verde : CromoAM.branco,
            tamanho: 19,
            onTap: selected == null
                ? null
                : () => ref
                      .read(editorControllerProvider.notifier)
                      .toggleSolo(selected),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-camera'),
            icone: CupertinoIcons.videocam_fill,
            dica: 'Câmeras',
            tamanho: 18,
            onTap: () {
              final camera = primeiraCamera(project.layers);
              if (camera == null) {
                AureaSnack.show(
                  context,
                  'Adicione uma Câmera (+ › Objeto) primeiro',
                );
                return;
              }
              showCamerasSheet(context, ref, camera.id, playback);
            },
          ),
          Container(
            width: 22,
            height: 1,
            margin: const EdgeInsets.symmetric(vertical: 3),
            color: CromoAM.apagado.withValues(alpha: .2),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-mais'),
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
                height: 26,
                child: Center(
                  child: AppText(
                    '${(zoom * 100).round()}%',
                    style: TextStyle(
                      fontSize: 9.5,
                      color: zoom == 1.0 ? CromoAM.apagado : CromoAM.verde,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
            ),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-menos'),
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

/// O ⋮ DA TIMELINE (canto inferior esquerdo, como no AM): marcas,
/// batidas, agrupar e o guia.
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
