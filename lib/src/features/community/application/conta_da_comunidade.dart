import 'dart:convert';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';
import '../domain/moderacao.dart';
import 'comunidade_service.dart';
import 'social_service.dart';

/// A CONTA DO MURAL.
///
/// Ela vive NO SERVIDOR, e nao mais so no aparelho. O que muda com isso
/// nao e burocracia, e uma coisa so: o autor de um post deixa de ser um
/// texto que o aplicativo manda junto e passa a sair do CODIGO DE
/// ACESSO. Antes, quem montasse a requisicao a mao assinava com o nome
/// de quem quisesse.
///
/// Nao ha senha nem e-mail, de proposito. Senha pede recuperacao,
/// recuperacao pede e-mail, e-mail pede caixa de saida: tres pecas novas
/// num mural de beta. O codigo faz o mesmo papel, cabe num bloco de
/// notas, e serve para entrar noutro aparelho.
///
/// O CODIGO E O QUE NAO PODE SE PERDER. Ele fica gravado aqui, e a tela
/// da conta mostra para copiar. Perder o codigo e perder o apelido: sem
/// e-mail, nao ha para onde mandar um "esqueci".
class ContaDaComunidade {
  const ContaDaComunidade({
    required this.id,
    required this.apelido,
    required this.codigo,
    required this.criadaEm,
    this.avatar,
    this.nome = '',
    this.bio = '',
    this.verificado = false,
    this.oficial = false,
    this.criador = false,
  });

  final String id;
  final String nome, bio;
  final bool verificado, oficial, criador;
  final String apelido;

  /// O que prova quem e. Nunca sai daqui a nao ser no cabecalho de uma
  /// requisicao para o proprio mural.
  final String codigo;

  final DateTime criadaEm;

  /// Caminho de um arquivo no aparelho. Nulo = usa a inicial do apelido.
  final String? avatar;

  String get inicial =>
      apelido.trim().isEmpty ? 'A' : apelido.trim()[0].toUpperCase();

  Map<String, dynamic> toJson() => {
    'id': id,
    'nome': nome,
    'bio': bio,
    'verificado': verificado,
    'oficial': oficial,
    'criador': criador,
    'apelido': apelido,
    'codigo': codigo,
    'criadaEm': criadaEm.toUtc().toIso8601String(),
    if (avatar != null) 'avatar': avatar,
  };

  static ContaDaComunidade? deJson(String fonte) {
    try {
      final m = (jsonDecode(fonte) as Map).cast<String, dynamic>();
      final apelido = '${m['apelido'] ?? ''}';
      final codigo = '${m['codigo'] ?? ''}';
      if (apelido.trim().isEmpty ||
          !RegExp(r'^[a-f0-9]{48}$').hasMatch(codigo) ||
          '${m['id'] ?? ''}'.isEmpty) {
        return null;
      }
      return ContaDaComunidade(
        id: '${m['id'] ?? ''}',
        apelido: apelido,
        codigo: codigo,
        criadaEm:
            DateTime.tryParse('${m['criadaEm']}')?.toLocal() ?? DateTime.now(),
        avatar: m['avatar'] as String?,
        nome: '${m['nome'] ?? ''}',
        bio: '${m['bio'] ?? ''}',
        verificado: m['verificado'] == true,
        oficial: m['oficial'] == true,
        criador: m['criador'] == true,
      );
    } catch (_) {
      return null;
    }
  }

  ContaDaComunidade copyWith({String? apelido, String? avatar}) =>
      ContaDaComunidade(
        id: id,
        apelido: apelido ?? this.apelido,
        codigo: codigo,
        criadaEm: criadaEm,
        avatar: avatar ?? this.avatar,
        nome: nome,
        bio: bio,
        verificado: verificado,
        oficial: oficial,
        criador: criador,
      );

  ContaDaComunidade withPerfil(Map<String, dynamic> p) => ContaDaComunidade(
    id: id,
    codigo: codigo,
    criadaEm: criadaEm,
    apelido: '${p['apelido'] ?? apelido}',
    avatar: p['avatar'] as String?,
    nome: '${p['nome'] ?? ''}',
    bio: '${p['bio'] ?? ''}',
    verificado: p['verificado'] == true,
    oficial: p['oficial'] == true,
    criador: p['criador'] == true,
  );
}

class ContaDaComunidadeController extends Notifier<ContaDaComunidade?> {
  static const _chave = 'comunidade.conta';

  @override
  ContaDaComunidade? build() {
    try {
      final bruto = ref.read(sharedPreferencesProvider).getString(_chave);
      return bruto == null ? null : ContaDaComunidade.deJson(bruto);
    } catch (_) {
      return null;
    }
  }

  ComunidadeService get _servico => ref.read(comunidadeServiceProvider);

  /// CRIA A CONTA NO SERVIDOR. Devolve o motivo da recusa, ou null.
  ///
  /// O filtro roda aqui antes de chamar a rede — nao para valer (o que
  /// vale e o do servidor), mas para a pessoa saber na hora que o
  /// apelido nao serve, sem esperar a viagem.
  Future<String?> criar(String apelido, {String? avatar}) async {
    final veredito = moderarApelido(apelido);
    if (veredito.bloqueia) return veredito.motivo;
    final resposta = await _servico.criarConta(apelido);
    if (resposta.erro != null) return resposta.erro;
    _gravar(
      ContaDaComunidade(
        id: resposta.id!,
        apelido: resposta.apelido!,
        codigo: resposta.codigo!,
        criadaEm: DateTime.now(),
        avatar: avatar,
      ).withPerfil(resposta.perfil ?? const {}),
    );
    return null;
  }

  /// ENTRA COM O CODIGO, noutro aparelho. Devolve o motivo, ou null.
  Future<String?> entrar(String codigo) async {
    final limpo = codigo.trim().toLowerCase();
    if (!RegExp(r'^[0-9a-f]{48}$').hasMatch(limpo)) {
      return 'Esse código não tem a cara de um código de acesso. '
          'São 48 caracteres, entre 0 e 9 e a e f.';
    }
    final resposta = await _servico.entrarComCodigo(limpo);
    if (resposta.erro != null) return resposta.erro;
    _gravar(
      ContaDaComunidade(
        id: resposta.id!,
        apelido: resposta.apelido!,
        codigo: limpo,
        criadaEm: DateTime.now(),
      ).withPerfil(resposta.perfil ?? const {}),
    );
    return null;
  }

  /// Troca o apelido no servidor, ou so a foto (que e local).
  Future<String?> atualizar({
    String? apelido,
    String? avatar,
    String? nome,
    String? bio,
    bool removerFoto = false,
  }) async {
    final atual = state;
    if (atual == null) return 'Crie a conta primeiro.';
    try {
      var foto = avatar;
      if (foto != null && !foto.startsWith('https://')) {
        final uploaded = await _servico.subirArquivo(
          File(foto),
          foto.toLowerCase().endsWith('.png') ? 'image/png' : 'image/jpeg',
          atual.codigo,
        );
        if (!uploaded.deuCerto) return uploaded.erro;
        foto = uploaded.url;
      }
      final p = await ref
          .read(socialServiceProvider)
          .request(
            '/social/me',
            atual.codigo,
            method: 'PATCH',
            data: {
              'apelido': ?apelido,
              'nome': ?nome,
              'bio': ?bio,
              if (foto != null || removerFoto)
                'avatar': removerFoto ? null : foto,
            },
          );
      _gravar(atual.withPerfil(p));
      return null;
    } catch (e) {
      return '$e';
    }
  }

  void sincronizarPerfil(Map<String, dynamic> perfil) {
    final atual = state;
    if (atual != null && perfil['id'] == atual.id) {
      _gravar(atual.withPerfil(perfil));
    }
  }

  /// Sai DESTE APARELHO. A conta continua no servidor, e o codigo faz
  /// voltar — apagar a conta de verdade e outra conversa, e nao cabe
  /// atras de um botao que qualquer toque errado alcanca.
  void sair() {
    state = null;
    try {
      ref.read(sharedPreferencesProvider).remove(_chave);
    } catch (_) {}
  }

  void _gravar(ContaDaComunidade conta) {
    state = conta;
    try {
      ref
          .read(sharedPreferencesProvider)
          .setString(_chave, jsonEncode(conta.toJson()));
    } catch (_) {}
  }
}

final contaDaComunidadeProvider =
    NotifierProvider<ContaDaComunidadeController, ContaDaComunidade?>(
      ContaDaComunidadeController.new,
    );

final comunidadeServiceProvider = Provider<ComunidadeService>(
  (ref) => ComunidadeService.instance,
);

/// O que o servidor devolve ao criar conta ou entrar.
class RespostaDaConta {
  const RespostaDaConta({
    this.id,
    this.apelido,
    this.codigo,
    this.erro,
    this.perfil,
  });
  final Map<String, dynamic>? perfil;

  final String? id;
  final String? apelido;
  final String? codigo;

  /// Em portugues, vindo do proprio servidor quando ele recusa.
  final String? erro;

  static RespostaDaConta falha(String motivo) => RespostaDaConta(erro: motivo);

  static RespostaDaConta lerOuFalhar(int status, String corpo) {
    try {
      final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
      if (status == 200 || status == 201) {
        return RespostaDaConta(
          id: '${m['id']}',
          apelido: '${m['apelido']}',
          codigo: m['codigo'] as String?,
          perfil: m,
        );
      }
      final erro = m['erro'];
      if (erro is String && erro.isNotEmpty) return RespostaDaConta.falha(erro);
    } catch (_) {}
    return RespostaDaConta.falha(
      'O mural respondeu de um jeito estranho ($status).',
    );
  }
}

/// Traduz uma falha de rede para uma frase que ajuda.
String erroDeRede(Object e) => e is SocketException
    ? 'Sem conexão com o mural.'
    : 'Não consegui falar com o mural agora.';
