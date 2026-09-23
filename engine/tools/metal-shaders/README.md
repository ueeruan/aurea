# Compilador de Metal do host (SPIR-V → MSL)

A peça que faz o iOS desenhar **os mesmos shaders** do Android. O motor compila
GLSL → SPIR-V no build (`cmake/AureaShaders.cmake`, com o glslc); este programa
traduz esses `.spv` para Metal Shading Language, e o que entra no binário é o
MESMO `ShaderBlob` — só que em MSL. Não existe um segundo conjunto de shaders
para o iOS: um efeito escrito uma vez roda nos dois aparelhos.

```
    GLSL ──glslc──▶ .spv ──este programa──▶ blob MSL ──xcrun metal──▶ .metallib
                                                                    (embutido)
```

## Como ele é construído

**Ninguém precisa construí-lo à mão.** O `CMakeLists.txt` do iOS
(`engine/platform/ios/CMakeLists.txt`) configura e compila este diretório para o
HOST durante a configuração, e aponta `AUREA_METAL_COMPILER` para o executável.
Se você já tem um binário, passe `-DAUREA_METAL_COMPILER=<caminho>` e ele usa o
seu.

O SPIRV-Cross vem por `FetchContent` (o commit fixado no `CMakeLists.txt`), então
a **primeira** configuração precisa de rede — depois fica em cache.

À mão, se precisar:

```bash
cmake -S engine/tools/metal-shaders -B build/metal-compiler -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-compiler --config Release
```

## O que ele faz (e por que cada parte importa)

1. **Renomeia o ponto de entrada** para `vs_main` / `fs_main` / `cs_main`. O
   SPIRV-Cross emite `vertex_main`/`fragment_main`/`kernel_main`; o backend
   procura os nomes do motor (e só avisa no log se achar os outros).
2. **Fixa os índices de recurso.** O SPIRV-Cross numera em sequência por espaço
   de nome — um shader que só usa a textura 3 sairia com `[[texture(0)]]`. O
   programa fixa cada binding no índice da tabela do motor (a mesma de
   `aurea::mtl::slot`): texturas 0..11 com os samplers no mesmo índice, o bloco
   de uniforms em 12, as imagens de armazenamento em 13/14, os buffers em 15/16
   e os **push constants em `[[buffer(30)]]`**. Um índice trocado aqui não dá
   erro nenhum no aparelho: desenha com o recurso errado.
3. **Escreve o cabeçalho `AUREAMSL`** de 32 bytes (magic, versão, flags, tamanho
   e — para compute — o tamanho do grupo), porque o Metal não descobre o
   `threadgroup` pelo pipeline.

O formato do blob está em `engine/gpu/metal/msl_glue.md`; este programa e o
checker abaixo são os dois lados dele.

## Verificação (roda no host, sem Mac)

```bash
# 1. o backend consome um MSL por shader?
python engine/tools/metal-shaders/check_msl_blob.py <pasta-dos-blobs> <pasta-dos-.spv>

# 2. no Mac, depois do xcrun: os .metallib estão ligados e válidos?
python engine/tools/metal-shaders/verify_msl.py <pasta-dos-blobs>
```

O `check_msl_blob.py` lê os **bindings declarados no SPIR-V** e exige que cada um
apareça no MSL no MESMO índice — é o teste que pega um build que não fixou os
índices. Medido nesta árvore: **64 shaders (9 de vértice, 55 de fragmento), 64
blobs válidos, 0 problema**.

O `verify_msl.py` é do Mac: ele confere os 64 `.mslblob` com a flag de
`.metallib` depois do `xcrun metal` + `metallib` (o `pack_metallib.py` troca o
texto MSL pelo binário pré-compilado, para a abertura do app não pagar compilação
de shader).
