import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';

import '../application/conta_da_comunidade.dart';
import '../application/social_service.dart';
import '../domain/post_da_comunidade.dart';
import 'social_widgets.dart';
import 'community_tab.dart' show CommunityPostView;

void openProfile(BuildContext context, String id) => Navigator.of(
  context,
).push(MaterialPageRoute<void>(builder: (_) => SocialProfilePage(userId: id)));
void _error(BuildContext context, Object e) =>
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('$e')));

class SocialProfilePage extends ConsumerStatefulWidget {
  const SocialProfilePage({super.key, this.userId, this.embedded = false});
  final String? userId;
  final bool embedded;
  @override
  ConsumerState<SocialProfilePage> createState() => _SocialProfilePageState();
}

class _SocialProfilePageState extends ConsumerState<SocialProfilePage> {
  Map<String, dynamic>? _profile;
  List<PostDaComunidade> _posts = [];
  String? _failure, _cursor;
  bool _busy = false;
  String? get _id => widget.userId ?? ref.read(contaDaComunidadeProvider)?.id;
  Future<Map<String, dynamic>> _request(
    String path, {
    String method = 'GET',
    Map<String, dynamic>? data,
  }) => ref
      .read(socialServiceProvider)
      .request(
        path,
        ref.read(contaDaComunidadeProvider)!.codigo,
        method: method,
        data: data,
      );
  @override
  void initState() {
    super.initState();
    Future.microtask(_load);
  }

  Future<void> _load() async {
    if (_id == null) return;
    try {
      final p = await _request('/social/profiles/$_id');
      final feed = await _request(
        '/social/feed?autor=${Uri.encodeQueryComponent(_id!)}',
      );
      if (!mounted) return;
      setState(() {
        _profile = p;
        _posts = _parsePosts(feed);
        _cursor = feed['cursor'] as String?;
        _failure = null;
      });
      if (_id == ref.read(contaDaComunidadeProvider)?.id) {
        ref.read(contaDaComunidadeProvider.notifier).sincronizarPerfil(p);
      }
    } catch (e) {
      if (mounted) setState(() => _failure = '$e');
    }
  }

  List<PostDaComunidade> _parsePosts(Map<String, dynamic> f) => [
    for (final item in (f['posts'] as List? ?? []))
      ?PostDaComunidade.deJson(item),
  ];
  Future<void> _more() async {
    if (_cursor == null || _busy) return;
    setState(() => _busy = true);
    try {
      final f = await _request(
        '/social/feed?autor=${Uri.encodeQueryComponent(_id!)}&cursor=${Uri.encodeQueryComponent(_cursor!)}',
      );
      if (mounted) {
        setState(() {
          _posts.addAll(_parsePosts(f));
          _cursor = f['cursor'] as String?;
        });
      }
    } catch (e) {
      if (mounted) _error(context, e);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _action(String path, String method) async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await _request(path, method: method);
      await _load();
    } catch (e) {
      if (mounted) _error(context, e);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _verify(bool value) async {
    try {
      await _request(
        '/social/admin/verify/$_id',
        method: 'POST',
        data: {'verificado': value},
      );
      await _load();
    } catch (e) {
      if (mounted) _error(context, e);
    }
  }

  Future<void> _code() async {
    final code = ref.read(contaDaComunidadeProvider)!.codigo;
    await showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Seu codigo de acesso'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Guarde em segredo. Quem tiver este codigo pode entrar na sua conta. Ele nunca aparece no seu perfil publico.',
            ),
            const SizedBox(height: 16),
            SelectableText(
              code,
              style: const TextStyle(fontFamily: 'monospace', fontSize: 13),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Fechar'),
          ),
          FilledButton(
            key: const ValueKey('conta-copiar-codigo'),
            onPressed: () {
              Clipboard.setData(ClipboardData(text: code));
              Navigator.pop(ctx);
            },
            child: const Text('Copiar'),
          ),
        ],
      ),
    );
  }

  Future<void> _delete() async {
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Excluir sua conta?'),
        content: const Text(
          'Seu perfil e suas publicacoes deixam de aparecer. Suas conversas serao excluidas. O codigo deixa de funcionar. Seus projetos locais permanecem no aparelho.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancelar'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text(
              'Excluir conta',
              style: TextStyle(color: Colors.redAccent),
            ),
          ),
        ],
      ),
    );
    if (yes != true || !mounted) return;
    try {
      await _request(
        '/social/me',
        method: 'DELETE',
        data: {'confirmar': 'EXCLUIR'},
      );
      if (!mounted) return;
      Navigator.of(context).popUntil((r) => r.isFirst);
      ref.read(contaDaComunidadeProvider.notifier).sair();
    } catch (e) {
      if (mounted) _error(context, e);
    }
  }

  @override
  Widget build(BuildContext context) {
    final p = _profile, own = _id == ref.watch(contaDaComunidadeProvider)?.id;
    return Scaffold(
      appBar: AppBar(
        automaticallyImplyLeading: !widget.embedded,
        title: Text(p == null ? 'Perfil' : '@${p['apelido']}'),
        actions: [
          if (own)
            PopupMenuButton<String>(
              key: const ValueKey('perfil-menu'),
              onSelected: (s) {
                if (s == 'code') _code();
                if (s == 'delete') _delete();
                if (s == 'logout') {
                  Navigator.of(context).popUntil((r) => r.isFirst);
                  ref.read(contaDaComunidadeProvider.notifier).sair();
                }
              },
              itemBuilder: (_) => [
                const PopupMenuItem(
                  value: 'code',
                  child: Text('Codigo de acesso'),
                ),
                const PopupMenuItem(
                  key: ValueKey('conta-sair'),
                  value: 'logout',
                  child: Text('Sair desta conta'),
                ),
                if (p?['criador'] != true && p?['oficial'] != true)
                  const PopupMenuItem(
                    value: 'delete',
                    child: Text('Excluir conta'),
                  ),
              ],
            ),
          if (!own && p != null)
            PopupMenuButton<String>(
              onSelected: (s) {
                if (s == 'block') {
                  _action(
                    '/social/profiles/$_id/block',
                    p['bloqueado'] == true ? 'DELETE' : 'POST',
                  );
                }
                if (s == 'report') {
                  showSocialReport(context, ref, 'perfil:$_id');
                }
                if (s == 'verify') _verify(p['verificado'] != true);
              },
              itemBuilder: (_) => [
                PopupMenuItem(
                  value: 'block',
                  child: Text(
                    p['bloqueado'] == true ? 'Desbloquear' : 'Bloquear',
                  ),
                ),
                const PopupMenuItem(
                  value: 'report',
                  child: Text('Denunciar perfil'),
                ),
                if (ref.read(contaDaComunidadeProvider)?.criador == true &&
                    p['oficial'] != true &&
                    p['criador'] != true)
                  PopupMenuItem(
                    value: 'verify',
                    child: Text(
                      p['verificado'] == true
                          ? 'Remover verificação'
                          : 'Verificar conta',
                    ),
                  ),
              ],
            ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: _load,
        child: ListView(
          padding: const EdgeInsets.only(bottom: 32),
          children: [
            if (_failure != null)
              Padding(
                padding: const EdgeInsets.all(20),
                child: Column(
                  children: [
                    Text(_failure!),
                    TextButton(
                      onPressed: _load,
                      child: const Text('Tentar novamente'),
                    ),
                  ],
                ),
              ),
            if (p == null && _failure == null)
              const Padding(
                padding: EdgeInsets.all(60),
                child: Center(child: CircularProgressIndicator()),
              ),
            if (p != null) ...[
              Padding(
                padding: const EdgeInsets.all(20),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        ProfileAvatar(
                          name: '${p['apelido']}',
                          url: p['avatar'] as String?,
                          radius: 42,
                        ),
                        const SizedBox(width: 20),
                        Expanded(
                          child: Wrap(
                            spacing: 18,
                            runSpacing: 12,
                            children: [
                              _stat(
                                '${p['seguidores'] ?? 0}',
                                'Seguidores',
                                'followers',
                              ),
                              _stat(
                                '${p['seguindo'] ?? 0}',
                                'Seguindo',
                                'following',
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 18),
                    Row(
                      children: [
                        Flexible(
                          child: Text(
                            '${p['nome']}',
                            style: const TextStyle(
                              fontSize: 23,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ),
                        if (p['verificado'] == true) ...[
                          const SizedBox(width: 6),
                          VerifiedBadge(
                            official: p['oficial'] == true,
                            size: 22,
                          ),
                        ],
                      ],
                    ),
                    if (p['criador'] == true)
                      const Text(
                        'Criador do Aurea',
                        style: TextStyle(color: Color(0xff258bff)),
                      ),
                    if (p['oficial'] == true)
                      const Text(
                        'Conta oficial do Aurea',
                        style: TextStyle(color: Color(0xff258bff)),
                      ),
                    if ('${p['bio']}'.isNotEmpty)
                      Padding(
                        padding: const EdgeInsets.only(top: 10, bottom: 8),
                        child: Text(
                          '${p['bio']}',
                          style: const TextStyle(height: 1.4),
                        ),
                      ),
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        Expanded(
                          child: FilledButton(
                            onPressed: _busy
                                ? null
                                : own
                                ? () async {
                                    await Navigator.push(
                                      context,
                                      MaterialPageRoute<void>(
                                        builder: (_) =>
                                            EditSocialProfile(profile: p),
                                      ),
                                    );
                                    await _load();
                                  }
                                : () => _action(
                                    '/social/profiles/$_id/follow',
                                    p['euSigo'] == true ? 'DELETE' : 'POST',
                                  ),
                            child: Text(
                              own
                                  ? 'Editar perfil'
                                  : p['euSigo'] == true
                                  ? 'Seguindo'
                                  : 'Seguir',
                            ),
                          ),
                        ),
                        if (!own) ...[
                          const SizedBox(width: 10),
                          Expanded(
                            child: OutlinedButton(
                              onPressed: p['bloqueado'] == true
                                  ? null
                                  : () => Navigator.push(
                                      context,
                                      MaterialPageRoute<void>(
                                        builder: (_) => SocialChatPage(peer: p),
                                      ),
                                    ),
                              child: const Text('Mensagem'),
                            ),
                          ),
                        ],
                      ],
                    ),
                  ],
                ),
              ),
              const Divider(),
              const Padding(
                padding: EdgeInsets.fromLTRB(20, 8, 20, 18),
                child: Text(
                  'Publicacoes',
                  style: TextStyle(fontSize: 17, fontWeight: FontWeight.w700),
                ),
              ),
              if (_posts.isEmpty)
                const Padding(
                  padding: EdgeInsets.all(28),
                  child: Center(child: Text('Nenhuma publicacao ainda.')),
                ),
              for (final post in _posts)
                Padding(
                  padding: const EdgeInsets.only(bottom: 24),
                  child: CommunityPostView(post: post, onDeleted: _load),
                ),
              if (_cursor != null)
                TextButton(
                  onPressed: _busy ? null : _more,
                  child: const Text('Carregar mais'),
                ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _stat(String number, String label, String relation) => InkWell(
    onTap: () => Navigator.push(
      context,
      MaterialPageRoute<void>(
        builder: (_) => SocialPeoplePage(
          title: label,
          path: '/social/profiles/$_id/$relation',
        ),
      ),
    ),
    child: Column(
      children: [
        Text(
          number,
          style: const TextStyle(fontSize: 21, fontWeight: FontWeight.bold),
        ),
        Text(label),
      ],
    ),
  );
}

class EditSocialProfile extends ConsumerStatefulWidget {
  const EditSocialProfile({super.key, required this.profile});
  final Map<String, dynamic> profile;
  @override
  ConsumerState<EditSocialProfile> createState() => _EditSocialProfileState();
}

class _EditSocialProfileState extends ConsumerState<EditSocialProfile> {
  late final _handle = TextEditingController(
    text: '${widget.profile['apelido']}',
  );
  late final _name = TextEditingController(text: '${widget.profile['nome']}');
  late final _bio = TextEditingController(text: '${widget.profile['bio']}');
  String? _photo;
  bool _remove = false, _saving = false;
  String? _failure;
  @override
  void dispose() {
    _handle.dispose();
    _name.dispose();
    _bio.dispose();
    super.dispose();
  }

  Future<void> _pick() async {
    try {
      final image = await ImagePicker().pickImage(
        source: ImageSource.gallery,
        maxWidth: 512,
        maxHeight: 512,
        imageQuality: 85,
      );
      if (image != null && mounted) {
        setState(() {
          _photo = image.path;
          _remove = false;
        });
      }
    } catch (e) {
      if (mounted) _error(context, e);
    }
  }

  Future<void> _save() async {
    setState(() => _saving = true);
    final error = await ref
        .read(contaDaComunidadeProvider.notifier)
        .atualizar(
          apelido: _handle.text.trim(),
          nome: _name.text.trim(),
          bio: _bio.text.trim(),
          avatar: _photo,
          removerFoto: _remove,
        );
    if (!mounted) return;
    if (error == null) {
      Navigator.pop(context);
    } else {
      setState(() {
        _failure = error;
        _saving = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Editar perfil')),
    body: ListView(
      padding: const EdgeInsets.all(24),
      children: [
        Center(
          child: ProfileAvatar(
            name: _name.text,
            url: _remove ? null : _photo ?? widget.profile['avatar'] as String?,
            radius: 46,
          ),
        ),
        TextButton(
          onPressed: _saving ? null : _pick,
          child: const Text('Trocar foto publica'),
        ),
        TextButton(
          onPressed: _saving ? null : () => setState(() => _remove = true),
          child: const Text('Remover foto'),
        ),
        TextField(
          controller: _name,
          maxLength: 50,
          decoration: const InputDecoration(labelText: 'Nome de exibicao'),
        ),
        TextField(
          controller: _handle,
          maxLength: 24,
          decoration: const InputDecoration(
            labelText: 'NickName unico',
            prefixText: '@',
          ),
        ),
        TextField(
          controller: _bio,
          maxLength: 180,
          minLines: 3,
          maxLines: 5,
          decoration: const InputDecoration(labelText: 'Biografia'),
        ),
        if (_failure != null)
          Text(_failure!, style: const TextStyle(color: Colors.redAccent)),
        const SizedBox(height: 20),
        FilledButton(
          onPressed: _saving ? null : _save,
          child: Text(_saving ? 'Salvando...' : 'Salvar perfil'),
        ),
      ],
    ),
  );
}

class SocialPeoplePage extends ConsumerStatefulWidget {
  const SocialPeoplePage({
    super.key,
    this.title = 'Encontrar pessoas',
    this.path = '/social/profiles',
  });
  final String title, path;
  @override
  ConsumerState<SocialPeoplePage> createState() => _SocialPeoplePageState();
}

class _SocialPeoplePageState extends ConsumerState<SocialPeoplePage> {
  List<Map<String, dynamic>> _people = [];
  String? _failure, _after;
  Timer? _debounce;
  int _generation = 0;
  @override
  void initState() {
    super.initState();
    _load('');
  }

  @override
  void dispose() {
    _debounce?.cancel();
    super.dispose();
  }

  Future<void> _load(String query, {bool more = false}) async {
    final generation = ++_generation;
    try {
      final r = await ref
          .read(socialServiceProvider)
          .request(
            '${widget.path}?q=${Uri.encodeQueryComponent(query)}${more && _after != null ? '&after=${Uri.encodeQueryComponent(_after!)}' : ''}',
            ref.read(contaDaComunidadeProvider)!.codigo,
          );
      if (mounted && generation == _generation) {
        setState(() {
          final items = [
            for (final p in r['perfis'] as List)
              (p as Map).cast<String, dynamic>(),
          ];
          _people = more ? [..._people, ...items] : items;
          _after = r['after'] as String?;
          _failure = null;
        });
      }
    } catch (e) {
      if (mounted) setState(() => _failure = '$e');
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: Text(widget.title)),
    body: Column(
      children: [
        if (widget.path == '/social/profiles')
          Padding(
            padding: const EdgeInsets.all(16),
            child: TextField(
              decoration: const InputDecoration(
                prefixIcon: Icon(Icons.search),
                hintText: 'Nome ou @nickname',
              ),
              onChanged: (v) {
                _debounce?.cancel();
                _debounce = Timer(
                  const Duration(milliseconds: 350),
                  () => _load(v),
                );
              },
            ),
          ),
        if (_failure != null)
          Padding(padding: const EdgeInsets.all(20), child: Text(_failure!)),
        Expanded(
          child: ListView(
            children: [
              for (final p in _people)
                ListTile(
                  leading: ProfileAvatar(
                    name: '${p['apelido']}',
                    url: p['avatar'] as String?,
                  ),
                  title: Row(
                    children: [
                      Flexible(child: Text('${p['nome']}')),
                      if (p['verificado'] == true) ...[
                        const SizedBox(width: 5),
                        VerifiedBadge(official: p['oficial'] == true),
                      ],
                    ],
                  ),
                  subtitle: Text('@${p['apelido']}'),
                  onTap: () => openProfile(context, '${p['id']}'),
                ),
              if (_people.isEmpty && _failure == null)
                const Padding(
                  padding: EdgeInsets.all(32),
                  child: Text('Nenhum perfil encontrado.'),
                ),
              if (_after != null)
                TextButton(
                  onPressed: () => _load('', more: true),
                  child: const Text('Carregar mais'),
                ),
            ],
          ),
        ),
      ],
    ),
  );
}

class SocialInboxPage extends ConsumerStatefulWidget {
  const SocialInboxPage({super.key});
  @override
  ConsumerState<SocialInboxPage> createState() => _SocialInboxPageState();
}

class _SocialInboxPageState extends ConsumerState<SocialInboxPage> {
  List<Map<String, dynamic>> _chats = [];
  String? _failure;
  bool _loading = true;
  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      final r = await ref
          .read(socialServiceProvider)
          .request(
            '/social/chats',
            ref.read(contaDaComunidadeProvider)!.codigo,
          );
      if (mounted) {
        setState(() {
          _chats = [
            for (final c in r['conversas'] as List)
              (c as Map).cast<String, dynamic>(),
          ];
          _failure = null;
        });
      }
    } catch (e) {
      if (mounted) setState(() => _failure = '$e');
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(
      title: const Text('Mensagens'),
      actions: [
        IconButton(
          tooltip: 'Nova conversa',
          onPressed: () => Navigator.push(
            context,
            MaterialPageRoute<void>(builder: (_) => const SocialPeoplePage()),
          ),
          icon: const Icon(Icons.edit_square),
        ),
      ],
    ),
    body: RefreshIndicator(
      onRefresh: _load,
      child: ListView(
        children: [
          if (_loading) const LinearProgressIndicator(),
          if (_failure != null)
            Padding(padding: const EdgeInsets.all(20), child: Text(_failure!)),
          if (!_loading && _chats.isEmpty)
            const Padding(
              padding: EdgeInsets.all(40),
              child: Text(
                'Suas conversas aparecem aqui. Abra um perfil e toque em Mensagem.',
              ),
            ),
          for (final c in _chats)
            Builder(
              builder: (context) {
                final p = (c['perfil'] as Map).cast<String, dynamic>();
                return ListTile(
                  leading: ProfileAvatar(
                    name: '${p['apelido']}',
                    url: p['avatar'] as String?,
                  ),
                  title: Row(
                    children: [
                      Flexible(child: Text('${p['nome']}')),
                      if (p['verificado'] == true)
                        VerifiedBadge(official: p['oficial'] == true),
                    ],
                  ),
                  subtitle: Text(
                    '${c['ultima']}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                  trailing: (c['naoLidas'] as num) > 0
                      ? Badge(label: Text('${c['naoLidas']}'))
                      : null,
                  onTap: () async {
                    await Navigator.push(
                      context,
                      MaterialPageRoute<void>(
                        builder: (_) => SocialChatPage(peer: p),
                      ),
                    );
                    await _load();
                  },
                );
              },
            ),
        ],
      ),
    ),
  );
}

class SocialChatPage extends ConsumerStatefulWidget {
  const SocialChatPage({super.key, required this.peer});
  final Map<String, dynamic> peer;
  @override
  ConsumerState<SocialChatPage> createState() => _SocialChatPageState();
}

class _SocialChatPageState extends ConsumerState<SocialChatPage>
    with WidgetsBindingObserver {
  final _input = TextEditingController();
  final _scroll = ScrollController();
  final List<Map<String, dynamic>> _messages = [];
  Timer? _timer;
  bool _sending = false, _loading = false, _foreground = true;
  String? _failure, _pendingId;
  String get _path => '/social/chats/${widget.peer['id']}';
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _load();
    _timer = Timer.periodic(const Duration(seconds: 4), (_) {
      if (_foreground && ModalRoute.of(context)?.isCurrent == true) _load();
    });
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _foreground = state == AppLifecycleState.resumed;
    if (_foreground) _load();
  }

  @override
  void dispose() {
    _timer?.cancel();
    WidgetsBinding.instance.removeObserver(this);
    _input.dispose();
    _scroll.dispose();
    super.dispose();
  }

  Future<Map<String, dynamic>> _request(
    String path, {
    String method = 'GET',
    Map<String, dynamic>? data,
  }) => ref
      .read(socialServiceProvider)
      .request(
        path,
        ref.read(contaDaComunidadeProvider)!.codigo,
        method: method,
        data: data,
      );
  Future<void> _load({bool older = false}) async {
    if (_loading || !mounted) return;
    _loading = true;
    try {
      final query = _messages.isEmpty
          ? ''
          : older
          ? '?before=${_messages.first['id']}'
          : '?after=${_messages.last['id']}';
      final r = await _request('$_path$query');
      if (!mounted) return;
      final items = [
        for (final m in r['mensagens'] as List)
          (m as Map).cast<String, dynamic>(),
      ];
      final nearBottom =
          !_scroll.hasClients ||
          _scroll.position.maxScrollExtent - _scroll.offset < 100;
      setState(() {
        final known = _messages.map((m) => m['id']).toSet();
        final fresh = items.where((m) => !known.contains(m['id']));
        if (older) {
          _messages.insertAll(0, fresh);
        } else {
          _messages.addAll(fresh);
        }
        _failure = null;
      });
      if (_foreground && ModalRoute.of(context)?.isCurrent == true) {
        await _request('$_path/read', method: 'POST');
      }
      if (items.isNotEmpty && !older && nearBottom) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (mounted && _scroll.hasClients) {
            _scroll.jumpTo(_scroll.position.maxScrollExtent);
          }
        });
      }
    } catch (e) {
      if (mounted) setState(() => _failure = '$e');
    } finally {
      _loading = false;
    }
  }

  Future<void> _send() async {
    final text = _input.text.trim();
    if (text.isEmpty || _sending) return;
    setState(() => _sending = true);
    _pendingId ??=
        '${DateTime.now().microsecondsSinceEpoch}-${ref.read(contaDaComunidadeProvider)!.id}';
    try {
      await _request(
        _path,
        method: 'POST',
        data: {'texto': text, 'clientId': _pendingId},
      );
      if (!mounted) return;
      _input.clear();
      _pendingId = null;
      await _load();
    } catch (e) {
      if (mounted) setState(() => _failure = '$e');
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final mine = ref.watch(contaDaComunidadeProvider)?.id;
    return Scaffold(
      appBar: AppBar(
        title: InkWell(
          onTap: () => openProfile(context, '${widget.peer['id']}'),
          child: Row(
            children: [
              ProfileAvatar(
                name: '${widget.peer['apelido']}',
                url: widget.peer['avatar'] as String?,
                radius: 17,
              ),
              const SizedBox(width: 10),
              Flexible(child: Text('${widget.peer['nome']}')),
              if (widget.peer['verificado'] == true)
                VerifiedBadge(official: widget.peer['oficial'] == true),
            ],
          ),
        ),
      ),
      body: SafeArea(
        child: Column(
          children: [
            if (_failure != null)
              MaterialBanner(
                content: Text(_failure!),
                actions: [
                  TextButton(
                    onPressed: _load,
                    child: const Text('Tentar novamente'),
                  ),
                ],
              ),
            Expanded(
              child: ListView(
                controller: _scroll,
                padding: const EdgeInsets.all(16),
                children: [
                  if (_messages.length >= 50)
                    TextButton(
                      onPressed: () => _load(older: true),
                      child: const Text('Mensagens anteriores'),
                    ),
                  if (_messages.isEmpty)
                    const Padding(
                      padding: EdgeInsets.all(24),
                      child: Text(
                        'Conversa privada. Apenas voces dois podem acessar as mensagens pelo app.',
                      ),
                    ),
                  for (final m in _messages)
                    Align(
                      alignment: m['sender'] == mine
                          ? Alignment.centerRight
                          : Alignment.centerLeft,
                      child: Container(
                        constraints: BoxConstraints(
                          maxWidth: MediaQuery.sizeOf(context).width * .78,
                        ),
                        margin: const EdgeInsets.symmetric(vertical: 4),
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: m['sender'] == mine
                              ? Theme.of(context).colorScheme.primaryContainer
                              : Theme.of(context)
                                    .colorScheme
                                    .surfaceContainerHighest,
                          borderRadius: BorderRadius.circular(16),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.end,
                          children: [
                            SelectableText('${m['text']}'),
                            const SizedBox(height: 4),
                            Text(
                              '${DateTime.tryParse('${m['created_at']}')?.toLocal().toString().substring(11, 16) ?? ''}${m['sender'] == mine ? '  ✓' : ''}',
                              style: const TextStyle(fontSize: 10),
                            ),
                          ],
                        ),
                      ),
                    ),
                ],
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 6, 8, 10),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Expanded(
                    child: TextField(
                      controller: _input,
                      maxLength: 2000,
                      minLines: 1,
                      maxLines: 5,
                      enabled: !_sending,
                      onChanged: (_) => _pendingId = null,
                      decoration: const InputDecoration(
                        hintText: 'Mensagem...',
                        counterText: '',
                      ),
                    ),
                  ),
                  IconButton(
                    tooltip: 'Enviar',
                    onPressed: _sending ? null : _send,
                    icon: const Icon(Icons.send_rounded),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

Future<void> showSocialReport(
  BuildContext context,
  WidgetRef ref,
  String target,
) async {
  final input = TextEditingController();
  final reason = await showDialog<String>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('Denunciar'),
      content: TextField(
        controller: input,
        maxLength: 500,
        maxLines: 4,
        decoration: const InputDecoration(hintText: 'O que aconteceu?'),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx),
          child: const Text('Cancelar'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, input.text.trim()),
          child: const Text('Enviar'),
        ),
      ],
    ),
  );
  input.dispose();
  if (reason == null || !context.mounted) return;
  try {
    await ref
        .read(socialServiceProvider)
        .request(
          '/social/reports',
          ref.read(contaDaComunidadeProvider)!.codigo,
          method: 'POST',
          data: {'alvo': target, 'motivo': reason},
        );
    if (context.mounted) _error(context, 'Denuncia enviada para revisao.');
  } catch (e) {
    if (context.mounted) _error(context, e);
  }
}
