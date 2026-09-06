import 'dart:convert';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

/// Aba de icones via API publica do Iconify (spec AUREA-atualizacao §6).
/// - Busca por `/search`, dados do icone em LOTE por `/{prefix}.json`
///   (nunca .svg um a um);
/// - cache em disco por icone;
/// - LICENCA e requisito: cada conjunto informa a sua, o filtro "sem
///   exigencia de atribuicao" vem LIGADO por padrao e o selo aparece
///   em cada resultado.
class IconifyService {
  static const _base = 'api.iconify.design';

  /// Licencas sem exigencia de atribuicao.
  static const _permissiveSpdx = {
    'MIT',
    'Apache-2.0',
    'ISC',
    'CC0-1.0',
    'Unlicense',
    'OFL-1.1',
    'MPL-2.0',
  };

  final HttpClient _http = HttpClient()
    ..connectionTimeout = const Duration(seconds: 8);

  Map<String, ({String title, String spdx, bool permissive})>?
      _collections;
  final Map<String, String> _bodyCache = {};

  Future<Map<String, dynamic>?> _getJson(String path) async {
    try {
      final req = await _http.getUrl(Uri.https(_base, path.split('?').first,
          path.contains('?') ? Uri.splitQueryString(path.split('?')[1]) : null));
      final res = await req.close();
      if (res.statusCode != 200) return null;
      final body = await res.transform(utf8.decoder).join();
      return jsonDecode(body) as Map<String, dynamic>;
    } catch (_) {
      return null;
    }
  }

  Future<void> _ensureCollections() async {
    if (_collections != null) return;
    final json = await _getJson('/collections');
    final out =
        <String, ({String title, String spdx, bool permissive})>{};
    if (json != null) {
      for (final e in json.entries) {
        final info = e.value as Map<String, dynamic>;
        final lic = info['license'] as Map<String, dynamic>?;
        final spdx = (lic?['spdx'] as String?) ?? '';
        out[e.key] = (
          title: (lic?['title'] as String?) ?? 'desconhecida',
          spdx: spdx,
          permissive: _permissiveSpdx.contains(spdx),
        );
      }
    }
    _collections = out;
  }

  /// A lista de colecoes chegou? Sem ela nao da para afirmar licenca.
  bool get collectionsLoaded =>
      _collections != null && _collections!.isNotEmpty;

  ({String title, bool permissive}) licenseOf(String prefix) {
    final info = _collections?[prefix];
    if (info == null) return (title: '?', permissive: false);
    return (title: info.spdx.isEmpty ? info.title : info.spdx,
        permissive: info.permissive);
  }

  /// Busca icones; com [permissiveOnly] filtra para conjuntos sem
  /// exigencia de atribuicao (padrao LIGADO). Se a lista de colecoes
  /// nao carregou (rede fraca), o filtro FALHA ABERTO: mostrar tudo com
  /// selo "?" e melhor que fingir que nao ha resultados.
  Future<List<(String prefix, String name)>> search(String query,
      {bool permissiveOnly = true, int limit = 48}) async {
    await _ensureCollections();
    final json = await _getJson(
        '/search?query=${Uri.encodeQueryComponent(query)}&limit=96');
    if (json == null) return const [];
    final applyFilter = permissiveOnly && collectionsLoaded;
    final out = <(String, String)>[];
    for (final id in (json['icons'] as List? ?? const [])) {
      final parts = (id as String).split(':');
      if (parts.length != 2) continue;
      if (applyFilter && !licenseOf(parts[0]).permissive) continue;
      out.add((parts[0], parts[1]));
      if (out.length >= limit) break;
    }
    return out;
  }

  /// Path data (atributos `d` concatenados) dos icones pedidos, em LOTE
  /// por prefixo, com cache em memoria e disco.
  Future<Map<String, String>> pathDataFor(
      List<(String prefix, String name)> icons) async {
    final out = <String, String>{};
    final missing = <String, List<String>>{};

    Directory? dir;
    try {
      final support = await getApplicationSupportDirectory();
      dir = Directory('${support.path}/icons');
      if (!dir.existsSync()) dir.createSync(recursive: true);
    } catch (_) {}

    for (final (prefix, name) in icons) {
      final key = '$prefix:$name';
      final cached = _bodyCache[key];
      if (cached != null) {
        out[key] = cached;
        continue;
      }
      final file = dir == null ? null : File('${dir.path}/${prefix}_$name.d');
      if (file != null && file.existsSync()) {
        final d = file.readAsStringSync();
        _bodyCache[key] = d;
        out[key] = d;
        continue;
      }
      missing.putIfAbsent(prefix, () => []).add(name);
    }

    for (final e in missing.entries) {
      final json = await _getJson(
          '/${e.key}.json?icons=${e.value.join(',')}');
      final iconsJson = json?['icons'] as Map<String, dynamic>?;
      if (iconsJson == null) continue;
      for (final name in e.value) {
        final body = (iconsJson[name]
            as Map<String, dynamic>?)?['body'] as String?;
        if (body == null) continue;
        // Extrai todos os `d="..."` do body (a maioria dos conjuntos
        // usa um unico <path>).
        final ds = RegExp(r'd="([^"]+)"')
            .allMatches(body)
            .map((m) => m.group(1)!)
            .join(' ');
        if (ds.isEmpty) continue;
        final key = '${e.key}:$name';
        _bodyCache[key] = ds;
        out[key] = ds;
        try {
          File('${dir!.path}/${e.key}_$name.d').writeAsStringSync(ds);
        } catch (_) {}
      }
    }
    return out;
  }
}

final iconifyServiceProvider =
    Provider<IconifyService>((ref) => IconifyService());
