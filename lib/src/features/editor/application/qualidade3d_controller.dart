import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/orcamento_render.dart';
import 'preview_stats.dart';
import 'sistema_nativo.dart';

/// O CONTROLADOR DE QUALIDADE — quem decide em que nivel a cena 3D
/// desenha, e desce a escada ANTES de o app cair.
///
/// Quatro sinais entram, e o nivel e o menor que qualquer um deles pede:
///
/// 1. O ORCAMENTO (antes do primeiro quadro): a estimativa de GPU da cena
///    em cada nivel, contra o orcamento do aparelho. E o unico sinal que
///    age antes de a memoria ser gasta — os outros reagem.
/// 2. O TEMPO DE QUADRO: se a rasterizacao passa de 33 ms (abaixo de
///    30 fps) por doze quadros seguidos, desce um degrau; so sobe de
///    volta depois de 120 quadros folgados. Lag e aceitavel; o degrau
///    existe para o quadro nao chegar perto do timeout da GPU.
/// 3. A MEMORIA DO SISTEMA: o aviso de pressao do sistema (o mesmo que
///    precede o jetsam) manda direto para emergencia por 30 segundos;
///    a memoria disponivel, lida a cada dois segundos, poe um teto.
/// 4. O TERMICO: aparelho serio/critico nao ganha ultra nem alta.
///
/// A pessoa ainda manda: o teto dos Ajustes (Automatica, Maxima,
/// Equilibrada, Leve) vale acima de tudo, para baixo.
class ControladorDeQualidade3D with WidgetsBindingObserver {
  ControladorDeQualidade3D._();

  static final ControladorDeQualidade3D instancia =
      ControladorDeQualidade3D._();

  static const _kTeto = 'qualidade3d_teto';

  /// O nivel em vigor.
  final ValueNotifier<Qualidade3D> nivel = ValueNotifier(Qualidade3D.alta);

  /// Quao perto do limite (o pior entre estimativa e memoria real).
  final ValueNotifier<NivelDePressao> pressao = ValueNotifier(
    NivelDePressao.seguro,
  );

  /// Por que o nivel e o que e — para o overlay e para o relatorio.
  final ValueNotifier<String> motivo = ValueNotifier('sem cena 3D');

  /// A ultima estimativa, no nivel escolhido pelo orcamento.
  final ValueNotifier<EstimativaGpu> estimativa = ValueNotifier(
    EstimativaGpu.vazia,
  );

  /// Toda mudanca de nivel, para o relatorio de estresse.
  final List<String> historico = [];

  /// Sobe um a cada leitura da sonda e a cada aviso de memoria. Quem
  /// deriva coisas de [termico]/[disponivelBytes] (o gerente de
  /// desempenho) escuta isto em vez de abrir uma segunda sonda.
  final ValueNotifier<int> revisaoDosSinais = ValueNotifier(0);

  int orcamentoBytes = orcamentoGpuBytes(0);
  int ramBytes = 0;
  int disponivelBytes = -1;
  int termico = 0;
  bool memoriaBaixa = false;
  TetoDeQualidade3D teto = TetoDeQualidade3D.automatico;

  SharedPreferences? _prefs;
  Qualidade3D _tetoDoOrcamento = Qualidade3D.ultra;
  Qualidade3D _tetoTermico = Qualidade3D.ultra;
  Qualidade3D _tetoDeMemoria = Qualidade3D.ultra;

  /// O teto que o PERFIL de desempenho pede para a previa. Fica de fora
  /// da exportacao de proposito (ver [paraExportacao]).
  Qualidade3D _tetoDaPolitica = Qualidade3D.ultra;
  int _degrausPorTempo = 0;
  double? _ema;
  int _lentos = 0;
  int _rapidos = 0;
  DateTime? _ultimaDescida;
  DateTime? _emergenciaAte;
  Timer? _fimDaEmergencia;
  int _cenasNaTela = 0;
  Timer? _sonda;
  bool _observando = false;
  bool _timingsLigados = false;

  /// Quem, alem das cenas 3D, precisa das sondas ligadas (o gerente de
  /// desempenho, enquanto o editor esta aberto).
  final Set<Object> _interessados = {};

  /// O app esta em segundo plano: nada de sonda nem de medicao.
  bool _emSegundoPlano = false;
  PerfilDaCena _perfil = PerfilDaCena.nada;
  double _larguraPx = 0, _alturaPx = 0;

  /// Sem sinais de tempo e memoria (testes): o nivel vem so do orcamento
  /// e do teto.
  @visibleForTesting
  static DateTime Function() agora = DateTime.now;

  ReceitaDeQualidade get receita => ReceitaDeQualidade.de(nivel.value);

  /// O que o tempo de quadro pediu (0..2 degraus abaixo do teto).
  int get degrausPorTempo => _degrausPorTempo;

  Future<void> carregar(SharedPreferences prefs) async {
    _prefs = prefs;
    teto = TetoDeQualidade3D.values.firstWhere(
      (t) => t.name == prefs.getString(_kTeto),
      orElse: () => TetoDeQualidade3D.automatico,
    );
    _recalcular('teto dos Ajustes');
  }

  Future<void> definirTeto(TetoDeQualidade3D t) async {
    teto = t;
    await _prefs?.setString(_kTeto, t.name);
    // Trocar o teto e um recomeco para os sinais de tempo.
    _degrausPorTempo = 0;
    _lentos = 0;
    _rapidos = 0;
    _registrarDeNovo();
    _recalcular('teto dos Ajustes');
  }

  /// A memoria fisica, quando o sistema disse; refaz o orcamento.
  void informarRam(int bytes) {
    if (bytes <= 0 || bytes == ramBytes) return;
    ramBytes = bytes;
    orcamentoBytes = orcamentoGpuBytes(bytes);
    _registrarDeNovo();
    _recalcular('orcamento do aparelho');
  }

  /// A cena que vai desenhar, com o tamanho da area em pixels da
  /// composicao. Chamar quando a cena (ou a area) muda — e barato.
  void registrarCena(PerfilDaCena perfil, double larguraPx, double alturaPx) {
    _perfil = perfil;
    _larguraPx = larguraPx;
    _alturaPx = alturaPx;
    _registrarDeNovo();
    _recalcular('orcamento da cena');
  }

  void _registrarDeNovo() {
    if (_larguraPx <= 0 || _alturaPx <= 0) return;
    final r = escolherPeloOrcamento(
      perfil: _perfil,
      orcamentoBytes: orcamentoBytes,
      larguraPx: _larguraPx,
      alturaPx: _alturaPx,
      teto: tetoComoNivel(teto),
    );
    _tetoDoOrcamento = r.nivel;
    estimativa.value = r.estimativa;
  }

  /// A receita e a escala da EXPORTACAO em [larguraPx] x [alturaPx]:
  /// nunca melhor que o preview (para o que se exporta ser o que se viu)
  /// e nunca alem do que cabe na memoria.
  ({Qualidade3D nivel, double escala, EstimativaGpu estimativa}) paraExportacao(
    double larguraPx,
    double alturaPx,
  ) {
    final r = receitaDeExportacao(
      perfil: _perfil,
      orcamentoBytes: orcamentoBytes,
      larguraPx: larguraPx,
      alturaPx: alturaPx,
    );
    // O PERFIL DE DESEMPENHO NAO ENTRA AQUI. Ele e uma escolha sobre a
    // PREVIA (bateria, calor, fluidez ao editar); o arquivo exportado nao
    // pode sair pior por causa dela.
    final tetoDoPreview = _nivelEstatico(comPolitica: false);
    final n = Qualidade3D.values[math.max(r.nivel.index, tetoDoPreview.index)];
    return (nivel: n, escala: r.escala, estimativa: r.estimativa);
  }

  Qualidade3D _nivelEstatico({bool comPolitica = true}) {
    var i = tetoComoNivel(teto).index;
    i = math.max(i, _tetoDoOrcamento.index);
    i = math.max(i, _tetoTermico.index);
    i = math.max(i, _tetoDeMemoria.index);
    if (comPolitica) i = math.max(i, _tetoDaPolitica.index);
    return Qualidade3D.values[i];
  }

  /// O teto que o perfil de desempenho pede a PREVIA 3D. So para baixo, e
  /// nunca para a exportacao.
  void definirTetoDaPolitica(Qualidade3D t) {
    if (t == _tetoDaPolitica) return;
    _tetoDaPolitica = t;
    _recalcular('perfil de desempenho');
  }

  Qualidade3D get tetoDaPolitica => _tetoDaPolitica;

  // ------------------------------------------------------------ sinais

  /// Uma cena 3D entrou na tela: liga as sondas.
  void entrou() {
    _cenasNaTela++;
    if (_cenasNaTela > 1) return;
    _acertarSondas();
  }

  void saiu() {
    _cenasNaTela = math.max(0, _cenasNaTela - 1);
    if (_cenasNaTela > 0) return;
    _acertarSondas();
  }

  int get cenasNaTela => _cenasNaTela;

  /// [quem] precisa de termico e memoria mesmo sem cena 3D na tela (o
  /// gerente de desempenho, com o editor aberto). Devolver com
  /// [dispensarSondas] — e o par que desliga tudo.
  void manterSondas(Object quem) {
    if (!_interessados.add(quem)) return;
    _acertarSondas();
  }

  void dispensarSondas(Object quem) {
    if (!_interessados.remove(quem)) return;
    _acertarSondas();
  }

  bool get _alguemPrecisa => _cenasNaTela > 0 || _interessados.isNotEmpty;

  /// A sonda de dois segundos esta rodando agora? (diagnostico e testes)
  bool get sondando => _sonda != null;

  /// A medicao de quadro esta ligada agora? (diagnostico e testes)
  bool get medindoQuadros => _timingsLigados;

  /// LIGA E DESLIGA DE FORMA SIMETRICA.
  ///
  /// `saiu()` so cancelava o Timer: o `addTimingsCallback` e o observador
  /// ficavam para sempre depois da primeira cena 3D — um callback por lote
  /// de quadros, no app inteiro, para nao medir nada. E nada disto olhava
  /// o ciclo de vida: com o app em segundo plano a sonda seguia batendo no
  /// canal nativo a cada dois segundos.
  ///
  /// O observador fica enquanto alguem precisa (e ele que ouve o
  /// `resumed` para religar); Timer e medicao de quadro so com o app na
  /// frente.
  void _acertarSondas() {
    final precisa = _alguemPrecisa;
    if (precisa && !_observando) {
      _observando = true;
      WidgetsBinding.instance.addObserver(this);
    } else if (!precisa && _observando) {
      _observando = false;
      WidgetsBinding.instance.removeObserver(this);
    }
    final medir = precisa && !_emSegundoPlano;
    // A medicao de quadro e do 3D: sem cena na tela nao ha o que medir.
    final medirQuadros = medir && _cenasNaTela > 0;
    if (medirQuadros && !_timingsLigados) {
      _timingsLigados = true;
      SchedulerBinding.instance.addTimingsCallback(_quadros);
    } else if (!medirQuadros && _timingsLigados) {
      _timingsLigados = false;
      SchedulerBinding.instance.removeTimingsCallback(_quadros);
    }
    if (medir && _sonda == null) {
      _sonda = Timer.periodic(const Duration(seconds: 2), (_) => sondar());
      unawaited(sondar());
    } else if (!medir && _sonda != null) {
      _sonda?.cancel();
      _sonda = null;
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // `inactive` nao conta: e a central de notificacoes por cima do app,
    // que continua na tela.
    final fundo =
        state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached;
    if (state != AppLifecycleState.resumed && !fundo) return;
    if (fundo == _emSegundoPlano) return;
    _emSegundoPlano = fundo;
    _acertarSondas();
  }

  /// Le memoria e termico do sistema, e o RSS do processo.
  Future<void> sondar() async {
    final m = await SistemaNativo.memoria();
    final t = await SistemaNativo.termico();
    int? rss;
    try {
      rss = ProcessInfo.currentRss;
    } catch (_) {
      rss = null;
    }
    atualizarSistema(memoria: m, termico: t, rssBytes: rss);
  }

  /// O que a sonda leu (separado para os testes injetarem valores).
  void atualizarSistema({
    MemoriaDoSistema? memoria,
    int termico = 0,
    int? rssBytes,
  }) {
    if (memoria != null) {
      informarRam(memoria.total);
      disponivelBytes = memoria.disponivel;
      memoriaBaixa = memoria.baixa;
    }
    this.termico = termico;
    if (rssBytes != null) PreviewStats.rssMb.value = rssBytes ~/ (1024 * 1024);

    const mb = 1024 * 1024;
    _tetoTermico = switch (termico) {
      >= 3 => Qualidade3D.baixa,
      2 => Qualidade3D.media,
      _ => Qualidade3D.ultra,
    };
    final d = disponivelBytes;
    if (memoriaBaixa || (d >= 0 && d < 150 * mb)) {
      _tetoDeMemoria = Qualidade3D.baixa;
    } else if (d >= 0 && d < 300 * mb) {
      _tetoDeMemoria = Qualidade3D.media;
    } else if (d < 0 || d > 400 * mb) {
      _tetoDeMemoria = Qualidade3D.ultra;
    }
    if (d >= 0 && d < 80 * mb) {
      _emergencia('memoria disponivel abaixo de 80 MB');
    }
    _recalcular(
      termico >= 2
          ? 'aparelho esquentando'
          : (d >= 0 && d < 300 * mb)
          ? 'pouca memoria disponivel'
          : 'sinais do sistema',
    );
    revisaoDosSinais.value++;
  }

  void _quadros(List<FrameTiming> timings) {
    if (_cenasNaTela == 0) return;
    for (final t in timings) {
      amostraDeQuadro(t.rasterDuration.inMicroseconds / 1000.0);
    }
  }

  /// Um quadro rasterizado em [rasterMs]. Doze lentos seguidos descem um
  /// degrau (ate dois); 120 folgados sobem um — nunca antes de cinco
  /// segundos da ultima descida.
  void amostraDeQuadro(double rasterMs) {
    if (!rasterMs.isFinite || rasterMs <= 0) return;
    _ema = _ema == null ? rasterMs : _ema! * .85 + rasterMs * .15;
    // "Lento" e o quadro CRU, nao a media: uma media movel carrega um
    // pico isolado de 200 ms por doze quadros e desceria a escada por um
    // engasgo so. "Folgado" e a media, para a subida nao ser nervosa.
    final lento = rasterMs > 33.0;
    final rapido = _ema! < 12.0;
    _lentos = lento ? _lentos + 1 : 0;
    _rapidos = rapido ? _rapidos + 1 : 0;
    if (_lentos >= 12 && _degrausPorTempo < 2) {
      _degrausPorTempo++;
      _lentos = 0;
      _rapidos = 0;
      _ultimaDescida = agora();
      _recalcular('quadros acima de 33 ms');
      return;
    }
    if (_rapidos >= 120 && _degrausPorTempo > 0) {
      final desde = _ultimaDescida;
      if (desde != null &&
          agora().difference(desde) < const Duration(seconds: 5)) {
        return;
      }
      _degrausPorTempo--;
      _rapidos = 0;
      _recalcular('quadros folgados');
    }
  }

  @override
  void didHaveMemoryPressure() => pressaoDeMemoria();

  /// Quanto a emergencia segura o nivel depois do ultimo aviso.
  ///
  /// Eram oito segundos. Subir de nivel refaz luzes, sombras, malhas de
  /// LOD e alvos de desenho — memoria nova pedida enquanto a antiga ainda
  /// espera o coletor, e justamente logo depois de o sistema avisar que
  /// ela esta acabando. Com a memoria ainda apertada, o aviso voltava, e
  /// o nivel oscilava. Trinta segundos: um trecho mais simples da cena
  /// vale mais que o app morto (estabilidade antes de qualidade).
  static const janelaDeEmergencia = Duration(seconds: 30);

  /// O sistema avisou que a memoria esta acabando (iOS: o aviso que
  /// precede o jetsam; Android: onTrimMemory). Emergencia por
  /// [janelaDeEmergencia], e um degrau a menos depois.
  void pressaoDeMemoria() {
    _degrausPorTempo = math.min(2, _degrausPorTempo + 1);
    _emergencia('aviso de memoria do sistema');
    revisaoDosSinais.value++;
  }

  void _emergencia(String porQue) {
    _emergenciaAte = agora().add(janelaDeEmergencia);
    _fimDaEmergencia?.cancel();
    _fimDaEmergencia = Timer(janelaDeEmergencia, () {
      _recalcular('emergencia passou');
      revisaoDosSinais.value++;
    });
    _recalcular(porQue);
  }

  bool get emEmergencia {
    final ate = _emergenciaAte;
    return ate != null && agora().isBefore(ate);
  }

  void _recalcular(String porQue) {
    var i = _nivelEstatico().index;
    i = math.min(Qualidade3D.values.length - 1, i + _degrausPorTempo);
    if (emEmergencia) i = Qualidade3D.emergencia.index;
    final novo = Qualidade3D.values[i];

    var p = estimativa.value.pressaoEm(orcamentoBytes);
    const mb = 1024 * 1024;
    final d = disponivelBytes;
    // A memoria REAL do sistema tambem e pressao, mesmo que a estimativa
    // esteja folgada: e ela que o jetsam olha.
    if (memoriaBaixa || (d >= 0 && d < 150 * mb)) {
      p = NivelDePressao
          .values[math.max(p.index, NivelDePressao.pressao.index)];
    } else if (d >= 0 && d < 300 * mb) {
      p = NivelDePressao.values[math.max(p.index, NivelDePressao.alerta.index)];
    }
    if (emEmergencia) p = NivelDePressao.emergencia;
    pressao.value = p;

    if (novo != nivel.value) {
      historico.add(
        '${agora().toIso8601String().substring(11, 19)} '
        '${qualidade3dRotulo(nivel.value)} -> ${qualidade3dRotulo(novo)}: $porQue',
      );
      if (historico.length > 200) historico.removeAt(0);
      nivel.value = novo;
      motivo.value = porQue;
    } else if (motivo.value != porQue &&
        (porQue.startsWith('orcamento') || porQue.startsWith('teto'))) {
      motivo.value = porQue;
    }
  }

  /// Uma linha por sinal, para o overlay e o relatorio.
  String resumo() {
    final e = estimativa.value;
    return 'nivel ${qualidade3dRotulo(nivel.value)} (${motivo.value}) · '
        'pressao ${nivelDePressaoRotulo(pressao.value)}\n'
        'GPU estimada ${bytesLegiveis(e.total)} de ${bytesLegiveis(orcamentoBytes)} '
        '(alvos ${bytesLegiveis(e.alvosDeRender)}, sombras ${bytesLegiveis(e.sombras)}, '
        'texturas ${bytesLegiveis(e.texturas)}, geometria ${bytesLegiveis(e.geometria)})\n'
        'RAM ${ramBytes > 0 ? bytesLegiveis(ramBytes) : '?'} · '
        'disponivel ${disponivelBytes >= 0 ? bytesLegiveis(disponivelBytes) : '?'} · '
        'termico $termico · degraus por tempo $_degrausPorTempo';
  }

  /// Volta ao estado inicial (testes e o comeco de cada cena de estresse).
  void zerar() {
    _degrausPorTempo = 0;
    _ema = null;
    _lentos = 0;
    _rapidos = 0;
    _ultimaDescida = null;
    _emergenciaAte = null;
    _fimDaEmergencia?.cancel();
    _fimDaEmergencia = null;
    _tetoDoOrcamento = Qualidade3D.ultra;
    _tetoTermico = Qualidade3D.ultra;
    _tetoDeMemoria = Qualidade3D.ultra;
    _tetoDaPolitica = Qualidade3D.ultra;
    _perfil = PerfilDaCena.nada;
    _larguraPx = 0;
    _alturaPx = 0;
    disponivelBytes = -1;
    memoriaBaixa = false;
    termico = 0;
    historico.clear();
    estimativa.value = EstimativaGpu.vazia;
    _recalcular('zerado');
  }
}
