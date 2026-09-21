import 'dart:io';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/publicador_na_galeria.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// "EU EXPORTO MEU VIDEO E ELE NAO APARECE NA GALERIA."
///
/// O relato tinha duas causas, e as duas estao presas aqui:
///
///   1. O arquivo final nascia na pasta PRIVADA do app. A publicacao no
///      `MediaStore` era um efeito colateral opcional, sem resposta — e
///      o CORTE PURO, que e o caminho mais comum (cortar um clipe e
///      exportar), nem passava por ela.
///   2. A tela dizia "Video pronto" antes de saber se o registro existia.
///      Sucesso declarado sem prova nenhuma e exatamente o que faz a
///      pessoa procurar o video onde ele nunca esteve.
///
/// A regra que estes testes prendem: SO HA SUCESSO COM A URI NA MAO.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory temp;
  late List<MethodCall> chamadasDaGaleria;

  /// O que o canal falso da galeria vai responder.
  late Object? Function(MethodCall) respostaDaGaleria;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('aurea_galeria');
    chamadasDaGaleria = [];
    respostaDaGaleria = (call) => null;
    GaleriaDoAparelho.plataforma = PlataformaDaGaleria.android;

    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(GaleriaDoAparelho.canal, (call) async {
          chamadasDaGaleria.add(call);
          return respostaDaGaleria(call);
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(GaleriaDoAparelho.canal, null);
    GaleriaDoAparelho.restaurarPlataforma();
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  });

  File video({int bytes = 4096, String nome = 'aurea_p_30f.mp4'}) {
    final f = File('${temp.path}/$nome');
    f.writeAsBytesSync(List.filled(bytes, 7));
    return f;
  }

  group('a porta da galeria', () {
    test('o registro leva caminho, nome, mime e album', () async {
      final f = video();
      respostaDaGaleria = (_) => {
        'uri': 'content://media/external/video/media/42',
        'bytes': 4096,
        'nome': 'aurea_p_30f.mp4',
        'caminho': 'Movies/Aurea/aurea_p_30f.mp4',
      };

      final r = await GaleriaDoAparelho.publicarVideo(f);

      expect(chamadasDaGaleria.single.method, 'publicarVideo');
      final args = chamadasDaGaleria.single.arguments as Map;
      expect(args['caminho'], f.path);
      expect(args['nome'], 'aurea_p_30f.mp4');
      expect(args['mime'], 'video/mp4');
      expect(args['album'], 'Aurea');

      expect(r.ok, isTrue);
      expect(r.uri, 'content://media/external/video/media/42');
      expect(r.bytes, 4096);
      expect(r.ondeEsta, 'Movies/Aurea/aurea_p_30f.mp4');
      expect(r.podeAbrir, isTrue);
    });

    test('sem URI de volta NAO e sucesso', () async {
      respostaDaGaleria = (_) => {'bytes': 4096};
      final r = await GaleriaDoAparelho.publicarVideo(video());
      expect(r.ok, isFalse);
      expect(r.podeAbrir, isFalse);
      expect(r.mensagem, contains('nao devolveu o registro'));
      // O arquivo continua existindo, e a tela precisa dizer onde.
      expect(r.ondeEsta, isNotNull);
    });

    test('URI vazia tambem nao e sucesso', () async {
      respostaDaGaleria = (_) => {'uri': '', 'bytes': 4096};
      expect((await GaleriaDoAparelho.publicarVideo(video())).ok, isFalse);
    });

    test('registro de 0 byte e falha, nao sucesso', () async {
      respostaDaGaleria = (_) => {
        'uri': 'content://media/external/video/media/7',
        'bytes': 0,
      };
      final r = await GaleriaDoAparelho.publicarVideo(video());
      expect(r.ok, isFalse);
      expect(r.mensagem, contains('0 bytes'));
    });

    test('arquivo vazio vira erro ANTES de incomodar a galeria', () async {
      final f = video(bytes: 0);
      expect(GaleriaDoAparelho.conferir(f), contains('0 bytes'));

      final r = await GaleriaDoAparelho.publicarVideo(f);
      expect(r.ok, isFalse);
      expect(r.mensagem, contains('0 bytes'));
      expect(
        chamadasDaGaleria,
        isEmpty,
        reason: 'nao ha por que registrar um arquivo que nao tem video',
      );
    });

    test('arquivo que nem existe e erro com nome proprio', () async {
      final r = await GaleriaDoAparelho.publicarVideo(
        File('${temp.path}/nunca_existiu.mp4'),
      );
      expect(r.ok, isFalse);
      expect(r.mensagem, contains('nao foi criado'));
      expect(chamadasDaGaleria, isEmpty);
    });

    test('canal ausente e falha honesta, nao sucesso silencioso', () async {
      respostaDaGaleria = (_) => throw MissingPluginException('sem canal');
      final r = await GaleriaDoAparelho.publicarVideo(video());
      expect(r.ok, isFalse);
      expect(r.mensagem, contains('nao respondeu'));
    });

    test('erro da plataforma chega ao usuario com o motivo', () async {
      respostaDaGaleria = (_) => throw PlatformException(
        code: 'galeria',
        message: 'sem permissao para escrever na galeria',
      );
      final r = await GaleriaDoAparelho.publicarVideo(video());
      expect(r.ok, isFalse);
      expect(r.mensagem, contains('sem permissao'));
    });

    test('abrir e compartilhar levam a URI ao aparelho', () async {
      respostaDaGaleria = (_) => true;
      expect(await GaleriaDoAparelho.abrir('content://x/1'), isTrue);
      expect(await GaleriaDoAparelho.compartilhar('content://x/1'), isTrue);
      expect(
        chamadasDaGaleria.map((c) => c.method),
        ['abrir', 'compartilhar'],
      );
      expect(chamadasDaGaleria.first.arguments['uri'], 'content://x/1');
      expect(chamadasDaGaleria.last.arguments['mime'], 'video/mp4');
    });
  });

  group('a tela do fim', () {
    late List<String> encoder;
    var cenario = 0;

    setUp(() {
      encoder = [];
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            const MethodChannel('plugins.flutter.io/path_provider'),
            (call) async => temp.path,
          );
      // O corte puro: o unico caminho que chega ao fim sem desenhar
      // quadro nenhum (o `toImage` do laco nao completa sob o relogio
      // falso do teste de widget).
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(const MethodChannel('aurea/encoder'), (
            call,
          ) async {
            encoder.add(call.method);
            switch (call.method) {
              case 'available':
                return true;
              case 'remux':
                final alvo = File(call.arguments['target'] as String);
                alvo.parent.createSync(recursive: true);
                alvo.writeAsBytesSync(List.filled(8192, 3));
                return true;
              default:
                return true;
            }
          });
    });

    tearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            const MethodChannel('plugins.flutter.io/path_provider'),
            null,
          );
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(const MethodChannel('aurea/encoder'), null);
    });

    Future<void> exportarCortePuro(WidgetTester tester) async {
      tester.view.physicalSize = const Size(390, 844);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);

      // "Salvar na galeria" vem das preferencias: sem elas o ajuste nem
      // pode ser lido, e a exportacao terminaria em erro por um motivo
      // que nao tem nada a ver com a galeria.
      SharedPreferences.setMockInitialValues({});
      final prefs = await SharedPreferences.getInstance();
      final container = ProviderContainer(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      );
      addTearDown(container.dispose);
      container
          .read(editorControllerProvider.notifier)
          .openProject(
            VideoProject(
              name: 'p',
              createdAt: DateTime(2026, 9, 20),
              layers: [
                VideoLayer(
                  name: 'v',
                  startTime: Duration.zero,
                  // 6 s: o projeto tem no minimo 5 s, e o corte puro so
                  // vale quando o clipe cobre o relogio inteiro.
                  duration: const Duration(seconds: 6),
                  sourcePath: '${temp.path}/fonte.mp4',
                ),
              ],
            ),
          );

      // CHAVE NOVA A CADA CENARIO: sem ela o Flutter REUSA o `State` da
      // tela anterior (a arvore e identica) e a segunda exportacao abre
      // ja na tela de fim da primeira.
      cenario++;
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: ExportVideoScreen(key: ValueKey('cenario-$cenario')),
          ),
        ),
      );
      await tester.tap(find.text('Exportar'));
      for (var i = 0; i < 60; i++) {
        await tester.pump(const Duration(milliseconds: 16));
        if (find
            .byKey(const ValueKey('export-titulo-do-fim'))
            .evaluate()
            .isNotEmpty) {
          break;
        }
      }
      await tester.pump();
    }

    // OS TRES CENARIOS NUM TESTE SO, e isto e de proposito.
    //
    // O aquecimento dos shaders (`initState`) guarda os `Future` em
    // estaticos. O primeiro teste de widget do arquivo os resolve dentro
    // do relogio falso DELE; um segundo teste recebe o mesmo estatico e
    // fica esperando para sempre um `Future` de uma zona que ja morreu —
    // a tela nem sai da fase de ajustes. Rodando as tres exportacoes na
    // MESMA zona, o aquecimento acontece uma vez e vale para todas.
    testWidgets('o fim da exportacao so comemora com o registro na mao', (
      tester,
    ) async {
      // 1. COM URI: sucesso de verdade, com "Abrir" e "Compartilhar".
      respostaDaGaleria = (_) => {
        'uri': 'content://media/external/video/media/9',
        'bytes': 8192,
        'caminho': 'Movies/Aurea/aurea_p_180f.mp4',
      };
      await exportarCortePuro(tester);

      expect(encoder, contains('remux'));
      expect(
        chamadasDaGaleria.map((c) => c.method),
        contains('publicarVideo'),
        reason:
            'o caminho mais usado do app (cortar e exportar) saia sem '
            'registrar nada na galeria — era a causa mais frequente do '
            'relato',
      );
      final registro = chamadasDaGaleria
          .firstWhere((c) => c.method == 'publicarVideo')
          .arguments as Map;
      expect(registro['mime'], 'video/mp4');
      expect(registro['album'], 'Aurea');
      // O titulo do fim encurtou para "Exportado" quando a tela foi
      // simplificada (20/09): "com sucesso" nao acrescenta nada ao lado
      // do selo verde, e o caminho do arquivo so aparece quando ha
      // problema — com a URI na mao existem "Abrir" e "Compartilhar".
      expect(find.text('Exportado'), findsOneWidget);
      expect(find.byKey(const ValueKey('export-abrir')), findsOneWidget);
      expect(find.byKey(const ValueKey('export-compartilhar')), findsOneWidget);

      // 2. SEM URI: a tela nao pode dizer que deu certo.
      chamadasDaGaleria.clear();
      respostaDaGaleria = (_) => <String, Object?>{};
      await exportarCortePuro(tester);

      expect(find.text('Exportado'), findsNothing);
      expect(find.text('Exportado, mas nao entrou na galeria'), findsOneWidget);
      // A mensagem honesta do que houve, e o caminho para procurar.
      expect(find.byKey(const ValueKey('export-galeria')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('export-copiar-caminho')),
        findsOneWidget,
      );
      expect(find.byKey(const ValueKey('export-abrir')), findsNothing);

      // 3. A GALERIA RECUSA: o motivo dela aparece na tela.
      chamadasDaGaleria.clear();
      respostaDaGaleria = (_) => throw PlatformException(
        code: 'galeria',
        message: 'o sistema recusou o registro na galeria',
      );
      await exportarCortePuro(tester);

      expect(find.text('Exportado'), findsNothing);
      expect(
        find.textContaining('recusou o registro na galeria'),
        findsOneWidget,
      );
    });
  });
}
