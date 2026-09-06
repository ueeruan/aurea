import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../application/font_service.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// ESCOLHER A FONTE — e trazer a sua.
///
/// A fonte e metade da identidade de um video. Sem importar, a saida e
/// fazer o titulo em outro aplicativo e trazer como imagem — e ai o
/// texto deixa de ser texto: nao anima por letra, nao muda depois, nao
/// aceita o animador.
Future<void> showFontSheet(
    BuildContext context, WidgetRef ref, String layerId) async {
  await showParamSheet(
    context,
    title: 'Fonte',
    heightFactor: 0.55,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer is! TextLayer) return const SizedBox.shrink();

        final servico = FontService.instance;
        final fontes = servico.families;
        final atual = layer.fontFamily;

        Future<void> importar() async {
          final r = await FilePicker.platform.pickFiles(
            type: FileType.custom,
            allowedExtensions: const ['ttf', 'otf'],
          );
          final caminho = r?.files.single.path;
          if (caminho == null) return;
          final familia = await servico.import(caminho);
          if (!sheetContext.mounted) return;
          if (familia == null) {
            AureaSnack.show(
                sheetContext, 'Nao consegui ler essa fonte');
            return;
          }
          controller.editTextLayer(layerId, fontFamily: familia);
          setSheetState(() {});
          AureaSnack.show(sheetContext, 'Fonte "$familia" instalada');
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 18),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Fonte',
                    style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text)),
                const SizedBox(height: 4),
                const Text(
                  'Arquivos .ttf e .otf. A fonte e copiada para dentro do '
                  'Aurea — o projeto continua abrindo mesmo se o arquivo '
                  'original sumir.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),
                const SizedBox(height: 12),

                _Linha(
                  nome: 'Fonte do Aurea',
                  familia: null,
                  aceso: atual == null,
                  onTap: () {
                    controller.editTextLayer(layerId, clearFont: true);
                    setSheetState(() {});
                  },
                ),
                for (final f in fontes)
                  _Linha(
                    nome: f,
                    familia: f,
                    aceso: atual == f,
                    onTap: () {
                      controller.editTextLayer(layerId, fontFamily: f);
                      setSheetState(() {});
                    },
                    onRemove: servico.isBundled(f) ? null : () async {
                      await servico.remove(f);
                      if (atual == f) {
                        controller.editTextLayer(layerId, clearFont: true);
                      }
                      setSheetState(() {});
                    },
                  ),

                const SizedBox(height: 10),
                GestureDetector(
                  onTap: importar,
                  child: Container(
                    padding: const EdgeInsets.symmetric(vertical: 11),
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: AmColors.accentDim,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: const Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(CupertinoIcons.add, size: 15,
                            color: AmColors.accent),
                        SizedBox(width: 6),
                        Text('Importar fonte',
                            style: TextStyle(
                                fontSize: 13,
                                fontWeight: FontWeight.w600,
                                color: AmColors.accent)),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

class _Linha extends StatelessWidget {
  const _Linha({
    required this.nome,
    required this.familia,
    required this.aceso,
    required this.onTap,
    this.onRemove,
  });

  final String nome;
  final String? familia;
  final bool aceso;
  final VoidCallback onTap;
  final VoidCallback? onRemove;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: onTap,
        behavior: HitTestBehavior.opaque,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 9),
          child: Row(
            children: [
              Expanded(
                // O nome desenhado NA PROPRIA fonte: e como se escolhe
                // fonte de verdade, sem ficar aplicando para ver.
                child: Text(
                  nome,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 17,
                    fontFamily: resolveFontFamily(familia),
                    color: aceso ? AmColors.accent : AmColors.text,
                  ),
                ),
              ),
              if (aceso)
                const Icon(CupertinoIcons.checkmark_alt,
                    size: 16, color: AmColors.accent),
              if (onRemove != null)
                GestureDetector(
                  onTap: onRemove,
                  child: const Padding(
                    padding: EdgeInsets.only(left: 12),
                    child: Icon(CupertinoIcons.trash,
                        size: 15, color: AmColors.muted),
                  ),
                ),
            ],
          ),
        ),
      );
}
