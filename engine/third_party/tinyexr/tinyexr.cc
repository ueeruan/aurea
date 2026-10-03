// Implementação do tinyexr (leitor/gravador OpenEXR) para o motor Aurea.
//
// Sem miniz: o deflate vem do stb_image (stbi_zlib_decode_buffer, compilado
// em src/scene3d/GltfImporter.cpp). A compressão (só usada ao GRAVAR, nos
// testes) é o "stored" do zlib abaixo — um fluxo válido, sem compactar.
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_USE_OPENMP 0
#define TINYEXR_USE_THREAD 0
#define TINYEXR_IMPLEMENTATION
#include <cstdlib>
#include <cstring>
#include "tinyexr.h"

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int /*quality*/) {
    if (!data || data_len < 0 || !out_len) return nullptr;
    const size_t n = static_cast<size_t>(data_len);
    const size_t blocks = n / 65535 + 1;
    unsigned char* out = static_cast<unsigned char*>(std::malloc(2 + blocks * 5 + n + 4));
    if (!out) return nullptr;
    size_t o = 0;
    out[o++] = 0x78; out[o++] = 0x01;
    size_t i = 0;
    do {
        const size_t len = n - i < 65535 ? n - i : 65535;
        out[o++] = i + len >= n ? 1 : 0;
        out[o++] = static_cast<unsigned char>(len & 0xff); out[o++] = static_cast<unsigned char>(len >> 8);
        out[o++] = static_cast<unsigned char>(~len & 0xff); out[o++] = static_cast<unsigned char>((~len >> 8) & 0xff);
        std::memcpy(out + o, data + i, len);
        o += len; i += len;
    } while (i < n);
    unsigned a = 1, b = 0;
    for (size_t k = 0; k < n; ++k) { a = (a + data[k]) % 65521u; b = (b + a) % 65521u; }
    const unsigned adler = (b << 16) | a;
    out[o++] = static_cast<unsigned char>(adler >> 24); out[o++] = static_cast<unsigned char>(adler >> 16);
    out[o++] = static_cast<unsigned char>(adler >> 8); out[o++] = static_cast<unsigned char>(adler);
    *out_len = static_cast<int>(o);
    return out;
}
