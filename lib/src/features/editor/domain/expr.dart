/// ENTRADA NUMERICA COM ARITMETICA (spec motion-graphics-pro, PR-X4):
/// todo campo aceita teclado e aceita expressao simples — digitar
/// `1080/3` no campo de posicao resolve para 360. Custa pouco e economiza
/// muito.
///
/// Suporta + - * / ( ) %, decimal com virgula OU ponto, e o sufixo % como
/// divisao por 100 quando no fim ("50%" = 0.5 de [percentOf], se dado).
library;

/// Avalia [input]. Devolve null quando a expressao nao faz sentido —
/// quem chama mantem o valor anterior (campo nunca fica num estado
/// invalido silencioso).
double? evalExpression(String input, {double? percentOf}) {
  final src = input.trim();
  if (src.isEmpty) return null;

  // "50%" com base conhecida vira 50% DA BASE; sem base, vira 0.5.
  final percentMatch = RegExp(r'^([-+]?[\d.,]+)\s*%$').firstMatch(src);
  if (percentMatch != null) {
    final n = _number(percentMatch.group(1)!);
    if (n == null) return null;
    return percentOf == null ? n / 100 : percentOf * n / 100;
  }

  final tokens = _tokenize(src);
  if (tokens == null || tokens.isEmpty) return null;
  final parser = _Parser(tokens);
  final v = parser.parseExpression();
  if (v == null || !parser.atEnd) return null;
  if (v.isNaN || v.isInfinite) return null;
  return v;
}

double? _number(String s) {
  // Aceita "1.234,56" (pt-BR) e "1234.56".
  var t = s.trim();
  if (t.contains(',') && t.contains('.')) {
    t = t.replaceAll('.', '').replaceAll(',', '.');
  } else {
    t = t.replaceAll(',', '.');
  }
  return double.tryParse(t);
}

sealed class _Tok {
  const _Tok();
}

class _Num extends _Tok {
  const _Num(this.value);
  final double value;
}

class _Op extends _Tok {
  const _Op(this.ch);
  final String ch;
}

List<_Tok>? _tokenize(String src) {
  final out = <_Tok>[];
  final buf = StringBuffer();
  void flush() {
    if (buf.isEmpty) return;
    final n = _number(buf.toString());
    if (n != null) out.add(_Num(n));
    buf.clear();
  }

  for (var i = 0; i < src.length; i++) {
    final c = src[i];
    if (RegExp(r'[\d.,]').hasMatch(c)) {
      buf.write(c);
    } else if ('+-*/()'.contains(c)) {
      flush();
      out.add(_Op(c));
    } else if (c == ' ') {
      flush();
    } else {
      return null; // caractere inesperado
    }
  }
  flush();
  return out;
}

class _Parser {
  _Parser(this.tokens);

  final List<_Tok> tokens;
  int _i = 0;

  bool get atEnd => _i >= tokens.length;

  _Tok? get _peek => _i < tokens.length ? tokens[_i] : null;

  double? parseExpression() {
    var left = _parseTerm();
    if (left == null) return null;
    while (true) {
      final t = _peek;
      if (t is _Op && (t.ch == '+' || t.ch == '-')) {
        _i++;
        final right = _parseTerm();
        if (right == null) return null;
        left = t.ch == '+' ? left! + right : left! - right;
      } else {
        return left;
      }
    }
  }

  double? _parseTerm() {
    var left = _parseUnary();
    if (left == null) return null;
    while (true) {
      final t = _peek;
      if (t is _Op && (t.ch == '*' || t.ch == '/')) {
        _i++;
        final right = _parseUnary();
        if (right == null) return null;
        if (t.ch == '/' && right == 0) return null; // divisao por zero
        left = t.ch == '*' ? left! * right : left! / right;
      } else {
        return left;
      }
    }
  }

  double? _parseUnary() {
    final t = _peek;
    if (t is _Op && (t.ch == '-' || t.ch == '+')) {
      _i++;
      final v = _parseUnary();
      if (v == null) return null;
      return t.ch == '-' ? -v : v;
    }
    return _parseAtom();
  }

  double? _parseAtom() {
    final t = _peek;
    if (t is _Num) {
      _i++;
      return t.value;
    }
    if (t is _Op && t.ch == '(') {
      _i++;
      final v = parseExpression();
      if (v == null) return null;
      final close = _peek;
      if (close is! _Op || close.ch != ')') return null;
      _i++;
      return v;
    }
    return null;
  }
}
