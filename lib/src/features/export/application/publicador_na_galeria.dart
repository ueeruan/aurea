import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:gal/gal.dart';

/// LEVAR O VIDEO EXPORTADO PARA A GALERIA — e SABER se ele chegou.
///
/// O RELATO QUE ORIGINOU ESTE ARQUIVO: "eu exporto meu video e ele nao
/// aparece na galeria". A causa era dupla:
///
///   1. O arquivo final nasce em `getApplicationDocumentsDirectory()`,
///      que no Android e a pasta PRIVADA do app
///      (`/data/user/0/<pacote>/app_flutter/exports`). Nenhum indexador
///      olha para la: o Google Fotos nunca ia ver o video, por mais que
///      ele estivesse perfeito no disco.
///   2. A copia para a galeria era um efeito colateral OPCIONAL, sem
///      resposta: `Gal.putVideo` nao devolve nada. A tela ja dizia
///      "Video pronto" antes de saber se o registro existia, e o
///      caminho mais comum de todos — o CORTE PURO, que copia as
///      trilhas sem recodificar — nem chamava a copia.
///
/// Aqui a publicacao passa a ser uma OPERACAO COM RESULTADO: ou volta
/// uma URI de conteudo que o sistema reconhece (e o tamanho que ele
/// gravou), ou volta o motivo. A tela so declara sucesso com a URI na
/// mao.
///
/// A escrita de verdade e do lado nativo (`aurea/galeria` em
/// `MainActivity.kt`), porque so de la se alcanca o `MediaStore` com
/// `RELATIVE_PATH`, `IS_PENDING` e o volume externo — o que o Android 10
/// em diante exige para um arquivo publico. No iOS quem sabe falar com o
/// `PHPhotoLibrary` e o `gal`, e ele continua sendo o caminho.
enum PlataformaDaGaleria { android, ios, outra }

/// O QUE ACONTECEU COM A PUBLICACAO.
///
/// Sem `throw`: uma exportacao que terminou nao vira erro porque a
/// galeria recusou. O arquivo existe, e [ondeEsta] continua dizendo
/// onde — mas [ok] so e verdadeiro quando o registro voltou.
@immutable
class PublicacaoNaGaleria {
  const PublicacaoNaGaleria({
    required this.ok,
    required this.mensagem,
    this.uri,
    this.bytes = 0,
    this.ondeEsta,
  });

  /// O registro existe. [uri] so e nula no iOS, onde a biblioteca de
  /// fotos nao devolve identificador utilizavel para abrir/compartilhar.
  const PublicacaoNaGaleria.publicado({
    required this.mensagem,
    required this.bytes,
    this.uri,
    this.ondeEsta,
  }) : ok = true;

  /// Nao chegou na galeria. [mensagem] diz o que houve, em portugues.
  const PublicacaoNaGaleria.falhou(this.mensagem, {this.ondeEsta})
    : ok = false,
      uri = null,
      bytes = 0;

  /// Se o video esta na galeria.
  final bool ok;

  /// A frase que a tela mostra.
  final String mensagem;

  /// O `content://` que o sistema devolveu. E ele que abre e compartilha.
  final String? uri;

  /// Quantos bytes o sistema gravou. Zero num registro que ficou vazio
  /// e falha, nao sucesso.
  final int bytes;

  /// Onde o arquivo esta, para quem precisar procurar na mao.
  final String? ondeEsta;

  /// So da para oferecer "Abrir" e "Compartilhar" com a URI na mao.
  bool get podeAbrir => ok && (uri?.isNotEmpty ?? false);
}

/// A PORTA PARA A GALERIA DO APARELHO.
class GaleriaDoAparelho {
  GaleriaDoAparelho._();

  static const canal = MethodChannel('aurea/galeria');

  /// A PLATAFORMA, como um valor e nao como um `if` espalhado.
  ///
  /// Os testes rodam no computador, onde `Platform.isAndroid` e falso —
  /// sem este ponto de troca nenhum teste chegaria ao canal, e a regra
  /// "so declara sucesso com a URI" ficaria sem prova.
  static PlataformaDaGaleria plataforma = Platform.isAndroid
      ? PlataformaDaGaleria.android
      : Platform.isIOS
      ? PlataformaDaGaleria.ios
      : PlataformaDaGaleria.outra;

  @visibleForTesting
  static void restaurarPlataforma() {
    plataforma = Platform.isAndroid
        ? PlataformaDaGaleria.android
        : Platform.isIOS
        ? PlataformaDaGaleria.ios
        : PlataformaDaGaleria.outra;
  }

  /// O ARQUIVO ESTA MESMO PRONTO?
  ///
  /// Devolve o motivo, ou nulo quando esta tudo certo. Um arquivo de
  /// zero byte e o fim silencioso classico: o codificador fechou, o
  /// `File` existe, e nao ha video nenhum dentro. Dizer "concluido"
  /// nesse caso e mentir.
  static String? conferir(File arquivo) {
    if (!arquivo.existsSync()) {
      return 'O arquivo final nao foi criado.';
    }
    final bytes = arquivo.lengthSync();
    if (bytes <= 0) {
      return 'O arquivo final saiu com 0 bytes.';
    }
    return null;
  }

  /// PUBLICA O VIDEO e devolve o que aconteceu.
  static Future<PublicacaoNaGaleria> publicarVideo(
    File arquivo, {
    String album = 'Aurea',
    String mime = 'video/mp4',
  }) async {
    final problema = conferir(arquivo);
    if (problema != null) {
      return PublicacaoNaGaleria.falhou(problema, ondeEsta: arquivo.path);
    }

    switch (plataforma) {
      case PlataformaDaGaleria.outra:
        return PublicacaoNaGaleria.falhou(
          'Galeria so no celular',
          ondeEsta: arquivo.path,
        );
      case PlataformaDaGaleria.ios:
        return _pelaBibliotecaDeFotos(arquivo, album);
      case PlataformaDaGaleria.android:
        return _peloMediaStore(arquivo, album, mime);
    }
  }

  /// ANDROID: o `MediaStore`, pelo canal nativo.
  static Future<PublicacaoNaGaleria> _peloMediaStore(
    File arquivo,
    String album,
    String mime,
  ) async {
    final Map<Object?, Object?>? resposta;
    try {
      resposta = await canal.invokeMethod<Map<Object?, Object?>>(
        'publicarVideo',
        {
          'caminho': arquivo.path,
          'nome': arquivo.uri.pathSegments.last,
          'mime': mime,
          'album': album,
        },
      );
    } on MissingPluginException {
      return PublicacaoNaGaleria.falhou(
        'O aparelho nao respondeu ao registro na galeria',
        ondeEsta: arquivo.path,
      );
    } on PlatformException catch (e) {
      return PublicacaoNaGaleria.falhou(
        'A galeria recusou o video: ${e.message ?? e.code}',
        ondeEsta: arquivo.path,
      );
    } catch (e) {
      return PublicacaoNaGaleria.falhou(
        'Nao deu para registrar na galeria: $e',
        ondeEsta: arquivo.path,
      );
    }

    // SEM URI NAO HOUVE REGISTRO. Uma resposta vazia (ou sem a chave) e
    // exatamente o caso que fazia a tela comemorar sozinha.
    final uri = resposta?['uri'] as String?;
    if (uri == null || uri.isEmpty) {
      return PublicacaoNaGaleria.falhou(
        'A galeria nao devolveu o registro do video',
        ondeEsta: arquivo.path,
      );
    }
    final bytes = (resposta?['bytes'] as num?)?.toInt() ?? 0;
    if (bytes <= 0) {
      return PublicacaoNaGaleria.falhou(
        'O video entrou na galeria com 0 bytes',
        ondeEsta: arquivo.path,
      );
    }
    final onde = resposta?['caminho'] as String?;
    return PublicacaoNaGaleria.publicado(
      mensagem: 'Salvo na galeria, no album $album',
      bytes: bytes,
      uri: uri,
      ondeEsta: onde == null || onde.isEmpty ? arquivo.path : onde,
    );
  }

  /// IOS: a biblioteca de fotos, pelo `gal`.
  static Future<PublicacaoNaGaleria> _pelaBibliotecaDeFotos(
    File arquivo,
    String album,
  ) async {
    try {
      if (!await Gal.hasAccess(toAlbum: true) &&
          !await Gal.requestAccess(toAlbum: true)) {
        return PublicacaoNaGaleria.falhou(
          'Sem permissao para a galeria',
          ondeEsta: arquivo.path,
        );
      }
      await Gal.putVideo(arquivo.path, album: album);
    } on GalException catch (e) {
      return PublicacaoNaGaleria.falhou(
        'Nao deu para salvar na galeria: ${e.type.message}',
        ondeEsta: arquivo.path,
      );
    } catch (e) {
      return PublicacaoNaGaleria.falhou(
        'Nao deu para salvar na galeria: $e',
        ondeEsta: arquivo.path,
      );
    }
    return PublicacaoNaGaleria.publicado(
      mensagem: 'Salvo em Fotos, no album $album',
      bytes: arquivo.lengthSync(),
      ondeEsta: arquivo.path,
    );
  }

  /// ABRE O VIDEO no aplicativo que o aparelho usa para video.
  static Future<bool> abrir(String uri, {String mime = 'video/mp4'}) async {
    try {
      return await canal.invokeMethod<bool>('abrir', {
            'uri': uri,
            'mime': mime,
          }) ??
          false;
    } catch (_) {
      return false;
    }
  }

  /// COMPARTILHA o video registrado.
  static Future<bool> compartilhar(
    String uri, {
    String mime = 'video/mp4',
  }) async {
    try {
      return await canal.invokeMethod<bool>('compartilhar', {
            'uri': uri,
            'mime': mime,
          }) ??
          false;
    } catch (_) {
      return false;
    }
  }
}
