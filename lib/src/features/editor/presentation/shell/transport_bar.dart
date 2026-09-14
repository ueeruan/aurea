import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/tokens.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../am/am_colors.dart';
import 'layer_actions.dart';

/// ZONA C — O TRANSPORTE (48 pt).
///
/// `|◀ ▶ ▶|` · timecode atual / total (toque = digitar) · loop · ◆
/// keyframe · marca. So reproducao e tempo: desfazer mora na barra de
/// cima e duplicar mora nas acoes da camada (secao 4C do prompt).
class EditorTransportBar extends ConsumerWidget {
  const EditorTransportBar({
    super.key,
    required this.playback,
    required this.onKeyframe,
    required this.keyframeHere,
    required this.keyframeEnabled,
    this.onAdd,
  });

  /// O "+" unico do editor (adicionar camada). Mora aqui, no fim do
  /// transporte, para nunca cobrir uma linha da timeline.
  final VoidCallback? onAdd;

  final PlaybackController playback;

  /// Crava ou tira o keyframe da propriedade ativa no instante atual.
  final VoidCallback onKeyframe;
  final bool keyframeHere;
  final bool keyframeEnabled;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = AureaTokens.of(context);
    final duration = ref.watch(
      editorControllerProvider.select((p) => p.duration),
    );
    final fps = ref.watch(editorControllerProvider.select((p) => p.fps));
    final controller = ref.read(editorControllerProvider.notifier);
    final selected = ref.watch(selectedLayerProvider);
    final project = ref.watch(editorControllerProvider);
    // OS KEYFRAMES DA CAMADA SELECIONADA, em tempo do projeto.
    //
    // "Quando voce poe um keyframe e clica aqui, tinha de ir para o
    // keyframe, e nao para o fim do projeto" — o relato dos testadores,
    // e e o que o Alight faz: com uma camada animada, |◀ e ▶| andam de
    // marca em marca. Sem camada, ou sem marca do lado, vao ao comeco e
    // ao fim como antes.
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
    Widget botao({
      required Key key,
      required IconData icon,
      required String tooltip,
      required VoidCallback? onTap,
      Color? cor,
      double size = 22,
      VoidCallback? onLongPress,
    }) => Tooltip(
      message: tooltip,
      child: GestureDetector(
        key: key,
        behavior: HitTestBehavior.opaque,
        onTap: onTap == null
            ? null
            : () {
                HapticFeedback.lightImpact();
                onTap();
              },
        onLongPress: onLongPress == null
            ? null
            : () {
                HapticFeedback.mediumImpact();
                onLongPress();
              },
        child: SizedBox(
          width: AureaTokens.minTap,
          height: AureaTokens.minTap,
          child: Icon(
            icon,
            size: size,
            color:
                cor ?? (onTap == null ? t.muted.withValues(alpha: .5) : t.text),
          ),
        ),
      ),
    );

    // OITO BOTOES NUM CELULAR DE 320 PX. Cada um mede 44 (o alvo minimo),
    // e oito vezes 44 sao 352: a tesoura nova estourava a fileira em 32
    // pixels no aparelho mais estreito. Aqui cada botao recebe uma fatia
    // igual da largura que existe — 44 quando cabe, e nunca menos de 36,
    // que ainda e um alvo que o dedo acerta.
    return Container(
      height: AureaTokens.transport,
      color: t.surface,
      child: LayoutBuilder(
        builder: (context, c) {
          final filhos = <Widget>[
            botao(
              key: const ValueKey('editor-undo'),
              icon: CupertinoIcons.arrow_uturn_left,
              tooltip: 'Desfazer',
              onTap: controller.canUndo ? controller.undo : null,
            ),
            botao(
              key: const ValueKey('editor-redo'),
              icon: CupertinoIcons.arrow_uturn_right,
              tooltip: 'Refazer',
              onTap: controller.canRedo ? controller.redo : null,
            ),
            botao(
              key: const ValueKey('transport-start'),
              icon: CupertinoIcons.backward_end,
              tooltip: marcas.isEmpty
                  ? 'Início · segure para marcas'
                  : 'Keyframe anterior · segure para marcas',
              onTap: () => playback.seek(
                anterior(playback.time.value) ?? Duration.zero,
              ),
              onLongPress: () => menuDasMarcas(context, ref, playback),
            ),
            ListenableBuilder(
              listenable: Listenable.merge([playback.playing, playback.loop]),
              builder: (context, _) => botao(
                key: const ValueKey('transport-play'),
                icon: playback.playing.value
                    ? CupertinoIcons.pause_fill
                    : CupertinoIcons.play_fill,
                tooltip: playback.loop.value
                    ? 'Repetição ligada · segure para desligar'
                    : (playback.playing.value ? 'Pausar' : 'Reproduzir'),
                cor: playback.loop.value ? t.accent : t.text,
                size: 24,
                onTap: playback.toggle,
                onLongPress: () => playback.loop.value = !playback.loop.value,
              ),
            ),
            botao(
              key: const ValueKey('transport-end'),
              icon: CupertinoIcons.forward_end,
              tooltip: marcas.isEmpty
                  ? 'Fim · segure para ir ao tempo'
                  : 'Próximo keyframe · segure para ir ao tempo',
              onTap: () =>
                  playback.seek(proxima(playback.time.value) ?? duration),
              onLongPress: () =>
                  _digitarTempo(context, playback, duration, fps),
            ),
            botao(
              key: const ValueKey('transport-keyframe'),
              icon: keyframeHere
                  ? CupertinoIcons.rhombus_fill
                  : CupertinoIcons.rhombus,
              tooltip: keyframeHere
                  ? 'Remover keyframe no cabeçote'
                  : 'Adicionar keyframe no cabeçote',
              cor: !keyframeEnabled
                  ? t.muted.withValues(alpha: .35)
                  : (keyframeHere ? AmColors.accent : t.text),
              size: 24,
              onTap: keyframeEnabled ? onKeyframe : null,
            ),
            botao(
              key: const ValueKey('camada-duplicar'),
              icon: CupertinoIcons.plus_square_on_square,
              tooltip: 'Duplicar camada',
              onTap: selected == null
                  ? null
                  : () => controller.duplicateLayer(selected),
            ),
            // DIVIDIR SEMPRE A VISTA. Estava numa fila de acoes que a
            // largura da tela escondia atras de "Mais" — e cortar no
            // cabecote e o gesto mais comum de um editor. Aqui, no
            // transporte, ele nunca sai da tela.
            botao(
              key: const ValueKey('camada-dividir'),
              icon: CupertinoIcons.scissors,
              tooltip: 'Dividir a camada no cabeçote',
              onTap: selected == null
                  ? null
                  : () => controller.splitLayer(selected, playback.time.value),
            ),
            botao(
              key: const ValueKey('transport-expand'),
              // TELA CHEIA com cara de tela cheia. O visor nao dizia o que
              // fazia: "e tela cheia tambem" pedia quem nunca achou o botao.
              icon: ref.watch(editorSessionProvider).previewExpanded
                  ? CupertinoIcons.fullscreen_exit
                  : CupertinoIcons.fullscreen,
              tooltip: ref.watch(editorSessionProvider).previewExpanded
                  ? 'Sair da tela cheia'
                  : 'Tela cheia',
              onTap: () => ref
                  .read(editorSessionProvider.notifier)
                  .togglePreviewExpanded(),
            ),
          ];
          final fatia = c.maxWidth.isFinite && filhos.isNotEmpty
              ? (c.maxWidth / filhos.length).clamp(0.0, AureaTokens.minTap)
              : AureaTokens.minTap;
          Widget caixa(Widget f) => SizedBox(
            width: fatia,
            child: Center(child: f),
          );
          // TRES GRUPOS, e a reproducao no CENTRO da tela. Os nove
          // botoes repartidos por igual deixavam o play deslocado para
          // a esquerda; o pedido dos testadores foi o arranjo da
          // referencia: desfazer/refazer a esquerda, |◀ ▶ ▶| no meio,
          // e o que age na camada a direita.
          //
          // A conta de largura: o trio do meio fica no centro EXATO quando
          // as laterais cabem com alvos de 36 pt (a partir de 420 px);
          // abaixo disso as laterais dividem a largura na proporcao de
          // botoes, e o play fica um pouco a esquerda do centro em vez de
          // estourar a fileira num aparelho de 375 px.
          final esquerda = filhos.sublist(0, 2);
          final meio = filhos.sublist(2, 5);
          final direita = filhos.sublist(5);
          final trio = fatia * meio.length;
          // O PISO DE 40 PT VALE PARA TODO BOTAO. O trio fica o mais
          // perto do centro que os pisos permitem: a folga vai primeiro
          // para o lado que empurra o play para o meio.
          const piso = 40.0;
          final minEsq = esquerda.length * piso;
          final minDir = direita.length * piso;
          final cabeCentrado =
              c.maxWidth.isFinite && c.maxWidth >= trio + minEsq + minDir;
          if (cabeCentrado) {
            final idealEsq = (c.maxWidth - trio) / 2;
            final esq = idealEsq.clamp(minEsq, c.maxWidth - trio - minDir);
            final dir = c.maxWidth - trio - esq;
            final fatiaEsq = (esq / esquerda.length).clamp(
              piso,
              AureaTokens.minTap,
            );
            final fatiaDir = (dir / direita.length).clamp(
              piso,
              AureaTokens.minTap,
            );
            Widget lateral(Widget f, double largura) => SizedBox(
              width: largura,
              child: Center(child: f),
            );
            return Row(
              children: [
                SizedBox(
                  width: esq,
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.start,
                    children: [for (final f in esquerda) lateral(f, fatiaEsq)],
                  ),
                ),
                for (final f in meio) caixa(f),
                SizedBox(
                  width: dir,
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.end,
                    children: [for (final f in direita) lateral(f, fatiaDir)],
                  ),
                ),
              ],
            );
          }
          return Row(
            children: [
              Expanded(
                flex: esquerda.length,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.start,
                  children: [for (final f in esquerda) caixa(f)],
                ),
              ),
              for (final f in meio) caixa(f),
              Expanded(
                flex: direita.length,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [for (final f in direita) caixa(f)],
                ),
              ),
            ],
          );
        },
      ),
    );
  }

  Future<void> _digitarTempo(
    BuildContext context,
    PlaybackController playback,
    Duration total,
    int fps,
  ) async {
    playback.pause();
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
}

/// Le "12.5", "1:02.5", "00:01:02:15" (o ultimo campo em quadros quando
/// ha tres separadores).
Duration? parseTimecodeInput(String texto, int fps) {
  final s = texto.trim().replaceAll(',', '.');
  if (s.isEmpty) return null;
  final partes = s.split(':');
  try {
    if (partes.length == 1) {
      return Duration(microseconds: (double.parse(partes[0]) * 1e6).round());
    }
    if (partes.length == 2) {
      final m = int.parse(partes[0]);
      final seg = double.parse(partes[1]);
      return Duration(microseconds: ((m * 60 + seg) * 1e6).round());
    }
    if (partes.length == 3) {
      final h = int.parse(partes[0]);
      final m = int.parse(partes[1]);
      final seg = double.parse(partes[2]);
      return Duration(microseconds: ((h * 3600 + m * 60 + seg) * 1e6).round());
    }
    if (partes.length == 4) {
      final h = int.parse(partes[0]);
      final m = int.parse(partes[1]);
      final seg = int.parse(partes[2]);
      final q = int.parse(partes[3]);
      return Duration(
        microseconds:
            ((h * 3600 + m * 60 + seg) * 1e6).round() +
            (q * 1e6 / (fps <= 0 ? 30 : fps)).round(),
      );
    }
  } catch (_) {
    return null;
  }
  return null;
}
