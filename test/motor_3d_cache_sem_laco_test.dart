// O CACHE DE QUADROS 3D NAO ENTRA EM LACO COM O PALCO PARADO.
//
// O que a bancada mediu (emulador, perfil): cena 3D + casca de cebola EM
// REPOUSO produzia 193 quadros em 6 s com CPU a 185%. A causa era de
// contabilidade: quatro vagas em fila, cinco chaves vivas na tela — guardar
// a quinta despejava a primeira, a revisao subia, a primeira desenhava de
// novo, despejava a segunda... para sempre.
//
// Estes testes rodam no PC, sem placa: a `PlacaDeTeste3D` responde pelas
// tres chamadas que tocam a GPU (subir malha, soltar alca, desenhar) e o
// resto — o que decide QUANDO chama-las — e o codigo de producao.
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// Uma placa que conta o que a producao pediu.
class _Placa {
  int criados = 0;
  int soltos = 0;
  int proximaAlca = 1;
  final List<int> alcasVivas = [];

  late final PlacaDeTeste3D porta = PlacaDeTeste3D(
    criarModelo: (malhas) {
      criados++;
      final alca = proximaAlca++;
      alcasVivas.add(alca);
      return alca;
    },
    soltar: (alca) {
      soltos++;
      alcasVivas.remove(alca);
    },
    desenhar: (cena) =>
        Uint8List(cena.largura * cena.altura * 4)..fillRange(0, 4, 255),
  );
}

Scene3D _cenaCom(Element3DKind kind, {String id = 'n1'}) => Scene3D(
  nodes: [SceneNode(id: id, name: 'objeto', kind: kind)],
);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late _Placa placa;
  final motor = Motor3DNativo.instance;

  setUp(() {
    placa = _Placa();
    motor.placaDeTeste = placa.porta;
    motor.limpar();
  });

  tearDown(() {
    motor.limpar();
    motor.placaDeTeste = null;
  });

  /// Espera a revisao parar de subir: nenhum aviso em 150 ms seguidos.
  Future<void> ateSossegar() async {
    var ultima = motor.revision.value;
    for (var i = 0; i < 40; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 150));
      if (motor.revision.value == ultima) return;
      ultima = motor.revision.value;
    }
    fail('a revisao nao parou de subir: ${motor.revision.value}');
  }

  test('cinco chaves vivas estabilizam em cinco desenhos — nao ha laco', () async {
    // CINCO VISTAS DA MESMA CENA (1 palco + 4 fantasmas da casca), cada
    // uma com o proprio instante — cinco chaves vivas ao mesmo tempo, uma
    // a mais do que o cache tinha de vagas.
    final cena = _cenaCom(Element3DKind.cube);
    final donos = List.generate(5, (i) => Object());
    final chaves = List.generate(5, (i) => 'quadro-$i');

    void rodada() {
      for (var i = 0; i < 5; i++) {
        if (!motor.temQuadro(chaves[i])) {
          motor.montar(
            cena: cena,
            camera: null,
            local: Duration(milliseconds: i * 33),
            largura: 8,
            altura: 8,
            familia: 'cena',
          );
        }
        motor.quadro(chaves[i], familia: 'cena', dono: donos[i]);
      }
    }

    // O QUE O PALCO FAZ: cada vista escuta a revisao e reconstroi.
    motor.revision.addListener(rodada);
    addTearDown(() => motor.revision.removeListener(rodada));

    rodada();
    await ateSossegar();

    // ignore: avoid_print
    print('DESENHOS com 5 chaves vivas: ${motor.desenhosFeitos}');
    expect(motor.desenhosFeitos, 5, reason: 'um desenho por chave, e chega');
    for (final c in chaves) {
      expect(motor.temQuadro(c), isTrue, reason: '$c tem de estar guardada');
    }

    // TRES RODADAS A MAIS (rebuilds por qualquer motivo): nada desenha.
    rodada();
    rodada();
    rodada();
    await ateSossegar();
    expect(motor.desenhosFeitos, 5, reason: 'em repouso a placa nao trabalha');
  });

  test('a mesma chave pedida duas vezes antes de decodificar desenha uma vez', () async {
    final cena = _cenaCom(Element3DKind.cube);
    motor.montar(
      cena: cena,
      camera: null,
      local: Duration.zero,
      largura: 4,
      altura: 4,
      familia: 'a',
    );
    motor.quadro('k', familia: 'a', dono: 1);
    motor.quadro('k', familia: 'a', dono: 2);
    motor.quadro('k', familia: 'a', dono: 3);
    expect(motor.desenhosFeitos, 1, reason: 'a chave ja estava em voo');
    expect(motor.temQuadro('k'), isTrue);
    await ateSossegar();
    expect(motor.quadro('k', familia: 'a', dono: 1), isNotNull);
    expect(motor.desenhosFeitos, 1);
  });

  test('o fantasma so olha o cache: nunca desenha', () async {
    final cena = _cenaCom(Element3DKind.cube);
    motor.montar(
      cena: cena,
      camera: null,
      local: Duration.zero,
      largura: 4,
      altura: 4,
      familia: 'a',
    );
    expect(motor.quadroDoCache('nunca-desenhada'), isNull);
    expect(motor.desenhosFeitos, 0);

    motor.quadro('vista', familia: 'a', dono: 1);
    await ateSossegar();
    expect(motor.quadroDoCache('vista'), isNotNull, reason: 'ja visitada');
    expect(motor.desenhosFeitos, 1);
  });

  test('duas cenas em N rebuilds sobem cada malha UMA vez', () {
    // ANTES: cada `montar` soltava as malhas da OUTRA camada e a outra as
    // recriava no rebuild seguinte — 2N subidas de geometria para a placa.
    final a = _cenaCom(Element3DKind.cube, id: 'a');
    final b = _cenaCom(Element3DKind.sphere, id: 'b');
    for (var i = 0; i < 20; i++) {
      motor.montar(
        cena: a,
        camera: null,
        local: Duration.zero,
        largura: 4,
        altura: 4,
        familia: 'camada-a',
      );
      motor.montar(
        cena: b,
        camera: null,
        local: Duration.zero,
        largura: 4,
        altura: 4,
        familia: 'camada-b',
      );
    }
    // ignore: avoid_print
    print('MODELOS CRIADOS em 20 rebuilds de 2 cenas: ${placa.criados}');
    expect(placa.criados, 2);
    expect(placa.soltos, 0);

    // A VISTA SAIU: so as malhas dela vao embora.
    motor.esquecerFamilia('camada-a');
    expect(placa.soltos, 1);
    expect(placa.alcasVivas.length, 1);
    motor.esquecerFamilia('camada-b');
    expect(placa.alcasVivas, isEmpty);
  });

  test('a chave do quadro muda quando o mapa do modelo chega ao cache', () {
    const caminho = 'x:mapa-do-teste-de-chave';
    final asset = ModelAsset3D({
      'materials': [
        {'image': caminho},
      ],
    });
    final cena = Scene3D(
      nodes: [SceneNode(id: 'm', name: 'modelo', modelAsset: asset)],
    );
    String chave() => chaveDaCena(
      cena: cena,
      camera: null,
      local: Duration.zero,
      largura: 4,
      altura: 4,
    );
    final antes = chave();
    TextureCache.instance.putRgba(caminho, Uint8List(4), 1, 1);
    addTearDown(TextureCache.instance.clear);
    final depois = chave();
    // SEM ISTO o cache devolvia o quadro cinza (sem mapa) para sempre.
    expect(antes, isNot(depois));
  });

  test('a placa de teste liga o motor no PC', () {
    expect(motor.ligado, isTrue);
    expect(Motor3D.disponivel, isFalse, reason: 'o motor nao compila no PC');
  });
}
