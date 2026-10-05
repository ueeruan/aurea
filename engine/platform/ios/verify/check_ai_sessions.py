"""Run real Swift reward-flow races where swiftc exists; report static coverage otherwise."""
import pathlib
import shutil
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
APP = HERE.parent / "app"
state = (APP / "Ai/AureaAiState.swift").read_text(encoding="utf-8")
flow = (APP / "Ai/AiRewardFlow.swift").read_text(encoding="utf-8")
callback = state.split("onChange: { [weak self] s in", 1)[1].split("self.session = s", 1)[0]
assert callback.index("s.generationId == self.currentSessionId") < callback.index("GuardaDaSessao.shared.write(s)")
assert "defer { if generationRun == run { generationTask = nil; downloading = false } }" in state
assert "guard !Task.isCancelled, connectionRun == run else { return }" in state
assert "self.activeSessionId == id, self.activeRun == run" in flow
print("PASS: session persistence, connection cancellation and generation ownership source contracts")
compiler = shutil.which("swiftc")
if compiler:
    with tempfile.TemporaryDirectory(prefix="aurea-ai-check-") as directory:
        exe = str(pathlib.Path(directory) / "ai-session-checks")
        subprocess.run([compiler, "-parse-as-library", str(APP / "Ai/AiRewardFlow.swift"),
                        str(HERE / "check_ai_sessions.swift"), "-o", exe], check=True)
        subprocess.run([exe], check=True)
else:
    print("SKIP: swiftc unavailable; race fixture was NOT compiled/executed. Source checks are not native validation.")
