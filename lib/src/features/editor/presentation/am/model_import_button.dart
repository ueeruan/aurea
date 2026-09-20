import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../domain/model_import3d.dart';
import '../widgets/importacao_3d.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// Acrescenta um modelo do aparelho a uma cena que JA existe.
///
/// O que vem depois de ter os arquivos e o MESMO da folha de adicionar —
/// aviso de modelo pesado, os cinco mapas preparados, conferencia do motor
/// — porque os dois chamam [concluirImportacao3D]. Antes havia duas copias
/// desse trecho, e so esta preparava os cinco mapas.
class ModelImportButton extends ConsumerStatefulWidget {
  const ModelImportButton({
    super.key,
    required this.layerId,
    required this.onImported,
  });
  final String layerId;
  final ValueChanged<String> onImported;
  @override
  ConsumerState<ModelImportButton> createState() => _ModelImportButtonState();
}

class _ModelImportButtonState extends ConsumerState<ModelImportButton> {
  bool busy = false;
  String? status;
  Future<void> _import() async {
    if (busy) return;
    setState(() {
      busy = true;
      status = null;
    });
    try {
      final selection = await FilePicker.platform.pickFiles(
        allowMultiple: true,
        type: FileType.any,
        withData: false,
      );
      final paths =
          selection?.files.map((f) => f.path).whereType<String>().toList() ??
          [];
      if (paths.isEmpty || !mounted) return;
      final id = await concluirImportacao3D(
        context,
        ref,
        paths,
        playhead: Duration.zero,
        sceneId: widget.layerId,
      );
      if (id == null || id.isEmpty || !mounted) return;
      widget.onImported(id);
      setState(() => status = null);
    } on ModelImportException catch (e) {
      if (mounted) setState(() => status = e.message);
    } catch (_) {
      if (mounted) {
        setState(
          () => status = 'Nao foi possivel ler o modelo. Verifique os arquivos complementares e tente GLB.',
        );
      }
    } finally {
      if (mounted) setState(() => busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      OutlinedButton.icon(
        onPressed: busy ? null : _import,
        icon: busy
            ? const SizedBox.square(
                dimension: 18,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.file_open),
        label: AppText(
          busy ? 'Importando modelo...' : 'Importar GLB / glTF / OBJ / FBX / ZIP',
        ),
      ),
      const AppText('Selecione o modelo com .bin, .mtl e texturas — ou um .zip com tudo dentro.',
        style: TextStyle(fontSize: 11),
      ),
      if (status != null)
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: AppText(status!, style: const TextStyle(fontSize: 12)),
        ),
    ],
  );
}
