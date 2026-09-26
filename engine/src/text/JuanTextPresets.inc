// Generated from actual AE property/keyframe audits. See tools/convert_juan_text_presets.py.
static bool apply_juan_text(u32 id, TextData& t, TrackSet& tr, i64 start, f64 fps) {
  auto set = [&](u32 ai, u32 param, f32 value) { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,ai,param); (void)track.set(FrameIndex{start},value,Interpolation::Hold); };
  auto expression = [&](u32 ai, const char* source) {
    std::string program = "if (localTime < " + std::to_string(static_cast<double>(start)/fps) + ") { 0; } else { var presetTime = localTime - " + std::to_string(static_cast<double>(start)/fps) + "; " + source + " }";
    auto& track = tr.get_or_create(TrackProperty::TextAnimParam,ai,kSelAmount); track.expression=expr::compile(program); track.expressionEnabled=true;
  };
  switch(id) {
  case 11: { // juan Text Bounce 2
    { TextAnimator a; a.name="juan Text Bounce 2 / 1";
      a.position=Vec3{0.0f,730.0f,0.0f}; a.props |= kTextPropPosition;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=0.0f;
      a.selector.easeHigh=0.0f;
      a.selector.easeLow=0.0f;
      a.selector.shape=0;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    { TextAnimator a; a.name="juan Text Bounce 2 / 2";
      a.position=Vec3{0.0f,-800.0f,0.0f}; a.props |= kTextPropPosition;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=-100.0f;
      a.selector.easeHigh=100.0f;
      a.selector.easeLow=0.0f;
      a.selector.shape=2;
      t.animators.push_back(a); }
    set(1,kAnchorGrouping,0.0f); set(1,kTrackingEm,1);
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,1,2);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.0f*fps))},-100.0f,Interpolation::Linear);
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.8008138021f*fps))},100.0f,Interpolation::Linear);
      }
    }
    { TextAnimator a; a.name="juan Text Bounce 2 / 3";
      a.position=Vec3{0.0f,140.0f,0.0f}; a.props |= kTextPropPosition;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=-100.0f;
      a.selector.easeHigh=50.0f;
      a.selector.easeLow=50.0f;
      a.selector.shape=2;
      t.animators.push_back(a); }
    set(2,kAnchorGrouping,0.0f); set(2,kTrackingEm,1);
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,2,2);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.1334635417f*fps))},-100.0f,Interpolation::Linear);
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.8008138021f*fps))},100.0f,Interpolation::Linear);
      }
    }
    { TextAnimator a; a.name="juan Text Bounce 2 / 4";
      a.position=Vec3{0.0f,-70.0f,0.0f}; a.props |= kTextPropPosition;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=-100.0f;
      a.selector.easeHigh=50.0f;
      a.selector.easeLow=50.0f;
      a.selector.shape=2;
      t.animators.push_back(a); }
    set(3,kAnchorGrouping,0.0f); set(3,kTrackingEm,1);
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,3,2);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.2669270833f*fps))},-100.0f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::ScaleX);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.2001953125f*fps))},1.006450223f,Interpolation::Bezier);
        track.keys[index].bx1=1.0f;
        track.keys[index].by1=-0.006120803052f;
        track.keys[index].bx2=0.6666666667f;
        track.keys[index].by2=0.6666666667f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.8675455729f*fps))},0.38f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::ScaleY);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.2001953125f*fps))},1.006450223f,Interpolation::Bezier);
        track.keys[index].bx1=1.0f;
        track.keys[index].by1=-0.006120803052f;
        track.keys[index].bx2=0.6666666667f;
        track.keys[index].by2=0.6666666667f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.8675455729f*fps))},0.38f,Interpolation::Linear);
      }
    }
    return true; }
  case 12: { // juan TEXT ANIMATION 01
    { TextAnimator a; a.name="juan TEXT ANIMATION 01 / 1";
      a.position=Vec3{123.0f,-156.0f,0.0f}; a.props |= kTextPropPosition;
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.rotation.z=53.0f; a.props |= kTextPropRotation;
      a.opacity=0.0f; a.props |= kTextPropOpacity;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=0.0f;
      a.selector.easeHigh=0.0f;
      a.selector.easeLow=0.0f;
      a.selector.shape=0;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    expression(0,"delay = .060 ;\nmyDelay = delay*textIndex;\nt = (presetTime - 0) - myDelay;\nif (t >= 0){\n  freq =2;\n  amplitude = 50;\n  decay = 8.0;\n  s = amplitude*Math.cos(freq*t*2*Math.PI)/Math.exp(decay*t);\n  [s,s]\n}else{\n  value\n}");
    return true; }
  case 13: { // Juan Text Animation 5
    { TextAnimator a; a.name="Juan Text Animation 5 / 1";
      a.position=Vec3{0.0f,1200.0f,0.0f}; a.props |= kTextPropPosition;
      a.rotation.z=0.0f; a.props |= kTextPropRotation;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,0,10);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.002018229f*fps))},0.0f,Interpolation::Linear);
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.035384115f*fps))},0.0f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,0,11);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.002018229f*fps))},1200.0f,Interpolation::Linear);
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.035384115f*fps))},-1200.0f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,0,12);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.002018229f*fps))},0.0f,Interpolation::Linear);
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.035384115f*fps))},0.0f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,0,17);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(1.368033854f*fps))},0.0f,Interpolation::Linear);
      }
    }
    { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,0,3);
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.0f*fps))},100.0f,Interpolation::Bezier);
        track.keys[index].bx1=0.3333333333f;
        track.keys[index].by1=0.3333333333f;
        track.keys[index].bx2=0.2731121651f;
        track.keys[index].by2=1.0f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.5004882812f*fps))},-5.0f,Interpolation::Bezier);
        track.keys[index].bx1=0.4061082662f;
        track.keys[index].by1=0.01461705674f;
        track.keys[index].bx2=0.5496742251f;
        track.keys[index].by2=0.9524780574f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(0.8341796875f*fps))},2.0f,Interpolation::Bezier);
        track.keys[index].bx1=0.416335365f;
        track.keys[index].by1=-0.1331700708f;
        track.keys[index].bx2=0.400594407f;
        track.keys[index].by2=1.0f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(1.234570313f*fps))},-8.585854898e-07f,Interpolation::Bezier);
        track.keys[index].bx1=0.3333333333f;
        track.keys[index].by1=0.3333333333f;
        track.keys[index].bx2=0.400594407f;
        track.keys[index].by2=1.0f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(2.802799479f*fps))},-5.028937906e-07f,Interpolation::Bezier);
        track.keys[index].bx1=0.599405593f;
        track.keys[index].by1=0.0f;
        track.keys[index].bx2=0.5855902769f;
        track.keys[index].by2=1.079794491f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(3.169824219f*fps))},5.598164926f,Interpolation::Bezier);
        track.keys[index].bx1=0.4107509665f;
        track.keys[index].by1=0.06004586136f;
        track.keys[index].bx2=0.4982100825f;
        track.keys[index].by2=1.008231216f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(3.570247396f*fps))},-4.999996332f,Interpolation::Bezier);
        track.keys[index].bx1=1.0f;
        track.keys[index].by1=0.003173178961f;
        track.keys[index].bx2=0.6666666667f;
        track.keys[index].by2=0.6666666667f;
      }
      { auto index=track.set(FrameIndex{start+static_cast<i64>(std::llround(4.33766276f*fps))},100.0f,Interpolation::Linear);
      }
    }
    expression(0,"valueAtTime(time - 2*thisComp.frameDuration*(textIndex-1));");
    return true; }
  case 14: { // juan Text Animation2
    { TextAnimator a; a.name="juan Text Animation2 / 1";
      a.position=Vec3{0.0f,70.0f,0.0f}; a.props |= kTextPropPosition;
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.skew=30.0f; a.props |= kTextPropSkew;
      a.rotation.z=65.0f; a.props |= kTextPropRotation;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    set(0,kSkewAxis,60.0f);
    expression(0,"freq = 1.5;\n\ndecay = 15;\n\nduration = 0.18;\n\nretard = textIndex*thisComp.frameDuration*1;\n\nt = presetTime - (0 + retard);\n\nstartVal = [100,100,100];\n\nendVal = [0,0,0];\n\nif (t < duration){\n\nlinear(t,0,duration,startVal,endVal);\n\n}else{\n\namp = (endVal - startVal)/duration;\n\nw = freq*Math.PI*2;\n\nendVal + amp*(Math.sin((t-duration)*w)/Math.exp(decay*(t-duration))/w);\n\n}");
    { TextAnimator a; a.name="juan Text Animation2 / 2";
      a.position=Vec3{-25.0f,-30.0f,0.0f}; a.props |= kTextPropPosition;
      a.skew=-30.0f; a.props |= kTextPropSkew;
      a.rotation.z=40.0f; a.props |= kTextPropRotation;
      a.tracking=-60.0f; a.props |= kTextPropTracking;
      a.blur=10.0f; a.props |= kTextPropBlur;
      t.animators.push_back(a); }
    set(1,kAnchorGrouping,0.0f); set(1,kTrackingEm,1);
    set(1,kSkewAxis,0.0f);
    expression(1,"freq = 1;\n\ndecay = 8;\n\nduration = 0.25;\n\nretard = textIndex*thisComp.frameDuration*1;\n\nt = presetTime - (0 + retard);\n\nstartVal = [100,100,100];\n\nendVal = [0,0,0];\n\nif (t < duration){\n\nlinear(t,0,duration,startVal,endVal);\n\n}else{\n\namp = (endVal - startVal)/duration;\n\nw = freq*Math.PI*2;\n\nendVal + amp*(Math.sin((t-duration)*w)/Math.exp(decay*(t-duration))/w);\n\n}");
    return true; }
  case 15: { // juan text animation fast 1
    { TextAnimator a; a.name="juan text animation fast 1 / 1";
      a.position=Vec3{-114.0f,-156.0f,0.0f}; a.props |= kTextPropPosition;
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.rotation.z=53.0f; a.props |= kTextPropRotation;
      a.opacity=0.0f; a.props |= kTextPropOpacity;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=0.0f;
      a.selector.easeHigh=0.0f;
      a.selector.easeLow=0.0f;
      a.selector.shape=0;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    expression(0,"delay = .030 ;\nmyDelay = delay*textIndex;\nt = (presetTime - 0) - myDelay;\nif (t >= 0){\n  freq =2;\n  amplitude = 50;\n  decay = 8.0;\n  s = amplitude*Math.cos(freq*t*2*Math.PI)/Math.exp(decay*t);\n  [s,s]\n}else{\n  value\n}");
    return true; }
  case 16: { // juan text animation jump bounce
    { TextAnimator a; a.name="juan text animation jump bounce / 1";
      a.position=Vec3{-28.0f,173.0f,0.0f}; a.props |= kTextPropPosition;
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.rotation.z=69.0f; a.props |= kTextPropRotation;
      a.opacity=0.0f; a.props |= kTextPropOpacity;
      a.selector.type=2; // AE range endpoints clamp, rather than wrap.
      a.selector.start=0.0f;
      a.selector.end=100.0f;
      a.selector.offset=0.0f;
      a.selector.easeHigh=0.0f;
      a.selector.easeLow=0.0f;
      a.selector.shape=0;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,0.0f); set(0,kTrackingEm,1);
    expression(0,"delay = .020 ;\n\nmyDelay = delay*textIndex;\n\nt = (presetTime - 0) - myDelay;\n\nif (t >= 0){\n\n  freq =3;\n\n  amplitude = 50;\n\n  decay = 7.0;\n\n  s = amplitude*Math.cos(freq*t*2*Math.PI)/Math.exp(decay*t);\n\n  [s,s]\n\n}else{\n\n  value\n\n}");
    return true; }
  case 17: { // juan text animation word jump
    { TextAnimator a; a.name="juan text animation word jump / 1";
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.rotation.z=0.0f; a.props |= kTextPropRotation;
      a.tracking=0.0f; a.props |= kTextPropTracking;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,1.0f); set(0,kTrackingEm,1);
    expression(0,"freq = 2;\n\ndecay = 9;\n\nduration = 0.10;\n\nretard = textIndex*thisComp.frameDuration*1;\n\nt = presetTime - (0 + retard);\n\nstartVal = [100,100,100];endVal = [0,0,0];\n\nif (t < duration){\n\nlinear(t,0,duration,startVal,endVal);\n\n}else{\n\namp = (endVal - startVal)/duration;\n\nw = freq*Math.PI*2;\n\nendVal + amp*(Math.sin((t-duration)*w)/Math.exp(decay*(t-duration))/w);}");
    return true; }
  case 18: { // juan Text Animation
    { TextAnimator a; a.name="juan Text Animation / 1";
      a.scale=Vec2{0.0f,0.0f}; a.props |= kTextPropScale;
      a.rotation.z=48.0f; a.props |= kTextPropRotation;
      a.tracking=-21.0f; a.props |= kTextPropTracking;
      t.animators.push_back(a); }
    set(0,kAnchorGrouping,1.0f); set(0,kTrackingEm,1);
    expression(0,"freq = 1;\n\ndecay = 6;\n\nduration = 0.25;\n\nretard = textIndex*thisComp.frameDuration*2;\n\nt = presetTime - (0 + retard);\n\nstartVal = [100,100,100];endVal = [0,0,0];\n\nif (t < duration){\n\nlinear(t,0,duration,startVal,endVal);\n\n}else{\n\namp = (endVal - startVal)/duration;\n\nw = freq*Math.PI*2;\n\nendVal + amp*(Math.sin((t-duration)*w)/Math.exp(decay*(t-duration))/w);}");
    return true; }
  default: return false;
  }
}
