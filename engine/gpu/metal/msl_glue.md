# MSL embutido — o que o build precisa gerar (e o que o backend consome)

O motor compila GLSL → SPIR-V com o glslc no build (`cmake/AureaShaders.cmake`) e embute os
`.spv` num `ShaderBlob{words, bytes}`. No iOS o MESMO SPIR-V é traduzido para MSL com o
SPIRV-Cross no build, e o resultado entra no MESMO `ShaderBlob`. Nada de compilar GLSL em
runtime; o backend só consome.

## 1. Formato do blob (32 bytes de cabeçalho + payload, alinhado em palavras de 4)

    offset  0  char magic[8]   "AUREAMSL"
    offset  8  u32 versao      1
    offset 12  u32 flags       bit 0: o payload é um `.metallib` já compilado
    offset 16  u32 payloadBytes
    offset 20  u32 threadgroup[3]   compute: local_size_x/y/z (0 = ausente: erro na abertura)
    offset 32  payload         texto MSL em UTF-8, ou os bytes do `.metallib`

Sem o cabeçalho o backend aceita um blob que seja MSL puro (útil para vert/frag). Um blob que
comece com o magic do SPIR-V (`0x07230203`) é recusado com erro claro, e um blob de COMPUTE sem
`threadgroup` também: o Metal não conhece o tamanho do grupo pelo pipeline, e chutar errado
daria resultado errado em silêncio (memória de threadgroup e `threadgroup_position_in_grid`).

## 2. Pontos de entrada

`vs_main` (vert) · `fs_main` (frag) · `cs_main` (compute). Um `ShaderDesc::entryPoint` diferente
de `"main"` vence os três. O SPIRV-Cross emite `vertex_main`/`fragment_main`/`kernel_main` por
padrão — renomeie no build (`--rename-entry-point`); o backend aceita os nomes do SPIRV-Cross com
um AVISO no log, para não derrubar a abertura por detalhe de script.

## 3. Índices de recurso (o SPIRV-Cross NÃO acerta sozinho)

O SPIRV-Cross numera recursos EM SEQUÊNCIA por espaço de nome (`next_metal_resource_index_*`),
não pelo binding do Vulkan. Fixe os índices explicitamente (`MSLResourceBinding{set=0,
binding=…, msl_buffer/msl_texture/msl_sampler=…}` na API do SPIRV-Cross) para que valha a
tabela do motor — a mesma que `aurea::mtl::slot` documenta:

    sampler2D  u_tex0..u_tex11  binding 0..11  ->  [[texture(0..11)]]  +  [[sampler(0..11)]]
    uniform    u_params         binding 12     ->  [[buffer(12)]]
    image2D    u_img0..u_img1   binding 13,14  ->  [[texture(13)]] [[texture(14)]]
    buffer     u_data           binding 15     ->  [[buffer(15)]]
    buffer     u_data1 (aux)    binding 16     ->  [[buffer(16)]]
    push constants (≤128 B)                    ->  [[buffer(30)]]

O backend amarra o sampler no MESMO índice do slot (um sampler por slot, o padrão
linear/clamp quando a slot não recebe nenhum). O bloco de push constants tem de sair em
`buffer(30)`: se a sua versão do SPIRV-Cross emitir outro índice, ajuste
`aurea::mtl::slot::kPushConstant` — é o único acoplamento entre o build e o backend.

## 4. Comando de tradução (referência)

    spirv-cross --msl --msl-version 20100 --rename-entry-point main vs_main --output x.metal x.spv

`--msl-version 20100` (Metal 2.1) é o piso do que o motor usa (amostragem de YCbCr biplanar e
`setBytes:` de 128 B existem em todas as versões que o Metal 2.1 cobre). Para o caminho rápido,
compile o `.metal` com `xcrun -sdk iphoneos metal -c` + `metallib` e embuta o `.metallib` com
`flags |= 1` — a abertura não paga compilação de shader nenhuma.
