<#
  Limpeza do projeto antigo do Aurea (Flutter) - para o DONO executar.

  O QUE FAZ
    Apaga, de forma PERMANENTE (sem Lixeira), a pasta
        C:\Users\SnyX\Documents\Projetos - Claude\Aurea
    e nada mais. Nenhum outro caminho e tocado: nem Documents, nem SDK/NDK,
    nem caches globais do Gradle/Flutter, nem ~/.android (a keystore fica).

  COMO USAR (PowerShell)
    1) Ensaio (padrao, nao apaga nada - so confere e mede):
         powershell -ExecutionPolicy Bypass -File .\docs\migration\limpar_aurea_antigo.ps1
    2) Se quiser guardar a pasta output/ do projeto antigo (renders, demos,
       narracao do tutorial), passe um destino FORA da pasta antiga. Ela e
       MOVIDA (nao copiada, para nao duplicar 1,2 GB):
         ... -ManterOutput "D:\Aurea-output-antigo"
    3) Apagar de verdade:
         ... -Executar
       (pode combinar com -ManterOutput)

  TRAVAS (qualquer uma falhando aborta sem apagar nada)
    - o alvo e exatamente o caminho acima, existe, NAO e link/junction;
    - o alvo tem a cara do projeto antigo (pubspec.yaml + lib\main.dart) e
      NAO tem a do novo (engine\CMakeLists.txt);
    - o repositorio novo existe e o bundle dos refs so-locais esta nele com o
      SHA-256 registrado no OLD_AUREA_CLEANUP.md;
    - a identidade preservada esta no repo novo (_identity, prints aprovados);
    - nenhum ref do repo antigo ganhou commit so-local fora do bundle desde a
      auditoria (se ganhou, o script para e diz qual).
#>
[CmdletBinding()]
param(
    [switch]$Executar,
    [string]$ManterOutput
)

$ErrorActionPreference = 'Stop'

$Alvo      = 'C:\Users\SnyX\Documents\Projetos - Claude\Aurea'
$RepoNovo  = 'C:\Users\SnyX\Documents\Projetos - Claude\Aureabeta'
$Bundle    = Join-Path $RepoNovo 'docs\migration\git\aurea-antigo-so-local.bundle'
$BundleSha = 'd2a210c267fdbe79dfc2f11da07c6dfc3879aa0558cc38aca5af797f84567155'
# Refs so-locais que o bundle cobre (ref -> commit na hora da auditoria).
$RefsNoBundle = @{
    'refs/heads/ui-nova'          = '959d7251d53cf7e2bf392cf6a75a7ca2e0798c89'
    'refs/heads/3d-diligent'      = 'a85588e014f5e75cf559b68d126b7ec9edbf2385'
    'refs/tags/ui-nova-aprovada'  = 'e09998929e70afe2e7f9ff78db72333a7527b6d6'
    'refs/stash'                  = 'f7a82e2fb2991b253bc186ee6581f6e77a3a495a'
}

function Falha([string]$msg) { Write-Host "ABORTADO: $msg" -ForegroundColor Red; exit 1 }
function Ok([string]$msg)    { Write-Host "  ok  $msg" -ForegroundColor Green }

function Tamanho-GB([string]$caminho) {
    $ErrorActionPreference = 'Continue'
    # robocopy /L so lista (nao copia) e aguenta caminho longo; a ultima
    # linha de "Bytes" traz o total.
    $lista = robocopy $caminho 'C:\__nao_existe__' /L /S /NJH /BYTES /FP /NC /NDL /NFL /R:0 /W:0 2>$null
    $linha = $lista | Where-Object { $_ -match '^\s*Bytes\s*:' } | Select-Object -First 1
    if (-not $linha) { return $null }
    $bytes = [double](($linha -split ':')[1].Trim() -split '\s+')[0]
    return [math]::Round($bytes / 1GB, 2)
}

Write-Host "Limpeza do Aurea antigo - $(if ($Executar) { 'EXECUTANDO' } else { 'ENSAIO (nada sera apagado)' })"
Write-Host "Alvo: $Alvo`n"

# --- 1. O alvo ----------------------------------------------------------------
if (-not (Test-Path -LiteralPath $Alvo)) { Falha "o alvo nao existe (ja foi apagado?)" }
$item = Get-Item -LiteralPath $Alvo -Force
if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Falha "o alvo e link/junction - nao apago atraves de link" }
if ((Resolve-Path -LiteralPath $Alvo).Path -ne $Alvo) { Falha "o caminho resolvido difere do esperado" }
if (-not (Test-Path -LiteralPath (Join-Path $Alvo 'pubspec.yaml')))  { Falha "sem pubspec.yaml - nao parece o projeto Flutter antigo" }
if (-not (Test-Path -LiteralPath (Join-Path $Alvo 'lib\main.dart'))) { Falha "sem lib\main.dart - nao parece o projeto Flutter antigo" }
if (Test-Path -LiteralPath (Join-Path $Alvo 'engine\CMakeLists.txt')) { Falha "tem engine\CMakeLists.txt - isto parece o projeto NOVO" }
Ok "alvo e o projeto Flutter antigo, sem link"

# --- 2. O que foi preservado no repo novo --------------------------------------
if (-not (Test-Path -LiteralPath (Join-Path $RepoNovo 'engine\CMakeLists.txt'))) { Falha "repo novo nao encontrado em $RepoNovo" }
if (-not (Test-Path -LiteralPath $Bundle)) { Falha "bundle nao encontrado: $Bundle" }
$sha = (Get-FileHash -LiteralPath $Bundle -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sha -ne $BundleSha) { Falha "SHA-256 do bundle mudou ($sha)" }
Ok "bundle dos refs so-locais presente e integro"
foreach ($p in @('_identity\signing\IDENTIDADE.md', '_identity\signing\ExportOptions.plist', '_identity\branding\icon',
                 'docs\migration\ui_reference\13_home_aviso.png', 'docs\migration\ui_reference\14_editor_efeitos_motion_tile.png',
                 'docs\migration\ui_reference\15_home_projetos.png', 'docs\migration\ui_reference\16_navegador_de_efeitos.png',
                 'docs\migration\ui_reference\17_splash.png', 'docs\licenses\CupertinoIcons-MIT.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $RepoNovo $p))) { Falha "faltando no repo novo: $p" }
}
Ok "identidade, prints aprovados e licencas estao no repo novo"
if (-not (Test-Path -LiteralPath "$env:USERPROFILE\.android\debug.keystore")) { Falha "a keystore ~/.android/debug.keystore sumiu - pare e verifique" }
Ok "keystore de assinatura continua em ~/.android (fora do alvo)"

# --- 3. Nada novo so-local no git antigo ---------------------------------------
Push-Location -LiteralPath $Alvo
# No PowerShell 5.1, stderr de comando nativo vira erro terminal sob 'Stop'.
$ErrorActionPreference = 'Continue'
try {
    $refs = git for-each-ref --format='%(refname)' refs/heads refs/tags 2>$null
    foreach ($r in $refs) {
        $n = [int](git rev-list --count $r --not --remotes 2>$null)
        if ($n -gt 0) {
            $atual = (git rev-parse $r).Trim()
            if (-not $RefsNoBundle.ContainsKey($r) -or $RefsNoBundle[$r] -ne $atual) {
                Falha "o ref $r tem $n commit(s) so-local(is) fora do bundle - faca push ou refaca o bundle"
            }
        }
    }
    $stash = (git rev-parse -q --verify refs/stash 2>$null)
    if ($stash -and $stash.Trim() -ne $RefsNoBundle['refs/stash']) { Falha "o stash mudou desde a auditoria" }
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }
Ok "todo commit so-local esta no bundle (o resto esta nos remotos)"

# --- 4. output/ (decisao do dono) ----------------------------------------------
$saida = Join-Path $Alvo 'output'
if ($ManterOutput) {
    if ($ManterOutput.StartsWith($Alvo, [StringComparison]::OrdinalIgnoreCase)) { Falha "-ManterOutput precisa ficar FORA da pasta antiga" }
    if (Test-Path -LiteralPath $ManterOutput) { Falha "o destino de -ManterOutput ja existe: $ManterOutput" }
    if ($Executar) {
        Move-Item -LiteralPath $saida -Destination $ManterOutput
        Ok "output/ movida para $ManterOutput"
    } else {
        Write-Host "  (ensaio) output/ seria movida para $ManterOutput"
    }
} elseif (Test-Path -LiteralPath $saida) {
    Write-Host "  aviso  output/ (renders, demo floresta-magica, narracao do tutorial) sera APAGADA junto." -ForegroundColor Yellow
    Write-Host "         Para guardar, rode de novo com -ManterOutput <destino fora da pasta antiga>."
}

# --- 5. Medir e apagar ---------------------------------------------------------
$antes = Tamanho-GB $Alvo
$livreAntes = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
Write-Host "`nTamanho do alvo: $antes GB   |   livre em C: $livreAntes GB"

if (-not $Executar) {
    Write-Host "`nEnsaio concluido. Nada foi apagado. Para apagar: acrescente -Executar." -ForegroundColor Cyan
    exit 0
}

# rd com prefixo \\?\ aguenta caminho > 260 e arquivo somente-leitura do .git.
cmd /c "rd /s /q `"\\?\$Alvo`""
if (Test-Path -LiteralPath $Alvo) { Falha "a pasta ainda existe (algum arquivo em uso? feche IDE/Flutter/Gradle e rode de novo)" }

$livreDepois = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
Write-Host "`nApagado: $Alvo" -ForegroundColor Green
Write-Host "Livre em C: $livreAntes GB -> $livreDepois GB  (+$([math]::Round($livreDepois - $livreAntes, 1)) GB)"
