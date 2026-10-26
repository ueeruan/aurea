// =============================================================================
//  Aurea / hyper / HyperGraph.cpp — a fila limitada e a thread das etapas.
// =============================================================================
#include "HyperGraph.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/export/UpscaleColor.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace aurea::hyper {

namespace {

/// Ajuste final: o quadro inteiro reamostrado (nunca recortado).
class FitStage final : public HyperStage {
public:
    FitStage(u32 w, u32 h) : w_(w), h_(h) {}
    const char* name() const noexcept override { return "ajuste"; }
    StageKind kind() const noexcept override { return StageKind::Upscale; }
    Path path() const noexcept override { return Path::Turbo; }
    Status prepare(const DeviceProfile&, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        if (!w_ || !h_ || (w_ | h_) & 1u) return Errc::InvalidArgument;
        out = in;
        out.width = w_;
        out.height = h_;
        return OkStatus;
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext&) noexcept override {
        const FrameView& v = in.current;
        if (v.yStride != v.width || v.uvStride != v.width || v.uv != v.y + static_cast<usize>(v.width) * v.height)
            return Status{Errc::InvalidArgument, "ajuste espera NV12 contiguo"};
        if (!ai::resize_nv12_709(v.y, v.width, v.height, out.y(), out.width, out.height))
            return Status{Errc::InvalidState, "dimensoes invalidas no ajuste do Hyper"};
        return OkStatus;
    }
private:
    u32 w_, h_;
};

usize nv12_bytes(u32 w, u32 h) noexcept { return static_cast<usize>(w) * h * 3 / 2; }

} // namespace

std::unique_ptr<HyperStage> make_fit_stage(u32 width, u32 height) {
    return std::unique_ptr<HyperStage>(new (std::nothrow) FitStage(width, height));
}

Status HyperGraph::start(const DeviceProfile& profile, const Recipe& recipe,
                         std::span<const StageDescriptor* const> stages, const StageFormat& in,
                         u32 fitW, u32 fitH, const std::atomic<bool>* cancel,
                         void (*beat)(void*) noexcept, void* beatContext) noexcept {
    std::vector<std::unique_ptr<HyperStage>> made;
    try {
        for (const StageDescriptor* d : stages) {
            if (!d || !d->create) return Status{Errc::InvalidArgument, "etapa Hyper sem fabrica"};
            auto stage = d->create();
            if (!stage) return Status{Errc::OutOfMemory, "etapa Hyper nao criada"};
            made.push_back(std::move(stage));
        }
    } catch (...) {
        return Errc::OutOfMemory;
    }
    return start_with(profile, recipe, std::move(made), in, fitW, fitH, cancel, beat, beatContext);
}

Status HyperGraph::start_with(const DeviceProfile& profile, const Recipe& recipe,
                              std::vector<std::unique_ptr<HyperStage>> stages, const StageFormat& in,
                              u32 fitW, u32 fitH, const std::atomic<bool>* cancel,
                              void (*beat)(void*) noexcept, void* beatContext) noexcept {
    shutdown();
    if (!in.width || !in.height || (in.width | in.height) & 1u) return Status{Errc::InvalidArgument, "entrada do grafo"};
    in_ = in;
    cancel_ = cancel;
    stageCtx_ = StageContext{};
    stageCtx_.cancel = cancel;
    stageCtx_.beatFn = beat;
    stageCtx_.beatContext = beatContext;
    stageCtx_.progressFn = [](void* p, f32 f) noexcept {
        static_cast<std::atomic<f32>*>(p)->store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    };
    stageCtx_.progressContext = &frameProgress_;
    failure_ = OkStatus;
    stop_ = false;
    resumable_ = true;
    stageBytes_ = 0;
    allocated_ = 0;
    maxLive_.store(0);
    havePopped_ = false;
    try {
        lanes_.clear();
        StageFormat cur = in;
        for (auto& s : stages) {
            if (!s) return Errc::OutOfMemory;
            Lane lane;
            lane.in = cur;
            const Status prepared = s->prepare(profile, recipe, cur, lane.out);
            if (!prepared.ok()) {
                AUREA_LOG_ERROR("hyper: etapa %s nao preparou (%d)", s->name(), prepared.raw());
                return prepared;
            }
            if (!lane.out.width || !lane.out.height || (lane.out.width | lane.out.height) & 1u)
                return Status{Errc::InvalidState, "etapa Hyper com saida invalida"};
            lane.motion = s->kind() == StageKind::Motion;
            // Motion recebe o par de origem (motionA/B) E, se pedir, os vizinhos.
            lane.radius = std::min<u32>(s->temporal_radius(), 2);
            if (lane.radius > 0 || lane.motion || s->stateful()) resumable_ = false;
            stageBytes_ += s->peak_bytes();
            lane.stage = std::move(s);
            cur = lane.out;
            lanes_.push_back(std::move(lane));
        }
        if (fitW && fitH && (fitW != cur.width || fitH != cur.height)) {
            Lane lane;
            lane.in = cur;
            lane.stage = make_fit_stage(fitW, fitH);
            if (!lane.stage) return Errc::OutOfMemory;
            if (const Status p = lane.stage->prepare(profile, recipe, cur, lane.out); !p.ok()) return p;
            cur = lane.out;
            lanes_.push_back(std::move(lane));
        }
        out_ = cur;
        windows_.assign(lanes_.size() + 1, Window{});
        for (usize k = 0; k < lanes_.size(); ++k)
            windows_[k].capacity = lanes_[k].motion ? std::max(kMotionLookahead + 2, 2 * lanes_[k].radius + 2)
                                                    : 2 * lanes_[k].radius + 2;
        windows_.back().capacity = 2;
        pools_.assign(lanes_.size() + 1, {});
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        lanes_.clear();
        return Errc::OutOfMemory;
    }
    return OkStatus;
}

void HyperGraph::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    for (Lane& l : lanes_) if (l.stage) l.stage->release();
    lanes_.clear();
    windows_.clear();
    pools_.clear();
    popped_ = Item{};
    havePopped_ = false;
}

HyperGraph::Item HyperGraph::take_buffer_locked(u32 pool) noexcept {
    Item item;
    item.pool = pool;
    if (pool < pools_.size() && !pools_[pool].empty()) {
        item.fb = std::move(pools_[pool].back());
        pools_[pool].pop_back();
    } else {
        ++allocated_;
    }
    return item;
}

void HyperGraph::give_back_locked(Item&& item) noexcept {
    if (item.pool < pools_.size()) {
        try {
            pools_[item.pool].push_back(std::move(item.fb));
            return;
        } catch (...) {}
    }
    if (allocated_) --allocated_;
}

u32 HyperGraph::live_locked() const noexcept {
    u32 pooled = 0;
    for (const auto& p : pools_) pooled += static_cast<u32>(p.size());
    return allocated_ > pooled ? allocated_ - pooled : 0;
}

u32 HyperGraph::live_frames() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return live_locked();
}

void HyperGraph::note_live_locked() noexcept {
    const u32 live = live_locked();
    if (live > maxLive_.load(std::memory_order_relaxed)) maxLive_.store(live, std::memory_order_relaxed);
}

u64 HyperGraph::peak_bytes() const noexcept {
    u64 bytes = stageBytes_;
    StageFormat f = in_;
    for (usize k = 0; k <= lanes_.size() && k < windows_.size(); ++k) {
        const StageFormat& at = k == 0 ? in_ : lanes_[k - 1].out;
        f = at;
        bytes += static_cast<u64>(windows_[k].capacity + 1) * nv12_bytes(f.width, f.height);
    }
    return bytes;
}

Result<bool> HyperGraph::try_push(const FrameView& frame) noexcept {
    if (!frame.valid() || frame.width != in_.width || frame.height != in_.height)
        return Status{Errc::InvalidArgument, "quadro com tamanho diferente da entrada do grafo"};
    Item item;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failure_.ok()) return failure_;
        if (windows_.empty() || stop_) return Status{Errc::InvalidState, "grafo parado"};
        if (cancel_ && cancel_->load(std::memory_order_acquire)) return Status{Errc::Cancelled};
        Window& w0 = windows_.front();
        if (w0.ended) return Status{Errc::InvalidState, "entrada do grafo encerrada"};
        if (w0.items.size() >= w0.capacity) return false;
        item = take_buffer_locked(0);
        note_live_locked();
    }
    if (const Status r = item.fb.resize(frame.width, frame.height); !r.ok()) {
        std::lock_guard<std::mutex> lock(mutex_);
        give_back_locked(std::move(item));
        return r;
    }
    for (u32 row = 0; row < frame.height; ++row)
        std::memcpy(item.fb.y() + static_cast<usize>(row) * frame.width, frame.y + static_cast<usize>(row) * frame.yStride, frame.width);
    for (u32 row = 0; row < frame.height / 2; ++row)
        std::memcpy(item.fb.uv() + static_cast<usize>(row) * frame.width, frame.uv + static_cast<usize>(row) * frame.uvStride, frame.width);
    item.fb.ptsUs = frame.ptsUs;
    item.fb.index = frame.index;
    item.fb.sourcePosition = frame.sourcePosition;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            Window& w0 = windows_.front();
            w0.items.push_back(std::move(item));
            ++w0.received;
        } catch (...) {
            failure_ = Errc::OutOfMemory;
        }
    }
    cv_.notify_all();
    return true;
}

void HyperGraph::end_input() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!windows_.empty()) windows_.front().ended = true;
    }
    cv_.notify_all();
}

Result<bool> HyperGraph::pop(const FrameBuffer*& out, u64 timeoutNs) noexcept {
    out = nullptr;
    std::unique_lock<std::mutex> lock(mutex_);
    if (havePopped_) {
        give_back_locked(std::move(popped_));
        popped_ = Item{};
        havePopped_ = false;
        cv_.notify_all();
    }
    if (windows_.empty()) return Status{Errc::NotFound};
    Window& o = windows_.back();
    const auto ready = [&] {
        return !o.items.empty() || !failure_.ok() || o.ended || stop_
            || (cancel_ && cancel_->load(std::memory_order_acquire));
    };
    if (!ready()) cv_.wait_for(lock, std::chrono::nanoseconds(timeoutNs), ready);
    if (!failure_.ok()) return failure_;
    if (cancel_ && cancel_->load(std::memory_order_acquire)) return Status{Errc::Cancelled};
    if (o.items.empty()) {
        if (o.ended) return Status{Errc::NotFound};
        return false;
    }
    popped_ = std::move(o.items.front());
    o.items.pop_front();
    ++o.first;
    havePopped_ = true;
    out = &popped_.fb;
    cv_.notify_all();
    return true;
}

void HyperGraph::run() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    std::vector<FrameView> neighbors;
    std::vector<i32> offsets;
    neighbors.reserve(4);
    offsets.reserve(4);
    const usize n = lanes_.size();
    while (!stop_ && failure_.ok()) {
        if (cancel_ && cancel_->load(std::memory_order_acquire)) { failure_ = Errc::Cancelled; break; }
        // Fim propagado: a etapa que já processou tudo que vai receber fecha
        // a entrada da seguinte.
        for (usize k = 0; k < n; ++k)
            if (windows_[k].ended && lanes_[k].next >= windows_[k].received) windows_[k + 1].ended = true;
        // Do fim para o começo: esvazia o pipeline antes de aceitar mais.
        i64 pick = -1;
        for (usize k = n; k-- > 0;) {
            const Window& w = windows_[k];
            const Lane& l = lanes_[k];
            if (l.next >= w.received) continue;
            if (l.motion) {
                // O centro precisa do quadro B de origem (o 1º à frente com
                // floor maior), a não ser que seja um original, o vídeo tenha
                // acabado ou B esteja além da fila.
                const FrameBuffer& c = w.items[l.next - w.first].fb;
                bool ready = w.ended || c.sourcePosition < 0.0 || w.received - l.next > kMotionLookahead;
                if (!w.ended && l.next + l.radius >= w.received) continue;   // vizinhos pedidos ainda não chegaram
                const f64 cf = std::floor(c.sourcePosition + 1e-3);
                if (!ready && c.sourcePosition - cf < 1e-3) ready = true;
                for (u32 i = l.next + 1; !ready && i < w.received; ++i)
                    if (std::floor(w.items[i - w.first].fb.sourcePosition + 1e-3) > cf) ready = true;
                if (!ready) continue;
            } else if (!(l.next + l.radius < w.received || w.ended)) {
                continue;
            }
            if (windows_[k + 1].items.size() >= windows_[k + 1].capacity) continue;
            pick = static_cast<i64>(k);
            break;
        }
        if (pick < 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(50));
            continue;
        }
        const usize k = static_cast<usize>(pick);
        Lane& lane = lanes_[k];
        Window& w = windows_[k];
        const u32 c = lane.next;
        const u32 last = w.received - 1;
        auto at = [&](u32 index) -> const FrameBuffer& { return w.items[index - w.first].fb; };
        StageInput input;
        neighbors.clear();
        offsets.clear();
        for (i32 o = -static_cast<i32>(lane.radius); o <= static_cast<i32>(lane.radius); ++o) {
            if (o == 0) continue;
            const i64 want = static_cast<i64>(c) + o;
            const u32 idx = static_cast<u32>(std::clamp<i64>(want, w.first, last));
            neighbors.push_back(at(idx).view());
            offsets.push_back(o);
        }
        input.current = at(c).view();
        input.neighbors = neighbors;
        input.offsets = offsets;
        // Motion: A = o próprio quadro (o render repetiu o de origem), B = o
        // 1º à frente com o floor seguinte; fase = a parte fracionária.
        bool reuse = false;
        const f64 cpos = input.current.sourcePosition;
        const i64 cfloor = cpos >= 0.0 ? static_cast<i64>(std::floor(cpos + 1e-3)) : -1;
        if (lane.motion) {
            input.motionA = input.current;
            input.motionB = input.current;
            if (cpos >= 0.0) {
                input.motionPhase = static_cast<f32>(std::clamp(cpos - static_cast<f64>(cfloor), 0.0, 0.999));
                if (input.motionPhase < 1e-3f) input.motionPhase = 0.0f;
                for (u32 i = c + 1; i < w.received; ++i)
                    if (static_cast<i64>(std::floor(at(i).sourcePosition + 1e-3)) > cfloor) { input.motionB = at(i).view(); break; }
            }
        } else if (cfloor >= 0 && lane.haveLast && cfloor == lane.lastFloor) {
            reuse = true;   // o mesmo quadro de origem: a mesma saída
        }
        Item out = take_buffer_locked(static_cast<u32>(k + 1));
        note_live_locked();
        HyperStage* stage = lane.stage.get();
        const StageFormat fmt = lane.out;
        lock.unlock();
        frameProgress_.store(0.0f, std::memory_order_relaxed);
        Status s = out.fb.resize(fmt.width, fmt.height);
        if (s.ok() && reuse) std::memcpy(out.fb.nv12.data(), lane.lastOut.nv12.data(), out.fb.nv12.size());
        else if (s.ok()) s = stage->process(input, out.fb, stageCtx_);
        if (s.ok() && !lane.motion && cfloor >= 0 && !reuse) {
            // Guarda a saída deste quadro de origem (uma cópia por etapa).
            s = lane.lastOut.resize(fmt.width, fmt.height);
            if (s.ok()) std::memcpy(lane.lastOut.nv12.data(), out.fb.nv12.data(), out.fb.nv12.size());
            lane.lastFloor = cfloor;
            lane.haveLast = s.ok();
        }
        out.fb.ptsUs = input.current.ptsUs;
        out.fb.index = input.current.index;
        out.fb.sourcePosition = input.current.sourcePosition;
        stageCtx_.beat();
        lock.lock();
        if (!s.ok()) {
            give_back_locked(std::move(out));
            failure_ = s.code() == Errc::Cancelled || (cancel_ && cancel_->load()) ? Status{Errc::Cancelled} : s;
            if (!(failure_.code() == Errc::Cancelled))
                AUREA_LOG_ERROR("hyper: etapa %s falhou no quadro %u (%d)", stage->name(), input.current.index, s.raw());
            break;
        }
        try {
            Window& dst = windows_[k + 1];
            dst.items.push_back(std::move(out));
            ++dst.received;
        } catch (...) {
            failure_ = Errc::OutOfMemory;
            break;
        }
        ++lane.next;
        // Sai da janela o que nenhum centro futuro usa (índice < next − raio).
        while (!w.items.empty() && static_cast<i64>(w.first) < static_cast<i64>(lane.next) - static_cast<i64>(lane.radius)) {
            give_back_locked(std::move(w.items.front()));
            w.items.pop_front();
            ++w.first;
        }
        cv_.notify_all();
    }
    // Falha/cancelamento: acorda quem espera (push/pop devolvem o motivo).
    cv_.notify_all();
}

} // namespace aurea::hyper
