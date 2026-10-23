param(
    [string]$CygwinRoot = 'C:\cygwin64',
    [switch]$Install
)
$ErrorActionPreference = 'Stop'
$taskBash = Join-Path $CygwinRoot 'bin\bash.exe'
if (!(Test-Path -LiteralPath $taskBash)) {
    throw "Cygwin bash not found at $taskBash. Install the packages listed in docs/WINDOWS.md."
}
$taskOldPath = $env:PATH
try {
    $env:PATH = (Join-Path $CygwinRoot 'bin') + ';' + $taskOldPath
    $taskScript = (Join-Path $PSScriptRoot 'build-cygwin.sh').Replace('\', '/')
    $taskArguments = @('--login', $taskScript)
    if ($Install) { $taskArguments += '--install' }
    & $taskBash @taskArguments
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
} finally {
    $env:PATH = $taskOldPath
}
