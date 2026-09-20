import 'dart:convert';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'conta_da_comunidade.dart';

class SocialException implements Exception {
  SocialException(this.message, [this.status = 0]);
  final String message;
  final int status;
  @override
  String toString() => message;
}

class SocialService {
  SocialService(this.base, {HttpClient? client})
    : _http = client ?? HttpClient();
  final String base;
  final HttpClient _http;
  Future<Map<String, dynamic>> request(
    String path,
    String code, {
    String method = 'GET',
    Map<String, dynamic>? data,
  }) async {
    try {
      final req = await _http
          .openUrl(method, Uri.parse('$base$path'))
          .timeout(const Duration(seconds: 15));
      req.followRedirects = false;
      req.headers.set(HttpHeaders.authorizationHeader, 'Bearer $code');
      if (data != null) {
        req.headers.contentType = ContentType.json;
        req.write(jsonEncode(data));
      }
      final response = await req.close().timeout(const Duration(seconds: 20));
      final text = await response
          .transform(utf8.decoder)
          .join()
          .timeout(const Duration(seconds: 20));
      final body = jsonDecode(text) as Map<String, dynamic>;
      if (response.statusCode >= 400) {
        throw SocialException(
          '${body['erro'] ?? 'Tente novamente.'}',
          response.statusCode,
        );
      }
      return body;
    } on SocialException {
      rethrow;
    } catch (_) {
      throw SocialException(
        'Nao foi possivel conectar. Confira sua internet e tente novamente.',
      );
    }
  }

  void dispose() => _http.close();
}

final socialServiceProvider = Provider<SocialService>((ref) {
  final service = SocialService(ref.watch(comunidadeServiceProvider).endereco);
  ref.onDispose(service.dispose);
  return service;
});
