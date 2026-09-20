#!/usr/bin/env bash
# GERA O `src/shaders_3d.h` — O SPIR-V DOS SHADERS 3D EMBUTIDO NO BINARIO.
#
# ============================== POR QUE EMBUTIDO =======================
# O APK NAO TEM COMO LER UM `.spv` DO DISCO. Um arquivo ao lado do binario
# funciona no computador e falha no celular, onde o pacote e um zip
# assinado e as pastas sao outras. Empacotar como recurso do Flutter daria
# uma copia em memoria por leitura e um caminho a mais para errar. O array
# em C nao tem caminho, nao tem leitura e nao tem permissao: ele ja esta la.
#
# O CUSTO E O REPOSITORIO. Vinte e cinco quilobytes de SPIR-V viram uns
# cento e cinquenta de texto em C, e o diff de um shader aparece como
# milhares de linhas de numeros. E por isso que este script existe: o que
# se le e revisa e o `.vert`/`.frag`, e este arquivo e so o resultado —
# uma copia gerada, que se refaz com um comando.
#
# ============================== COMO USAR ==============================
#   bash packages/aurea_render/tool/gerar_shaders.sh
#
# O `glslc` sai do NDK. Para apontar para outro, defina GLSLC.
# Depois de rodar, confira com o `spirv-val` — o script ja valida os seis.
#
# ============================== AS SEIS VARIANTES ======================
# `pbr` (comum) e `pbr` com PELE=1 (deformada); `sombra` nas duas; e o
# fragmento do PBR. O mesmo par de compilacao do `pbr.vert` vale para o
# `sombra.vert`. Compilar quatro arquivos a mao garante que um dia alguem
# esquece o `-DPELE` e o modelo animado cai no shader errado — que nao da
# erro, desenha na pose de repouso.
set -u

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACOTE="$(dirname "$AQUI")"
FONTES="$PACOTE/shaders"
SAIDA="$PACOTE/src/shaders_3d.h"

GLSLC="${GLSLC:-C:/Users/SnyX/AppData/Local/Android/Sdk/ndk/28.2.13676358/shader-tools/windows-x86_64/glslc.exe}"
ST="$(dirname "$GLSLC")"

if [ ! -x "$GLSLC" ]; then
  echo "glslc nao encontrado em: $GLSLC" >&2
  echo "defina GLSLC=/caminho/para/glslc" >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# AS BANDEIRAS DE COMPILACAO.
#   --target-env=vulkan1.1  o ambiente minimo do Android 24+; um alvo mais
#                           novo geraria instrucoes que aparelho antigo nao
#                           tem, e o shader falharia so neles.
#
# SEM `-O`, E ISSO NAO E DESLIZE. O `-O` manda o `spirv-opt` apagar tudo o
# que ele acha que nao serve — inclusive as instrucoes `OpName`. O Diligent
# casa recurso POR NOME: sem os nomes, o `Quadro` e o `Desenho` deixam de
# ter com o que se ligar e a criacao do pipeline falha. O sintoma nao e um
# erro de compilacao, e a cena nao aparecer.
#
# O codigo do `#if PELE` quem tira e o PRE-PROCESSADOR, com `-DPELE=0`, e
# nao o otimizador: por isso nao se perde nada em abrir mao do `-O`.
BANDEIRAS=(--target-env=vulkan1.1)

# A CONFERENCIA DOS NOMES. Uma linha, e ela evita a tarde inteira de
# procura: se o `OpName` sumir de novo, o script avisa antes de gravar.
conferir_nomes() {  # conferir_nomes <arquivo.spv> <nome-esperado...>
  local arquivo="$1"; shift
  if [ ! -x "$ST/spirv-dis.exe" ]; then return 0; fi
  local nomes
  nomes="$("$ST/spirv-dis.exe" "$arquivo" | grep -c 'OpName')"
  if [ "$nomes" -eq 0 ]; then
    echo "SEM NOMES em $arquivo: o Diligent nao vai achar os recursos." >&2
    exit 1
  fi
  local alvo
  for alvo in "$@"; do
    if ! "$ST/spirv-dis.exe" "$arquivo" | grep -q "OpName.*$alvo"; then
      echo "falta o recurso '$alvo' em $arquivo" >&2
      exit 1
    fi
  done
}

compilar() {  # compilar <arquivo-fonte> <saida> [define...]
  local fonte="$1"; shift
  local destino="$1"; shift
  if ! "$GLSLC" "${BANDEIRAS[@]}" "$@" -o "$destino" "$FONTES/$fonte"; then
    echo "falhou: $fonte $*" >&2
    exit 1
  fi
  if [ -x "$ST/spirv-val.exe" ]; then
    "$ST/spirv-val.exe" --target-env vulkan1.1 "$destino" || {
      echo "SPIR-V invalido: $fonte $*" >&2; exit 1; }
  fi
}

compilar pbr.vert           "$TMP/pbr_vert.spv"      -DPELE=0
compilar pbr.vert           "$TMP/pbr_vert_pele.spv" -DPELE=1
compilar pbr.frag           "$TMP/pbr_frag.spv"
compilar sombra.vert        "$TMP/sombra_vert.spv"      -DPELE=0
compilar sombra.vert        "$TMP/sombra_vert_pele.spv" -DPELE=1
compilar sombra.frag        "$TMP/sombra_frag.spv"

# OS NOMES QUE O `renderizador_3d.cpp` PROCURA. Se algum sumir daqui, a
# ligacao do lado do C++ vira uma variavel nula e o desenho sai preto.
conferir_nomes "$TMP/pbr_vert.spv"      Quadro Desenho Ossos
conferir_nomes "$TMP/pbr_vert_pele.spv" Quadro Desenho Ossos
conferir_nomes "$TMP/pbr_frag.spv"      Quadro Luzes Desenho \
  tex_cor tex_normal tex_metalico_rugosidade tex_emissiva tex_oclusao tex_sombra
conferir_nomes "$TMP/sombra_vert.spv"      Quadro Desenho
conferir_nomes "$TMP/sombra_vert_pele.spv" Quadro Desenho Ossos
conferir_nomes "$TMP/sombra_frag.spv"  profundidade

# O `od` E O QUE SEMPRE EXISTE. O `xxd` nao vem em todo Git para Windows, e
# um script que so roda na maquina de quem escreveu nao serve de nada.
# Cada byte sai indentado, dezesseis por linha, para o arquivo continuar
# legivel por um humano que precise conferir um tamanho.
emitir() {  # emitir <nome-em-C> <comentario> <arquivo>
  local nome="$1" nota="$2" arquivo="$3"
  local bytes
  bytes="$(wc -c < "$arquivo" | tr -d ' ')"
  echo ""
  echo "/// $nota"
  echo "/// $bytes bytes."
  echo "inline constexpr unsigned char $nome[] = {"
  od -An -v -tu1 "$arquivo" | tr -s ' ' | sed 's/^ //; s/ $//; s/ /, /g' |
    awk 'NF { printf "    %s,\n", $0 }'
  echo "};"
}

{
  cat <<'CABECALHO'
// ============================================================================
//  GERADO POR tool/gerar_shaders.sh — NAO EDITE ESTE ARQUIVO A MAO.
//
//  O QUE EDITA E O `shaders/*.vert` E O `shaders/*.frag`. Este aqui e o
//  resultado da compilacao deles para SPIR-V, embutido no binario porque o
//  APK nao consegue ler um arquivo de shader do disco (§41).
//
//  OS NUMEROS DE LIGACAO (binding) E O ALINHAMENTO DOS BLOCOS estao
//  repetidos em `renderizador_3d.cpp`, do lado do C++, e nos dois lados
//  eles TEM de bater. Conferidos contra esta saida:
//
//    binding 0  Quadro   mat4 vista, projecao, vista_projecao, luz_espaco;
//                        vec4 olho @256, ambiente @272, ajustes @288
//    binding 1  Luzes    8 x { vec4 x4 }  passo 64
//    binding 2  Desenho  mat4 mundo @0, normal @64; vec4 cor_base @128,
//                        parametros @144, emissivo @160, bandeiras @176,
//                        bandeiras2 @192, tinta @208
//    binding 3  Ossos    mat4[]  passo 64   (somente nos shaders com PELE)
//    binding 4..9  tex_cor, tex_normal, tex_metalico_rugosidade,
//                  tex_emissiva, tex_oclusao, tex_sombra
//
//  UMA MUDANCA AQUI PEDE A CONFERENCIA LA. O std140 nao guarda nome de
//  campo: um atributo trocado de lugar desenha lixo sem dar erro.
// ============================================================================
#pragma once

namespace aurea::shaders {
CABECALHO

  emitir kPbrVert "O VERTICE DO PBR, para a malha comum." "$TMP/pbr_vert.spv"
  emitir kPbrVertPele "O VERTICE DO PBR, para a malha deformada (PELE=1)." "$TMP/pbr_vert_pele.spv"
  emitir kPbrFrag "O FRAGMENTO DO PBR. O mesmo serve a malha comum e a deformada." "$TMP/pbr_frag.spv"
  emitir kSombraVert "O VERTICE DO PASSE DE SOMBRA, malha comum." "$TMP/sombra_vert.spv"
  emitir kSombraVertPele "O VERTICE DO PASSE DE SOMBRA, malha deformada." "$TMP/sombra_vert_pele.spv"
  emitir kSombraFrag "O FRAGMENTO DO PASSE DE SOMBRA: profundidade no canal vermelho." "$TMP/sombra_frag.spv"

  cat <<'RODAPE'
}  // namespace aurea::shaders
RODAPE
} > "$SAIDA"

echo "escrito: $SAIDA"
wc -c < "$SAIDA" | tr -d ' ' | sed 's/^/bytes do cabecalho: /'
