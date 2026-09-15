import 'dart:math' as math;
import 'dart:ui';

/// ANIMADORES AUTOMATICOS (v1.1.1): a propriedade anda sozinha, sem
/// keyframe nenhum. Um numero usa a EXPRESSAO que o motor ja sabe rodar;
/// um ponto (posicao, ancora) guarda o animador nele mesmo, porque
/// expressao aqui so fala em numero.
enum TipoDoAnimador { seno, triangulo, dente, aleatorio, pulso }

String rotuloDoAnimador(TipoDoAnimador t) => switch (t) {
  TipoDoAnimador.seno => 'Onda',
  TipoDoAnimador.triangulo => 'Vaivém',
  TipoDoAnimador.dente => 'Rampa',
  TipoDoAnimador.aleatorio => 'Sorteio',
  TipoDoAnimador.pulso => 'Pulso',
};

String explicacaoDoAnimador(TipoDoAnimador t) => switch (t) {
  TipoDoAnimador.seno => 'Sobe e desce liso, sem parar.',
  TipoDoAnimador.triangulo => 'Vai e volta em linha reta.',
  TipoDoAnimador.dente => 'Sobe e recomeça do zero.',
  TipoDoAnimador.aleatorio => 'Tremida macia, sempre igual no mesmo projeto.',
  TipoDoAnimador.pulso => 'Fica num valor, pula para o outro.',
};

/// COMO o animador entra no valor da propriedade.
enum ModoDoAnimador { somar, multiplicar }

/// O animador de uma propriedade: a forma da onda, o quanto ela mexe
/// (em X e em Y, para um ponto), o tempo de uma volta e de onde ela
/// comeca.
class AnimadorAutomatico {
  const AnimadorAutomatico({
    this.tipo = TipoDoAnimador.seno,
    this.forca = 20,
    this.forcaY,
    this.periodo = 2,
    this.fase = 0,
    this.semente = 1,
    this.modo = ModoDoAnimador.somar,
  });

  final TipoDoAnimador tipo;

  /// O quanto a propriedade sai do lugar. Em [ModoDoAnimador.multiplicar]
  /// e uma fracao (0,2 = 20% para mais e para menos).
  final double forca;

  /// A forca do eixo Y de um ponto; nula = a mesma de X.
  final double? forcaY;

  /// Segundos de uma volta inteira.
  final double periodo;

  /// De onde a volta comeca, em voltas (0,25 = um quarto adiantado).
  final double fase;

  /// A semente do sorteio: o mesmo projeto treme sempre igual.
  final int semente;

  final ModoDoAnimador modo;

  double get forcaDoY => forcaY ?? forca;

  AnimadorAutomatico copyWith({
    TipoDoAnimador? tipo,
    double? forca,
    double? forcaY,
    double? periodo,
    double? fase,
    int? semente,
    ModoDoAnimador? modo,
    bool limparForcaY = false,
  }) => AnimadorAutomatico(
    tipo: tipo ?? this.tipo,
    forca: forca ?? this.forca,
    forcaY: limparForcaY ? null : (forcaY ?? this.forcaY),
    periodo: periodo ?? this.periodo,
    fase: fase ?? this.fase,
    semente: semente ?? this.semente,
    modo: modo ?? this.modo,
  );

  /// A onda crua no tempo [t] (segundos), entre -1 e 1.
  double onda(double t, {int eixo = 0}) {
    final p = periodo.abs() < 1e-6 ? 1e-6 : periodo.abs();
    // O eixo Y anda um quarto de volta atrasado: dois eixos em fase dao
    // uma reta diagonal, e nao um movimento.
    final x = t / p + fase + (eixo == 1 ? .25 : 0);
    switch (tipo) {
      case TipoDoAnimador.seno:
        return math.sin(x * 2 * math.pi);
      case TipoDoAnimador.triangulo:
        final f = (x % 1 + 1) % 1;
        return f < .5 ? (f * 4 - 1) : (3 - f * 4);
      case TipoDoAnimador.dente:
        return ((x % 1 + 1) % 1) * 2 - 1;
      case TipoDoAnimador.pulso:
        return ((x % 1 + 1) % 1) < .5 ? 1 : -1;
      case TipoDoAnimador.aleatorio:
        return _ruido(x, eixo);
    }
  }

  /// RUIDO MACIO: sorteia um valor por volta e passa liso de um para o
  /// outro (suavizacao de terceiro grau). Determinista pela [semente] —
  /// exportar duas vezes da o mesmo video.
  double _ruido(double x, int eixo) {
    final i = x.floor();
    final f = x - i;
    final s = f * f * (3 - 2 * f);
    final a = _sorteio(i, eixo);
    final b = _sorteio(i + 1, eixo);
    return a + (b - a) * s;
  }

  double _sorteio(int i, int eixo) {
    var h =
        (i * 374761393 + semente * 668265263 + eixo * 2147483647) & 0x7fffffff;
    h = (h ^ (h >> 13)) * 1274126177 & 0x7fffffff;
    return ((h ^ (h >> 16)) % 20001) / 10000 - 1;
  }

  /// O valor de um NUMERO no tempo [t] (segundos).
  double valor(double base, double t) => switch (modo) {
    ModoDoAnimador.somar => base + onda(t) * forca,
    ModoDoAnimador.multiplicar => base * (1 + onda(t) * forca),
  };

  /// O valor de um PONTO no tempo [t] (segundos).
  Offset valorDoPonto(Offset base, double t) => switch (modo) {
    ModoDoAnimador.somar =>
      base + Offset(onda(t) * forca, onda(t, eixo: 1) * forcaDoY),
    ModoDoAnimador.multiplicar => Offset(
      base.dx * (1 + onda(t) * forca),
      base.dy * (1 + onda(t, eixo: 1) * forcaDoY),
    ),
  };

  Map<String, dynamic> toJson() => {
    't': tipo.index,
    'f': forca,
    if (forcaY != null) 'fy': forcaY,
    'p': periodo,
    if (fase != 0) 'fa': fase,
    if (semente != 1) 's': semente,
    if (modo != ModoDoAnimador.somar) 'm': modo.index,
  };

  static AnimadorAutomatico? fromJson(Object? raw) {
    if (raw is! Map) return null;
    int indice(String k, int teto) =>
        ((raw[k] as num?)?.toInt() ?? 0).clamp(0, teto);
    return AnimadorAutomatico(
      tipo:
          TipoDoAnimador.values[indice('t', TipoDoAnimador.values.length - 1)],
      forca: (raw['f'] as num?)?.toDouble() ?? 20,
      forcaY: (raw['fy'] as num?)?.toDouble(),
      periodo: (raw['p'] as num?)?.toDouble() ?? 2,
      fase: (raw['fa'] as num?)?.toDouble() ?? 0,
      semente: (raw['s'] as num?)?.toInt() ?? 1,
      modo: ModoDoAnimador.values[indice('m', 1)],
    );
  }

  @override
  bool operator ==(Object other) =>
      other is AnimadorAutomatico &&
      other.tipo == tipo &&
      other.forca == forca &&
      other.forcaY == forcaY &&
      other.periodo == periodo &&
      other.fase == fase &&
      other.semente == semente &&
      other.modo == modo;

  @override
  int get hashCode =>
      Object.hash(tipo, forca, forcaY, periodo, fase, semente, modo);
}

/// O RESUMO que a interface mostra na linha da propriedade.
String resumoDoAnimador(AnimadorAutomatico a, {String unidade = ''}) {
  final forca = a.modo == ModoDoAnimador.multiplicar
      ? '${(a.forca * 100).round()}%'
      : '${a.forca.round()}$unidade';
  return '${rotuloDoAnimador(a.tipo)} · ±$forca · ${_n(a.periodo)}s';
}

String _n(double v) {
  final s = v.toStringAsFixed(2);
  final limpo = s.contains('.')
      ? s.replaceFirst(RegExp(r'0+$'), '').replaceFirst(RegExp(r'\.$'), '')
      : s;
  return limpo.isEmpty ? '0' : limpo;
}
