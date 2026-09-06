import 'dart:io' show Platform;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../../core/ui/snack.dart';
import '../../editor/presentation/am/am_colors.dart';

/// Dados do criador — usados no Sobre e no envio do relato.
class AureaAutor {
  AureaAutor._();

  static const nome = 'Ruanzitwo';
  static const email = 'ruanpablombl@gmail.com';
  static const instagram = 'ofruanzitwo';
  static const tiktok = 'ruanzitwo';
  static const versao = '1.2.0 (35) · FX V2 beta';
}

enum _Tipo { bug, ferramenta, efeito, outro }

String _tipoLabel(_Tipo t) => switch (t) {
      _Tipo.bug => 'Erro / bug',
      _Tipo.ferramenta => 'Nova ferramenta',
      _Tipo.efeito => 'Novo efeito',
      _Tipo.outro => 'Outro',
    };

/// REPORTAR: o relato vai direto para o e-mail do criador, com os dados
/// do aparelho ja preenchidos — que e o que normalmente falta num
/// relato de bug e obriga a ficar perguntando.
Future<void> showReportSheet(BuildContext context) async {
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (_) => const _ReportSheet(),
  );
}

class _ReportSheet extends StatefulWidget {
  const _ReportSheet();

  @override
  State<_ReportSheet> createState() => _ReportSheetState();
}

class _ReportSheetState extends State<_ReportSheet> {
  _Tipo _tipo = _Tipo.bug;
  final _texto = TextEditingController();
  final _passos = TextEditingController();
  bool _incluirAparelho = true;

  @override
  void dispose() {
    _texto.dispose();
    _passos.dispose();
    super.dispose();
  }

  String get _assunto => 'Aurea ${AureaAutor.versao} — ${_tipoLabel(_tipo)}';

  String get _corpo {
    final b = StringBuffer()
      ..writeln(_texto.text.trim())
      ..writeln();
    if (_tipo == _Tipo.bug && _passos.text.trim().isNotEmpty) {
      b
        ..writeln('Como acontece:')
        ..writeln(_passos.text.trim())
        ..writeln();
    }
    if (_incluirAparelho) {
      b
        ..writeln('---')
        ..writeln('Aurea ${AureaAutor.versao}')
        ..writeln('Sistema: ${Platform.operatingSystem} '
            '${Platform.operatingSystemVersion}');
    }
    return b.toString();
  }

  Future<void> _enviar() async {
    if (_texto.text.trim().isEmpty) {
      AureaSnack.show(context, 'Escreva o que aconteceu antes de enviar');
      return;
    }
    final uri = Uri(
      scheme: 'mailto',
      path: AureaAutor.email,
      queryParameters: {'subject': _assunto, 'body': _corpo},
    );
    var abriu = false;
    try {
      abriu = await launchUrl(uri, mode: LaunchMode.externalApplication);
    } catch (_) {
      abriu = false;
    }
    if (!mounted) return;
    if (abriu) {
      Navigator.of(context).pop();
      AureaSnack.show(context, 'Abrindo seu app de e-mail');
      return;
    }
    // Sem app de e-mail: o relato nao pode simplesmente sumir.
    await Clipboard.setData(
        ClipboardData(text: '$_assunto\n\n$_corpo'));
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Sem app de e-mail. Copiei o relato — cole em '
      '${AureaAutor.email}',
      duration: const Duration(seconds: 6),
    );
  }

  Future<void> _abrir(String url) async {
    try {
      await launchUrl(Uri.parse(url),
          mode: LaunchMode.externalApplication);
    } catch (_) {
      if (!mounted) return;
      AureaSnack.show(context, 'Nao consegui abrir o link');
    }
  }

  @override
  Widget build(BuildContext context) {
    final maxH = MediaQuery.of(context).size.height * 0.85;
    return SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(maxHeight: maxH),
        child: SingleChildScrollView(
          padding: EdgeInsets.fromLTRB(
              20, 16, 20, 20 + MediaQuery.of(context).viewInsets.bottom),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const Icon(CupertinoIcons.exclamationmark_bubble,
                      size: 20, color: AmColors.accent),
                  const SizedBox(width: 8),
                  const Text('Reportar',
                      style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text)),
                  const Spacer(),
                  CupertinoButton(
                    padding: EdgeInsets.zero,
                    onPressed: () => Navigator.of(context).pop(),
                    child: const Icon(CupertinoIcons.xmark,
                        size: 18, color: AmColors.muted),
                  ),
                ],
              ),
              const SizedBox(height: 4),
              const Text(
                'Erro, sugestao de ferramenta ou de efeito — vai direto '
                'para o criador do app.',
                style: TextStyle(
                    fontSize: 12, height: 1.35, color: AmColors.muted),
              ),
              const SizedBox(height: 14),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final t in _Tipo.values)
                    GestureDetector(
                      onTap: () => setState(() => _tipo = t),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 8),
                        decoration: BoxDecoration(
                          color: t == _tipo
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: Text(_tipoLabel(t),
                            style: TextStyle(
                                fontSize: 12,
                                color: t == _tipo
                                    ? AmColors.accent
                                    : AmColors.muted)),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 14),
              _campo(
                controller: _texto,
                hint: _tipo == _Tipo.bug
                    ? 'O que aconteceu?'
                    : 'O que voce gostaria que existisse?',
                lines: 4,
              ),
              if (_tipo == _Tipo.bug) ...[
                const SizedBox(height: 10),
                _campo(
                  controller: _passos,
                  hint: 'Como fazer acontecer de novo (opcional)',
                  lines: 3,
                ),
              ],
              const SizedBox(height: 10),
              Row(
                children: [
                  Expanded(
                    child: Text(
                      'Incluir modelo e versao do sistema',
                      style: const TextStyle(
                          fontSize: 12, color: AmColors.muted),
                    ),
                  ),
                  CupertinoSwitch(
                    value: _incluirAparelho,
                    activeTrackColor: AmColors.accent,
                    onChanged: (v) =>
                        setState(() => _incluirAparelho = v),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              SizedBox(
                width: double.infinity,
                child: CupertinoButton(
                  color: AmColors.accent,
                  borderRadius: BorderRadius.circular(12),
                  onPressed: _enviar,
                  child: const Text('Enviar',
                      style: TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                          color: Color(0xFF10151D))),
                ),
              ),
              const SizedBox(height: 16),
              const Divider(color: AmColors.hairline, height: 1),
              const SizedBox(height: 14),
              const Text('Criador',
                  style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text)),
              const SizedBox(height: 8),
              _link(
                icon: CupertinoIcons.person_crop_circle,
                label: AureaAutor.nome,
                sub: AureaAutor.email,
                onTap: () => _abrir('mailto:${AureaAutor.email}'),
              ),
              _link(
                icon: CupertinoIcons.camera,
                label: 'Instagram',
                sub: '@${AureaAutor.instagram}',
                onTap: () => _abrir(
                    'https://instagram.com/${AureaAutor.instagram}'),
              ),
              _link(
                icon: CupertinoIcons.music_note_2,
                label: 'TikTok',
                sub: '@${AureaAutor.tiktok}',
                onTap: () =>
                    _abrir('https://tiktok.com/@${AureaAutor.tiktok}'),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _campo({
    required TextEditingController controller,
    required String hint,
    required int lines,
  }) =>
      CupertinoTextField(
        controller: controller,
        placeholder: hint,
        maxLines: lines,
        minLines: lines,
        placeholderStyle:
            const TextStyle(fontSize: 13, color: AmColors.muted),
        style: const TextStyle(fontSize: 14, color: AmColors.text),
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(12),
        ),
      );

  Widget _link({
    required IconData icon,
    required String label,
    required String sub,
    required VoidCallback onTap,
  }) =>
      GestureDetector(
        onTap: onTap,
        behavior: HitTestBehavior.opaque,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 9),
          child: Row(
            children: [
              Icon(icon, size: 18, color: AmColors.accent),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(label,
                        style: const TextStyle(
                            fontSize: 13, color: AmColors.text)),
                    Text(sub,
                        style: const TextStyle(
                            fontSize: 11, color: AmColors.muted)),
                  ],
                ),
              ),
              const Icon(CupertinoIcons.chevron_right,
                  size: 14, color: AmColors.muted),
            ],
          ),
        ),
      );
}

/// AVISO DE BETA: honesto e discreto, com o caminho para reportar logo
/// ali — reclamar de um bug so ajuda se for facil contar.
class BetaBanner extends StatelessWidget {
  const BetaBanner({super.key, this.compact = false});

  final bool compact;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => showReportSheet(context),
      child: Container(
        padding: EdgeInsets.symmetric(
            horizontal: 14, vertical: compact ? 10 : 14),
        decoration: BoxDecoration(
          color: const Color(0x22FFB020),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: const Color(0x55FFB020)),
        ),
        child: Row(
          children: [
            const Icon(CupertinoIcons.exclamationmark_triangle,
                size: 18, color: Color(0xFFFFB020)),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'Versao beta para testes',
                    style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w700,
                        color: Color(0xFFFFB020)),
                  ),
                  if (!compact) ...[
                    const SizedBox(height: 3),
                    const Text(
                      'Pode ter erros, travar ou perder alteracoes nao '
                      'salvas. Achou um problema ou quer sugerir algo? '
                      'Toque aqui.',
                      style: TextStyle(
                          fontSize: 11,
                          height: 1.35,
                          color: AmColors.muted),
                    ),
                  ],
                ],
              ),
            ),
            const SizedBox(width: 8),
            const Icon(CupertinoIcons.chevron_right,
                size: 14, color: Color(0xFFFFB020)),
          ],
        ),
      ),
    );
  }
}
