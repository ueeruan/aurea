import 'dart:ui';

import 'package:uuid/uuid.dart';

/// Legendas (spec AM2-modo-edicao-e-legendas §6.5): UMA camada com muitos
/// cues — nunca uma camada de texto por fala.

class Cue {
  Cue({
    String? id,
    required this.start,
    required this.end,
    required this.text,
    this.locked = false,
  }) : id = id ?? const Uuid().v4();

  /// Tempos locais a camada de legenda.
  final Duration start;
  final Duration end;
  final String text;

  /// Editado a mao: nova transcricao nao sobrescreve.
  final bool locked;

  final String id;

  Duration get duration => end - start;

  Cue copyWith({Duration? start, Duration? end, String? text, bool? locked}) =>
      Cue(
        id: id,
        start: start ?? this.start,
        end: end ?? this.end,
        text: text ?? this.text,
        locked: locked ?? this.locked,
      );
}

class CaptionStyle {
  const CaptionStyle({
    this.fontSize = 56,
    this.color = const Color(0xFFFFFFFF),
    this.backgroundColor = const Color(0xFF000000),
    this.backgroundOpacity = 0.55,
    this.bold = true,
  });

  final double fontSize;
  final Color color;
  final Color backgroundColor;
  final double backgroundOpacity;
  final bool bold;

  CaptionStyle copyWith({
    double? fontSize,
    Color? color,
    Color? backgroundColor,
    double? backgroundOpacity,
    bool? bold,
  }) {
    return CaptionStyle(
      fontSize: fontSize ?? this.fontSize,
      color: color ?? this.color,
      backgroundColor: backgroundColor ?? this.backgroundColor,
      backgroundOpacity: backgroundOpacity ?? this.backgroundOpacity,
      bold: bold ?? this.bold,
    );
  }
}

/// Modo de segmentacao da legenda automatica.
enum CaptionMode {
  /// Frases inteiras (segmentos do Whisper, normalizados).
  frases,

  /// Blocos curtos de 2-3 palavras (estilo reels).
  curtas,

  /// Uma palavra por cue (karaoke) — timestamps por token.
  palavra,
}

String captionModeLabel(CaptionMode m) => switch (m) {
      CaptionMode.frases => 'Frases',
      CaptionMode.curtas => 'Curtas',
      CaptionMode.palavra => 'Palavra por palavra',
    };

/// Agrupa cues de UMA palavra em blocos curtos: junta enquanto couber em
/// [maxWords]/[maxChars] e a pausa ate a proxima palavra for menor que
/// [maxGap] (pausa grande = frase nova).
List<Cue> groupWordCues(
  List<Cue> words, {
  int maxWords = 3,
  int maxChars = 20,
  Duration maxGap = const Duration(milliseconds: 600),
}) {
  if (words.isEmpty) return words;
  final sorted = [...words]..sort((a, b) => a.start.compareTo(b.start));
  final out = <Cue>[];
  var texts = <String>[sorted.first.text.trim()];
  var start = sorted.first.start;
  var end = sorted.first.end;
  for (var i = 1; i < sorted.length; i++) {
    final w = sorted[i];
    final joined = '${texts.join(' ')} ${w.text.trim()}';
    final gap = w.start - end;
    if (texts.length >= maxWords ||
        joined.length > maxChars ||
        gap > maxGap) {
      out.add(Cue(start: start, end: end, text: texts.join(' ')));
      texts = [w.text.trim()];
      start = w.start;
      end = w.end;
    } else {
      texts.add(w.text.trim());
      end = w.end;
    }
  }
  out.add(Cue(start: start, end: end, text: texts.join(' ')));
  return out;
}

/// Busca binaria do cue ativo em [t] (lista ordenada por start).
Cue? activeCueAt(List<Cue> cues, Duration t) {
  var lo = 0;
  var hi = cues.length - 1;
  while (lo <= hi) {
    final mid = (lo + hi) >> 1;
    final c = cues[mid];
    if (t < c.start) {
      hi = mid - 1;
    } else if (t >= c.end) {
      lo = mid + 1;
    } else {
      return c;
    }
  }
  return null;
}

/// Regras de normalizacao (§6.5): duracao minima 0,8 s (une vizinhos
/// curtos), quebra de linha em ate [maxLines] x [maxCharsPerLine] sem
/// partir palavra.
List<Cue> normalizeCues(
  List<Cue> cues, {
  int maxCharsPerLine = 42,
  int maxLines = 2,
  Duration minCueDuration = const Duration(milliseconds: 800),
}) {
  if (cues.isEmpty) return cues;
  final sorted = [...cues]..sort((a, b) => a.start.compareTo(b.start));

  // 1. Une cues curtos com o vizinho seguinte.
  final merged = <Cue>[];
  Cue? pending;
  for (final cue in sorted) {
    if (pending == null) {
      pending = cue;
      continue;
    }
    if (pending.duration < minCueDuration && !pending.locked && !cue.locked) {
      pending = Cue(
        start: pending.start,
        end: cue.end,
        text: '${pending.text.trim()} ${cue.text.trim()}',
      );
    } else {
      merged.add(pending);
      pending = cue;
    }
  }
  if (pending != null) merged.add(pending);

  // 2. Reflui o texto em linhas sem partir palavra.
  return [
    for (final cue in merged)
      cue.copyWith(
        text: wrapCaptionText(cue.text,
            maxCharsPerLine: maxCharsPerLine, maxLines: maxLines),
      ),
  ];
}

/// Quebra o texto em ate [maxLines] linhas de [maxCharsPerLine], nunca no
/// meio de palavra. Excedente fica na ultima linha (sem cortar conteudo).
String wrapCaptionText(String text,
    {int maxCharsPerLine = 42, int maxLines = 2}) {
  final words = text
      .replaceAll('\n', ' ')
      .split(RegExp(r'\s+'))
      .where((w) => w.isNotEmpty)
      .toList();
  final lines = <String>[];
  var current = StringBuffer();
  for (final w in words) {
    if (current.isEmpty) {
      current.write(w);
    } else if (current.length + 1 + w.length <= maxCharsPerLine ||
        lines.length >= maxLines - 1) {
      current.write(' $w');
    } else {
      lines.add(current.toString());
      current = StringBuffer(w);
    }
  }
  if (current.isNotEmpty) lines.add(current.toString());
  return lines.join('\n');
}

// ------------------------------------------------------------------- SRT

Duration _parseSrtTime(String s) {
  final m =
      RegExp(r'(\d+):(\d+):(\d+)[,.](\d+)').firstMatch(s.trim());
  if (m == null) return Duration.zero;
  return Duration(
    hours: int.parse(m.group(1)!),
    minutes: int.parse(m.group(2)!),
    seconds: int.parse(m.group(3)!),
    milliseconds: int.parse(m.group(4)!.padRight(3, '0')),
  );
}

String _fmtSrtTime(Duration d) {
  String two(int v) => v.toString().padLeft(2, '0');
  final h = d.inHours;
  final m = d.inMinutes % 60;
  final s = d.inSeconds % 60;
  final ms = (d.inMilliseconds % 1000).toString().padLeft(3, '0');
  return '${two(h)}:${two(m)}:${two(s)},$ms';
}

final _timeRe = RegExp(r'(\d+):(\d+):(\d+)[,.](\d+)');

/// Importa SRT. Tolerante a \r\n, indices ausentes e a seta "-->"
/// deformada por autocorrecao: a linha de tempo e detectada pelos DOIS
/// timestamps, nao pela seta.
List<Cue> parseSrt(String content) {
  final blocks = content
      .replaceAll('\r\n', '\n')
      .split(RegExp(r'\n\s*\n'))
      .where((b) => b.trim().isNotEmpty);
  final cues = <Cue>[];
  for (final block in blocks) {
    final lines =
        block.split('\n').where((l) => l.trim().isNotEmpty).toList();
    final timeIdx = lines.indexWhere(
        (l) => _timeRe.allMatches(l).length >= 2);
    if (timeIdx < 0) continue;
    final times = _timeRe.allMatches(lines[timeIdx]).toList();
    final text = lines.sublist(timeIdx + 1).join('\n').trim();
    if (text.isEmpty) continue;
    cues.add(Cue(
      start: _parseSrtTime(times[0].group(0)!),
      end: _parseSrtTime(times[1].group(0)!),
      text: text,
    ));
  }
  cues.sort((a, b) => a.start.compareTo(b.start));
  return cues;
}

/// Exporta SRT.
String serializeSrt(List<Cue> cues) {
  final sorted = [...cues]..sort((a, b) => a.start.compareTo(b.start));
  final buffer = StringBuffer();
  for (var i = 0; i < sorted.length; i++) {
    final c = sorted[i];
    buffer
      ..writeln(i + 1)
      ..writeln('${_fmtSrtTime(c.start)} --> ${_fmtSrtTime(c.end)}')
      ..writeln(c.text)
      ..writeln();
  }
  return buffer.toString();
}
