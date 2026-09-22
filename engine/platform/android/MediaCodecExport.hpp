// =============================================================================
//  Aurea / platform / android / MediaCodecExport.hpp
//
//  ExportSink do Android: MediaCodec de hardware (H.264 / HEVC para vídeo,
//  AAC-LC para áudio) e AMediaMuxer para o MP4.
//
//  Entrada de vídeo por ByteBuffer em NV12 (ou I420, se o encoder só aceitar
//  planar), respeitando o stride e a altura de fatia que o PRÓPRIO encoder
//  declara — é onde encoder de fabricante costuma quebrar quem supõe
//  "stride = largura".
//
//  O muxer só começa quando todas as trilhas já informaram o formato (o CSD
//  do H.264 chega no primeiro buffer de saída); amostras que chegam antes
//  esperam numa fila curta.
// =============================================================================
#pragma once

#include "aurea/export/ExportSink.hpp"

namespace aurea::android {

[[nodiscard]] std::unique_ptr<ExportSink> make_mediacodec_export_sink(void* user);

} // namespace aurea::android
