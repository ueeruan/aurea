import 'package:flutter_riverpod/flutter_riverpod.dart';

/// O ZOOM DO PALCO: 1.0 = a composicao ajustada a janela.
///
/// E estado da sessao de edicao (o editor zera ao abrir), escrito pela
/// pinca fora de objeto e pelo transporte, lido pelo `PreviewStage`. Mora
/// aqui, ao lado dos gestos do palco, para o render nao depender de
/// nenhuma barra da UI.
final zoomDoPalcoProvider = StateProvider<double>((ref) => 1.0);
