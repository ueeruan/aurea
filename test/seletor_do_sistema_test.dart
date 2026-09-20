import 'package:aurea/src/features/media/application/seletor_do_sistema.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late SharedPreferences prefs;
  final chamadas = <MethodCall>[];
  Object? Function(MethodCall call)? responder;

  setUp(() async {
    chamadas.clear();
    responder = null;
    SharedPreferences.setMockInitialValues({});
    prefs = await SharedPreferences.getInstance();
    TestDefaultBinaryMessengerBinding
        .instance
        .defaultBinaryMessenger
        .setMockMethodCallHandler(SeletorDoSistema.canal, (call) async {
          chamadas.add(call);
          return responder?.call(call);
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(SeletorDoSistema.canal, null);
  });

  SeletorDoSistema seletor({bool android = true}) => SeletorDoSistema(
    noAndroid: () => android,
    prefs: () async => prefs,
  );

  test('fora do Android o canal nem é chamado: vai direto na reserva', () async {
    var reservas = 0;
    final r = await seletor(android: false).escolher(
      TipoDeSeletor.audio,
      reserva: () async {
        reservas++;
        return const ArquivoEscolhido(caminho: '/tmp/a.mp3', nome: 'a.mp3');
      },
    );
    expect(chamadas, isEmpty);
    expect(reservas, 1);
    expect(r?.caminho, '/tmp/a.mp3');
  });

  test('a URI do escolhido fica guardada POR TIPO e volta como dica', () async {
    responder = (_) => {
      'caminho': '/cache/beat.mp3',
      'nome': 'beat.mp3',
      'uri': 'content://doc/beat.mp3',
    };
    final s = seletor();
    Future<ArquivoEscolhido?> nunca() async {
      fail('a reserva não deveria abrir um segundo seletor');
    }

    final primeira = await s.escolher(TipoDeSeletor.audio, reserva: nunca);
    expect(primeira?.nome, 'beat.mp3');
    expect(primeira?.uri, 'content://doc/beat.mp3');
    expect(chamadas.single.arguments['uriInicial'], isNull);
    expect(prefs.getString(TipoDeSeletor.audio.chave), 'content://doc/beat.mp3');

    // Outro tipo NAO herda a pasta do audio.
    chamadas.clear();
    responder = (_) => {'caminho': '/cache/foto.png', 'nome': 'foto.png'};
    await s.escolher(TipoDeSeletor.imagem, reserva: nunca);
    expect(chamadas.single.arguments['uriInicial'], isNull);

    // O mesmo tipo reabre onde estava.
    chamadas.clear();
    await s.escolher(TipoDeSeletor.audio, reserva: nunca);
    expect(
      chamadas.single.arguments['uriInicial'],
      'content://doc/beat.mp3',
    );
    expect(chamadas.single.arguments['mimes'], ['audio/*']);
  });

  test('cancelar não é erro: nulo, e a reserva não abre outro seletor', () async {
    responder = (_) => null;
    var reservas = 0;
    final r = await seletor().escolher(
      TipoDeSeletor.modelo,
      reserva: () async {
        reservas++;
        return null;
      },
    );
    expect(r, isNull);
    expect(reservas, 0);
  });

  test('qualquer erro do canal cai no file_picker de antes', () async {
    responder = (_) => throw PlatformException(code: 'seletor');
    var reservas = 0;
    final r = await seletor().escolher(
      TipoDeSeletor.fonte,
      reserva: () async {
        reservas++;
        return const ArquivoEscolhido(caminho: '/tmp/f.ttf', nome: 'f.ttf');
      },
    );
    expect(reservas, 1);
    expect(r?.nome, 'f.ttf');
    expect(prefs.getString(TipoDeSeletor.fonte.chave), isNull);
  });

  test('resposta sem caminho também cai na reserva', () async {
    responder = (_) => {'nome': 'so o nome'};
    var reservas = 0;
    final r = await seletor().escolher(
      TipoDeSeletor.video,
      reserva: () async {
        reservas++;
        return const ArquivoEscolhido(caminho: '/tmp/v.mp4', nome: 'v.mp4');
      },
    );
    expect(reservas, 1);
    expect(r?.caminho, '/tmp/v.mp4');
  });

  test('sem nome na resposta, o nome sai do caminho', () async {
    responder = (_) => {'caminho': '/cache/123/Meu Clipe.mp4'};
    final r = await seletor().escolher(
      TipoDeSeletor.midia,
      reserva: () async => null,
    );
    expect(r?.nome, 'Meu Clipe.mp4');
    expect(r?.uri, isNull);
  });
}
