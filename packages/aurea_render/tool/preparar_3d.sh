#!/usr/bin/env bash
set -euo pipefail

# Dependencias nativas do motor 3D. Elas ficam fora do Git para nao inflar o
# repositorio; este script torna um checkout limpo compilavel no Android e no
# iOS. As versoes sao fixas e o pacote binario do iOS tem SHA-256 verificado.
PACOTE="$(cd "$(dirname "$0")/.." && pwd)"
TERCEIROS="$PACOTE/third_party"
DILIGENT="$TERCEIROS/diligent"
ASSIMP="$TERCEIROS/assimp"
MOLTENVK="$TERCEIROS/moltenvk/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a"

mkdir -p "$TERCEIROS"

if [[ ! -f "$DILIGENT/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h" ]]; then
  if [[ -e "$DILIGENT" ]]; then
    echo "Diligent incompleto em $DILIGENT; mova a pasta e rode novamente." >&2
    exit 1
  fi
  git clone --depth 1 --branch v2.5.6 \
    https://github.com/DiligentGraphics/DiligentCore.git "$DILIGENT"
  git -C "$DILIGENT" submodule update --init --depth 1 \
    ThirdParty/SPIRV-Cross ThirdParty/Vulkan-Headers ThirdParty/xxHash
fi

if [[ ! -f "$ASSIMP/include/assimp/config.h" || \
      ! -f "$ASSIMP/include/assimp/revision.h" ]]; then
  if [[ -e "$ASSIMP" ]]; then
    echo "Assimp incompleto em $ASSIMP; mova a pasta e rode novamente." >&2
    exit 1
  fi
  git clone --depth 1 --branch v5.4.3 \
    https://github.com/assimp/assimp.git "$ASSIMP"
  CONFIG_TEMP="$(mktemp -d)"
  trap 'rm -rf "$CONFIG_TEMP"' EXIT
  cmake -S "$ASSIMP" -B "$CONFIG_TEMP/assimp" \
    -DASSIMP_BUILD_TESTS=OFF \
    -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
    -DASSIMP_INSTALL=OFF \
    -DASSIMP_WARNINGS_AS_ERRORS=OFF >/dev/null
  cp "$CONFIG_TEMP/assimp/include/assimp/config.h" "$ASSIMP/include/assimp/config.h"
  cp "$CONFIG_TEMP/assimp/include/assimp/revision.h" "$ASSIMP/include/assimp/revision.h"
  rm -rf "$CONFIG_TEMP"
  trap - EXIT
fi

if [[ "${1:-}" == "--ios" && ! -f "$MOLTENVK" ]]; then
  MOLTEN_TEMP="$(mktemp -d)"
  trap 'rm -rf "$MOLTEN_TEMP"' EXIT
  ARQUIVO="$MOLTEN_TEMP/MoltenVK-ios.tar"
  curl --fail --location --retry 3 \
    'https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-ios.tar' \
    --output "$ARQUIVO"
  echo 'b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1  '"$ARQUIVO" | shasum -a 256 --check
  tar -xf "$ARQUIVO" -C "$MOLTEN_TEMP"
  mkdir -p "$(dirname "$MOLTENVK")"
  cp "$MOLTEN_TEMP/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a" "$MOLTENVK"
  cp "$MOLTEN_TEMP/MoltenVK/LICENSE" "$TERCEIROS/moltenvk/LICENSE"
fi

# O REMENDO DO OBJECTIVE-C++. O motor compila no iOS como Objective-C++ (os
# dois `.mm` do Diligent pedem, e o clang recusa `-std=c++20` em Objective-C
# puro). Nesse modo `CAMetalLayer` e um tipo de verdade e `void*` nao vira
# ponteiro dele sozinho — o `SwapChainVkImpl.cpp` deixa de compilar. A pasta
# `third_party` nao vai para o git, entao o remendo mora aqui e e idempotente.
TROCA="$DILIGENT/Graphics/GraphicsEngineVulkan/src/SwapChainVkImpl.cpp"
if [[ -f "$TROCA" ]] && grep -q 'surfaceCreateInfo.pLayer = pLayer;' "$TROCA"; then
  perl -0pi -e 's/surfaceCreateInfo\.pLayer = pLayer;/surfaceCreateInfo.pLayer = (decltype(surfaceCreateInfo.pLayer))pLayer;/' "$TROCA"
  echo 'Diligent: remendo do CAMetalLayer aplicado.'
fi

echo 'Dependencias do motor 3D prontas.'
