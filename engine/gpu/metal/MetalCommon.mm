// =============================================================================
//  Vocabulário do motor → Metal (formatos, comparações, topologia, endereço de
//  sampler), o anel de memória compartilhada e o ciclo de vida da instância.
//
//  Nenhuma decisão de política aqui: é tradução de enum, e um lugar só para
//  cada tabela. Mudou um formato no motor, o erro aparece nesta tabela.
// =============================================================================
#include "MetalInternal.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <new>

namespace aurea::mtl {

// =============================================================================
// Erros do Metal
// =============================================================================
Status check_ns(NSError* err, const char* what) noexcept {
    if (!err) return OkStatus;
    // A descrição do NSError vem de fora do motor (driver): entra no log como
    // dado, nunca é formatada como se fosse nossa.
    AUREA_LOG_ERROR("metal: %s -> %s", what, err.localizedDescription.UTF8String);
    const NSInteger code = err.code;
    if (code == (NSInteger)MTLCommandBufferErrorOutOfMemory) {
        return Status{Errc::OutOfDeviceMemory, what};
    }
    if (code == (NSInteger)MTLCommandBufferErrorDeviceRemoved) {
        return Status{Errc::DeviceLost, what};
    }
    return Status{Errc::InvalidState, what};
}

// =============================================================================
// Formatos
// =============================================================================
MTLPixelFormat to_mtl(SurfaceFormat f) noexcept {
    switch (f) {
        case SurfaceFormat::R8:       return MTLPixelFormatR8Unorm;
        case SurfaceFormat::RG8:      return MTLPixelFormatRG8Unorm;
        case SurfaceFormat::RGBA8:    return MTLPixelFormatRGBA8Unorm;
        case SurfaceFormat::BGRA8:    return MTLPixelFormatBGRA8Unorm;
        case SurfaceFormat::R16F:     return MTLPixelFormatR16Float;
        case SurfaceFormat::RG16F:    return MTLPixelFormatRG16Float;
        case SurfaceFormat::RGBA16F:  return MTLPixelFormatRGBA16Float;
        case SurfaceFormat::R32F:     return MTLPixelFormatR32Float;
        case SurfaceFormat::RGBA32F:  return MTLPixelFormatRGBA32Float;
        case SurfaceFormat::R16:      return MTLPixelFormatR16Unorm;
        case SurfaceFormat::RG16:     return MTLPixelFormatRG16Unorm;
        // O Metal NÃO tem profundidade de 24 bits. O mapa de sombra do motor
        // pede Depth24 em alguns caminhos: vira 32F (mesmo resultado visual,
        // 4 bytes por texel). `caps_.depth24Attachment` sai false — a verdade.
        case SurfaceFormat::Depth24:  return MTLPixelFormatDepth32Float;
        case SurfaceFormat::Depth32F: return MTLPixelFormatDepth32Float;
        case SurfaceFormat::RGBA8_sRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
        case SurfaceFormat::BC7:        return MTLPixelFormatBC7_RGBAUnorm;
        case SurfaceFormat::BC7_sRGB:   return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        case SurfaceFormat::ETC2_RGBA8: return MTLPixelFormatETC2_RGBA8Unorm;
        case SurfaceFormat::ETC2_RGBA8_sRGB: return MTLPixelFormatETC2_RGBA8Unorm_sRGB;
        case SurfaceFormat::ASTC4x4:    return MTLPixelFormatASTC_4x4_LDR;
        case SurfaceFormat::ASTC4x4_sRGB: return MTLPixelFormatASTC_4x4_sRGB;
    }
    return MTLPixelFormatRGBA8Unorm;
}

SurfaceFormat from_mtl(MTLPixelFormat f) noexcept {
    switch (f) {
        case MTLPixelFormatBGRA8Unorm: return SurfaceFormat::BGRA8;
        case MTLPixelFormatRGBA16Float: return SurfaceFormat::RGBA16F;
        default: return SurfaceFormat::RGBA8;
    }
}

bool is_compressed_format(MTLPixelFormat f) noexcept {
    switch (f) {
        case MTLPixelFormatBC7_RGBAUnorm: case MTLPixelFormatBC7_RGBAUnorm_sRGB:
        case MTLPixelFormatETC2_RGBA8Unorm: case MTLPixelFormatETC2_RGBA8Unorm_sRGB:
        case MTLPixelFormatASTC_4x4_LDR: case MTLPixelFormatASTC_4x4_sRGB:
            return true;
        default:
            return false;
    }
}

MTLVertexFormat to_mtl(VertexFormat f) noexcept {
    switch (f) {
        case VertexFormat::Float:       return MTLVertexFormatFloat;
        case VertexFormat::Float2:      return MTLVertexFormatFloat2;
        case VertexFormat::Float3:      return MTLVertexFormatFloat3;
        case VertexFormat::Float4:      return MTLVertexFormatFloat4;
        case VertexFormat::UByte4Norm:  return MTLVertexFormatUChar4Normalized;
        case VertexFormat::UShort2Norm: return MTLVertexFormatUShort2Normalized;
        case VertexFormat::UShort4Norm: return MTLVertexFormatUShort4Normalized;
        case VertexFormat::UByte4:      return MTLVertexFormatUChar4;
        case VertexFormat::UShort4:     return MTLVertexFormatUShort4;
        case VertexFormat::Short4Norm:  return MTLVertexFormatShort4Normalized;
    }
    return MTLVertexFormatFloat3;
}

MTLCompareFunction to_mtl(CompareOp c) noexcept {
    switch (c) {
        case CompareOp::Never:          return MTLCompareFunctionNever;
        case CompareOp::Less:           return MTLCompareFunctionLess;
        case CompareOp::Equal:          return MTLCompareFunctionEqual;
        case CompareOp::LessOrEqual:    return MTLCompareFunctionLessEqual;
        case CompareOp::Greater:        return MTLCompareFunctionGreater;
        case CompareOp::NotEqual:       return MTLCompareFunctionNotEqual;
        case CompareOp::GreaterOrEqual: return MTLCompareFunctionGreaterEqual;
        case CompareOp::Always:         return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionLessEqual;
}

MTLPrimitiveType to_mtl(Topology t) noexcept {
    switch (t) {
        case Topology::TriangleList:  return MTLPrimitiveTypeTriangle;
        case Topology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
        case Topology::PointList:     return MTLPrimitiveTypePoint;
        case Topology::LineList:      return MTLPrimitiveTypeLine;
    }
    return MTLPrimitiveTypeTriangle;
}

MTLPrimitiveTopologyClass to_mtl_topology(Topology t) noexcept {
    switch (t) {
        case Topology::PointList: return MTLPrimitiveTopologyClassPoint;
        case Topology::LineList:  return MTLPrimitiveTopologyClassLine;
        default:                  return MTLPrimitiveTopologyClassTriangle;
    }
}

MTLSamplerAddressMode to_mtl(SamplerDesc::Wrap w) noexcept {
    switch (w) {
        case SamplerDesc::Wrap::Repeat:         return MTLSamplerAddressModeRepeat;
        case SamplerDesc::Wrap::ClampToEdge:    return MTLSamplerAddressModeClampToEdge;
        case SamplerDesc::Wrap::MirroredRepeat: return MTLSamplerAddressModeMirrorRepeat;
        case SamplerDesc::Wrap::ClampToBorder:  return MTLSamplerAddressModeClampToBorderColor;
    }
    return MTLSamplerAddressModeClampToEdge;
}

MTLIndexType to_mtl(IndexType t) noexcept {
    return t == IndexType::U16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

const char* stage_entry_point(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:   return "vs_main";
        case ShaderStage::Fragment: return "fs_main";
        case ShaderStage::Compute:  return "cs_main";
    }
    return "main";
}

// =============================================================================
// Anel de memória compartilhada
//
// `MTLResourceStorageModeShared` é a memória unificada do aparelho: a CPU
// escreve no mesmo endereço que a GPU lê. Um bloco por anel, subalocado por
// deslocamento linear que volta a zero no começo do frame — não há alocação por
// draw, e no regime o anel não cresce.
// =============================================================================
namespace {
usize align_up(usize v, usize a) noexcept { return a ? (v + a - 1) / a * a : v; }
} // namespace

bool HostRing::initialize(id<MTLDevice> device, usize capacity, const char* name) noexcept {
    device_ = device;
    name_ = name;
    return add_chunk(capacity);
}

bool HostRing::add_chunk(usize size) noexcept {
    @autoreleasepool {
        id<MTLBuffer> buffer = [device_ newBufferWithLength:size options:MTLResourceStorageModeShared];
        if (!buffer) {
            AUREA_LOG_ERROR("metal: anel '%s' sem memoria (%zu bytes)", name_ ? name_ : "?", size);
            return false;
        }
        buffer.label = [NSString stringWithUTF8String:name_ ? name_ : "anel"];
        chunks_.push_back(Chunk{buffer, size});
        return true;
    }
}

void HostRing::shutdown() noexcept {
    chunks_.clear();
    offset_ = used_ = peak_ = 0;
}

void HostRing::reset() noexcept {
    // Se o frame precisou de mais de um bloco, troca todos por um só do tamanho
    // do pico: em regime não há mais alocação por frame.
    if (chunks_.size() > 1) {
        usize total = 0;
        for (const Chunk& c : chunks_) total += c.size;
        const usize want = std::max(total, peak_);
        shutdown();
        (void)add_chunk(want + want / 4);
    }
    offset_ = 0;
    used_ = 0;
}

bool HostRing::allocate(usize size, usize align, id<MTLBuffer>& outBuffer, u32& outOffset,
                        void*& outPtr) noexcept {
    if (chunks_.empty()) return false;
    Chunk* c = &chunks_.back();
    usize start = align_up(offset_, align ? align : 4);
    if (start + size > c->size) {
        if (!add_chunk(std::max<usize>(size + align + 4, c->size * 2))) return false;
        c = &chunks_.back();
        offset_ = 0;
        start = 0;
    }
    outBuffer = c->buffer;
    outOffset = static_cast<u32>(start);
    outPtr = static_cast<u8*>(c->buffer.contents) + start;
    offset_ = start + size;
    used_ += size;
    peak_ = std::max(peak_, used_);
    return true;
}

// =============================================================================
// Ciclo de vida da instância
// =============================================================================
Backend::Backend() : impl_(std::make_unique<Impl>()) {
    impl_->self = this;
}

Backend::~Backend() { shutdown(); }

Impl& Backend::impl() noexcept { return *impl_; }
const Impl& Backend::impl() const noexcept { return *impl_; }

void Backend::set_device(void* mtlDevice) noexcept {
    if (impl_->initialized) {
        AUREA_LOG_WARN("metal: set_device depois de initialize; ignorado");
        return;
    }
    impl_->requestedDevice = mtlDevice;
}

GPUBackend* create_backend(void* device) noexcept {
    Backend* backend = new (std::nothrow) Backend();
    if (!backend) return nullptr;
    backend->set_device(device);
    return backend;
}

} // namespace aurea::mtl
