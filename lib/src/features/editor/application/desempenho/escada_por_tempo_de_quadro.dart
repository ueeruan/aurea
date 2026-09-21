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
///
/// ================== A ESCADA NAO PODE MEDIR A SI MESMA =================
///
/// Mudar de degrau muda o que a previa desenha (resolucao, fotos dos
/// efeitos, niveis do brilho). Desenhar de novo PRODUZ UM QUADRO, e esse
/// quadro volta para ca. Sem freio isso e um circuito fechado: o quadro
/// caro desce um degrau, o degrau novo faz o quadro ficar barato, a
/// folga sobe o degrau de volta, e a tela parada fica em 60 fps para
/// sempre — foi exatamente o que a bancada mediu em
/// "2d-com-brilho-repouso" (360 quadros em 6 s, 80% de CPU).
///
/// Dois freios fecham esse circuito:
///
///  * [acomodar] — depois de a politica mudar, os proximos
///    [quadrosDeAcomodacao] quadros sao CONSEQUENCIA dela e nao votam;
///  * [carencia] — nenhum degrau muda antes de [carencia] desde a ultima
///    mudanca, nem para baixo nem para cima.
///
/// E quem chama tem o terceiro e mais importante: com o editor parado
/// nao se mede nada (ver `AureaPerformanceManager`).
class EscadaPorTempoDeQuadro {
  EscadaPorTempoDeQuadro({DateTime Function()? agora})
    : _agora = agora ?? DateTime.now;

  static const double lentoMs = 33.0;
  static const double folgadoMs = 12.0;
  static const int lentosParaDescer = 12;
  static const int folgadosParaSubir = 240;
  static const Duration esperaParaSubir = Duration(seconds: 8);

  /// TEMPO MINIMO ENTRE DUAS MUDANCAS DE DEGRAU, nos dois sentidos. Dois
  /// degraus seguidos sao dois redesenhos completos da previa; se eles
  /// couberem no mesmo instante, a escada vira um oscilador.
  static const Duration carencia = Duration(seconds: 2);

  /// QUADROS DESCARTADOS DEPOIS DE [acomodar]. Os primeiros quadros
  /// depois de uma politica nova sao os mais caros que ela tem (texturas
  /// refeitas, fotos refeitas) e nao dizem nada sobre o regime dela.
  static const int quadrosDeAcomodacao = 8;

  final DateTime Function() _agora;

  int _degraus = 0;
  double? _ema;
  int _lentos = 0;
  int _folgados = 0;
  DateTime? _ultimaDescida;
  DateTime? _ultimaMudanca;
  int _acomodando = 0;

  int get degraus => _degraus;

  /// Esta descartando os quadros logo depois de uma politica nova?
  bool get acomodando => _acomodando > 0;

  /// A POLITICA MUDOU: os proximos quadros sao efeito dela, nao causa.
  ///
  /// NAO MEXE NA CARENCIA de proposito. A politica muda por muita coisa
  /// que nao e a escada (o dedo, o play, a sonda de temperatura), e uma
  /// dessas nao pode congelar a escada por dois segundos — ela so
  /// descarta os quadros de transicao.
  void acomodar() {
    _acomodando = quadrosDeAcomodacao;
    _ema = null;
    _lentos = 0;
    _folgados = 0;
  }

  /// NAO HA MOTIVO EXTERNO PARA HAVER QUADRO: o que passar por aqui nao
  /// conta, e a contagem em curso morre. Sem isto uma rajada de quadros
  /// nascidos da propria politica somaria com a proxima rajada de
  /// verdade e decidiria um degrau que ninguem pediu.
  void pausar() {
    _lentos = 0;
    _folgados = 0;
  }

  /// Um quadro que levou [ms]. Devolve `true` quando o degrau mudou.
  bool amostra(double ms) {
    if (!ms.isFinite || ms <= 0) return false;
    if (_acomodando > 0) {
      _acomodando--;
      return false;
    }
    _ema = _ema == null ? ms : _ema! * .85 + ms * .15;
    // "Lento" e o quadro cru: uma media carregaria um pico isolado por
    // doze quadros e desceria a escada por um engasgo so.
    _lentos = ms > lentoMs ? _lentos + 1 : 0;
    _folgados = _ema! < folgadoMs ? _folgados + 1 : 0;
    final agora = _agora();
    final ultima = _ultimaMudanca;
    final emCarencia = ultima != null && agora.difference(ultima) < carencia;
    if (_lentos >= lentosParaDescer && _degraus < 2) {
      if (emCarencia) return false;
      _degraus++;
      _lentos = 0;
      _folgados = 0;
      _ultimaDescida = agora;
      _ultimaMudanca = agora;
      return true;
    }
    if (_folgados >= folgadosParaSubir && _degraus > 0) {
      final desde = _ultimaDescida;
      if (desde != null && agora.difference(desde) < esperaParaSubir) {
        return false;
      }
      if (emCarencia) return false;
      _degraus--;
      _folgados = 0;
      _ultimaMudanca = agora;
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
    _ultimaMudanca = null;
    _acomodando = 0;
  }
}
