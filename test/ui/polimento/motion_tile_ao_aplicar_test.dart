// APLICAR O MOTION TILE NAO ENCOLHE A IMAGEM.
//
// Relato de testador: "ao aplicar Motion Tile numa imagem 1:1, a imagem
// comprime, diminui de tamanho". O passe estava certo; o catalogo aplicava
// o preset da miniatura (Tijolos, mosaico 50% x 25%). Este teste prende o
// que o toque entrega: mosaico em 100% (a copia do centro do tamanho da
// camada) e saida maior que 100% (as copias ao redor).
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos/catalogo_de_efeitos.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('o toque aplica o mosaico em 100% e a saida maior', () {
    final pronto = prontoAoAplicar(EffectType.motionTile);
    expect(pronto, isNotNull);
    final v = pronto!.valores;
    expect(v['tile_width'], 100, reason: 'a copia do centro e a camada');
    expect(v['tile_height'], 100, reason: 'a copia do centro e a camada');
    expect(v['output_width']!, greaterThan(100), reason: 'copias ao redor');
    expect(v['output_height']!, greaterThan(100), reason: 'copias ao redor');
  });

  test('nenhum valor do toque sai da faixa da ficha', () {
    final spec = effectSpecs[EffectType.motionTile]!;
    final pronto = prontoAoAplicar(EffectType.motionTile)!;
    for (final e in pronto.valores.entries) {
      final p = spec.params[e.key]!;
      expect(e.value, inInclusiveRange(p.min, p.max), reason: e.key);
    }
  });
}
