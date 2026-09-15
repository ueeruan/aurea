import 'dart:math' as math;

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/video_project.dart' show anguloDaLente;
import '../context/parameter_row.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';

/// AS OPCOES DA CAMERA da composicao: a vista (perspectiva ou
/// ortografica, angulo de visao ligado a lente), o desfoque de foco e a
/// neblina.
///
/// Posicao, giro 3D e Z moram no transform, como em qualquer camada. 1200
/// e a lente neutra: a composicao fica identica a um projeto sem camera.
/// Menos e grande-angular, mais e teleobjetiva; animar a lente com a
/// posicao e o dolly-zoom.
Future<void> showCameraSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) => showParamSheet(
  context,
  title: 'Câmera',
  heightFactor: 0.62,
  builder: (sheetContext) => _Camera(layerId: layerId, playback: playback),
);

class _Camera extends ConsumerWidget {
  const _Camera({required this.layerId, required this.playback});

  final String layerId;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) => SafeArea(
    child: ListenableBuilder(
      listenable: playback.time,
      builder: (context, _) {
        final projeto = ref.watch(editorControllerProvider);
        final camada = projeto.layerById(layerId);
        if (camada is! CameraLayer) return const SizedBox.shrink();
        final c = ref.read(editorControllerProvider.notifier);
        final t = playback.time.value;
        final local = camada.localTime(t);
        final zoom = camada.zoom.valueAt(local);
        final largura = projeto.outputWidth.toDouble();
        final graus = anguloDaLente(largura, zoom);
        // A lente em "milimetros" na convencao do app inteiro (36 mm de
        // filme na largura da composicao) — e o numero que quem veio de
        // camera reconhece.
        final mm = 36 * zoom / largura;
        final o = camada.opcoes;

        void mudar(OpcoesDaCamera Function(OpcoesDaCamera) f) =>
            c.atualizarOpcoesDaCamera(layerId, f);

        /// Uma linha numerica de opcao: o losango crava na trilha dela;
        /// editar segue a regra de todo numero animado.
        Widget linha({
          required String rotulo,
          required String chave,
          required AnimatedDouble trilha,
          required OpcoesDaCamera Function(OpcoesDaCamera, AnimatedDouble) poe,
          required double min,
          required double max,
          double unitsPerPixel = 2,
          String unidade = 'px',
        }) => ParameterRow(
          label: rotulo,
          value: trilha.valueAt(local).clamp(min, max).toDouble(),
          min: min,
          max: max,
          unitsPerPixel: unitsPerPixel,
          decimals: 0,
          unit: unidade,
          valueKey: ValueKey(chave),
          keyframe: KeyframeState(
            animated: trilha.isAnimated,
            here: trilha.hasKeyframeAt(local),
            onToggle: () => mudar(
              (x) => poe(
                x,
                trilha.hasKeyframeAt(local)
                    ? trilha.withoutKeyframe(local)
                    : trilha.withKeyframe(local, trilha.valueAt(local)),
              ),
            ),
          ),
          onChanged: (v) {
            if (!trilha.aceitaEdicaoEm(local)) {
              AureaSnack.show(
                context,
                'Esta opção tem keyframes: toque no losango para marcar este instante',
              );
              return;
            }
            mudar((x) => poe(x, trilha.edited(local, v)));
          },
        );

        return ListView(
          key: const ValueKey('camera-opcoes'),
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
          children: [
            const _Secao('Vista'),
            CupertinoSlidingSegmentedControl<bool>(
              key: const ValueKey('camera-projecao'),
              groupValue: o.ortografica,
              thumbColor: AmColors.accentDim,
              backgroundColor: AmColors.chip,
              children: const {
                false: Padding(
                  key: ValueKey('camera-perspectiva'),
                  padding: EdgeInsets.symmetric(vertical: 7),
                  child: AppText(
                    'Perspectiva',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
                true: Padding(
                  key: ValueKey('camera-ortografica'),
                  padding: EdgeInsets.symmetric(vertical: 7),
                  child: AppText(
                    'Ortográfica',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
              },
              onValueChanged: (v) {
                if (v != null) mudar((x) => x.copyWith(ortografica: v));
              },
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                SizedBox(
                  width: 110,
                  height: 70,
                  child: CustomPaint(
                    key: const ValueKey('camera-cone'),
                    painter: _ConeDaVista(o.ortografica ? null : graus),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: AppText(
                    o.ortografica
                        ? 'Ortográfica: o fundo não encolhe com a distância; a lente só aproxima ou afasta.'
                        : '≈ ${mm.toStringAsFixed(0)} mm · ${graus.toStringAsFixed(0)}° de ângulo. 1200 é a lente neutra.',
                    style: const TextStyle(
                      fontSize: 11.5,
                      height: 1.35,
                      color: AmColors.muted,
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 6),
            if (!o.ortografica)
              ParameterRow(
                label: 'Ângulo de visão',
                value: graus.clamp(1.0, 170.0).toDouble(),
                min: 1,
                max: 170,
                unitsPerPixel: .25,
                decimals: 0,
                unit: '°',
                valueKey: const ValueKey('camera-angulo'),
                keyframe: KeyframeState(
                  animated: camada.zoom.isAnimated,
                  here: camada.zoom.hasKeyframeAt(local),
                  onToggle: () => c.toggleCameraZoomKeyframe(layerId, t),
                ),
                onChanged: (v) => c.editCameraFov(layerId, t, v),
              ),
            ParameterRow(
              label: 'Lente',
              value: zoom,
              min: 60,
              max: 12000,
              unitsPerPixel: 8,
              decimals: 0,
              valueKey: const ValueKey('camera-lente'),
              keyframe: KeyframeState(
                animated: camada.zoom.isAnimated,
                here: camada.zoom.hasKeyframeAt(local),
                onToggle: () => c.toggleCameraZoomKeyframe(layerId, t),
              ),
              onChanged: (v) => c.editCameraZoom(layerId, t, v),
              onReset: () =>
                  c.editCameraZoom(layerId, t, CameraLayer.lenteNeutra),
            ),
            const SizedBox(height: 14),
            _Titulo(
              'Desfoque de foco',
              chave: 'camera-foco',
              ligado: o.focoLigado,
              onLigar: (v) => mudar((x) => x.copyWith(focoLigado: v)),
            ),
            if (o.focoLigado) ...[
              linha(
                rotulo: 'Distância do foco',
                chave: 'camera-foco-distancia',
                trilha: o.distanciaDoFoco,
                poe: (x, a) => x.copyWith(distanciaDoFoco: a),
                min: 1,
                max: 20000,
                unitsPerPixel: 6,
              ),
              linha(
                rotulo: 'Intensidade',
                chave: 'camera-foco-intensidade',
                trilha: o.intensidadeDoFoco,
                poe: (x, a) => x.copyWith(intensidadeDoFoco: a),
                min: 0,
                max: 100,
                unitsPerPixel: .2,
              ),
              linha(
                rotulo: 'Profundidade de campo',
                chave: 'camera-foco-profundidade',
                trilha: o.profundidadeDeCampo,
                poe: (x, a) => x.copyWith(profundidadeDeCampo: a),
                min: 1,
                max: 10000,
                unitsPerPixel: 4,
              ),
              const AppText(
                'O plano da composição fica a 1200 do olho da câmera: camadas mais longe ou mais perto que a faixa nítida desfocam.',
                style: TextStyle(fontSize: 11, height: 1.35, color: AmColors.muted),
              ),
            ],
            const SizedBox(height: 14),
            _Titulo(
              'Neblina',
              chave: 'camera-neblina',
              ligado: o.neblinaLigada,
              onLigar: (v) => mudar((x) => x.copyWith(neblinaLigada: v)),
            ),
            if (o.neblinaLigada) ...[
              Tocavel(
                key: const ValueKey('camera-neblina-cor'),
                onTap: () async {
                  final nova = await showColorPicker(
                    context,
                    initial: o.corDaNeblina,
                    withAlpha: false,
                    onChanged: (cor) =>
                        mudar((x) => x.copyWith(corDaNeblina: cor)),
                  );
                  if (nova != null) {
                    mudar((x) => x.copyWith(corDaNeblina: nova));
                  }
                },
                child: Container(
                  height: 44,
                  margin: const EdgeInsets.only(bottom: 6),
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  decoration: BoxDecoration(
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Row(
                    children: [
                      Container(
                        width: 26,
                        height: 26,
                        decoration: BoxDecoration(
                          color: o.corDaNeblina,
                          borderRadius: BorderRadius.circular(7),
                          border: Border.all(color: Colors.white24),
                        ),
                      ),
                      const SizedBox(width: 10),
                      const Expanded(
                        child: AppText(
                          'Cor da neblina',
                          style: TextStyle(fontSize: 14, color: AmColors.text),
                        ),
                      ),
                      const Icon(
                        CupertinoIcons.chevron_right,
                        size: 14,
                        color: AmColors.muted,
                      ),
                    ],
                  ),
                ),
              ),
              linha(
                rotulo: 'Começa em',
                chave: 'camera-neblina-perto',
                trilha: o.neblinaPerto,
                poe: (x, a) => x.copyWith(neblinaPerto: a),
                min: 0,
                max: 50000,
                unitsPerPixel: 10,
              ),
              linha(
                rotulo: 'Cobre tudo em',
                chave: 'camera-neblina-longe',
                trilha: o.neblinaLonge,
                poe: (x, a) => x.copyWith(neblinaLonge: a),
                min: 0,
                max: 50000,
                unitsPerPixel: 10,
              ),
            ],
            const SizedBox(height: 12),
            const AppText(
              'Posição, giro 3D e Z da câmera moram em Mover/Transformar. Só camadas com o 3D ligado são vistas pela câmera — e só elas ganham foco e neblina.',
              style: TextStyle(fontSize: 11.5, height: 1.35, color: AmColors.muted),
            ),
          ],
        );
      },
    ),
  );
}

class _Secao extends StatelessWidget {
  const _Secao(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(top: 6, bottom: 8),
    child: AppText(
      texto,
      style: const TextStyle(
        fontSize: 15,
        fontWeight: FontWeight.w700,
        color: AmColors.text,
      ),
    ),
  );
}

class _Titulo extends StatelessWidget {
  const _Titulo(
    this.texto, {
    required this.chave,
    required this.ligado,
    required this.onLigar,
  });

  final String texto;
  final String chave;
  final bool ligado;
  final ValueChanged<bool> onLigar;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(child: _Secao(texto)),
      CupertinoSwitch(
        key: ValueKey(chave),
        value: ligado,
        activeTrackColor: AmColors.accent,
        onChanged: onLigar,
      ),
    ],
  );
}

/// O DESENHO DA VISTA: a camera a esquerda e o cone do angulo de visao
/// abrindo para a direita; na ortografica, duas paralelas.
class _ConeDaVista extends CustomPainter {
  const _ConeDaVista(this.graus);

  /// Nulo = ortografica.
  final double? graus;

  @override
  void paint(Canvas canvas, Size size) {
    final meio = size.height / 2;
    const olho = Offset(18, 0);
    final origem = olho.translate(0, meio);
    final corpo = RRect.fromRectAndRadius(
      Rect.fromCenter(center: origem.translate(-8, 0), width: 18, height: 14),
      const Radius.circular(3),
    );
    canvas.drawRRect(corpo, Paint()..color = AmColors.text);
    final alcance = size.width - origem.dx - 4;
    final linha = Paint()
      ..color = AmColors.accent
      ..strokeWidth = 1.6
      ..style = PaintingStyle.stroke;
    final preenche = Paint()..color = AmColors.accent.withValues(alpha: .18);
    final g = graus;
    if (g == null) {
      final r = Rect.fromLTRB(origem.dx, meio - 16, origem.dx + alcance, meio + 16);
      canvas
        ..drawRect(r, preenche)
        ..drawLine(r.topLeft, r.topRight, linha)
        ..drawLine(r.bottomLeft, r.bottomRight, linha);
      return;
    }
    final meioAngulo = (g.clamp(1.0, 170.0)) * math.pi / 360;
    final abertura = math.min(meio - 2, alcance * math.tan(meioAngulo));
    final cima = Offset(origem.dx + alcance, meio - abertura);
    final baixo = Offset(origem.dx + alcance, meio + abertura);
    final cone = Path()
      ..moveTo(origem.dx, origem.dy)
      ..lineTo(cima.dx, cima.dy)
      ..lineTo(baixo.dx, baixo.dy)
      ..close();
    canvas
      ..drawPath(cone, preenche)
      ..drawLine(origem, cima, linha)
      ..drawLine(origem, baixo, linha);
  }

  @override
  bool shouldRepaint(_ConeDaVista old) => old.graus != graus;
}
