[CmdletBinding()]
param([switch]$PlanOnly)
$ErrorActionPreference = 'Stop'
$reportRoot = 'C:\Users\Ruan\Documents\Aureabeta\docs\maintenance\cleanup-20261013'
$primaryRoot = 'C:\Users\Ruan\Documents\Aureabeta'
$worktreeRoots = @(
    'C:\Users\Ruan\Documents\Aureabeta-015',
    'C:\Users\Ruan\Documents\Aureabeta-astra',
    'C:\Users\Ruan\Documents\Aureabeta-b2adj',
    'C:\Users\Ruan\Documents\Aureabeta-b2export',
    'C:\Users\Ruan\Documents\Aureabeta-b2fx',
    'C:\Users\Ruan\Documents\Aureabeta-b2surf',
    'C:\Users\Ruan\Documents\Aureabeta-base017',
    'C:\Users\Ruan\Documents\Aureabeta-cc',
    'C:\Users\Ruan\Documents\Aureabeta-rel',
    'C:\Users\Ruan\Documents\Aureabeta-voz',
    'C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta'
)
$targets = [System.Collections.Generic.List[object]]::new()
foreach ($relative in @(
    'engine\build', 'build\android', 'android\app\.cxx',
    '.claude\worktrees\agent-a29d0154df0988e64\engine\build',
    '.claude\worktrees\agent-a486e05843be197f3\engine\build',
    'android\.gradle', 'android\.kotlin', 'android\build',
    'build\astra-ffmpeg\objects',
    'build\prompt03\emulator-recovery',
    'build\reference\alight-motion-5.0.280\Aurea_AM_Ref.avd',
    'build\reference\alight-motion-5.0.280\avd-registry',
    'build\reference\keyflow-ui-trial-20261009\Keyflow_Play_API35.avd',
    'output\iphone17-motion-15s\movie.rgba',
    'output\hera-motion-aurea\render.rgba',
    'build\reference\alight-motion-5.0.280\aurea-validation\aurea-uitest-x86_64.apk',
    'build\reference\alight-motion-5.0.280\aurea-validation\aurea-instrumentation.apk',
    'build\reference\alight-motion-5.0.280\aurea-validation\aurea-instrumentation-final.apk'
)) { $targets.Add([pscustomobject]@{Root=$primaryRoot; Relative=$relative}) }
foreach ($root in $worktreeRoots) {
    foreach ($relative in @('build\android', 'android\app\.cxx', 'android\.gradle', 'android\.kotlin', 'android\build', 'engine\build')) {
        $targets.Add([pscustomobject]@{Root=$root; Relative=$relative})
    }
}
$allowedRoots = @($primaryRoot) + $worktreeRoots
if ($PlanOnly) {
    $plan = foreach ($target in $targets) {
        $root = [System.IO.Path]::GetFullPath($target.Root).TrimEnd('\')
        $path = [System.IO.Path]::GetFullPath((Join-Path $root $target.Relative))
        if (-not $path.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw "Plan target escaped root: $path" }
        if (Test-Path -LiteralPath $path) {
            $tracked = @(& git -C $root ls-files -- $target.Relative)
            if ($LASTEXITCODE -ne 0) { throw "Cannot verify Git ownership: $root" }
            if ($tracked.Count -gt 0) { throw "Plan includes tracked files: $path" }
            [pscustomobject]@{Root=$root; Relative=$target.Relative; Path=$path; TrackedFiles=0; Kind='Regenerable output/cache or stopped emulator/raw render'}
        }
    }
    $plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportRoot 'proposed-directories.json') -Encoding utf8
    Write-Output "Read-only plan: $(@($plan).Count) existing untracked targets. No deletion."
    return
}
# Execute only the frozen list explicitly approved by the user.
$approvedPlan = Get-Content -LiteralPath (Join-Path $reportRoot 'proposed-directories.json') -Raw | ConvertFrom-Json
$targets = @($approvedPlan | Select-Object Root, Relative)
$records = [System.Collections.Generic.List[object]]::new()
$freeBefore = [System.IO.DriveInfo]::new('C:\').AvailableFreeSpace
@{FreeBytes=$freeBefore; Timestamp=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot 'deletion-start.json') -Encoding utf8
foreach ($target in $targets) {
    $root = [System.IO.Path]::GetFullPath($target.Root).TrimEnd('\')
    $path = [System.IO.Path]::GetFullPath((Join-Path $root $target.Relative))
    if ($root -notin $allowedRoots -or -not $path.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw "Target escaped authorized root: $path" }
    if ($path -eq $root -or $target.Relative -match '(^|\\)(\.git|_identity|src|third_party|assets|golden)(\\|$)') { throw "Protected target: $path" }
    $record = [pscustomobject]@{Path=$path; Root=$root; Reason='Regenerable build/cache, stopped reference emulator, or raw render intermediate'; State='pending'; Error=$null}
    $records.Add($record)
    if (-not (Test-Path -LiteralPath $path)) { $record.State='already-absent'; continue }
    try {
        $cursor = $path
        while ($cursor.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            $check = Get-Item -LiteralPath $cursor -Force
            if ($check.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { throw "Reparse point in target ancestry: $cursor" }
            if ($cursor -eq $root) { break }
            $cursor = [System.IO.Directory]::GetParent($cursor).FullName
        }
        $tracked = @(& git -C $root ls-files -- $target.Relative)
        if ($LASTEXITCODE -ne 0) { throw "Cannot verify Git ownership: $root" }
        if ($tracked.Count -gt 0) { throw "Target contains tracked files: $path" }
        $resolved = (Resolve-Path -LiteralPath $path).ProviderPath
        if (-not $resolved.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw "Resolved target escaped root: $resolved" }
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop
        if (Test-Path -LiteralPath $resolved) { throw "Target remains after deletion: $resolved" }
        $record.State='deleted'
        Write-Output "Deleted $resolved"
    } catch {
        $record.State='skipped-or-partial'
        $record.Error=$_.Exception.Message
        Write-Output "Skipped/partial $path : $($record.Error)"
    }
    $records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportRoot 'deleted-paths.json') -Encoding utf8
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportRoot 'deleted-paths.json') -Encoding utf8
$freeAfter = [System.IO.DriveInfo]::new('C:\').AvailableFreeSpace
[pscustomobject]@{Deleted=($records | Where-Object State -eq 'deleted').Count; Skipped=($records | Where-Object State -eq 'skipped-or-partial').Count; FreeBefore=$freeBefore; FreeAfter=$freeAfter; RecoveredBytes=$freeAfter-$freeBefore} | ConvertTo-Json | Tee-Object -FilePath (Join-Path $reportRoot 'deletion-result.json')
