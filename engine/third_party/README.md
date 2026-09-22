# Dependências de terceiros

Código-fonte vendorizado, compilado junto com o motor. Nenhuma é runtime
separado: o Aurea continua dono do renderer, do FrameGraph, do grafo de cena,
dos materiais e da integração com timeline/export.

| Pasta | Projeto | Versão | Licença | Uso |
|---|---|---|---|---|
| `cgltf/` | github.com/jkuhlmann/cgltf | v1.15 (360db1a) | MIT | parser glTF 2.0 / GLB |
| `meshoptimizer/` | github.com/zeux/meshoptimizer | v0.23 (3e9d1ff) | MIT | cache/fetch/overdraw, simplificação (LOD) |
| `stb/stb_truetype.h`, `stb/stb_image.h` | github.com/nothings/stb | 2c980bb | MIT ou domínio público | contornos de fonte do Texto 3D; PNG/JPEG embutidos nos modelos (igual em Android e iOS) |
| `ufbx/` | github.com/ufbx/ufbx | v0.18.0 (729ab83) | MIT ou domínio público | leitor FBX (entrada → formato interno) |
| `basisu/` | github.com/BinomialLLC/basis_universal | v1_50_0_2 (b76a431), só o transcoder + zstd de decodificação (`zstd/zstd.h` é nosso: declara só as 2 funções usadas) | Apache-2.0 (zstd: BSD) | KTX2 (ETC1S/UASTC, Zstd) → RGBA no import |

Atualizar: trocar os arquivos pela nova versão e esta tabela no mesmo commit.
