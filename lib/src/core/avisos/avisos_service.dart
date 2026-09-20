import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../features/community/application/comunidade_service.dart';

/// UM AVISO AO VIVO, escrito de fora e visto em todo aparelho.
///
/// "Estamos resolvendo um bug que trava a exportação" precisa chegar a
/// quem tem o app instalado HOJE, sem esperar build novo. Os avisos
/// moram no servidor do mural; o app pergunta ao abrir e de dez em dez
/// minutos. Quem publica é quem tem a senha de moderação — o mesmo
/// canal que apaga post no mural.
///
/// O aviso tem id: dispensar esconde AQUELE aviso, e o próximo aparece
/// de novo. Sem id, a pessoa fecharia o primeiro e nunca veria mais
/// nenhum.
class Aviso {
  const Aviso({
    required this.id,
    required this.texto,
    this.nivel = NivelDoAviso.info,
    this.link,
    this.ate,
    this.popup = false,
  });

  final String id;
  final String texto;
  final NivelDoAviso nivel;

  /// Para onde "saiba mais" leva, quando há.
  final String? link;

  /// Depois deste instante o aviso some sozinho, mesmo sem o servidor
  /// ser atualizado.
  final DateTime? ate;

  /// Além da faixa, aparece como JANELA na primeira vez que o app abre.
  ///
  /// É para o recado que não pode passar batido — e por isso mesmo se
  /// usa pouco: uma janela que aparece toda vez vira a janela que se
  /// fecha sem ler.
  final bool popup;

  bool get vencido => ate != null && DateTime.now().isAfter(ate!);

  static Aviso? deJson(Object? bruto) {
    if (bruto is! Map) return null;
    final m = bruto.cast<String, dynamic>();
    final texto = '${m['texto'] ?? ''}'.trim();
    final id = '${m['id'] ?? ''}'.trim();
    if (texto.isEmpty || id.isEmpty) return null;
    return Aviso(
      id: id,
      texto: texto,
      nivel: NivelDoAviso.values.firstWhere(
        (n) => n.name == m['nivel'],
        orElse: () => NivelDoAviso.info,
      ),
      link: (m['link'] as String?)?.trim().isEmpty ?? true
          ? null
          : m['link'] as String,
      ate: DateTime.tryParse('${m['ate'] ?? ''}'),
      popup: m['popup'] == true,
    );
  }

  Map<String, dynamic> toJson() => {
    'id': id,
    'texto': texto,
    'nivel': nivel.name,
    if (link != null) 'link': link,
    if (ate != null) 'ate': ate!.toUtc().toIso8601String(),
    if (popup) 'popup': true,
  };
}

enum NivelDoAviso { info, atencao, problema }

class AvisosService with WidgetsBindingObserver {
  AvisosService({
    HttpClient? http,
    this.endereco = ComunidadeService.enderecoPadrao,
  }) : _http =
           http ??
           (HttpClient()..connectionTimeout = const Duration(seconds: 8));

  static final instance = AvisosService();

  static const _chaveGuardado = 'aviso.atual';
  static const _chaveDispensados = 'aviso.dispensados';
  static const _chaveVistosEmJanela = 'aviso.popupVistos';

  /// A chave de quando havia UM aviso só. Lida na primeira vez, para
  /// quem atualizou o app não ver de volta o recado que já fechou.
  static const _chaveDispensadoAntiga = 'aviso.dispensado';
  static const intervalo = Duration(minutes: 10);

  final HttpClient _http;
  final String endereco;

  /// Os avisos que a tela deve mostrar agora, de cima para baixo.
  final ValueNotifier<List<Aviso>> todos = ValueNotifier(const []);

  /// O aviso que deve aparecer como JANELA agora — e null assim que
  /// alguém fecha. Quem mostra é a Início.
  final ValueNotifier<Aviso?> emJanela = ValueNotifier(null);

  Timer? _relogio;
  final Set<String> _dispensados = {};
  final Set<String> _vistosEmJanela = {};
  bool _leuPrefs = false;
  bool _ligado = false;
  bool _emSegundoPlano = false;
  DateTime? _ultimaBusca;

  /// Quem pergunta ao sistema que horas são (o teste injeta).
  @visibleForTesting
  DateTime Function() agora = DateTime.now;

  /// O relógio das buscas está rodando agora? (diagnóstico e testes)
  bool get buscando => _relogio != null;

  /// Quando foi a última ida ao servidor (só o teste da janela precisa).
  @visibleForTesting
  DateTime? get ultimaBuscaParaTeste => _ultimaBusca;

  /// Chamar uma vez, na Início: lê os últimos guardados (aparecem na
  /// hora, mesmo sem rede) e vai buscar os de agora.
  Future<void> iniciar() async {
    _ligado = true;
    _observar(true);
    await _lerPrefs();
    _acertarRelogio();
  }

  /// O APP EM SEGUNDO PLANO NÃO PERGUNTA NADA.
  ///
  /// A faixa vive na Início, que nunca sai da árvore: na prática este
  /// relógio nascia junto com o app e batia no servidor de dez em dez
  /// minutos para sempre — inclusive com o app fora da tela, gastando
  /// rádio e bateria para atualizar um aviso que ninguém está vendo.
  /// Ao voltar, se a janela já passou, busca uma vez.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    final fundo =
        state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached;
    if (state != AppLifecycleState.resumed && !fundo) return;
    if (fundo == _emSegundoPlano) return;
    _emSegundoPlano = fundo;
    _acertarRelogio();
  }

  void _acertarRelogio() {
    if (_ligado && !_emSegundoPlano) {
      final desde = _ultimaBusca;
      if (desde == null || agora().difference(desde) >= intervalo) {
        unawaited(atualizar());
      }
      _relogio ??= Timer.periodic(intervalo, (_) => atualizar());
    } else {
      _relogio?.cancel();
      _relogio = null;
    }
  }

  void _observar(bool ligar) {
    final binding = WidgetsBinding.instance;
    if (ligar) {
      binding.addObserver(this);
    } else {
      binding.removeObserver(this);
    }
  }

  Future<void> _lerPrefs() async {
    if (_leuPrefs) return;
    _leuPrefs = true;
    try {
      final prefs = await SharedPreferences.getInstance();
      _dispensados.addAll(prefs.getStringList(_chaveDispensados) ?? const []);
      final antigo = prefs.getString(_chaveDispensadoAntiga);
      if (antigo != null) _dispensados.add(antigo);
      _vistosEmJanela.addAll(
        prefs.getStringList(_chaveVistosEmJanela) ?? const [],
      );
      final guardado = prefs.getString(_chaveGuardado);
      if (guardado != null) {
        final lista = jsonDecode(guardado);
        if (lista is List) {
          _mostrar([for (final item in lista) ?Aviso.deJson(item)]);
        }
      }
    } catch (_) {
      // Sem prefs (ou com prefs de outra versao), comeca do zero: o
      // servidor manda os avisos de novo em seguida.
    }
  }

  Future<void> atualizar() async {
    _ultimaBusca = agora();
    await _lerPrefs();
    try {
      final req = await _http.getUrl(Uri.parse('$endereco/aviso'));
      final res = await req.close().timeout(const Duration(seconds: 8));
      final corpo = await res.transform(utf8.decoder).join();
      if (res.statusCode != 200) return;
      final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
      // `avisos` e a lista do servidor novo; `aviso` (um so) e o que os
      // servidores antigos mandam — e continua valendo.
      final lista = <Aviso>[];
      if (m['avisos'] case final List bruta) {
        for (final item in bruta) {
          if (Aviso.deJson(item) case final a?) lista.add(a);
        }
      } else if (Aviso.deJson(m['aviso']) case final a?) {
        lista.add(a);
      }
      _mostrar(lista);
      try {
        final prefs = await SharedPreferences.getInstance();
        if (lista.isEmpty) {
          await prefs.remove(_chaveGuardado);
        } else {
          await prefs.setString(
            _chaveGuardado,
            jsonEncode([for (final a in lista) a.toJson()]),
          );
        }
      } catch (_) {}
    } catch (_) {
      // Sem rede, fica o que já estava na tela. Um aviso velho por dez
      // minutos é melhor do que um aviso que pisca a cada falha.
    }
  }

  void _mostrar(List<Aviso> lista) {
    final vivos = [
      for (final a in lista)
        if (!a.vencido && !_dispensados.contains(a.id)) a,
    ];
    todos.value = vivos;
    // A JANELA e para o primeiro popup ainda nao visto. Uma vez por id:
    // reabrir o app nao repete o mesmo recado.
    if (emJanela.value == null) {
      for (final a in vivos) {
        if (a.popup && !_vistosEmJanela.contains(a.id)) {
          emJanela.value = a;
          break;
        }
      }
    }
  }

  /// Fecha ESTE aviso. O próximo, com outro id, volta a aparecer.
  Future<void> dispensar(String id) async {
    _dispensados.add(id);
    todos.value = [
      for (final a in todos.value)
        if (a.id != id) a,
    ];
    if (emJanela.value?.id == id) emJanela.value = null;
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_chaveDispensados, _dispensados.toList());
    } catch (_) {}
  }

  /// A janela foi lida. A faixa continua na tela — quem quiser o link
  /// depois, acha lá.
  Future<void> fecharJanela() async {
    final a = emJanela.value;
    emJanela.value = null;
    if (a == null) return;
    _vistosEmJanela.add(a.id);
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_chaveVistosEmJanela, _vistosEmJanela.toList());
    } catch (_) {}
  }

  /// Para o relogio das buscas. Quem chama e a faixa, ao sair da tela.
  void parar() {
    _ligado = false;
    _observar(false);
    _relogio?.cancel();
    _relogio = null;
  }
}
