import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';

import '../../../core/theme/app_theme.dart';
import '../../../core/ui/snack.dart';
import '../../community/application/conta_da_comunidade.dart';
import '../../projects/application/projects_controller.dart';
import '../../projects/presentation/home_shell.dart';

/// ABA PERFIL: Gestão de perfil e conta REAL conectada ao Cloudflare Workers/KV.
class UserTab extends ConsumerStatefulWidget {
  const UserTab({super.key});

  @override
  ConsumerState<UserTab> createState() => _UserTabState();
}

class _UserTabState extends ConsumerState<UserTab> {
  bool _modoEntrar = false;
  bool _carregando = false;
  bool _codigoVisivel = false;
  String? _erro;

  final _apelidoController = TextEditingController();
  final _codigoController = TextEditingController();
  String? _avatarPath;

  @override
  void dispose() {
    _apelidoController.dispose();
    _codigoController.dispose();
    super.dispose();
  }

  Future<void> _escolherFoto() async {
    try {
      final picker = ImagePicker();
      final x = await picker.pickImage(
        source: ImageSource.gallery,
        maxWidth: 512,
        imageQuality: 85,
      );
      if (x == null || !mounted) return;
      setState(() => _avatarPath = x.path);
      final conta = ref.read(contaDaComunidadeProvider);
      if (conta != null) {
        await ref
            .read(contaDaComunidadeProvider.notifier)
            .atualizar(avatar: x.path);
        if (mounted) {
          AureaSnack.show(context, 'Foto de perfil atualizada.');
        }
      }
    } catch (_) {
      if (mounted) {
        AureaSnack.show(context, 'Não foi possível acessar a galeria.');
      }
    }
  }

  Future<void> _criarConta() async {
    final apelido = _apelidoController.text.trim();
    if (apelido.isEmpty) {
      setState(() => _erro = 'Digite um apelido para sua conta.');
      return;
    }
    setState(() {
      _carregando = true;
      _erro = null;
    });

    final erro = await ref
        .read(contaDaComunidadeProvider.notifier)
        .criar(apelido, avatar: _avatarPath);

    if (!mounted) return;
    setState(() => _carregando = false);
    if (erro != null) {
      setState(() => _erro = erro);
    } else {
      AureaSnack.show(context, 'Conta criada no Cloudflare com sucesso!');
    }
  }

  Future<void> _entrarComCodigo() async {
    final codigo = _codigoController.text.trim();
    if (codigo.isEmpty) {
      setState(() => _erro = 'Cole seu código de acesso.');
      return;
    }
    setState(() {
      _carregando = true;
      _erro = null;
    });

    final erro = await ref
        .read(contaDaComunidadeProvider.notifier)
        .entrar(codigo);

    if (!mounted) return;
    setState(() => _carregando = false);
    if (erro != null) {
      setState(() => _erro = erro);
    } else {
      AureaSnack.show(context, 'Bem-vindo de volta!');
    }
  }

  Future<void> _editarApelido(BuildContext context, ContaDaComunidade conta) async {
    final controller = TextEditingController(text: conta.apelido);
    final novo = await showCupertinoDialog<String>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: const AppText('Editar apelido'),
        content: Padding(
          padding: const EdgeInsets.only(top: 10),
          child: CupertinoTextField(
            controller: controller,
            placeholder: translate(context, 'Novo apelido'),
            maxLength: 20,
            autofocus: true,
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(c).pop(),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(c).pop(controller.text.trim()),
            child: const AppText('Salvar'),
          ),
        ],
      ),
    );

    if (novo == null || novo.isEmpty || novo == conta.apelido || !mounted) return;
    final erro = await ref
        .read(contaDaComunidadeProvider.notifier)
        .atualizar(apelido: novo);
    if (!mounted || !context.mounted) return;
    if (erro != null) {
      AureaSnack.show(context, erro);
    } else {
      AureaSnack.show(context, 'Apelido atualizado no Cloudflare.');
    }
  }

  Future<void> _confirmarSaida(BuildContext context) async {
    final confirma = await showCupertinoDialog<bool>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: const AppText('Sair desta conta?'),
        content: const AppText('Sua conta e posts continuarão seguros no Cloudflare. '
          'Certifique-se de ter copiado seu código de acesso antes de sair.',
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(c).pop(false),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Sair'),
          ),
        ],
      ),
    );

    if (confirma == true && mounted) {
      ref.read(contaDaComunidadeProvider.notifier).sair();
      setState(() {
        _modoEntrar = false;
        _apelidoController.clear();
        _codigoController.clear();
        _avatarPath = null;
      });
      if (!context.mounted) return;
      AureaSnack.show(context, 'Você saiu da conta.');
    }
  }

  String _formatarData(DateTime data) {
    return '${data.day.toString().padLeft(2, '0')}/'
        '${data.month.toString().padLeft(2, '0')}/'
        '${data.year}';
  }

  @override
  Widget build(BuildContext context) {
    final conta = ref.watch(contaDaComunidadeProvider);
    final projetosCount = ref.watch(projectsControllerProvider).length;

    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 120),
        children: [
          Row(
            children: [
              Expanded(
                child: AppText(
                  'Perfil',
                  style: Theme.of(context).textTheme.headlineLarge,
                ),
              ),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                decoration: BoxDecoration(
                  color: AppColors.surface,
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(color: AppColors.hairline),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Container(
                      width: 7,
                      height: 7,
                      decoration: const BoxDecoration(
                        color: Color(0xFF1ED6B1),
                        shape: BoxShape.circle,
                      ),
                    ),
                    const SizedBox(width: 6),
                    AppText('Cloudflare KV',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.muted,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          // SE NÃO ESTIVER LOGADO -> TELA DE CRIAÇÃO / LOGIN
          if (conta == null) ...[
            _construirCardAutenticacao(),
          ] else ...[
            // SE ESTIVER LOGADO -> PERFIL COMPLETO REAL
            _construirPerfilReal(conta, projetosCount),
          ],
        ],
      ),
    );
  }

  Widget _construirCardAutenticacao() {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: AppColors.hairline),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: 0.35),
            blurRadius: 20,
            offset: const Offset(0, 4),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Banner de Destaque
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: [
                  AppColors.lime.withValues(alpha: 0.15),
                  AppColors.violet.withValues(alpha: 0.15),
                ],
              ),
              borderRadius: BorderRadius.circular(14),
              border: Border.all(
                color: AppColors.lime.withValues(alpha: 0.25),
              ),
            ),
            child: Row(
              children: [
                Container(
                  width: 40,
                  height: 40,
                  decoration: BoxDecoration(
                    color: AppColors.lime.withValues(alpha: 0.2),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(
                    CupertinoIcons.cloud_fill,
                    color: AppColors.lime,
                    size: 22,
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      AppText('Conta Oficial do Criador',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: AppColors.onDark,
                        ),
                      ),
                      const SizedBox(height: 2),
                      AppText('Publique projetos no mural e sincronize com a nuvem.',
                        style: TextStyle(
                          fontSize: 12,
                          color: AppColors.muted,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Alternador Criar / Entrar
          Row(
            children: [
              Expanded(
                child: GestureDetector(
                  onTap: () => setState(() {
                    _modoEntrar = false;
                    _erro = null;
                  }),
                  child: Container(
                    padding: const EdgeInsets.symmetric(vertical: 10),
                    decoration: BoxDecoration(
                      color: !_modoEntrar
                          ? AppColors.surfaceHigh
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(10),
                      border: !_modoEntrar
                          ? Border.all(
                              color: AppColors.lime.withValues(alpha: 0.4),
                            )
                          : null,
                    ),
                    alignment: Alignment.center,
                    child: AppText('Criar Conta',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: !_modoEntrar ? AppColors.lime : AppColors.muted,
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: GestureDetector(
                  key: const ValueKey('conta-alternar-entrada'),
                  onTap: () => setState(() {
                    _modoEntrar = true;
                    _erro = null;
                  }),
                  child: Container(
                    padding: const EdgeInsets.symmetric(vertical: 10),
                    decoration: BoxDecoration(
                      color: _modoEntrar
                          ? AppColors.surfaceHigh
                          : Colors.transparent,
                      borderRadius: BorderRadius.circular(10),
                      border: _modoEntrar
                          ? Border.all(
                              color: AppColors.lime.withValues(alpha: 0.4),
                            )
                          : null,
                    ),
                    alignment: Alignment.center,
                    child: AppText('Entrar com Código',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: _modoEntrar ? AppColors.lime : AppColors.muted,
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          if (!_modoEntrar) ...[
            // Formulário de Criação de Conta
            Center(
              child: GestureDetector(
                key: const ValueKey('conta-foto'),
                onTap: _escolherFoto,
                child: Stack(
                  alignment: Alignment.bottomRight,
                  children: [
                    Container(
                      width: 76,
                      height: 76,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: AppColors.surfaceHigh,
                        border: Border.all(
                          color: AppColors.lime.withValues(alpha: 0.5),
                          width: 2,
                        ),
                      ),
                      child: _avatarPath != null
                          ? ClipOval(
                              child: Image.file(
                                File(_avatarPath!),
                                width: 76,
                                height: 76,
                                fit: BoxFit.cover,
                              ),
                            )
                          : Center(
                              child: Text(
                                _apelidoController.text.trim().isNotEmpty
                                    ? _apelidoController.text.trim()[0].toUpperCase()
                                    : '?',
                                style: TextStyle(
                                  fontSize: 28,
                                  fontWeight: FontWeight.w700,
                                  color: AppColors.lime,
                                ),
                              ),
                            ),
                    ),
                    Container(
                      padding: const EdgeInsets.all(5),
                      decoration: BoxDecoration(
                        color: AppColors.lime,
                        shape: BoxShape.circle,
                      ),
                      child: const Icon(
                        CupertinoIcons.camera_fill,
                        size: 13,
                        color: Colors.black,
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),
            TextField(
              key: const ValueKey('conta-apelido'),
              controller: _apelidoController,
              maxLength: 20,
              onChanged: (_) => setState(() => _erro = null),
              style: TextStyle(color: AppColors.onDark, fontSize: 15),
              decoration: InputDecoration(
                hintText: translate(context, 'Seu apelido criativo (ex: Pedro Motion)'),
                hintStyle: TextStyle(color: AppColors.muted),
                counterText: '',
                filled: true,
                fillColor: AppColors.surfaceHigh,
                prefixIcon: Icon(
                  CupertinoIcons.at,
                  size: 18,
                  color: AppColors.lime,
                ),
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(12),
                  borderSide: BorderSide.none,
                ),
              ),
            ),
            const SizedBox(height: 8),
            AppText('Este apelido é único e assinará seus vídeos e templates no mural.',
              style: TextStyle(fontSize: 11.5, color: AppColors.muted),
            ),
            const SizedBox(height: 20),
            FilledButton(
              key: const ValueKey('conta-salvar'),
              onPressed: _carregando ? null : _criarConta,
              style: FilledButton.styleFrom(
                backgroundColor: AppColors.lime,
                foregroundColor: Colors.black,
                padding: const EdgeInsets.symmetric(vertical: 14),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
              child: _carregando
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.black,
                      ),
                    )
                  : const AppText('Criar Minha Conta no Cloudflare',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
            ),
          ] else ...[
            // Formulário de Login por Código
            TextField(
              key: const ValueKey('conta-codigo'),
              controller: _codigoController,
              maxLength: 48,
              onChanged: (_) => setState(() => _erro = null),
              style: TextStyle(
                color: AppColors.onDark,
                fontSize: 14,
                fontFamily: 'monospace',
              ),
              decoration: InputDecoration(
                labelText: translate(context, 'Código de acesso (48 dígitos)'),
                labelStyle: TextStyle(color: AppColors.muted),
                hintText: translate(context, 'ex: 8f4e2b0c1a9d...'),
                hintStyle: TextStyle(color: AppColors.muted.withValues(alpha: .5)),
                counterText: '',
                filled: true,
                fillColor: AppColors.surfaceHigh,
                prefixIcon: Icon(
                  CupertinoIcons.lock_shield,
                  size: 18,
                  color: AppColors.lime,
                ),
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(12),
                  borderSide: BorderSide.none,
                ),
              ),
            ),
            const SizedBox(height: 8),
            AppText('Cole o código de 48 caracteres gerado na criação da sua conta.',
              style: TextStyle(fontSize: 11.5, color: AppColors.muted),
            ),
            const SizedBox(height: 20),
            FilledButton(
              key: const ValueKey('conta-entrar'),
              onPressed: _carregando ? null : _entrarComCodigo,
              style: FilledButton.styleFrom(
                backgroundColor: AppColors.lime,
                foregroundColor: Colors.black,
                padding: const EdgeInsets.symmetric(vertical: 14),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
              child: _carregando
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.black,
                      ),
                    )
                  : const AppText('Restaurar Minha Conta',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
            ),
          ],

          if (_erro != null) ...[
            const SizedBox(height: 14),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.red.withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: Colors.red.withValues(alpha: 0.4)),
              ),
              child: Row(
                children: [
                  const Icon(
                    CupertinoIcons.exclamationmark_triangle_fill,
                    color: Colors.redAccent,
                    size: 16,
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: AppText(
                      _erro!,
                      style: const TextStyle(
                        color: Colors.redAccent,
                        fontSize: 12.5,
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _construirPerfilReal(ContaDaComunidade conta, int projetosCount) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Cartão do Perfil
        Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: AppColors.surface,
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: AppColors.hairline),
          ),
          child: Column(
            children: [
              Center(
                child: Stack(
                  alignment: Alignment.bottomRight,
                  children: [
                    Container(
                      width: 88,
                      height: 88,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        gradient: LinearGradient(
                          colors: [AppColors.lime, AppColors.violet],
                        ),
                        boxShadow: [
                          BoxShadow(
                            color: AppColors.lime.withValues(alpha: 0.25),
                            blurRadius: 18,
                          ),
                        ],
                      ),
                      child: Padding(
                        padding: const EdgeInsets.all(3),
                        child: Container(
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: AppColors.background,
                          ),
                          child: conta.avatar != null &&
                                  File(conta.avatar!).existsSync()
                              ? ClipOval(
                                  child: Image.file(
                                    File(conta.avatar!),
                                    width: 82,
                                    height: 82,
                                    fit: BoxFit.cover,
                                  ),
                                )
                              : Center(
                                  child: AppText(
                                    conta.inicial,
                                    style: TextStyle(
                                      fontSize: 34,
                                      fontWeight: FontWeight.w700,
                                      color: AppColors.lime,
                                    ),
                                  ),
                                ),
                        ),
                      ),
                    ),
                    GestureDetector(
                      key: const ValueKey('conta-foto'),
                      onTap: _escolherFoto,
                      child: Container(
                        padding: const EdgeInsets.all(6),
                        decoration: BoxDecoration(
                          color: AppColors.lime,
                          shape: BoxShape.circle,
                        ),
                        child: const Icon(
                          CupertinoIcons.camera_fill,
                          size: 14,
                          color: Colors.black,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 14),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Text(
                    conta.apelido,
                    style: TextStyle(
                      fontSize: 22,
                      fontWeight: FontWeight.w700,
                      color: AppColors.onDark,
                    ),
                  ),
                  const SizedBox(width: 6),
                  Icon(
                    CupertinoIcons.checkmark_seal_fill,
                    size: 18,
                    color: AppColors.lime,
                  ),
                ],
              ),
              const SizedBox(height: 4),
              AppText(
                'Criador Áurea • Membro desde ${_formatarData(conta.criadaEm)}',
                style: TextStyle(fontSize: 12, color: AppColors.muted),
              ),
              const SizedBox(height: 14),
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 8),
                color: AppColors.surfaceHigh,
                borderRadius: BorderRadius.circular(20),
                onPressed: () => _editarApelido(context, conta),
                child: AppText('Editar apelido',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: AppColors.lime,
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // Cartão da Chave de Acesso Mestre Cloudflare
        Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: AppColors.surface,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: AppColors.lime.withValues(alpha: 0.35),
            ),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(
                    CupertinoIcons.lock,
                    size: 18,
                    color: AppColors.lime,
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: AppText('Código de Acesso Mestre',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
                        color: AppColors.onDark,
                      ),
                    ),
                  ),
                  GestureDetector(
                    onTap: () => setState(() => _codigoVisivel = !_codigoVisivel),
                    child: Icon(
                      _codigoVisivel
                          ? CupertinoIcons.eye_slash_fill
                          : CupertinoIcons.eye_fill,
                      size: 18,
                      color: AppColors.muted,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                decoration: BoxDecoration(
                  color: AppColors.background,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: AppText(
                  _codigoVisivel
                      ? conta.codigo
                      : '••••••••••••••••••••••••••••••••••••••••••••••••',
                  style: const TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 12.5,
                    color: Color(0xFF1ED6B1),
                    letterSpacing: 0.5,
                  ),
                ),
              ),
              const SizedBox(height: 10),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  key: const ValueKey('conta-copiar-codigo'),
                  icon: const Icon(CupertinoIcons.doc_on_clipboard, size: 16),
                  label: const AppText('Copiar código de acesso'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: AppColors.lime,
                    side: BorderSide(
                      color: AppColors.lime.withValues(alpha: 0.5),
                    ),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                  onPressed: () async {
                    await Clipboard.setData(ClipboardData(text: conta.codigo));
                    if (!mounted) return;
                    AureaSnack.show(
                      context,
                      'Código copiado! Salve-o num local seguro.',
                    );
                  },
                ),
              ),
              const SizedBox(height: 6),
              AppText('Guarde este código para acessar seu perfil em outros aparelhos.',
                style: TextStyle(fontSize: 11, color: AppColors.muted),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // Cartões de Estatísticas Reais
        Row(
          children: [
            Expanded(
              child: _CartaoEstatistica(
                icone: CupertinoIcons.film,
                valor: '$projetosCount',
                rotulo: projetosCount == 1 ? 'Projeto local' : 'Projetos locais',
                aoTocar: () => ref.read(homeTabProvider.notifier).state = 0,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _CartaoEstatistica(
                icone: CupertinoIcons.person_2_fill,
                valor: 'Mural',
                rotulo: 'Comunidade ativa',
                aoTocar: () => ref.read(homeTabProvider.notifier).state = 1,
              ),
            ),
          ],
        ),
        const SizedBox(height: 20),

        // Atalho para Publicar na Comunidade
        Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: AppColors.surface,
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: AppColors.hairline),
          ),
          child: Row(
            children: [
              Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: AppColors.lime.withValues(alpha: 0.15),
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  CupertinoIcons.share,
                  color: AppColors.lime,
                  size: 18,
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    AppText('Compartilhar criações',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: AppColors.onDark,
                      ),
                    ),
                    AppText('Mostre seus projetos na aba Comunidade.',
                      style: TextStyle(fontSize: 12, color: AppColors.muted),
                    ),
                  ],
                ),
              ),
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
                color: AppColors.surfaceHigh,
                borderRadius: BorderRadius.circular(14),
                onPressed: () => ref.read(homeTabProvider.notifier).state = 1,
                child: AppText('Ir',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AppColors.lime,
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 24),

        // Botão de Sair da Conta
        Center(
          child: CupertinoButton(
            key: const ValueKey('conta-sair'),
            padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 10),
            onPressed: () => _confirmarSaida(context),
            child: const Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(
                  CupertinoIcons.square_arrow_right,
                  size: 18,
                  color: Colors.redAccent,
                ),
                SizedBox(width: 8),
                AppText('Sair desta conta',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: Colors.redAccent,
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}

class _CartaoEstatistica extends StatelessWidget {
  const _CartaoEstatistica({
    required this.icone,
    required this.valor,
    required this.rotulo,
    this.aoTocar,
  });

  final IconData icone;
  final String valor;
  final String rotulo;
  final VoidCallback? aoTocar;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: aoTocar,
      behavior: HitTestBehavior.opaque,
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 18, horizontal: 12),
        decoration: BoxDecoration(
          color: AppColors.surface,
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: AppColors.hairline),
        ),
        child: Column(
          children: [
            Icon(icone, color: AppColors.lime, size: 22),
            const SizedBox(height: 8),
            AppText(
              valor,
              style: TextStyle(
                fontSize: 20,
                fontWeight: FontWeight.w700,
                color: AppColors.onDark,
              ),
            ),
            const SizedBox(height: 2),
            AppText(
              rotulo,
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 11.5, color: AppColors.muted),
            ),
          ],
        ),
      ),
    );
  }
}
