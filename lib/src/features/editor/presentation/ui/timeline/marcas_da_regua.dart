import 'package:flutter/cupertino.dart';
import 'package:flutter/gestures.dart' show DragStartBehavior;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/pedir_nome.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer_meta.dart' show LayerLabel;
import '../../../domain/video_project.dart' show Marker;
import 'area_de_toque.dart';
import 'estado_da_timeline.dart';

// ===========================================================================
// AS MARCAS DA REGUA: o toque que cada uma aceita
// ===========================================================================
//
// A regua desenha as marcas num pintor (so repinta quando a vista anda).
// Por cima dela fica esta area, transparente ao toque fora das marcas: o
// dedo que pousa numa bandeirinha
//
//  * SEGURA -> o menu da marca: renomear, pintar, apagar (o menu do editor
//    antigo, `_menuDaMarca` da `am_timeline`);
//  * ARRASTA de lado -> a marca anda junto (um arrasto = um desfazer).
//
// Fora das bandeirinhas o toque segue para a timeline (scrub, rolagem).

/// A meia largura da bandeirinha que aceita o dedo (22 no total, a do
/// editor antigo).
const double _meiaLarguraDoToque = 11;

/// A altura da faixa da regua em que a bandeirinha aceita o dedo. Abaixo
/// dela a regua continua sendo do scrub.
const double _alturaDoToque = 20;

/// A MARCA sob o ponto [x] da regua, ou nula.
Marker? marcaNoX(EstadoDaTimeline estado, List<Marker> marcas, double x) {
  Marker? melhor;
  var distancia = double.infinity;
  for (final m in marcas) {
    final d = (estado.xDoTempo(m.time.inMicroseconds) - x).abs();
    if (d <= _meiaLarguraDoToque && d < distancia) {
      melhor = m;
      distancia = d;
    }
  }
  return melhor;
}

/// O MENU DE UMA MARCA: renomear, pintar e apagar — cada escolha um passo
/// de desfazer.
Future<void> menuDaMarca(
  BuildContext context,
  WidgetRef ref,
  Marker marca, {
  Rect? ancora,
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  final escolha = await mostrarAureaMenu<String>(
    context,
    ancora: ancora,
    titulo: marca.label.isEmpty ? 'Marca' : null,
    itens: const [
      AureaMenuItem(
        valor: 'renomear',
        rotulo: 'Renomear a marca',
        icone: CupertinoIcons.pencil,
        chave: 'marca-renomear',
      ),
      AureaMenuItem(
        valor: 'cor',
        rotulo: 'Cor da marca…',
        icone: CupertinoIcons.paintbrush,
        chave: 'marca-cor',
      ),
      AureaMenuItem(
        valor: 'apagar',
        rotulo: 'Apagar a marca',
        icone: CupertinoIcons.trash,
        destrutivo: true,
        chave: 'marca-apagar',
      ),
    ],
  );
  if (escolha == null || !context.mounted) return;
  switch (escolha) {
    case 'renomear':
      final nome = await pedirNome(
        context,
        titulo: 'Nome da marca',
        atual: marca.label,
      );
      if (nome == null) return;
      c.runAsOneUndo(() => c.renameMarker(marca.time, nome));
    case 'cor':
      final i = await mostrarAureaMenu<int>(
        context,
        ancora: ancora,
        titulo: 'Cor da marca',
        itens: [
          for (var i = 0; i < LayerLabel.palette.length; i++)
            AureaMenuItem(
              valor: i,
              rotulo: LayerLabel.palette[i].name,
              icone: CupertinoIcons.circle_fill,
              marcado:
                  marca.color.toARGB32() ==
                  LayerLabel.palette[i].color.toARGB32(),
              chave: 'marca-cor-$i',
            ),
        ],
      );
      if (i == null) return;
      c.runAsOneUndo(
        () => c.setMarkerColor(marca.time, LayerLabel.palette[i].color),
      );
    case 'apagar':
      c.runAsOneUndo(() => c.removeMarker(marca.time));
  }
}

/// A AREA DE TOQUE DAS MARCAS, por cima da regua.
class ToqueDasMarcas extends ConsumerStatefulWidget {
  const ToqueDasMarcas({
    super.key,
    required this.estado,
    required this.marcas,
  });

  final EstadoDaTimeline estado;
  final List<Marker> marcas;

  @override
  ConsumerState<ToqueDasMarcas> createState() => _ToqueDasMarcasState();
}

class _ToqueDasMarcasState extends ConsumerState<ToqueDasMarcas> {
  /// A marca que o dedo esta levando, no instante em que ela esta agora.
  Duration? _arrastando;
  double _xDoArrasto = 0;

  bool _acerta(Offset p) =>
      p.dy <= _alturaDoToque &&
      marcaNoX(widget.estado, widget.marcas, p.dx) != null;

  void _menu(LongPressStartDetails d) {
    final m = marcaNoX(widget.estado, widget.marcas, d.localPosition.dx);
    if (m == null) return;
    HapticFeedback.mediumImpact();
    menuDaMarca(
      context,
      ref,
      m,
      ancora: Rect.fromLTWH(d.globalPosition.dx, d.globalPosition.dy, 0, 0),
    );
  }

  void _comecar(DragStartDetails d) {
    final m = marcaNoX(widget.estado, widget.marcas, d.localPosition.dx);
    if (m == null) return;
    HapticFeedback.lightImpact();
    _arrastando = m.time;
    _xDoArrasto = widget.estado.xDoTempo(m.time.inMicroseconds);
    // UM ARRASTO = UM PASSO de desfazer.
    ref.read(editorControllerProvider.notifier).beginGesture();
  }

  void _seguir(DragUpdateDetails d) {
    final de = _arrastando;
    if (de == null) return;
    _xDoArrasto += d.delta.dx;
    var para = Duration(
      microseconds: widget.estado.tempoDoX(_xDoArrasto).round(),
    );
    if (para < Duration.zero) para = Duration.zero;
    if (para == de) return;
    ref.read(editorControllerProvider.notifier).moveMarker(de, para);
    _arrastando = para;
  }

  void _terminar() {
    if (_arrastando == null) return;
    _arrastando = null;
    ref.read(editorControllerProvider.notifier).endGesture();
  }

  @override
  void dispose() {
    if (_arrastando != null) {
      _arrastando = null;
      // O arrasto que morreu no meio (a regua saiu da tela) fecha o gesto.
      try {
        ref.read(editorControllerProvider.notifier).endGesture();
      } catch (_) {}
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AreaDeToqueCalculada(
    acerta: _acerta,
    child: GestureDetector(
      key: const ValueKey('timeline-marcas'),
      behavior: HitTestBehavior.opaque,
      dragStartBehavior: DragStartBehavior.down,
      onLongPressStart: _menu,
      onHorizontalDragStart: _comecar,
      onHorizontalDragUpdate: _seguir,
      onHorizontalDragEnd: (_) => _terminar(),
      onHorizontalDragCancel: _terminar,
      child: const SizedBox.expand(),
    ),
  );
}

/// AS MIGALHAS DO GRUPO: o projeto e cada grupo aberto por fora do atual.
/// Escolher uma volta ate aquele nivel (`sairAteONivel`) — a trilha de
/// grupos da barra do editor antigo.
Future<void> menuDasMigalhas(
  BuildContext context,
  WidgetRef ref, {
  Rect? ancora,
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  final caminho = c.caminhoDoGrupo;
  if (caminho.isEmpty) return;
  final nivel = await mostrarAureaMenu<int>(
    context,
    ancora: ancora,
    titulo: 'Voltar a…',
    itens: [
      AureaMenuItem(
        valor: 0,
        rotulo: c.nomeDoProjetoRaiz,
        icone: CupertinoIcons.film,
        traduzir: false,
        chave: 'migalha-0',
      ),
      for (var i = 0; i < caminho.length - 1; i++)
        AureaMenuItem(
          valor: i + 1,
          rotulo: caminho[i],
          icone: CupertinoIcons.rectangle_stack,
          traduzir: false,
          chave: 'migalha-${i + 1}',
        ),
    ],
  );
  if (nivel == null) return;
  c.sairAteONivel(nivel);
}
