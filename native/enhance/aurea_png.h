/*
 * PNG de passagem para os motores de IA (RIFE e aprimoramento).
 *
 * A exportacao do editor troca quadros com o C++ em arquivos PNG. Ler e
 * gravar aqui (stb, zlib do sistema no Android) evita decodificar e
 * codificar cada quadro em Dart, que custava mais que a propria rede.
 */
#ifndef AUREA_PNG_H
#define AUREA_PNG_H

#include <cstdio>
#include <memory>
#include <string>

namespace aurea_png {

struct LiberaPixels {
  void operator()(unsigned char* p) const;
};

/* Quadro RGB24 decodificado. */
struct Imagem {
  int w = 0;
  int h = 0;
  std::unique_ptr<unsigned char, LiberaPixels> rgb;
};

/* fopen com caminho UTF-8 (no Windows passa por _wfopen). */
FILE* abrir(const std::string& caminho, const char* modo);

bool legivel(const std::string& caminho);

/* Le qualquer PNG como RGB24. Falso se nao abre ou nao decodifica. */
bool ler(const std::string& caminho, Imagem* saida);

/* Grava RGB24 como PNG sem filtro e com compressao minima, num temporario
 * renomeado no fim: um PNG pela metade nunca aparece com o nome final. */
bool gravar(const std::string& caminho, const unsigned char* rgb, int w, int h);

}  // namespace aurea_png

#endif
