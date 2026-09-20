import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:uuid/uuid.dart';

import '../../../core/storage/prefs.dart';

/// UMA FOTO OU VIDEO QUE JA ENTROU NUM PROJETO.
///
/// O [caminho] e o da COPIA dentro do app (`imported_media`), e nao o do
/// seletor: reusar um recente nao copia nada de novo, e o arquivo nao some
/// quando o Android limpa o cache. A [origem] e o rastro do original (id do
/// item da galeria, ou a URI do seletor) — serve so para reconhecer "esta
/// eu ja tenho" antes de copiar de novo. A [miniatura] e um jpg pequeno em
/// cache; imagem nao precisa (o proprio arquivo serve de miniatura).
class MidiaRecente {
  const MidiaRecente({
    required this.caminho,
    required this.nome,
    required this.video,
    this.duracaoMs = 0,
    this.origem,
    this.miniatura,
  });

  final String caminho;
  final String nome;
  final bool video;
  final int duracaoMs;
  final String? origem;
  final String? miniatura;

  Duration get duracao => Duration(milliseconds: duracaoMs);

  Map<String, dynamic> toJson() => {
    'c': caminho,
    'n': nome,
    'v': video,
    if (duracaoMs > 0) 'd': duracaoMs,
    if (origem != null) 'o': origem,
    if (miniatura != null) 'm': miniatura,
  };

  /// TOLERANTE: registro torto e pulado, campo opcional torto vira nulo. A
  /// lista e conveniencia — ela nunca pode impedir a galeria de abrir.
  static MidiaRecente? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final c = raw['c'];
    final n = raw['n'];
    if (c is! String || c.isEmpty || n is! String) return null;
    final d = raw['d'];
    final o = raw['o'];
    final m = raw['m'];
    return MidiaRecente(
      caminho: c,
      nome: n,
      video: raw['v'] == true,
      duracaoMs: d is num && d.isFinite && d > 0 ? d.round() : 0,
      origem: o is String && o.isNotEmpty ? o : null,
      miniatura: m is String && m.isNotEmpty ? m : null,
    );
  }
}

/// OS RECENTES DA AUREA: quem usa o mesmo logo ou o mesmo clipe em varios
/// projetos nao deveria procurar o arquivo na galeria toda vez. Mais novo
/// na frente, sem repetir arquivo, e some com o que foi apagado do disco.
class MidiasRecentesNotifier extends Notifier<List<MidiaRecente>> {
  static const kChave = 'midia.recentes';

  /// Oito linhas de tres na grade. Mais que isso ninguem rola para achar.
  static const maximo = 24;

  @override
  List<MidiaRecente> build() {
    try {
      final bruto = ref.read(sharedPreferencesProvider).getString(kChave);
      if (bruto == null) return const [];
      final lista = jsonDecode(bruto);
      if (lista is! List) return const [];
      final vistos = <String>{};
      return List.unmodifiable([
        for (final m in lista)
          if (MidiaRecente.fromJson(m) case final r?)
            if (vistos.add(r.caminho) && File(r.caminho).existsSync()) r,
      ].take(maximo));
    } catch (_) {
      return const [];
    }
  }

  void registrar(MidiaRecente midia) {
    final anteriores = state.where((m) => m.caminho == midia.caminho).toList();
    // REGISTRAR DE NOVO NAO PERDE A MINIATURA: reusar um recente sobe o
    // item para a frente, e quem chama nesse caso nao traz miniatura.
    final herdada = anteriores
        .map((m) => m.miniatura)
        .whereType<String>()
        .firstOrNull;
    final entra = midia.miniatura == null && herdada != null
        ? MidiaRecente(
            caminho: midia.caminho,
            nome: midia.nome,
            video: midia.video,
            duracaoMs: midia.duracaoMs,
            origem: midia.origem ?? anteriores.first.origem,
            miniatura: herdada,
          )
        : midia;
    final novo = [entra, ...state.where((m) => m.caminho != midia.caminho)];
    for (final velho in anteriores) {
      if (velho.miniatura != entra.miniatura) _apagarMiniatura(velho);
    }
    if (novo.length > maximo) {
      novo.sublist(maximo).forEach(_apagarMiniatura);
    }
    state = List.unmodifiable(
      novo.length > maximo ? novo.sublist(0, maximo) : novo,
    );
    _gravar();
  }

  void tirar(String caminho) {
    state.where((m) => m.caminho == caminho).forEach(_apagarMiniatura);
    state = List.unmodifiable(state.where((m) => m.caminho != caminho));
    _gravar();
  }

  /// JA TENHO ESTA? Procura pela [origem] e so devolve se a copia ainda
  /// esta no disco — e o que deixa a mesma foto da galeria entrar em dez
  /// projetos ocupando o espaco de uma.
  MidiaRecente? daOrigem(String? origem) {
    if (origem == null || origem.isEmpty) return null;
    for (final m in state) {
      if (m.origem == origem && File(m.caminho).existsSync()) return m;
    }
    return null;
  }

  /// SO O JPG DA MINIATURA. O arquivo de midia NUNCA e apagado aqui: outras
  /// camadas, em outros projetos, podem estar apontando para ele.
  void _apagarMiniatura(MidiaRecente m) {
    final caminho = m.miniatura;
    if (caminho == null) return;
    try {
      final f = File(caminho);
      if (f.existsSync()) f.deleteSync();
    } catch (_) {}
  }

  void _gravar() {
    try {
      ref
          .read(sharedPreferencesProvider)
          .setString(kChave, jsonEncode([for (final m in state) m.toJson()]));
    } catch (_) {}
  }
}

final midiasRecentesProvider =
    NotifierProvider<MidiasRecentesNotifier, List<MidiaRecente>>(
      MidiasRecentesNotifier.new,
    );

/// A PASTA DAS MINIATURAS DOS RECENTES. Fica no suporte do app, e nao no
/// cache do sistema: o Android limpa o cache quando quer, e a lista ficaria
/// com videos sem rosto.
Future<Directory> _pastaDasMiniaturas(
  Future<Directory> Function()? raiz,
) async {
  final base = await (raiz ?? getApplicationSupportDirectory)();
  return Directory('${base.path}/cache/recentes').create(recursive: true);
}

/// GUARDA OS BYTES DE UMA MINIATURA (os 200x200 que a galeria ja tinha na
/// mao). Nulo em qualquer falha: miniatura que nao saiu nao derruba o
/// registro — o item aparece com o icone de video no lugar.
Future<String?> guardarMiniaturaDeRecente(
  Uint8List? bytes, {
  Future<Directory> Function()? raiz,
}) async {
  if (bytes == null || bytes.isEmpty) return null;
  try {
    final pasta = await _pastaDasMiniaturas(raiz);
    final alvo = File('${pasta.path}/${const Uuid().v4()}.jpg');
    await alvo.writeAsBytes(bytes, flush: true);
    return alvo.path;
  } catch (_) {
    return null;
  }
}

/// UM QUADRO DO VIDEO como miniatura, para o que veio do seletor do sistema
/// (la nao existe miniatura pronta). Mesmo molde da tira da timeline: um
/// quadro so, 200 px de altura. Nulo em qualquer falha.
Future<String?> extrairMiniaturaDoVideo(
  String video, {
  Future<Directory> Function()? raiz,
}) async {
  try {
    final pasta = await _pastaDasMiniaturas(raiz);
    final alvo = File('${pasta.path}/${const Uuid().v4()}.jpg');
    final sessao = await FFmpegKit.executeWithArguments([
      '-v',
      'error',
      '-y',
      '-threads',
      '1',
      '-i',
      video,
      '-frames:v',
      '1',
      '-vf',
      'scale=-1:200',
      '-q:v',
      '5',
      alvo.path,
    ]);
    if (!ReturnCode.isSuccess(await sessao.getReturnCode()) ||
        !alvo.existsSync() ||
        alvo.lengthSync() == 0) {
      if (alvo.existsSync()) alvo.deleteSync();
      return null;
    }
    return alvo.path;
  } catch (_) {
    return null;
  }
}

/// O REGISTRO DEPOIS DE IMPORTAR, num ponto so.
///
/// Recebe o [recentes] ja lido (e nao um `ref`): quem importa costuma
/// fechar a folha no mesmo gesto, e o `ref` de um widget desmontado nao
/// pode mais ser usado. Nunca levanta — a camada ja entrou no projeto, e
/// um defeito na lista de conveniencia nao pode virar "falha ao importar".
Future<void> registrarMidiaImportada(
  MidiasRecentesNotifier recentes, {
  required String caminho,
  required String nome,
  required bool video,
  Duration duracao = Duration.zero,
  String? origem,
  Future<Uint8List?> Function()? miniaturaPronta,
}) async {
  try {
    if (!File(caminho).existsSync()) return;
    String? miniatura;
    if (video) {
      Uint8List? bytes;
      try {
        bytes = await miniaturaPronta?.call();
      } catch (_) {}
      miniatura =
          await guardarMiniaturaDeRecente(bytes) ??
          await extrairMiniaturaDoVideo(caminho);
    }
    recentes.registrar(
      MidiaRecente(
        caminho: caminho,
        nome: nome,
        video: video,
        duracaoMs: duracao.inMilliseconds,
        origem: origem,
        miniatura: miniatura,
      ),
    );
  } catch (_) {}
}
