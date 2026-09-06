import 'package:flutter_riverpod/flutter_riverpod.dart';

/// Ferramenta temporária da sessão do editor; nunca pertence ao projeto salvo.
final freehandRequestProvider = StateProvider.autoDispose<bool>((ref) => false);

/// Quadros fantasma para desenhar/animar à mão: 0 desliga a comparação.
final onionSkinProvider = StateProvider.autoDispose<int>((ref) => 0);
