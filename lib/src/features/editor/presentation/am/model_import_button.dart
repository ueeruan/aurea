import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/model_import_service.dart';
import '../../application/texture_cache.dart';
import '../../domain/model_import3d.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

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
        withData: false,
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
        // OS CINCO MAPAS SAO PREPARADOS ANTES DE A CAMADA NASCER.
        //
        // O `prepare` deixa a imagem no cache do pintor e o `prepareRgba`
        // deixa os pixels prontos para a placa. Sem o segundo, o modelo
        // nasceria sem mapa nenhum e so se vestiria um quadro depois — o
        // dono veria um cinza aparecer e virar textura, que parece defeito.
        //
        // SO A TEXTURA DE COR DERRUBA A IMPORTACAO. Um relevo ilegivel e um
        // modelo sem relevo, e nao um modelo que nao entra: recusar o
        // arquivo inteiro por causa de um mapa secundario seria trocar um
        // resultado bom por nenhum.
        for (final chave in const [
          'image',
          'normalImage',
          'metalRoughImage',
          'emissiveImage',
          'occlusionImage',
        ]) {
          final caminho = m[chave];
          if (caminho is! String || caminho.isEmpty) continue;
          final deuCerto =
              await TextureCache.instance.prepare(caminho) &&
              await TextureCache.instance.prepareRgba(caminho);
          if (!deuCerto && chave == 'image') {
            modelFail(
              'Uma textura nao pode ser decodificada. Use PNG/JPEG/WebP.',
            );
          }
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
