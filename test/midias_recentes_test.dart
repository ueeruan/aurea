import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/media/application/midias_recentes.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory temp;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('recentes');
  });

  tearDown(() {
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  });

  Future<ProviderContainer> com([
    Map<String, Object> inicial = const {},
  ]) async {
    SharedPreferences.setMockInitialValues(inicial);
    final prefs = await SharedPreferences.getInstance();
    return ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
  }

  String arquivo(String nome) {
    final f = File('${temp.path}/$nome')..writeAsBytesSync(const [1, 2, 3]);
    return f.path;
  }

  MidiaRecente foto(String caminho, {String? origem, String? miniatura}) =>
      MidiaRecente(
        caminho: caminho,
        nome: caminho.split(RegExp(r'[\\/]')).last,
        video: false,
        origem: origem,
        miniatura: miniatura,
      );

  test('a última mídia usada vai para a frente, sem repetir arquivo', () async {
    final c = await com();
    addTearDown(c.dispose);
    final a = arquivo('a.png');
    final b = arquivo('b.png');
    c.read(midiasRecentesProvider.notifier)
      ..registrar(foto(a))
      ..registrar(foto(b))
      ..registrar(foto(a));
    expect(c.read(midiasRecentesProvider).map((m) => m.caminho), [a, b]);
  });

  test('sobrevive a reabrir, e o que sumiu do disco sai da lista', () async {
    final vivo = arquivo('vivo.png');
    final c = await com();
    addTearDown(c.dispose);
    c.read(midiasRecentesProvider.notifier)
      ..registrar(foto(arquivo('morto.png')))
      ..registrar(foto(vivo));
    final gravado = c
        .read(sharedPreferencesProvider)
        .getString(MidiasRecentesNotifier.kChave)!;
    File('${temp.path}/morto.png').deleteSync();

    final outro = await com({MidiasRecentesNotifier.kChave: gravado});
    addTearDown(outro.dispose);
    expect(outro.read(midiasRecentesProvider).single.caminho, vivo);
  });

  test('a lista tem teto de 24', () async {
    final c = await com();
    addTearDown(c.dispose);
    final lista = c.read(midiasRecentesProvider.notifier);
    for (var i = 0; i < 30; i++) {
      lista.registrar(foto(arquivo('m$i.png')));
    }
    expect(c.read(midiasRecentesProvider), hasLength(24));
    expect(c.read(midiasRecentesProvider).first.nome, 'm29.png');
  });

  test('tirar apaga só o jpg da miniatura, nunca a mídia', () async {
    final c = await com();
    addTearDown(c.dispose);
    final a = arquivo('a.mp4');
    final mini = arquivo('a.jpg');
    c.read(midiasRecentesProvider.notifier)
      ..registrar(
        MidiaRecente(
          caminho: a,
          nome: 'a.mp4',
          video: true,
          miniatura: mini,
        ),
      )
      ..tirar(a);
    expect(c.read(midiasRecentesProvider), isEmpty);
    expect(File(mini).existsSync(), isFalse);
    expect(File(a).existsSync(), isTrue, reason: 'outra camada pode usá-la');
  });

  test('registrar de novo herda a miniatura que já existia', () async {
    final c = await com();
    addTearDown(c.dispose);
    final v = arquivo('v.mp4');
    final mini = arquivo('v.jpg');
    final lista = c.read(midiasRecentesProvider.notifier);
    lista.registrar(
      MidiaRecente(caminho: v, nome: 'v.mp4', video: true, miniatura: mini),
    );
    // O reuso a partir dos Recentes nao traz miniatura na mao.
    lista.registrar(MidiaRecente(caminho: v, nome: 'v.mp4', video: true));
    expect(c.read(midiasRecentesProvider).single.miniatura, mini);
    expect(File(mini).existsSync(), isTrue);
  });

  test('daOrigem reconhece o que já foi copiado, e só se o arquivo existe', () async {
    final c = await com();
    addTearDown(c.dispose);
    final a = arquivo('a.png');
    final sumido = arquivo('sumido.png');
    c.read(midiasRecentesProvider.notifier)
      ..registrar(foto(a, origem: 'galeria:1@10'))
      ..registrar(foto(sumido, origem: 'galeria:2@20'));
    File(sumido).deleteSync();
    final lista = c.read(midiasRecentesProvider.notifier);
    expect(lista.daOrigem('galeria:1@10')?.caminho, a);
    expect(lista.daOrigem('galeria:2@20'), isNull);
    expect(lista.daOrigem('galeria:1@99'), isNull, reason: 'foto editada');
    expect(lista.daOrigem(null), isNull);
  });

  test('lixo nas prefs vira lista vazia', () async {
    final c = await com({MidiasRecentesNotifier.kChave: '{nada'});
    addTearDown(c.dispose);
    expect(c.read(midiasRecentesProvider), isEmpty);

    final outro = await com({
      MidiasRecentesNotifier.kChave: jsonEncode([
        {'n': 'sem caminho'},
        42,
        {'c': arquivo('ok.png'), 'n': 'ok.png', 'd': 'não é número'},
      ]),
    });
    addTearDown(outro.dispose);
    expect(outro.read(midiasRecentesProvider).single.duracaoMs, 0);
  });

  test('a miniatura guardada vira jpg; bytes vazios não viram nada', () async {
    final alvo = await guardarMiniaturaDeRecente(
      Uint8List.fromList(const [9, 9, 9]),
      raiz: () async => temp,
    );
    expect(alvo, isNotNull);
    expect(File(alvo!).readAsBytesSync(), const [9, 9, 9]);
    expect(await guardarMiniaturaDeRecente(null, raiz: () async => temp), isNull);
    expect(
      await guardarMiniaturaDeRecente(Uint8List(0), raiz: () async => temp),
      isNull,
    );
  });

  test('registrar depois de importar ignora arquivo que não existe', () async {
    final c = await com();
    addTearDown(c.dispose);
    final lista = c.read(midiasRecentesProvider.notifier);
    await registrarMidiaImportada(
      lista,
      caminho: '${temp.path}/fantasma.png',
      nome: 'fantasma.png',
      video: false,
    );
    expect(c.read(midiasRecentesProvider), isEmpty);

    final a = arquivo('a.png');
    await registrarMidiaImportada(
      lista,
      caminho: a,
      nome: 'a.png',
      video: false,
      duracao: const Duration(seconds: 3),
      origem: 'galeria:7@70',
    );
    final entrou = c.read(midiasRecentesProvider).single;
    expect(entrou.caminho, a);
    expect(entrou.duracao, const Duration(seconds: 3));
    expect(entrou.origem, 'galeria:7@70');
    expect(entrou.miniatura, isNull, reason: 'foto usa o próprio arquivo');
  });
}
