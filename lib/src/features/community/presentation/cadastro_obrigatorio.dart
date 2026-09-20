import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/l10n/app_language.dart';
import '../../../core/theme/app_theme.dart';
import '../application/conta_da_comunidade.dart';

/// Bloqueia o restante do app ate existir uma identidade do mural neste
/// aparelho. A conta e leve: apelido + codigo de acesso, sem e-mail ou senha.
class CadastroObrigatorioGate extends ConsumerWidget {
  const CadastroObrigatorioGate({required this.child, super.key});

  final Widget child;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conta = ref.watch(contaDaComunidadeProvider);
    return conta == null ? const CadastroObrigatorioPage() : child;
  }
}

class CadastroObrigatorioPage extends ConsumerStatefulWidget {
  const CadastroObrigatorioPage({super.key});

  @override
  ConsumerState<CadastroObrigatorioPage> createState() =>
      _CadastroObrigatorioPageState();
}

class _CadastroObrigatorioPageState
    extends ConsumerState<CadastroObrigatorioPage> {
  final _apelido = TextEditingController();
  final _codigo = TextEditingController();
  bool _entrar = false;
  bool _carregando = false;
  String? _erro;

  @override
  void dispose() {
    _apelido.dispose();
    _codigo.dispose();
    super.dispose();
  }

  Future<void> _enviar() async {
    final valor = (_entrar ? _codigo : _apelido).text.trim();
    if (valor.isEmpty) {
      setState(() {
        _erro = _entrar
            ? 'Cole seu código de acesso.'
            : 'Escolha um NickName para continuar.';
      });
      return;
    }
    setState(() {
      _carregando = true;
      _erro = null;
    });
    final controlador = ref.read(contaDaComunidadeProvider.notifier);
    final erro = _entrar
        ? await controlador.entrar(valor)
        : await controlador.criar(valor);
    if (!mounted) return;
    setState(() {
      _carregando = false;
      _erro = erro;
    });
  }

  void _alternar() {
    if (_carregando) return;
    setState(() {
      _entrar = !_entrar;
      _erro = null;
    });
  }

  @override
  Widget build(BuildContext context) => PopScope(
    canPop: false,
    child: Scaffold(
      body: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 24),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 430),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Align(
                    alignment: Alignment.centerLeft,
                    child: Container(
                      width: 58,
                      height: 58,
                      decoration: BoxDecoration(
                        color: AppColors.accentDim,
                        borderRadius: BorderRadius.circular(18),
                        border: Border.all(color: AppColors.hairline),
                      ),
                      child: Icon(
                        CupertinoIcons.person_crop_circle_badge_plus,
                        color: AppColors.lime,
                        size: 31,
                      ),
                    ),
                  ),
                  const SizedBox(height: 26),
                  AppText(
                    _entrar ? 'Entre na sua conta' : 'Crie sua conta',
                    key: const ValueKey('cadastro-obrigatorio-titulo'),
                    style: Theme.of(context).textTheme.headlineLarge,
                  ),
                  const SizedBox(height: 10),
                  AppText(
                    _entrar
                        ? 'Use o código que você guardou para recuperar seu perfil.'
                        : 'Escolha um NickName. Ele identifica você na Comunidade e sua conta entra no total de usuários do Aurea.',
                    style: TextStyle(
                      fontSize: 16,
                      height: 1.4,
                      color: AppColors.muted,
                    ),
                  ),
                  const SizedBox(height: 28),
                  TextField(
                    key: ValueKey(
                      _entrar ? 'cadastro-codigo' : 'cadastro-nickname',
                    ),
                    controller: _entrar ? _codigo : _apelido,
                    autofocus: true,
                    enabled: !_carregando,
                    maxLength: _entrar ? 48 : 20,
                    autocorrect: !_entrar,
                    textCapitalization: _entrar
                        ? TextCapitalization.none
                        : TextCapitalization.words,
                    textInputAction: TextInputAction.done,
                    onSubmitted: (_) => _enviar(),
                    decoration: InputDecoration(
                      labelText: _entrar ? 'Código de acesso' : 'NickName',
                      hintText: _entrar
                          ? '48 caracteres'
                          : 'Como vão chamar você?',
                      prefixIcon: Icon(
                        _entrar ? CupertinoIcons.lock : CupertinoIcons.at,
                      ),
                      errorText: _erro,
                    ),
                  ),
                  const SizedBox(height: 12),
                  FilledButton(
                    key: const ValueKey('cadastro-criar-conta'),
                    onPressed: _carregando ? null : _enviar,
                    child: _carregando
                        ? const SizedBox(
                            width: 22,
                            height: 22,
                            child: CircularProgressIndicator(strokeWidth: 2.3),
                          )
                        : AppText(_entrar ? 'Entrar' : 'Criar conta'),
                  ),
                  const SizedBox(height: 10),
                  TextButton(
                    key: const ValueKey('cadastro-alternar'),
                    onPressed: _carregando ? null : _alternar,
                    child: AppText(
                      _entrar
                          ? 'Quero criar uma conta nova'
                          : 'Já tenho um código de acesso',
                    ),
                  ),
                  const SizedBox(height: 20),
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Icon(
                        CupertinoIcons.cloud_fill,
                        size: 16,
                        color: AppColors.muted,
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: AppText(
                          'A conta é salva no servidor Cloudflare do Aurea. Não pedimos e-mail nem senha.',
                          style: TextStyle(
                            fontSize: 12.5,
                            height: 1.35,
                            color: AppColors.muted,
                          ),
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    ),
  );
}
