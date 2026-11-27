"""Evaluate 2x reconstruction using two past observations and fractional motion."""
import argparse
import json
from pathlib import Path
from statistics import median
from time import perf_counter

import cv2 as cv
import numpy as np
from skimage import data

from benchmark import AureaShader, input_at_half, luma


def translation(x, y, scale=1.0):
    return np.array(((scale, 0, x), (0, scale, y)), dtype=np.float32)


def estimate_shift(current, past, points):
    if points is None or len(points) < 12:
        return None
    options = dict(winSize=(15, 15), maxLevel=2,
                   criteria=(cv.TERM_CRITERIA_COUNT | cv.TERM_CRITERIA_EPS, 15, 0.02))
    moved, found, _ = cv.calcOpticalFlowPyrLK(current, past, points, None, **options)
    if moved is None:
        return None
    back, valid, _ = cv.calcOpticalFlowPyrLK(past, current, moved, None, **options)
    if back is None:
        return None
    ok = (found[:, 0] != 0) & (valid[:, 0] != 0) & (np.linalg.norm((back - points)[:, 0], axis=1) < 0.44)
    if np.count_nonzero(ok) < 12:
        return None
    shifts = (moved - points)[ok, 0]
    center = np.median(shifts, axis=0)
    agreeing = np.linalg.norm(shifts - center, axis=1) < 0.45
    if np.mean(agreeing) < 0.70:
        return None
    shift = np.median(shifts[agreeing], axis=0)
    if np.max(np.abs(shift)) > 5 or np.linalg.norm(shift - np.round(shift)) < 0.15:
        return None
    return shift


def accept_observations(current, past_frames):
    h, w = current.shape
    current_u8 = np.rint(current * 255).astype(np.uint8)
    points = cv.goodFeaturesToTrack(current_u8, 80, 0.015, 8)
    gradient = np.abs(cv.Laplacian(current, cv.CV_32F))
    tolerance = 0.022 + 0.12 * np.minimum(gradient, 0.22)
    observations = []
    for frame in past_frames[-2:]:
        past_u8 = np.rint(frame * 255).astype(np.uint8)
        shift = estimate_shift(current_u8, past_u8, points)
        if shift is None:
            continue
        dx, dy = shift
        aligned = cv.warpAffine(frame, translation(-dx, -dy), (w, h), borderMode=cv.BORDER_REFLECT101)
        error = np.abs(current - aligned)
        meaningful = error[5:-5, 5:-5]
        if np.median(meaningful) > 0.018 or np.mean(meaningful > 0.08) > 0.13:
            continue
        # The mask rejects local motion or occlusion. Smooth it before
        # changing grids so nearby pixels do not toggle on and off abruptly.
        confidence = np.clip(1.0 - error / tolerance, 0, 1)
        safe = max(4, int(np.ceil(max(abs(dx), abs(dy)))) + 2)
        confidence[:safe] = confidence[-safe:] = 0
        confidence[:, :safe] = confidence[:, -safe:] = 0
        mask = cv.resize(cv.GaussianBlur(confidence, (3, 3), 0.8), (w * 2, h * 2))
        observations.append((frame, dx, dy, mask))
    return observations


def back_project(base, low, observations):
    h, w = low.shape
    out = base.copy()
    size = (w * 2, h * 2)
    weight = 1.0 + sum(mask for _, _, _, mask in observations)
    for _ in range(2):
        estimated_low = cv.resize(out, (w, h), interpolation=cv.INTER_AREA)
        delta = cv.resize(low - estimated_low, size, interpolation=cv.INTER_LINEAR)
        for past, dx, dy, mask in observations:
            projected = cv.warpAffine(out, translation(2 * dx, 2 * dy), size, borderMode=cv.BORDER_REFLECT101)
            residual = past - cv.resize(projected, (w, h), interpolation=cv.INTER_AREA)
            # Project the residual from past sample centres onto the output
            # grid with its original subpixel phase intact.
            correction = cv.warpAffine(residual, translation(0.5 - 2 * dx, 0.5 - 2 * dy, 2), size,
                                        borderMode=cv.BORDER_REFLECT101)
            delta += mask * np.clip(correction, -0.075, 0.075)
        out += 0.88 * delta / weight
    return np.clip(out, 0, 1)


def compare(reference, low_rgb, history, shader):
    low = luma(low_rgb.astype(np.float32) / 255)
    base = luma(shader.apply(low_rgb).astype(np.float32) / 255)
    start = perf_counter()
    neighbors = accept_observations(low, history)
    one = back_project(base, low, [])
    multiple = back_project(base, low, neighbors) if neighbors else one
    took_ms = (perf_counter() - start) * 1000
    real = luma(reference.astype(np.float32) / 255)[12:-12, 12:-12]
    errors = np.array([np.mean((result[12:-12, 12:-12] - real) ** 2) for result in (base, one, multiple)])
    return errors, len(neighbors), took_ms


def synthetic(shader):
    reference = cv.resize(data.moon(), (512, 360), interpolation=cv.INTER_AREA)
    for i in range(0, 150, 7):
        cv.line(reference, (55 + i, 35), (90 + i, 138), 210, 1, cv.LINE_AA)
        cv.line(reference, (295, 150 + i // 2), (458, 161 + i // 2), 26, 1, cv.LINE_AA)
    reference = np.repeat(reference[..., None], 3, axis=2)
    view = input_at_half(reference)
    history = []
    for x, y in ((0.82, -0.65), (-0.52, 0.67)):
        shifted = cv.warpAffine(reference, translation(x, y), (reference.shape[1], reference.shape[0]),
                                borderMode=cv.BORDER_REFLECT101)
        history.append(luma(input_at_half(shifted).astype(np.float32) / 255))
    errors, accepted, took_ms = compare(reference, view, history, shader)
    assert accepted == 2, 'the two known fractional observations must be used'
    low = luma(view.astype(np.float32) / 255)
    assert not accept_observations(low, [1.0 - low, np.full_like(low, 0.04)]), 'hard cut must discard history'
    assert not accept_observations(low, [low, low]), 'identical sampling phases add no information'
    return {'psnr_y_db': (-10 * np.log10(errors)).round(3).tolist(),
            'accepted': accepted, 'elapsed_ms': round(took_ms, 2)}


def segment(path, shader):
    cap = cv.VideoCapture(str(path))
    if not cap.isOpened():
        raise ValueError(f'Could not open {path}')
    errors, durations = [], []
    accepted = 0
    for fraction in (0.23, 0.48, 0.67):
        cap.set(cv.CAP_PROP_POS_FRAMES, int(fraction * cap.get(cv.CAP_PROP_FRAME_COUNT)))
        history = []
        for _ in range(15):
            ok, frame = cap.read()
            if not ok:
                break
            h, w = frame.shape[:2]
            scale = min(1.0, 864 / max(h, w))
            rgb = cv.cvtColor(frame, cv.COLOR_BGR2RGB)
            ref = cv.resize(rgb, (int(w * scale) & ~1, int(h * scale) & ~1), interpolation=cv.INTER_AREA)
            obs = input_at_half(ref)
            if len(history) == 2:
                mse, n, took_ms = compare(ref, obs, history, shader)
                errors.append(mse)
                durations.append(took_ms)
                accepted += int(n > 0)
                history.pop(0)
            history.append(luma(obs.astype(np.float32) / 255))
    cap.release()
    return {'psnr_y_db': (-10 * np.log10(np.mean(errors, axis=0))).round(3).tolist(),
            'frames_using_history': accepted, 'total_frames': len(errors), 'median_probe_ms': round(median(durations), 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', action='append', required=True, metavar='NAME=VIDEO')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    cv.setNumThreads(4)
    shader = AureaShader()
    report = {'order': ['Aurea Detail', 'Back projection, current frame', 'Back projection, up to 2 past frames'],
              'synthetic_translation': synthetic(shader)}
    for pair in args.reference:
        name, path = pair.split('=', 1)
        report[name] = segment(path, shader)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
