# =============================================================================
#  Aurea / platform / ios / verify / check_symbols.py
#
#  AUDITORIA DE SIMBOLOS da ponte iOS.
#
#  A pergunta que ele responde: TODO simbolo do nucleo que a ponte chama existe
#  de fato em engine/include/? Um nome inventado (um metodo que "deveria
#  existir") so apareceria no compilador de um Mac — e esta entrega nao tem um.
#
#  Como: extrai das unidades ObjC++/C++ da ponte
#    · nomes qualificados  aurea::Foo / aurea::ns::func / CommandType::X
#    · chamadas  ponteiro->metodo(   (o nucleo e C++: isto e quase sempre nossa)
#    · membros de structs do nucleo usados via  .campo / ->campo
#  e confirma cada um nos cabecalhos de engine/include/.
#
#  Sai com codigo 1 se sobrar qualquer simbolo NAO encontrado (a auditoria tem
#  de chegar a 100%).
# =============================================================================
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))       # engine/
BRIDGE = os.path.join(ROOT, "platform", "ios", "bridge")
INCLUDE = os.path.join(ROOT, "include")

BRIDGE_FILES = ["AureaBridge.mm", "AureaEngine.mm", "IOSSurfaceView.mm",
                "IOSVideoDecoder.mm", "IOSAudio.mm", "AureaBridge.h"]

# Chamadas que NAO sao do nucleo: ObjC em notacao de ponteiro (raro), CF/Core*,
# std::, ARC. Lista curta e explicita — qualquer coisa fora dela tem de existir
# num cabecalho do motor.
KNOWN_NON_CORE = {
    # CoreFoundation / CoreMedia / VideoToolbox / CoreVideo
    "CFRetain", "CFRelease", "CFEqual", "CFArrayGetCount", "CFArrayGetValueAtIndex",
    "CFDictionaryGetValue", "CFNumberGetValue", "CFBooleanGetValue",
    "CMTimeMake", "CMTimeGetSeconds", "CMTimeRangeMake", "CMBlockBufferGetDataPointer",
    "CMBlockBufferCreateWithMemoryBlock", "CMBlockBufferReplaceDataBytes",
    "CMSampleBufferGetPresentationTimeStamp", "CMSampleBufferGetFormatDescription",
    "CMSampleBufferGetDataBuffer", "CMSampleBufferCreateReady", "CMSampleBufferCreate",
    "CMVideoFormatDescriptionGetCleanAperture", "CMFormatDescriptionGetExtensions",
    "CMFormatDescriptionGetMediaSubType", "CMAudioFormatDescriptionGetStreamBasicDescription",
    "CMAudioFormatDescriptionCreate", "CVPixelBufferGetWidth", "CVPixelBufferGetHeight",
    "CVPixelBufferGetPixelFormatType", "CVPixelBufferGetIOSurface",
    "CVPixelBufferGetBaseAddressOfPlane", "CVPixelBufferGetBytesPerRowOfPlane",
    "CVPixelBufferGetHeightOfPlane", "CVPixelBufferLockBaseAddress",
    "CVPixelBufferUnlockBaseAddress", "CVPixelBufferPoolCreate", "CVPixelBufferPoolCreatePixelBuffer",
    "CVPixelBufferRelease", "CVPixelBufferPoolRelease", "CGImageSourceCreateWithURL",
    "CGImageSourceCreateImageAtIndex", "CGImageGetWidth", "CGImageGetHeight",
    "CGImageRelease", "CGColorSpaceCreateDeviceRGB", "CGColorSpaceRelease",
    "CGBitmapContextCreate", "CGContextRelease", "CGContextSetBlendMode", "CGContextDrawImage",
    "IOSurfaceGetID",
    # std
    "data", "size", "empty", "clear", "push_back", "reserve", "resize", "assign",
    "append", "c_str", "load", "store", "begin", "end", "insert", "erase",
    # ObjC sob ARC
    "UTF8String", "localizedDescription", "components", "location", "translation",
}

QUALIFIED = re.compile(r"\baurea::([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)")
CALL = re.compile(r"(?:->|\.)\s*([a-z_][a-z0-9_]*)\s*\(")
ENUM_MEMBER = re.compile(r"\b(CommandType|TrackProperty|BlendMode|Interpolation|MaskOperation|"
                         r"Errc|LogLevel|PixelFormat|BufferUsage|MemoryAccess|SurfaceFormat|"
                         r"ResourceState|LoadOp|ShaderStage|Topology|VertexFormat|CompareOp|"
                         r"CullMode|IndexType|ExportCodec|AudioCodec|ColorSpace|TransferFunction|"
                         r"ColorPrimaries|YCbCrMatrix|LayerKind|PreviewScale|ParticleParam|"
                         r"MaskOperation|ThermalState|ExportLimit|DeviceClass|DecodeMode|"
                         r"MediaPriority|ParticleParam|TextAnimParam|VectorParam|ShapeParam|"
                         r"ParamType)\s*::\s*([A-Za-z_][A-Za-z0-9_]*)")


def read(path):
    return io.open(path, encoding="utf-8", errors="replace").read()


def header_text():
    """Todo o nucleo MAIS o cabecalho da propria ponte.

    O `aurea::ios::` (Host, Batch, StringRef) nao esta em engine/include — e da
    ponte, e o contrato dele e o AureaBridge.h. Sem ele aqui, a auditoria
    acusaria os proprios simbolos dela.
    """
    chunks = []
    for base, _dirs, files in os.walk(INCLUDE):
        for name in files:
            if name.endswith(".hpp"):
                chunks.append(read(os.path.join(base, name)))
    bridge_header = os.path.join(BRIDGE, "AureaBridge.h")
    if os.path.exists(bridge_header):
        chunks.append(read(bridge_header))
    return "\n".join(chunks)


# A FONTE DE VERDADE tambem inclui os .cpp do motor: ha metodos declarados no
# cabecalho, mas tambem nomes que so existem no .cpp (raro, e sinal de erro) —
# conferir no include e o contrato (o "externo" e o header).
HEADERS = header_text()


def exists_as_function(name):
    return re.search(r"\b" + re.escape(name) + r"\s*\(", HEADERS) is not None


def exists_as_word(name):
    return re.search(r"\b" + re.escape(name) + r"\b", HEADERS) is not None


def defined_in_bridge(name):
    """Um tipo da PROPRIA ponte (definido no .mm, num namespace anonimo) nao
    esta em cabecalho nenhum — e nao precisa estar."""
    for name_file in BRIDGE_FILES:
        path = os.path.join(BRIDGE, name_file)
        if not os.path.exists(path):
            continue
        text = read(path)
        if re.search(r"\b(struct|class|enum\s+class)\s+" + re.escape(name) + r"\b", text):
            return True
    return False


def main():
    symbols = 0
    confirmed = 0
    missing = []

    for name in BRIDGE_FILES:
        path = os.path.join(BRIDGE, name)
        if not os.path.exists(path):
            print("ARQUIVO AUSENTE: " + path)
            return 1
        text = read(path)
        # Tira comentarios: um nome citado em comentario nao e uso.
        text = re.sub(r"//[^\n]*", "", text)
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)

        for match in set(QUALIFIED.findall(text)):
            symbols += 1
            leaf = match.split("::")[-1]
            if exists_as_word(leaf) or defined_in_bridge(leaf):
                confirmed += 1
            else:
                missing.append("%s: aurea::%s" % (name, match))

        for enum, member in set(ENUM_MEMBER.findall(text)):
            symbols += 1
            if re.search(r"\b" + re.escape(member) + r"\b", HEADERS):
                confirmed += 1
            else:
                missing.append("%s: %s::%s" % (name, enum, member))

        for call in set(CALL.findall(text)):
            if call in KNOWN_NON_CORE:
                continue
            symbols += 1
            if exists_as_function(call) or defined_in_bridge(call):
                confirmed += 1
            else:
                missing.append("%s: chamada ->%s(" % (name, call))

    print("SIMBOLOS usados pela ponte: %d" % symbols)
    print("CONFIRMADOS em engine/include: %d" % confirmed)
    print("NAO ENCONTRADOS: %d" % len(missing))
    for item in sorted(missing):
        print("   " + item)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
