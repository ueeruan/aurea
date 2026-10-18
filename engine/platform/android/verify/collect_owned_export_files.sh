#!/system/bin/sh
# Run as the isolated instrumentation app. Keep completed movies and diagnostic
# sidecars even when another test failed before creating its requested output.
set -eu
cd files
set --
for owned in text-export-*.mp4 text-export-*-progress.txt preview-latency-*.jsonl preview-latency-*.png; do
    if [ -f "$owned" ]; then
        set -- "$@" "$owned"
    fi
done
[ "$#" -gt 0 ]
tar -cf - "$@"
