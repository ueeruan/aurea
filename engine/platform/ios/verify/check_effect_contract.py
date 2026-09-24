"""Static ABI audit, not a substitute for compiling/running the Swift UI.

Protect the C++ ParamType -> Swift constants boundary and forbid the raw-number
dispatch that previously interpreted Bool as Color and Enum as Bool.
"""
import re
from pathlib import Path


def validate():
    root = Path(__file__).resolve().parents[4]
    native = (root / 'engine/include/aurea/effects/Parameter.hpp').read_text(encoding='utf-8')
    native = re.sub(r'//[^\n]*', '', native)
    body = re.search(r'enum class ParamType\s*:\s*\w+\s*\{(.*?)\};', native, re.S).group(1)
    values = {}
    next_value = 0
    for entry in body.split(','):
        entry = entry.strip()
        if not entry:
            continue
        name, *explicit = entry.split('=')
        if explicit:
            next_value = int(explicit[0].strip(), 0)
        values[name.strip()] = next_value
        next_value += 1
    app = root / 'engine/platform/ios/app'
    swift = (app / 'EffectsHuman.swift').read_text(encoding='utf-8')
    constants = dict(re.findall(r'let fxParam(\w+)\s*=\s*(\d+)', swift))
    for name in ('Float', 'Int', 'Bool', 'Color', 'Point2D', 'Point3D', 'Angle', 'Enum'):
        if int(constants.get(name, '-1')) != values[name]:
            raise ValueError(f'Swift fxParam{name} does not match C++ ParamType::{name}')
    view = (app / 'EffectsView.swift').read_text(encoding='utf-8')
    view = re.sub(r'//[^\n]*', '', view)
    if re.search(r'param\.type\s*==\s*\d|case\s+\d', view):
        raise ValueError('EffectsView must dispatch parameter types through the audited fxParam constants')
    print('8 Swift parameter types match C++; effect controls use named types')


if __name__ == '__main__':
    validate()
