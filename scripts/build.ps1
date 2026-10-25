param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$BuildDirectory,
    [string]$DependencyCache,
    [switch]$CoreOnly,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $taskProject "build\$($Configuration.ToLowerInvariant())" }
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)

if ($IsWindows -and -not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $taskVswhere)) { throw 'Install the Visual Studio C++ Build Tools or use a configured C++ developer terminal.' }
    $taskInstallation = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $taskInstallation) { throw 'The Visual Studio Desktop C++ workload is missing.' }
    & (Join-Path $taskInstallation 'Common7\Tools\Launch-VsDevShell.ps1') -SkipAutomaticLocation -Arch amd64 -HostArch amd64
}
if ($IsWindows) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $taskVswhere) {
        $taskInstallation = & $taskVswhere -latest -products '*' -property installationPath
        foreach ($taskRelative in @('Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin','Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja')) {
            $taskDirectory = Join-Path $taskInstallation $taskRelative
            if (Test-Path -LiteralPath $taskDirectory) { $env:Path = "$taskDirectory;$env:Path" }
        }
    }
}
foreach ($taskTool in @('cmake','ninja')) {
    if (-not (Get-Command $taskTool -ErrorAction SilentlyContinue)) { throw "Missing tool: $taskTool" }
}

$taskConfigure = @('--preset',$(if($CoreOnly){'core-only'}else{$Configuration.ToLowerInvariant()}),'-B',$BuildDirectory,'-DSTREFA_WARNINGS_AS_ERRORS=ON')
if ($DependencyCache) { $taskConfigure += "-DSTREFA_DOWNLOAD_CACHE=$([System.IO.Path]::GetFullPath($DependencyCache))" }
Push-Location $taskProject
try {
    & cmake @taskConfigure
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $BuildDirectory --parallel ([Math]::Min(8,[Environment]::ProcessorCount))
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if (-not $SkipTests) {
        & ctest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
} finally { Pop-Location }
