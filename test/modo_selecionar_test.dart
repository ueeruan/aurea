// "DEIXE O APP MAIS FACIL DE USAR" + mapa do agrupamento (14/09/2026).
//
// A selecao multipla so existia no toque longo PARADO numa barra; o botao
// Agrupar ficava escondido atras dele. O modo Selecionar, na regua da
// timeline, faz o toque simples marcar e desmarcar — como no Alight
// Motion — e o Agrupar aparece com duas camadas.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter_test/flutter_test.dart';


void main() {
  test('alternar marca, desmarca e tira ate a camada principal', () {
    var r = alternarNaSelecao(const {}, null, 'a');
    expect(r.principal, 'a');
    expect(r.multi, isEmpty, reason: 'uma so e selecao simples');

    r = alternarNaSelecao(r.multi, r.principal, 'b');
    expect(r.multi, {'a', 'b'});
    expect(r.principal, 'a');

    // Tocar de novo na principal a tira do conjunto.
    r = alternarNaSelecao(r.multi, r.principal, 'a');
    expect(r.multi, isEmpty);
    expect(r.principal, 'b');

    r = alternarNaSelecao(r.multi, r.principal, 'b');
    expect(r.principal, isNull);
  });

}
