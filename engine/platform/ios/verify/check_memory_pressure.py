"""Source audit of native iOS memory contracts; does not substitute Apple runtime tests."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ios = Path(__file__).resolve().parents[1]
model = (ios / "app/AureaModel.swift").read_text(encoding="utf-8")
decoder = (ios / "bridge/IOSVideoDecoder.mm").read_text(encoding="utf-8")
cmake = (ios / "CMakeLists.txt").read_text(encoding="utf-8")
xcode = (ios / "Aurea.xcodeproj/project.pbxproj").read_text(encoding="utf-8")
effects = (ios / "app/EffectPreviewStore.swift").read_text(encoding="utf-8")
cards = (ios / "app/ProjectCards.swift").read_text(encoding="utf-8")
timeline = (ios / "app/TimelineView.swift").read_text(encoding="utf-8")
strip = (ios / "app/TimelineModel.swift").read_text(encoding="utf-8")
image = decoder[decoder.index("bool ios_load_image("):decoder.index("const char* ios_default_font_path()")]
assert "CGImageSourceCreateImageAtIndex" not in image, "full image decoding precedes the memory bound"
assert image.index("pixelBudget") < image.index("CGImageSourceCreateThumbnailAtIndex")
assert "width > 4096" in image and "height > 4096" in image
sampling_loop = image.split("while (std::ceil(sourceW / sample)", 1)[1].split("sample *= 2;", 1)[0]
assert "pixelBudget" not in sampling_loop, "memory pressure changed canonical image geometry"
assert image.index("sample *= 2;") < image.index("* std::ceil(sourceH / sample) > pixelBudget")
assert "out.rgba.resize" not in image and "out.rgba = std::move(premul)" in image
assert "times.size() >= capacityLimit" in decoder and "times.reserve(next)" in decoder
index = decoder[decoder.index("bool AVFoundationVideoDecoder::build_timing_index()"):decoder.index("bool AVFoundationVideoDecoder::start_reader(")]
assert "catch (const std::bad_alloc&)" in index and "catch (const std::bad_alloc&)" in image
allocation_failure = index.split("catch (const std::bad_alloc&)", 1)[1].split("return false;", 1)[0]
assert "CFRelease(sample)" in allocation_failure and "[indexReader cancelReading]" in allocation_failure
assert "timingIndexLimited_ = true" in allocation_failure
assert "CGImageRelease(image)" in image.split("catch (const std::bad_alloc&)", 1)[1].split("return false;", 1)[0]
assert 'bridge/IOSVideoDecoder.mm PROPERTIES COMPILE_OPTIONS "-fexceptions"' in cmake
assert 'IOSVideoDecoder.mm in Sources */ = ' in xcode
assert 'COMPILER_FLAGS = "-fexceptions"' in xcode.split('IOSVideoDecoder.mm in Sources */ = ', 1)[1].splitlines()[0]
assert "if (available <= reserve)" in image and "available ?" not in index
headroom = decoder[decoder.index("u64 process_memory_headroom()"):decoder.index("namespace {")]
assert "#if TARGET_OS_SIMULATOR" in headroom and "task_info(" in headroom
assert "total / 8" in headroom and "usage.resident_size" in headroom
assert headroom.index("#endif") < headroom.index("return available;")
assert "process_memory_headroom()" in image and "process_memory_headroom()" in index
assert "std::vector<PresentationTime>().swap(presentationTimes_)" in decoder
shape = model[model.index("func setShapePartImage("):]
assert "Data(contentsOf: url)" not in shape and "decodeHomeThumbnail" in shape
assert "beginProjectOperation()" in shape and "endProjectOperation(operation)" in shape
assert "native.trimMemory(level)" in model and "lifecycleQueue.async" in model
assert "now - memoryCheckAt >= 1" in model and "now - memoryTrimAt >= 5" in model
assert "level > memoryTrimLevel || now - memoryTrimAt >= 5" in model
assert "memoryTrimRequestedLevel = max(memoryTrimRequestedLevel, level)" in model
assert "level > max(memoryTrimLevel, memoryTrimRequestedLevel)" in model
assert "128 * 1024 * 1024" in model and "64 * 1024 * 1024" in model
# Classe de memória LOW (até ~4 GB): reserva maior, prévias de efeito pela metade.
assert "enum DeviceMemoryClass" in model and "physicalMemory < lowTotalBytes" in model
assert "DeviceMemoryClass.low ? 192 * 1024 * 1024 : 128 * 1024 * 1024" in model
assert "DeviceMemoryClass.low ? 96 * 1024 * 1024 : 64 * 1024 * 1024" in model
assert "available >= reserve + 64 * 1024 * 1024" in model
assert "DeviceMemoryClass.low ? 12 * 1024 * 1024 : 24 * 1024 * 1024" in effects
assert "UIApplication.didReceiveMemoryWarningNotification" in model and "trimForMemoryPressure(level: 15)" in model
assert "guard available > 0" not in model
assert "self.requestEpoch() == epoch" in effects and "pressurePaused = true" in effects
assert "generation == epoch" in cards and "generation == HomeThumbCache.shared.generation" in cards
assert "model.memoryCacheEpoch" in timeline and "thumbCache = TimelineThumbStrip()" in timeline
assert "guard model.thumbnailWorkAllowed" in strip
print("PASS: bounded predecode, single RGBA buffer, async headroom trim and cache invalidation source contracts")
print("NOT EXECUTED: ImageIO/CoreVideo/Metal allocations, jetsam and iPhone memory pressure")


def method(signature):
    start = model.index(signature)
    cursor = model.index("{", start) + 1
    depth = 1
    while depth:
        depth += (model[cursor] == "{") - (model[cursor] == "}")
        cursor += 1
    return model[start:cursor]


compiler = shutil.which("swiftc")
if not compiler:
    print("SKIP: swiftc unavailable; actual trim escalation/queue regression NOT compiled/executed")
else:
    # These are the actual model methods; only clock/dispatch/native dependencies
    # are deterministic stand-ins. No second implementation of the trim policy.
    harness = '''import Foundation
final class TestQueue {
    var work: [() -> Void] = []
    func async(_ action: @escaping () -> Void) { work.append(action) }
    func runOne() { precondition(!work.isEmpty); work.removeFirst()() }
}
enum DispatchQueue { static let main = TestQueue() }
final class Clock { var systemUptime: TimeInterval = 100 }
enum ProcessInfo { static let processInfo = Clock() }
final class Native {
    var available: UInt64 = 100 * 1024 * 1024
    var trims: [Int32] = []
    func availableMemoryBytes() -> UInt64 { available }
    func trimMemory(_ level: Int32) -> Int64 { trims.append(level); return 0 }
}
final class Previews { func resumeMemoryWork() {} }
enum DeviceMemoryClass { static let low = false }
final class Subject {
    var memoryPressureLimited = false
    var memoryCheckAt: TimeInterval = 0
    var memoryTrimAt: TimeInterval = -.infinity
    var memoryTrimPending = false
    var memoryTrimLevel: Int32 = 0
    var memoryTrimRequestedLevel: Int32 = 0
    var releases = 0
    let lifecycleQueue = TestQueue()
    let engine = Native()
    let effectPreviews = Previews()
    func releaseInterfaceCaches() { releases += 1 }
    func poll() { checkMemoryPressure(force: true) }
    func warning() { trimForMemoryPressure(level: 15) }
    TRIM
    SCHEDULE
    CHECK
}
let subject = Subject()
subject.poll()
precondition(subject.lifecycleQueue.work.count == 1 && subject.memoryTrimPending)
ProcessInfo.processInfo.systemUptime = 101
subject.engine.available = 32 * 1024 * 1024
subject.poll(); subject.warning(); subject.warning()
precondition(subject.lifecycleQueue.work.count == 1 && subject.memoryTrimRequestedLevel == 15,
             "critical pressure started duplicate GPU trim or was discarded")
subject.lifecycleQueue.runOne()
precondition(subject.engine.trims == [10])
DispatchQueue.main.runOne()
precondition(subject.lifecycleQueue.work.count == 1 && subject.memoryTrimPending,
             "critical request must run as soon as moderate trim returns, within cooldown")
subject.lifecycleQueue.runOne(); DispatchQueue.main.runOne()
precondition(subject.engine.trims == [10, 15] && subject.lifecycleQueue.work.isEmpty)
ProcessInfo.processInfo.systemUptime = 102
subject.warning()
precondition(subject.lifecycleQueue.work.isEmpty, "same-level pressure bypassed cooldown")
ProcessInfo.processInfo.systemUptime = 106.1
subject.poll()
precondition(subject.lifecycleQueue.work.count == 1)
subject.lifecycleQueue.runOne(); DispatchQueue.main.runOne()
precondition(subject.engine.trims == [10, 15, 15])
let completedModerate = Subject()
completedModerate.poll(); completedModerate.lifecycleQueue.runOne(); DispatchQueue.main.runOne()
ProcessInfo.processInfo.systemUptime = 107
completedModerate.engine.available = 32 * 1024 * 1024
completedModerate.poll()
precondition(completedModerate.lifecycleQueue.work.count == 1,
             "poll suppressed escalation after a completed moderate trim")
completedModerate.lifecycleQueue.runOne(); DispatchQueue.main.runOne()
precondition(completedModerate.engine.trims == [10, 15])
let exhausted = Subject()
exhausted.engine.available = 0
exhausted.poll()
precondition(exhausted.lifecycleQueue.work.count == 1 && exhausted.memoryPressureLimited,
             "zero app headroom must trigger critical pressure, not be ignored")
exhausted.lifecycleQueue.runOne(); DispatchQueue.main.runOne()
precondition(exhausted.engine.trims == [15])
print("PASS: actual Swift trim methods escalate immediately, coalesce pending critical warnings and serialize GPU work")
'''
    harness = harness.replace("TRIM", method("private func trimForMemoryPressure("))
    harness = harness.replace("SCHEDULE", method("private func scheduleMemoryTrimIfNeeded()"))
    harness = harness.replace("CHECK", method("private func checkMemoryPressure("))
    with tempfile.TemporaryDirectory(prefix="aurea-memory-pressure-") as directory:
        main = Path(directory) / "main.swift"
        main.write_text(harness, encoding="utf-8")
        executable = str(Path(directory) / "memory-pressure")
        subprocess.run([compiler, str(main), "-o", executable], check=True)
        subprocess.run([executable], check=True)
