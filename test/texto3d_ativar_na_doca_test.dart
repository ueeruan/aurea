// TEXTO COMUM -> TEXTO 3D pela barra da camada: a ferramenta "Texto 3D"
// (`AcaoDaFerramenta.ativar3d`) e a porta para extrudar um texto ja escrito
// e posicionado, sem apagar nada. Ela so vale se aparece para o texto e
// NAO aparece para o resto — a lista e a mesma que a barra contextual
// desenha (`ferramentasDa`).
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('a ferramenta Texto 3D so aparece no texto', () {
    List<String> ids(Layer camada) => [
      for (final f in ferramentasDa(camada)) f.id,
    ];
    final texto = TextLayer(
      name: 't',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      text: 'AUREA',
    );
    final imagem = ImageLayer(
      name: 'imagem',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      sourcePath: '/tmp/i.png',
    );
    expect(ids(texto), contains(AcaoDaFerramenta.ativar3d));
    expect(
      ids(imagem),
      isNot(contains(AcaoDaFerramenta.ativar3d)),
      reason: 'imagem nao tem letra para extrudar',
    );
  });
}
