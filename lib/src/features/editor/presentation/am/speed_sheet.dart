import 'package:aurea/src/core/l10n/app_language.dart';

import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/storage/prefs.dart';
import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../application/proxy_service.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/layer.dart';
import '../../domain/velocidade.dart';
import '../context/parameter_row.dart' show showNumberInput;
import 'am_colors.dart';
import '../../../../core/ui/tocavel.dart';
import 'am_widgets.dart';
import 'estudio_do_tempo.dart';

const _chaveDaCompensacao = 'velocidade.compensacao';

/// O modo de compensacao da ultima vez (Estender fim na primeira).
CompensacaoDaVelocidade compensacaoLembrada(WidgetRef ref) {
  try {
    final i = ref.read(sharedPreferencesProvider).getInt(_chaveDaCompensacao);
    if (i != null && i >= 0 && i < CompensacaoDaVelocidade.values.length) {
      return CompensacaoDaVelocidade.values[i];
    }
  } catch (_) {}
  return CompensacaoDaVelocidade.estenderFim;
}

void _lembrarCompensacao(WidgetRef ref, CompensacaoDaVelocidade c) {
  try {
    ref.read(sharedPreferencesProvider).setInt(_chaveDaCompensacao, c.index);
  } catch (_) {}
}

/// A FOLHA "TEMPO" da camada: a porta do Time Remap no topo, e abaixo a
/// velocidade constante com o modo de compensacao, as rampas prontas, o
/// reverso, o blur temporal e a interpolacao de quadros.
///
/// O Time Remap (curva, keyframes de tempo, congelar, reverso por trecho)
/// mora no Estudio do tempo; esta folha e o caminho ate ele. A porta ja
/// foi apagada duas vezes (da folha em `aba36bb`, da galeria em `7e7c294`)
/// e o recurso ficou sem chamador nenhum — `time_remap_porta_test.dart`
/// existe para isso nao se repetir em silencio.
Future<void> showSpeedSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  PlaybackController? playback,
}) async {
  var modo = compensacaoLembrada(ref);
  await showParamSheet(
    context,
    title: 'Tempo e velocidade',
    heightFactor: 0.84,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer == null) return const SizedBox.shrink();

        if (layer is! VideoLayer && layer is! AudioLayer) {
          return const Padding(
            key: ValueKey('velocidade-sem-midia'),
            padding: EdgeInsets.fromLTRB(18, 18, 18, 24),
            child: AppText(
              'A velocidade vale para vídeo e áudio. Nas outras camadas, '
              'aproxime ou afaste os keyframes para animar mais rápido ou '
              'mais devagar.',
              style: TextStyle(
                fontSize: 13,
                height: 1.4,
                color: AmColors.muted,
              ),
            ),
          );
        }

        final speed = controller.clipSpeedOf(layerId);
        final video = layer is VideoLayer ? layer : null;
        final temCurva = video != null && hasTimeRemap(video);
        final audio = switch (layer) {
          VideoLayer v => v.audio,
          AudioLayer a => a.audio,
          _ => const AudioSpec(),
        };
        final hasSound =
            layer is AudioLayer ||
            (layer is VideoLayer && layer.volume > 0.001);

        void setSpeed(double value) {
          controller.setClipSpeed(layerId, value, modo: modo);
          setSheetState(() {});
        }

        void setReverse(bool value) {
          if (video == null) return;
          controller.setClipReverse(layerId, value);
          setSheetState(() {});
          // Frame-driven playback works from the source immediately. The
          // optional short-GOP cache only accelerates subsequent seeks.
          if (value) {
            unawaited(
              ProxyService.instance.ensureProxy(video.sourcePath, force: true),
            );
          }
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                AppText(
                  '${speed.toStringAsFixed(2)}x · ${formatTime(layer.duration)}',
                  style: TextStyle(fontSize: 12, color: AmColors.accent),
                ),
                if (video != null) ...[
                  const SizedBox(height: 10),
                  // NO TOPO, e nao no fim da folha: quem entra em "Tempo"
                  // atras do Time Remap nao pode ter de rolar para acha-lo.
                  _PortaDoTimeRemap(
                    video: video,
                    onTap: () async {
                      await showEstudioDoTempo(
                        sheetContext,
                        ref,
                        layerId,
                        playback,
                      );
                      if (sheetContext.mounted) setSheetState(() {});
                    },
                  ),
                ],
                const SizedBox(height: 10),
                _LinhaDeCompensacao(
                  modo: modo,
                  onEscolher: (novo) {
                    modo = novo;
                    _lembrarCompensacao(ref, novo);
                    setSheetState(() {});
                  },
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    const Icon(
                      CupertinoIcons.tortoise,
                      size: 20,
                      color: AmColors.muted,
                    ),
                    const SizedBox(width: 6),
                    Expanded(
                      child: ReguaDaVelocidade(
                        key: const ValueKey('velocidade-regua'),
                        valor: speed,
                        onChanged: setSpeed,
                      ),
                    ),
                    const SizedBox(width: 6),
                    const Icon(
                      CupertinoIcons.hare,
                      size: 20,
                      color: AmColors.muted,
                    ),
                    const SizedBox(width: 8),
                    Tocavel(
                      key: const ValueKey('velocidade-valor'),
                      onTap: () async {
                        final v = await showNumberInput(
                          sheetContext,
                          value: speed,
                          unit: 'x',
                          min: velocidadeMinima,
                          max: velocidadeMaxima,
                          decimals: 2,
                          title: 'Velocidade',
                        );
                        if (v != null) setSpeed(v);
                      },
                      child: Container(
                        width: 64,
                        height: 34,
                        alignment: Alignment.center,
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: AppText(
                          '${speed.toStringAsFixed(2)}x',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w700,
                            color: AmColors.accent,
                            fontFeatures: [FontFeature.tabularFigures()],
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    for (final preset in const [0.5, 1.0, 2.0])
                      _SpeedChip(
                        label: preset == 1 ? '1x' : '${preset}x',
                        selected: !temCurva && (speed - preset).abs() < 0.01,
                        onTap: () => setSpeed(preset),
                      ),
                    if (video != null)
                      for (final preset in SpeedRampPreset.values)
                        _SpeedChip(
                          label: preset.label,
                          selected: false,
                          onTap: () {
                            controller.applySpeedRamp(layerId, preset);
                            setSheetState(() {});
                          },
                        ),
                  ],
                ),
                if (hasSound) ...[
                  const SizedBox(height: 8),
                  _ToggleRow(
                    label: 'Manter tom do audio',
                    value: audio.preservePitch,
                    onChanged: (value) {
                      controller.setClipPreservePitch(layerId, value);
                      setSheetState(() {});
                    },
                  ),
                ],
                if (video != null) ...[
                  _ToggleRow(
                    label: 'Reverso',
                    value: video.reverse,
                    onChanged: setReverse,
                  ),
                  _ToggleRow(
                    label: 'Blur proporcional a velocidade',
                    value: video.speedBlur,
                    onChanged: (value) {
                      controller.setClipSpeedBlur(layerId, value);
                      setSheetState(() {});
                    },
                  ),
                  const SizedBox(height: 12),
                  // INTERPOLACAO DE QUADROS, aqui e nao so dentro da curva:
                  // uma camera lenta de velocidade CONSTANTE (0,25x sem
                  // nenhum keyframe) tambem precisa escolher como os
                  // quadros do meio nascem, e sem este bloco o seletor so
                  // aparecia depois de existir uma curva.
                  AppText(
                    'Interpolação de quadros',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final interp in InterpolacaoDeQuadros.values)
                        _SpeedChip(
                          key: ValueKey('interpolacao-${interp.name}'),
                          label: rotuloDaInterpolacao(interp),
                          selected: video.interpolacao == interp,
                          onTap: () {
                            controller.setClipInterpolacao(layerId, interp);
                            setSheetState(() {});
                          },
                        ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  if (seloDaInterpolacao(video.interpolacao) case final selo?)
                    AppText(
                      selo,
                      key: const ValueKey('interpolacao-selo-da-previa'),
                      style: TextStyle(
                        fontSize: 10.5,
                        fontWeight: FontWeight.w600,
                        color: AmColors.accent,
                      ),
                    ),
                  const SizedBox(height: 4),
                  AppText(
                    'Vale para câmera lenta. Vídeos de 60 fps ou mais usam '
                    'os quadros reais; o fluxo óptico com IA (RIFE) roda no '
                    'Android com GPU, e nos outros aparelhos cai no fluxo '
                    'óptico comum.',
                    style: TextStyle(
                      fontSize: 10,
                      height: 1.35,
                      color: AmColors.muted,
                    ),
                  ),
                ],
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// A PORTA DO ESTUDIO DO TEMPO: uma faixa cheia, com o nome do recurso e o
/// estado da curva — quem ja remapeou o clipe ve isso antes de abrir.
class _PortaDoTimeRemap extends StatelessWidget {
  const _PortaDoTimeRemap({required this.video, required this.onTap});

  final VideoLayer video;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final trilha = timeRemapTrackOf(video);
    final estiloDoEstado = TextStyle(fontSize: 11, color: AmColors.onAction);
    return Tocavel(
      key: const ValueKey('abrir-estudio-do-tempo'),
      haptico: true,
      onTap: onTap,
      child: Container(
        height: 48,
        padding: const EdgeInsets.symmetric(horizontal: 14),
        decoration: BoxDecoration(
          color: AmColors.action,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: [
            Icon(
              Icons.show_chart_rounded,
              size: 18,
              color: AmColors.onAction,
            ),
            const SizedBox(width: 8),
            AppText(
              'Time Remap',
              maxLines: 1,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w700,
                color: AmColors.onAction,
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Align(
                alignment: Alignment.centerRight,
                child: trilha == null
                    ? AppText(
                        'Curva, keyframes, congelar, reverso',
                        key: const ValueKey('time-remap-estado'),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: estiloDoEstado,
                      )
                    : AppTextMoldado(
                        'Curva ativa · {0} keyframes',
                        [trilha.keyframes.length],
                        key: const ValueKey('time-remap-estado'),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: estiloDoEstado,
                      ),
              ),
            ),
            const SizedBox(width: 4),
            Icon(
              CupertinoIcons.chevron_right,
              size: 14,
              color: AmColors.onAction,
            ),
          ],
        ),
      ),
    );
  }
}

/// OS QUATRO MODOS lado a lado, cada um com o desenho do que acontece com
/// a barra: a seta aponta o lado que anda; a tesoura, o lado que perde
/// fonte.
class _LinhaDeCompensacao extends StatelessWidget {
  const _LinhaDeCompensacao({required this.modo, required this.onEscolher});

  final CompensacaoDaVelocidade modo;
  final ValueChanged<CompensacaoDaVelocidade> onEscolher;

  static IconData _icone(CompensacaoDaVelocidade c) => switch (c) {
    CompensacaoDaVelocidade.estenderInicio => CupertinoIcons.arrow_left_to_line,
    CompensacaoDaVelocidade.cortarInicio => CupertinoIcons.scissors,
    CompensacaoDaVelocidade.cortarFim => CupertinoIcons.scissors,
    CompensacaoDaVelocidade.estenderFim => CupertinoIcons.arrow_right_to_line,
  };

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(3),
    decoration: BoxDecoration(
      color: AmColors.chip,
      borderRadius: BorderRadius.circular(12),
    ),
    child: Row(
      children: [
        for (final c in CompensacaoDaVelocidade.values)
          Expanded(
            child: Tocavel(
              key: ValueKey('velocidade-modo-${c.name}'),
              onTap: () => onEscolher(c),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 160),
                height: 52,
                decoration: BoxDecoration(
                  color: c == modo ? AmColors.accentDim : Colors.transparent,
                  borderRadius: BorderRadius.circular(9),
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Transform.flip(
                      flipX: c == CompensacaoDaVelocidade.cortarInicio,
                      child: Icon(
                        _icone(c),
                        size: 17,
                        color: c == modo ? AmColors.accent : AmColors.text,
                      ),
                    ),
                    const SizedBox(height: 4),
                    FittedBox(
                      fit: BoxFit.scaleDown,
                      child: AppText(
                        rotuloDaCompensacao(c),
                        maxLines: 1,
                        style: TextStyle(
                          fontSize: 10.5,
                          color: c == modo ? AmColors.accent : AmColors.muted,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
      ],
    ),
  );
}

/// A posicao na regua para uma velocidade: metade esquerda de 0,1x a 1x
/// (camera lenta precisa de precisao), metade direita de 1x a 4x.
double posicaoDaVelocidade(double v) {
  final x = v.clamp(velocidadeMinima, velocidadeMaximaDaRegua).toDouble();
  if (x <= 1) return (x - velocidadeMinima) / (1 - velocidadeMinima) * .5;
  return .5 + (x - 1) / (velocidadeMaximaDaRegua - 1) * .5;
}

double velocidadeDaPosicao(double p) {
  final q = p.clamp(0.0, 1.0);
  if (q <= .5) return velocidadeMinima + q / .5 * (1 - velocidadeMinima);
  return 1 + (q - .5) / .5 * (velocidadeMaximaDaRegua - 1);
}

/// A REGUA DA VELOCIDADE: trilho com as marcas dos imas, o polegar na
/// velocidade atual; tocar ou arrastar escolhe, e o valor gruda nos imas.
class ReguaDaVelocidade extends StatelessWidget {
  const ReguaDaVelocidade({
    super.key,
    required this.valor,
    required this.onChanged,
  });

  final double valor;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, c) {
      final w = c.maxWidth;
      void escolher(double dx) =>
          onChanged(velocidadeDaRegua(velocidadeDaPosicao(dx / w)));
      return GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapDown: (d) => escolher(d.localPosition.dx),
        onHorizontalDragUpdate: (d) => escolher(d.localPosition.dx),
        child: SizedBox(
          width: w,
          height: 44,
          child: CustomPaint(painter: _PintorDaReguaDaVelocidade(valor)),
        ),
      );
    },
  );
}

class _PintorDaReguaDaVelocidade extends CustomPainter {
  const _PintorDaReguaDaVelocidade(this.valor);

  final double valor;

  @override
  void paint(Canvas canvas, Size size) {
    final meio = size.height / 2;
    final trilho = RRect.fromLTRBR(
      0,
      meio - 3,
      size.width,
      meio + 3,
      const Radius.circular(3),
    );
    canvas.drawRRect(trilho, Paint()..color = const Color(0xFF2E3440));
    final x = posicaoDaVelocidade(valor) * size.width;
    final umX = posicaoDaVelocidade(1) * size.width;
    canvas.drawRRect(
      RRect.fromLTRBR(
        x < umX ? x : umX,
        meio - 3,
        x < umX ? umX : x,
        meio + 3,
        const Radius.circular(3),
      ),
      Paint()..color = AmColors.accent,
    );
    final marca = Paint()
      ..color = const Color(0xFF7A8699)
      ..strokeWidth = 1.5;
    for (final ima in [...imasDaVelocidade, velocidadeMaximaDaRegua]) {
      final mx = posicaoDaVelocidade(ima) * size.width;
      final alto = ima == 1 ? 11.0 : 7.0;
      canvas.drawLine(Offset(mx, meio + 6), Offset(mx, meio + 6 + alto), marca);
    }
    canvas.drawCircle(Offset(x, meio), 10, Paint()..color = Colors.white);
    canvas.drawCircle(
      Offset(x, meio),
      10,
      Paint()
        ..color = Colors.black26
        ..style = PaintingStyle.stroke,
    );
  }

  @override
  bool shouldRepaint(_PintorDaReguaDaVelocidade old) => old.valor != valor;
}

class _SpeedChip extends StatelessWidget {
  const _SpeedChip({
    super.key,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 10),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: AppText(
        label,
        style: TextStyle(
          fontSize: 12,
          color: selected ? AmColors.accent : AmColors.text,
        ),
      ),
    ),
  );
}

class _ToggleRow extends StatelessWidget {
  const _ToggleRow({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(
        child: AppText(
          label,
          style: const TextStyle(fontSize: 12, color: AmColors.text),
        ),
      ),
      CupertinoSwitch(
        value: value,
        activeTrackColor: AmColors.accent,
        onChanged: onChanged,
      ),
    ],
  );
}
