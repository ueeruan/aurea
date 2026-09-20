import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../features/community/application/comunidade_service.dart';

/// A ULTIMA VERSAO PUBLICADA, do jeito que o servidor do mural conta.
@immutable
class VersaoPublicada {
  const VersaoPublicada({
    required this.codigo,
    required this.nome,
    required this.apk,
    this.notas = '',
    this.obrigatoria = false,
    this.tamanho = 0,
    this.sha256 = '',
  });

  /// O versionCode do Android. E ELE que decide quem e mais novo — o
  /// nome e para ler. Comparar "1.1.10" com "1.1.9" por texto diria que
  /// 1.1.9 vem depois.
  final int codigo;
  final String nome;

  /// O endereco https do arquivo.
  final String apk;

  /// O que aparece na faixa. Curto: a faixa e uma linha.
  final String notas;

  /// A faixa nao fecha, e o app so segue depois de atualizar.
  final bool obrigatoria;

  /// Tamanho em bytes, para a barra de progresso ter um fim. Zero quando
  /// o servidor nao sabe.
  final int tamanho;

  /// SHA-256 do arquivo, em hex. Vazio quando o servidor nao informa.
  final String sha256;

  bool get valida => codigo > 0 && nome.isNotEmpty && apk.startsWith('https://');

  static VersaoPublicada? deJson(Object? bruto) {
    if (bruto is! Map) return null;
    final m = bruto.cast<String, dynamic>();
    final v = VersaoPublicada(
      codigo: (m['codigo'] as num?)?.toInt() ?? 0,
      nome: '${m['versao'] ?? ''}'.trim(),
      apk: '${m['apk'] ?? ''}'.trim(),
      notas: '${m['notas'] ?? ''}'.trim(),
      obrigatoria: m['obrigatoria'] == true,
      tamanho: (m['tamanho'] as num?)?.toInt() ?? 0,
      sha256: '${m['sha256'] ?? ''}'.trim().toLowerCase(),
    );
    return v.valida ? v : null;
  }

  @override
  bool operator ==(Object other) =>
      other is VersaoPublicada && other.codigo == codigo && other.apk == apk;

  @override
  int get hashCode => Object.hash(codigo, apk);
}

/// O QUE ESTA ACONTECENDO AGORA, na tela.
enum FaseDaAtualizacao { parada, baixando, conferindo, instalando, pronto }

/// ATUALIZACAO DENTRO DO PROPRIO APLICATIVO (Android).
///
/// O APARELHO SE ATUALIZA SOZINHO. O servidor do mural guarda qual e a
/// ultima versao e onde o arquivo esta; o app pergunta ao abrir e, quando
/// a de la e mais nova, mostra a faixa e baixa.
///
/// POR QUE O ENDERECO NAO MORA NO APK: dentro do APK qualquer endereco e
/// publico — um APK e um zip, e trocar uma linha no meio leva minutos.
/// Com o endereco no servidor, trocar o que todo mundo baixa exige a
/// senha de moderacao. E o mesmo motivo pelo qual a chave do mural mora
/// la.
///
/// ISTO E SO PARA ANDROID. O iOS nao instala aplicativo fora da loja; la
/// o caminho e o TestFlight, e o service responde que nao da.
class AtualizacaoService with WidgetsBindingObserver {
  AtualizacaoService({
    HttpClient? http,
    MethodChannel? canal,
    this.endereco = ComunidadeService.enderecoPadrao,
    this.soAndroid,
    Future<Directory> Function()? pastaDeDownload,
  }) : _pasta = pastaDeDownload ?? _pastaDoCache,
       _http =
           http ??
           (HttpClient()..connectionTimeout = const Duration(seconds: 15)),
       _canal = canal ?? const MethodChannel('aurea/atualizacao');

  static final instance = AtualizacaoService();

  static const _chaveCodigoInstalado = 'atualizacao.codigoNaEpoca';
  static const _chaveAdiada = 'atualizacao.adiadaAte';
  static const _chaveVista = 'atualizacao.vista';

  /// Uma vez por abertura, e mais uma de vez em quando: o app fica horas
  /// aberto e uma versao nova pode sair no meio.
  static const intervalo = Duration(hours: 2);

  final HttpClient _http;
  final MethodChannel _canal;
  final String endereco;

  /// Nulo = pergunta ao sistema. O teste roda no PC, onde
  /// `Platform.isAndroid` e falso — e sem poder dizer "finja que e
  /// Android" o caminho inteiro ficaria sem teste nenhum. Publico de
  /// proposito: um parametro nomeado nao pode preencher um campo privado
  /// sem uma linha a mais de cerimonia.
  final bool? soAndroid;

  /// ONDE O APK E BAIXADO. E `cache/atualizacao/`, e nao os Documentos: e
  /// a pasta que o provedor de arquivos expoe ao instalador, e o sistema
  /// pode recolher sozinho quando precisar de espaco — um APK ja
  /// instalado nao precisa mais existir. Injetavel para o teste, que nao
  /// tem o `path_provider`.
  final Future<Directory> Function() _pasta;

  static Future<Directory> _pastaDoCache() async =>
      Directory('${(await getTemporaryDirectory()).path}/atualizacao');

  /// A versao oferecida, ou nula quando nao ha o que atualizar.
  final ValueNotifier<VersaoPublicada?> oferecida = ValueNotifier(null);

  /// Em que pe esta o download.
  final ValueNotifier<FaseDaAtualizacao> fase = ValueNotifier(
    FaseDaAtualizacao.parada,
  );

  /// 0..1 enquanto baixa. Negativo quando nao da para saber.
  final ValueNotifier<double> progresso = ValueNotifier(-1);

  /// A ultima falha, em palavras. Nulo = nada falhou.
  final ValueNotifier<String?> erro = ValueNotifier(null);

  Timer? _relogio;
  bool _iniciado = false;
  bool _emSegundoPlano = false;
  DateTime? _ultimaVerificacao;

  /// Quem pergunta ao sistema que horas sao (o teste injeta).
  @visibleForTesting
  DateTime Function() agora = DateTime.now;

  /// O caminho do APK ja baixado e conferido, quando ha um.
  String? _arquivoBaixado;

  static bool get suportado => Platform.isAndroid;

  bool get _suportado => soAndroid ?? Platform.isAndroid;

  Future<void> iniciar() async {
    if (!_suportado || _iniciado) return;
    _iniciado = true;
    WidgetsBinding.instance.addObserver(this);
    _acertarRelogio();
  }

  void parar() {
    if (_iniciado) WidgetsBinding.instance.removeObserver(this);
    _relogio?.cancel();
    _relogio = null;
    _iniciado = false;
  }

  /// O relogio de duas horas esta rodando agora? (diagnostico e testes)
  bool get verificando => _relogio != null;

  /// EM SEGUNDO PLANO NAO SE PROCURA VERSAO NOVA.
  ///
  /// A faixa vive na Inicio, que nunca sai da arvore: este relogio nascia
  /// com o app e ficava para sempre, batendo no servidor a cada duas horas
  /// mesmo com o app fora da tela. Ao voltar, se ja passou o intervalo,
  /// verifica uma vez.
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
    if (_iniciado && !_emSegundoPlano) {
      final desde = _ultimaVerificacao;
      if (desde == null || agora().difference(desde) >= intervalo) {
        unawaited(verificar());
      }
      _relogio ??= Timer.periodic(intervalo, (_) => verificar());
    } else {
      _relogio?.cancel();
      _relogio = null;
    }
  }

  /// A VERSAO INSTALADA, do sistema — e nao do pubspec.
  ///
  /// O pubspec diz o que foi COMPILADO. Depois de uma atualizacao
  /// recusada pelo sistema (falta de espaco, instalacao cancelada) os
  /// dois discordam, e o app ofereceria para sempre uma versao que ele ja
  /// tentou instalar.
  Future<int> codigoInstalado() async {
    try {
      final r = await _canal.invokeMapMethod<String, Object?>('versao');
      return (r?['codigo'] as num?)?.toInt() ?? 0;
    } catch (_) {
      return 0;
    }
  }

  /// PERGUNTA AO SERVIDOR E DECIDE SE HA O QUE OFERECER.
  Future<void> verificar() async {
    if (!_suportado) return;
    _ultimaVerificacao = agora();
    try {
      final req = await _http.getUrl(Uri.parse('$endereco/versao'));
      final res = await req.close().timeout(const Duration(seconds: 10));
      if (res.statusCode != 200) return;
      final corpo = await res.transform(utf8.decoder).join();
      final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
      final versao = VersaoPublicada.deJson(m['versao']);

      final instalado = await codigoInstalado();
      if (instalado <= 0) return;  // sem canal: nao ha o que comparar

      if (versao == null || versao.codigo <= instalado) {
        // ATUALIZOU. A marca de adiada e a de vista morrem junto: a
        // proxima versao pode ser adiada e vista de novo, e uma marca
        // velha faria o aviso nao aparecer.
        if (oferecida.value != null) {
          oferecida.value = null;
          _arquivoBaixado = null;
          fase.value = FaseDaAtualizacao.parada;
          progresso.value = -1;
        }
        await _esquecerMarcas();
        return;
      }
      if (await _adiada(versao, instalado)) return;
      oferecida.value = versao;
    } catch (_) {
      // SEM REDE NAO HA ATUALIZACAO. Fica o que ja estava — e o erro nao
      // fica na tela por uma falha que a pessoa nao pode resolver.
    }
  }

  Future<bool> _adiada(VersaoPublicada v, int instalado) async {
    if (v.obrigatoria) return false;
    try {
      final prefs = await SharedPreferences.getInstance();
      // A marca vale para ESTA versao e ESTE aparelho: se o codigo
      // instalado mudou, a marca e de outro mundo.
      if (prefs.getInt(_chaveCodigoInstalado) != instalado) return false;
      final ate = prefs.getInt(_chaveAdiada) ?? 0;
      if (ate == 0) return false;
      return DateTime.now().millisecondsSinceEpoch < ate;
    } catch (_) {
      return false;
    }
  }

  Future<void> _esquecerMarcas() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove(_chaveAdiada);
      await prefs.remove(_chaveCodigoInstalado);
      await prefs.remove(_chaveVista);
    } catch (_) {}
  }

  /// "DEPOIS". Nao insiste por um dia — e nunca quando a versao e
  /// obrigatoria, porque ai nao ha escolha a respeitar.
  Future<void> adiar() async {
    final v = oferecida.value;
    if (v == null || v.obrigatoria) return;
    try {
      final prefs = await SharedPreferences.getInstance();
      final instalado = await codigoInstalado();
      await prefs.setInt(
        _chaveAdiada,
        DateTime.now().add(const Duration(hours: 24)).millisecondsSinceEpoch,
      );
      await prefs.setInt(_chaveCodigoInstalado, instalado);
    } catch (_) {}
    oferecida.value = null;
  }

  /// BAIXA O APK E ABRE O INSTALADOR.
  ///
  /// O ARQUIVO VAI PARA `cache/atualizacao/`, e nao para os Documentos: e
  /// a pasta que o provedor de arquivos expoe ao instalador, e o sistema
  /// pode recolher sozinho quando precisar de espaco — um APK ja
  /// instalado nao precisa mais existir.
  ///
  /// A CONFERENCIA DO SHA-256 NAO E ENFEITE. Um download cortado no meio
  /// da uma instalacao que falha no fim, depois de gastar a internet da
  /// pessoa; e um arquivo trocado no caminho seria um aplicativo trocado.
  /// Sem sha publicado, o tamanho declarado ainda barra o corte.
  Future<bool> baixarEInstalar() async {
    final v = oferecida.value;
    if (v == null || !_suportado) return false;
    if (fase.value == FaseDaAtualizacao.baixando ||
        fase.value == FaseDaAtualizacao.instalando) {
      return false;
    }
    erro.value = null;
    try {
      // O ARQUIVO JA BAIXADO E CONFERIDO SERVE. Quem fecha e reabre o app
      // no meio nao perde os noventa megabytes da primeira vez.
      final destino = _arquivoBaixado ?? await _baixar(v);
      if (destino == null) return false;
      fase.value = FaseDaAtualizacao.instalando;
      final r = await _canal.invokeMethod<String>('instalar', {
        'caminho': destino,
      });
      if (r == 'abriu') {
        fase.value = FaseDaAtualizacao.pronto;
        return true;
      }
      if (r == 'permissao') {
        erro.value = 'Falta deixar o Aurea instalar aplicativos. '
            'Ligue o ajuste e toque em Atualizar de novo.';
      } else {
        erro.value = r ?? 'nao deu para abrir o instalador';
      }
      fase.value = FaseDaAtualizacao.parada;
      return false;
    } catch (e) {
      erro.value = 'a atualizacao falhou: $e';
      fase.value = FaseDaAtualizacao.parada;
      progresso.value = -1;
      return false;
    }
  }

  Future<String?> _baixar(VersaoPublicada v) async {
    fase.value = FaseDaAtualizacao.baixando;
    progresso.value = v.tamanho > 0 ? 0 : -1;

    final pasta = await _pasta();
    if (!await pasta.exists()) await pasta.create(recursive: true);
    // O MESMO NOME SEMPRE: um arquivo por versao encheria o cache de
    // APKs de noventa megabytes que ninguem mais vai instalar.
    final destino = File('${pasta.path}/aurea.apk');
    final parcial = File('${destino.path}.parcial');

    final req = await _http.getUrl(Uri.parse(v.apk));
    final res = await req.close().timeout(const Duration(minutes: 5));
    if (res.statusCode != 200) {
      erro.value = 'o servidor respondeu ${res.statusCode}';
      fase.value = FaseDaAtualizacao.parada;
      return null;
    }
    final total = res.contentLength > 0 ? res.contentLength : v.tamanho;
    var recebidos = 0;
    final sink = parcial.openWrite();
    try {
      await for (final pedaco in res) {
        sink.add(pedaco);
        recebidos += pedaco.length;
        if (total > 0) progresso.value = recebidos / total;
      }
    } finally {
      await sink.close();
    }

    fase.value = FaseDaAtualizacao.conferindo;
    if (total > 0 && recebidos != total) {
      await parcial.delete();
      erro.value = 'o download veio cortado ($recebidos de $total bytes)';
      fase.value = FaseDaAtualizacao.parada;
      progresso.value = -1;
      return null;
    }
    if (v.sha256.isNotEmpty) {
      final bytes = await parcial.readAsBytes();
      final digest = sha256.convert(bytes).toString();
      if (digest != v.sha256) {
        await parcial.delete();
        erro.value = 'o arquivo baixado nao e o que o servidor publicou';
        fase.value = FaseDaAtualizacao.parada;
        progresso.value = -1;
        return null;
      }
    }
    if (await destino.exists()) await destino.delete();
    await parcial.rename(destino.path);
    _arquivoBaixado = destino.path;
    progresso.value = 1;
    return destino.path;
  }

  /// O ajuste do sistema que libera instalar aplicativo de fora da loja.
  /// Existe para a faixa poder mandar a pessoa direto para la.
  Future<void> abrirAjusteDoSistema() async {
    try {
      await _canal.invokeMethod<String>('instalar', {'caminho': ''});
    } catch (_) {}
  }

  /// A VERSAO OFERECIDA JA FOI MOSTRADA como janela. Uma vez por codigo.
  Future<bool> jaVista(int codigo) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      return prefs.getInt(_chaveVista) == codigo;
    } catch (_) {
      return false;
    }
  }

  Future<void> marcarVista(int codigo) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setInt(_chaveVista, codigo);
    } catch (_) {}
  }
}
