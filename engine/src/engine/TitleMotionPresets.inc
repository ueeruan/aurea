namespace {

constexpr u32 kTitleMotionBase = 1100;
constexpr std::array kTitleMotionNames{
    "Letter Cascade", "Soft Tracking", "Bar Reveal", "Return Sweep",
    "Bar Crossfade", "Diagonal Split", "Orbit Switch",
};

struct TitleMotion {
    i64 first;
    i64 span;

    i64 at(f64 point) const {
        return first + static_cast<i64>(std::lround(static_cast<f64>(span) * point));
    }
};

void title_track(Layer& layer, TrackProperty property, u32 index, u32 param, i64 time,
                 f32 value, Interpolation interpolation = Interpolation::EaseInOut) {
    (void)layer.tracks.get_or_create(property, index, param).set(FrameIndex{time}, value, interpolation);
}

void title_letters(Layer& layer, u32 preset, TitleMotion time) {
    auto animator = [&](const char* name, u32 props, u8 shape, u8 units) {
        TextAnimator a;
        a.name = name;
        a.props = props;
        a.selector.shape = shape;
        a.selector.basedOn = units;
        layer.text.animators.push_back(std::move(a));
        return static_cast<u32>(layer.text.animators.size() - 1);
    };
    auto key = [&](u32 a, u32 p, f64 t, f32 value) {
        title_track(layer, TrackProperty::TextAnimParam, a, p, time.at(t), value);
    };
    const bool cascade = preset == 0;
    const u32 in = animator(kTitleMotionNames[preset], kTextPropOpacity, cascade ? 0 : 2, cascade ? 3 : 0);
    layer.text.animators[in].opacity = 0;
    key(in, cascade ? text::kSelStart : text::kSelOffset, 0, cascade ? 0.0f : -100.0f);
    key(in, cascade ? text::kSelStart : text::kSelOffset, cascade ? .43 : .60, 100);

    const u32 tracking = animator("Tracking", kTextPropTracking, 0, 0);
    key(tracking, text::kTracking, 0, 0);
    key(tracking, text::kTracking, .94, layer.text.size * .12f);

    const u32 out = animator("Fade out", kTextPropOpacity, 1, 0);
    layer.text.animators[out].opacity = 0;
    key(out, text::kSelOffset, 0, -100);
    key(out, text::kSelOffset, .50, -100);
    key(out, text::kSelOffset, 1, 100);
}

void title_layer_motion(Layer& layer, const char* name, TitleMotion time, f32 x, f32 y,
                        f32 rotation, bool fade, bool exit, f64 start, f64 end) {
    LayerAnimator animator;
    animator.name = name;
    animator.ease = 0;
    animator.exit = exit;
    animator.fromPosX = x;
    animator.fromPosY = y;
    animator.fromRotation = rotation;
    animator.fromOpacity = fade ? 0.0f : 100.0f;
    const u32 index = static_cast<u32>(layer.layerAnimators.size());
    layer.layerAnimators.push_back(std::move(animator));
    title_track(layer, TrackProperty::LayerAnimParam, index, layeranim::kProgress, time.at(start), exit ? 100.0f : 0.0f);
    title_track(layer, TrackProperty::LayerAnimParam, index, layeranim::kProgress, time.at(end), exit ? 0.0f : 100.0f);
}

Mask& title_mask(Layer& layer, const char* name) {
    const auto operation = layer.masks.empty() ? MaskOperation::Add : MaskOperation::Intersect;
    Mask& mask = layer.masks.emplace_back();
    mask.id = layer.alloc_mask_id();
    mask.name = name;
    mask.operation = operation;
    return mask;
}

bool apply_title_motion(Composition& comp, LayerId id, u32 preset, Rect ink, i64 localSpan) {
    Layer* layer = comp.layer(id);
    const TitleMotion time{layer->local_time(layer->start).value, localSpan};
    const char* name = kTitleMotionNames[preset];
    if (preset < 2) {
        title_letters(*layer, preset, time);
        return true;
    }
    const FrameIndex start = layer->start;
    const LayerId extra = preset >= 5 ? comp.duplicate_layer(id, start) : comp.add_layer(LayerKind::Shape, name);
    Layer* other = comp.layer(extra);
    layer = comp.layer(id);
    if (!other || !layer) return false;
    const f32 em = layer->text.size;
    if (preset >= 5) {
        other->name = name;
        if (preset == 5) {
            const f32 x0 = ink.x - em, x1 = ink.x + ink.w + em;
            const f32 y0 = ink.y - em, y1 = ink.y + ink.h * .68f;
            const f32 slope = ink.h * .36f;
            const std::vector<MaskPoint> points{{{x0, y0}}, {{x1, y0}}, {{x1, y1 - slope}}, {{x0, y1}}};
            title_mask(*layer, "Diagonal · upper").points = points;
            auto& lower = title_mask(*other, "Diagonal · lower");
            lower.points = points;
            lower.inverted = true;
            title_layer_motion(*layer, name, time, -em * .58f, -em * .62f, -5, false, true, .23, .76);
            title_layer_motion(*other, name, time, em * .58f, em * .62f, -5, false, true, .23, .76);
        } else {
            title_layer_motion(*layer, name, time, 0, 0, 180, true, true, .34, .94);
            title_layer_motion(*other, name, time, 0, 0, -180, true, false, .34, .94);
        }
        return true;
    }

    const bool reverse = layer->text.rtl;
    const f32 left = ink.x - em * .18f, right = ink.x + ink.w + em * .18f;
    const f32 top = ink.y - em * .20f, bottom = ink.y + ink.h + em * .20f;
    const f32 from = reverse ? right : left, to = reverse ? left : right;
    Mask& mask = title_mask(*layer, name);
    auto cut = [&](f64 progress, f32 edge) {
        const f32 lo = reverse ? edge : left, hi = reverse ? right : edge;
        MaskPathKey key;
        key.frame = time.at(progress);
        key.interp = 2;
        key.points = {{{lo, top}}, {{hi, top}}, {{hi, bottom}}, {{lo, bottom}}};
        mask.pathKeys.push_back(std::move(key));
    };
    const bool returns = preset == 3;
    const f64 sweepEnd = returns ? .45 : .68;
    cut(0, from);
    cut(sweepEnd, to);
    if (returns) {
        cut(.59, to);
        cut(1, from);
    }
    mask.points = mask.pathKeys.front().points;

    other->parent = id;
    other->start = start;
    other->end = FrameIndex{std::min(layer->end.value, start.value + localSpan + 1)};
    other->shape.bounds = Rect{0, 0, std::max(2.0f, em * .22f), bottom - top};
    other->shape.fillColor = layer->text.color;
    other->shape.filled = true;
    other->transform.anchor = Vec3{other->shape.bounds.w * .5f, other->shape.bounds.h * .5f, 0};
    other->transform.position = Vec3{from, (top + bottom) * .5f, 0};
    auto bar_key = [&](TrackProperty prop, f64 t, f32 value) {
        const auto frame = FrameIndex{static_cast<i64>(std::lround(t * localSpan))};
        (void)other->tracks.get_or_create(prop).set(frame, value, Interpolation::EaseInOut);
    };
    bar_key(TrackProperty::PositionX, 0, from);
    bar_key(TrackProperty::PositionX, sweepEnd, to);
    if (returns) {
        bar_key(TrackProperty::PositionX, .59, to);
        bar_key(TrackProperty::PositionX, 1, from);
    } else {
        bar_key(TrackProperty::Opacity, sweepEnd, 1);
        bar_key(TrackProperty::Opacity, 1, 0);
    }
    if (preset == 4) title_layer_motion(*layer, name, time, 0, 0, 0, true, false, .20, .87);
    const i32 row = comp.order().index_of(id);
    if (row >= 0) comp.reorder_layer(extra, static_cast<u32>(row) + 1);
    return true;
}

} // namespace
