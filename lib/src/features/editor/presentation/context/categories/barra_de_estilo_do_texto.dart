import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../../core/storage/prefs.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../domain/layer.dart';
import '../../am/am_colors.dart';
import '../../am/color_picker_sheet.dart';
import '../../am/font_sheet.dart';
import '../parameter_row.dart';

/// A fonte que o aplicativo usa quando a camada nao escolheu nenhuma.
const fonteDoAplicativo = 'Aurea Motion Sans';

const _chaveDasFavoritas = 'fontes.favoritas';
const _chaveDasRecentes = 'fontes.recentes';

/// Quantas fontes usadas por ultimo ficam na lista de recentes.
const maximoDeRecentes = 8;

/// Sem preferencias (testes), favoritas e recentes vivem aqui.
final List<String> _favoritasNaMemoria = [];
final List<String> _recentesNaMemoria = [];

SharedPreferences? _prefs(WidgetRef ref) {
  try {
    return ref.read(sharedPreferencesProvider);
  } catch (_) {
    return null;
  }
}

List<String> fontesFavoritas(WidgetRef ref) =>
    _prefs(ref)?.getStringList(_chaveDasFavoritas) ?? [..._favoritasNaMemoria];

List<String> fontesRecentes(WidgetRef ref) =>
    _prefs(ref)?.getStringList(_chaveDasRecentes) ?? [..._recentesNaMemoria];

void _gravar(WidgetRef ref, String chave, List<String> memoria, List<String> v) {
  final prefs = _prefs(ref);
  if (prefs == null) {
    memoria
      ..clear()
      ..addAll(v);
    return;
  }
  prefs.setStringList(chave, v);
}

/// A ESTRELA de uma fonte: poe ou tira das favoritas.
void alternarFonteFavorita(WidgetRef ref, String familia) {
  final lista = fontesFavoritas(ref);
  if (!lista.remove(familia)) lista.add(familia);
  _gravar(ref, _chaveDasFavoritas, _favoritasNaMemoria, lista);
}

/// A fonte usada agora vai para o topo das recentes.
void registrarFonteUsada(WidgetRef ref, String familia) {
  final lista = fontesRecentes(ref)
    ..remove(familia)
    ..insert(0, familia);
  if (lista.length > maximoDeRecentes) {
    lista.removeRange(maximoDeRecentes, lista.length);
  }
  _gravar(ref, _chaveDasRecentes, _recentesNaMemoria, lista);
}

/// O proximo alinhamento do botao que cicla.
TextAlign proximoAlinhamento(TextAlign a) => switch (a) {
  TextAlign.left || TextAlign.start => TextAlign.center,
  TextAlign.center => TextAlign.right,
  _ => TextAlign.left,
};

/// A BARRA DE ESTILO DO TEXTO: alinhar (cicla), fonte (mini navegador com
/// favoritas e recentes), tamanho (regua ao vivo), cor e concluir.
class BarraDeEstiloDoTexto extends ConsumerWidget {
  const BarraDeEstiloDoTexto({
    super.key,
    required this.layer,
    required this.playback,
  });

  final TextLayer layer;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final id = layer.id;
    return SizedBox(
      height: 48,
      child: Row(
        children: [
          _Botao(
            chave: 'texto-alinhar',
            dica: 'Alinhamento',
            onTap: () => c.editTextLayer(
              id,
              alinhamento: proximoAlinhamento(layer.alinhamento),
            ),
            child: Icon(
              switch (layer.alinhamento) {
                TextAlign.left || TextAlign.start => CupertinoIcons.text_alignleft,
                TextAlign.right || TextAlign.end => CupertinoIcons.text_alignright,
                _ => CupertinoIcons.text_aligncenter,
              },
              size: 20,
              color: AmColors.text,
            ),
          ),
          Expanded(
            flex: 3,
            child: _Botao(
              chave: 'texto-fonte-rapida',
              dica: 'Fonte',
              onTap: () {
                playback.pause();
                mostrarMiniNavegadorDeFontes(context, ref, id);
              },
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    'Aa',
                    style: TextStyle(
                      fontFamily: resolveFontFamily(layer.fontFamily),
                      fontSize: 16,
                      fontWeight: layer.bold ? FontWeight.w700 : FontWeight.w400,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(width: 6),
                  Flexible(
                    child: Text(
                      layer.fontFamily ?? fonteDoAplicativo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 12,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
          Expanded(
            flex: 2,
            child: _Botao(
              chave: 'texto-tamanho-rapido',
              dica: 'Tamanho',
              onTap: () {
                playback.pause();
                mostrarTamanhoDoTexto(context, ref, id);
              },
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Icon(
                      CupertinoIcons.textformat_size,
                      size: 16,
                      color: AmColors.muted,
                    ),
                    const SizedBox(width: 6),
                    Text(
                      layer.fontSize.round().toString(),
                      style: const TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text,
                        fontFeatures: [FontFeature.tabularFigures()],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
          _Botao(
            chave: 'texto-cor-rapida',
            dica: 'Cor',
            onTap: () async {
              playback.pause();
              final escolhida = await showColorPicker(
                context,
                initial: layer.color,
                onChanged: (cor) => c.editTextLayer(id, color: cor),
              );
              if (escolhida != null) c.editTextLayer(id, color: escolhida);
            },
            child: Container(
              width: 22,
              height: 22,
              decoration: BoxDecoration(
                color: layer.color,
                shape: BoxShape.circle,
                border: Border.all(color: Colors.white38, width: 1.5),
              ),
            ),
          ),
          _Botao(
            chave: 'texto-concluir',
            dica: 'Concluir',
            onTap: () {
              FocusScope.of(context).unfocus();
              ref.read(editorSessionProvider.notifier).closePanel();
            },
            child: const Icon(
              CupertinoIcons.checkmark_alt,
              size: 20,
              color: AmColors.accent,
            ),
          ),
        ],
      ),
    );
  }
}

class _Botao extends StatelessWidget {
  const _Botao({
    required this.chave,
    required this.dica,
    required this.onTap,
    required this.child,
  });

  final String chave;
  final String dica;
  final VoidCallback onTap;
  final Widget child;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 3, vertical: 5),
    child: Tooltip(
      message: dica,
      child: Tocavel(
        key: ValueKey(chave),
        onTap: onTap,
        child: Container(
          constraints: const BoxConstraints(minWidth: 44),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(10),
          ),
          child: child,
        ),
      ),
    ),
  );
}

/// O TAMANHO NUMA FOLHA CURTA: regua ao vivo e tamanhos prontos.
Future<void> mostrarTamanhoDoTexto(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) => showModalBottomSheet<void>(
  context: context,
  backgroundColor: AmColors.panel,
  barrierColor: Colors.black26,
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
  ),
  builder: (folha) => Consumer(
    builder: (folha, ref, _) {
      final layer = ref.watch(editorControllerProvider).layerById(layerId);
      if (layer is! TextLayer) return const SizedBox.shrink();
      final c = ref.read(editorControllerProvider.notifier);
      return SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText(
                'Tamanho do texto',
                style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 8),
              ParameterRow(
                label: 'Tamanho',
                value: layer.fontSize,
                min: 6,
                max: 400,
                unitsPerPixel: .5,
                decimals: 0,
                unit: ' pt',
                valueKey: const ValueKey('texto-tamanho-folha'),
                onChanged: (v) => c.editTextLayer(layerId, fontSize: v),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final t in const [24, 48, 72, 120, 200, 300])
                    Tocavel(
                      key: ValueKey('texto-tamanho-pronto-$t'),
                      onTap: () =>
                          c.editTextLayer(layerId, fontSize: t.toDouble()),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 14,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          color: layer.fontSize.round() == t
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(16),
                        ),
                        child: Text(
                          '$t',
                          style: TextStyle(
                            fontSize: 13,
                            color: layer.fontSize.round() == t
                                ? AmColors.accent
                                : AmColors.text,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ],
          ),
        ),
      );
    },
  ),
);

/// O MINI NAVEGADOR DE FONTES: favoritas (estrela), recentes (relogio) e
/// todas, cada nome desenhado na propria fonte; "Ver todas" abre a folha
/// completa, onde tambem se importam fontes.
Future<void> mostrarMiniNavegadorDeFontes(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final abrirCompleta = await showModalBottomSheet<bool>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AmColors.panel,
    barrierColor: Colors.black26,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (folha) => ConstrainedBox(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(folha).height * .62,
      ),
      child: _MiniNavegadorDeFontes(layerId: layerId, ref: ref),
    ),
  );
  if (abrirCompleta == true && context.mounted) {
    await showFontSheet(context, ref, layerId);
  }
}

class _MiniNavegadorDeFontes extends StatefulWidget {
  const _MiniNavegadorDeFontes({required this.layerId, required this.ref});

  final String layerId;
  final WidgetRef ref;

  @override
  State<_MiniNavegadorDeFontes> createState() => _MiniNavegadorDeFontesState();
}

class _MiniNavegadorDeFontesState extends State<_MiniNavegadorDeFontes> {
  WidgetRef get ref => widget.ref;

  @override
  Widget build(BuildContext context) {
    final layer = ref.read(editorControllerProvider).layerById(widget.layerId);
    if (layer is! TextLayer) return const SizedBox.shrink();
    final todas = FontService.instance.families;
    final favoritas = [
      for (final f in fontesFavoritas(ref))
        if (todas.contains(f)) f,
    ];
    final recentes = [
      for (final f in fontesRecentes(ref))
        if (todas.contains(f)) f,
    ];
    final atual = layer.fontFamily ?? fonteDoAplicativo;
    final amostra = layer.text.trim().isEmpty ? 'Aa Bb Cc 123' : layer.text;

    void escolher(String familia) {
      final c = ref.read(editorControllerProvider.notifier);
      if (familia == fonteDoAplicativo) {
        c.editTextLayer(widget.layerId, clearFont: true);
      } else {
        c.editTextLayer(widget.layerId, fontFamily: familia);
      }
      registrarFonteUsada(ref, familia);
      Navigator.of(context).pop(false);
    }

    Widget linha(String secao, String familia, {IconData? icone}) => Tocavel(
      key: ValueKey('fonte-$secao-$familia'),
      onTap: () => escolher(familia),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Row(
          children: [
            if (icone != null) ...[
              Icon(icone, size: 14, color: AmColors.muted),
              const SizedBox(width: 8),
            ],
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    amostra,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 18,
                      fontFamily: resolveFontFamily(familia),
                      color: familia == atual ? AmColors.accent : AmColors.text,
                    ),
                  ),
                  Text(
                    familia,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                ],
              ),
            ),
            if (familia == atual)
              const Padding(
                padding: EdgeInsets.only(right: 4),
                child: Icon(
                  CupertinoIcons.checkmark_alt,
                  size: 16,
                  color: AmColors.accent,
                ),
              ),
            IconButton(
              key: ValueKey('fonte-estrela-$secao-$familia'),
              tooltip: favoritas.contains(familia)
                  ? 'Tirar das favoritas'
                  : 'Favoritar',
              onPressed: () {
                alternarFonteFavorita(ref, familia);
                setState(() {});
              },
              icon: Icon(
                favoritas.contains(familia)
                    ? CupertinoIcons.star_fill
                    : CupertinoIcons.star,
                size: 18,
                color: favoritas.contains(familia)
                    ? AmColors.action
                    : AmColors.muted,
              ),
            ),
          ],
        ),
      ),
    );

    Widget titulo(String texto) => Padding(
      padding: const EdgeInsets.only(top: 10, bottom: 2),
      child: AppText(
        texto,
        style: const TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w700,
          color: AmColors.muted,
        ),
      ),
    );

    return SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 8, 4),
            child: Row(
              children: [
                const Expanded(
                  child: AppText(
                    'Fontes',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
                CupertinoButton(
                  key: const ValueKey('fontes-ver-todas'),
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  onPressed: () => Navigator.of(context).pop(true),
                  child: const AppText(
                    'Ver todas e importar',
                    style: TextStyle(fontSize: 13, color: AmColors.accent),
                  ),
                ),
              ],
            ),
          ),
          Flexible(
            child: ListView(
              key: const ValueKey('mini-navegador-de-fontes'),
              shrinkWrap: true,
              padding: const EdgeInsets.fromLTRB(16, 0, 8, 16),
              children: [
                if (favoritas.isNotEmpty) ...[
                  titulo('Favoritas'),
                  for (final f in favoritas)
                    linha('fav', f, icone: CupertinoIcons.star_fill),
                ],
                if (recentes.isNotEmpty) ...[
                  titulo('Recentes'),
                  for (final f in recentes)
                    linha('rec', f, icone: CupertinoIcons.clock),
                ],
                titulo('Todas'),
                for (final f in todas) linha('todas', f),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
