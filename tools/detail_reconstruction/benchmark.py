"""Compare experimental 2x reconstruction against the current Aurea shader.

The fitted filters and tiny network are learned from scikit-image samples only.
Reference videos and the optional real low-resolution image are never training inputs.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
from statistics import median
import struct
import subprocess
from time import perf_counter

import cv2 as cv
import moderngl
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from skimage import data
from skimage.metrics import structural_similarity
import torch
from torch import nn


LUMA = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)
METHODS = ('Bicúbica', 'Lanczos', 'Aurea Detail', 'LUT em C++', 'Rede pequena', 'LUT na GPU')
ROOT = Path(__file__).resolve().parents[2]


def luma(rgb):
    return np.ascontiguousarray(rgb @ LUMA)


def jpeg(rgb, quality):
    _, encoded = cv.imencode('.jpg', cv.cvtColor(rgb, cv.COLOR_RGB2BGR), [cv.IMWRITE_JPEG_QUALITY, quality])
    return cv.cvtColor(cv.imdecode(encoded, cv.IMREAD_COLOR), cv.COLOR_BGR2RGB)


def input_at_half(rgb, blur=0.0, quality=None):
    src = cv.GaussianBlur(rgb, (0, 0), blur) if blur else rgb
    low = cv.resize(src, (rgb.shape[1] // 2, rgb.shape[0] // 2), interpolation=cv.INTER_AREA)
    return low if quality is None else jpeg(low, quality)


def training_pairs():
    functions = (data.astronaut, data.coffee, data.chelsea, data.rocket, data.camera, data.brick, data.grass, data.gravel)
    for get_image in functions:
        rgb = get_image()
        if rgb.ndim == 2:
            rgb = np.repeat(rgb[..., None], 3, axis=2)
        rgb = rgb[:rgb.shape[0] & ~1, :rgb.shape[1] & ~1, :3]
        ref = luma(rgb.astype(np.float32) / 255)
        for blur, quality in ((0.0, None), (0.45, None), (0.0, 55), (0.55, 75)):
            low = luma(input_at_half(rgb, blur, quality).astype(np.float32) / 255)
            base = cv.resize(low, (ref.shape[1], ref.shape[0]), interpolation=cv.INTER_CUBIC)
            yield low, ref - base


def fit_filters(pairs):
    selectors = np.ones(25, bool)
    selectors[12] = False
    gram = np.zeros((12, 24, 24), np.float64)
    cross = np.zeros((12, 24, 4), np.float64)
    counts = np.zeros(12, np.int64)
    for img, target in pairs:
        h, w = img.shape
        windows = np.lib.stride_tricks.sliding_window_view(np.pad(img, 2, mode='edge'), (5, 5))
        gx = (windows[:, :, 1:4, 3] - windows[:, :, 1:4, 1]).mean(axis=2)
        gy = (windows[:, :, 3, 1:4] - windows[:, :, 1, 1:4]).mean(axis=2)
        ax, ay = np.abs(gx), np.abs(gy)
        direction = np.select((ay < 0.41421356 * ax, ax < 0.41421356 * ay, gx * gy >= 0), (0, 1, 2), default=3)
        strength = np.digitize(np.maximum(ax, ay), (6 / 255, 27 / 255))
        classes = direction * 3 + strength
        patch = windows.reshape(h * w, 25)[:, selectors] - img.reshape(-1, 1)
        residual = target.reshape(h, 2, w, 2).transpose(0, 2, 1, 3).reshape(-1, 4)
        for i in range(12):
            mask = classes.reshape(-1) == i
            x = patch[mask]
            gram[i] += x.T @ x
            cross[i] += x.T @ residual[mask]
            counts[i] += len(x)
    ridge = 0.00002 * counts[:, None, None] * np.eye(24)
    return np.ascontiguousarray(np.linalg.solve(gram + ridge, cross).transpose(0, 2, 1), dtype=np.float32)


class TinyDetail(nn.Module):
    def __init__(self):
        super().__init__()
        self.residual = nn.Sequential(
            nn.Conv2d(1, 8, 3, padding=1, padding_mode='replicate'), nn.LeakyReLU(0.1),
            nn.Conv2d(8, 8, 3, padding=1, padding_mode='replicate'), nn.LeakyReLU(0.1),
            nn.Conv2d(8, 4, 3, padding=1, padding_mode='replicate'), nn.PixelShuffle(2),
        )

    def forward(self, low):
        return self.residual(low - 0.5)


def train_model(pairs, steps):
    torch.manual_seed(11)
    rng = np.random.default_rng(29)
    model = TinyDetail()
    nn.init.zeros_(model.residual[-2].weight)
    nn.init.zeros_(model.residual[-2].bias)
    opt = torch.optim.AdamW(model.parameters(), lr=0.002)
    side = 32
    for i in range(steps):
        inputs, targets = [], []
        for _ in range(12):
            low, residual = pairs[rng.integers(len(pairs))]
            x = rng.integers(low.shape[1] - side + 1)
            y = rng.integers(low.shape[0] - side + 1)
            inputs.append(low[y:y + side, x:x + side])
            targets.append(residual[2*y:2*(y + side), 2*x:2*(x + side)])
        pred = model(torch.from_numpy(np.stack(inputs)).unsqueeze(1))
        diff = pred[:, 0, 6:-6, 6:-6] - torch.from_numpy(np.stack(targets))[:, 6:-6, 6:-6]
        loss = torch.mean(diff.square())
        opt.zero_grad(set_to_none=True)
        loss.backward()
        opt.step()
        if (i + 1) % 150 == 0:
            print(f'Treino {i+1}/{steps}: MSE {loss.item():.6f}', flush=True)
    return model.eval()


class Lut:
    def __init__(self, library, filters):
        self.library = C.CDLL(str(library.resolve()))
        self.kernel = self.library.aurea_detail_lut_2x
        array = np.ctypeslib.ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
        self.kernel.argtypes = [array, array, array, C.c_int, C.c_int, array]
        self.kernel.restype = None
        self.filters = filters

    def apply(self, low, base):
        h, w = low.shape
        result = np.empty_like(base)
        self.kernel(low, base, result, w, h, self.filters)
        return result


class AureaShader:
    def __init__(self):
        src = (ROOT / 'engine/shaders/effects/adaptive_detail.frag').read_text(encoding='utf-8-sig')
        # Same colour conversion, filter and parameters as the app. Only the
        # shader ABI is adapted for the desktop OpenGL measurement harness.
        src = src.replace('#include "../common/bindings.glsl"', '')
        src = src.replace('set = 0, binding = AUREA_PARAMS', 'binding = 0')
        src = src.replace('set = 0, binding = AUREA_TEX0', 'binding = 0')
        src = src.replace('layout(location = 0) in vec2 v_uv;', 'in vec2 v_uv;').replace('void main()', 'void process()')
        src += '''
            void main() {
                process();
                vec3 x = max(o_color.rgb, vec3(0.0));
                o_color.rgb = mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, 12.92 * x, lessThanEqual(x, vec3(0.0031308)));
            }
        '''
        self.ctx = moderngl.create_standalone_context(require=450)
        vertex = '''#version 450
            out vec2 v_uv;
            void main() {
                v_uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
            }
        '''
        self.vao = self.ctx.vertex_array(self.ctx.program(vertex_shader=vertex, fragment_shader=src), [])
        self.params = self.ctx.buffer(reserve=32)
        self.params.bind_to_uniform_block(0)
        self.size = None

    def apply(self, low, anime=False):
        h, w = low.shape[:2]
        if self.size != (w, h):
            if self.size:
                self.src.release()
                self.dst.release()
                self.fbo.release()
            self.src = self.ctx.texture((w, h), 4, internal_format=0x8C43)  # SRGB8_ALPHA8
            self.src.filter = moderngl.LINEAR, moderngl.LINEAR
            self.src.repeat_x = self.src.repeat_y = False
            self.dst = self.ctx.texture((w * 2, h * 2), 4)
            self.fbo = self.ctx.framebuffer([self.dst])
            self.size = (w, h)
        self.src.write(cv.cvtColor(low, cv.COLOR_RGB2RGBA).tobytes())
        self.src.use()
        self.fbo.use()
        self.params.write(struct.pack('8f', w, h, 1/w, 1/h, 0.7, float(anime), 1, 0))
        self.vao.render(vertices=3)
        result = self.fbo.read(components=3, alignment=1)
        return np.frombuffer(result, np.uint8).reshape(h * 2, w * 2, 3)


class LutGpu:
    def __init__(self, context, filters):
        self.ctx = context
        src = Path(__file__).with_name('spatial_lut.comp').read_text(encoding='utf-8')
        src = src.replace('#include "common/bindings.glsl"',
                          '#define AUREA_TEX0 0\n#define AUREA_IMG0 1\n#define AUREA_DATA 2').replace('set = 0, ', '')
        self.program = context.compute_shader(src)
        self.coefficients = context.buffer(np.ascontiguousarray(filters.transpose(0, 2, 1)).tobytes())
        self.coefficients.bind_to_storage_buffer(2)
        self.size = None

    def apply(self, low):
        h, w = low.shape[:2]
        if self.size != (w, h):
            if self.size:
                self.fbo.release()
                self.dst.release()
                self.src.release()
            self.src = self.ctx.texture((w, h), 4)
            self.dst = self.ctx.texture((w * 2, h * 2), 4)
            self.fbo = self.ctx.framebuffer([self.dst])
            self.size = (w, h)
        self.src.write(cv.cvtColor(low, cv.COLOR_RGB2RGBA).tobytes())
        self.src.use()
        self.dst.bind_to_image(1, read=False, write=True)
        self.program.run((w + 7) // 8, (h + 7) // 8, 1)
        self.ctx.memory_barrier(moderngl.SHADER_IMAGE_ACCESS_BARRIER_BIT | moderngl.FRAMEBUFFER_BARRIER_BIT)
        return np.frombuffer(self.fbo.read(components=3, alignment=1), np.uint8).reshape(h * 2, w * 2, 3)


def reference_frames(path, consecutive=False):
    cap = cv.VideoCapture(str(path))
    count = int(cap.get(cv.CAP_PROP_FRAME_COUNT))
    if count <= 0:
        raise ValueError(f'Could not open {path}')
    if consecutive:
        cap.set(cv.CAP_PROP_POS_FRAMES, int(0.48 * (count - 1)))
    for fraction in (0.19, 0.31, 0.48, 0.62, 0.74, 0.85):
        if not consecutive:
            cap.set(cv.CAP_PROP_POS_FRAMES, int(fraction * (count - 1)))
        ok, bgr = cap.read()
        if not ok:
            raise ValueError(f'Could not read frame {fraction} of {path}')
        h, w = bgr.shape[:2]
        ratio = min(1.0, 1280 / max(h, w))
        rgb = cv.cvtColor(bgr, cv.COLOR_BGR2RGB)
        yield cv.resize(rgb, (int(w * ratio) & ~1, int(h * ratio) & ~1), interpolation=cv.INTER_AREA)
    cap.release()


def encode_h264(inputs):
    h, w = inputs[0].shape[:2]
    ffmpeg = ['ffmpeg', '-v', 'error', '-threads', '2']
    stream = subprocess.run(ffmpeg + ['-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', f'{w}x{h}',
                            '-framerate', '30', '-i', 'pipe:0', '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                            '-preset', 'fast', '-crf', '26', '-f', 'matroska', 'pipe:1'],
                            input=b''.join(x.tobytes() for x in inputs), capture_output=True, check=True).stdout
    raw = subprocess.run(ffmpeg + ['-i', 'pipe:0', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                         input=stream, capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(len(inputs), h, w, 3)


def evaluate(low, lut, model, shader, lut_gpu, anime=False):
    def timed(fn):
        start = perf_counter()
        a = fn()
        return a, (perf_counter() - start) * 1000

    def bicubic():
        rgb = cv.resize(low.astype(np.float32) / 255, None, fx=2, fy=2, interpolation=cv.INTER_CUBIC)
        return np.ascontiguousarray(rgb)

    def color(rgb, residual):
        return np.rint(np.clip(rgb + residual[..., None], 0, 1) * 255).astype(np.uint8)

    def native_lut():
        rgb = bicubic()
        base = luma(rgb)
        signal = luma(low.astype(np.float32) / 255)
        return color(rgb, lut.apply(signal, base) - base)

    @torch.inference_mode()
    def neural():
        rgb = bicubic()
        signal = torch.from_numpy(luma(low.astype(np.float32) / 255))[None, None]
        correction = model(signal).clamp(-0.08, 0.08).numpy()[0, 0]
        return color(rgb, correction)

    paths = (lambda: np.rint(np.clip(bicubic(), 0, 1) * 255).astype(np.uint8),
             lambda: cv.resize(low, None, fx=2, fy=2, interpolation=cv.INTER_LANCZOS4),
             lambda: shader.apply(low, anime), native_lut, neural, lambda: lut_gpu.apply(low))
    results = [timed(path) for path in paths]
    return [a for a, _ in results], [ms for _, ms in results]


def panel(path, inputs, labels, heading):
    source = Image.fromarray(inputs[0])
    width, height = source.size
    block_w = 360
    block_h = round(height * block_w / width)
    out = Image.new('RGB', (20 * (len(inputs) + 1) + block_w * len(inputs), 70 + block_h + 235), '#101722')
    draw = ImageDraw.Draw(out)
    font_face = Path('C:/Windows/Fonts/segoeui.ttf')
    font_path = str(font_face) if font_face.exists() else 'DejaVuSans.ttf'
    font = ImageFont.truetype(font_path, 18)
    title = ImageFont.truetype(font_path, 25)
    draw.text((20, 12), heading, font=title, fill='#f2f6ff')
    for i, (img, name) in enumerate(zip(inputs, labels, strict=True)):
        x = 20 + i * (block_w + 20)
        pic = Image.fromarray(img)
        crop = pic.crop((width * 0.29, height * 0.26, width * 0.71, height * 0.71))
        draw.text((x, 43), name, font=font, fill='#c5dfff')
        out.paste(pic.resize((block_w, block_h), Image.Resampling.LANCZOS), (x, 70))
        crop.thumbnail((block_w, 205), Image.Resampling.LANCZOS)
        out.paste(crop, (x + (block_w - crop.width)//2, 88 + block_h))
    out.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--reference', action='append', required=True, metavar='NAME=VIDEO')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--steps', type=int, default=450)
    parser.add_argument('--h264', action='store_true', help='also measure six consecutive frames recompressed with FFmpeg')
    args = parser.parse_args()
    cv.setNumThreads(4)
    torch.set_num_threads(4)
    args.out.mkdir(parents=True, exist_ok=True)
    pairs = list(training_pairs())
    filters = fit_filters(pairs)
    model = train_model(pairs, args.steps)
    filters.tofile(args.out / 'spatial-lut-f32.bin')
    torch.save(model.state_dict(), args.out / 'residual-2x-weights.pt')
    lut = Lut(args.library, filters)
    shader = AureaShader()
    lut_gpu = LutGpu(shader.ctx, filters)
    solid = np.full((11, 17, 3), 127, np.uint8)
    assert np.all(lut_gpu.apply(solid) == 127) and np.all(evaluate(solid, lut, model, shader, lut_gpu)[0][3] == 127)
    # Warm up allocations/dispatch before collecting durations.
    sample = np.full((360, 640, 3), 127, np.uint8)
    evaluate(sample, lut, model, shader, lut_gpu)
    report = {'gpu': shader.ctx.info['GL_RENDERER'], 'lut_bytes': filters.nbytes,
              'model_weights': sum(p.numel() for p in model.parameters()), 'sequences': {}, 'timing_640x360_ms': {}}
    lut_agreement = []
    for pair in args.reference:
        name, path = pair.split('=', 1)
        frames = list(reference_frames(path))
        report['sequences'][name] = {}
        experiments = [('Area', frames, [input_at_half(ref) for ref in frames]),
                       ('Area + JPEG 55', frames, [input_at_half(ref, quality=55) for ref in frames])]
        if args.h264:
            sequence = list(reference_frames(path, consecutive=True))
            experiments.append(('Area + H.264 CRF 26', sequence, encode_h264([input_at_half(ref) for ref in sequence])))
        for label, references, lows in experiments:
            scores = []
            for k, (ref, low) in enumerate(zip(references, lows, strict=True)):
                outputs, _ = evaluate(low, lut, model, shader, lut_gpu)
                lut_agreement.append(np.mean(np.abs(outputs[3].astype(np.int16) - outputs[-1]) <= 1))
                ref_y = luma(ref[8:-8, 8:-8].astype(np.float32) / 255)
                row = []
                for out in outputs:
                    test_y = luma(out[8:-8, 8:-8].astype(np.float32) / 255)
                    mse = np.mean((ref_y - test_y) ** 2)
                    row.append((float(mse), structural_similarity(ref_y, test_y, data_range=1)))
                scores.append(row)
                if k == 2 and label == 'Area + JPEG 55':
                    chosen = (2, 5, 4)
                    panel(args.out / f'{name}-comparativo.png', [ref, *(outputs[i] for i in chosen)],
                          ['Referência', *(METHODS[i] for i in chosen)], f'{name} · 2× após redução e JPEG')
            summary = np.asarray(scores).mean(axis=0)
            report['sequences'][name][label] = {
                method: {'psnr_y_db': round(-10 * np.log10(mse), 3), 'ssim_y': round(float(ssim), 5)}
                for method, (mse, ssim) in zip(METHODS, summary, strict=True)
            }
        print(name, report['sequences'][name], flush=True)

    # Compare elapsed time at one fixed size and on the same content. This
    # includes transfer/readback for the shader and colour assembly for CPU.
    sample = cv.resize(input_at_half(frames[2]), (640, 360), interpolation=cv.INTER_AREA)
    evaluate(sample, lut, model, shader, lut_gpu)
    elapsed = [evaluate(sample, lut, model, shader, lut_gpu)[1] for _ in range(15)]
    report['timing_640x360_ms'] = {method: round(median(times), 3) for method, times in zip(METHODS, zip(*elapsed), strict=True)}
    report['lut_cpu_gpu_channels_within_one_code'] = float(np.mean(lut_agreement))
    assert report['lut_cpu_gpu_channels_within_one_code'] > 0.999
    if args.image:
        img = np.asarray(Image.open(args.image).convert('RGB'))
        outputs, _ = evaluate(img, lut, model, shader, lut_gpu, anime=True)
        chosen = (0, 2, 5, 4)
        panel(args.out / 'Imagem-enviada-2x.png', [outputs[i] for i in chosen], [METHODS[i] for i in chosen],
              'Imagem enviada · mesma saída 2× em todos os métodos')
    (args.out / 'resultados.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(report['timing_640x360_ms'], flush=True)


if __name__ == '__main__':
    main()
