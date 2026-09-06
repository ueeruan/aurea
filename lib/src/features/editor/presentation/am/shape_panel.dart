import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/shape.dart';
import '../../domain/shape_ops.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'curve_panel.dart';
import 'gradient_fill_sheet.dart';
import 'layer_menu.dart' show showReasonToast;
import 'oficio_sheets.dart' show showLayerStylesSheet;
import 'panel_chrome.dart';

/// As sub-abas do "Editar forma".
enum ShapeTool { size, corners, points, angle, rotation, stroke, draw, nodes }

/// EDITAR FORMA (o "Edit Shape" do Alight Motion): o painel de baixo
/// com o trilho esquerdo (voltar, diamante, curva) e sub-abas pelos
/// numeros da forma — Tamanho, Cantos, Pontas, Angulo, Rotacao — mais
/// Traco (Border), Desenhar (Drawing Progress) e Pontos (Edit Points).
///
/// A regra do nivel 1: selecionar a forma, ir na sub-aba do raio,
/// cravar dois keyframes e ajustar a curva em NO MAXIMO quatro toques.
/// O diamante do trilho crava na(s) trilha(s) da aba ativa; a curva
/// abre o editor da trilha principal dela.
class ShapePanel extends ConsumerStatefulWidget {
  const ShapePanel({
    super.key,
    required this.playback,
    required this.tool,
    required this.onToolChanged,
    required this.onBack,
    required this.onEditPoints,
  });

  final PlaybackController playback;
  final ShapeTool tool;
  final ValueChanged<ShapeTool> onToolChanged;
  final VoidCallback onBack;
  final VoidCallback onEditPoints;

  @override
  ConsumerState<ShapePanel> createState() => _ShapePanelState();
}

/// Uma trilha da aba: de onde ler e como escrever.
class _Trilha {
  const _Trilha(this.label, this.min, this.max,
      {required this.read,
      required this.write,
      required this.toggle,
      required this.ease,
      required this.easeAll,
      this.scale = 1,
      this.suffix = '',
      this.decimals = 0});

  final String label;
  final double min;
  final double max;
  final AnimatedDouble? Function(Layer layer) read;
  final void Function(Duration t, double v) write;
  final void Function(Duration t) toggle;
  final void Function(Duration segStartLocal, Easing e) ease;
  final void Function(Easing e) easeAll;

  /// Mostra valor * scale (ex.: 100 para fracoes).
  final double scale;
  final String suffix;
  final int decimals;
}

class _ShapePanelState extends ConsumerState<ShapePanel> {
  bool _sizeLinked = true;
  bool _compoundAdvanced = false;

  ShapeParametric? _param(ShapeLayer l) {
    for (final i in l.contents) {
      if (i is ShapeParametric) return i;
    }
    return null;
  }

  ShapeStroke? _stroke(ShapeLayer l) {
    for (final i in l.contents) {
      if (i is ShapeStroke) return i;
    }
    return null;
  }

  TrimOperator? _trim(ShapeLayer l) {
    for (final i in l.contents) {
      if (i is TrimOperator) return i;
    }
    return null;
  }

  List<_Trilha> _paramTrilhas(EditorController c, String id, ShapeLayer l,
      ShapeParametric sp, ShapeTool tool) {
    _Trilha p(String key, String label, double min, double max,
            {double scale = 1, String suffix = '', int decimals = 0}) =>
        _Trilha(label, min, max,
            read: (layer) {
              final s = layer is ShapeLayer ? _param(layer) : null;
              return s == null ? null : shapeParamTrackOf(s, key);
            },
            write: (t, v) => c.editShapeParam(id, key, t, v),
            toggle: (t) => c.toggleShapeParamKeyframe(id, key, t),
            ease: (seg, e) => c.setShapeParamSegmentEase(id, key, seg, e),
            easeAll: (e) => c.applyEaseToAllShapeParamSegments(id, key, e),
            scale: scale,
            suffix: suffix,
            decimals: decimals);
    final pct = sp.roundnessPercent;
    switch (tool) {
      case ShapeTool.size:
        return switch (sp.kind) {
          ParamShapeKind.rect || ParamShapeKind.ellipse => [
              p('sizeX', 'Largura', 1, 2000),
              p('sizeY', 'Altura', 1, 2000),
            ],
          ParamShapeKind.star => [
              p('outerRadius', 'Raio externo', 1, 1200),
              p('innerRadius', 'Raio interno', 0, 1200),
            ],
          ParamShapeKind.sector => [
              p('outerRadius', 'Raio', 1, 1200),
              p('sectorInner', 'Miolo', 0, 1, scale: 100, suffix: '%'),
            ],
          ParamShapeKind.polygon => [p('outerRadius', 'Raio', 1, 1200)],
        };
      case ShapeTool.corners:
        return switch (sp.kind) {
          ParamShapeKind.star => [
              p('outerRoundness', 'Cantos externos', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
              p('innerRoundness', 'Cantos internos', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
            ],
          ParamShapeKind.rect => [
              p('cornerTopLeft', 'Superior esq.', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
              p('cornerTopRight', 'Superior dir.', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
              p('cornerBottomRight', 'Inferior dir.', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
              p('cornerBottomLeft', 'Inferior esq.', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
            ],
          _ => [
              p('roundness', 'Raio de canto', 0, pct ? 100 : 400,
                  suffix: pct ? '%' : ''),
            ],
        };
      case ShapeTool.points:
        return [p('points', 'Pontas', 3, 24)];
      case ShapeTool.angle:
        return [
          p('startAngle', 'Inicio', -360, 360, suffix: '°'),
          p('sweep', 'Varredura', 0, 360, suffix: '°'),
        ];
      case ShapeTool.rotation:
        return [p('shapeRotation', 'Rotacao', -360, 360, suffix: '°')];
      default:
        return const [];
    }
  }

  List<_Trilha> _itemTrilhas(
      EditorController c, String id, ShapeLayer l, ShapeTool tool) {
    _Trilha item(String itemId, String key, String label, double min,
            double max, {double scale = 1, String suffix = ''}) =>
        _Trilha(label, min, max,
            read: (layer) {
              if (layer is! ShapeLayer) return null;
              for (final i in layer.contents) {
                if (i.id == itemId) return EditorController.shapeItemTrack(i, key);
              }
              return null;
            },
            write: (t, v) => c.editShapeItemTrack(id, itemId, key, t, v),
            toggle: (t) => c.toggleShapeItemTrackKeyframe(id, itemId, key, t),
            ease: (seg, e) =>
                c.setShapeItemTrackSegmentEase(id, itemId, key, seg, e),
            easeAll: (e) =>
                c.applyEaseToAllShapeItemTrackSegments(id, itemId, key, e),
            scale: scale,
            suffix: suffix);
    switch (tool) {
      case ShapeTool.stroke:
        final s = _stroke(l);
        if (s == null) return const [];
        // TODO NUMERO ANIMA (regra 6): espessura, tracejado, espaco e
        // deslocamento tem diamante no trilho e curva.
        return [
          item(s.id, 'width', 'Espessura', 0, 120),
          item(s.id, 'dashLength', 'Tracejado', 0, 200),
          item(s.id, 'gapLength', 'Espaco', 0, 200),
          item(s.id, 'dashOffset', 'Deslocamento', -2000, 2000),
          item(s.id, 'opacity', 'Opacidade', 0, 1, scale: 100, suffix: '%'),
        ];
      case ShapeTool.draw:
        final t = _trim(l);
        if (t == null) return const [];
        return [
          item(t.id, 'start', 'Inicio', 0, 1, scale: 100, suffix: '%'),
          item(t.id, 'end', 'Fim', 0, 1, scale: 100, suffix: '%'),
          item(t.id, 'offset', 'Deslocamento', -1, 1, scale: 100, suffix: '%'),
        ];
      default:
        return const [];
    }
  }

  List<ParamTab> _abas(ShapeLayer l, ShapeParametric? sp) {
    bool anim(List<AnimatedDouble?> ts) =>
        ts.any((t) => t != null && t.isAnimated);
    final abas = <ParamTab>[];
    if (sp != null) {
      abas.add(ParamTab(
          id: ShapeTool.size.name,
          label: 'Tamanho',
          animated: anim([
            sp.sizeX, sp.sizeY, sp.outerRadius, sp.innerRadius, sp.sectorInner
          ])));
      if (sp.kind != ParamShapeKind.ellipse && sp.kind != ParamShapeKind.sector) {
        abas.add(ParamTab(
            id: ShapeTool.corners.name,
            label: 'Cantos',
            animated: anim([
              sp.roundness,
              sp.cornerTopLeft,
              sp.cornerTopRight,
              sp.cornerBottomRight,
              sp.cornerBottomLeft,
              sp.outerRoundness,
              sp.innerRoundness,
            ])));
      }
      if (sp.kind == ParamShapeKind.polygon || sp.kind == ParamShapeKind.star) {
        abas.add(ParamTab(
            id: ShapeTool.points.name,
            label: 'Pontas',
            animated: anim([sp.points])));
      }
      if (sp.kind == ParamShapeKind.sector) {
        abas.add(ParamTab(
            id: ShapeTool.angle.name,
            label: 'Angulo',
            animated: anim([sp.startAngle, sp.sweep])));
      }
      abas.add(ParamTab(
          id: ShapeTool.rotation.name,
          label: 'Rotacao',
          animated: anim([sp.shapeRotation])));
    }
    final stroke = _stroke(l);
    abas.add(ParamTab(
        id: ShapeTool.stroke.name,
        label: 'Traco',
        animated: stroke?.dashOffset.isAnimated ?? false));
    final trim = _trim(l);
    abas.add(ParamTab(
        id: ShapeTool.draw.name,
        label: 'Desenhar',
        animated: trim != null &&
            (trim.start.isAnimated || trim.end.isAnimated || trim.offset.isAnimated)));
    abas.add(ParamTab(id: ShapeTool.nodes.name, label: 'Pontos'));
    return abas;
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer is! ShapeLayer || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final sp = _param(layer);
    final abas = _abas(layer, sp);
    // Aba que nao existe para esta forma: cai na primeira.
    final tool = abas.any((a) => a.id == widget.tool.name)
        ? widget.tool
        : ShapeTool.values.firstWhere((e) => e.name == abas.first.id);

    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, t, _) {
        final local = layer.localTime(t);
        final trilhas = sp != null && _ehParametrica(tool)
            ? _paramTrilhas(controller, id, layer, sp, tool)
            : _itemTrilhas(controller, id, layer, tool);
        final principal = trilhas.isEmpty ? null : trilhas.first;
        final tracks = [for (final tr in trilhas) tr.read(layer)];
        final animado = tracks.any((x) => x != null && x.isAnimated);
        final temKf = tracks.any((x) => x != null && x.hasKeyframeAt(local));

        return AmPanelChrome(
          onBack: widget.onBack,
          acoes: [
            if (layer.contents.whereType<ShapeGradientFill>().isNotEmpty)
              TextButton.icon(
                onPressed: () => showGradientFillSheet(
                  context,
                  id,
                  playback: widget.playback,
                ),
                icon: const Icon(Icons.gradient, size: 18),
                label: const Text('Gradiente'),
              ),
          ],
          animado: animado,
          temKfAqui: temKf,
          onCravar: () {
            if (trilhas.isEmpty) {
              showReasonToast(context, _semTrilha(tool));
              return;
            }
            // Le o relogio NO TOQUE; crava em todas as trilhas da aba.
            final agora = widget.playback.time.value;
            for (final tr in trilhas) {
              tr.toggle(agora);
            }
          },
          onCurva: principal == null
              ? null
              : () => showTrackCurveSheet(
                    context,
                    ref,
                    widget.playback,
                    label: principal.label,
                    layerId: id,
                    trackOf: principal.read,
                    onSetEase: principal.ease,
                    onSetEaseAll: principal.easeAll,
                  ),
          abas: abas,
          abaAtiva: tool.name,
          onAba: (nome) => widget
              .onToolChanged(ShapeTool.values.firstWhere((e) => e.name == nome)),
          corpo: Padding(
            padding: const EdgeInsets.fromLTRB(6, 6, 6, 2),
            child: _corpo(context, controller, id, layer, sp, tool, trilhas, t),
          ),
        );
      },
    );
  }

  bool _ehParametrica(ShapeTool tool) => switch (tool) {
        ShapeTool.size ||
        ShapeTool.corners ||
        ShapeTool.points ||
        ShapeTool.angle ||
        ShapeTool.rotation =>
          true,
        _ => false,
      };

  String _semTrilha(ShapeTool tool) => switch (tool) {
        ShapeTool.stroke => 'Ligue o traco primeiro',
        ShapeTool.draw => 'Ligue o Desenhar primeiro',
        ShapeTool.nodes => 'Os keyframes dos pontos ficam no Edit Points',
        _ => 'Nada para cravar aqui',
      };

  Widget _corpo(
    BuildContext context,
    EditorController controller,
    String id,
    ShapeLayer layer,
    ShapeParametric? sp,
    ShapeTool tool,
    List<_Trilha> trilhas,
    Duration t,
  ) {
    switch (tool) {
      case ShapeTool.nodes:
        return _Pontos(onEditPoints: widget.onEditPoints, parametrica: sp != null);
      case ShapeTool.stroke:
        return _Traco(
          layer: layer,
          stroke: _stroke(layer),
          trilhas: trilhas,
          t: t,
          onLigar: () => setState(() => controller.ensureShapeStroke(id)),
          onDesligar: () => setState(() => controller.removeShapeStroke(id)),
          onUpdate: (fn) => controller.updateShapeStroke(id, fn),
          onSombra: () => showLayerStylesSheet(context, ref, id, widget.playback),
        );
      case ShapeTool.draw:
        return _Desenhar(
          layer: layer,
          trim: _trim(layer),
          trilhas: trilhas,
          t: t,
          onLigar: () => setState(() => controller.ensureShapeTrim(id)),
          onDesligar: () => setState(() => controller.removeShapeTrim(id)),
        );
      case ShapeTool.size:
        if (sp != null &&
            (sp.kind == ParamShapeKind.rect || sp.kind == ParamShapeKind.ellipse) &&
            trilhas.length == 2) {
          return _Tamanho(
            layer: layer,
            largura: trilhas[0],
            altura: trilhas[1],
            t: t,
            linked: _sizeLinked,
            onToggleLink: () => setState(() => _sizeLinked = !_sizeLinked),
          );
        }
        return _Reguas(layer: layer, trilhas: trilhas, t: t);
      case ShapeTool.corners:
        if (sp?.kind == ParamShapeKind.rect) {
          return _CompoundShapeEditor(
            layer: layer,
            trilhas: trilhas,
            t: t,
            advanced: _compoundAdvanced,
            onDepth: (v) => setState(() => _compoundAdvanced = v),
            onAddGeometry: (kind) =>
                controller.addCompoundShapeGeometry(id, kind),
            onAddMerge: () =>
                controller.addPathOperator(id, ShapePathOp.merge),
            onSetMerge: (operator, mode) {
              final delta = (mode.index - operator.mode.index +
                      MergeMode.values.length) %
                  MergeMode.values.length;
              for (var i = 0; i < delta; i++) {
                controller.cycleMergeMode(id, operator.id);
              }
            },
          );
        }
        return _Reguas(layer: layer, trilhas: trilhas, t: t);
      default:
        return _Reguas(layer: layer, trilhas: trilhas, t: t);
    }
  }
}

class _CompoundShapeEditor extends StatelessWidget {
  const _CompoundShapeEditor({
    required this.layer,
    required this.trilhas,
    required this.t,
    required this.advanced,
    required this.onDepth,
    required this.onAddGeometry,
    required this.onAddMerge,
    required this.onSetMerge,
  });

  final ShapeLayer layer;
  final List<_Trilha> trilhas;
  final Duration t;
  final bool advanced;
  final ValueChanged<bool> onDepth;
  final ValueChanged<ParamShapeKind> onAddGeometry;
  final VoidCallback onAddMerge;
  final void Function(MergePathsOperator operator, MergeMode mode) onSetMerge;

  @override
  Widget build(BuildContext context) {
    Widget depth(String label, bool value) => Expanded(
          child: GestureDetector(
            onTap: () => onDepth(value),
            child: Container(
              height: 32,
              alignment: Alignment.center,
              margin: const EdgeInsets.symmetric(horizontal: 3),
              decoration: BoxDecoration(
                color: advanced == value ? AmColors.accent : AmColors.bg,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(label,
                  style: TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                      color: advanced == value
                          ? AmColors.bg
                          : AmColors.muted)),
            ),
          ),
        );

    final merges = layer.contents.whereType<MergePathsOperator>().toList();
    return Column(
      children: [
        Row(children: [depth('Montar', false), depth('Avancado', true)]),
        const SizedBox(height: 8),
        Expanded(
          child: advanced
              ? SingleChildScrollView(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      const Text('Adicionar geometria ao composto',
                          style: TextStyle(
                              fontSize: 11, color: AmColors.muted)),
                      const SizedBox(height: 6),
                      Row(
                        children: [
                          Expanded(
                            child: CupertinoButton(
                              color: AmColors.panelHigh,
                              padding: const EdgeInsets.symmetric(vertical: 8),
                              onPressed: () =>
                                  onAddGeometry(ParamShapeKind.rect),
                              child: const Text('Retangulo',
                                  style: TextStyle(
                                      fontSize: 11, color: AmColors.text)),
                            ),
                          ),
                          const SizedBox(width: 6),
                          Expanded(
                            child: CupertinoButton(
                              color: AmColors.panelHigh,
                              padding: const EdgeInsets.symmetric(vertical: 8),
                              onPressed: () =>
                                  onAddGeometry(ParamShapeKind.ellipse),
                              child: const Text('Circulo',
                                  style: TextStyle(
                                      fontSize: 11, color: AmColors.text)),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),
                      for (final merge in merges)
                        Container(
                          margin: const EdgeInsets.only(bottom: 8),
                          padding: const EdgeInsets.all(10),
                          decoration: BoxDecoration(
                            color: AmColors.bg,
                            borderRadius: BorderRadius.circular(10),
                          ),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              const Text('Combinar caminhos',
                                  style: TextStyle(
                                      fontSize: 12,
                                      fontWeight: FontWeight.w700,
                                      color: AmColors.text)),
                              const SizedBox(height: 7),
                              Wrap(
                                spacing: 6,
                                runSpacing: 6,
                                children: [
                                  for (final mode in MergeMode.values)
                                    GestureDetector(
                                      onTap: () => onSetMerge(merge, mode),
                                      child: Container(
                                        padding: const EdgeInsets.symmetric(
                                            horizontal: 9, vertical: 6),
                                        decoration: BoxDecoration(
                                          color: merge.mode == mode
                                              ? AmColors.accent
                                              : AmColors.chip,
                                          borderRadius:
                                              BorderRadius.circular(7),
                                        ),
                                        child: Text(
                                          mergeModeLabel(mode),
                                          style: TextStyle(
                                            fontSize: 10,
                                            color: merge.mode == mode
                                                ? AmColors.bg
                                                : AmColors.text,
                                          ),
                                        ),
                                      ),
                                    ),
                                ],
                              ),
                            ],
                          ),
                        ),
                      CupertinoButton(
                        color: AmColors.panelHigh,
                        onPressed: onAddMerge,
                        child: Text(
                          merges.isEmpty
                              ? 'Adicionar Merge Paths'
                              : 'Adicionar outro Merge Paths',
                          style: const TextStyle(color: AmColors.text),
                        ),
                      ),
                      const SizedBox(height: 8),
                      const Text(
                        'O operador combina os caminhos que aparecem antes dele.',
                        textAlign: TextAlign.center,
                        style: TextStyle(fontSize: 10, color: AmColors.muted),
                      ),
                    ],
                  ),
                )
              : _Reguas(layer: layer, trilhas: trilhas, t: t),
        ),
      ],
    );
  }
}

/// Reguas empilhadas, uma por trilha, com o valor em cima.
class _Reguas extends StatelessWidget {
  const _Reguas({required this.layer, required this.trilhas, required this.t});

  final Layer layer;
  final List<_Trilha> trilhas;
  final Duration t;

  @override
  Widget build(BuildContext context) {
    final local = layer.localTime(t);
    if (trilhas.isEmpty) {
      return const Center(
        child: Text('Esta forma nao tem esse numero',
            style: TextStyle(fontSize: 13, color: AmColors.muted)),
      );
    }
    return Column(
      children: [
        Wrap(
          alignment: WrapAlignment.center,
          spacing: 6,
          runSpacing: 4,
          children: [
            for (final tr in trilhas)
              AmValueChip(
                  text:
                      '${amNumber((tr.read(layer)?.valueAt(local) ?? 0) * tr.scale, tr.decimals)}${tr.suffix}',
                  label: tr.label,
                  width: trilhas.length > 2 ? 76 : 112),
          ],
        ),
        const SizedBox(height: 8),
        for (final tr in trilhas) ...[
          Expanded(
            child: AmTickRuler(
              value: (tr.read(layer)?.valueAt(local) ?? 0) * tr.scale,
              min: tr.min * tr.scale,
              max: tr.max * tr.scale,
              unitsPerPixel: (tr.max - tr.min) * tr.scale / 420,
              accentCenter: false,
              height: double.infinity,
              onChanged: (v) => tr.write(t, v / tr.scale),
            ),
          ),
          const SizedBox(height: 6),
        ],
      ],
    );
  }
}

/// Tamanho com cadeado: largura e altura juntas ou separadas.
class _Tamanho extends StatelessWidget {
  const _Tamanho({
    required this.layer,
    required this.largura,
    required this.altura,
    required this.t,
    required this.linked,
    required this.onToggleLink,
  });

  final Layer layer;
  final _Trilha largura;
  final _Trilha altura;
  final Duration t;
  final bool linked;
  final VoidCallback onToggleLink;

  @override
  Widget build(BuildContext context) {
    final local = layer.localTime(t);
    final w = largura.read(layer)?.valueAt(local) ?? 0;
    final h = altura.read(layer)?.valueAt(local) ?? 0;
    return Column(
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            AmValueChip(text: amNumber(w, 0), label: 'Largura', width: 112),
            GestureDetector(
              onTap: onToggleLink,
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 10),
                child: Icon(
                    linked ? CupertinoIcons.link : CupertinoIcons.link_circle,
                    size: 20,
                    color: linked ? AmColors.accent : AmColors.muted),
              ),
            ),
            AmValueChip(text: amNumber(h, 0), label: 'Altura', width: 112),
          ],
        ),
        const SizedBox(height: 8),
        Expanded(
          child: AmTickRuler(
            value: w,
            min: 1,
            max: 2000,
            unitsPerPixel: 2.4,
            accentCenter: false,
            height: double.infinity,
            onChanged: (v) {
              largura.write(t, v);
              if (linked) altura.write(t, v * (w.abs() < 1e-6 ? 1 : h / w));
            },
          ),
        ),
        const SizedBox(height: 6),
        Expanded(
          child: AmTickRuler(
            value: h,
            min: 1,
            max: 2000,
            unitsPerPixel: 2.4,
            accentCenter: false,
            height: double.infinity,
            onChanged: (v) {
              altura.write(t, v);
              if (linked) largura.write(t, v * (h.abs() < 1e-6 ? 1 : w / h));
            },
          ),
        ),
      ],
    );
  }
}

/// TRACO (Border & Shadow): liga/desliga, espessura, cor, tracejado com
/// deslocamento animavel; a sombra mora nos estilos da camada.
class _Traco extends StatelessWidget {
  const _Traco({
    required this.layer,
    required this.stroke,
    required this.trilhas,
    required this.t,
    required this.onLigar,
    required this.onDesligar,
    required this.onUpdate,
    required this.onSombra,
  });

  final ShapeLayer layer;
  final ShapeStroke? stroke;
  final List<_Trilha> trilhas;
  final Duration t;
  final VoidCallback onLigar;
  final VoidCallback onDesligar;
  final void Function(ShapeStroke Function(ShapeStroke)) onUpdate;
  final VoidCallback onSombra;

  @override
  Widget build(BuildContext context) {
    final s = stroke;
    if (s == null) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text('Sem traco',
                style: TextStyle(fontSize: 13, color: AmColors.muted)),
            const SizedBox(height: 10),
            _Botao(texto: 'Ligar traco', onTap: onLigar, cheio: true),
            const SizedBox(height: 8),
            _Botao(texto: 'Sombra e brilho (estilos)', onTap: onSombra),
          ],
        ),
      );
    }
    final local = layer.localTime(t);
    return SingleChildScrollView(
      child: Column(
        children: [
          // Toda regua daqui e uma TRILHA: le do keyframe, escreve no
          // tempo, e o diamante do trilho crava (regras 5 e 6).
          for (final tr in trilhas)
            _Linha(
              label: tr.label,
              value: (tr.read(layer)?.valueAt(local) ?? 0) * tr.scale,
              min: tr.min * tr.scale,
              max: tr.max * tr.scale,
              upp: (tr.max - tr.min) * tr.scale / 420,
              display: tr.label == 'Tracejado' &&
                      (tr.read(layer)?.valueAt(local) ?? 0) <= 0
                  ? 'Solido'
                  : '${amNumber((tr.read(layer)?.valueAt(local) ?? 0) * tr.scale, tr.decimals)}${tr.suffix}',
              onChanged: (v) => tr.write(t, v / tr.scale),
              animado: tr.read(layer)?.isAnimated ?? false,
            ),
          const SizedBox(height: 6),
          Row(
            children: [
              const SizedBox(
                  width: 92,
                  child: Text('Cor',
                      style: TextStyle(fontSize: 13, color: AmColors.muted))),
              for (final c in const [
                Color(0xFFFFFFFF),
                Color(0xFFB8FF3D),
                Color(0xFFFF3B52),
                Color(0xFF7C62FF),
                Color(0xFF35C4E7),
                Color(0xFFFFB020),
                Color(0xFF0B0E12),
              ])
                GestureDetector(
                  onTap: () => onUpdate((x) => x.copyWith(color: c)),
                  child: Container(
                    width: 26,
                    height: 26,
                    margin: const EdgeInsets.only(right: 7),
                    decoration: BoxDecoration(
                      color: c,
                      shape: BoxShape.circle,
                      border: Border.all(
                          color: s.color == c ? Colors.white : AmColors.hairline,
                          width: s.color == c ? 2.5 : 1),
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              _Botao(texto: 'Sombra e brilho', onTap: onSombra),
              const SizedBox(width: 8),
              _Botao(texto: 'Tirar traco', onTap: onDesligar),
            ],
          ),
        ],
      ),
    );
  }
}

/// DESENHAR (Drawing Progress): inicio e fim de 0 a 100%.
class _Desenhar extends StatelessWidget {
  const _Desenhar({
    required this.layer,
    required this.trim,
    required this.trilhas,
    required this.t,
    required this.onLigar,
    required this.onDesligar,
  });

  final ShapeLayer layer;
  final TrimOperator? trim;
  final List<_Trilha> trilhas;
  final Duration t;
  final VoidCallback onLigar;
  final VoidCallback onDesligar;

  @override
  Widget build(BuildContext context) {
    if (trim == null) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              'Drawing Progress: a linha se desenhando, o contorno animado.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 13, color: AmColors.muted),
            ),
            const SizedBox(height: 10),
            _Botao(texto: 'Ligar Desenhar', onTap: onLigar, cheio: true),
          ],
        ),
      );
    }
    return Column(
      children: [
        Expanded(child: _Reguas(layer: layer, trilhas: trilhas, t: t)),
        Align(
          alignment: Alignment.centerRight,
          child: _Botao(texto: 'Tirar', onTap: onDesligar),
        ),
      ],
    );
  }
}

class _Pontos extends StatelessWidget {
  const _Pontos({required this.onEditPoints, required this.parametrica});

  final VoidCallback onEditPoints;
  final bool parametrica;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            parametrica
                ? 'Editar os pontos vira a forma em caminho: os numeros (tamanho, cantos, pontas) deixam de valer.'
                : 'Mova, adicione e anime os pontos do caminho pelo trackpad.',
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 13, color: AmColors.muted),
          ),
          const SizedBox(height: 10),
          _Botao(texto: 'Abrir Edit Points', onTap: onEditPoints, cheio: true),
        ],
      ),
    );
  }
}

class _Linha extends StatelessWidget {
  const _Linha({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.upp,
    required this.display,
    required this.onChanged,
    this.animado = false,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final double upp;
  final String display;
  final ValueChanged<double> onChanged;
  final bool animado;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        children: [
          SizedBox(
              width: 92,
              child: Text(label,
                  style: TextStyle(
                      fontSize: 13,
                      color: animado ? AmColors.accent : AmColors.muted))),
          Expanded(
            child: AmTickRuler(
              value: value,
              min: min,
              max: max,
              unitsPerPixel: upp,
              height: 40,
              onChanged: onChanged,
            ),
          ),
          SizedBox(
              width: 58,
              child: Text(display,
                  textAlign: TextAlign.center,
                  style:
                      const TextStyle(fontSize: 13, color: AmColors.accent))),
        ],
      ),
    );
  }
}

class _Botao extends StatelessWidget {
  const _Botao({required this.texto, required this.onTap, this.cheio = false});

  final String texto;
  final VoidCallback onTap;
  final bool cheio;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
        decoration: BoxDecoration(
          color: cheio ? AmColors.accent : AmColors.chip,
          borderRadius: BorderRadius.circular(10),
        ),
        child: Text(texto,
            style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: cheio ? const Color(0xFF0B0E12) : AmColors.accent)),
      ),
    );
  }
}
