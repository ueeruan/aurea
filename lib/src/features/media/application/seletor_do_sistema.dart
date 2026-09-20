import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// PARA QUE O SELETOR ESTA SENDO ABERTO. Cada tipo lembra a PROPRIA ultima
/// pasta: quem busca trilha em "Musicas" e video em "DCIM/Camera" nao quer
/// que um atropele o outro.
enum TipoDeSeletor {
  midia(['image/*', 'video/*']),
  imagem(['image/*']),
  video(['video/*']),
  audio(['audio/*']),
  // Fonte, modelo 3D e SVG nao tem MIME confiavel no Android (cada
  // provedor inventa o seu para .otf/.obj/.fbx): abrir sem filtro e
  // conferir a extensao depois, como o file_picker `any` ja fazia.
  fonte(['*/*']),
  modelo(['*/*']),
  svg(['image/svg+xml', '*/*']);

  const TipoDeSeletor(this.mimes);
  final List<String> mimes;

  String get chave => 'seletor.ultima_uri.$name';
}

/// O que o seletor devolve: o arquivo ja copiado para o cache do app (o
/// chamador ainda tem de passar por `persist` se for guardar), o nome como
/// a pessoa conhece e a URI do original.
class ArquivoEscolhido {
  const ArquivoEscolhido({required this.caminho, required this.nome, this.uri});
  final String caminho;
  final String nome;
  final String? uri;
}

/// O SELETOR DE DOCUMENTOS QUE VOLTA PARA ONDE A PESSOA ESTAVA.
///
/// POR QUE UM CANAL PROPRIO: o `file_picker` aceita `initialDirectory` na
/// assinatura do `pickFiles` e o DESCARTA no celular — so o `saveFile`
/// repassa. No Android a unica forma de abrir o navegador de documentos
/// num lugar e o `EXTRA_INITIAL_URI` do `ACTION_OPEN_DOCUMENT`, e isso e o
/// que o canal `aurea/seletor` do `MainActivity.kt` faz. A dica aceita a
/// URI de um ARQUIVO (o sistema abre na pasta que o contem), entao basta
/// guardar a URI do ultimo escolhido, por tipo — nao precisa derivar pasta.
///
/// O QUE NAO DA:
/// - iOS: o PHPicker roda fora do processo e nao aceita album inicial; o
///   `UIDocumentPicker.directoryURL` pediria bookmark com escopo de
///   seguranca e um canal em Swift que nao existe. La o unico "voltar onde
///   estava" e o do album da galeria propria (`GalleryPanel`).
/// - Botoes "Fotos/Videos do sistema" (image_picker): `ACTION_GET_CONTENT`
///   sem extras, e o plugin nao devolve URI nenhuma para lembrar.
/// - A dica e so uma dica: se a pasta sumiu ou o provedor nao a acha, o
///   Android abre no lugar de sempre, sem erro.
///
/// QUALQUER ERRO DO CANAL CAI NA [reserva] (o file_picker de antes): o
/// canal e melhoria, e nunca pode ser o motivo de a pessoa nao conseguir
/// importar. Cancelar NAO e erro — devolve nulo e nao abre um segundo
/// seletor.
class SeletorDoSistema {
  const SeletorDoSistema({
    bool Function()? noAndroid,
    Future<SharedPreferences> Function()? prefs,
  }) : _noAndroid = noAndroid ?? _ehAndroid,
       _prefs = prefs ?? SharedPreferences.getInstance;

  static const canal = MethodChannel('aurea/seletor');

  final bool Function() _noAndroid;
  final Future<SharedPreferences> Function() _prefs;

  static bool _ehAndroid() => Platform.isAndroid;

  Future<ArquivoEscolhido?> escolher(
    TipoDeSeletor tipo, {
    required Future<ArquivoEscolhido?> Function() reserva,
  }) async {
    if (!_noAndroid()) return reserva();
    SharedPreferences? prefs;
    String? ultima;
    try {
      prefs = await _prefs();
      ultima = prefs.getString(tipo.chave);
    } catch (_) {}
    final Map<String, Object?>? r;
    try {
      r = await canal.invokeMapMethod<String, Object?>('escolher', {
        'mimes': tipo.mimes,
        'uriInicial': ?ultima,
      });
    } catch (_) {
      return reserva();
    }
    if (r == null) return null;
    final caminho = r['caminho'];
    if (caminho is! String || caminho.isEmpty) return reserva();
    final nome = r['nome'];
    final uri = r['uri'];
    if (uri is String && uri.isNotEmpty) {
      try {
        await prefs?.setString(tipo.chave, uri);
      } catch (_) {}
    }
    return ArquivoEscolhido(
      caminho: caminho,
      nome: nome is String && nome.isNotEmpty
          ? nome
          : caminho.split(RegExp(r'[\\/]')).last,
      uri: uri is String && uri.isNotEmpty ? uri : null,
    );
  }

  /// A RESERVA PADRAO: o mesmo `pickFiles` que o app sempre usou.
  static Future<ArquivoEscolhido?> Function() peloFilePicker(
    FileType tipo, {
    List<String>? extensoes,
  }) => () async {
    final r = await FilePicker.platform.pickFiles(
      type: tipo,
      allowedExtensions: extensoes,
      allowMultiple: false,
      withData: false,
    );
    final f = r?.files.single;
    final caminho = f?.path;
    if (f == null || caminho == null) return null;
    return ArquivoEscolhido(caminho: caminho, nome: f.name, uri: f.identifier);
  };
}
