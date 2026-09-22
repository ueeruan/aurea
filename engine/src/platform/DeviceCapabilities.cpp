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
    #define NOMINMAX   // sem as macros min/max: std::min/std::max continuam funções
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

/// Slots de tag de codec no PlatformInfo. O mapeamento tag→slot é o MESMO do
/// `PlatformInfo::tag_slot_of` — uma tabela só, senão o dia em que um codec
/// novo entrar aqui e não lá, o decoder some sem erro nenhum.
constexpr u32 kTagSlotAvc1 = 0;
constexpr u32 kTagSlotHvc1 = 1;
constexpr u32 kTagSlotAv01 = 2;
constexpr u32 kTagSlotVp09 = 3;

u32 tag_slot_for(u32 codecTag) noexcept {
    const u32 slot = PlatformInfo::tag_slot_of(codecTag);
    return slot < 8 ? slot : kInvalidIndex;
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

bool DeviceCapabilities::apply_gpu(const GpuCapabilities& gpu) noexcept {
    if (gpu.deviceName.empty()) return false;   // nada medido: não inventa

    const bool changed = gpu.deviceName != gpu_.deviceName
                      || gpu.maxTextureSize != gpu_.maxTextureSize
                      || gpu.totalVideoMemoryBytes != gpu_.totalVideoMemoryBytes;
    gpu_ = gpu;
    // A RAM do sistema é medida pela plataforma, não pela GPU: o backend não
    // tem como saber quanta RAM o aparelho tem, e sobrescrever com zero
    // derrubaria o orçamento para o piso.
    gpu_.totalSystemMemoryBytes = cpu_.totalMemoryBytes;
    compute_budget();
    return changed;
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
    // O que a plataforma mediu ganha da heurística daqui.
    //
    // A daqui olha /proc e /sys DE FORA: conta núcleos e adivinha os grandes
    // pela frequência máxima. A da plataforma sabe o que o sistema operacional
    // sabe — no Android, o `ActivityManager` conhece o limite real de memória
    // do processo, que o /proc não mostra. Sem esta guarda, `apply_platform_info`
    // seria sobrescrito por `detect()` e o doc dele viraria mentira.
    //
    // "Medido" é o que veio em `platform_` (apply_platform_info), NÃO o que
    // está em `cpu_`: os padrões de CpuCapabilities são 1 núcleo / 1 grande /
    // 1 pequeno, e tomá-los por medição fazia TODO aparelho (e o host) sair
    // com 1 núcleo — 1 worker, "1 núcleos (1+1)" nos Ajustes, classe LOW
    // pela CPU. O Kotlin não manda núcleos: eles são medidos aqui.
    CpuCapabilities measured{};
    measured.totalCores           = platform_.totalCores;
    measured.performanceCores     = platform_.performanceCores;
    measured.efficiencyCores      = platform_.performanceCores ? platform_.efficiencyCores : 0;
    measured.maxFrequencyKhz      = platform_.maxFrequencyKhz;
    measured.totalMemoryBytes     = platform_.totalMemoryBytes;
    measured.availableMemoryBytes = platform_.availableMemoryBytes;

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

    // Devolve o que veio medido. `acima` fica o que a heurística só conseguiu
    // estimar; o campo que a plataforma mediu não é reescrito.
    if (measured.totalCores)         cpu_.totalCores = measured.totalCores;
    if (measured.performanceCores) {
        cpu_.performanceCores = measured.performanceCores;
        cpu_.efficiencyCores = measured.efficiencyCores;   // 0 pode ser medição
    } else if (measured.totalCores) {
        // A plataforma contou os núcleos mas não separou: todos iguais (a regra
        // de apply_platform_info — subestimar workers é o lado seguro).
        cpu_.performanceCores = measured.totalCores;
        cpu_.efficiencyCores = 0;
    }
    if (cpu_.performanceCores > cpu_.totalCores) cpu_.performanceCores = cpu_.totalCores;
    if (cpu_.performanceCores + cpu_.efficiencyCores > cpu_.totalCores) {
        cpu_.efficiencyCores = cpu_.totalCores - cpu_.performanceCores;
    }
    if (measured.maxFrequencyKhz)    cpu_.maxFrequencyKhz = measured.maxFrequencyKhz;
    if (measured.totalMemoryBytes)   cpu_.totalMemoryBytes = measured.totalMemoryBytes;
    if (measured.availableMemoryBytes) cpu_.availableMemoryBytes = measured.availableMemoryBytes;
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
    // Aparelho de entrada (§107): o preview já COMEÇA em 1/4 — o adaptativo
    // sobe se o tempo de quadro medido deixar. Composição pequena não desce
    // tanto: abaixo de ~180 linhas o preview vira borrão sem ganhar nada.
    if (class_.tier == DeviceTier::Low) {
        const u32 shortSide = std::min(compWidth, compHeight);
        const u32 den = device_policy(DeviceTier::Low, ThermalTier::Normal).previewInitialDenominator;
        if (den >= 4 && shortSide >= 720) return PreviewScale::Quarter;
        if (den >= 2 && shortSide >= 360) return PreviewScale::Half;
        return PreviewScale::Auto;
    }
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
       << " orcamento_mb=" << (memory_budget_bytes() / (1024ull * 1024ull))
       << " classe=" << device_tier_name(class_.tier)
       << " termico=" << thermal_tier_name(thermal_.tier());
    return os.str();
}

void DeviceCapabilities::compute_budget() noexcept {
    // Teto de preview. Um preview acima da resolução do display não mostra
    // nada a mais — só queima GPU. O teto é a maior entre a resolução lógica
    // do display e 1080p, para não mutilar aparelhos de tela grande.
    maxPreviewWidth_ = 1920;
    maxPreviewHeight_ = 1080;

    // Teto de export. Com a tabela de codecs da plataforma, é o do CODIFICADOR
    // H.264: é ele que escreve o arquivo (o HEVC é opção, o H.264 é o que
    // sempre existe). Antes o teto saía do DECODER — e um aparelho de entrada
    // que decodifica 4K mas só codifica 1080p oferecia 4K e falhava no meio
    // do export. Lado maior × lado menor: o codificador aceita o quadro em pé.
    if (codecs_known()) {
        if (encodeH264_.supported && encodeH264_.maxWidth && encodeH264_.maxHeight) {
            // Nada acima de 4K é oferecido: o resto do pipeline (memória,
            // textura, tempo) não foi medido além disso.
            maxExportWidth_  = std::min<u32>(std::max(encodeH264_.maxWidth, encodeH264_.maxHeight), 3840);
            maxExportHeight_ = std::min<u32>(std::min(encodeH264_.maxWidth, encodeH264_.maxHeight), 2160);
            exportLimit_ = (maxExportHeight_ < 2160) ? ExportLimit::Encoder : ExportLimit::None;
        } else {
            // A tabela veio e não tem H.264: conservador, e a UI diz o porquê.
            maxExportWidth_ = 1920;
            maxExportHeight_ = 1080;
            exportLimit_ = ExportLimit::NoEncoder;
        }
    } else if (detected_) {
        // Sem tabela (host, ou a sondagem falhou): mantido como era — 1080p
        // depois que a GPU real chega — para o host não mudar de
        // comportamento; num aparelho, a tabela sempre vem.
        maxExportWidth_ = 1920;
        maxExportHeight_ = 1080;
        exportLimit_ = ExportLimit::Unknown;
    }

    // Teto por memória: um frame 4K RGBA16F ocupa 3840*2160*8 = 66 MB. Com
    // orçamento de 384 MB, três desses já enchem — então o teto cai para 1080p.
    const u64 budget = memory_budget_bytes();
    const u64 frame4k = 3840ull * 2160ull * 8ull;
    if (budget < frame4k * 6) {
        if (exportLimit_ == ExportLimit::None) exportLimit_ = ExportLimit::Memory;   // era o codificador que deixava 4K
        if (maxExportWidth_ > 1920) maxExportWidth_ = 1920;
        if (maxExportHeight_ > 1080) maxExportHeight_ = 1080;
    }

    class_ = classify_device(class_input());
}

DeviceClassInput DeviceCapabilities::class_input() const noexcept {
    DeviceClassInput in;
    in.totalMemoryBytes     = cpu_.totalMemoryBytes;
    in.availableMemoryBytes = cpu_.availableMemoryBytes;
    in.totalCores           = cpu_.totalCores;
    in.performanceCores     = cpu_.performanceCores;
    in.maxFrequencyKhz      = cpu_.maxFrequencyKhz;

    in.gpuKnown        = !gpu_.deviceName.empty();
    in.gpuApiMajor     = gpu_.apiVersionMajor;
    in.gpuApiMinor     = gpu_.apiVersionMinor;
    in.gpuMaxTexture   = gpu_.maxTextureSize;
    in.gpuMaxWorkgroup = gpu_.maxComputeWorkgroupSize;
    in.gpuCompute      = gpu_.supportsCompute;
    in.gpuFp16         = gpu_.supportsFloat16;
    in.gpuFp16Storage  = gpu_.supportsFloat16Storage;

    in.codecsKnown  = codecs_known();
    in.hwDecodeH264 = decodeH264_.supported && decodeH264_.hardwareAccelerated;
    in.hwDecodeHevc = decodeHevc_.supported && decodeHevc_.hardwareAccelerated;
    for (const CodecCapability* c : {&decodeH264_, &decodeHevc_, &decodeAv1_, &decodeVp9_}) {
        if (!c->supported || !c->hardwareAccelerated) continue;
        const u32 lng = std::max(c->maxWidth, c->maxHeight);
        const u32 sht = std::min(c->maxWidth, c->maxHeight);
        if (static_cast<u64>(lng) * sht > static_cast<u64>(in.hwDecodeMaxLong) * in.hwDecodeMaxShort) {
            in.hwDecodeMaxLong = lng;
            in.hwDecodeMaxShort = sht;
        }
    }
    return in;
}

// =============================================================================
// Classe do aparelho (§110–111)
// =============================================================================
namespace {

constexpr u64 kMB = 1024ull * 1024ull;

/// RAM TOTAL → faixa. Os cortes ficam entre as capacidades nominais, porque o
/// Android reporta menos que o rótulo da caixa (o kernel e a GPU reservam):
/// 3 GB ≈ 2,7 GB · 4 GB ≈ 3,6 · 6 GB ≈ 5,5 · 8 GB ≈ 7,4 · 12 GB ≈ 11,2.
/// Aparelho de 4 GB ainda é de entrada para edição de vídeo: o sistema e a
/// UI ficam com metade, e um único quadro 4K RGBA16F são 66 MB.
DeviceTier memory_tier(u64 total) noexcept {
    if (total == 0) return DeviceTier::Ultra;            // não medido: neutro
    if (total < 4608 * kMB)  return DeviceTier::Low;     // até 4 GB
    if (total < 7168 * kMB)  return DeviceTier::Mid;     // 6 GB
    if (total < 10752 * kMB) return DeviceTier::High;    // 8 GB
    return DeviceTier::Ultra;                            // 12 GB ou mais
}

/// GPU pelos LIMITES que o backend respondeu — não pelo nome. O que separa
/// uma GPU de entrada de uma de topo, entre os limites expostos:
///   · API: Vulkan 1.0 é driver de geração antiga (Mali-T, Adreno 3xx/4xx);
///   · compute: sem ele, flow, partículas e metade dos efeitos não rodam;
///   · invocações por workgroup: 256 (Mali-T, Mali-G31/G51) × 1024 (Adreno
///     6xx/7xx, Mali-G7xx, Xclipse, GPUs de desktop);
///   · fp16 aritmético/armazenamento: metade da banda nos efeitos;
///   · textura 2D máxima.
DeviceTier gpu_tier(const DeviceClassInput& in) noexcept {
    const u32 api = in.gpuApiMajor * 100 + in.gpuApiMinor;
    if (!in.gpuCompute || in.gpuMaxTexture < 4096 || in.gpuMaxWorkgroup < 256 || api < 101) {
        return DeviceTier::Low;
    }
    if (in.gpuMaxWorkgroup < 512 || !in.gpuFp16 || in.gpuMaxTexture < 8192) return DeviceTier::Mid;
    if (in.gpuMaxWorkgroup >= 1024 && api >= 103 && in.gpuMaxTexture >= 16384 && in.gpuFp16Storage) {
        return DeviceTier::Ultra;
    }
    return DeviceTier::High;
}

/// CPU pela frequência máxima do núcleo mais rápido. É o que manda na thread
/// de render e no decode de software; o número de núcleos só pesa no extremo
/// (menos de 4 = entrada). Sem frequência medida (Windows, iOS): neutro.
DeviceTier cpu_tier(const DeviceClassInput& in) noexcept {
    if (in.totalCores > 0 && in.totalCores < 4) return DeviceTier::Low;
    const u32 f = in.maxFrequencyKhz;
    if (f == 0) return DeviceTier::Ultra;
    if (f < 2'200'000) return DeviceTier::Low;    // A53/A55 a 2,0 GHz, Helio G85, SD 665
    if (f < 2'400'000) return DeviceTier::Mid;    // SD 720G/732G, Dimensity 700
    if (f < 2'950'000) return DeviceTier::High;   // SD 778G, 865/888, Tensor G2
    return DeviceTier::Ultra;                     // SD 8 Gen 1+, Dimensity 9000+
}

/// Codecs: sem H.264 de hardware ou sem 1080p de hardware, o preview de vídeo
/// não acompanha o tempo real (entrada). Sem HEVC de hardware ou sem 4K,
/// metade dos vídeos de celular atuais decodifica em software (no máximo MID).
DeviceTier codec_tier(const DeviceClassInput& in) noexcept {
    if (!in.codecsKnown) return DeviceTier::Ultra;
    if (!in.hwDecodeH264 || in.hwDecodeMaxShort < 1080) return DeviceTier::Low;
    if (!in.hwDecodeHevc || in.hwDecodeMaxShort < 2160) return DeviceTier::Mid;
    return DeviceTier::Ultra;
}

DeviceTier lower(DeviceTier t) noexcept {
    return t == DeviceTier::Low ? DeviceTier::Low : static_cast<DeviceTier>(static_cast<u8>(t) - 1);
}

} // namespace

DeviceClass classify_device(const DeviceClassInput& in) noexcept {
    DeviceClass c;
    c.memory = memory_tier(in.totalMemoryBytes);
    c.gpu    = in.gpuKnown ? gpu_tier(in) : DeviceTier::Mid;
    c.cpu    = cpu_tier(in);
    c.codecs = codec_tier(in);

    // Pressão AGORA: com menos de 512 MB livres, ou menos de 1/8 da RAM, o
    // sistema já está matando processos — o plano desce uma faixa.
    DeviceTier memNow = c.memory;
    const bool pressure = in.availableMemoryBytes > 0 && in.totalMemoryBytes > 0
                       && (in.availableMemoryBytes < 512 * kMB || in.availableMemoryBytes * 8 < in.totalMemoryBytes);
    if (pressure) memNow = lower(c.memory);

    // A menor faixa manda; o empate fica com o eixo mais grave (a ordem abaixo).
    struct Axis { DeviceTier t; DeviceLimit why; };
    const Axis axes[] = {
        {c.memory, DeviceLimit::Memory},
        {memNow,   DeviceLimit::MemoryPressure},
        {c.gpu,    in.gpuKnown ? DeviceLimit::Gpu : DeviceLimit::GpuUnknown},
        {c.cpu,    DeviceLimit::Cpu},
        {c.codecs, DeviceLimit::Codecs},
    };
    c.tier = DeviceTier::Ultra;
    c.limit = DeviceLimit::None;
    for (const Axis& a : axes) {
        if (static_cast<u8>(a.t) < static_cast<u8>(c.tier)) {
            c.tier = a.t;
            c.limit = a.why;
        }
    }
    return c;
}

const char* device_tier_name(DeviceTier t) noexcept {
    switch (t) {
        case DeviceTier::Low:   return "LOW";
        case DeviceTier::Mid:   return "MID";
        case DeviceTier::High:  return "HIGH";
        case DeviceTier::Ultra: return "ULTRA";
    }
    return "?";
}

const char* thermal_tier_name(ThermalTier t) noexcept {
    switch (t) {
        case ThermalTier::Normal:   return "NORMAL";
        case ThermalTier::Warm:     return "WARM";
        case ThermalTier::Hot:      return "HOT";
        case ThermalTier::Critical: return "CRITICAL";
    }
    return "?";
}

// =============================================================================
// Política (§107–108, §35–37)
// =============================================================================
DevicePolicy device_policy(DeviceTier tier, ThermalTier thermal) noexcept {
    DevicePolicy p;
    p.tier = tier;
    p.thermal = thermal;

    // --- Classe ---------------------------------------------------------------
    switch (tier) {
        case DeviceTier::Low:
            // Perfil de entrada (§107): preview 1/4, metade do custo das
            // operações caras, sombra e partículas menores, proxy para tudo
            // acima de 720p, um terço a menos de cache de decode, prévias de
            // efeito com metade do lado, pipeline de export raso (memória).
            p.previewInitialDenominator  = 4;
            p.heavyScale                 = 0.5f;
            p.shadowMapSize              = 1024;
            p.particleScale              = 0.5f;
            p.preferProxyAboveShortSide  = 720;
            p.decodedFramesBudgetPercent = 16;
            p.effectPreviewMaxSide       = 160;
            p.exportPipelineDepth        = 2;
            break;
        case DeviceTier::Mid:
            // O plano de sempre (o de antes da Fase 8): nada muda para quem já
            // funcionava. Proxy só para fonte acima de 1440p (4K).
            p.preferProxyAboveShortSide  = 1440;
            break;
        case DeviceTier::High:
            p.preferProxyAboveShortSide  = 2160;   // só acima de 4K
            p.effectPreviewMaxSide       = 512;
            break;
        case DeviceTier::Ultra:
            p.preferProxyAboveShortSide  = 0;      // nunca
            p.effectPreviewMaxSide       = 512;
            p.exportPipelineDepth        = 4;
            break;
    }

    // --- Temperatura (§35–36) -------------------------------------------------
    // Por cima da classe, só para baixo. A qualidade do EXPORT não aparece em
    // nenhum ramo (§37): o calor tira paralelismo dele, nunca pixel.
    switch (thermal) {
        case ThermalTier::Normal:
            break;
        case ThermalTier::Warm:
            // Menos trabalho de fundo. O preview fica como está: o usuário
            // não vê diferença, e a folga térmica dura mais.
            p.backgroundPauseMs = 150;
            break;
        case ThermalTier::Hot:
            p.minPreviewDenominator = std::max<u32>(p.minPreviewDenominator, 2);
            p.heavyScale     *= 0.5f;          // partículas, amostras de blur, flow
            p.particleScale  *= 0.5f;
            p.shadowMapSize   = std::max<u32>(512, p.shadowMapSize / 2);
            p.backgroundPauseMs = 500;
            p.exportPipelineDepth = std::max<u32>(1, p.exportPipelineDepth - 1);
            break;
        case ThermalTier::Critical:
            // O editor tem de continuar respondendo: preview no mínimo útil
            // (1/4, mistura no lugar do flow, sombra 512), fundo quase parado
            // e export serial. Nada é desligado — só fica mais barato.
            p.minPreviewDenominator = std::max<u32>(p.minPreviewDenominator, 4);
            p.heavyScale     = std::min(0.25f, p.heavyScale * 0.25f);
            p.particleScale  = std::min(0.25f, p.particleScale * 0.25f);
            p.shadowMapSize  = 512;
            p.backgroundPauseMs = 1000;
            p.exportPipelineDepth = 1;
            break;
    }
    // Pisos: abaixo disto o renderer já trava por conta própria (0,1 blur,
    // 0,05 partículas), e um preview irreconhecível não ajuda ninguém.
    p.heavyScale    = std::max(0.125f, p.heavyScale);
    p.particleScale = std::max(0.0625f, p.particleScale);
    p.previewInitialDenominator = std::max(p.previewInitialDenominator, p.minPreviewDenominator);
    p.previewOpticalFlow = p.heavyScale > 0.25f;
    return p;
}

void write_device_report(const DeviceCapabilities& caps, i64 out[kDeviceReportSlots]) noexcept {
    constexpr u64 mb = 1024ull * 1024ull;
    const DeviceClass& cls = caps.device_class();
    const DeviceClassInput in = caps.class_input();
    const DevicePolicy pol = caps.policy();
    const DevicePolicy base = device_policy(cls.tier, ThermalTier::Normal);

    for (u32 i = 0; i < kDeviceReportSlots; ++i) out[i] = 0;
    out[0]  = caps.cpu().totalCores;
    out[1]  = caps.cpu().performanceCores;
    out[2]  = caps.cpu().efficiencyCores;
    out[3]  = static_cast<i64>(caps.cpu().totalMemoryBytes / mb);
    out[4]  = static_cast<i64>(caps.cpu().availableMemoryBytes / mb);
    out[5]  = static_cast<i64>(caps.memory_budget_bytes() / mb);
    out[6]  = caps.max_texture_dimension();
    out[7]  = caps.max_preview_width();
    out[8]  = caps.max_preview_height();
    out[9]  = caps.max_export_width();
    out[10] = caps.max_export_height();
    out[11] = caps.decode_parallelism();
    out[12] = caps.recommended_worker_count();
    out[13] = static_cast<i64>(caps.recommended_initial_scale(1920, 1080));
    out[14] = static_cast<i64>(cls.tier);
    out[15] = static_cast<i64>(cls.limit);
    out[16] = static_cast<i64>(cls.memory);
    out[17] = static_cast<i64>(cls.gpu);
    out[18] = static_cast<i64>(cls.cpu);
    out[19] = static_cast<i64>(cls.codecs);

    i64 bits = 0;
    if (in.codecsKnown) bits |= kReportCodecsKnown;
    if (in.hwDecodeH264) bits |= kReportHwDecodeH264;
    if (in.hwDecodeHevc) bits |= kReportHwDecodeHevc;
    if (in.hwDecodeMaxShort >= 2160) bits |= kReportHwDecode4K;
    if (caps.encoder_h264().supported) bits |= kReportEncodeH264;
    if (caps.encoder_h264().supported && caps.encoder_h264().hardwareAccelerated) bits |= kReportHwEncodeH264;
    if (caps.encoder_hevc().supported) bits |= kReportEncodeHevc;
    if (in.gpuKnown) bits |= kReportGpuKnown;
    out[20] = bits;
    out[21] = static_cast<i64>(caps.export_limit());
    if (caps.encoder_hevc().supported) {
        out[22] = std::max(caps.encoder_hevc().maxWidth, caps.encoder_hevc().maxHeight);
        out[23] = std::min(caps.encoder_hevc().maxWidth, caps.encoder_hevc().maxHeight);
    }
    out[24] = static_cast<i64>(pol.heavyScale * 100.0f + 0.5f);
    out[25] = base.previewInitialDenominator;
    out[26] = base.effectPreviewMaxSide;
    out[27] = base.decodedFramesBudgetPercent;
    out[28] = static_cast<i64>(pol.thermal);
    out[29] = base.shadowMapSize;
    out[30] = base.preferProxyAboveShortSide;
    out[31] = caps.cpu().maxFrequencyKhz / 1000;
}

} // namespace aurea
