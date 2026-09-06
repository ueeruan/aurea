import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'mask.dart';

/// Parser de path data SVG -> [Path] (spec AUREA-atualizacao §6.4):
/// o icone entra como CAMINHO vetorial editavel, nunca como imagem.
/// Suporta M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z.
Path parseSvgPathData(String data) {
  final path = Path();
  final tokens = _tokenize(data);
  var i = 0;

  var cx = 0.0, cy = 0.0; // ponto atual
  var sx = 0.0, sy = 0.0; // inicio do subcaminho
  var lastCx = 0.0, lastCy = 0.0; // controle anterior (S/T)
  var lastCmd = '';

  double num_() => tokens[i++] as double;

  while (i < tokens.length) {
    var cmd = tokens[i] is String ? tokens[i++] as String : lastCmd;
    // Comando implicito: apos M vem L; apos m vem l.
    if (lastCmd == 'M' && cmd == 'M' && tokens[i - 1] is! String) cmd = 'L';
    if (lastCmd == 'm' && cmd == 'm' && tokens[i - 1] is! String) cmd = 'l';

    switch (cmd) {
      case 'M':
        cx = num_();
        cy = num_();
        path.moveTo(cx, cy);
        sx = cx;
        sy = cy;
        lastCmd = 'M';
      case 'm':
        cx += num_();
        cy += num_();
        path.moveTo(cx, cy);
        sx = cx;
        sy = cy;
        lastCmd = 'm';
      case 'L':
        cx = num_();
        cy = num_();
        path.lineTo(cx, cy);
        lastCmd = 'L';
      case 'l':
        cx += num_();
        cy += num_();
        path.lineTo(cx, cy);
        lastCmd = 'l';
      case 'H':
        cx = num_();
        path.lineTo(cx, cy);
        lastCmd = 'H';
      case 'h':
        cx += num_();
        path.lineTo(cx, cy);
        lastCmd = 'h';
      case 'V':
        cy = num_();
        path.lineTo(cx, cy);
        lastCmd = 'V';
      case 'v':
        cy += num_();
        path.lineTo(cx, cy);
        lastCmd = 'v';
      case 'C':
      case 'c':
        final rel = cmd == 'c';
        final x1 = (rel ? cx : 0) + num_();
        final y1 = (rel ? cy : 0) + num_();
        final x2 = (rel ? cx : 0) + num_();
        final y2 = (rel ? cy : 0) + num_();
        final x = (rel ? cx : 0) + num_();
        final y = (rel ? cy : 0) + num_();
        path.cubicTo(x1, y1, x2, y2, x, y);
        lastCx = x2;
        lastCy = y2;
        cx = x;
        cy = y;
        lastCmd = 'C';
      case 'S':
      case 's':
        final rel = cmd == 's';
        final x1 = lastCmd == 'C' || lastCmd == 'S'
            ? 2 * cx - lastCx
            : cx;
        final y1 = lastCmd == 'C' || lastCmd == 'S'
            ? 2 * cy - lastCy
            : cy;
        final x2 = (rel ? cx : 0) + num_();
        final y2 = (rel ? cy : 0) + num_();
        final x = (rel ? cx : 0) + num_();
        final y = (rel ? cy : 0) + num_();
        path.cubicTo(x1, y1, x2, y2, x, y);
        lastCx = x2;
        lastCy = y2;
        cx = x;
        cy = y;
        lastCmd = 'S';
      case 'Q':
      case 'q':
        final rel = cmd == 'q';
        final x1 = (rel ? cx : 0) + num_();
        final y1 = (rel ? cy : 0) + num_();
        final x = (rel ? cx : 0) + num_();
        final y = (rel ? cy : 0) + num_();
        path.quadraticBezierTo(x1, y1, x, y);
        lastCx = x1;
        lastCy = y1;
        cx = x;
        cy = y;
        lastCmd = 'Q';
      case 'T':
      case 't':
        final rel = cmd == 't';
        final x1 = lastCmd == 'Q' || lastCmd == 'T'
            ? 2 * cx - lastCx
            : cx;
        final y1 = lastCmd == 'Q' || lastCmd == 'T'
            ? 2 * cy - lastCy
            : cy;
        final x = (rel ? cx : 0) + num_();
        final y = (rel ? cy : 0) + num_();
        path.quadraticBezierTo(x1, y1, x, y);
        lastCx = x1;
        lastCy = y1;
        cx = x;
        cy = y;
        lastCmd = 'T';
      case 'A':
      case 'a':
        final rel = cmd == 'a';
        final rx = num_();
        final ry = num_();
        final rot = num_();
        final largeArc = num_() != 0;
        final sweep = num_() != 0;
        final x = (rel ? cx : 0) + num_();
        final y = (rel ? cy : 0) + num_();
        _arcTo(path, cx, cy, x, y, rx, ry, rot, largeArc, sweep);
        cx = x;
        cy = y;
        lastCmd = 'A';
      case 'Z':
      case 'z':
        path.close();
        cx = sx;
        cy = sy;
        lastCmd = 'Z';
      default:
        // Token desconhecido: aborta com o que deu para ler.
        return path;
    }
  }
  return path;
}

/// Arco eliptico SVG -> arcToPoint do Flutter (mesma semantica).
void _arcTo(Path path, double x0, double y0, double x, double y, double rx,
    double ry, double rotDeg, bool largeArc, bool sweep) {
  if (rx <= 0 || ry <= 0) {
    path.lineTo(x, y);
    return;
  }
  path.arcToPoint(
    Offset(x, y),
    radius: Radius.elliptical(rx, ry),
    rotation: rotDeg,
    largeArc: largeArc,
    clockwise: sweep,
  );
}

/// Divide o path data em comandos (String) e numeros (double).
List<Object> _tokenize(String d) {
  final out = <Object>[];
  final re = RegExp(r'([MmLlHhVvCcSsQqTtAaZz])|(-?\d*\.?\d+(?:[eE][-+]?\d+)?)');
  for (final m in re.allMatches(d)) {
    if (m.group(1) != null) {
      out.add(m.group(1)!);
    } else {
      out.add(double.parse(m.group(2)!));
    }
  }
  return out;
}

/// Normaliza um path para caber centrado num box de [size] logicos
/// (usado ao inserir icones: viewBox varia por conjunto).
Path fitPathToBox(Path source, double size) {
  final b = source.getBounds();
  if (b.isEmpty) return source;
  final s = size / math.max(b.width, b.height);
  return source.transform(Float64List.fromList([
    s, 0, 0, 0, //
    0, s, 0, 0, //
    0, 0, 1, 0, //
    -b.center.dx * s, -b.center.dy * s, 0, 1,
  ]));
}

/// SVG PATH DATA -> [BezierPath], com os NOS de verdade.
///
/// [parseSvgPathData] devolve um Path do Flutter, que e uma caixa preta:
/// da para desenhar, nao da para pegar um no com o dedo nem para
/// interpolar ate outra forma. Este parser guarda cada ancora e as duas
/// tangentes, que e o que o editor de nos e o morph precisam.
///
/// Suporta M/m L/l H/h V/v C/c S/s Q/q T/t Z/z. Quadratica vira cubica
/// (elevacao de grau exata). Arco (A/a) vira reta ate o ponto final —
/// raro em icone, e fiel o bastante para editar depois. So o PRIMEIRO
/// subcaminho entra: um BezierPath e um contorno so.
BezierPath svgPathToBezier(String data) {
  final tokens = _tokenize(data);
  var i = 0;

  // Ancoras e tangentes montadas aos poucos: PathVertex e imutavel.
  final pts = <Offset>[];
  final ins = <Offset>[];
  final outs = <Offset>[];
  final corners = <bool>[];
  var closed = false;

  var cx = 0.0, cy = 0.0;
  var lastCx = 0.0, lastCy = 0.0;
  var lastCmd = '';
  var subcaminhos = 0;

  double num_() => tokens[i++] as double;

  void ancora(Offset p, {Offset inT = Offset.zero, bool corner = true}) {
    pts.add(p);
    ins.add(inT);
    outs.add(Offset.zero);
    corners.add(corner);
  }

  void saidaDoUltimo(Offset outT) {
    if (outs.isNotEmpty) {
      outs[outs.length - 1] = outT;
      corners[corners.length - 1] = false;
    }
  }

  void cubica(double x1, double y1, double x2, double y2, double x, double y) {
    saidaDoUltimo(Offset(x1 - cx, y1 - cy));
    ancora(Offset(x, y), inT: Offset(x2 - x, y2 - y), corner: false);
    lastCx = x2;
    lastCy = y2;
    cx = x;
    cy = y;
  }

  void quadratica(double qx, double qy, double x, double y) {
    // Elevacao de grau: os controles cubicos ficam a 2/3 do caminho.
    final x1 = cx + 2 / 3 * (qx - cx);
    final y1 = cy + 2 / 3 * (qy - cy);
    final x2 = x + 2 / 3 * (qx - x);
    final y2 = y + 2 / 3 * (qy - y);
    saidaDoUltimo(Offset(x1 - cx, y1 - cy));
    ancora(Offset(x, y), inT: Offset(x2 - x, y2 - y), corner: false);
    lastCx = qx;
    lastCy = qy;
    cx = x;
    cy = y;
  }

  void reta(double x, double y) {
    ancora(Offset(x, y));
    cx = x;
    cy = y;
  }

  bool ultimoFoiCubica() =>
      lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's';
  bool ultimoFoiQuadratica() =>
      lastCmd == 'Q' || lastCmd == 'q' || lastCmd == 'T' || lastCmd == 't';

  loop:
  while (i < tokens.length) {
    var cmd = tokens[i] is String ? tokens[i++] as String : lastCmd;
    if (lastCmd == 'M' && cmd == 'M' && tokens[i - 1] is! String) cmd = 'L';
    if (lastCmd == 'm' && cmd == 'm' && tokens[i - 1] is! String) cmd = 'l';

    switch (cmd) {
      case 'M':
      case 'm':
        if (subcaminhos > 0) break loop; // so o primeiro contorno
        subcaminhos++;
        if (cmd == 'M') {
          cx = num_();
          cy = num_();
        } else {
          cx += num_();
          cy += num_();
        }
        ancora(Offset(cx, cy));
        lastCmd = cmd;
      case 'L':
        reta(num_(), num_());
        lastCmd = 'L';
      case 'l':
        final dx = num_(), dy = num_();
        reta(cx + dx, cy + dy);
        lastCmd = 'l';
      case 'H':
        reta(num_(), cy);
        lastCmd = 'H';
      case 'h':
        reta(cx + num_(), cy);
        lastCmd = 'h';
      case 'V':
        reta(cx, num_());
        lastCmd = 'V';
      case 'v':
        reta(cx, cy + num_());
        lastCmd = 'v';
      case 'C':
        final x1 = num_(), y1 = num_(), x2 = num_(), y2 = num_();
        final x = num_(), y = num_();
        cubica(x1, y1, x2, y2, x, y);
        lastCmd = 'C';
      case 'c':
        final x1 = cx + num_(), y1 = cy + num_();
        final x2 = cx + num_(), y2 = cy + num_();
        final x = cx + num_(), y = cy + num_();
        cubica(x1, y1, x2, y2, x, y);
        lastCmd = 'c';
      case 'S':
      case 's':
        final rel = cmd == 's';
        final x2 = (rel ? cx : 0) + num_(), y2 = (rel ? cy : 0) + num_();
        final x = (rel ? cx : 0) + num_(), y = (rel ? cy : 0) + num_();
        // O primeiro controle e o reflexo do anterior, se houve cubica.
        final x1 = ultimoFoiCubica() ? 2 * cx - lastCx : cx;
        final y1 = ultimoFoiCubica() ? 2 * cy - lastCy : cy;
        cubica(x1, y1, x2, y2, x, y);
        lastCmd = cmd;
      case 'Q':
        final qx = num_(), qy = num_(), x = num_(), y = num_();
        quadratica(qx, qy, x, y);
        lastCmd = 'Q';
      case 'q':
        final qx = cx + num_(), qy = cy + num_();
        final x = cx + num_(), y = cy + num_();
        quadratica(qx, qy, x, y);
        lastCmd = 'q';
      case 'T':
      case 't':
        final rel = cmd == 't';
        final x = (rel ? cx : 0) + num_(), y = (rel ? cy : 0) + num_();
        final qx = ultimoFoiQuadratica() ? 2 * cx - lastCx : cx;
        final qy = ultimoFoiQuadratica() ? 2 * cy - lastCy : cy;
        quadratica(qx, qy, x, y);
        lastCmd = cmd;
      case 'A':
      case 'a':
        final rel = cmd == 'a';
        num_(); num_(); num_(); num_(); num_(); // rx ry rot large sweep
        final x = (rel ? cx : 0) + num_(), y = (rel ? cy : 0) + num_();
        reta(x, y);
        lastCmd = cmd;
      case 'Z':
      case 'z':
        closed = true;
        lastCmd = cmd;
        break loop;
      default:
        i++;
    }
  }

  // Fechado com o ultimo no em cima do primeiro: e o mesmo no. Fundir
  // evita um segmento de comprimento zero que estraga o morph.
  if (closed && pts.length > 1 && (pts.last - pts.first).distance < 1e-6) {
    ins[0] = ins.last;
    if (!corners.last) corners[0] = false;
    pts.removeLast();
    ins.removeLast();
    outs.removeLast();
    corners.removeLast();
  }

  return BezierPath(
    closed: closed,
    vertices: [
      for (var k = 0; k < pts.length; k++)
        PathVertex(p: pts[k], inT: ins[k], outT: outs[k], corner: corners[k]),
    ],
  );
}
