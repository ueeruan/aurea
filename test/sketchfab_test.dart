// O ACERVO DO SKETCHFAB DENTRO DO AUREA.
//
// Estes testes prendem o CONTRATO com a Data API v3 — o que o app manda e
// o que ele entende de volta — e as tres coisas que nao podem regredir sem
// causar dano: o token nunca ir parar numa URL, a busca continuar
// funcionando sem conta, e um download interrompido nao deixar para tras
// um arquivo com cara de pronto.
//
// Nenhum pedido de verdade sai daqui: o `HttpClient` e de brinquedo, como
// no resto da suite (o `TestWidgetsFlutterBinding` troca o do processo por
// um que devolve 400 e nao fala com ninguem).
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/application/sketchfab_service.dart';
import 'package:aurea/src/features/editor/presentation/sketchfab/sketchfab_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/importacao_3d.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

// ==========================================================================
// O SERVIDOR DE BRINQUEDO
// ==========================================================================

class _PedidoFeito {
  _PedidoFeito(this.url, this.cabecalhos);
  final Uri url;
  final Map<String, String> cabecalhos;
}

class _Http implements HttpClient {
  _Http(this.responder);

  /// Recebe a URL pedida e devolve a resposta que o teste quer.
  final _Resposta Function(Uri url) responder;

  final pedidos = <_PedidoFeito>[];

  @override
  Future<HttpClientRequest> getUrl(Uri url) async {
    final pedido = _Pedido(url, this);
    return pedido;
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Pedido implements HttpClientRequest {
  _Pedido(this.url, this.http);

  final Uri url;
  final _Http http;

  @override
  final _Cabecalhos headers = _Cabecalhos();

  @override
  Future<HttpClientResponse> close() async {
    http.pedidos.add(_PedidoFeito(url, Map.of(headers.valores)));
    return http.responder(url);
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Cabecalhos implements HttpHeaders {
  final valores = <String, String>{};

  @override
  void set(String name, Object value, {bool preserveHeaderCase = false}) {
    valores[name.toLowerCase()] = '$value';
  }

  @override
  String? value(String name) => valores[name.toLowerCase()];

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Resposta extends Stream<List<int>> implements HttpClientResponse {
  _Resposta.texto(this.statusCode, String corpo)
    : _pedacos = [utf8.encode(corpo)],
      _controlado = null,
      _tamanho = null;

  _Resposta.bytes(this.statusCode, List<List<int>> pedacos)
    : _pedacos = pedacos,
      _controlado = null,
      _tamanho = null;

  /// Resposta que o teste alimenta pedaco a pedaco (para cancelar no meio).
  _Resposta.torneira(
    this.statusCode,
    StreamController<List<int>> c,
    this._tamanho,
  ) : _pedacos = const [],
      _controlado = c;

  @override
  final int statusCode;

  final List<List<int>> _pedacos;
  final StreamController<List<int>>? _controlado;
  final int? _tamanho;

  @override
  int get contentLength =>
      _tamanho ?? _pedacos.fold(0, (a, p) => a + p.length);

  @override
  StreamSubscription<List<int>> listen(
    void Function(List<int>)? onData, {
    Function? onError,
    void Function()? onDone,
    bool? cancelOnError,
  }) {
    final fonte = _controlado?.stream ?? Stream.fromIterable(_pedacos);
    return fonte.listen(
      onData,
      onError: onError,
      onDone: onDone,
      cancelOnError: cancelOnError,
    );
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

// ==========================================================================
// AS RESPOSTAS DE VERDADE (recortadas de chamadas reais a API)
// ==========================================================================

const _uid = 'abc123def456';

String _buscaJson({String? next, String uid = _uid, bool miniaturas = true}) =>
    jsonEncode({
  'results': [
    {
      'uid': uid,
      'name': 'Castelo medieval',
      'user': {
        'username': 'tulio',
        'displayName': 'Túlio Modelador',
        'profileUrl': 'https://sketchfab.com/tulio',
      },
      'license': {'uid': 'by', 'label': 'CC Attribution'},
      'faceCount': 46261,
      'vertexCount': 25110,
      'animationCount': 2,
      'isDownloadable': true,
      'viewerUrl': 'https://sketchfab.com/3d-models/$uid',
      // As miniaturas ficam de fora dos testes de tela: `Image.network`
      // num teste tenta falar com a rede, e o que se mede aqui e a grade,
      // nao o carregador de imagem.
      if (miniaturas)
        'thumbnails': {
          'images': [
            {
              'url': 'https://media.sketchfab.com/g.jpeg',
              'width': 1024,
              'height': 576,
            },
            {
              'url': 'https://media.sketchfab.com/p.jpeg',
              'width': 256,
              'height': 144,
            },
          ],
        },
      'archives': {
        'glb': {
          'size': 59426816,
          'faceCount': 46261,
          'vertexCount': 25110,
          'textureCount': 12,
          'textureMaxResolution': 2048,
        },
        'gltf': {'size': 61000000, 'textureCount': 12},
      },
    },
    // Um resultado quebrado no meio da lista: o parse tolerante pula, e nao
    // derruba a pagina inteira.
    {'name': 'sem uid'},
  ],
  'next': ?next,
});

const _downloadJson =
    '{"gltf":{"url":"https://dl.sketchfab.com/pacote.zip?sig=x","size":1234,'
    '"expires":300},'
    '"glb":{"url":"https://dl.sketchfab.com/modelo.glb?sig=y","size":999,'
    '"expires":300}}';

void main() {
  late Directory tmp;

  setUp(() {
    tmp = Directory.systemTemp.createTempSync('aurea_sketchfab_test_');
  });

  tearDown(() {
    try {
      if (tmp.existsSync()) tmp.deleteSync(recursive: true);
    } catch (_) {
      // No Windows um arquivo recem-fechado pode continuar preso por um
      // instante; a pasta e temporaria e o sistema recolhe depois.
    }
  });

  // ------------------------------------------------------------------ busca

  test('a busca le uid, autor, licenca, ficha e miniaturas', () async {
    final http = _Http((_) => _Resposta.texto(200, _buscaJson()));
    final s = SketchfabService(http: http);

    final pagina = await s.buscar(termo: 'castelo');

    expect(pagina.itens, hasLength(1), reason: 'o item sem uid e descartado');
    final m = pagina.itens.single;
    expect(m.uid, _uid);
    expect(m.nome, 'Castelo medieval');
    expect(m.autor, 'Túlio Modelador');
    expect(m.autorUrl, 'https://sketchfab.com/tulio');
    expect(m.licenca, 'CC Attribution');
    expect(m.faces, 46261);
    expect(m.animacoes, 2);

    // As miniaturas saem ordenadas da menor para a maior, e a escolha e a
    // primeira que serve — pedir 512 nao pode devolver a de 1024 se houver
    // uma de 512, nem a de 256 se nao houver.
    expect(m.miniaturas.map((t) => t.largura), [256, 1024]);
    expect(m.miniatura(200), contains('p.jpeg'));
    expect(m.miniatura(512), contains('g.jpeg'));

    // GLB antes de glTF: um arquivo so em vez de um zip.
    expect(m.pacote?.formato, 'glb');
    expect(m.pacote?.texturas, 12);
    expect(m.pacote?.maiorTextura, 2048);

    // A ficha do aviso de modelo pesado sai da BUSCA, antes do download.
    expect(m.ficha.triangulos, 46261);
    expect(m.ficha.arquivoBytes, 59426816);
    expect(m.ficha.maiorTextura, 2048);

    // O pedido: busca de modelos baixaveis, com o termo.
    final url = http.pedidos.single.url;
    expect(url.path, endsWith('/search'));
    expect(url.queryParameters['type'], 'models');
    expect(url.queryParameters['downloadable'], 'true');
    expect(url.queryParameters['q'], 'castelo');
  });

  test('sem token a busca funciona e nao manda cabecalho de autorizacao',
      () async {
    final http = _Http((_) => _Resposta.texto(200, _buscaJson()));
    final s = SketchfabService(http: http);

    final pagina = await s.buscar(termo: 'arvore');

    expect(pagina.itens, hasLength(1));
    expect(http.pedidos.single.cabecalhos.containsKey('authorization'), isFalse);
  });

  test('a paginacao usa a URL que o servidor devolveu, tal como veio',
      () async {
    const proxima = 'https://api.sketchfab.com/v3/search?cursor=ABC123&count=24';
    final http = _Http(
      (u) => _Resposta.texto(
        200,
        u.toString() == proxima ? _buscaJson(uid: 'segunda') : _buscaJson(next: proxima),
      ),
    );
    final s = SketchfabService(http: http);

    final primeira = await s.buscar(termo: 'castelo');
    expect(primeira.proxima, proxima);

    final segunda = await s.buscar(proxima: primeira.proxima);
    expect(segunda.itens.single.uid, 'segunda');
    // A ultima pagina nao tem `next`: e assim que a rolagem sabe parar.
    expect(segunda.proxima, isNull);
    expect(http.pedidos.last.url.toString(), proxima);
  });

  test('o filtro "Leves" vira max_face_count', () async {
    final http = _Http((_) => _Resposta.texto(200, _buscaJson()));
    await SketchfabService(http: http).buscar(termo: 'x', maxFaces: 100000);
    expect(http.pedidos.single.url.queryParameters['max_face_count'], '100000');
  });

  // ------------------------------------------------------------------ token

  test('o token viaja no cabecalho e NUNCA na URL', () async {
    final http = _Http((_) => _Resposta.texto(200, _downloadJson));
    final s = SketchfabService(http: http);

    await s.linkDeDownload(_uid, token: 'segredo-do-dono');

    final p = http.pedidos.single;
    expect(p.cabecalhos['authorization'], 'Token segredo-do-dono');
    // A garantia que importa: a credencial nao entra em log de servidor,
    // historico nem relatorio de erro.
    expect(p.url.toString(), isNot(contains('segredo')));
    expect(p.url.path, endsWith('/models/$_uid/download'));
  });

  test('o /me confere o token e diz de quem ele e', () async {
    final http = _Http(
      (_) => _Resposta.texto(
        200,
        '{"username":"tulio","displayName":"Túlio Modelador"}',
      ),
    );
    final conta = await SketchfabService(http: http).eu('t');
    expect(conta.nome, 'Túlio Modelador');
    expect(conta.usuario, 'tulio');
  });

  test('401 pede reconectar; 429 pede esperar; 404 fala do modelo', () async {
    Future<SketchfabException> erroCom(int status) async {
      final s = SketchfabService(http: _Http((_) => _Resposta.texto(status, '{}')));
      try {
        await s.buscar(termo: 'x');
      } on SketchfabException catch (e) {
        return e;
      }
      fail('esperava SketchfabException para $status');
    }

    final e401 = await erroCom(401);
    expect(e401.status, 401);
    expect(e401.precisaDeToken, isTrue);
    expect(e401.mensagem, contains('Conecte a conta'));

    final e429 = await erroCom(429);
    expect(e429.precisaDeToken, isFalse);
    expect(e429.mensagem.toLowerCase(), contains('espere'));

    expect((await erroCom(404)).mensagem, contains('não está mais'));
  });

  test('sem rede a mensagem fala de conexao, e nao de HTTP', () async {
    final http = _Http((_) => throw const SocketException('sem rota'));
    final s = SketchfabService(http: http);
    await expectLater(
      s.buscar(termo: 'x'),
      throwsA(
        isA<SketchfabException>().having(
          (e) => e.mensagem,
          'mensagem',
          contains('Sem conexão'),
        ),
      ),
    );
  });

  // --------------------------------------------------------------- download

  test('o link prefere glb ao zip e guarda a extensao certa', () async {
    final http = _Http((_) => _Resposta.texto(200, _downloadJson));
    final link = await SketchfabService(http: http).linkDeDownload(
      _uid,
      token: 't',
    );
    expect(link.formato, 'glb');
    expect(link.extensao, '.glb');
    expect(link.expiraEm, 300);
  });

  test('so gltf: o pacote vem como zip, que o importador ja sabe abrir',
      () async {
    final http = _Http(
      (_) => _Resposta.texto(
        200,
        '{"gltf":{"url":"https://dl/pacote.zip","size":10,"expires":300}}',
      ),
    );
    final link = await SketchfabService(http: http).linkDeDownload(
      _uid,
      token: 't',
    );
    expect(link.formato, 'gltf');
    expect(link.extensao, '.zip');
  });

  test('baixar grava o arquivo e informa o progresso do inicio ao fim',
      () async {
    final http = _Http(
      (_) => _Resposta.bytes(200, [
        [1, 2, 3, 4, 5],
        [6, 7, 8, 9, 10],
      ]),
    );
    final s = SketchfabService(http: http);
    final destino = '${tmp.path}${Platform.pathSeparator}m.glb';
    final vistos = <double>[];

    final arquivo = await s.baixar(
      const LinkDeDownload(url: 'https://dl/m.glb', formato: 'glb', bytes: 10),
      destino,
      onProgresso: (r, t) => vistos.add(t > 0 ? r / t : -1),
    );

    expect(arquivo.existsSync(), isTrue);
    expect(arquivo.lengthSync(), 10);
    expect(vistos.first, 0);
    expect(vistos.last, 1.0);
    // O `.part` nao fica para tras.
    expect(File('$destino.part').existsSync(), isFalse);
  });

  test('o download NAO leva o token: o link ja vem assinado', () async {
    final http = _Http((_) => _Resposta.bytes(200, [
          [1, 2, 3],
        ]));
    await SketchfabService(http: http).baixar(
      const LinkDeDownload(url: 'https://dl/m.glb', formato: 'glb', bytes: 3),
      '${tmp.path}${Platform.pathSeparator}m.glb',
    );
    expect(http.pedidos.single.cabecalhos.containsKey('authorization'), isFalse);
  });

  test('cancelar no meio nao deixa arquivo nenhum para tras', () async {
    final torneira = StreamController<List<int>>();
    final http = _Http((_) => _Resposta.torneira(200, torneira, 100));
    final s = SketchfabService(http: http);
    final destino = '${tmp.path}${Platform.pathSeparator}m.glb';
    final cancelamento = Cancelamento();

    final futuro = s.baixar(
      const LinkDeDownload(url: 'https://dl/m.glb', formato: 'glb', bytes: 100),
      destino,
      cancelamento: cancelamento,
      // O dono desiste depois do primeiro pedaco.
      onProgresso: (r, _) {
        if (r > 0) cancelamento.cancelar();
      },
    );

    torneira.add(List.filled(30, 7));
    await Future<void>.delayed(Duration.zero);
    torneira.add(List.filled(30, 7));
    unawaited(torneira.close());

    await expectLater(futuro, throwsA(isA<DownloadCancelado>()));
    // NEM O PRONTO NEM O PEDACO: o importador so olha a extensao, e um
    // `.glb` cortado pela metade seria lido como se estivesse inteiro.
    expect(File(destino).existsSync(), isFalse);
    expect(File('$destino.part').existsSync(), isFalse);
  });

  test('download com erro HTTP vira frase de erro e apaga o pedaco', () async {
    final http = _Http((_) => _Resposta.texto(500, 'ops'));
    final destino = '${tmp.path}${Platform.pathSeparator}m.glb';
    await expectLater(
      SketchfabService(http: http).baixar(
        const LinkDeDownload(url: 'https://dl/m.glb', formato: 'glb'),
        destino,
      ),
      throwsA(isA<SketchfabException>()),
    );
    expect(File('$destino.part').existsSync(), isFalse);
  });

  // ---------------------------------------------------------------- credito

  test('o credito sai pronto no formato TASL, com a origem', () {
    final http = _Http((_) => _Resposta.texto(200, _buscaJson()));
    return SketchfabService(http: http).buscar(termo: 'x').then((pagina) {
      final c = pagina.itens.single.credito;
      expect(c.author, 'Túlio Modelador');
      expect(c.license, 'CC Attribution');
      expect(c.source, 'Sketchfab');
      expect(c.title, 'Castelo medieval');
      expect(c.url, contains(_uid));
      expect(
        c.porExtenso,
        '"Castelo medieval" (https://sketchfab.com/3d-models/$_uid) '
        'por Túlio Modelador, licença CC Attribution, via Sketchfab',
      );
    });
  });

  // ------------------------------------------------------------------ cofre

  test('o cofre de memoria guarda, le e apaga', () async {
    final cofre = CofreNaMemoria();
    expect(await cofre.ler(), isNull);
    await cofre.gravar('  abc  ');
    expect(await cofre.ler(), 'abc');
    // Gravar vazio e o mesmo que remover: um campo limpo nao pode deixar
    // uma credencial velha valendo em silencio.
    await cofre.gravar('   ');
    expect(await cofre.ler(), isNull);
  });

  // ------------------------------------------------------------------ etapas

  test('as etapas vao do download ate a camada, nesta ordem', () {
    // A ordem e o contrato entre quem baixa (a tela do Sketchfab, dona da
    // etapa `baixando`) e a porta unica da importacao, dona das outras.
    expect(EtapaDaImportacao3D.values, [
      EtapaDaImportacao3D.baixando,
      EtapaDaImportacao3D.lendo,
      EtapaDaImportacao3D.texturas,
      EtapaDaImportacao3D.importando,
      EtapaDaImportacao3D.finalizando,
    ]);
    for (final e in EtapaDaImportacao3D.values) {
      expect(e.rotulo, isNotEmpty);
    }
  });

  // ========================================================================
  // A TELA
  // ========================================================================

  Widget app(SketchfabService servico, {CofreDoToken? cofre}) => ProviderScope(
    overrides: [
      sketchfabServiceProvider.overrideWithValue(servico),
      cofreDoTokenProvider.overrideWithValue(cofre ?? CofreNaMemoria()),
    ],
    child: MaterialApp(
      home: TelaDoSketchfab(playhead: Duration.zero),
    ),
  );

  /// VOLTAS AO RELOGIO REAL ATE [pronto] VALER.
  ///
  /// O download grava num arquivo de verdade, e o relogio falso do teste
  /// nao adianta E/S: quem fecha e apaga o `.part` e o sistema, no tempo
  /// dele. Medido nesta maquina, o caminho do cancelamento leva ~90 ms no
  /// caso comum e ja foi visto em 770 ms quando o antivirus para para olhar
  /// o arquivo recem-criado — sempre com o MESMO numero de passos, so com o
  /// relogio mais lento.
  ///
  /// Por isso a espera nao pode ser um numero fixo de voltas: ele compra
  /// tempo no escuro e falha em toda maquina mais lenta que a media. Aqui a
  /// espera acaba assim que a condicao vale, e o teto existe so para o teste
  /// morrer com recado se ela nunca valer.
  Future<void> ateQue(
    WidgetTester tester,
    bool Function() pronto,
    String oQue,
  ) async {
    final relogio = Stopwatch()..start();
    while (!pronto()) {
      if (relogio.elapsed > const Duration(seconds: 10)) {
        fail('$oQue não aconteceu em 10 s');
      }
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 5)),
      );
      await tester.pump();
    }
  }

  testWidgets('a tela lista o que a busca devolveu e diz de onde vem', (
    tester,
  ) async {
    final http = _Http(
      (_) => _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http)));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('sketchfab-cartao-$_uid')), findsOneWidget);
    expect(find.text('Castelo medieval'), findsOneWidget);
    expect(find.text('Túlio Modelador'), findsOneWidget);
    expect(find.text('CC Attribution'), findsWidgets);
    // Exigencia das diretrizes do Sketchfab.
    expect(find.text('Modelos 3D fornecidos por Sketchfab'), findsOneWidget);
  });

  testWidgets('digitar so busca depois da pausa, e uma vez so', (tester) async {
    final http = _Http(
      (_) => _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http)));
    await tester.pumpAndSettle();
    final abertura = http.pedidos.length;

    final campo = find.byKey(const ValueKey('sketchfab-busca'));
    await tester.enterText(campo, 'cas');
    await tester.pump(const Duration(milliseconds: 150));
    await tester.enterText(campo, 'caste');
    await tester.pump(const Duration(milliseconds: 150));
    await tester.enterText(campo, 'castelo');
    // Ainda nada: a pausa de 400 ms nunca se completou.
    expect(http.pedidos.length, abertura);

    await tester.pump(const Duration(milliseconds: 450));
    await tester.pumpAndSettle();
    expect(http.pedidos.length, abertura + 1);
    expect(http.pedidos.last.url.queryParameters['q'], 'castelo');
  });

  testWidgets('sem token o botao de baixar vira "Conectar conta"', (
    tester,
  ) async {
    final http = _Http(
      (_) => _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http)));
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('sketchfab-cartao-$_uid')));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('sketchfab-detalhe')), findsOneWidget);
    expect(find.text('Conectar conta'), findsWidgets);
    expect(find.text('Baixar e importar'), findsNothing);
  });

  testWidgets('o detalhe mostra a atribuicao e copia a linha TASL', (
    tester,
  ) async {
    // O que foi parar na area de transferencia; o canal de plataforma nao
    // existe no teste, entao ele e atendido aqui.
    Object? copiado;
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (chamada) async {
        if (chamada.method == 'Clipboard.setData') {
          copiado = (chamada.arguments as Map)['text'];
        }
        return null;
      },
    );
    addTearDown(
      () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      ),
    );

    final http = _Http(
      (_) => _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http)));
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('sketchfab-cartao-$_uid')));
    await tester.pumpAndSettle();

    final linha = tester
        .widget<Text>(find.byKey(const ValueKey('sketchfab-creditos')))
        .data!;
    expect(linha, contains('Castelo medieval'));
    expect(linha, contains('Túlio Modelador'));
    expect(linha, contains('CC Attribution'));
    expect(linha, contains('Sketchfab'));

    await tester.tap(find.byKey(const ValueKey('sketchfab-copiar-creditos')));
    await tester.pumpAndSettle();
    expect(copiado, linha);
    // O aviso de "copiado" tem timer proprio: deixa-lo fechar.
    await tester.pump(const Duration(seconds: 5));
  });

  testWidgets('com token o botao baixa, e o dialogo do token grava no cofre', (
    tester,
  ) async {
    final cofre = CofreNaMemoria();
    final http = _Http(
      (u) => u.path.endsWith('/me')
          ? _Resposta.texto(200, '{"username":"tulio","displayName":"Túlio"}')
          : _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http), cofre: cofre));
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('sketchfab-conta')));
    await tester.pumpAndSettle();
    await tester.enterText(
      find.byKey(const ValueKey('sketchfab-token-campo')),
      'meu-token',
    );
    await tester.tap(find.byKey(const ValueKey('sketchfab-token-testar')));
    await tester.pumpAndSettle();

    expect(await cofre.ler(), 'meu-token');
    // E o token foi conferido contra a API antes de ser guardado.
    expect(http.pedidos.any((p) => p.url.path.endsWith('/me')), isTrue);

    await tester.tap(find.byKey(const ValueKey('sketchfab-cartao-$_uid')));
    await tester.pumpAndSettle();
    expect(find.text('Baixar e importar'), findsWidgets);
    await tester.tap(find.byKey(const ValueKey('sketchfab-detalhe-fechar')));
    await tester.pumpAndSettle();
    // O aviso de "conta conectada" tem um Timer proprio; deixa-lo fechar
    // e o que impede o teste de terminar com um timer pendente.
    await tester.pump(const Duration(seconds: 5));
  });

  testWidgets('a tela mostra o erro da busca e deixa tentar de novo', (
    tester,
  ) async {
    var falhar = true;
    final http = _Http(
      (_) => falhar
          ? _Resposta.texto(429, '{}')
          : _Resposta.texto(200, _buscaJson(miniaturas: false)),
    );
    await tester.pumpWidget(app(SketchfabService(http: http)));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('sketchfab-tentar')), findsOneWidget);
    falhar = false;
    await tester.tap(find.byKey(const ValueKey('sketchfab-tentar')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('sketchfab-cartao-$_uid')), findsOneWidget);
  });

  testWidgets('baixando, a tela mostra a etapa e o cancelar funciona', (
    tester,
  ) async {
    final torneira = StreamController<List<int>>();
    final http = _Http((u) {
      if (u.path.endsWith('/download')) {
        return _Resposta.texto(200, _downloadJson);
      }
      if (u.host == 'dl.sketchfab.com') {
        return _Resposta.torneira(200, torneira, 1000);
      }
      return _Resposta.texto(200, _buscaJson(miniaturas: false));
    });
    await tester.pumpWidget(
      app(
        SketchfabService(http: http, pastaDeDownload: () async => tmp),
        cofre: CofreNaMemoria('t'),
      ),
    );
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('sketchfab-cartao-$_uid')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('sketchfab-baixar')));
    await tester.pumpAndSettle();

    torneira.add(List.filled(200, 3));
    // O progresso e contado no mesmo instante em que o pedaco chega, entao
    // aqui basta o relogio falso andar ate a tela se redesenhar.
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('sketchfab-etapas')), findsOneWidget);
    expect(find.text('Baixando…'), findsOneWidget);
    expect(find.text('20%'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('sketchfab-cancelar')));
    // O pedido de parar so e visto no proximo pedaco que chega — e quem
    // esta baixando nao pode ser interrompido no meio de uma escrita.
    torneira.add(List.filled(200, 3));
    unawaited(torneira.close());
    // Fechar o arquivo e apagar o pedaco e E/S de verdade: ate o sistema
    // devolver o arquivo fechado, o `finally` da importacao nao corre.
    await ateQue(
      tester,
      () => find.byKey(const ValueKey('sketchfab-etapas')).evaluate().isEmpty,
      'o painel de etapas sumir depois do cancelamento',
    );

    // O painel some e a tela volta ao acervo, sem camada nenhuma criada.
    expect(find.byKey(const ValueKey('sketchfab-etapas')), findsNothing);
    expect(find.byKey(const ValueKey('sketchfab-cartao-$_uid')), findsOneWidget);
    // E nada sobrou no cache: nem o pacote pronto nem o pedaco.
    expect(tmp.listSync(), isEmpty);
  });
}
