// ATUALIZAR DENTRO DO PROPRIO APLICATIVO (Android).
//
// O servidor do mural guarda qual e a ultima versao e onde o arquivo
// esta; o app pergunta e, quando a de la e mais nova, mostra a faixa com
// o botao. Estes testes prendem o que decide: QUANDO oferecer, o que
// aceitar do servidor, e o que NAO pode acontecer (oferecer uma versao
// mais velha, insistir depois de um "depois", ou instalar um arquivo que
// nao e o publicado).
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/core/atualizacao/atualizacao_service.dart';
import 'package:crypto/crypto.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// UM SERVIDOR DE BRINQUEDO, sem soquete.
///
/// O `TestWidgetsFlutterBinding` troca o `HttpClient` do processo por um
/// que devolve 400 e nao fala com ninguem, entao um servidor de verdade
/// em `127.0.0.1` nao serve: o servico nunca o alcanca. O caminho e o
/// mesmo que o resto da suite usa — um `HttpClient` proprio.
class _Servidor implements HttpClient {
  int codigo = 91;
  String nome = '1.1.6-beta';
  String notas = 'O texto arabe voltou a ligar.';
  bool obrigatoria = false;
  String corpoDoApk = 'APK-DE-BRINQUEDO';
  String sha = '';
  bool cortar = false;

  /// O endereco do APK, que aponta de volta para ca.
  String apk = 'https://exemplo/aurea.apk';

  String? _ultimoCaminho;

  /// O que o app pediu, na ordem.
  final pedidos = <String>[];

  List<int> _bytesDaVersao() => utf8.encode(
    jsonEncode({
      'versao': {
        'codigo': codigo,
        'versao': nome,
        'apk': apk,
        'notas': notas,
        'obrigatoria': obrigatoria,
        'tamanho': utf8.encode(corpoDoApk).length,
        'sha256': sha,
      },
    }),
  );

  @override
  Future<HttpClientRequest> getUrl(Uri url) async {
    pedidos.add(url.path.isEmpty ? url.toString() : url.path);
    _ultimoCaminho = url.path;
    return _Pedido(this);
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Pedido implements HttpClientRequest {
  _Pedido(this.servidor);
  final _Servidor servidor;

  @override
  final HttpHeaders headers = _Cabecalhos();

  @override
  Future<HttpClientResponse> close() async =>
      _Resposta(servidor, servidor._ultimoCaminho ?? '/versao');

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Cabecalhos implements HttpHeaders {
  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Resposta extends Stream<List<int>> implements HttpClientResponse {
  _Resposta(this.servidor, this.caminho);

  final _Servidor servidor;
  final String caminho;

  List<int> get _bytes => caminho == '/versao'
      ? servidor._bytesDaVersao()
      : utf8.encode(servidor.corpoDoApk);

  @override
  int get statusCode => 200;

  @override
  int get contentLength {
    final total = _bytes.length;
    // O CORTE ACONTECE AQUI, e nao no corpo: e assim que um download
    // interrompido se parece — o tamanho declarado chega inteiro e os
    // bytes, nao.
    return total;
  }

  @override
  StreamSubscription<List<int>> listen(
    void Function(List<int>)? onData, {
    Function? onError,
    void Function()? onDone,
    bool? cancelOnError,
  }) {
    var bytes = _bytes;
    if (caminho != '/versao' && servidor.cortar && bytes.length > 3) {
      bytes = bytes.sublist(0, bytes.length - 3);
    }
    return Stream.value(bytes).listen(
      onData,
      onError: onError,
      onDone: onDone,
      cancelOnError: cancelOnError,
    );
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

/// Uma pasta de verdade, mas fora do `path_provider` — que nao existe
/// no teste.
late Directory _pastaDoTeste;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(() {
    SharedPreferences.setMockInitialValues({});
    _pastaDoTeste = Directory.systemTemp.createTempSync('aurea-atualizacao');
  });

  tearDown(() {
    if (_pastaDoTeste.existsSync()) _pastaDoTeste.deleteSync(recursive: true);
  });

  /// Um canal que diz qual versao esta instalada e anota o que foi pedido.
  MethodChannel canalDeTeste(int instalado, {String resposta = 'abriu'}) {
    final chamadas = <String>[];
    final canal = MethodChannel('aurea/atualizacao-teste');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(canal, (call) async {
      chamadas.add(call.method);
      return switch (call.method) {
        'versao' => {'codigo': instalado, 'nome': '1.1.0-beta'},
        'podeInstalar' => true,
        'instalar' => resposta,
        _ => null,
      };
    });
    canalDeTesteUltimas = chamadas;
    return canal;
  }

  group('o que o servidor manda', () {
    test('uma versao valida e aceita', () {
      final v = VersaoPublicada.deJson({
        'codigo': 92,
        'versao': '1.1.7-beta',
        'apk': 'https://exemplo/aurea.apk',
        'notas': 'oi',
      });
      expect(v, isNotNull);
      expect(v!.codigo, 92);
      expect(v.obrigatoria, isFalse);
    });

    test('sem codigo, sem nome ou sem https NAO e uma versao', () {
      for (final ruim in [
        {'codigo': 0, 'versao': 'a', 'apk': 'https://x/y'},
        {'codigo': 92, 'versao': '', 'apk': 'https://x/y'},
        // Endereco http: recusado mesmo vindo do servidor. O arquivo que
        // se instala e o aplicativo inteiro.
        {'codigo': 92, 'versao': 'a', 'apk': 'http://x/y'},
        {'codigo': 92, 'versao': 'a'},
        'lixo',
        null,
      ]) {
        expect(VersaoPublicada.deJson(ruim), isNull, reason: '$ruim');
      }
    });
  });

  group('quando oferecer', () {
    late _Servidor servidor;

    setUp(() => servidor = _Servidor());

    AtualizacaoService servico(int instalado) =>
        AtualizacaoService(
          canal: canalDeTeste(instalado),
          http: servidor,
          soAndroid: true,
          pastaDeDownload: () async => _pastaDoTeste,
        );

    test('versao mais nova: oferece', () async {
      final s = servico(90);
      await s.verificar();
      expect(s.oferecida.value?.codigo, 91);
      expect(s.oferecida.value?.nome, '1.1.6-beta');
    });

    test('a MESMA versao: nao oferece', () async {
      final s = servico(91);
      await s.verificar();
      expect(s.oferecida.value, isNull);
    });

    test('versao MAIS VELHA: nao oferece', () async {
      // Um servidor com a versao antiga no ar nao pode fazer o app
      // "atualizar" para tras.
      final s = servico(120);
      await s.verificar();
      expect(s.oferecida.value, isNull);
    });

    test('"depois" cala por um dia — mas nunca quando e obrigatoria', () async {
      final s = servico(90);
      await s.verificar();
      expect(s.oferecida.value, isNotNull);
      await s.adiar();
      expect(s.oferecida.value, isNull);

      final deNovo = servico(90);
      await deNovo.verificar();
      expect(deNovo.oferecida.value, isNull, reason: 'calou por um dia');

      servidor.obrigatoria = true;
      final obrigada = servico(90);
      await obrigada.verificar();
      expect(
        obrigada.oferecida.value,
        isNotNull,
        reason: 'a versao obrigatoria nao respeita o "depois"',
      );
    });
  });

  group('baixar e instalar', () {
    late _Servidor servidor;

    setUp(() => servidor = _Servidor());

    test('baixa, confere o sha e abre o instalador', () async {
      final esperado = sha256De(servidor.corpoDoApk);
      servidor.sha = esperado;
      final s = AtualizacaoService(
        canal: canalDeTeste(90),
        http: servidor,
        soAndroid: true,
        pastaDeDownload: () async => _pastaDoTeste,
      );
      await s.verificar();
      expect(await s.baixarEInstalar(), isTrue);
      expect(s.fase.value, FaseDaAtualizacao.pronto);
      expect(s.erro.value, isNull);
      expect(canalDeTesteUltimas, contains('instalar'));
    });

    test('um sh diferente NAO instala', () async {
      // O arquivo que se instala e o aplicativo inteiro. Um download
      // trocado no caminho e um aplicativo trocado.
      servidor.sha = 'a' * 64;
      final s = AtualizacaoService(
        canal: canalDeTeste(90),
        http: servidor,
        soAndroid: true,
        pastaDeDownload: () async => _pastaDoTeste,
      );
      await s.verificar();
      expect(await s.baixarEInstalar(), isFalse);
      expect(s.erro.value, isNotNull);
      expect(
        canalDeTesteUltimas,
        isNot(contains('instalar')),
        reason: 'nao pode abrir o instalador com o arquivo errado',
      );
    });

    test('download cortado e recusado pelo tamanho declarado', () async {
      servidor.cortar = true;
      final s = AtualizacaoService(
        canal: canalDeTeste(90),
        http: servidor,
        soAndroid: true,
        pastaDeDownload: () async => _pastaDoTeste,
      );
      await s.verificar();
      expect(await s.baixarEInstalar(), isFalse);
      expect(s.erro.value, contains('cortado'));
    });

    test('faltando o ajuste do sistema, a faixa explica', () async {
      final s = AtualizacaoService(
        canal: canalDeTeste(90, resposta: 'permissao'),
        http: servidor,
        soAndroid: true,
        pastaDeDownload: () async => _pastaDoTeste,
      );
      await s.verificar();
      expect(await s.baixarEInstalar(), isFalse);
      expect(s.erro.value, contains('instalar aplicativos'));
    });

    test('sem versao oferecida, baixar nao faz nada', () async {
      final s = AtualizacaoService(
        canal: canalDeTeste(90),
        http: servidor,
        soAndroid: true,
        pastaDeDownload: () async => _pastaDoTeste,
      );
      expect(await s.baixarEInstalar(), isFalse);
      expect(canalDeTesteUltimas, isNot(contains('instalar')));
    });
  });
}

/// As ultimas chamadas feitas no canal, para o teste conferir.
List<String> canalDeTesteUltimas = <String>[];

/// O MESMO pacote de criptografia que o servico usa, e nao uma segunda
/// implementacao que poderia discordar dele.
String sha256De(String texto) => sha256.convert(utf8.encode(texto)).toString();
