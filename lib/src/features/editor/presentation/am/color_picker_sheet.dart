import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'am_colors.dart';

/// SELETOR DE COR — qualquer cor, nao uma paleta fixa.
///
/// Espectro de matiz + area de saturacao/brilho + alfa + campo HEX +
/// conta-gotas da paleta do projeto. Devolve a cor viva enquanto se
/// arrasta (via [onChanged]), porque escolher cor olhando o resultado e
/// diferente de escolher e so depois ver.
Future<Color?> showColorPicker(
  BuildContext context, {
  required Color initial,
  ValueChanged<Color>? onChanged,
  bool withAlpha = true,
  List<Color> recent = const [],
}) {
  return showModalBottomSheet<Color>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    barrierColor: Colors.black38,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (ctx) => _ColorPickerSheet(
      initial: initial,
      onChanged: onChanged,
      withAlpha: withAlpha,
      recent: recent,
    ),
  );
}

class _ColorPickerSheet extends StatefulWidget {
  const _ColorPickerSheet({
    required this.initial,
    required this.onChanged,
    required this.withAlpha,
    required this.recent,
  });

  final Color initial;
  final ValueChanged<Color>? onChanged;
  final bool withAlpha;
  final List<Color> recent;

  @override
  State<_ColorPickerSheet> createState() => _ColorPickerSheetState();
}

class _ColorPickerSheetState extends State<_ColorPickerSheet> {
  late HSVColor _hsv;
  late double _alpha;
  late TextEditingController _hex;

  static const _swatches = <Color>[
    Color(0xFFFFFFFF), Color(0xFF000000), Color(0xFFB8FF3D),
    Color(0xFF7C62FF), Color(0xFF35C4E7), Color(0xFF2BE3A0),
    Color(0xFFFFB020), Color(0xFFFF6B6B), Color(0xFFFF4FA3),
    Color(0xFF8B94A3), Color(0xFF1E242E), Color(0xFFE9EDF2),
  ];

  @override
  void initState() {
    super.initState();
    _hsv = HSVColor.fromColor(widget.initial.withValues(alpha: 1));
    _alpha = widget.initial.a;
    _hex = TextEditingController(text: _hexOf(_current));
  }

  @override
  void dispose() {
    _hex.dispose();
    super.dispose();
  }

  Color get _current => _hsv.toColor().withValues(alpha: _alpha);

  static String _hexOf(Color c) {
    String two(double v) =>
        (v * 255).round().clamp(0, 255).toRadixString(16).padLeft(2, '0');
    return '${two(c.r)}${two(c.g)}${two(c.b)}'.toUpperCase();
  }

  void _emit({bool syncHex = true}) {
    if (syncHex) _hex.text = _hexOf(_current);
    widget.onChanged?.call(_current);
    setState(() {});
  }

  void _applyHex(String raw) {
    var s = raw.trim().replaceAll('#', '');
    if (s.length == 3) {
      s = '${s[0]}${s[0]}${s[1]}${s[1]}${s[2]}${s[2]}';
    }
    if (s.length != 6) return;
    final v = int.tryParse(s, radix: 16);
    if (v == null) return;
    _hsv = HSVColor.fromColor(Color(0xFF000000 | v));
    _emit(syncHex: false);
  }

  @override
  Widget build(BuildContext context) {
    final maxH = MediaQuery.of(context).size.height * 0.72;
    return SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(maxHeight: maxH),
        child: SingleChildScrollView(
          padding: EdgeInsets.fromLTRB(
              18, 14, 18, 16 + MediaQuery.of(context).viewInsets.bottom),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const Text('Cor',
                      style: TextStyle(
                          fontSize: 17,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text)),
                  const SizedBox(width: 12),
                  Container(
                    width: 34,
                    height: 22,
                    decoration: BoxDecoration(
                      color: _current,
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(color: AmColors.hairline),
                    ),
                  ),
                  const Spacer(),
                  CupertinoButton(
                    padding: EdgeInsets.zero,
                    onPressed: () => Navigator.of(context).pop(_current),
                    child: const Text('Pronto',
                        style: TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w600,
                            color: AmColors.accent)),
                  ),
                ],
              ),
              const SizedBox(height: 12),

              // AREA SATURACAO x BRILHO.
              LayoutBuilder(
                builder: (context, c) {
                  final w = c.maxWidth;
                  const h = 170.0;
                  return GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onPanDown: (d) => _pickSV(d.localPosition, w, h),
                    onPanUpdate: (d) => _pickSV(d.localPosition, w, h),
                    child: SizedBox(
                      width: w,
                      height: h,
                      child: CustomPaint(
                        painter: _SvPainter(
                          hue: _hsv.hue,
                          saturation: _hsv.saturation,
                          value: _hsv.value,
                        ),
                      ),
                    ),
                  );
                },
              ),
              const SizedBox(height: 14),

              // MATIZ.
              _Strip(
                height: 26,
                painter: _HuePainter(),
                position: _hsv.hue / 360,
                onChanged: (v) {
                  _hsv = _hsv.withHue((v * 360).clamp(0.0, 359.999));
                  _emit();
                },
              ),
              if (widget.withAlpha) ...[
                const SizedBox(height: 10),
                _Strip(
                  height: 26,
                  painter: _AlphaPainter(color: _hsv.toColor()),
                  position: _alpha,
                  onChanged: (v) {
                    _alpha = v.clamp(0.0, 1.0);
                    _emit();
                  },
                ),
              ],
              const SizedBox(height: 14),

              // HEX + valores.
              Row(
                children: [
                  const Text('HEX',
                      style: TextStyle(
                          fontSize: 12, color: AmColors.muted)),
                  const SizedBox(width: 8),
                  SizedBox(
                    width: 110,
                    child: TextField(
                      controller: _hex,
                      onSubmitted: _applyHex,
                      onChanged: (v) {
                        if (v.replaceAll('#', '').length == 6) _applyHex(v);
                      },
                      textCapitalization: TextCapitalization.characters,
                      inputFormatters: [
                        LengthLimitingTextInputFormatter(7),
                        FilteringTextInputFormatter.allow(
                            RegExp(r'[0-9a-fA-F#]')),
                      ],
                      style: const TextStyle(
                          fontSize: 14, color: AmColors.text),
                      decoration: InputDecoration(
                        isDense: true,
                        filled: true,
                        fillColor: AmColors.chip,
                        contentPadding: const EdgeInsets.symmetric(
                            horizontal: 10, vertical: 9),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(8),
                          borderSide: BorderSide.none,
                        ),
                      ),
                    ),
                  ),
                  const Spacer(),
                  Text(
                    widget.withAlpha
                        ? '${(_alpha * 100).round()}%'
                        : '',
                    style: const TextStyle(
                        fontSize: 12, color: AmColors.muted),
                  ),
                ],
              ),
              const SizedBox(height: 14),

              const Text('Rapidas',
                  style: TextStyle(fontSize: 12, color: AmColors.muted)),
              const SizedBox(height: 8),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                children: [
                  for (final c in [...widget.recent, ..._swatches])
                    GestureDetector(
                      onTap: () {
                        _hsv = HSVColor.fromColor(c.withValues(alpha: 1));
                        if (widget.withAlpha) _alpha = c.a;
                        _emit();
                      },
                      child: Container(
                        width: 30,
                        height: 30,
                        decoration: BoxDecoration(
                          color: c,
                          shape: BoxShape.circle,
                          border: Border.all(
                            color: c.toARGB32() == _current.toARGB32()
                                ? AmColors.accent
                                : AmColors.hairline,
                            width: 2,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  void _pickSV(Offset p, double w, double h) {
    _hsv = _hsv
        .withSaturation((p.dx / w).clamp(0.0, 1.0))
        .withValue((1 - p.dy / h).clamp(0.0, 1.0));
    _emit();
  }
}

class _Strip extends StatelessWidget {
  const _Strip({
    required this.height,
    required this.painter,
    required this.position,
    required this.onChanged,
  });

  final double height;
  final CustomPainter painter;
  final double position;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        final w = c.maxWidth;
        void pick(Offset p) => onChanged((p.dx / w).clamp(0.0, 1.0));
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanDown: (d) => pick(d.localPosition),
          onPanUpdate: (d) => pick(d.localPosition),
          child: SizedBox(
            width: w,
            height: height,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(8),
                  child: CustomPaint(
                      size: Size(w, height), painter: painter),
                ),
                Positioned(
                  left: (position.clamp(0.0, 1.0) * w) - 7,
                  top: -2,
                  child: Container(
                    width: 14,
                    height: height + 4,
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(7),
                      border: Border.all(color: Colors.white, width: 2.5),
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _SvPainter extends CustomPainter {
  const _SvPainter({
    required this.hue,
    required this.saturation,
    required this.value,
  });

  final double hue;
  final double saturation;
  final double value;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final rrect =
        RRect.fromRectAndRadius(rect, const Radius.circular(10));
    canvas.save();
    canvas.clipRRect(rrect);

    // Base: branco -> matiz pura.
    canvas.drawRect(
      rect,
      Paint()
        ..shader = LinearGradient(
          colors: [
            Colors.white,
            HSVColor.fromAHSV(1, hue, 1, 1).toColor(),
          ],
        ).createShader(rect),
    );
    // Por cima: transparente -> preto.
    canvas.drawRect(
      rect,
      Paint()
        ..shader = const LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [Colors.transparent, Colors.black],
        ).createShader(rect),
    );
    canvas.restore();

    final p = Offset(saturation * size.width, (1 - value) * size.height);
    canvas.drawCircle(
        p, 9, Paint()..color = Colors.white.withValues(alpha: 0.9)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2.5);
    canvas.drawCircle(
        p, 9, Paint()..color = Colors.black.withValues(alpha: 0.35)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1);
  }

  @override
  bool shouldRepaint(_SvPainter old) =>
      old.hue != hue ||
      old.saturation != saturation ||
      old.value != value;
}

class _HuePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    canvas.drawRect(
      rect,
      Paint()
        ..shader = LinearGradient(
          colors: [
            for (var i = 0; i <= 6; i++)
              HSVColor.fromAHSV(1, i * 59.99, 1, 1).toColor(),
          ],
        ).createShader(rect),
    );
  }

  @override
  bool shouldRepaint(_HuePainter old) => false;
}

class _AlphaPainter extends CustomPainter {
  const _AlphaPainter({required this.color});

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    // Xadrez, para transparencia ficar legivel.
    const s = 7.0;
    final a = Paint()..color = const Color(0xFF3A4150);
    final b = Paint()..color = const Color(0xFF2A303B);
    for (var y = 0.0; y < size.height; y += s) {
      for (var x = 0.0; x < size.width; x += s) {
        final even = ((x / s).floor() + (y / s).floor()).isEven;
        canvas.drawRect(
            Rect.fromLTWH(x, y, s, s), even ? a : b);
      }
    }
    final rect = Offset.zero & size;
    canvas.drawRect(
      rect,
      Paint()
        ..shader = LinearGradient(
          colors: [color.withValues(alpha: 0), color],
        ).createShader(rect),
    );
  }

  @override
  bool shouldRepaint(_AlphaPainter old) => old.color != color;
}

/// Botao de cor padrao: mostra a cor e abre o seletor completo.
class ColorWell extends StatelessWidget {
  const ColorWell({
    super.key,
    required this.color,
    required this.onChanged,
    this.label,
    this.size = 30,
    this.withAlpha = true,
    this.recent = const [],
  });

  final Color color;
  final ValueChanged<Color> onChanged;
  final String? label;
  final double size;
  final bool withAlpha;
  final List<Color> recent;

  @override
  Widget build(BuildContext context) {
    final well = GestureDetector(
      onTap: () async {
        final before = color;
        final picked = await showColorPicker(
          context,
          initial: before,
          withAlpha: withAlpha,
          recent: recent,
          onChanged: onChanged,
        );
        // Fechar arrastando mantem a ultima cor vista, que e o que a
        // pessoa acabou de escolher olhando.
        if (picked != null) onChanged(picked);
      },
      child: Container(
        width: size,
        height: size,
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(math.min(8, size / 3)),
          border: Border.all(color: AmColors.hairline, width: 1.5),
        ),
      ),
    );
    if (label == null) return well;
    return Row(
      children: [
        Expanded(
          child: Text(label!,
              style:
                  const TextStyle(fontSize: 12, color: AmColors.muted)),
        ),
        well,
      ],
    );
  }
}
