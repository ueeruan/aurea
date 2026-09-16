import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/effect_preset_store.dart';
import '../../application/estilo_preset_store.dart';
import '../../domain/effect.dart';
import '../../domain/effect_preset.dart';
import '../../domain/estilo_preset.dart';
import '../../domain/layer_meta.dart';
import '../context/effects/previa_do_efeito.dart';
import 'am_colors.dart';

enum AbaDosPresets { efeitos, estilos }

/// A TELA DOS PRESETS: tudo que a pessoa guardou para usar de novo, com
/// a cara de cada um. Ate aqui dava para SALVAR um preset e nunca mais
/// achar — a lista nao aparecia em lugar nenhum.
Future<void> abrirTelaDePresets(
  BuildContext context, {
  required String layerId,
  required Duration at,
  AbaDosPresets aba = AbaDosPresets.efeitos,
}) => Navigator.of(context).push(
  MaterialPageRoute<void>(
    builder: (_) => TelaDePresets(layerId: layerId, at: at, aba: aba),
  ),
);

class TelaDePresets extends ConsumerStatefulWidget {
  const TelaDePresets({
    super.key,
    required this.layerId,
    required this.at,
    this.aba = AbaDosPresets.efeitos,
  });

  final String layerId;
  final Duration at;
  final AbaDosPresets aba;

  @override
  ConsumerState<TelaDePresets> createState() => _TelaDePresetsState();
}

class _TelaDePresetsState extends ConsumerState<TelaDePresets> {
  late AbaDosPresets _aba = widget.aba;
  String _busca = '';
  String _recado = '';

  @override
  void initState() {
    super.initState();
    EffectPresetStore.instance.load();
    EstiloPresetStore.instance.load();
  }

  bool _casa(String nome, Iterable<String> extras) {
    final q = _busca.trim().toLowerCase();
    if (q.isEmpty) return true;
    return nome.toLowerCase().contains(q) ||
        extras.any((e) => e.toLowerCase().contains(q));
  }

  void _avisar(String texto) {
    setState(() => _recado = texto);
  }

  /// EXPORTAR: o preset vira um arquivo que se manda para alguem.
  Future<void> _exportar(String nome, Map<String, dynamic> dados) async {
    final limpo = nome.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1f]'), '_');
    final bytes = Uint8List.fromList(
      utf8.encode(const JsonEncoder.withIndent('  ').convert(dados)),
    );
    try {
      final caminho = await FilePicker.platform.saveFile(
        dialogTitle: 'Salvar preset',
        fileName: '$limpo.aurea.json',
        type: FileType.custom,
        allowedExtensions: const ['json'],
        bytes: bytes,
      );
      if (caminho == null) return;
      if (!Platform.isAndroid && !Platform.isIOS) {
        await File(caminho).writeAsBytes(bytes, flush: true);
      }
      _avisar('"$nome" salvo no arquivo');
    } catch (_) {
      _avisar('Não deu para salvar o arquivo');
    }
  }

  /// IMPORTAR: um preset que veio de fora entra na lista da pessoa.
  Future<void> _importar() async {
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['json'],
        withData: true,
      );
      final arquivo = r?.files.firstOrNull;
      if (arquivo == null) return;
      final bruto = arquivo.bytes != null
          ? utf8.decode(arquivo.bytes!)
          : await File(arquivo.path!).readAsString();
      final m = jsonDecode(bruto);
      if (m is! Map<String, dynamic>) {
        _avisar('Esse arquivo não é um preset');
        return;
      }
      if (m.containsKey('estilos')) {
        await EstiloPresetStore.instance.add(estiloPresetFromJson(m));
        setState(() => _aba = AbaDosPresets.estilos);
        _avisar('Estilo importado');
      } else if (m.containsKey('effects')) {
        await EffectPresetStore.instance.add(effectPresetFromJson(m));
        setState(() => _aba = AbaDosPresets.efeitos);
        _avisar('Preset importado');
      } else {
        _avisar('Esse arquivo não é um preset');
      }
    } catch (_) {
      _avisar('Não deu para ler o arquivo');
    }
  }

  Future<String?> _pedirNome(String atual) => showDialog<String>(
    context: context,
    builder: (dialogo) {
      final campo = TextEditingController(text: atual);
      return AlertDialog(
        backgroundColor: AmColors.panel,
        title: const AppText(
          'Nome do preset',
          style: TextStyle(color: AmColors.text, fontSize: 16),
        ),
        content: TextField(
          key: const ValueKey('preset-nome-campo'),
          controller: campo,
          autofocus: true,
          style: const TextStyle(color: AmColors.text),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(dialogo).pop(),
            child: const AppText('Cancelar'),
          ),
          TextButton(
            key: const ValueKey('preset-nome-ok'),
            onPressed: () => Navigator.of(dialogo).pop(campo.text.trim()),
            child: const AppText('Salvar'),
          ),
        ],
      );
    },
  );

  Future<void> _menuDoPreset({
    required String nome,
    required bool meu,
    required VoidCallback aplicar,
    required Future<void> Function() exportar,
    Future<void> Function(String novo)? renomear,
    Future<void> Function()? excluir,
  }) async {
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: AppText(nome),
        actions: [
          CupertinoActionSheetAction(
            key: const ValueKey('preset-aplicar'),
            onPressed: () => Navigator.of(c).pop('aplicar'),
            child: const AppText('Aplicar nesta camada'),
          ),
          CupertinoActionSheetAction(
            key: const ValueKey('preset-exportar'),
            onPressed: () => Navigator.of(c).pop('exportar'),
            child: const AppText('Exportar para um arquivo'),
          ),
          if (meu && renomear != null)
            CupertinoActionSheetAction(
              key: const ValueKey('preset-renomear'),
              onPressed: () => Navigator.of(c).pop('renomear'),
              child: const AppText('Renomear'),
            ),
          if (meu && excluir != null)
            CupertinoActionSheetAction(
              key: const ValueKey('preset-excluir'),
              isDestructiveAction: true,
              onPressed: () => Navigator.of(c).pop('excluir'),
              child: const AppText('Excluir'),
            ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    switch (acao) {
      case 'aplicar':
        aplicar();
      case 'exportar':
        await exportar();
      case 'renomear':
        final novo = await _pedirNome(nome);
        if (novo != null && novo.isNotEmpty) await renomear!(novo);
        if (mounted) setState(() {});
      case 'excluir':
        await excluir!();
        if (mounted) setState(() {});
    }
  }

  void _aplicarEfeito(EffectPreset p) {
    final avisos = ref
        .read(editorControllerProvider.notifier)
        .applyPreset(widget.layerId, p, at: widget.at);
    Navigator.of(context).pop();
    if (avisos.isNotEmpty) _avisar(avisos.first);
  }

  void _aplicarEstilo(EstiloPreset e) {
    ref
        .read(editorControllerProvider.notifier)
        .aplicarEstilo(widget.layerId, e);
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AmColors.bg,
      body: SafeArea(
        child: Column(
          children: [
            Row(
              children: [
                IconButton(
                  key: const ValueKey('presets-voltar'),
                  onPressed: () => Navigator.of(context).pop(),
                  icon: const Icon(
                    CupertinoIcons.chevron_left,
                    color: AmColors.text,
                  ),
                ),
                const Expanded(
                  child: AppText(
                    'Presets',
                    style: TextStyle(
                      fontSize: 19,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
                IconButton(
                  key: const ValueKey('presets-importar'),
                  tooltip: 'Importar de um arquivo',
                  onPressed: _importar,
                  icon: const Icon(
                    CupertinoIcons.tray_arrow_down,
                    color: AmColors.text,
                    size: 21,
                  ),
                ),
              ],
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
              child: Row(
                children: [
                  for (final aba in AbaDosPresets.values)
                    Padding(
                      padding: const EdgeInsets.only(right: 6),
                      child: Tocavel(
                        key: ValueKey('presets-aba-${aba.name}'),
                        onTap: () => setState(() => _aba = aba),
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 14,
                            vertical: 8,
                          ),
                          decoration: BoxDecoration(
                            color: _aba == aba
                                ? AmColors.accent.withValues(alpha: .22)
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: AppText(
                            aba == AbaDosPresets.efeitos
                                ? 'Efeitos'
                                : 'Estilos',
                            style: TextStyle(
                              fontSize: 13,
                              fontWeight: FontWeight.w600,
                              color: _aba == aba
                                  ? AmColors.accent
                                  : AmColors.text,
                            ),
                          ),
                        ),
                      ),
                    ),
                  Expanded(
                    child: Container(
                      height: 34,
                      padding: const EdgeInsets.symmetric(horizontal: 10),
                      decoration: BoxDecoration(
                        color: AmColors.chip,
                        borderRadius: BorderRadius.circular(9),
                      ),
                      child: Row(
                        children: [
                          const Icon(
                            CupertinoIcons.search,
                            size: 15,
                            color: AmColors.muted,
                          ),
                          const SizedBox(width: 6),
                          Expanded(
                            child: TextField(
                              key: const ValueKey('presets-busca'),
                              onChanged: (v) => setState(() => _busca = v),
                              style: const TextStyle(
                                color: AmColors.text,
                                fontSize: 13,
                              ),
                              decoration: const InputDecoration.collapsed(
                                hintText: 'Buscar',
                                hintStyle: TextStyle(
                                  color: AmColors.muted,
                                  fontSize: 13,
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
            if (_recado.isNotEmpty)
              Padding(
                padding: const EdgeInsets.fromLTRB(14, 0, 14, 6),
                child: AppText(
                  _recado,
                  style: const TextStyle(fontSize: 12, color: AmColors.accent),
                ),
              ),
            Expanded(
              child: _aba == AbaDosPresets.efeitos
                  ? _listaDeEfeitos()
                  : _listaDeEstilos(),
            ),
          ],
        ),
      ),
    );
  }

  Widget _grade(List<Widget> cartoes) {
    if (cartoes.isEmpty) {
      return const Center(
        child: Padding(
          padding: EdgeInsets.all(24),
          child: AppText(
            'Nada aqui ainda. Salve um preset a partir de uma camada e ele aparece nesta lista, em todos os projetos.',
            textAlign: TextAlign.center,
            style: TextStyle(color: AmColors.muted, fontSize: 13),
          ),
        ),
      );
    }
    return GridView.count(
      padding: const EdgeInsets.fromLTRB(12, 0, 12, 16),
      crossAxisCount: 2,
      childAspectRatio: .82,
      mainAxisSpacing: 10,
      crossAxisSpacing: 10,
      children: cartoes,
    );
  }

  Widget _listaDeEfeitos() => ValueListenableBuilder<int>(
    valueListenable: EffectPresetStore.instance.revision,
    builder: (context, _, _) {
      final meus = EffectPresetStore.instance.presets;
      final todos = [...meus, ...factoryPresets()];
      return _grade([
        for (final p in todos)
          if (_casa(p.name, [p.category, ...p.tags]))
            _CartaoDoPreset(
              key: ValueKey('preset-efeito-${p.id}'),
              nome: p.name,
              detalhe: p.effects.length == 1
                  ? '1 efeito'
                  : '${p.effects.length} efeitos',
              meu: !p.builtIn,
              previa: _previaDoPreset(p),
              onTap: () => _aplicarEfeito(p),
              onMenu: () => _menuDoPreset(
                nome: p.name,
                meu: !p.builtIn,
                aplicar: () => _aplicarEfeito(p),
                exportar: () => _exportar(p.name, effectPresetToJson(p)),
                renomear: (novo) =>
                    EffectPresetStore.instance.rename(p.id, novo),
                excluir: () => EffectPresetStore.instance.remove(p.id),
              ),
            ),
      ]);
    },
  );

  Widget _previaDoPreset(EffectPreset p) {
    final tipo = p.effects.firstOrNull?.type;
    if (tipo == null || effectSpecs[tipo] == null) {
      return const ColoredBox(color: AmColors.panelHigh);
    }
    return LayoutBuilder(
      builder: (context, c) => PreviaDoEfeito(tipo: tipo, lado: c.maxWidth),
    );
  }

  Widget _listaDeEstilos() => ValueListenableBuilder<int>(
    valueListenable: EstiloPresetStore.instance.revisao,
    builder: (context, _, _) {
      final todos = [
        ...EstiloPresetStore.instance.estilos,
        ...estilosDeFabrica(),
      ];
      return _grade([
        for (final e in todos)
          if (_casa(e.nome, e.partes))
            _CartaoDoPreset(
              key: ValueKey('preset-estilo-${e.id}'),
              nome: e.nome,
              detalhe: e.partes.isEmpty
                  ? 'sem acabamento'
                  : e.partes.join(' · '),
              meu: !e.deFabrica,
              previa: CustomPaint(painter: _PintorDoEstilo(e.estilos)),
              onTap: () => _aplicarEstilo(e),
              onMenu: () => _menuDoPreset(
                nome: e.nome,
                meu: !e.deFabrica,
                aplicar: () => _aplicarEstilo(e),
                exportar: () => _exportar(e.nome, estiloPresetToJson(e)),
                renomear: (novo) =>
                    EstiloPresetStore.instance.renomear(e.id, novo),
                excluir: () => EstiloPresetStore.instance.remove(e.id),
              ),
            ),
      ]);
    },
  );
}

class _CartaoDoPreset extends StatelessWidget {
  const _CartaoDoPreset({
    super.key,
    required this.nome,
    required this.detalhe,
    required this.meu,
    required this.previa,
    required this.onTap,
    required this.onMenu,
  });

  final String nome;
  final String detalhe;
  final bool meu;
  final Widget previa;
  final VoidCallback onTap;
  final VoidCallback onMenu;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: onTap,
    onLongPress: onMenu,
    child: Container(
      decoration: BoxDecoration(
        color: AmColors.panel,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: ClipRRect(
              borderRadius: const BorderRadius.vertical(
                top: Radius.circular(12),
              ),
              child: previa,
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(9, 7, 3, 7),
            child: Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      AppText(
                        nome,
                        maxLines: 1,
                        style: const TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w600,
                          color: AmColors.text,
                        ),
                      ),
                      AppText(
                        detalhe,
                        maxLines: 1,
                        style: const TextStyle(
                          fontSize: 11,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                ),
                Tocavel(
                  onTap: onMenu,
                  child: const Padding(
                    padding: EdgeInsets.all(6),
                    child: Icon(
                      CupertinoIcons.ellipsis,
                      size: 16,
                      color: AmColors.muted,
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    ),
  );
}

/// A CARA DE UM ESTILO: um retangulo com o acabamento por cima, que e a
/// unica forma honesta de mostrar sombra, brilho e borda.
class _PintorDoEstilo extends CustomPainter {
  const _PintorDoEstilo(this.estilos);

  final LayerStyles estilos;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0xFF1E242E),
    );
    final lado = math.min(size.width, size.height) * .52;
    final corpo = RRect.fromRectAndRadius(
      Rect.fromCenter(
        center: size.center(Offset.zero),
        width: lado,
        height: lado,
      ),
      Radius.circular(lado * .18),
    );
    const t = Duration.zero;
    final brilho = estilos.outerGlow;
    if (brilho != null && brilho.enabled) {
      canvas.drawRRect(
        corpo,
        Paint()
          ..color = brilho.color.withValues(
            alpha: brilho.opacity.valueAt(t).clamp(0.0, 1.0),
          )
          ..maskFilter = MaskFilter.blur(
            BlurStyle.normal,
            math.max(1, brilho.size.valueAt(t) * .5),
          ),
      );
    }
    final sombra = estilos.dropShadow;
    if (sombra != null && sombra.enabled) {
      canvas.drawRRect(
        corpo.shift(sombra.offsetAt(t) * .5),
        Paint()
          ..color = sombra.color.withValues(
            alpha: sombra.opacity.valueAt(t).clamp(0.0, 1.0),
          )
          ..maskFilter = MaskFilter.blur(
            BlurStyle.normal,
            math.max(1, sombra.size.valueAt(t) * .5),
          ),
      );
    }
    // As bordas de fora para dentro, para a primeira ficar na frente.
    for (final borda in estilos.bordas.reversed) {
      if (!borda.enabled) continue;
      canvas.drawRRect(
        corpo,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = math.max(1, borda.width.valueAt(t) * .6)
          ..color = borda.color.withValues(
            alpha: borda.opacity.valueAt(t).clamp(0.0, 1.0),
          ),
      );
    }
    final cobertura = estilos.colorOverlay;
    canvas.drawRRect(
      corpo,
      Paint()
        ..color = cobertura != null && cobertura.enabled
            ? cobertura.color.withValues(
                alpha: cobertura.opacity.valueAt(t).clamp(0.0, 1.0),
              )
            : const Color(0xFF8C93A1),
    );
    final interna = estilos.innerShadow;
    if (interna != null && interna.enabled) {
      canvas
        ..save()
        ..clipRRect(corpo)
        ..drawRRect(
          corpo.shift(interna.offsetAt(t) * .5).inflate(2),
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = math.max(2, interna.size.valueAt(t) * .6)
            ..color = interna.color.withValues(
              alpha: interna.opacity.valueAt(t).clamp(0.0, 1.0),
            )
            ..maskFilter = MaskFilter.blur(
              BlurStyle.normal,
              math.max(1, interna.size.valueAt(t) * .4),
            ),
        )
        ..restore();
    }
  }

  @override
  bool shouldRepaint(_PintorDoEstilo old) => true;
}
