// Native adaptations of the audited text-animation specs in build/packs/spec.
// Timings preserve source ratios and fit the selected layer. User text is retained.
static bool apply_pack_text(u32 id, TextData& t, TrackSet& tr, i64 start, i64 duration) {
    auto at = [&](double frame, double entry) { return start + static_cast<i64>(std::llround(frame / entry * duration)); };
    auto add = [&](TextAnimator a, const char* name) { a.name = name; t.animators.push_back(a); return static_cast<u32>(t.animators.size() - 1); };
    if (id == 19) {
        auto a = reveal(0, 1, kTextPropScale | kTextPropFill | kTextPropStrokeWidth | kTextPropStroke);
        a.scale = Vec2{0, 0}; a.fill = Vec4{0, 0, 0, 1}; a.stroke = Vec4{1, 1, 1, 1}; a.strokeWidth = 50;
        a.selector.easeLow = 50;
        auto i = add(a, "Onda de contorno");
        key(tr, i, kSelOffset, start, -100, Interpolation::Linear);
        key(tr, i, kSelOffset, at(74, 74), 100, Interpolation::Linear);
        key(tr, i, kSelOffset, at(133, 74), -100, Interpolation::Linear);
    } else if (id == 20 || id == 21) {
        auto a = reveal(id == 21 ? 1 : 0, 0, kTextPropOpacity); a.opacity = 0;
        auto i = add(a, id == 21 ? "Palavras em sequência" : "Digitação reversa");
        key(tr, i, kSelStart, start, 0, Interpolation::Linear);
        key(tr, i, kSelStart, start + duration, 100, Interpolation::Linear);
        if (id == 20) key(tr, i, kSelStart, at(133, 78), 0, Interpolation::Linear);
        else {
            auto out = reveal(0, 0, kTextPropOpacity); out.opacity = 0;
            auto j = add(out, "Saída suave");
            key(tr, j, kSelAmount, start, 0, Interpolation::Linear);
            key(tr, j, kSelAmount, at(109, 86), 0, Interpolation::Linear);
            key(tr, j, kSelAmount, at(133, 86), 100, Interpolation::Linear);
        }
    } else if (id == 22) {
        auto a = reveal(0, 0, kTextPropFill); a.fill = Vec4{0, 0, 0, 1};
        a.selector.type = 1; a.selector.wiggleRate = 20; a.selector.seed = 0;
        add(a, "Cintilação");
    } else if (id == 23 || id == 24) {
        const double entry = id == 23 ? 88 : 120.121094;
        const double leave = id == 23 ? 90 : 116;
        const double end = id == 23 ? 184 : 236.121094;
        auto a = reveal(0, 1, kTextPropOpacity); a.opacity = 0; a.selector.type = 2; a.selector.easeHigh = 100;
        auto i = add(a, "Entrada suave");
        key(tr, i, kSelOffset, start, -100, Interpolation::Linear);
        key(tr, i, kSelOffset, start + duration, 100, Interpolation::Linear);
        a.selector.shape = 2;
        auto j = add(a, "Saída suave");
        key(tr, j, kSelOffset, start, -100, Interpolation::Linear);
        key(tr, j, kSelOffset, at(leave, entry), -100, Interpolation::Linear);
        key(tr, j, kSelOffset, at(end, entry), 100, Interpolation::Linear);
        if (id == 23) {
            auto tracking = reveal(0, 0, kTextPropTracking);
            auto k = add(tracking, "Espaçamento suave");
            key(tr, k, kTracking, start, 25); key(tr, k, kTracking, at(end, entry), 0);
        } else {
            auto light = reveal(0, 3, kTextPropFill); light.fill = Vec4{1, 1, 1, 1}; light.selector.end = 30;
            auto k = add(light, "Varredura de luz");
            key(tr, k, kSelOffset, start, -30, Interpolation::Linear);
            key(tr, k, kSelOffset, at(end, entry), 100, Interpolation::Linear);
        }
    } else return false;
    return true;
}
