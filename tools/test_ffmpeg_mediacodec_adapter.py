"""Exercise pinned upstream encoder functions with a fake MediaCodec boundary.

The extracted functions retain their logic (C void-pointer initializers receive
only an explicit cast for the C++ host compiler). Public
FFmpeg AVFrame, AVPacket and image-copy functions come from the host prefix;
only Android codec operations are controlled. This proves adapter behavior,
not compatibility with an unavailable manufacturer's encoder.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/validation/astra-export-20261013/ffmpeg-adapter"


def function(text: str, name: str) -> str:
    marker = text.index(name + "(")
    start = text.rfind("\nstatic ", 0, marker) + 1
    begin = text.index("{", marker)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


HEADER = r'''
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
using ssize_t = long long;
struct FFAMediaCodecBufferInfo { int32_t offset, size; int64_t presentationTimeUs; uint32_t flags; };
struct FFAMediaCodec { int queue_result = 0, releases = 0; size_t input_capacity = 384, queued_bytes = 0;
  int returned = 0; FFAMediaCodecBufferInfo output{0, 4, 0, 4}; uint8_t input[1024], encoded[16]{1,2,3,4}; };
struct MediaCodecEncContext { FFAMediaCodec* codec; void* window = nullptr; int width = 16, height = 16;
  int eof_sent = 0, eof_received = 0, extradata_size = 0, pts_as_dts = 1; uint8_t* extradata = nullptr; };
struct AVMediaCodecBuffer {};
static int mediacodec_get_input_index(AVCodecContext*, ssize_t* index) { *index = 0; return 0; }
static int mediacodec_get_output_index(AVCodecContext* c, ssize_t* index, FFAMediaCodecBufferInfo* info) {
  auto* s = (MediaCodecEncContext*) c->priv_data; *index = s->codec->returned++ ? -1 : 0; *info = s->codec->output; return 0; }
static int ff_AMediaCodec_infoTryAgainLater(FFAMediaCodec*, ssize_t index) { return index == -1; }
static int ff_AMediaCodec_infoOutputFormatChanged(FFAMediaCodec*, ssize_t) { return 0; }
static int ff_AMediaCodec_infoOutputBuffersChanged(FFAMediaCodec*, ssize_t) { return 0; }
static int ff_AMediaCodec_getBufferFlagEndOfStream(FFAMediaCodec*) { return 4; }
static int ff_AMediaCodec_getBufferFlagCodecConfig(FFAMediaCodec*) { return 2; }
static int ff_AMediaCodec_getBufferFlagKeyFrame(FFAMediaCodec*) { return 1; }
static void ff_AMediaCodec_cleanOutputBuffers(FFAMediaCodec*) {}
static int ff_AMediaCodec_releaseOutputBuffer(FFAMediaCodec* c, ssize_t, bool) { ++c->releases; return 0; }
static int ff_AMediaCodec_signalEndOfInputStream(FFAMediaCodec*) { return 0; }
static void av_mediacodec_release_buffer(void*, int) {}
static uint8_t* ff_AMediaCodec_getInputBuffer(FFAMediaCodec* c, ssize_t, size_t* size) { *size = c->input_capacity; return c->input; }
static int ff_AMediaCodec_queueInputBuffer(FFAMediaCodec* c, ssize_t, int, size_t size, int64_t, uint32_t) { c->queued_bytes = size; return c->queue_result; }
static uint8_t* ff_AMediaCodec_getOutputBuffer(FFAMediaCodec* c, ssize_t, size_t* size) { *size = 8; return c->encoded; }
static void mediacodec_output_format(AVCodecContext*) {}
static int ff_get_encode_buffer(AVCodecContext*, AVPacket* p, int size, int) { return av_new_packet(p, size); }
'''

MAIN = r'''
int main() {
  FFAMediaCodec codec; MediaCodecEncContext state{&codec};
  AVCodecContext* context = avcodec_alloc_context3(nullptr);
  context->priv_data = &state; context->width = 16; context->height = 16;
  context->pix_fmt = AV_PIX_FMT_NV12; context->time_base = AVRational{1, 1000000};
  context->color_range = AVCOL_RANGE_MPEG;
  AVFrame* frame = av_frame_alloc(); frame->width = 16; frame->height = 16; frame->format = AV_PIX_FMT_NV12;
  av_frame_get_buffer(frame, 32);
  for (int y=0; y<16; ++y) std::memset(frame->data[0] + y*frame->linesize[0], 99, 16);
  for (int y=0; y<8; ++y) std::memset(frame->data[1] + y*frame->linesize[1], 130, 16);
  std::memset(codec.input, 0xA5, sizeof(codec.input)); codec.input_capacity = 100;
  int result = mediacodec_send(context, frame);
  int guard_changes = 0; for (int i=100; i<384; ++i) guard_changes += codec.input[i] != 0xA5;
  bool bounds = result < 0 && guard_changes == 0 && codec.queued_bytes == 0;
  std::printf("short_input_buffer status=%d guardChanges=%d queueBytes=%zu %s\n", result, guard_changes, codec.queued_bytes, bounds ? "PASS" : "FAIL");
  codec.input_capacity = 384; codec.queue_result = -777; codec.queued_bytes = 0;
  result = mediacodec_send(context, frame);
  bool queue = result == -777;
  std::printf("queue_failure status=%d expected=-777 %s\n", result, queue ? "PASS" : "FAIL");
  AVPacket* packet = av_packet_alloc();
  result = mediacodec_receive(context, packet);
  bool eos = result == 0 && packet->size == 4 && codec.releases == 1;
  std::printf("final_data_with_eos status=%d packetBytes=%d released=%d %s\n", result, packet->size, codec.releases, eos ? "PASS" : "FAIL");
  av_packet_unref(packet); result = mediacodec_receive(context, packet);
  bool drained = result == AVERROR_EOF;
  std::printf("eos_after_final_data status=%d %s\n", result, drained ? "PASS" : "FAIL");
  codec.input_capacity = 1024; codec.queue_result = 0; codec.queued_bytes = 0;
  result = mediacodec_send(context, frame);
  bool exact = result == 0 && codec.queued_bytes == 384;
  std::printf("larger_capacity_exact_payload status=%d queueBytes=%zu %s\n", result, codec.queued_bytes, exact ? "PASS" : "FAIL");
  context->width = 14; context->height = 10; std::memset(codec.input, 0xA5, sizeof(codec.input));
  result = mediacodec_send(context, frame); bool padded = result == 0;
  for (int row=0; row<16; ++row) for (int col=0; col<16; ++col)
    padded &= codec.input[row*16+col] == (row<10 && col<14 ? 99 : 16);
  for (int row=0; row<8; ++row) for (int col=0; col<16; ++col)
    padded &= codec.input[256+row*16+col] == (row<5 && col<14 ? 130 : 128);
  std::printf("aligned_storage_padding %s\n", padded ? "PASS" : "FAIL");
  state.eof_received = 0; codec.returned = 0; codec.releases = 0; codec.output.size = 0;
  av_packet_unref(packet); result = mediacodec_receive(context, packet);
  bool empty = result == AVERROR_EOF && codec.releases == 1;
  std::printf("empty_eos status=%d released=%d %s\n", result, codec.releases, empty ? "PASS" : "FAIL");
  state.eof_received = 0; codec.returned = 0; codec.releases = 0; codec.output = {7, 4, 0, 1};
  result = mediacodec_receive(context, packet);
  bool out_bounds = result < 0 && codec.releases == 1;
  std::printf("output_range_outside_capacity status=%d released=%d %s\n", result, codec.releases, out_bounds ? "PASS" : "FAIL");
  state.eof_sent = 0; codec.queue_result = -888; result = mediacodec_send(context, nullptr);
  bool eos_queue = result == -888 && state.eof_sent == 0;
  std::printf("eos_queue_failure status=%d eofSent=%d %s\n", result, state.eof_sent, eos_queue ? "PASS" : "FAIL");
  av_packet_free(&packet); av_frame_free(&frame);
  context->priv_data = nullptr; avcodec_free_context(&context);
  return bounds && queue && eos && drained && exact && padded && empty && out_bounds && eos_queue ? 0 : 1;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT / "build/astra-ffmpeg/source/ffmpeg-8.0.3/libavcodec/mediacodecenc.c")
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/astra-ffmpeg/host-dev/ffmpeg-8.1.2-full_build-shared")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    text = args.source.read_text(encoding="utf-8")
    functions = "\n\n".join(function(text, name) for name in
        ("copy_frame_to_buffer", "mediacodec_send", "mediacodec_receive"))
    source = OUT / (args.label + ".cpp")
    compatible = functions.replace("MediaCodecEncContext *s = avctx->priv_data;",
                                   "MediaCodecEncContext *s = (MediaCodecEncContext *)avctx->priv_data;")
    source.write_text(HEADER + compatible + "\n" + MAIN, encoding="utf-8")
    spec = importlib.util.spec_from_file_location("host_tools", ROOT / "tools/build_aurea_pc.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    env = helper.development_environment()
    env["PATH"] = str(args.prefix / "bin") + os.pathsep + env["PATH"]
    exe = OUT / (args.label + ".exe")
    compiler = shutil.which("cl.exe", path=env["PATH"])
    if not compiler:
        raise RuntimeError("The Visual C++ environment has no compiler")
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W3", "/I" + str(args.prefix / "include"),
               str(source), "/Fe:" + str(exe), "/Fo:" + str(OUT / (args.label + ".obj")),
               "/link", "/LIBPATH:" + str(args.prefix / "lib"), "avcodec.lib", "avutil.lib"]
    build = subprocess.run(command, env=env, cwd=OUT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (args.label + "-build.log")).write_text(build.stdout, encoding="utf-8")
    if build.returncode:
        print(build.stdout)
        return build.returncode
    run = subprocess.run([str(exe)], env=env, cwd=OUT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (args.label + "-run.log")).write_text(run.stdout, encoding="utf-8")
    record = {"source": str(args.source), "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
              "function_sha256": hashlib.sha256(functions.encode()).hexdigest(),
              "public_ffmpeg_prefix": str(args.prefix), "boundary": "fake MediaCodec; real public FFmpeg AV/image helpers",
              "result": run.returncode, "log": run.stdout}
    (OUT / (args.label + ".json")).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(run.stdout, end="")
    return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
