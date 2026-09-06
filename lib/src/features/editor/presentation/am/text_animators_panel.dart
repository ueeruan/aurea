import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' show ImageFilter;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/text_anim.dart';
import '../../domain/text_animator.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// ANIMACAO DE TEXTO — painel no modelo do Alight Motion.
///
/// Escolhe-se a animacao numa grade que MOSTRA cada uma se mexendo, e
/// depois mexe-se em seis controles. O modelo do After Effects (animador
/// + seletor na mao) continua acessivel em "Avancado".
class TextAnimatorsPanel extends ConsumerStatefulWidget {
  const TextAnimatorsPanel({
    super.key,
    required this.playback,
    required this.onBack,
  });

  final PlaybackController playback;
  final VoidCallback onBack;

  @override
  ConsumerState<TextAnimatorsPanel> createState() =>
      _TextAnimatorsPanelState();
}

class _TextAnimatorsPanelState extends ConsumerState<TextAnimatorsPanel> {
  TextAnimSlot _slot = TextAnimSlot.entrada;
  bool _advanced = false;

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final selectedId = ref.watch(selectedLayerProvider);
    final layer =
        selectedId == null ? null : project.layerById(selectedId);

    if (layer is! TextLayer) {
      return _shell(const Center(
        child: Text('Selecione uma camada de texto',
            style: TextStyle(color: AmColors.muted, fontSize: 13)),
      ));
    }

    final current =
        layer.anims.where((a) => a.slot == _slot).firstOrNull;

    return _shell(Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _slotTabs(layer),
        // UMA superficie rolavel so. A grade era um GridView com
        // shrinkWrap dentro de uma ListView: alem do conflito de gesto,
        // shrinkWrap CONSTROI TUDO de uma vez — trinta e seis
        // miniaturas animadas nascendo juntas era o travamento.
        //
        // Em sliver, a grade nasce preguicosa: a miniatura de fora da
        // tela nao existe, entao nao anima e nao ocupa memoria.
        Expanded(
          child: CustomScrollView(
            slivers: [
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(12, 8, 12, 0),
                sliver: _catalogSliver(layer, controller, current),
              ),
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(12, 0, 12, 20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      if (current != null) ...[
                        const SizedBox(height: 14),
                        _controls(layer, controller, current),
                      ],
                      const SizedBox(height: 16),
                      _advancedSection(layer, controller),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      ],
    ));
  }

  Widget _shell(Widget child) => Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                onPressed: widget.onBack,
                child: const Icon(CupertinoIcons.chevron_left,
                    size: 20, color: AmColors.text),
              ),
              const Text('Animacao de texto',
                  style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      color: AmColors.text)),
            ],
          ),
          Expanded(child: child),
        ],
      );

  Widget _slotTabs(TextLayer layer) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 2, 12, 6),
      child: Row(
        children: [
          for (final s in TextAnimSlot.values)
            Expanded(
              child: GestureDetector(
                onTap: () => setState(() => _slot = s),
                child: Container(
                  margin: const EdgeInsets.only(right: 6),
                  padding: const EdgeInsets.symmetric(vertical: 9),
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color:
                        s == _slot ? AmColors.accentDim : AmColors.chip,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        textAnimSlotLabel(s),
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: s == _slot
                              ? FontWeight.w700
                              : FontWeight.w400,
                          color:
                              s == _slot ? AmColors.accent : AmColors.muted,
                        ),
                      ),
                      // Ponto verde na posicao que ja tem animacao.
                      if (layer.anims.any((a) => a.slot == s)) ...[
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
              ),
            ),
        ],
      ),
    );
  }

  Widget _catalogSliver(
      TextLayer layer, EditorController controller, TextAnim? current) {
    final specs = textAnimsForSlot(_slot);
    final total = specs.length + 1;

    return SliverGrid(
      // COLUNAS PELA LARGURA, nao um numero fixo: tres colunas quebram
      // em tela estreita e desperdicam espaco em tela larga ou deitada.
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
        maxCrossAxisExtent: 132,
        mainAxisSpacing: 8,
        crossAxisSpacing: 8,
        // PROPORCAO FIXA, reservada ANTES de a miniatura existir: o
        // cartao nunca muda de tamanho por causa do conteudo, entao a
        // grade nunca reflui.
        childAspectRatio: 0.86,
      ),
      delegate: SliverChildBuilderDelegate(
        (context, i) {
          if (i == 0) {
            return _tile(
              label: 'Nenhuma',
              selected: current == null,
              preview: const Center(
                child: Icon(CupertinoIcons.nosign,
                    size: 22, color: AmColors.muted),
              ),
              onTap: () => controller.setTextAnim(layer.id, _slot, null),
            );
          }
          final spec = specs[i - 1];
          return _tile(
            label: spec.label,
            selected: current?.specId == spec.id,
            preview: _AnimPreview(
              anim: current?.specId == spec.id
                  ? current!
                  : TextAnim(specId: spec.id, slot: _slot),
            ),
            onTap: () =>
                controller.setTextAnim(layer.id, _slot, spec.id),
          );
        },
        childCount: total,
      ),
    );
  }

  Widget _tile({
    required String label,
    required bool selected,
    required Widget preview,
    required VoidCallback onTap,
  }) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        decoration: BoxDecoration(
          color: selected ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          children: [
            Expanded(child: ClipRect(child: preview)),
            Padding(
              padding: const EdgeInsets.only(bottom: 7, left: 4, right: 4),
              // DUAS linhas com reticencias e altura FIXA: nome longo
              // nao pode esticar um cartao e desalinhar a fileira.
              child: SizedBox(
                height: 26,
                child: Text(
                  label,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 10.5,
                    height: 1.15,
                    color: selected ? AmColors.accent : AmColors.muted,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _controls(
      TextLayer layer, EditorController controller, TextAnim anim) {
    final spec = anim.spec;
    final n = controller.textAnimUnitCount(layer.id, anim.unit);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(anim.label,
                style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text)),
            const Spacer(),
            Text(
              'total ${_ms(anim.totalFor(n))}',
              style: const TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ],
        ),
        const SizedBox(height: 6),
        // O ESCALONAMENTO VISIVEL: uma barra por unidade. Mexer no atraso
        // move as barras, entao da para ver o efeito em vez de imaginar.
        Container(
          height: 46,
          decoration: BoxDecoration(
            color: AmColors.panelHigh,
            borderRadius: BorderRadius.circular(8),
          ),
          padding: const EdgeInsets.all(6),
          child: CustomPaint(
            size: Size.infinite,
            painter: _StaggerPainter(anim: anim, units: math.min(n, 6)),
          ),
        ),
        const SizedBox(height: 8),
        _chips(
          label: 'Unidade',
          options: [
            for (final u in TextAnimUnit.values) textAnimUnitLabel(u)
          ],
          index: anim.unit.index,
          onChanged: (i) => controller.updateTextAnim(layer.id, anim.id,
              (a) => a.copyWith(unit: TextAnimUnit.values[i])),
        ),
        _chips(
          label: 'Ordem',
          options: [
            for (final o in TextAnimOrder.values) textAnimOrderLabel(o)
          ],
          index: anim.order.index,
          onChanged: (i) => controller.updateTextAnim(layer.id, anim.id,
              (a) => a.copyWith(order: TextAnimOrder.values[i])),
        ),
        _chips(
          label: 'Curva',
          options: [
            for (final e in TextAnimEase.values) textAnimEaseLabel(e)
          ],
          index: anim.ease.index,
          onChanged: (i) => controller.updateTextAnim(layer.id, anim.id,
              (a) => a.copyWith(ease: TextAnimEase.values[i])),
        ),
        _slider(
          label: 'Duracao',
          value: anim.duration.inMilliseconds.toDouble(),
          min: 0,
          max: 3000,
          suffix: 'ms',
          onChanged: (v) => controller.updateTextAnim(
              layer.id,
              anim.id,
              (a) => a.copyWith(
                  duration: Duration(milliseconds: v.round()))),
        ),
        if (anim.unit != TextAnimUnit.all)
          _slider(
            label: 'Atraso',
            value: anim.stagger.inMilliseconds.toDouble(),
            min: 0,
            max: 500,
            suffix: 'ms',
            onChanged: (v) => controller.updateTextAnim(
                layer.id,
                anim.id,
                (a) => a.copyWith(
                    stagger: Duration(milliseconds: v.round()))),
          ),
        _slider(
          label: 'Inicio',
          value: anim.start.inMilliseconds.toDouble(),
          min: 0,
          max: 4000,
          suffix: 'ms',
          onChanged: (v) => controller.updateTextAnim(layer.id, anim.id,
              (a) => a.copyWith(start: Duration(milliseconds: v.round()))),
        ),
        if (anim.order == TextAnimOrder.random)
          _slider(
            label: 'Semente',
            value: anim.seed.toDouble(),
            min: 1,
            max: 999,
            onChanged: (v) => controller.updateTextAnim(
                layer.id, anim.id, (a) => a.copyWith(seed: v.round())),
          ),
        // Parametros proprios da animacao escolhida.
        for (final p in spec?.params ?? const <TextAnimParam>[])
          _slider(
            label: p.label,
            value: anim.params[p.key] ?? p.initial,
            min: p.min,
            max: p.max,
            suffix: p.suffix,
            onChanged: (v) => controller.setTextAnimParam(
                layer.id, anim.id, p.key, v),
          ),
        // A MOLA da extensao MultiTools: amplitude, frequencia e
        // decaimento, os mesmos tres numeros.
        if (anim.ease == TextAnimEase.mola) ...[
          const SizedBox(height: 4),
          const Text('Mola',
              style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text)),
          _slider(
            label: 'Amplitude',
            value: anim.amplitude,
            min: 0,
            max: 3,
            decimals: 2,
            onChanged: (v) => controller.updateTextAnim(
                layer.id, anim.id, (a) => a.copyWith(amplitude: v)),
          ),
          _slider(
            label: 'Frequencia',
            value: anim.frequency,
            min: 0.2,
            max: 8,
            decimals: 2,
            onChanged: (v) => controller.updateTextAnim(
                layer.id, anim.id, (a) => a.copyWith(frequency: v)),
          ),
          _slider(
            label: 'Decaimento',
            value: anim.decay,
            min: 0.5,
            max: 20,
            decimals: 2,
            onChanged: (v) => controller.updateTextAnim(
                layer.id, anim.id, (a) => a.copyWith(decay: v)),
          ),
        ],
      ],
    );
  }

  /// MODO AVANCADO: o modelo do After Effects, para quem quer montar
  /// seletor e propriedade na mao. Fica fora do caminho de quem so quer
  /// escolher uma animacao.
  Widget _advancedSection(TextLayer layer, EditorController controller) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        GestureDetector(
          onTap: () => setState(() => _advanced = !_advanced),
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 9),
            decoration: BoxDecoration(
              color: AmColors.chip,
              borderRadius: BorderRadius.circular(10),
            ),
            child: Row(
              children: [
                Icon(
                    _advanced
                        ? CupertinoIcons.chevron_down
                        : CupertinoIcons.chevron_right,
                    size: 13,
                    color: AmColors.muted),
                const SizedBox(width: 8),
                const Text('Avancado (animadores do AE)',
                    style:
                        TextStyle(fontSize: 12, color: AmColors.text)),
                const Spacer(),
                if (layer.animators.isNotEmpty)
                  Text('${layer.animators.length}',
                      style: const TextStyle(
                          fontSize: 11, color: AmColors.accent)),
              ],
            ),
          ),
        ),
        if (_advanced) ...[
          const SizedBox(height: 8),
          const Text(
            'Cada animador combina seletores e propriedades na mao — o '
            'modelo do After Effects. As animacoes acima sao compiladas '
            'para estes mesmos animadores.',
            style: TextStyle(
                fontSize: 11, height: 1.35, color: AmColors.muted),
          ),
          const SizedBox(height: 8),
          for (final a in layer.animators)
            Container(
              margin: const EdgeInsets.only(bottom: 6),
              padding:
                  const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
              decoration: BoxDecoration(
                color: AmColors.panelHigh,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Row(
                children: [
                  GestureDetector(
                    onTap: () =>
                        controller.toggleTextAnimator(layer.id, a.id),
                    child: Icon(
                      a.enabled
                          ? CupertinoIcons.eye
                          : CupertinoIcons.eye_slash,
                      size: 16,
                      color: a.enabled ? AmColors.text : AmColors.muted,
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(a.name,
                            style: const TextStyle(
                                fontSize: 12, color: AmColors.text)),
                        Text(
                          a.properties.isEmpty
                              ? 'sem propriedades'
                              : a.properties
                                  .map((p) => textAnimPropLabel(p.type))
                                  .join(' · '),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(
                              fontSize: 10, color: AmColors.muted),
                        ),
                      ],
                    ),
                  ),
                  GestureDetector(
                    onTap: () =>
                        controller.removeTextAnimator(layer.id, a.id),
                    child: const Icon(CupertinoIcons.trash,
                        size: 15, color: AmColors.muted),
                  ),
                ],
              ),
            ),
          GestureDetector(
            onTap: () => controller.addTextAnimator(layer.id),
            child: Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 11, vertical: 8),
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(9),
              ),
              child: const Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(CupertinoIcons.plus,
                      size: 13, color: AmColors.accent),
                  SizedBox(width: 6),
                  Text('Animador cru',
                      style: TextStyle(
                          fontSize: 12, color: AmColors.accent)),
                ],
              ),
            ),
          ),
        ],
      ],
    );
  }

  // ------------------------------------------------------ controles

  Widget _chips({
    required String label,
    required List<String> options,
    required int index,
    required ValueChanged<int> onChanged,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label,
              style:
                  const TextStyle(fontSize: 11, color: AmColors.muted)),
          const SizedBox(height: 5),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < options.length; i++)
                GestureDetector(
                  onTap: () => onChanged(i),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 10, vertical: 6),
                    decoration: BoxDecoration(
                      color: i == index ? AmColors.accentDim : AmColors.chip,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(options[i],
                        style: TextStyle(
                            fontSize: 11,
                            color: i == index
                                ? AmColors.accent
                                : AmColors.muted)),
                  ),
                ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _slider({
    required String label,
    required double value,
    required double min,
    required double max,
    required ValueChanged<double> onChanged,
    String suffix = '',
    int decimals = 0,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Row(
        children: [
          SizedBox(
            width: 78,
            child: Text(label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style:
                    const TextStyle(fontSize: 11, color: AmColors.muted)),
          ),
          Expanded(
            child: AmTickRuler(
  value: value.clamp(min, max),
  min: min,
  max: max,
  unitsPerPixel: ((max) - (min)) / 420,
  height: 40,
  onChanged: onChanged,
),
          ),
          SizedBox(
            width: 58,
            child: Text(
              '${value.toStringAsFixed(decimals)}$suffix',
              textAlign: TextAlign.right,
              style: const TextStyle(fontSize: 11, color: AmColors.text),
            ),
          ),
        ],
      ),
    );
  }

  static String _ms(Duration d) =>
      '${(d.inMilliseconds / 1000).toStringAsFixed(2)}s';
}

/// PREVIA VIVA de uma animacao do catalogo: tres letras rodando o efeito
/// em loop. E o que faz escolher olhando, em vez de ler nome.
class _AnimPreview extends StatefulWidget {
  const _AnimPreview({required this.anim});

  final TextAnim anim;

  @override
  State<_AnimPreview> createState() => _AnimPreviewState();
}

class _AnimPreviewState extends State<_AnimPreview>
    with SingleTickerProviderStateMixin {
  late final AnimationController _c;

  static const _units = 3;

  @override
  void initState() {
    super.initState();
    _c = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat();
  }

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final anim = widget.anim;
    // Um ciclo: a animacao inteira mais um respiro para dar para ver o
    // resultado antes de recomecar.
    final total = anim.totalFor(_units);
    final cycle = (anim.spec?.loop ?? false)
        ? const Duration(milliseconds: 1600)
        : total + const Duration(milliseconds: 700);
    final compiled = compileTextAnim(anim,
        layerDuration: cycle, unitCount: _units);

    return AnimatedBuilder(
      animation: _c,
      builder: (context, _) {
        final t = Duration(
            microseconds: (_c.value * cycle.inMicroseconds).round());
        return CustomPaint(
          size: Size.infinite,
          painter: _PreviewPainter(animator: compiled, time: t),
        );
      },
    );
  }
}

class _PreviewPainter extends CustomPainter {
  _PreviewPainter({required this.animator, required this.time});

  final TextAnimator animator;
  final Duration time;

  static const _glyphs = ['A', 'b', 'c'];

  @override
  void paint(Canvas canvas, Size size) {
    const n = 3;
    const fontSize = 19.0;
    const advance = 15.0;
    final cx = size.width / 2;
    final cy = size.height / 2;

    for (var i = 0; i < n; i++) {
      final c = animator.coverageAt(i, n, time);

      var dx = 0.0, dy = 0.0, rot = 0.0, blur = 0.0, skew = 0.0, hue = 0.0;
      var sc = 100.0, op = 100.0, scx = 100.0, scy = 100.0;
      var sat = 100.0, bri = 100.0, track = 0.0;
      for (final p in animator.properties) {
        switch (p.type) {
          case TextAnimProp.positionX:
            dx = p.apply(dx, time, c);
          case TextAnimProp.positionY:
            dy = p.apply(dy, time, c);
          case TextAnimProp.rotation:
            rot = p.apply(rot, time, c);
          case TextAnimProp.tracking:
            track = p.apply(track, time, c);
          case TextAnimProp.scale:
            sc = p.apply(sc, time, c);
          case TextAnimProp.opacity:
            op = p.apply(op, time, c);
          case TextAnimProp.scaleX:
            scx = p.apply(scx, time, c);
          case TextAnimProp.scaleY:
            scy = p.apply(scy, time, c);
          case TextAnimProp.blur:
            blur = p.apply(blur, time, c);
          case TextAnimProp.skew:
            skew = p.apply(skew, time, c);
          case TextAnimProp.hue:
            hue = p.apply(hue, time, c);
          case TextAnimProp.saturation:
            sat = p.apply(sat, time, c);
          case TextAnimProp.brightness:
            bri = p.apply(bri, time, c);
          case TextAnimProp.rotationX:
          case TextAnimProp.rotationY:
          case TextAnimProp.positionZ:
            // A miniatura do painel e 2D; o 3D aparece no preview.
            break;
        }
      }

      final opacity = (op / 100).clamp(0.0, 1.0);
      if (opacity <= 0.01) continue;
      final sx = math.max(0.0, sc / 100 * scx / 100);
      final sy = math.max(0.0, sc / 100 * scy / 100);
      if (sx <= 0.01 || sy <= 0.01) continue;

      var color = const Color(0xFFE9EDF2);
      if (hue != 0 || sat != 100 || bri != 100) {
        final hsl = HSLColor.fromColor(color);
        final h = (hsl.hue + hue) % 360;
        color = hsl
            .withHue(h < 0 ? h + 360 : h)
            .withSaturation(
                (hsl.saturation * sat / 100).clamp(0.0, 1.0))
            .withLightness(
                (hsl.lightness * bri / 100).clamp(0.0, 1.0))
            .toColor();
      }

      final tp = TextPainter(
        text: TextSpan(
          text: _glyphs[i],
          style: TextStyle(
            fontSize: fontSize,
            fontWeight: FontWeight.w700,
            color: color.withValues(alpha: opacity),
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();

      // A previa e pequena: o deslocamento entra reduzido, senao a letra
      // sai da miniatura e a pessoa nao ve nada.
      const k = 0.16;
      final x = cx + (i - 1) * (advance + track * k) + dx * k;
      final y = cy + dy * k;

      canvas.save();
      final blurring = blur > 0.4;
      if (blurring) {
        canvas.saveLayer(
          Rect.fromCenter(
              center: Offset(x, y), width: size.width, height: size.height),
          Paint()
            ..imageFilter = ImageFilter.blur(
                sigmaX: blur * 0.22, sigmaY: blur * 0.22),
        );
      }
      canvas.translate(x, y);
      if (rot != 0) canvas.rotate(rot * math.pi / 180);
      if (skew != 0) {
        canvas.transform(Float64List.fromList(<double>[
          1, 0, 0, 0, //
          math.tan(-skew * math.pi / 180), 1, 0, 0, //
          0, 0, 1, 0, //
          0, 0, 0, 1,
        ]));
      }
      canvas.scale(sx, sy);
      tp.paint(canvas, Offset(-tp.width / 2, -tp.height / 2));
      canvas.restore();
      if (blurring) canvas.restore();
    }
  }

  @override
  bool shouldRepaint(_PreviewPainter old) => true;
}

/// ESCALONAMENTO VISIVEL: uma barra por unidade, comecando em
/// i*atraso e durando "duracao".
class _StaggerPainter extends CustomPainter {
  const _StaggerPainter({required this.anim, required this.units});

  final TextAnim anim;
  final int units;

  @override
  void paint(Canvas canvas, Size size) {
    final n = math.max(1, units);
    final total = anim.totalFor(n).inMicroseconds.toDouble();
    if (total <= 0) return;
    final s = anim.stagger.inMicroseconds.toDouble();
    final d = anim.duration.inMicroseconds.toDouble();
    final rowH = size.height / n;
    final paint = Paint()..color = AmColors.accent;
    for (var i = 0; i < n; i++) {
      final idx = orderMapIndex(
          selectorOrderFor(anim.order), i, n, anim.seed);
      final x0 = (idx * s) / total * size.width;
      // Duracao zero (maquina de escrever) ainda precisa ser visivel.
      final w = math.max(2.0, d / total * size.width);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(x0, i * rowH + rowH * 0.18,
              math.min(w, size.width - x0), rowH * 0.64),
          const Radius.circular(2),
        ),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(_StaggerPainter old) =>
      old.anim.stagger != anim.stagger ||
      old.anim.duration != anim.duration ||
      old.anim.order != anim.order ||
      old.units != units;
}
