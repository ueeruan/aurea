#include "aurea/export/OfflineAudioSnapshot.hpp"

#include "aurea/core/Time.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>

namespace aurea {
namespace {
constexpr u64 kBlockBytes = static_cast<u64>(audio::kBlockFrames) * audio::kMixChannels * sizeof(f32);
constexpr u32 kMaximumInstances = 4096;

bool generated_tone(const Layer& layer) noexcept {
    const auto type = effect_type_id(audio::fx_keys::kTone);
    for (const auto& effect : layer.effects)
        if (effect.enabled && effect.type == type) return true;
    return false;
}
bool media_sound(const Layer& layer, const Project& project) noexcept {
    if (layer.kind != LayerKind::Audio && layer.kind != LayerKind::Video) return false;
    const Asset* asset = project.asset(layer.source);
    return asset && asset->has_audio();
}
} // namespace

struct OfflineAudioSnapshot::Impl final : audio::BlockSource {
    struct Node {
        u64 asset = 0;
        std::shared_ptr<audio::AudioMixSnapshot> snapshot;
        audio::MixState state;
        std::unordered_map<const Layer*, Node*> children;
    };
    struct Key {
        u64 asset;
        i64 block;
        bool operator==(const Key& other) const noexcept {
            return asset == other.asset && block == other.block;
        }
    };
    struct Hash {
        usize operator()(const Key& key) const noexcept {
            return static_cast<usize>(key.asset * 1000003u ^ static_cast<u64>(key.block));
        }
    };
    struct Entry {
        std::shared_ptr<audio::AudioBlock> block;
        u64 lastUse = 0;
        u64 leaseEpoch = 0;
    };

    std::vector<std::unique_ptr<Node>> nodes;
    std::unordered_map<u64, Node*> assets;
    std::unordered_map<Key, Entry, Hash> blocks;
    // Mixer caches raw block pointers; pin every returned bus block until the
    // outermost mix completes. All MixState raw pointers reset on the next call.
    std::vector<std::shared_ptr<audio::AudioBlock>> leases;
    Node* root = nullptr;
    audio::BlockSource* realSource = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    std::atomic<u64>* heartbeat = nullptr;
    u64 budget = 0, liveBytes = 0, peakBytes = 0, renderedBlocks = 0;
    u64 clock = 0, epoch = 0, nextAsset = 0xFFFFFFFE00000000ull;
    Status status;

    bool cancelled() const noexcept {
        return cancel && cancel->load(std::memory_order_relaxed);
    }
    void beat() noexcept {
        if (heartbeat) heartbeat->store(monotonic_ns(), std::memory_order_release);
    }
    static bool resolve_nested(void* user, const Layer& layer, u64& asset, i64& length) {
        const auto& node = *static_cast<const Node*>(user);
        const auto found = node.children.find(&layer);
        if (found == node.children.end() || !found->second->snapshot->audible()) return false;
        asset = found->second->asset;
        length = found->second->snapshot->endSample;
        return true;
    }
    Node* build(const Composition& composition, const Project& project,
                audio::AudioBlockCache* cache, audio::AssetPathResolver resolver,
                void* resolverUser, std::vector<const Composition*>& ancestry) {
        if (!status.ok()) return nullptr;
        if (cancelled()) { status = Errc::Cancelled; return nullptr; }
        if (ancestry.size() > 16 ||
            std::find(ancestry.begin(), ancestry.end(), &composition) != ancestry.end()) {
            status = Status{Errc::ProjectCorrupted, "ciclo ou profundidade excessiva no audio offline"};
            return nullptr;
        }
        if (nodes.size() >= kMaximumInstances) {
            status = Status{Errc::BudgetExceeded, "instancias demais no grafo de audio offline"};
            return nullptr;
        }
        auto owned = std::make_unique<Node>();
        Node* node = owned.get();
        // Reserved virtual IDs are checked against all real project handles.
        do { node->asset = ++nextAsset; } while (project.asset(AssetId::unpack(node->asset)));
        nodes.push_back(std::move(owned));
        assets.emplace(node->asset, node);
        ancestry.push_back(&composition);
        bool anySolo = false;
        composition.layers().for_each([&](LayerId, const Layer& layer) {
            if (layer.solo && (layer.kind == LayerKind::Composition ||
                              media_sound(layer, project) || generated_tone(layer))) anySolo = true;
        });
        composition.layers().for_each([&](LayerId, const Layer& layer) {
            if (!status.ok() || layer.kind != LayerKind::Composition || generated_tone(layer) ||
                layer.muted || (anySolo && !layer.solo) || layer.end.value <= layer.start.value ||
                (!layer.source_time_varies() && layer.speed <= 0.0f)) return;
            const auto* child = project.timeline().composition(layer.nested.composition);
            if (!child) {
                status = Status{Errc::ProjectCorrupted, "pre-composicao de audio offline ausente"};
                return;
            }
            if (Node* childNode = build(*child, project, cache, resolver, resolverUser, ancestry))
                node->children.emplace(&layer, childNode);
        });
        ancestry.pop_back();
        if (!status.ok()) return nullptr;
        const audio::ExportNestedAudioSource nested{node, &resolve_nested};
        node->snapshot = audio::build_snapshot(composition, project, cache, resolver, resolverUser, &nested);
        if (!node->snapshot) {
            status = Status{Errc::OutOfMemory, "snapshot de audio offline"};
            return nullptr;
        }
        // Resolver keys are needed only while constructing the snapshot. Drop
        // them now: runtime audio owns no frozen Layer/Composition pointers.
        node->children.clear();
        return node;
    }
    bool reserve_block() noexcept {
        while (liveBytes > budget || kBlockBytes > budget - liveBytes) {
            auto oldest = blocks.end();
            for (auto it = blocks.begin(); it != blocks.end(); ++it) {
                if (it->second.block.use_count() != 1) continue;
                if (oldest == blocks.end() || it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            if (oldest == blocks.end()) {
                status = Status{Errc::BudgetExceeded, "blocos de audio aninhado excedem o teto de memoria"};
                return false;
            }
            blocks.erase(oldest);
            liveBytes -= kBlockBytes;
        }
        liveBytes += kBlockBytes;
        peakBytes = std::max(peakBytes, liveBytes);
        return true;
    }
    const audio::AudioBlock* block(u64 asset, i64 index) override {
        if (!status.ok()) return nullptr;
        if (cancelled()) { status = Errc::Cancelled; return nullptr; }
        try {
            const auto virtualAsset = assets.find(asset);
            if (virtualAsset == assets.end()) {
                const auto* result = realSource ? realSource->block(asset, index) : nullptr;
                beat();
                if (cancelled()) { status = Errc::Cancelled; return nullptr; }
                if (!result && status.ok()) status = Status{Errc::DecodeFailed, "bloco da fonte de audio offline ausente"};
                return result;
            }
            if (index < 0 || index > std::numeric_limits<i64>::max() / audio::kBlockFrames) {
                status = Errc::OutOfRange;
                return nullptr;
            }
            const Key key{asset, index};
            auto found = blocks.find(key);
            if (found != blocks.end()) {
                found->second.lastUse = ++clock;
                if (found->second.leaseEpoch != epoch) {
                    leases.push_back(found->second.block);
                    found->second.leaseEpoch = epoch;
                }
                return found->second.block.get();
            }
            if (!reserve_block()) return nullptr;
            bool reservation = true;
            try {
                auto rendered = std::make_shared<audio::AudioBlock>();
                rendered->pcm.resize(static_cast<usize>(audio::kBlockFrames) * audio::kMixChannels);
                auto& child = *virtualAsset->second;
                audio::MixStats childStats;
                // Child buses preserve headroom. Only the final root output is
                // limited, as with the original flattened playback mix.
                audio::mix(*child.snapshot, index * audio::kBlockFrames, audio::kBlockFrames,
                           *this, rendered->pcm.data(), &childStats, &child.state, false);
                beat();
                if (cancelled() && status.ok()) status = Errc::Cancelled;
                if (childStats.missingBlocks && status.ok())
                    status = Status{Errc::DecodeFailed, "audio aninhado incompleto"};
                if (!status.ok()) { liveBytes -= kBlockBytes; return nullptr; }
                auto inserted = blocks.emplace(key, Entry{rendered, ++clock, epoch});
                reservation = false;
                leases.push_back(rendered);
                ++renderedBlocks;
                return inserted.first->second.block.get();
            } catch (...) {
                if (reservation) liveBytes -= kBlockBytes;
                throw;
            }
        } catch (const std::bad_alloc&) {
            status = Status{Errc::OutOfMemory, "memoria do audio offline"};
        } catch (...) {
            status = Status{Errc::DecodeFailed, "falha na fonte de audio offline"};
        }
        return nullptr;
    }
};

OfflineAudioSnapshot::OfflineAudioSnapshot(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
OfflineAudioSnapshot::~OfflineAudioSnapshot() = default;
Result<std::unique_ptr<OfflineAudioSnapshot>> OfflineAudioSnapshot::create(
    const Composition& root, const Project& project, audio::AudioBlockCache* cache,
    audio::AssetPathResolver resolver, void* resolverUser, u64 budget,
    const std::atomic<bool>* cancel) noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        impl->budget = budget;
        impl->cancel = cancel;
        std::vector<const Composition*> ancestry;
        impl->root = impl->build(root, project, cache, resolver, resolverUser, ancestry);
        if (!impl->status.ok()) return impl->status;
        impl->cancel = nullptr;
        return std::unique_ptr<OfflineAudioSnapshot>(new OfflineAudioSnapshot(std::move(impl)));
    } catch (const std::bad_alloc&) {
        return Status{Errc::OutOfMemory, "snapshot de audio offline"};
    } catch (...) {
        return Status{Errc::ProjectCorrupted, "grafo de audio offline invalido"};
    }
}
const std::shared_ptr<audio::AudioMixSnapshot>& OfflineAudioSnapshot::snapshot() const noexcept {
    return impl_->root->snapshot;
}
Status OfflineAudioSnapshot::mix(i64 start, u32 frames, audio::BlockSource& source, f32* stereo,
                                 audio::MixStats* stats, audio::MixState* state,
                                 const std::atomic<bool>* cancel, std::atomic<u64>* heartbeat) noexcept {
    if (!stereo || !frames || frames > 4096 || start < 0 ||
        start > std::numeric_limits<i64>::max() - frames) return Errc::InvalidArgument;
    auto& impl = *impl_;
    if (impl.realSource) return Status{Errc::InvalidState, "audio offline ja esta em uso"};
    impl.status = OkStatus;
    impl.cancel = cancel;
    impl.heartbeat = heartbeat;
    impl.realSource = &source;
    ++impl.epoch;
    audio::MixStats result;
    if (impl.cancelled()) impl.status = Errc::Cancelled;
    else audio::mix(*impl.root->snapshot, start, frames, impl, stereo, &result, state);
    impl.beat();
    if (result.missingBlocks && impl.status.ok()) impl.status = Errc::DecodeFailed;
    if (impl.cancelled() && impl.status.ok()) impl.status = Errc::Cancelled;
    impl.realSource = nullptr;
    impl.cancel = nullptr;
    impl.heartbeat = nullptr;
    impl.leases.clear();
    if (!impl.status.ok()) std::fill(stereo, stereo + static_cast<usize>(frames) * audio::kMixChannels, 0.0f);
    if (stats) {
        stats->missingBlocks += result.missingBlocks;
        stats->peak = std::max(stats->peak, result.peak);
    }
    return impl.status;
}
OfflineAudioSnapshot::Stats OfflineAudioSnapshot::stats() const noexcept {
    return {impl_->liveBytes, impl_->peakBytes, impl_->renderedBlocks, static_cast<u32>(impl_->nodes.size())};
}

} // namespace aurea
