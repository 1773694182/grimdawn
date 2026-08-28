param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$solutionPath = Join-Path $root 'GrimDawnTeleporter.sln'
$pluginProjectPath = Join-Path $root 'src\GrimDawnTeleporter.Plugin\GrimDawnTeleporter.Plugin.vcxproj'
$appProjectPath = Join-Path $root 'src\GrimDawnTeleporter\GrimDawnTeleporter.csproj'

function Resolve-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($msbuild) {
            return $msbuild
        }
    }

    $fallbackPaths = @(
        'E:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    )

    foreach ($path in $fallbackPaths) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    throw 'MSBuild.exe was not found. Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload.'
}

if (-not (Test-Path -LiteralPath $solutionPath)) {
    throw "Solution was not found: $solutionPath"
}

if (-not (Test-Path -LiteralPath $pluginProjectPath)) {
    throw "Plugin project was not found: $pluginProjectPath"
}

if (-not (Test-Path -LiteralPath $appProjectPath)) {
    throw "App project was not found: $appProjectPath"
}

$msbuildPath = Resolve-MSBuild

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE."
    }
}

"Configuration: $Configuration"
"Building x64 plugin DLL..."
Invoke-NativeCommand { & $msbuildPath $pluginProjectPath /restore /m /p:Configuration=$Configuration /p:Platform=x64 }

"Building x86 app EXE..."
Invoke-NativeCommand { dotnet build $appProjectPath -c $Configuration -p:Platform=x86 }

"Building x64 app EXE..."
Invoke-NativeCommand { dotnet build $appProjectPath -c $Configuration -p:Platform=x64 }

$pluginDll = Join-Path $root "src\GrimDawnTeleporter.Plugin\bin\x64\$Configuration\GrimDawnTeleporter.Plugin.dll"
$x64Output = Join-Path $root "src\GrimDawnTeleporter\bin\x64\$Configuration\net8.0-windows\win-x64"

if (-not (Test-Path -LiteralPath $pluginDll)) {
    throw "Plugin DLL was not generated: $pluginDll"
}

if (-not (Test-Path -LiteralPath $x64Output)) {
    throw "x64 output directory was not found: $x64Output"
}

Copy-Item -LiteralPath $pluginDll -Destination $x64Output -Force

$x86Exe = Join-Path $root "src\GrimDawnTeleporter\bin\x86\$Configuration\net8.0-windows\win-x86\GrimDawnTeleporter.exe"
$x64Exe = Join-Path $x64Output 'GrimDawnTeleporter.exe'
$x64PluginDll = Join-Path $x64Output 'GrimDawnTeleporter.Plugin.dll'

"Build completed."
"x86 EXE: $x86Exe"
"x64 EXE: $x64Exe"
"x64 plugin DLL: $x64PluginDll"
