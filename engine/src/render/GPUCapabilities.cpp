#include "aurea/render/GPUCapabilities.hpp"

#include <cstdio>

namespace aurea {

std::string GPUCapabilities::summary() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "%s %u.%u.%u | %s | tex %u | fp16 %s | timestamps %s | ycbcr %s | ahb %s | "
                  "heap local %.0f MB%s%s",
                  apiName.c_str(), apiMajor, apiMinor, apiPatch, deviceName.c_str(), maxTexture2D,
                  fp16Arithmetic ? "sim" : "nao", timestampQueries ? "sim" : "nao",
                  samplerYcbcrConversion ? "sim" : "nao", externalMemoryHardwareBuffer ? "sim" : "nao",
                  static_cast<double>(deviceLocalBytes) / (1024.0 * 1024.0),
                  unifiedMemory ? " (unificada)" : "", validationEnabled ? " | validacao" : "");
    return buf;
}

} // namespace aurea
