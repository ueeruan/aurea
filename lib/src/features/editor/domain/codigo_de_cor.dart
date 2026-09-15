import 'dart:ui' show Color;

/// CODIGOS DE COR para copiar e colar entre camadas, apps e sites.
///
/// Sai como #RRGGBB (ou #RRGGBBAA quando a cor e translucida) e como
/// rgba(r, g, b, a). Entra em qualquer um destes: #RGB, #RGBA, #RRGGBB,
/// #RRGGBBAA, 0xAARRGGBB, rgb(...) e rgba(...), com canais de 0 a 255
/// ou em porcentagem.
String codigoHexDaCor(Color c, {bool comAlfa = true}) {
  String dois(double v) => (v * 255)
      .round()
      .clamp(0, 255)
      .toRadixString(16)
      .padLeft(2, '0')
      .toUpperCase();
  final base = '#${dois(c.r)}${dois(c.g)}${dois(c.b)}';
  return comAlfa && c.a < .999 ? '$base${dois(c.a)}' : base;
}

String codigoRgbaDaCor(Color c) {
  int canal(double v) => (v * 255).round().clamp(0, 255);
  final alfa = c.a
      .clamp(0.0, 1.0)
      .toStringAsFixed(2)
      .replaceAll(RegExp(r'\.?0+$'), '');
  return 'rgba(${canal(c.r)}, ${canal(c.g)}, ${canal(c.b)}, '
      '${alfa.isEmpty ? '0' : alfa})';
}

/// A cor escrita em [bruto], ou nulo quando nao e um codigo de cor.
Color? corDoCodigo(String bruto) {
  final s = bruto.trim().toLowerCase();
  if (s.isEmpty) return null;

  final funcao = RegExp(
    r'^rgba?\(\s*([\d.]+%?)\s*[,\s]\s*([\d.]+%?)\s*[,\s]\s*([\d.]+%?)'
    r'(?:\s*[,/]\s*([\d.]+%?))?\s*\)$',
  ).firstMatch(s);
  if (funcao != null) {
    double? canal(String v) {
      if (v.endsWith('%')) {
        final p = double.tryParse(v.substring(0, v.length - 1));
        return p == null ? null : p / 100;
      }
      final n = double.tryParse(v);
      return n == null ? null : n / 255;
    }

    double? alfa(String? v) {
      if (v == null) return 1;
      if (v.endsWith('%')) {
        final p = double.tryParse(v.substring(0, v.length - 1));
        return p == null ? null : p / 100;
      }
      final n = double.tryParse(v);
      if (n == null) return null;
      return n > 1 ? n / 255 : n;
    }

    final r = canal(funcao.group(1)!);
    final g = canal(funcao.group(2)!);
    final b = canal(funcao.group(3)!);
    final a = alfa(funcao.group(4));
    if (r == null || g == null || b == null || a == null) return null;
    if ([r, g, b, a].any((v) => v < 0 || v > 1)) return null;
    return Color.from(alpha: a, red: r, green: g, blue: b);
  }

  if (s.startsWith('0x')) {
    final h = s.substring(2);
    if (!RegExp(r'^[0-9a-f]{8}$').hasMatch(h)) return null;
    return Color(int.parse(h, radix: 16));
  }

  var h = s.startsWith('#') ? s.substring(1) : s;
  if (!RegExp(r'^[0-9a-f]+$').hasMatch(h)) return null;
  if (h.length == 3 || h.length == 4) {
    h = h.split('').map((c) => '$c$c').join();
  }
  if (h.length == 6) return Color(0xFF000000 | int.parse(h, radix: 16));
  if (h.length == 8) {
    final v = int.parse(h, radix: 16);
    return Color(((v & 0xFF) << 24) | (v >> 8));
  }
  return null;
}
