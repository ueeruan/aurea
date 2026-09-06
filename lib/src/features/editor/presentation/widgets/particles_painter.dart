import 'dart:math' as math;

import 'package:flutter/rendering.dart';

import '../../domain/layer.dart';

/// Pinta o sistema de particulas em ESPACO 3D de verdade (estilo
/// Particular / CC Particle World): cada particula vive em (x, y, z) no
/// mundo, com velocidade 3D; a rotacao do sistema (da camada ou herdada
/// do nulo 3D) gira a NUVEM inteira no espaco — nao inclina o canvas
/// como um cartao. Cada particula e projetada em perspectiva
/// individualmente (posicao E tamanho convergem pro ponto de fuga) e
/// desenhada em ordem de profundidade (longe primeiro).
///
/// Simulacao PURA: cada particula e funcao de (seed, indice, tempo) —
/// scrub para frente/tras da o mesmo frame, nada acumula estado (mesmo
/// invariante I1 do motor de texto). Vento, resistencia do ar e
/// gravidade entram em FORMA FECHADA (dv/dt = g - k v), a turbulencia e
/// um campo de ruido 3D lido na posicao da particula, e o rastro sao
/// amostras da mesma trajetoria em idades anteriores.
class ParticlesPainter extends CustomPainter {
  const ParticlesPainter({
    required this.layer,
    required this.time,
    this.rotXDeg = 0,
    this.rotYDeg = 0,
    this.rotZDeg = 0,
  });

  final ParticlesLayer layer;
  final Duration time;

  /// Rotacao do SISTEMA (camada + delta herdado do pai 3D), em graus.
  final double rotXDeg;
  final double rotYDeg;
  final double rotZDeg;

  static const double _focal = 1200;

  /// xorshift32 de (seed, i, canal) -> [0,1).
  double _rand(int i, int channel) {
    var s = (layer.seed * 0x9E3779B9 ^ (i + 1) * 0x85EBCA6B ^
            (channel + 1) * 0xC2B2AE35) &
        0xFFFFFFFF;
    s ^= (s << 13) & 0xFFFFFFFF;
    s ^= s >> 17;
    s ^= (s << 5) & 0xFFFFFFFF;
    return (s & 0xFFFFFF) / 0x1000000;
  }

  /// Hash inteiro de um no da grade do ruido -> [0,1).
  static double _hash3(int x, int y, int z, int seed) {
    var h = (x * 0x27D4EB2D) ^ (y * 0x165667B1) ^ (z * 0x9E3779B1) ^
        (seed * 0x85EBCA6B);
    h &= 0xFFFFFFFF;
    h ^= h >> 15;
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF;
    h ^= h >> 12;
    h = (h * 0x297A2D39) & 0xFFFFFFFF;
    h ^= h >> 15;
    return (h & 0xFFFFFF) / 0x1000000;
  }

  /// Ruido de valor 3D suavizado, em [0,1).
  static double _noise3(double x, double y, double z, int seed) {
    final x0 = x.floor(), y0 = y.floor(), z0 = z.floor();
    final fx = x - x0, fy = y - y0, fz = z - z0;
    final ux = fx * fx * (3 - 2 * fx);
    final uy = fy * fy * (3 - 2 * fy);
    final uz = fz * fz * (3 - 2 * fz);
    double l(double a, double b, double t) => a + (b - a) * t;
    final c00 =
        l(_hash3(x0, y0, z0, seed), _hash3(x0 + 1, y0, z0, seed), ux);
    final c10 = l(
        _hash3(x0, y0 + 1, z0, seed), _hash3(x0 + 1, y0 + 1, z0, seed), ux);
    final c01 = l(
        _hash3(x0, y0, z0 + 1, seed), _hash3(x0 + 1, y0, z0 + 1, seed), ux);
    final c11 = l(_hash3(x0, y0 + 1, z0 + 1, seed),
        _hash3(x0 + 1, y0 + 1, z0 + 1, seed), ux);
    return l(l(c00, c10, uy), l(c01, c11, uy), uz);
  }

  /// Posicao em um eixo apos [t] segundos: velocidade inicial [v0] que
  /// decai com a resistencia [k], vento [w] (deriva) e gravidade [g]
  /// (com [k] > 0 chega a velocidade terminal g/k).
  static double _integra(
      double x0, double v0, double g, double w, double k, double t) {
    if (k < 1e-6) return x0 + (v0 + w) * t + 0.5 * g * t * t;
    final e = (1 - math.exp(-k * t)) / k;
    return x0 + v0 * e + w * t + g * (t - e) / k;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final life = math.max(0.2, layer.lifetimeMs / 1000.0);
    final tSec = time.inMicroseconds / 1e6;

    final rx = rotXDeg * math.pi / 180;
    final ry = rotYDeg * math.pi / 180;
    final rz = rotZDeg * math.pi / 180;
    final cxr = math.cos(rx), sxr = math.sin(rx);
    final cyr = math.cos(ry), syr = math.sin(ry);
    final czr = math.cos(rz), szr = math.sin(rz);

    final k = layer.drag.clamp(0.0, 20.0);
    final turb = layer.turbulence;
    final turbF = 1 / math.max(8.0, layer.turbulenceScale);
    final turbT = tSec * layer.turbulenceSpeed * 0.35;
    final nGhost = layer.trail <= 0.001 ? 0 : (layer.trail * 6).ceil();
    final ghostGap = 0.05 * (0.5 + layer.trail);

    // 1a passada: simula + projeta; 2a: desenha do fundo pra frente.
    final drawList = <_Proj>[];

    for (var i = 0; i < layer.count; i++) {
      // Vida propria: parte das particulas vive menos (Life Random).
      final lifeI = life *
          (1 - layer.lifeRandom.clamp(0.0, 1.0) * _rand(i, 14) * 0.8);
      // Emissor em REGIME CONTINUO (pre-roll): o campo ja nasce cheio no
      // frame 0, como se o sistema rodasse desde sempre; cada particula
      // "renasce" ao fim da vida, com fase propria.
      final phase = _rand(i, 0) * lifeI;
      var age = (tSec - phase) % lifeI;
      if (age < 0) age += lifeI;

      // NASCIMENTO: depende do emissor.
      double bx, by, bz;
      switch (layer.emitter) {
        case 1: // ponto
          bx = 0;
          by = 0;
          bz = 0;
        case 2: // esfera cheia
          final cz = 2 * _rand(i, 15) - 1;
          final ph = 2 * math.pi * _rand(i, 16);
          final rr = math.sqrt(1 - cz * cz);
          final rad = layer.emitW / 2 * math.pow(_rand(i, 19), 1 / 3);
          bx = rr * math.cos(ph) * rad;
          by = rr * math.sin(ph) * rad;
          bz = cz * rad;
        case 3: // anel no plano XY
          final ang = 2 * math.pi * _rand(i, 18);
          bx = math.cos(ang) * layer.emitW / 2;
          by = math.sin(ang) * layer.emitW / 2;
          bz = (_rand(i, 3) - 0.5) * layer.depth;
        default: // caixa
          bx = (_rand(i, 6) - 0.5) * layer.emitW;
          by = (_rand(i, 7) - 0.5) * layer.emitH;
          bz = (_rand(i, 3) - 0.5) * layer.depth;
      }

      // VELOCIDADE INICIAL: cone, todas as direcoes ou para fora.
      final v0 = layer.speed * (0.5 + _rand(i, 2));
      double vx, vy, vz;
      switch (layer.emitMode) {
        case 1: // esfera
          final cz = 2 * _rand(i, 15) - 1;
          final ph = 2 * math.pi * _rand(i, 16);
          final rr = math.sqrt(1 - cz * cz);
          vx = rr * math.cos(ph) * v0;
          vy = rr * math.sin(ph) * v0;
          vz = cz * v0;
        case 2: // para fora do centro
          final len = math.sqrt(bx * bx + by * by + bz * bz);
          if (len < 1e-3) {
            final cz = 2 * _rand(i, 15) - 1;
            final ph = 2 * math.pi * _rand(i, 16);
            final rr = math.sqrt(1 - cz * cz);
            vx = rr * math.cos(ph) * v0;
            vy = rr * math.sin(ph) * v0;
            vz = cz * v0;
          } else {
            vx = bx / len * v0;
            vy = by / len * v0;
            vz = bz / len * v0;
          }
        default: // cone no plano XY + componente em Z
          final dir = (layer.directionDeg +
                  (_rand(i, 1) - 0.5) * layer.spreadDeg) *
              math.pi /
              180;
          vx = math.cos(dir) * v0;
          vy = math.sin(dir) * v0;
          vz = (_rand(i, 10) - 0.5) * layer.speed;
      }

      // TRAJETORIA (forma fechada) em uma idade qualquer.
      (double, double, double) posAt(double a) {
        var px = _integra(bx, vx, 0, layer.windX, k, a);
        var py = _integra(by, vy, layer.gravity, layer.windY, k, a);
        var pz = _integra(bz, vz, 0, 0, k, a);
        if (turb > 0) {
          // Campo de ruido lido na posicao: cada particula sente uma
          // direcao diferente, e o campo evolui no tempo. Entra com
          // rampa para a particula nascer no lugar certo.
          final ramp = (a / 0.6).clamp(0.0, 1.0) * turb;
          final nx = px * turbF, ny = py * turbF, nz = pz * turbF + turbT;
          px += (_noise3(nx, ny, nz, layer.seed) - 0.5) * 2 * ramp;
          py += (_noise3(nx + 31.7, ny, nz, layer.seed + 1) - 0.5) *
              2 *
              ramp;
          pz += (_noise3(nx, ny + 47.3, nz, layer.seed + 2) - 0.5) *
              2 *
              ramp;
        }
        return (px, py, pz);
      }

      // Rotacao do sistema + projecao por particula.
      _Proj? projeta(double a, double alphaMul, double radiusMul) {
        final (px, py, pz) = posAt(a);
        final y1 = py * cxr - pz * sxr;
        final z1 = py * sxr + pz * cxr;
        final x1 = px * cyr + z1 * syr;
        final z2 = -px * syr + z1 * cyr;
        final wx = x1 * czr - y1 * szr;
        final wy = x1 * szr + y1 * czr;
        final wz = z2;
        final denom = _focal + wz;
        if (denom < 60) return null;
        final proj = (_focal / denom).clamp(0.02, 6.0);
        final p = center + Offset(wx, wy) * proj;

        // OPACIDADE NA VIDA.
        final uu = (a / lifeI).clamp(0.0, 1.0);
        double alpha;
        switch (layer.opacityOverLife) {
          case 1:
            alpha = (uu < 0.04 ? uu / 0.04 : 1.0) * (1 - uu);
          case 2:
            alpha = uu;
          case 3:
            alpha = (uu / 0.03).clamp(0.0, 1.0) *
                ((1 - uu) / 0.03).clamp(0.0, 1.0);
          default:
            alpha = (uu / 0.08).clamp(0.0, 1.0) *
                ((1 - uu) / 0.35).clamp(0.0, 1.0);
        }
        alpha *= 1 - layer.opacityRandom.clamp(0.0, 1.0) * _rand(i, 12);

        // Cintilar: oscila com frequencia e fase proprias.
        if (layer.twinkle) {
          final tw = 0.5 +
              0.5 *
                  math.sin(
                      (tSec * (0.7 + _rand(i, 8) * 1.5) + _rand(i, 9)) *
                          2 *
                          math.pi);
          alpha *= 0.30 + 0.70 * tw;
        }
        alpha *= alphaMul;
        if (alpha <= 0.01) return null;

        // TAMANHO NA VIDA.
        double vida;
        switch (layer.sizeOverLife) {
          case 1:
            vida = 0.15 + 0.85 * uu;
          case 2:
            vida = 1 - 0.85 * uu;
          case 3:
            vida = 0.15 + 0.85 * math.sin(uu * math.pi);
          default:
            vida = 1;
        }
        final espalha =
            1 + (_rand(i, 4) - 0.5) * layer.sizeRandom * 1.8;
        final r = layer.size * espalha * vida * proj * 0.5 * radiusMul;
        if (r < 0.3) return null;

        return _Proj(
          z: wz,
          pos: p,
          radius: r,
          alpha: alpha,
          variant: _rand(i, 5),
          u: uu,
          angle: (layer.spin * a + _rand(i, 13) * 360) * math.pi / 180,
        );
      }

      final main = projeta(age, 1, 1);
      if (main == null) continue;
      // RISCO: precisa de um ponto anterior para dar a direcao.
      if (layer.shape == 2) {
        final atras = projeta(math.max(0, age - 0.045), 1, 1);
        main.tail = atras?.pos;
      }
      drawList.add(main);
      // RASTRO: amostras da mesma trajetoria em idades anteriores.
      for (var g = 1; g <= nGhost; g++) {
        final a = age - g * ghostGap;
        if (a < 0) break;
        final f = 1 - g / (nGhost + 1);
        final ghost = projeta(a, 0.6 * f, 0.5 + 0.5 * f);
        if (ghost != null) drawList.add(ghost);
      }
    }

    // Longe primeiro: perto cobre longe (ordem 3D correta).
    drawList.sort((a, b) => b.z.compareTo(a.z));

    final paintDot = Paint()..style = PaintingStyle.fill;
    final halo = Paint()..style = PaintingStyle.fill;
    final linha = Paint()
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final anel = Paint()..style = PaintingStyle.stroke;
    final glow = layer.glow.clamp(0.0, 1.0);
    final fim = layer.colorEnd;
    for (final d in drawList) {
      final base =
          fim == null ? layer.color : Color.lerp(layer.color, fim, d.u)!;
      final color = base.withValues(alpha: base.a * d.alpha);
      paintDot.color = color;

      // Halo suave atras da o "brilho" sem blur caro.
      if (glow > 0.001) {
        halo.color = color.withValues(alpha: color.a * 0.9 * glow);
        canvas.drawCircle(d.pos, d.radius * (1.8 + glow * 1.5), halo);
      }

      switch (layer.shape) {
        case 1:
          _drawSparkle(
              canvas, d.pos, d.radius, paintDot, d.variant, d.angle);
        case 2:
          final tail = d.tail ?? d.pos;
          linha
            ..color = color
            ..strokeWidth = math.max(0.8, d.radius * 0.9);
          // Alonga na direcao do movimento: quanto mais rapido, mais
          // comprido o risco.
          final dir = d.pos - tail;
          final len = dir.distance;
          final alvo = len < 0.5
              ? d.pos - Offset(0, d.radius * 2)
              : d.pos - dir / len * math.max(d.radius * 2.5, len * 3);
          canvas.drawLine(alvo, d.pos, linha);
          canvas.drawCircle(d.pos, d.radius * 0.55, paintDot);
        case 3:
          // Nuvem: tres discos concentricos bem transparentes.
          halo.color = color.withValues(alpha: color.a * 0.10);
          canvas.drawCircle(d.pos, d.radius * 2.6, halo);
          halo.color = color.withValues(alpha: color.a * 0.16);
          canvas.drawCircle(d.pos, d.radius * 1.8, halo);
          halo.color = color.withValues(alpha: color.a * 0.28);
          canvas.drawCircle(d.pos, d.radius * 1.1, halo);
        case 4:
          canvas.save();
          canvas.translate(d.pos.dx, d.pos.dy);
          canvas.rotate(d.angle);
          canvas.drawRect(
              Rect.fromCenter(
                  center: Offset.zero,
                  width: d.radius * 2,
                  height: d.radius * 2),
              paintDot);
          canvas.restore();
        case 5:
          anel
            ..color = color
            ..strokeWidth = math.max(0.8, d.radius * 0.35);
          canvas.drawCircle(d.pos, d.radius, anel);
        default:
          canvas.drawCircle(d.pos, d.radius, paintDot);
      }
    }
  }

  /// Cruz de 4 pontas alongada (sparkle de lente): dois losangos finos +
  /// nucleo claro, como nas referencias de edicao.
  void _drawSparkle(Canvas canvas, Offset c, double r, Paint paint,
      double variant, double angle) {
    final len = r * (2.4 + variant * 1.6);
    final lenH = len * 0.72;
    final w = r * 0.40;
    canvas.save();
    canvas.translate(c.dx, c.dy);
    if (angle != 0) canvas.rotate(angle);
    final path = Path()
      ..moveTo(0, -len)
      ..lineTo(w, 0)
      ..lineTo(0, len)
      ..lineTo(-w, 0)
      ..close()
      ..moveTo(-lenH, 0)
      ..lineTo(0, -w)
      ..lineTo(lenH, 0)
      ..lineTo(0, w)
      ..close();
    canvas.drawPath(path, paint);
    canvas.drawCircle(
      Offset.zero,
      w * 0.95,
      Paint()
        ..color = const Color(0xFFFFFFFF)
            .withValues(alpha: paint.color.a * 0.85),
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(ParticlesPainter old) =>
      old.layer != layer ||
      old.time != time ||
      old.rotXDeg != rotXDeg ||
      old.rotYDeg != rotYDeg ||
      old.rotZDeg != rotZDeg;
}

class _Proj {
  _Proj({
    required this.z,
    required this.pos,
    required this.radius,
    required this.alpha,
    required this.variant,
    required this.u,
    required this.angle,
  });

  final double z;
  final Offset pos;
  final double radius;
  final double alpha;
  final double variant;
  final double u;
  final double angle;
  Offset? tail;
}

/// Gizmo do objeto nulo: quadrado tracejado com X — visivel so no editor.
class NullGizmoPainter extends CustomPainter {
  const NullGizmoPainter({this.color = const Color(0xFF9F8CFF)});

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = (Offset.zero & size).deflate(3);
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..color = color.withValues(alpha: 0.9);

    void dashedLine(Offset a, Offset b) {
      const dash = 14.0, gap = 9.0;
      final delta = b - a;
      final len = delta.distance;
      final dir = delta / len;
      var d = 0.0;
      while (d < len) {
        final e = math.min(d + dash, len);
        canvas.drawLine(a + dir * d, a + dir * e, stroke);
        d = e + gap;
      }
    }

    dashedLine(rect.topLeft, rect.topRight);
    dashedLine(rect.topRight, rect.bottomRight);
    dashedLine(rect.bottomRight, rect.bottomLeft);
    dashedLine(rect.bottomLeft, rect.topLeft);

    final cross = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.5
      ..color = color.withValues(alpha: 0.65);
    final c = rect.center;
    canvas.drawLine(c - const Offset(26, 0), c + const Offset(26, 0), cross);
    canvas.drawLine(c - const Offset(0, 26), c + const Offset(0, 26), cross);
  }

  @override
  bool shouldRepaint(NullGizmoPainter old) => old.color != color;
}
