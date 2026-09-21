import 'dart:async';

import 'package:flutter/gestures.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../interacao.dart';
import '../playback_controller.dart';
import '../qualidade3d_controller.dart';
import 'escada_por_tempo_de_quadro.dart';
import 'politica_de_desempenho.dart';

export 'politica_de_desempenho.dart';

/// O GERENTE DE DESEMPENHO — um lugar so que decide quanto a PREVIA pode
/// gastar agora.
///
/// ============================ POR QUE EXISTE ==========================
///
/// O app ja tinha varios pedacos desta decisao, cada um no seu canto: o
/// rascunho enquanto toca, o seletor manual de resolucao, o controlador
/// de qualidade da cena 3D (o unico que lia temperatura e memoria). Nada
/// os juntava: o aparelho esquentava e so a camada 3D reagia; o brilho, a
/// resolucao da previa, as miniaturas e o decode seguiam no maximo.
///
/// Este gerente e uma CASCA FINA. Ele nao mede nada que ja nao se media:
///
///  * temperatura e memoria vem do [ControladorDeQualidade3D] (a sonda e
///    dele; aqui so se pede que fique ligada com o editor aberto);
///  * "o dedo esta mexendo" vem de [Interacao.agora];
///  * "esta tocando" vem de [PlaybackController.tocandoAgora];
///  * o tempo de quadro e medido aqui, e SO com o editor aberto.
///
/// A saida e uma [PoliticaDeDesempenho] imutavel em [politica]. Quem
/// desenha escuta e obedece; ninguem mais precisa saber de temperatura.
///
/// ======================= A EXPORTACAO E IMUNE =========================
///
/// [politicaDeExportacao] devolve SEMPRE a politica completa, constante.
/// Perfil, calor, memoria e gesto so mexem na previa. O teto 3D que este
/// gerente repassa ao controlador tambem fica de fora de `paraExportacao`.
class AureaPerformanceManager with WidgetsBindingObserver {
  AureaPerformanceManager._();

  static final AureaPerformanceManager instancia = AureaPerformanceManager._();

  /// Instancia isolada, com relogio e controlador injetados.
  @visibleForTesting
  AureaPerformanceManager.paraTeste({
    ControladorDeQualidade3D? controlador,
    DateTime Function()? agora,
  }) : _controladorInjetado = controlador,
       _escada = EscadaPorTempoDeQuadro(agora: agora);

  static const chaveDoPerfil = 'desempenho_perfil';

  ControladorDeQualidade3D? _controladorInjetado;
  ControladorDeQualidade3D get _q3d =>
      _controladorInjetado ?? ControladorDeQualidade3D.instancia;

  EscadaPorTempoDeQuadro _escada = EscadaPorTempoDeQuadro();

  /// O perfil escolhido nos Ajustes (persistido em [chaveDoPerfil]).
  final ValueNotifier<PerfilDeDesempenho> perfil = ValueNotifier(
    PerfilDeDesempenho.automatico,
  );

  /// A politica da PREVIA em vigor. So avisa quando muda de verdade
  /// (a politica compara por valor).
  final ValueNotifier<PoliticaDeDesempenho> politica = ValueNotifier(
    politicaPara(PerfilDeDesempenho.automatico, const SinaisDeDesempenho()),
  );

  /// A politica da EXPORTACAO: sempre a completa, venha o sinal que vier.
  PoliticaDeDesempenho get politicaDeExportacao =>
      PoliticaDeDesempenho.exportacao;

  SharedPreferences? _prefs;
  bool _carregado = false;
  int _editoresAbertos = 0;
  bool _emSegundoPlano = false;
  bool _medindo = false;

  // O dedo: quantos na tela, e se algum ja ARRASTOU (passou da folga de
  // toque). Um toque num botao nao e interacao — e uma acao.
  final Map<int, Offset> _dedos = {};
  bool _arrastando = false;

  bool get editorAberto => _editoresAbertos > 0;

  /// A medicao de quadro esta ligada agora? (diagnostico e testes)
  bool get medindoQuadros => _medindo;

  // ---------------------------------------------------------- perfil

  /// Le o perfil guardado. Chamar cedo e opcional: sem isto, o primeiro
  /// [editorAbriu] (ou a linha dos Ajustes) carrega sozinho.
  Future<void> carregar([SharedPreferences? prefs]) async {
    if (_carregado && prefs == null) return;
    try {
      _prefs = prefs ?? await SharedPreferences.getInstance();
    } catch (_) {
      // Sem prefs (teste sem mock, plataforma sem plugin): fica o padrao.
      _carregado = true;
      return;
    }
    _carregado = true;
    final nome = _prefs?.getString(chaveDoPerfil);
    perfil.value = PerfilDeDesempenho.values.firstWhere(
      (p) => p.name == nome,
      orElse: () => PerfilDeDesempenho.automatico,
    );
    _recalcular();
  }

  Future<void> definirPerfil(PerfilDeDesempenho p) async {
    if (perfil.value != p) {
      perfil.value = p;
      // Trocar de perfil e um recomeco para o tempo de quadro: os degraus
      // de antes foram medidos com outra carga.
      _escada.zerar();
      _recalcular();
    }
    try {
      _prefs ??= await SharedPreferences.getInstance();
      await _prefs?.setString(chaveDoPerfil, p.name);
    } catch (_) {
      // Nao gravou: vale ate fechar o app.
    }
  }

  // ------------------------------------------------- vida do editor

  /// O editor abriu: liga as sondas. Par obrigatorio de [editorFechou].
  void editorAbriu() {
    _editoresAbertos++;
    if (_editoresAbertos > 1) return;
    if (!_carregado) unawaited(carregar());
    WidgetsBinding.instance.addObserver(this);
    GestureBinding.instance.pointerRouter.addGlobalRoute(_ponteiro);
    Interacao.agora.addListener(_recalcular);
    PlaybackController.tocandoAgora.addListener(_recalcular);
    _q3d.revisaoDosSinais.addListener(_recalcular);
    _q3d.nivel.addListener(_recalcular);
    _q3d.manterSondas(this);
    _acertarMedicao();
    // DEPOIS DO QUADRO, e nao agora: isto roda dentro do `initState` do
    // editor, e publicar a politica (ou mexer no nivel 3D) acordaria
    // ouvintes no meio da montagem da arvore.
    scheduleMicrotask(() {
      if (editorAberto) _recalcular();
    });
  }

  /// O editor fechou: desliga TUDO o que [editorAbriu] ligou. Fora do
  /// editor este gerente nao custa nada — nem Timer, nem callback.
  void editorFechou() {
    if (_editoresAbertos == 0) return;
    _editoresAbertos--;
    if (_editoresAbertos > 0) return;
    WidgetsBinding.instance.removeObserver(this);
    GestureBinding.instance.pointerRouter.removeGlobalRoute(_ponteiro);
    Interacao.agora.removeListener(_recalcular);
    PlaybackController.tocandoAgora.removeListener(_recalcular);
    _q3d.revisaoDosSinais.removeListener(_recalcular);
    _q3d.nivel.removeListener(_recalcular);
    _q3d.dispensarSondas(this);
    final estavaArrastando = _arrastando;
    _dedos.clear();
    _arrastando = false;
    _escada.zerar();
    _acertarMedicao();
    // O RESTO AVISA OUVINTES, e isto roda no `dispose` do editor — com a
    // arvore trancada. Fica para o fim do quadro.
    scheduleMicrotask(() {
      if (editorAberto) return;
      // Um gesto nao sobrevive ao editor que o abrigava.
      if (estavaArrastando) Interacao.soltar();
      // Fora do editor nao ha previa: o teto pedido ao 3D volta ao neutro,
      // para uma tela que desenhe 3D fora dele nao herdar o perfil.
      _q3d.definirTetoDaPolitica(PoliticaDeDesempenho.exportacao.teto3D);
      politica.value = politicaPara(perfil.value, sinais);
    });
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    final fundo =
        state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached;
    if (state != AppLifecycleState.resumed && !fundo) return;
    if (fundo == _emSegundoPlano) return;
    _emSegundoPlano = fundo;
    if (fundo) {
      // Um gesto nao atravessa a ida para o segundo plano: o `up` pode
      // nunca chegar, e o sinal ficaria preso ligado.
      _dedos.clear();
      _arrastando = false;
      Interacao.soltar();
    }
    _acertarMedicao();
  }

  void _acertarMedicao() {
    final medir = editorAberto && !_emSegundoPlano;
    if (medir && !_medindo) {
      _medindo = true;
      SchedulerBinding.instance.addTimingsCallback(_quadros);
    } else if (!medir && _medindo) {
      _medindo = false;
      SchedulerBinding.instance.removeTimingsCallback(_quadros);
    }
  }

  /// HA MOTIVO EXTERNO PARA ESTE QUADRO EXISTIR?
  ///
  /// ===================== O LACO QUE ISTO FECHA ========================
  ///
  /// A politica decide o que a previa desenha; desenhar produz um quadro;
  /// o quadro vinha medido para ca e podia mexer na politica de novo. Com
  /// brilho na cena esse circuito se sustentava sozinho: a bancada mediu
  /// 360 quadros em 6 s de tela PARADA, 80% de uma CPU, com ui_p50 de
  /// 0,8 ms (ninguem reconstruia nada) e raster de 15 ms (repintava tudo).
  ///
  /// O corte e na raiz: com o editor parado nao ha o que medir. Quadro em
  /// repouso ou e defeito de outra pessoa — e a escada nao pode reagir a
  /// ele — ou e o quadro que a propria politica encomendou, e reagir a
  /// ele e realimentar o laco. So o dedo e o relogio dao motivo para um
  /// quadro existir; so eles ligam a medicao.
  bool get _haMotivoParaQuadro =>
      PlaybackController.tocandoAgora.value || Interacao.agora.value;

  void _quadros(List<FrameTiming> timings) {
    for (final t in timings) {
      _medirQuadro(t.totalSpan.inMicroseconds / 1000.0);
    }
  }

  void _medirQuadro(double ms) {
    if (!_haMotivoParaQuadro) {
      // Nem guarda para depois: uma rajada de quadros de repouso somada a
      // proxima rajada de verdade decidiria um degrau que ninguem pediu.
      _escada.pausar();
      return;
    }
    if (_escada.amostra(ms)) _recalcular();
  }

  /// Um quadro que levou [ms] (para os testes e para a bancada). Passa
  /// pela MESMA porta do quadro de verdade, inclusive o portao de
  /// repouso — senao o teste provaria um caminho que o app nao tem.
  @visibleForTesting
  void amostraDeQuadro(double ms) => _medirQuadro(ms);

  /// Esquece o que foi medido ate agora (testes e troca de perfil).
  @visibleForTesting
  void zerarMedicaoDoQuadro() => _escada.zerar();

  // ------------------------------------------------------ interacao

  /// O editor avisa a cada mutacao do projeto. Se ha um dedo ARRASTANDO,
  /// a mutacao e parte de um gesto: marca [Interacao.agora]. Um toque
  /// num botao muda o projeto sem arrastar nada — nao marca.
  void houveMutacao() {
    if (_arrastando) Interacao.marcar();
  }

  void _ponteiro(PointerEvent e) {
    if (e is PointerDownEvent) {
      _dedos[e.pointer] = e.position;
    } else if (e is PointerMoveEvent) {
      final origem = _dedos[e.pointer];
      if (origem != null &&
          !_arrastando &&
          (e.position - origem).distance > kTouchSlop) {
        _arrastando = true;
      }
    } else if (e is PointerUpEvent || e is PointerCancelEvent) {
      _dedos.remove(e.pointer);
      if (_dedos.isEmpty) {
        final estava = _arrastando;
        _arrastando = false;
        // Soltou: o sinal cai JA, sem esperar a folga, e sai o quadro
        // final em qualidade cheia.
        if (estava && Interacao.agora.value) Interacao.soltar();
      }
    }
  }

  // ---------------------------------------------------------- conta

  SinaisDeDesempenho get sinais {
    const mb = 1024 * 1024;
    final d = _q3d.disponivelBytes;
    return SinaisDeDesempenho(
      termico: estadoTermicoDe(_q3d.termico),
      memoriaApertada: _q3d.memoriaBaixa || (d >= 0 && d < 300 * mb),
      memoriaEmEmergencia: _q3d.emEmergencia,
      degrausPorTempo: _escada.degraus,
      interagindo: Interacao.agora.value,
      tocando: PlaybackController.tocandoAgora.value,
      receita3D: _q3d.receita,
    );
  }

  bool _recalculando = false;

  void _recalcular() {
    // Repassar o teto ao controlador 3D pode mudar o `nivel` dele, que
    // chama de volta aqui: uma volta so.
    if (_recalculando) return;
    _recalculando = true;
    try {
      var p = politicaPara(perfil.value, sinais);
      if (editorAberto) {
        _q3d.definirTetoDaPolitica(p.teto3D);
        // A receita pode ter mudado com o teto novo.
        p = politicaPara(perfil.value, sinais);
      }
      // A POLITICA SO AVISA QUANDO MUDA DE VALOR (o ValueNotifier compara
      // com o `==` da politica, que e por valor). Uma instancia nova e
      // igual nao acorda ninguem — e por isso um `recalcular` a toa nao
      // custa um quadro.
      final antes = politica.value;
      politica.value = p;
      // MUDOU DE VERDADE: os proximos quadros sao efeito desta politica.
      // Deixa-los votar no proximo degrau e fechar o laco de novo.
      if (p != antes) _escada.acomodar();
    } finally {
      _recalculando = false;
    }
  }

  /// Refaz a politica agora (testes; e quem injeta sinais no controlador
  /// sem passar pela sonda).
  @visibleForTesting
  void recalcular() => _recalcular();

  /// Uma linha para o overlay de diagnostico.
  String resumo() => politica.value.toString();
}

/// O perfil escolhido, para a UI dos Ajustes.
final perfilDeDesempenhoProvider = Provider<PerfilDeDesempenho>((ref) {
  final g = AureaPerformanceManager.instancia;
  void ouvir() => ref.invalidateSelf();
  g.perfil.addListener(ouvir);
  ref.onDispose(() => g.perfil.removeListener(ouvir));
  return g.perfil.value;
});

/// A politica da PREVIA em vigor. O palco le com
/// `ref.watch(politicaDeDesempenhoProvider.select((p) => p.escalaDaPrevia))`
/// — sempre com select: `rascunho` liga e desliga a cada gesto.
///
/// A exportacao NAO le este provider: usa
/// [AureaPerformanceManager.politicaDeExportacao].
final politicaDeDesempenhoProvider = Provider<PoliticaDeDesempenho>((ref) {
  final g = AureaPerformanceManager.instancia;
  void ouvir() => ref.invalidateSelf();
  g.politica.addListener(ouvir);
  ref.onDispose(() => g.politica.removeListener(ouvir));
  return g.politica.value;
});
