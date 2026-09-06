param(
  [ValidateSet('Info','Runs','Jobs','Artifact')][string]$Operation = 'Info',
  [long]$RunId = 0,
  [string]$OutputDirectory = 'build/releases/ipa-35'
)
$ErrorActionPreference = 'Stop'
$repo = 'ruanpablo9928-sys/aurea'
# Read credentials into memory only. Never print or persist them.
$credentialLines = @('protocol=https', 'host=github.com', '') | git credential fill
if ($LASTEXITCODE -ne 0) { throw 'GitHub authentication unavailable.' }
$token = $null
foreach ($line in $credentialLines) {
  if ($line.StartsWith('password=')) { $token = $line.Substring(9) }
}
if (-not $token) { throw 'GitHub credential unavailable.' }
$headers = @{Authorization="Bearer $token"; Accept='application/vnd.github+json'; 'X-GitHub-Api-Version'='2022-11-28'; 'User-Agent'='Aurea-IPA-Verification'}
$api = "https://api.github.com/repos/$repo"
switch ($Operation) {
  'Info' {
    $data = Invoke-RestMethod "$api" -Headers $headers
    [pscustomobject]@{repository=$data.full_name; private=$data.private; branch=$data.default_branch; push=$data.permissions.push} | ConvertTo-Json
  }
  'Runs' {
    $data = Invoke-RestMethod "$api/actions/workflows/build-ipa.yml/runs?per_page=5" -Headers $headers
    $data.workflow_runs | ForEach-Object {
      [pscustomobject]@{id=$_.id; status=$_.status; conclusion=$_.conclusion; sha=$_.head_sha; branch=$_.head_branch; url=$_.html_url; created=$_.created_at}
    } | ConvertTo-Json
  }
  'Artifact' {
    if ($RunId -le 0) { throw 'Specify RunId.' }
    $list = Invoke-RestMethod "$api/actions/runs/$RunId/artifacts" -Headers $headers
    $artifact = @($list.artifacts | Where-Object { $_.name -eq 'aurea-ipa' -and -not $_.expired })
    if ($artifact.Count -ne 1) { throw 'Expected exactly one aurea-ipa artifact.' }
    $directory = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
    $allowed = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path 'build/releases')) + [IO.Path]::DirectorySeparatorChar
    if (-not $directory.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must stay inside build/releases.' }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $client = [Net.Http.HttpClient]::new([Net.Http.HttpClientHandler]@{AllowAutoRedirect=$false})
    try {
      $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Get,$artifact[0].archive_download_url)
      foreach($key in $headers.Keys) { $request.Headers.TryAddWithoutValidation($key,$headers[$key]) | Out-Null }
      $response = $client.SendAsync($request).GetAwaiter().GetResult()
      if ([int]$response.StatusCode -ne 302) { throw "Unexpected artifact response: $($response.StatusCode)" }
      $download = $response.Headers.Location.AbsoluteUri
      # Signed artifact URL receives no GitHub Authorization header.
      $archive = Join-Path $directory 'artifact.zip'
      Invoke-WebRequest $download -OutFile $archive
      Expand-Archive -LiteralPath $archive -DestinationPath $directory -Force
      Get-ChildItem -LiteralPath $directory -Filter '*.ipa' | ForEach-Object {
        [pscustomobject]@{path=$_.FullName; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
      } | ConvertTo-Json
    } finally { $client.Dispose() }
  }
  'Jobs' {
    if ($RunId -le 0) { throw 'Specify RunId.' }
    $data = Invoke-RestMethod "$api/actions/runs/$RunId/jobs" -Headers $headers
    $data.jobs | ForEach-Object {
      [pscustomobject]@{name=$_.name;status=$_.status;conclusion=$_.conclusion;steps=@($_.steps | ForEach-Object {
        [pscustomobject]@{name=$_.name;status=$_.status;conclusion=$_.conclusion}
      })}
    } | ConvertTo-Json -Depth 5
  }
}
