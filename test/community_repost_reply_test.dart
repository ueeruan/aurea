import 'dart:convert';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/community/application/comunidade_service.dart';
import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:aurea/src/features/community/domain/post_da_comunidade.dart';
import 'package:aurea/src/features/community/presentation/community_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// O QUE ESTE TESTE PROVA: que responder e repostar chegam ao servidor
/// com o post certo pendurado.
///
/// O erro que ele pega e o silencioso: uma resposta publicada como post
/// solto. Na tela as duas coisas sao iguais — um cartao com texto — e so
/// o `respondeA` na requisicao diz qual das duas foi.
class _Mural extends ComunidadeService {
  @override
  Future<int?> totalDeUsuarios() async => 2;
  _Mural(this.feed);

  static final codigo = 'cd' * 24;
  final List<PostDaComunidade> feed;

  String? respondeA;
  String? repostaDe;
  PostDaComunidade? enviado;
  final List<PostDaComunidade> guardados = [];

  @override
  Future<List<PostDaComunidade>> carregar({bool daRede = true}) async => feed;

  @override
  Future<void> publicar(PostDaComunidade post) async => guardados.add(post);

  @override
  Future<void> marcarEnviado(String id) async {}

  @override
  Future<List<PostDaComunidade>> respostas(String postId) async => const [];

  @override
  Future<String?> enviar(
    PostDaComunidade post,
    String codigo, {
    String? respondeA,
    String? repostaDe,
  }) async {
    enviado = post;
    this.respondeA = respondeA;
    this.repostaDe = repostaDe;
    return null;
  }
}

final _doOutro = PostDaComunidade(
  id: 'post-do-outro',
  autor: 'Bruno 3D',
  autorId: 'conta-do-bruno',
  texto: 'Terminei a vinheta com o rastreio de camera.',
  quando: DateTime(2026, 9, 7, 10),
);

Future<ProviderContainer> _abrir(WidgetTester tester, _Mural mural) async {
  tester.view.physicalSize = const Size(430, 932);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues({
    'comunidade.conta': jsonEncode(
      ContaDaComunidade(
        id: 'conta-da-ana',
        apelido: 'Ana Motion',
        codigo: _Mural.codigo,
        criadaEm: DateTime(2026),
      ).toJson(),
    ),
  });
  final prefs = await SharedPreferences.getInstance();
  final c = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      comunidadeServiceProvider.overrideWithValue(mural),
    ],
  );
  addTearDown(c.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: Scaffold(body: CommunityTab())),
    ),
  );
  await tester.pumpAndSettle();
  return c;
}

void main() {
  testWidgets('repostar sem comentario chega com repostaDe', (tester) async {
    final mural = _Mural([_doOutro]);
    await _abrir(tester, mural);

    expect(find.byKey(const ValueKey('post-repostar-post-do-outro')), findsOne);
    await tester.tap(find.byKey(const ValueKey('post-repostar-post-do-outro')));
    await tester.pumpAndSettle();

    // A folha abriu citando o original.
    expect(find.byKey(const ValueKey('citacao-post-do-outro')), findsOne);

    // TEXTO VAZIO E O CASO NORMAL do repost: obrigar a escrever alguma
    // coisa faria todo mundo digitar um ponto.
    await tester.tap(find.byKey(const ValueKey('comunidade-guardar')));
    await tester.pumpAndSettle();
    // O aviso de "no mural" segura um timer proprio; sem deixa-lo
    // terminar, o teste falha depois de ja ter provado o que queria.
    await tester.pump(const Duration(seconds: 8));

    expect(mural.repostaDe, 'post-do-outro');
    expect(mural.respondeA, isNull);
    expect(mural.enviado?.texto, '');
    // O autor sai da conta deste aparelho, e nao do post citado.
    expect(mural.enviado?.autorId, 'conta-da-ana');
  });

  testWidgets('responder chega com respondeA', (tester) async {
    final mural = _Mural([_doOutro]);
    await _abrir(tester, mural);

    await tester.tap(
      find.byKey(const ValueKey('post-responder-post-do-outro')),
    );
    await tester.pumpAndSettle();

    await tester.enterText(
      find.byKey(const ValueKey('comunidade-texto')),
      'Ficou muito bom. Qual foi a distancia focal?',
    );
    await tester.tap(find.byKey(const ValueKey('comunidade-guardar')));
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 8));

    expect(mural.respondeA, 'post-do-outro');
    expect(mural.repostaDe, isNull);
  });

  testWidgets('o cartao do repost mostra o post citado', (tester) async {
    final repost = PostDaComunidade(
      id: 'o-repost',
      autor: 'Ana Motion',
      autorId: 'conta-da-ana',
      texto: '',
      quando: DateTime(2026, 9, 7, 11),
      repostaDe: _doOutro.id,
      original: _doOutro,
    );
    final mural = _Mural([repost]);
    await _abrir(tester, mural);

    expect(find.byKey(const ValueKey('citacao-post-do-outro')), findsOne);
    expect(find.text('Repostou'), findsOne);
    // Nao da para repostar um repost: o servidor recusa, entao o botao
    // aponta para o original — mas o cartao continua sendo o do repost.
    expect(find.byKey(const ValueKey('post-repostar-o-repost')), findsOne);
  });

  testWidgets('meu post ainda nao enviado nao oferece responder nem repostar', (
    tester,
  ) async {
    final rascunho = PostDaComunidade(
      id: 'meu-rascunho',
      autor: 'Ana Motion',
      texto: 'Ainda vai subir.',
      quando: DateTime(2026, 9, 7, 12),
      estado: EstadoDoPost.rascunho,
    );
    final mural = _Mural([rascunho]);
    await _abrir(tester, mural);

    // O post nao existe no servidor: responder a ele daria 404, e
    // mostrar o botao seria prometer uma coisa que nao acontece.
    expect(
      find.byKey(const ValueKey('post-responder-meu-rascunho')),
      findsNothing,
    );
    expect(
      find.byKey(const ValueKey('post-repostar-meu-rascunho')),
      findsNothing,
    );
    expect(find.byKey(const ValueKey('post-enviar-meu-rascunho')), findsOne);
  });
}
