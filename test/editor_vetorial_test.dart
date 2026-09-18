// O EDITOR VETORIAL ESTAVA ACESO FORA DO PAINEL.
//
// O relato: "o editor vetorial esta bugado". Nao estava errado — estava
// LIGADO. `MaskNodeEditor` desenha os nos sobre o palco e so sai da
// arvore quando `pathEditTargetProvider` e nulo, e ninguem limpava esse
// provider ao SAIR do painel por outro caminho que nao o Voltar: tocar
// numa barra da timeline, abrir outra ferramenta, o "+".
//
// Com o alvo velho de pe, o palco inteiro virava editor de nos. Tocar num
// objeto INSERIA um no no contorno antigo em vez de selecionar a camada,
// e arrastar mexia num no invisivel em vez de mover a camada.
//
// A trava e a sessao: o editor so existe com o painel de pontos aberto E
// apontando para o MESMO caminho que o painel edita. Os outros testes
// aqui prendem os defeitos de conta que apareceram no mesmo pedido.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/path_edit.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/mask_node_editor.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

BezierPath _aberto() => BezierPath(
  closed: false,
  vertices: const [
    PathVertex(p: Offset(0, 0)),
    PathVertex(p: Offset(100, 0)),
    PathVertex(p: Offset(100, 100)),
  ],
);

BezierPath _fechado() => BezierPath(
  closed: true,
  vertices: const [
    PathVertex(p: Offset(0, 0)),
    PathVertex(p: Offset(100, 0)),
    PathVertex(p: Offset(100, 100)),
    PathVertex(p: Offset(0, 100)),
  ],
);

void main() {
  group('o editor de nos nao fica aceso fora do painel', () {
    late ProviderContainer c;
    late String camadaId;
    late String contornoId;

    setUp(() {
      c = ProviderContainer(
        overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
      );
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(
        Duration.zero,
        name: 'F',
        contents: [ShapeBezier(path: AnimatedPath(_fechado())), ShapeFill()],
      );
      camadaId = c.read(editorControllerProvider).layers.first.id;
      contornoId = e.contornosDaForma(camadaId).first;
      c.read(selectedLayerProvider.notifier).state = camadaId;
      c.read(pathEditTargetProvider.notifier).state = PathEditTarget(
        camadaId,
        contornoId,
        forma: true,
      );
    });

    /// A pergunta que o palco faz antes de pegar o dedo.
    bool ativo() {
      final sessao = c.read(editorSessionProvider);
      return editorDeNosAtivo(
        painel: sessao.panel,
        itemDaSessao: sessao.pointsItemId,
        alvo: c.read(pathEditTargetProvider),
      );
    }

    test('painel aberto E o mesmo caminho: o palco edita os nos', () {
      c.read(editorSessionProvider.notifier).openEditPoints(
        contornoId,
        returnTo: EditorPanel.editShape,
      );
      expect(ativo(), isTrue);
    });

    test('sair do painel por QUALQUER caminho desliga o editor de nos', () {
      final s = c.read(editorSessionProvider.notifier);
      s.openEditPoints(contornoId, returnTo: EditorPanel.editShape);
      expect(ativo(), isTrue);

      // O caminho que os testadores usavam: tocar noutra coisa da tela,
      // que troca o painel SEM passar pelo Voltar.
      s.closePanel();
      expect(
        ativo(),
        isFalse,
        reason: 'o alvo continua de pe: o palco NAO pode editar nos',
      );
      // E nem deixando o alvo para tras o editor volta sozinho.
      s.openPanel(EditorPanel.transform);
      expect(ativo(), isFalse);
      s.openPanel(EditorPanel.editPoints);
      expect(ativo(), isFalse, reason: 'sem item, sem editor');
    });

    test('alvo de OUTRO caminho nao vale: quem manda e a sessao', () {
      final e = c.read(editorControllerProvider.notifier);
      final outro = e.adicionarContorno(camadaId)!;
      c.read(editorSessionProvider.notifier).openEditPoints(
        outro,
        returnTo: EditorPanel.editShape,
      );
      expect(c.read(editorSessionProvider).pointsItemId, outro);
      expect(c.read(pathEditTargetProvider)!.maskId, contornoId);
      expect(ativo(), isFalse, reason: 'o painel edita um, o alvo aponta outro');

      // Alinhando os dois, o palco volta a editar.
      c.read(pathEditTargetProvider.notifier).state = PathEditTarget(
        camadaId,
        outro,
        forma: true,
      );
      expect(ativo(), isTrue);
    });

    test('sem alvo nao ha editor, mesmo com o painel aberto', () {
      c.read(editorSessionProvider.notifier).openEditPoints(
        contornoId,
        returnTo: EditorPanel.editShape,
      );
      c.read(pathEditTargetProvider.notifier).state = null;
      expect(ativo(), isFalse);
    });
  });

  group('as contas do caminho ABERTO', () {
    test('apagar ponto funciona ate sobrarem dois', () {
      var p = _aberto();
      p = removeVertex(p, 2);
      expect(p.vertices, hasLength(2));
      p = removeVertex(p, 1);
      expect(p.vertices, hasLength(2), reason: 'dois nos ja sao um traco');
    });

    test('apagar ponto de um caminho FECHADO para em tres', () {
      var p = _fechado();
      p = removeVertex(p, 3);
      expect(p.vertices, hasLength(3));
      p = removeVertex(p, 2);
      expect(p.vertices, hasLength(3), reason: 'dois nos nao tem area');
    });

    test('canto vira curva com a alca na direcao do vizinho CERTO', () {
      final p = toggleCorner(_aberto(), 0);
      // A ponta tem UM vizinho: em (100,0). A alca sai na direcao dele.
      final v = p.vertices.first;
      expect(v.corner, isFalse);
      expect(v.outT.dy, closeTo(0, 1e-9));
      expect(v.outT.dx, greaterThan(0));
      expect(
        v.outT.dy.abs(),
        lessThan(1e-9),
        reason: 'a corda nao pode atravessar o desenho de ponta a ponta',
      );
    });

    test('no meio do caminho aberto usa os dois vizinhos', () {
      final p = toggleCorner(_aberto(), 1);
      final v = p.vertices[1];
      expect(v.corner, isFalse);
      // Vizinhos (0,0) e (100,100): a corda e diagonal.
      expect(v.outT.dx, closeTo(100 / 6, 1e-9));
      expect(v.outT.dy, closeTo(100 / 6, 1e-9));
      expect(v.inT, -v.outT);
    });

    test('no caminho fechado o primeiro e o ultimo continuam vizinhos', () {
      final p = toggleCorner(_fechado(), 0);
      final v = p.vertices.first;
      expect(v.corner, isFalse);
      // Vizinhos (0,100) e (100,0): a corda e (100,-100).
      expect(v.outT.dx, closeTo(100 / 6, 1e-9));
      expect(v.outT.dy, closeTo(-100 / 6, 1e-9));
    });
  });
}
