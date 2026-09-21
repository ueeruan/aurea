import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart' show ValueListenable;
import 'package:flutter/material.dart';

import '../../../application/perfil3d.dart';
import '../../../domain/peak_pyramid.dart';
import 'janela_da_timeline.dart';

/// FORMA DE ONDA na barra do clipe.
///
/// Desenhada como envelope espelhado no meio da barra: e a forma que
/// deixa a respiracao entre duas falas visivel de relance, que e para
/// isso que ela serve.
///
/// Os picos cobrem o arquivo INTEIRO; a barra mostra so o trecho usado,
/// entao a janela [start, end] recorta a leitura. Assim arrastar a alca
/// de corte revela o audio que estava fora, em vez de esticar o que ja
/// estava dentro.
/// FORMA DE ONDA COM CORPO: contorno de PICO e miolo de RMS.
///
/// So o pico da uma mancha cheia que nao diz o quao alto esta; so o RMS
/// da uma forma sem ataque, que esconde a batida seca. Os dois juntos
/// sao a forma de onda que se reconhece — e e a mesma leitura que
/// qualquer editor de audio mostra.
///
/// O nivel da piramide e escolhido pelo ZOOM: ampliar troca de nivel e
/// nunca recalcula nada.
class PyramidWaveformPainter extends CustomPainter {
  const PyramidWaveformPainter({
    required this.pyramid,
    required this.start,
    required this.end,
    required this.color,
    this.silences = const [],
  });

  final PeakPyramid pyramid;

  /// Trecho do ARQUIVO que esta barra mostra.
  final Duration start;
  final Duration end;
  final Color color;

  /// Regioes de silencio, em tempo do arquivo — marcadas para a
  /// decupagem: e onde o corte vai cair.
  final List<(Duration, Duration)> silences;

  @override
  void paint(Canvas canvas, Size size) {
    if (pyramid.isEmpty || size.width < 2 || size.height < 4) return;
    final janela = (end - start).inMicroseconds / 1000000.0;
    if (janela <= 0) return;

    final segundosPorPixel = janela / size.width;
    final nivel = pyramid.levelFor(segundosPorPixel);
    if (nivel.length == 0) return;

    final mid = size.height / 2;
    final half = size.height / 2 - 1.5;

    // SILENCIO primeiro, por tras da onda.
    if (silences.isNotEmpty) {
      final fundo = Paint()..color = color.withValues(alpha: 0.12);
      for (final sil in silences) {
        final a = (sil.$1 - start).inMicroseconds / 1000000.0 / janela;
        final b = (sil.$2 - start).inMicroseconds / 1000000.0 / janela;
        final x0 = (a * size.width).clamp(0.0, size.width);
        final x1 = (b * size.width).clamp(0.0, size.width);
        if (x1 > x0) {
          canvas.drawRect(Rect.fromLTRB(x0, 0, x1, size.height), fundo);
        }
      }
    }

    final contorno = Paint()
      ..color = color
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;
    final corpo = Paint()
      ..color = color.withValues(alpha: 0.55)
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;

    final de = nivel.bucketAt(start);
    final ate = nivel.bucketAt(end);
    final vao = ate - de;
    if (vao <= 0) return;

    final colunas = size.width.floor();
    final passo = vao / colunas;

    for (var x = 0; x < colunas; x++) {
      final a = de + (x * passo).floor();
      final b = de + ((x + 1) * passo).ceil();
      var alto = 0.0, baixo = 0.0, rms = 0.0;
      var contou = 0;
      for (var i = a; i < b; i++) {
        if (i < 0 || i >= nivel.length) continue;
        if (nivel.max[i] > alto) alto = nivel.max[i];
        if (nivel.min[i] < baixo) baixo = nivel.min[i];
        if (nivel.rms[i] > rms) rms = nivel.rms[i];
        contou++;
      }
      if (contou == 0) continue;
      final dx = x + 0.5;

      // Raiz comprime o alto e abre o baixo: som fraco continua visivel.
      final hAlto = math.sqrt(alto.abs()) * half;
      final hBaixo = math.sqrt(baixo.abs()) * half;
      if (hAlto > 0 || hBaixo > 0) {
        canvas.drawLine(
          Offset(dx, mid - hAlto),
          Offset(dx, mid + hBaixo),
          contorno,
        );
      }
      final hRms = math.sqrt(rms) * half;
      if (hRms > 0.5) {
        canvas.drawLine(Offset(dx, mid - hRms), Offset(dx, mid + hRms), corpo);
      }
    }
  }

  @override
  bool shouldRepaint(PyramidWaveformPainter old) =>
      old.pyramid != pyramid ||
      old.start != start ||
      old.end != end ||
      old.color != color ||
      old.silences.length != silences.length;
}

class WaveformPainter extends CustomPainter {
  const WaveformPainter({
    required this.peaks,
    required this.start,
    required this.end,
    required this.color,
  });

  final Float32List peaks;
  final Duration start;
  final Duration end;
  final Color color;

  /// Picos por segundo — tem de bater com quem gerou.
  static const perSecond = 100;

  @override
  void paint(Canvas canvas, Size size) {
    if (peaks.isEmpty || size.width < 2 || size.height < 4) return;

    final from = (start.inMilliseconds / 1000.0 * perSecond).floor();
    final to = (end.inMilliseconds / 1000.0 * perSecond).ceil();
    final span = to - from;
    if (span <= 0) return;

    final mid = size.height / 2;
    final half = size.height / 2 - 1.5;
    final paint = Paint()
      ..color = color
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;

    // Uma coluna por pixel: mais que isso nao aparece, menos que isso
    // esconde transiente.
    final columns = size.width.floor();
    final step = span / columns;
    for (var x = 0; x < columns; x++) {
      final a = from + (x * step).floor();
      final b = from + ((x + 1) * step).ceil();
      var peak = 0.0;
      for (var i = a; i < b; i++) {
        if (i < 0 || i >= peaks.length) continue;
        if (peaks[i] > peak) peak = peaks[i];
      }
      if (peak <= 0) continue;
      // Raiz comprime o alto e abre o baixo: som fraco continua visivel.
      final h = math.sqrt(peak) * half;
      final dx = x + 0.5;
      canvas.drawLine(Offset(dx, mid - h), Offset(dx, mid + h), paint);
    }
  }

  @override
  bool shouldRepaint(WaveformPainter old) =>
      old.peaks != peaks ||
      old.start != start ||
      old.end != end ||
      old.color != color;
}

/// TIRA DE MINIATURAS na barra do clipe de video.
///
/// Sem ela, achar o corte e tatear: a barra e um retangulo liso e a
/// unica pista e o playhead. Com ela, da para ver a cena mudar.
class FilmstripPainter extends CustomPainter {
  const FilmstripPainter({
    required this.frames,
    required this.start,
    required this.end,
    required this.sourceDuration,
    this.janela,
    this.esquerdaPx = 0,
  }) : super(repaint: janela);

  final List<ui.Image> frames;

  /// Trecho usado do arquivo.
  final Duration start;
  final Duration end;

  /// Duracao total do arquivo, que e o que as miniaturas cobrem.
  final Duration sourceDuration;

  /// O PEDACO VISIVEL DA LINHA DO TEMPO e onde esta barra comeca nela.
  /// Um video de dez minutos ampliado tem centenas de miniaturas na
  /// barra; so as da janela sao gravadas. Sem janela, pinta tudo.
  final ValueListenable<JanelaDaTimeline>? janela;
  final double esquerdaPx;

  // Reaproveitado: `paint` roda a cada troca de janela, e alocar aqui
  // dentro e lixo por pintura.
  static final Paint _tinta = Paint()
    ..filterQuality = FilterQuality.low
    ..isAntiAlias = false;

  @override
  void paint(Canvas canvas, Size size) {
    if (frames.isEmpty || size.width < 2 || size.height < 4) return;

    final total = sourceDuration.inMicroseconds;
    final a = total <= 0 ? 0.0 : start.inMicroseconds / total;
    final b = total <= 0 ? 1.0 : end.inMicroseconds / total;
    final span = (b - a).clamp(0.0001, 1.0);

    // Largura de cada miniatura na barra, mantendo a proporcao.
    final first = frames.first;
    final tileW = size.height * first.width / first.height;
    if (tileW <= 0) return;

    final n = (size.width / tileW).ceil() + 1;
    // SO AS MINIATURAS DA JANELA. O indice continua absoluto (a conta de
    // `x` parte de `i * tileW`), entao a tira nao "anda" quando a janela
    // troca: a miniatura 40 e sempre a mesma, entre ou nao na gravacao.
    var i0 = 0;
    var i1 = n;
    final j = janela?.value;
    if (j != null) {
      final de = j.iniPx - esquerdaPx;
      final ate = j.fimPx - esquerdaPx;
      if (de > 0) i0 = (de / tileW).floor().clamp(0, n);
      if (ate < size.width) i1 = ((ate / tileW).ceil() + 1).clamp(0, n);
    }
    for (var i = i0; i < i1; i++) {
      final x = i * tileW;
      // Qual instante do ARQUIVO esta neste ponto da barra.
      final f = a + (x / size.width) * span;
      final idx = (f * frames.length).floor().clamp(0, frames.length - 1);
      final img = frames[idx];
      canvas.drawImageRect(
        img,
        Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()),
        Rect.fromLTWH(x, 0, tileW, size.height),
        _tinta,
      );
    }
  }

  @override
  bool shouldRepaint(FilmstripPainter old) =>
      old.frames != frames ||
      old.start != start ||
      old.end != end ||
      old.sourceDuration != sourceDuration ||
      old.janela != janela ||
      old.esquerdaPx != esquerdaPx;
}

/// A ONDA DO CLIPE, legivel para decupar.
///
/// Diferencas para o [PyramidWaveformPainter]:
///   * segue o instante REAL do arquivo em cada coluna ([fonte]), entao
///     acompanha corte, velocidade, reverso e Time Remap;
///   * espelho de verdade: o maximo para cima e o minimo para baixo;
///   * altura em DECIBEIS (piso de -45 dB) com ganho de exibicao — fala
///     baixa continua visivel e um estalo nao achata o resto;
///   * le a piramide pela janela fracionaria, sem arredondar o comeco
///     para o balde (antes a onda podia ficar ate 640 ms fora do lugar).
/// A GRAVACAO DA ONDA, guardada entre quadros.
///
/// POR QUE UM CACHE, E NAO SO UM CAMPO: durante a rolagem TODOS os clipes
/// visiveis repintam, um depois do outro. Um cache de uma entrada so
/// seria jogado fora antes de servir — o clipe seguinte o substituiria, e
/// o anterior voltaria a construir o caminho. Dai a fila com teto.
class _OndaGravada {
  _OndaGravada(this.picture, this.assinatura);

  final ui.Picture picture;
  final Object assinatura;
}

class ClipWaveformPainter extends CustomPainter {
  const ClipWaveformPainter({
    required this.pyramid,
    required this.fonte,
    required this.color,
    this.contorno,
    this.gain = 1,
    this.muted = false,
    this.janela,
    this.esquerdaPx = 0,
  }) : super(repaint: janela);

  /// O TETO DO CACHE. Cada clipe visivel ocupa uma entrada; 32 cobre uma
  /// linha do tempo cheia com folga e nao guarda a sessao inteira.
  static const int _tetoDoCache = 32;

  static final Map<Object, _OndaGravada> _cache = <Object, _OndaGravada>{};

  /// O PEDACO VISIVEL DA LINHA DO TEMPO, e onde esta barra comeca nela
  /// (pixels do conteudo).
  ///
  /// SEM ISTO A ONDA ERA DA LARGURA DO CLIPE: uma musica de tres minutos a
  /// 400 px/s sao 72 mil colunas — tres `Float32List` e dois caminhos com
  /// 144 mil pontos, refeitos a cada passo de zoom, e entregues INTEIROS ao
  /// raster a cada quadro (o motor descarta por operacao, e a operacao e
  /// um caminho so). Com a janela, o caminho tem teto: o que cabe na tela
  /// mais a folga. Nula = a onda inteira, como sempre foi.
  final ValueListenable<JanelaDaTimeline>? janela;
  final double esquerdaPx;

  /// O passo em que o recorte LOCAL anda. A janela ja chega quantizada,
  /// mas em coordenadas do conteudo: arrastar o clipe mudaria o recorte
  /// local a cada pixel e a gravacao seria refeita a cada passo do dedo.
  /// Arredondado para fora neste passo, o recorte so muda quando a barra
  /// atravessa um balde — e um clipe que cabe inteiro na janela nunca muda.
  static const int _baldeLocal = 256;

  /// As colunas `[c0, c1)` que entram na gravacao.
  (int, int) _recorte(int colunas) {
    final j = janela?.value;
    if (j == null) return (0, colunas);
    final de = j.iniPx - esquerdaPx;
    final ate = j.fimPx - esquerdaPx;
    final c0 = de <= 0
        ? 0
        : math.min(colunas, (de / _baldeLocal).floor() * _baldeLocal);
    final c1 = ate >= colunas
        ? colunas
        : math.max(0, (ate / _baldeLocal).ceil() * _baldeLocal);
    return (c0, math.min(colunas, c1));
  }

  /// A CHAVE DA GRAVACAO: tudo o que muda o desenho. A piramide entra por
  /// IDENTIDADE (ela e imutavel depois de pronta); a fonte, pelos
  /// instantes das pontas — comparar os milhares de amostras do meio a
  /// cada quadro custaria mais caro do que redesenhar.
  ///
  /// UM RECORD, e nao um `Object.hash`: o inteiro do hash era a propria
  /// chave do mapa, e duas ondas diferentes com o mesmo hash devolveriam
  /// a gravacao uma da outra. O record compara campo a campo.
  Object _assinatura(Size size, int c0, int c1) => (
    pyramid,
    size.width,
    size.height,
    color,
    contorno,
    gain,
    muted,
    fonte.length,
    fonte.isEmpty ? 0.0 : fonte.first,
    fonte.length < 2 ? 0.0 : fonte.last,
    c0,
    c1,
  );

  /// ESVAZIA O CACHE. Chamado quando o projeto troca (outra linha do
  /// tempo, outro conjunto de clipes): manter gravacoes de uma sessao que
  /// acabou e memoria parada.
  static void limparCache() {
    for (final g in _cache.values) {
      g.picture.dispose();
    }
    _cache.clear();
    construcoes = 0;
    colunasDaUltimaConstrucao = 0;
  }

  static int get entradasNoCache => _cache.length;

  /// QUANTAS VEZES A ONDA FOI DE FATO CONSTRUIDA. E o numero que prova o
  /// cache: ele sobe uma vez por desenho NOVO, e nao uma vez por quadro.
  /// Sem ele, "o cache funciona" seria uma afirmacao sem medida — as
  /// chamadas que a gravacao faz nao passam pela caneta de fora.
  @visibleForTesting
  static int construcoes = 0;

  /// QUANTAS COLUNAS a ultima construcao percorreu: a prova de que a
  /// janela recorta (um clipe de 20 mil px nao pode construir 20 mil).
  @visibleForTesting
  static int colunasDaUltimaConstrucao = 0;

  final PeakPyramid pyramid;

  /// n+1 instantes (segundos, absolutos no arquivo) uniformes na largura.
  final Float64List fonte;
  final Color color;
  final Color? contorno;
  final double gain;
  final bool muted;

  static const double pisoDb = 45;

  double _em(double u) {
    final n = fonte.length - 1;
    if (n <= 0) return fonte.isEmpty ? 0 : fonte.first;
    final p = (u * n).clamp(0.0, n.toDouble());
    final i = p.floor().clamp(0, n - 1);
    final f = p - i;
    return fonte[i] * (1 - f) + fonte[i + 1] * f;
  }

  double _altura(double amplitude, double half) {
    final a = amplitude.abs() * gain;
    if (a <= 1e-5) return 0;
    final db = 20 * math.log(a) / math.ln10;
    return ((db + pisoDb) / pisoDb).clamp(0.0, 1.0) * half;
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (pyramid.isEmpty || fonte.length < 2 || size.width < 2 || size.height < 6) {
      return;
    }
    final (c0, c1) = _recorte(size.width.floor());
    // O clipe esta na arvore mas fora da janela (o selecionado fica
    // sempre): nao ha coluna para desenhar, e nada entra no cache.
    if (c1 <= c0) return;
    // O DESENHO PRONTO MANDA: enquanto nada na onda mudou, o que se faz
    // aqui e reexecutar a gravacao — e nao reconstruir milhares de pontos.
    final chave = _assinatura(size, c0, c1);
    final guardada = _cache.remove(chave);
    if (guardada != null) {
      // Volta para o FIM da fila: quem foi usado agora e o ultimo a sair.
      // Sem isto o teto expulsava por idade de NASCIMENTO, e o clipe que
      // esta na tela ha mais tempo era justamente o primeiro a ser refeito.
      _cache[chave] = guardada;
      canvas.drawPicture(guardada.picture);
      return;
    }
    final gravador = ui.PictureRecorder();
    Perfil3D.fase(
      'pintar.onda',
      () => _desenhar(Canvas(gravador), size, c0, c1),
    );
    final nova = _OndaGravada(gravador.endRecording(), chave);
    // Teto: a entrada mais antiga sai. O `Map` do Dart preserva a ordem de
    // insercao, entao a primeira chave e a mais velha.
    while (_cache.length >= _tetoDoCache) {
      final primeira = _cache.keys.first;
      _cache.remove(primeira)?.picture.dispose();
    }
    _cache[chave] = nova;
    canvas.drawPicture(nova.picture);
  }

  void _desenhar(Canvas canvas, Size size, int c0, int c1) {
    construcoes++;
    final colunas = size.width.floor();
    final n = c1 - c0;
    colunasDaUltimaConstrucao = n;
    final mid = size.height / 2;
    final half = size.height / 2 - 1;
    final cima = Float32List(n), baixo = Float32List(n);
    final corpo = Float32List(n);
    for (var k = 0; k < n; k++) {
      // A coluna e ABSOLUTA na barra: o recorte muda quais colunas entram,
      // nunca o que cada uma mostra.
      final x = c0 + k;
      final t0 = _em(x / colunas), t1 = _em((x + 1) / colunas);
      final lo = math.min(t0, t1), hi = math.max(t0, t1);
      final nivel = pyramid.levelFor(math.max(hi - lo, 1e-6));
      final b = nivel.bucketSeconds;
      if (nivel.length == 0 || b <= 0) continue;
      var a = (lo / b).floor();
      var z = (hi / b).ceil();
      if (z <= a) z = a + 1;
      if (a < 0) a = 0;
      if (z > nivel.length) z = nivel.length;
      var mx = 0.0, mn = 0.0, rms = 0.0;
      for (var i = a; i < z; i++) {
        if (nivel.max[i] > mx) mx = nivel.max[i];
        if (nivel.min[i] < mn) mn = nivel.min[i];
        if (nivel.rms[i] > rms) rms = nivel.rms[i];
      }
      cima[k] = _altura(mx, half);
      baixo[k] = _altura(mn, half);
      corpo[k] = _altura(rms, half);
    }
    final alfa = muted ? 0.35 : 1.0;
    Path forma(Float32List up, Float32List down) {
      final p = Path()..moveTo(c0.toDouble(), mid - up[0]);
      for (var k = 1; k < n; k++) {
        p.lineTo(c0 + k + 0.5, mid - up[k]);
      }
      for (var k = n - 1; k >= 0; k--) {
        p.lineTo(c0 + k + 0.5, mid + down[k]);
      }
      return p..close();
    }

    canvas.drawLine(
      Offset(c0.toDouble(), mid),
      Offset(c1 >= colunas ? size.width : c1.toDouble(), mid),
      Paint()
        ..color = Colors.white.withValues(alpha: 0.10 * alfa)
        ..strokeWidth = 1,
    );
    final pico = forma(cima, baixo);
    canvas.drawPath(
      pico,
      Paint()..color = color.withValues(alpha: color.a * 0.55 * alfa),
    );
    if (contorno != null) {
      canvas.drawPath(
        pico,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = contorno!.withValues(alpha: contorno!.a * alfa),
      );
    }
    canvas.drawPath(
      forma(corpo, corpo),
      Paint()..color = color.withValues(alpha: color.a * alfa),
    );
  }

  @override
  bool shouldRepaint(ClipWaveformPainter old) =>
      old.pyramid != pyramid ||
      old.color != color ||
      old.contorno != contorno ||
      old.gain != gain ||
      old.muted != muted ||
      old.janela != janela ||
      old.esquerdaPx != esquerdaPx ||
      old.fonte.length != fonte.length ||
      (fonte.isNotEmpty &&
          (old.fonte.first != fonte.first || old.fonte.last != fonte.last)) ||
      !_iguais(old.fonte, fonte);

  static bool _iguais(Float64List a, Float64List b) {
    if (identical(a, b)) return true;
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i += math.max(1, a.length ~/ 16)) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
