import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/model_import_service.dart';
import '../../application/texture_cache.dart';
import '../../domain/model_import3d.dart';

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
    final projectId = ref.read(editorControllerProvider).id;
    try {
      final selection = await FilePicker.platform.pickFiles(
        allowMultiple: true,
        type: FileType.any,
      );
      final paths =
          selection?.files.map((f) => f.path).whereType<String>().toList() ??
          [];
      if (paths.isEmpty || !mounted) return;
      final model = await readModel3DFiles(paths);
      if (!mounted || ref.read(editorControllerProvider).id != projectId) {
        return;
      }
      for (final m in model.data['materials'] as List) {
        if (!mounted || ref.read(editorControllerProvider).id != projectId) {
          return;
        }
        if (m['image'] != null &&
            !await TextureCache.instance.prepare(m['image'] as String)) {
          modelFail(
            'Uma textura nao pode ser decodificada. Use PNG/JPEG/WebP.',
          );
        }
      }
      if (!mounted || ref.read(editorControllerProvider).id != projectId) {
        return;
      }
      final id = ref
          .read(editorControllerProvider.notifier)
          .addModel3D(widget.layerId, model);
      if (id.isEmpty) return;
      widget.onImported(id);
      setState(
        () => status =
            '${model.triangleCount} triangulos · ${model.joints.length} ossos · ${model.clips.length} clipes. '
            '${model.warnings.join(' ')}',
      );
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
        label: Text(
          busy ? 'Importando modelo...' : 'Importar GLB / glTF / OBJ / FBX',
        ),
      ),
      const Text(
        'Selecione o modelo e, se necessario, .bin, .mtl e texturas juntos.',
        style: TextStyle(fontSize: 11),
      ),
      if (status != null)
        Padding(
          padding: const EdgeInsets.symmetric(vertical: 8),
          child: Text(status!, style: const TextStyle(fontSize: 12)),
        ),
    ],
  );
}
