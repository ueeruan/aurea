/// A ESCADA DO TEMPO DE QUADRO (0..2 degraus), sem relogio proprio.
///
/// A mesma regra que o controlador de qualidade 3D ja usa e ja provou em
/// teste — doze quadros CRUS acima de 33 ms descem um degrau; folga
/// sustentada sobe um — com duas diferencas, porque aqui o que muda e a
/// resolucao da previa inteira, e resolucao piscando se ve:
///
///  * sobe so depois de [esperaParaSubir] (8 s, e nao 5);
///  * sobe so com 240 quadros folgados (e nao 120).
///
/// Mede o quadro INTEIRO (montar + rasterizar), porque na previa 2D o
/// gargalo tanto pode ser a composicao quanto a GPU.
class EscadaPorTempoDeQuadro {
  EscadaPorTempoDeQuadro({DateTime Function()? agora})
    : _agora = agora ?? DateTime.now;

  static const double lentoMs = 33.0;
  static const double folgadoMs = 12.0;
  static const int lentosParaDescer = 12;
  static const int folgadosParaSubir = 240;
  static const Duration esperaParaSubir = Duration(seconds: 8);

  final DateTime Function() _agora;

  int _degraus = 0;
  double? _ema;
  int _lentos = 0;
  int _folgados = 0;
  DateTime? _ultimaDescida;

  int get degraus => _degraus;

  /// Um quadro que levou [ms]. Devolve `true` quando o degrau mudou.
  bool amostra(double ms) {
    if (!ms.isFinite || ms <= 0) return false;
    _ema = _ema == null ? ms : _ema! * .85 + ms * .15;
    // "Lento" e o quadro cru: uma media carregaria um pico isolado por
    // doze quadros e desceria a escada por um engasgo so.
    _lentos = ms > lentoMs ? _lentos + 1 : 0;
    _folgados = _ema! < folgadoMs ? _folgados + 1 : 0;
    if (_lentos >= lentosParaDescer && _degraus < 2) {
      _degraus++;
      _lentos = 0;
      _folgados = 0;
      _ultimaDescida = _agora();
      return true;
    }
    if (_folgados >= folgadosParaSubir && _degraus > 0) {
      final desde = _ultimaDescida;
      if (desde != null && _agora().difference(desde) < esperaParaSubir) {
        return false;
      }
      _degraus--;
      _folgados = 0;
      return true;
    }
    return false;
  }

  void zerar() {
    _degraus = 0;
    _ema = null;
    _lentos = 0;
    _folgados = 0;
    _ultimaDescida = null;
  }
}
