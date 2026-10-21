# 0.3.0: APK de sideload com ROTAÇÃO de chave (APK Signature Scheme v3).
# Até a 0.2.0 o APK saiu com a chave antiga (55bf3cc8…, D:\Users\Ruan\.android\debug.keystore);
# da 0.2.1 em diante com a nova (d7c008c5…, C:\Users\Ruan\.android\debug.keystore).
# A linhagem antiga → nova deixa os dois grupos atualizarem sem desinstalar:
#   - v1/v2 (Android 8) com a ANTIGA: quem tem a 0.2.0 atualiza;
#   - v3 com a NOVA + a linhagem a partir do Android 9: os dois grupos atualizam.
# As senhas saem do android/key.properties (o mesmo que o Gradle usa) para variáveis
# de ambiente; nada é impresso.
param(
    [Parameter(Mandatory = $true)][string]$Mode,          # "lineage" ou "sign"
    [string]$InApk = "",
    [string]$OutApk = ""
)
$ErrorActionPreference = "Stop"
$env:JAVA_HOME = "D:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot"
$env:PATH = "$env:JAVA_HOME\bin;$env:PATH"
$apksigner = "D:\Users\Ruan\AppData\Local\Android\Sdk\build-tools\36.0.0\apksigner.bat"
$oldKs = "D:\Users\Ruan\.android\debug.keystore"
$newKs = "C:\Users\Ruan\.android\debug.keystore"
$lineage = "D:\Users\Ruan\Documents\Aureabeta\build\release-signing\lineage-55bf-d7c0.bin"
$props = @{}
foreach ($line in Get-Content "D:\Users\Ruan\Documents\Aureabeta\android\key.properties") {
    if ($line -match '^\s*([A-Za-z]+)\s*=\s*(.*)$') { $props[$Matches[1]] = $Matches[2].Trim() }
}
$env:AUREA_SIDELOAD_KS_PASS = $props["storePassword"]
$env:AUREA_SIDELOAD_KEY_PASS = $props["keyPassword"]
$alias = $props["keyAlias"]
try {
    if ($Mode -eq "lineage") {
        New-Item -ItemType Directory -Force (Split-Path $lineage) | Out-Null
        & $apksigner rotate --out $lineage `
            --old-signer --ks $oldKs --ks-key-alias $alias --ks-pass env:AUREA_SIDELOAD_KS_PASS --key-pass env:AUREA_SIDELOAD_KEY_PASS `
            --new-signer --ks $newKs --ks-key-alias $alias --ks-pass env:AUREA_SIDELOAD_KS_PASS --key-pass env:AUREA_SIDELOAD_KEY_PASS
        if ($LASTEXITCODE -ne 0) { throw "apksigner rotate falhou ($LASTEXITCODE)" }
        & $apksigner lineage --in $lineage --print-certs
    } elseif ($Mode -eq "sign") {
        & $apksigner sign --min-sdk-version 26 --rotation-min-sdk-version 28 --lineage $lineage `
            --ks $oldKs --ks-key-alias $alias --ks-pass env:AUREA_SIDELOAD_KS_PASS --key-pass env:AUREA_SIDELOAD_KEY_PASS `
            --next-signer --ks $newKs --ks-key-alias $alias --ks-pass env:AUREA_SIDELOAD_KS_PASS --key-pass env:AUREA_SIDELOAD_KEY_PASS `
            --out $OutApk $InApk
        if ($LASTEXITCODE -ne 0) { throw "apksigner sign falhou ($LASTEXITCODE)" }
        & $apksigner verify --min-sdk-version 26 --print-certs -v $OutApk
    } else { throw "modo desconhecido: $Mode" }
} finally {
    Remove-Item Env:AUREA_SIDELOAD_KS_PASS -ErrorAction SilentlyContinue
    Remove-Item Env:AUREA_SIDELOAD_KEY_PASS -ErrorAction SilentlyContinue
}
