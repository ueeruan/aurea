// =============================================================================
//  Aurea / tests / SplitParentNullGpu.inl
//
//  Relato 0.2.5 (vídeo de 18/10/2026): DIVIDIR um Nulo que é pai de
//  pré-composições mudava a prévia — fantasma no ponto do corte e outro
//  conteúdo antes dele. Dividir não muda imagem nenhuma: os filhos seguem o
//  mesmo movimento antes e depois do corte, nos dois pedaços do pai.
//  Montado pela API que as duas telas usam (import_video, add_null,
//  precompose, LayerSetParent, keyframes, LayerSplit) e capturado pelo Engine.
// =============================================================================
namespace splitnull {

using camreg::Rig;

f64 image_diff(const Image8& a, const Image8& b) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size() || a.rgba.empty()) return 1e9;
    f64 sum = 0;
    for (usize i = 0; i < a.rgba.size(); ++i) sum += std::abs(static_cast<int>(a.rgba[i]) - static_cast<int>(b.rgba[i]));
    return sum / static_cast<f64>(a.rgba.size());
}

void split(Rig& r, u64 layer, i64 at) {
    Command c;
    c.type = CommandType::LayerSplit;
    c.layer_ref.layer = LayerId::unpack(layer);
    c.layer_split.at = FrameIndex{at};
    AUREA_CHECK(r.e.apply_command(c).ok());
}

/// Nulo 4 → Nulo 5 → pré-composição (com vídeo), como no projeto do relato;
/// os dois nulos andam e giram. Devolve {nulo4, nulo5, precomp}.
std::array<u64, 3> build(Rig& r) {
    AUREA_CHECK(r.e.new_project(camreg::kW, camreg::kH, 30.0, nullptr).ok());
    const u64 video = camreg::add_video(r);
    const u64 ids[1]{video};
    const auto pre = r.e.precompose(ids, 1, "Pre-composicao");
    AUREA_CHECK(pre.ok());
    const auto n4 = r.e.add_null(false);
    const auto n5 = r.e.add_null(false);
    AUREA_CHECK(n4.ok() && n5.ok());
    if (!pre.ok() || !n4.ok() || !n5.ok()) return {0, 0, 0};
    camreg::set_parent(r, *n5, *n4);
    camreg::set_parent(r, *pre, *n5);
    camreg::key(r, *n4, TrackProperty::PositionX, 0, 100.0f);
    camreg::key(r, *n4, TrackProperty::PositionX, 200, 220.0f);
    camreg::key(r, *n5, TrackProperty::RotationZ, 0, 0.0f);
    camreg::key(r, *n5, TrackProperty::RotationZ, 200, 40.0f);
    camreg::key(r, *n5, TrackProperty::ScaleX, 0, 1.0f);
    camreg::key(r, *n5, TrackProperty::ScaleX, 200, 0.5f);
    return {*n4, *n5, *pre};
}

} // namespace splitnull

AUREA_TEST(SplitParentNull, SplittingTheParentNullsKeepsEveryFrameOfTheChildren) {
    using namespace splitnull;
    Rig r;
    const auto [n4, n5, pre] = build(r);
    if (!n4) return;
    const i64 frames[]{20, 99, 100, 101, 150, 199};
    std::vector<Image8> before;
    for (const i64 f : frames) before.push_back(r.capture(f));
    // Controle: os nulos andam de verdade (quadros diferentes entre si).
    const f64 motion = image_diff(before.front(), before.back());
    std::printf("    controle: quadro 20 x 199 diferem %.3f\n", motion);
    AUREA_CHECK(motion > 2.0);
    // O relato dividiu os dois nulos, um depois do outro, no mesmo cabeçote.
    split(r, n5, 100);
    split(r, n4, 100);
    const Layer* child = r.layer(pre);
    AUREA_CHECK(child && child->parent == LayerId::unpack(n5));
    f64 worst = 0;
    for (usize i = 0; i < std::size(frames); ++i) {
        const Image8 after = r.capture(frames[i]);
        const f64 d = image_diff(before[i], after);
        worst = std::max(worst, d);
        std::printf("    quadro %lld: diferenca media %.3f\n", static_cast<long long>(frames[i]), d);
    }
    AUREA_CHECK(worst < 0.5);
}
