"""Generate native preset data from the nine user-supplied FFX files audited by AE.
The original FFX files are not loaded or executed by the mobile application.
"""
import json, re
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
AUDIT=ROOT/'output/juan-ffx-audit'
NAMES=['juan Text Bounce 2','juan TEXT ANIMATION 01','Juan Text Animation 5','juan Text Animation2',
       'juan text animation fast 1','juan text animation jump bounce','juan text animation word jump','juan Text Animation']
def walk(p):
    yield p
    for c in p.get('children',[]): yield from walk(c)
def num(v): return f'{float(v):.10g}f' if '.' in f'{float(v):.10g}' or 'e' in f'{float(v):.10g}' else f'{int(v)}.0f'
def quote(v): return json.dumps(v,ensure_ascii=True)
code=['// Generated from actual AE property/keyframe audits. See tools/convert_juan_text_presets.py.',
      'static bool apply_juan_text(u32 id, TextData& t, TrackSet& tr, i64 start, f64 fps) {',
      '  auto set = [&](u32 ai, u32 param, f32 value) { auto& track=tr.get_or_create(TrackProperty::TextAnimParam,ai,param); (void)track.set(FrameIndex{start},value,Interpolation::Hold); };',
      '  auto expression = [&](u32 ai, const char* source) {',
      '    std::string program = "if (localTime < " + std::to_string(static_cast<double>(start)/fps) + ") { 0; } else { var presetTime = localTime - " + std::to_string(static_cast<double>(start)/fps) + "; " + source + " }";',
      '    auto& track = tr.get_or_create(TrackProperty::TextAnimParam,ai,kSelAmount); track.expression=expr::compile(program); track.expressionEnabled=true;',
      '  };', '  switch(id) {']
PROP={
 'ADBE Text Position 3D':('position','Vec3','kTextPropPosition',[10,11,12]),
 'ADBE Text Scale 3D':('scale','Vec2','kTextPropScale',[13,14]),
 'ADBE Text Rotation':('rotation.z',None,'kTextPropRotation',[17]),
 'ADBE Text Skew':('skew',None,'kTextPropSkew',[21]),
 'ADBE Text Opacity':('opacity',None,'kTextPropOpacity',[18]),
 'ADBE Text Tracking Amount':('tracking',None,'kTextPropTracking',[19]),
 'ADBE Text Blur':('blur',None,'kTextPropBlur',[20]),
}
def keys(p, ai, params, multiplier=1, address=None):
    ks=p.get('keys',[])
    for component,param in enumerate(params):
        target=address or f'TrackProperty::TextAnimParam,{ai},{param}'
        code.append(f'    {{ auto& track=tr.get_or_create({target});')
        for i,key in enumerate(ks):
            value=key['value'];value=value[component] if isinstance(value,list) else value
            typ='Interpolation::Hold' if key['outType']=='6614' else 'Interpolation::Linear'
            control=None
            if i+1<len(ks):
                nxt=ks[i+1];nv=nxt['value'];nv=nv[component] if isinstance(nv,list) else nv
                dv=nv-value;dt=nxt['time']-key['time']
                if dv and (key['outType']=='6613' or nxt['inType']=='6613'):
                    out=key['outEase'][min(component,len(key['outEase'])-1)]
                    inc=nxt['inEase'][min(component,len(nxt['inEase'])-1)]
                    x1=out['influence']/100 if key['outType']=='6613' else 1/3
                    x2=1-inc['influence']/100 if nxt['inType']=='6613' else 2/3
                    y1=out['speed']*dt*x1/dv if key['outType']=='6613' else 1/3
                    y2=1-inc['speed']*dt*(1-x2)/dv if nxt['inType']=='6613' else 2/3
                    typ='Interpolation::Bezier';control=(x1,y1,x2,y2)
            code.append(f'      {{ auto index=track.set(FrameIndex{{start+static_cast<i64>(std::llround({num(key["time"])}*fps))}},{num(value*multiplier)},{typ});')
            if control:
                for field,v in zip(['bx1','by1','bx2','by2'],control):code.append(f'        track.keys[index].{field}={num(v)};')
            code.append('      }')
        code.append('    }')
for preset,name in enumerate(NAMES,11):
    doc=json.loads((AUDIT/(name+'.ffx.json')).read_text(encoding='utf-8-sig'))
    allprops=[p for root in doc['properties'] for p in walk(root)]
    animators=[p for p in allprops if p['match']=='ADBE Text Animator']
    grouping=next(p['value'] for p in allprops if p['match']=='ADBE Text Anchor Point Option')-1
    code.append(f'  case {preset}: {{ // {name}')
    for ai,a in enumerate(animators):
        code.append(f'    {{ TextAnimator a; a.name={quote(name+" / "+str(ai+1))};')
        props=next(x['children'] for x in a['children'] if x['match']=='ADBE Text Animator Properties')
        for p in props:
            if 'expression' not in p or p['match'] not in PROP:continue
            field,vec,flag,params=PROP[p['match']];v=p.get('value',0)
            value=(vec+'{'+','.join(num(x) for x in v[:len(params)])+'}') if vec else num(v[0] if isinstance(v,list) else v)
            code.append(f'      a.{field}={value}; a.props |= {flag};')
        sels=next(x['children'] for x in a['children'] if x['match']=='ADBE Text Selectors')
        for sel in sels:
            sp={x['match']:x for x in walk(sel)}
            if sel['match']=='ADBE Text Selector':
                code.append('      a.selector.type=2; // AE range endpoints clamp, rather than wrap.')
                for ae,native in [('ADBE Text Percent Start','start'),('ADBE Text Percent End','end'),('ADBE Text Percent Offset','offset'),('ADBE Text Levels Max Ease','easeHigh'),('ADBE Text Levels Min Ease','easeLow')]:
                    code.append(f'      a.selector.{native}={num(sp[ae]["value"])};')
                code.append(f'      a.selector.shape={int(sp["ADBE Text Range Shape"]["value"])-1};')
        code.append('      t.animators.push_back(a); }')
        code.append(f'    set({ai},kAnchorGrouping,{num(grouping)}); set({ai},kTrackingEm,1);')
        for p in props:
            if p['match']=='ADBE Text Skew Axis' and 'expression' in p:code.append(f'    set({ai},kSkewAxis,{num(p["value"])});')
            if p['match'] in PROP and p.get('keys'):keys(p,ai,PROP[p['match']][3])
        for sel in sels:
            for p in walk(sel):
                if p['match']=='ADBE Text Percent Offset' and p.get('keys'):keys(p,ai,[2])
                if p.get('expression'):
                    source=p['expression'].replace('\r','\n')
                    if 'Slider Control' in source:
                        slider=next(x for x in allprops if x['match']=='ADBE Slider Control-0001')
                        keys(slider,ai,[3])
                        source='valueAtTime(time - 2*thisComp.frameDuration*(textIndex-1));'
                    else:
                        source=re.sub(r'\btime\b','presetTime',source)
                        source=re.sub(r'\binPoint\b','0',source)
                    code.append(f'    expression({ai},{quote(source)});')
    if preset==11:
        scale=next(p for p in allprops if p['match']=='ADBE Scale')
        keys(scale,0,[0],.01,'TrackProperty::ScaleX')
        # The source X and Y curves are identical; both keep the supplied easing.
        keys(scale,0,[0],.01,'TrackProperty::ScaleY')
    code.append('    return true; }')
code+=['  default: return false;','  }','}']
(ROOT/'engine/src/text/JuanTextPresets.inc').write_text('\n'.join(code)+'\n',encoding='utf-8')
print('Generated eight native text presets from AE audits')
