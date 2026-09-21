#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <sstream>

#if defined(AUREA_PLATFORM_HOST) && defined(_WIN32)
    // CUIDADO COM O NOME DESTE ARQUIVO.
    //
    // `<windows.h>` inclui `wingdi.h`, que declara as funções de impressora
    // `DeviceCapabilitiesA` / `DeviceCapabilitiesW` e — sem UNICODE — define
    //
    //     #define DeviceCapabilities DeviceCapabilitiesA
    //
    // A macro colide exatamente com o nome desta classe. O resultado não é um
    // erro claro: o compilador passa a ver `DeviceCapabilities` como nome de
    // FUNÇÃO, e o primeiro sintoma aparece em outro arquivo, como
    // "';' ausente antes do identificador 'caps'".
    //
    // `NOGDI` impede a inclusão do `wingdi.h` e resolve a colisão sem renomear
    // nada — o nome da classe é o correto para o motor. `WIN32_LEAN_AND_MEAN`
    // evita trazer winsock, shell e companhia, que ninguém aqui usa.
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #include <windows.h>
#elif defined(AUREA_PLATFORM_HOST) || defined(__ANDROID__)
    #include <unistd.h>
    #if defined(__linux__)
        #include <sys/sysinfo.h>
    #endif
#endif

namespace aurea {
namespace {

#if defined(__linux__)
/// Lê um inteiro de um arquivo de /proc ou /sys; 0 se não existir.
u64 read_u64_file(const char* path) noexcept {
    FILE* f = std::fopen(path, "r");
    if (!f) return 0;
    unsigned long long v = 0;
    if (std::fscanf(f, "%llu", &v) != 1) v = 0;
    std::fclose(f);
    return v;
}

/// `MemAvailable` de /proc/meminfo em bytes (0 se indisponível).
u64 mem_available_bytes() noexcept {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[160];
    u64 kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long long v = 0;
        if (std::sscanf(line, "MemAvailable: %llu kB", &v) == 1) {
            kb = v;
            break;
        }
    }
    std::fclose(f);
    return kb * 1024ull;
}
#endif

/// Slots de tag de codec no PlatformInfo.
constexpr u32 kTagSlotAvc1 = 0;
constexpr u32 kTagSlotHvc1 = 1;
constexpr u32 kTagSlotAv01 = 2;
constexpr u32 kTagSlotVp09 = 3;

u32 tag_slot_for(u32 codecTag) noexcept {
    switch (codecTag) {
        case 0x61766331u: return kTagSlotAvc1;   // 'avc1'
        case 0x68766331u: return kTagSlotHvc1;   // 'hvc1'
        case 0x61763031u: return kTagSlotAv01;   // 'av01'
        case 0x76703039u: return kTagSlotVp09;   // 'vp09'
        default: return kInvalidIndex;
    }
}

const CodecCapability kUnsupported{};

} // namespace

void DeviceCapabilities::apply_platform_info(const PlatformInfo& info) noexcept {
    platform_ = info;

    if (info.totalCores) cpu_.totalCores = info.totalCores;
    if (info.performanceCores) cpu_.performanceCores = info.performanceCores;
    if (info.efficiencyCores) cpu_.efficiencyCores = info.efficiencyCores;
    if (info.maxFrequencyKhz) cpu_.maxFrequencyKhz = info.maxFrequencyKhz;
    if (info.totalMemoryBytes) cpu_.totalMemoryBytes = info.totalMemoryBytes;
    if (info.availableMemoryBytes) cpu_.availableMemoryBytes = info.availableMemoryBytes;

    // Se a plataforma não separou núcleos, assume que todos são iguais. Nunca
    // assume que a maioria é "grande": subestimar workers é seguro, superestimar
    // deixa o aparelho sem CPU para a thread de render.
    if (!info.performanceCores && info.totalCores) {
        cpu_.performanceCores = info.totalCores;
        cpu_.efficiencyCores = 0;
    }

    if (info.gpu.deviceName[0] != '\0') gpu_ = info.gpu;
    gpu_.totalSystemMemoryBytes = cpu_.totalMemoryBytes;

    apply_platform_codecs();
}

void DeviceCapabilities::apply_platform_codecs() noexcept {
    auto pick = [this](u32 codecTag, bool encoder) -> const CodecCapability& {
        const u32 slot = tag_slot_for(codecTag);
        if (slot == kInvalidIndex) return kUnsupported;
        const u32 count = encoder ? platform_.encoderCount : platform_.decoderCount;
        const u32* table = encoder ? platform_.encoderIndexForTag
                                   : platform_.decoderIndexForTag;
        const u32 idx = table[slot];
        if (idx == kInvalidIndex || idx >= count) return kUnsupported;
        return encoder ? platform_.encoders[idx] : platform_.decoders[idx];
    };

    decodeH264_ = pick(0x61766331u, false);
    decodeHevc_ = pick(0x68766331u, false);
    decodeAv1_  = pick(0x61763031u, false);
    decodeVp9_  = pick(0x76703039u, false);

    encodeH264_ = pick(0x61766331u, true);
    encodeHevc_ = pick(0x68766331u, true);
    encodeAv1_  = pick(0x61763031u, true);
}

const CodecCapability& DeviceCapabilities::best_decoder_for(u32 codecTag) const noexcept {
    const u32 slot = tag_slot_for(codecTag);
    if (slot == kInvalidIndex) return kUnsupported;

    const CodecCapability* chosen = nullptr;
    switch (slot) {
        case kTagSlotAvc1: chosen = &decodeH264_; break;
        case kTagSlotHvc1: chosen = &decodeHevc_; break;
        case kTagSlotAv01: chosen = &decodeAv1_; break;
        case kTagSlotVp09: chosen = &decodeVp9_; break;
        default: return kUnsupported;
    }
    if (chosen && chosen->supported) return *chosen;

    // Fallback em cascata, na ordem de compatibilidade real de aparelho:
    // AV1 → HEVC → H.264. Só devolve "não suportado" quando não há nenhum —
    // e quem chama precisa então avisar o usuário que a reprodução será lenta,
    // porque o caminho de software não acompanha o tempo real em 4K.
    if (slot == kTagSlotAv01 && decodeHevc_.supported) return decodeHevc_;
    if ((slot == kTagSlotAv01 || slot == kTagSlotHvc1) && decodeH264_.supported) return decodeH264_;

    return kUnsupported;
}

void DeviceCapabilities::detect_cpu() noexcept {
#if defined(AUREA_PLATFORM_HOST)
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    cpu_.totalCores = hw;

    #if defined(_WIN32)
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        if (si.dwNumberOfProcessors) cpu_.totalCores = si.dwNumberOfProcessors;

        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            cpu_.totalMemoryBytes = ms.ullTotalPhys;
            cpu_.availableMemoryBytes = ms.ullAvailPhys;
        }
        // No Windows não há distinção exposta de núcleos grandes/pequenos de
        // forma portável. Assume todos iguais — e o pool deixa 2 de folga.
        cpu_.performanceCores = cpu_.totalCores;
        cpu_.efficiencyCores = 0;
    #else
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0) {
            cpu_.totalMemoryBytes = static_cast<u64>(pages) * static_cast<u64>(pageSize);
        }
        cpu_.performanceCores = cpu_.totalCores;
    #endif
#elif defined(__ANDROID__)
    // Android é Linux: núcleos pelo sysconf, clusters pela frequência máxima
    // de cada CPU (big.LITTLE: os "grandes" são os de frequência mais alta) e
    // memória pelo /proc/meminfo.
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    cpu_.totalCores = n > 0 ? static_cast<u32>(n) : 1u;
    u64 freqs[64]{};
    u64 top = 0;
    const u32 count = std::min<u32>(cpu_.totalCores, 64);
    for (u32 i = 0; i < count; ++i) {
        char path[96];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", i);
        freqs[i] = read_u64_file(path);
        top = std::max(top, freqs[i]);
    }
    if (top > 0) {
        u32 big = 0;
        for (u32 i = 0; i < count; ++i) if (freqs[i] * 10 >= top * 8) ++big;   // ≥ 80 % do topo
        cpu_.performanceCores = std::max(1u, big);
        cpu_.efficiencyCores = cpu_.totalCores > cpu_.performanceCores ? cpu_.totalCores - cpu_.performanceCores : 0;
        cpu_.maxFrequencyKhz = static_cast<u32>(top);
    } else {
        cpu_.performanceCores = cpu_.totalCores;
    }
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) cpu_.totalMemoryBytes = static_cast<u64>(pages) * static_cast<u64>(pageSize);
    cpu_.availableMemoryBytes = mem_available_bytes();
#else
    // iOS: os números vêm de apply_platform_info. Se ninguém chamou, o padrão
    // conservador é 1 núcleo — o pior caso plausível.
    if (cpu_.totalCores == 0) cpu_.totalCores = 1;
    if (cpu_.performanceCores == 0) cpu_.performanceCores = cpu_.totalCores;
#endif
}

void DeviceCapabilities::detect_gpu_host() noexcept {
#if defined(AUREA_PLATFORM_HOST)
    // No host não há backend gráfico obrigatório: os testes de timeline,
    // animação e serialização rodam sem GPU. Os limites ficam no padrão
    // conservador e `gpu_` permanece sem device.
    if (gpu_.totalSystemMemoryBytes == 0) {
        gpu_.totalSystemMemoryBytes = cpu_.totalMemoryBytes;
    }
#endif
}

void DeviceCapabilities::detect() noexcept {
    detect_cpu();
    apply_platform_codecs();
    detect_gpu_host();
    compute_budget();
    detected_ = true;

    AUREA_LOG_INFO("dispositivo: %u nucleos (%u grandes), %llu MB RAM disponivel",
                   cpu_.totalCores, cpu_.performanceCores,
                   static_cast<unsigned long long>(cpu_.availableMemoryBytes / (1024ull * 1024ull)));
}

void DeviceCapabilities::refresh_dynamic() noexcept {
#if defined(AUREA_PLATFORM_HOST) && defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        cpu_.availableMemoryBytes = ms.ullAvailPhys;
    }
#elif defined(__ANDROID__)
    if (const u64 avail = mem_available_bytes()) cpu_.availableMemoryBytes = avail;
#endif
    // O estado térmico e a memória de aparelho vêm de set_thermal_state e de
    // apply_platform_info, chamados pela camada de plataforma. Aqui não se
    // inventa um valor.
}

u32 DeviceCapabilities::recommended_worker_count() const noexcept {
    const u32 perf = cpu_.performanceCores ? cpu_.performanceCores : cpu_.totalCores;

    // Deixa 1 núcleo para a thread de render e 1 para o sistema. Um pool que
    // ocupa tudo faz o scheduler tirar a thread de render da CPU no meio do
    // frame — o frame atrasa e o pool não compensa.
    u32 workers = perf > 2 ? perf - 2 : 1;

    // Teto por memória: cada worker de decode pode segurar alguns MB de
    // buffers. Num aparelho com pouca RAM, mais worker = menos memória para
    // cache = mais decode repetido. Não vale a pena.
    const u64 avail = cpu_.availableMemoryBytes;
    if (avail > 0) {
        const u32 byMemory = static_cast<u32>(avail / (64ull * 1024ull * 1024ull));
        if (byMemory < workers) workers = byMemory;
    }

    if (workers < 1) workers = 1;
    if (workers > 8) workers = 8;   // mais que isso não ajuda num celular
    return workers;
}

u32 DeviceCapabilities::decode_parallelism() const noexcept {
    // Limitado pelo número de instâncias de decoder de hardware. Pedir 8
    // decodes a um aparelho com 2 instâncias faz 6 esperarem na sessão — o
    // resultado é mais lento do que pedir 2.
    u32 instances = 0;
    if (decodeH264_.supported && decodeH264_.hardwareAccelerated) instances += decodeH264_.concurrentInstances;
    if (decodeHevc_.supported && decodeHevc_.hardwareAccelerated) instances += decodeHevc_.concurrentInstances;
    if (decodeAv1_.supported  && decodeAv1_.hardwareAccelerated)  instances += decodeAv1_.concurrentInstances;

    if (instances == 0) {
        // Nenhum decoder de hardware: o caminho é software, e paralelizar
        // decode em software compete com o compositor pelos mesmos núcleos.
        return 1;
    }
    const u32 workers = recommended_worker_count();
    return instances < workers ? instances : workers;
}

u64 DeviceCapabilities::memory_budget_bytes() const noexcept {
    const u64 avail = cpu_.availableMemoryBytes;
    if (avail == 0) {
        // Sem medição: 384 MB é o teto que qualquer aparelho que roda o Aurea
        // aguenta. Subir sem medir é como o processo é morto em background.
        return 384ull * 1024ull * 1024ull;
    }
    // Um quarto do disponível. O resto é do sistema, da UI nativa (Compose /
    // SwiftUI alocam bastante), do próprio app e das folgas do alocador.
    return avail / 4;
}

PreviewScale DeviceCapabilities::recommended_initial_scale(u32 compWidth,
                                                           u32 compHeight) const noexcept {
    if (compWidth <= maxPreviewWidth_ && compHeight <= maxPreviewHeight_) {
        return PreviewScale::Auto;
    }
    // Composição acima do teto do aparelho já começa reduzida — começar em
    // FULL e descer no primeiro frame mostra um engasgo desnecessário ao
    // abrir o projeto.
    if (compWidth <= maxPreviewWidth_ * 2 && compHeight <= maxPreviewHeight_ * 2) {
        return PreviewScale::Half;
    }
    return PreviewScale::Quarter;
}

std::string DeviceCapabilities::summary() const {
    std::ostringstream os;
    os << "nucleos=" << cpu_.totalCores
       << " grandes=" << cpu_.performanceCores
       << " ram_disp_mb=" << (cpu_.availableMemoryBytes / (1024ull * 1024ull))
       << " gpu=" << (gpu_.deviceName.empty() ? "desconhecida" : gpu_.deviceName)
       << " vulkan=" << (gpu_.vulkan ? "sim" : "nao")
       << " metal=" << (gpu_.metal ? "sim" : "nao")
       << " max_textura=" << gpu_.maxTextureSize
       << " decode_h264=" << (decodeH264_.supported ? (decodeH264_.hardwareAccelerated ? "hw" : "sw") : "nao")
       << " decode_hevc=" << (decodeHevc_.supported ? (decodeHevc_.hardwareAccelerated ? "hw" : "sw") : "nao")
       << " decode_av1=" << (decodeAv1_.supported ? (decodeAv1_.hardwareAccelerated ? "hw" : "sw") : "nao")
       << " encode_h264=" << (encodeH264_.supported ? (encodeH264_.hardwareAccelerated ? "hw" : "sw") : "nao")
       << " orcamento_mb=" << (memory_budget_bytes() / (1024ull * 1024ull));
    return os.str();
}

void DeviceCapabilities::compute_budget() noexcept {
    // Teto de preview. Um preview acima da resolução do display não mostra
    // nada a mais — só queima GPU. O teto é a maior entre a resolução lógica
    // do display e 1080p, para não mutilar aparelhos de tela grande.
    maxPreviewWidth_ = 1920;
    maxPreviewHeight_ = 1080;

    // Teto de export limitado pelo decoder: um aparelho que não decodifica 4K
    // não deve deixar o usuário escolher export 4K sem aviso — a exportação
    // sairia com frames faltando ou levaria uma eternidade em software.
    u32 decMaxW = 0, decMaxH = 0;
    for (const CodecCapability* c : {&decodeH264_, &decodeHevc_, &decodeAv1_}) {
        if (!c->supported) continue;
        if (c->maxWidth > decMaxW) decMaxW = c->maxWidth;
        if (c->maxHeight > decMaxH) decMaxH = c->maxHeight;
    }

    if (decMaxW && decMaxH) {
        maxExportWidth_ = decMaxW;
        maxExportHeight_ = decMaxH;
    } else if (detected_) {
        // Nenhum decoder detectado: o caminho é software. Exportar em software
        // aguenta 1080p com paciência; 4K não.
        maxExportWidth_ = 1920;
        maxExportHeight_ = 1080;
    }

    // Teto por memória: um frame 4K RGBA16F ocupa 3840*2160*8 = 66 MB. Com
    // orçamento de 384 MB, três desses já enchem — então o teto cai para 1080p.
    const u64 budget = memory_budget_bytes();
    const u64 frame4k = 3840ull * 2160ull * 8ull;
    if (budget < frame4k * 6) {
        if (maxExportWidth_ > 1920) maxExportWidth_ = 1920;
        if (maxExportHeight_ > 1080) maxExportHeight_ = 1080;
    }
}

} // namespace aurea
