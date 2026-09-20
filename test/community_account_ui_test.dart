import 'dart:convert';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/community/application/comunidade_service.dart';
import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:aurea/src/features/community/application/social_service.dart';
import 'package:aurea/src/features/community/domain/post_da_comunidade.dart';
import 'package:aurea/src/features/community/presentation/community_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _Social extends SocialService {
  _Social() : super('https://test.local');
  @override
  Future<Map<String, dynamic>> request(
    String path,
    String code, {
    String method = 'GET',
    Map<String, dynamic>? data,
  }) async => path.startsWith('/social/feed')
      ? {'posts': []}
      : {
          'id': 'account-id',
          'apelido': 'ana_motion',
          'nome': 'Ana Motion',
          'bio': '',
          'verificado': false,
          'seguidores': 0,
          'seguindo': 0,
        };
}

class _Mural extends ComunidadeService {
  static final code = 'ab' * 24;
  String? sentCode;
  PostDaComunidade? sentPost;
  @override
  Future<int?> totalDeUsuarios() async => 27;
  @override
  Future<List<PostDaComunidade>> carregar({bool daRede = true}) async => [];
  @override
  Future<void> publicar(PostDaComunidade post) async {}
  @override
  Future<void> marcarEnviado(String id) async {}
  @override
  Future<RespostaDaConta> entrarComCodigo(String codigo) async => codigo == code
      ? const RespostaDaConta(id: 'account-id', apelido: 'Ana Motion')
      : RespostaDaConta.falha('Código inválido');
  @override
  Future<String?> enviar(
    PostDaComunidade post,
    String codigo, {
    String? respondeA,
    String? repostaDe,
  }) async {
    sentCode = codigo;
    sentPost = post;
    return null;
  }
}

void main() {
  testWidgets(
    'account code can be copied and used to return; posting uses credential',
    (tester) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      SharedPreferences.setMockInitialValues({
        'comunidade.conta': jsonEncode(
          ContaDaComunidade(
            id: 'account-id',
            apelido: 'Ana Motion',
            codigo: _Mural.code,
            criadaEm: DateTime(2026),
          ).toJson(),
        ),
      });
      final prefs = await SharedPreferences.getInstance();
      final service = _Mural();
      final c = ProviderContainer(
        overrides: [
          sharedPreferencesProvider.overrideWithValue(prefs),
          comunidadeServiceProvider.overrideWithValue(service),
          socialServiceProvider.overrideWithValue(_Social()),
        ],
      );
      addTearDown(c.dispose);
      String? clipboard;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            clipboard = (call.arguments as Map)['text'] as String;
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
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: const MaterialApp(home: Scaffold(body: CommunityTab())),
        ),
      );
      await tester.pumpAndSettle();
      expect(find.text('27 usuários cadastrados'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('comunidade-conta')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('perfil-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Codigo de acesso'));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('conta-copiar-codigo')));
      await tester.pumpAndSettle();
      expect(clipboard, _Mural.code);
      expect(find.text('Apagar conta'), findsNothing);
      await tester.tap(find.byKey(const ValueKey('perfil-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('conta-sair')));
      await tester.pumpAndSettle();
      expect(c.read(contaDaComunidadeProvider), isNull);
      await tester.tap(find.byKey(const ValueKey('comunidade-conta')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('conta-alternar-entrada')));
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const ValueKey('conta-codigo')),
        _Mural.code,
      );
      await tester.tap(find.byKey(const ValueKey('conta-salvar')));
      await tester.pumpAndSettle();
      expect(c.read(contaDaComunidadeProvider)?.id, 'account-id');
      await tester.tap(find.byKey(const ValueKey('comunidade-publicar')));
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const ValueKey('comunidade-texto')),
        'Minha animação de teste',
      );
      await tester.ensureVisible(
        find.byKey(const ValueKey('comunidade-guardar')),
      );
      await tester.tap(find.byKey(const ValueKey('comunidade-guardar')));
      await tester.pumpAndSettle();
      expect(service.sentCode, _Mural.code);
      expect(service.sentPost?.texto, 'Minha animação de teste');
      await tester.pump(const Duration(seconds: 8));
      expect(tester.takeException(), isNull);
    },
  );
}
