// A PORTA DO 3D — o que o Dart chama.
//
// Ela e MINIMA de proposito, e por enquanto: abrir o dispositivo e dizer
// o que deu errado. Desenhar entra por aqui quando desenhar existir; o
// desenho nao atravessa esta porta como estado do Dart — quem monta a
// cena e o C++, a partir do que o avaliador da timeline entregar (a mesma
// regra do 2D: `estado -> comando -> C++ -> GPU -> superficie`).
#ifndef AUREA_RENDER_API_3D_H
#define AUREA_RENDER_API_3D_H

#include <cstdint>

namespace aurea::render::tresd {

/// Abre o dispositivo. 1 se subiu, 0 se nao. Barato depois da primeira
/// chamada: a resposta fica guardada.
int preparar();

/// O dispositivo esta de pe?
bool pronto();

/// POR QUE NAO SUBIU. Ponteiro para string estatica DENTRO da biblioteca:
/// nao pertence a quem chama e nao pode ser liberado.
const char* motivo();

/// O NOME DO RENDERIZADOR EM USO ("Diligent/Vulkan").
const char* backend();

}  // namespace aurea::render::tresd

#endif  // AUREA_RENDER_API_3D_H
