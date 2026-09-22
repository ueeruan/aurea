/* Aurea: cabeçalho mínimo para o transcoder Basis. O zstddeclib.c é o
   amálgama só de decodificação e não traz o zstd.h separado; o transcoder
   usa apenas estas duas funções (ver basisu_transcoder.cpp). */
#ifndef AUREA_ZSTD_SHIM_H
#define AUREA_ZSTD_SHIM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t ZSTD_decompress(void* dst, size_t dstCapacity, const void* src, size_t compressedSize);
unsigned ZSTD_isError(size_t code);
#ifdef __cplusplus
}
#endif
#endif
