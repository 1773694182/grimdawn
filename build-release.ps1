param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipPublish,
    [switch]$SkipPlugin,
    [string]$MsBuildPath = ''
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$solutionPath = Join-Path $root 'GrimDawnTeleporter.sln'
$pluginProjectPath = Join-Path $root 'src\GrimDawnTeleporter.Plugin\GrimDawnTeleporter.Plugin.vcxproj'
$appProjectPath = Join-Path $root 'src\GrimDawnTeleporter\GrimDawnTeleporter.csproj'

function Resolve-MSBuild {
    param([string]$ExplicitPath)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (Test-Path -LiteralPath $ExplicitPath) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }

        throw "指定的 MSBuild 路径不存在：$ExplicitPath"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:MSBUILD_EXE_PATH) -and (Test-Path -LiteralPath $env:MSBUILD_EXE_PATH)) {
        return $env:MSBUILD_EXE_PATH
    }

    # 1) vswhere（最可靠）
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswhere)) {
            continue
        }

        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($found) -and (Test-Path -LiteralPath $found)) {
            return $found
        }

        $installPath = & $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            $candidate = Join-Path $installPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    # 2) 注册表
    foreach ($key in @(
            'HKLM:\SOFTWARE\Microsoft\VisualStudio\SxS\VS7',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\SxS\VS7')) {
        try {
            $value = (Get-ItemProperty -Path $key -ErrorAction Stop).'17.0'
            if (-not [string]::IsNullOrWhiteSpace($value)) {
                $candidate = Join-Path $value 'MSBuild\Current\Bin\MSBuild.exe'
                if (Test-Path -LiteralPath $candidate) {
                    return $candidate
                }
            }
        }
        catch {
        }
    }

    # 3) 常见安装位置（含 VS2022 各版本与 BuildTools）
    $patterns = @(
        "$env:ProgramFiles\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
        'C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'D:\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'E:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'E:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    )

    foreach ($pattern in $patterns) {
        $match = Get-Item -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $match) {
            return $match.FullName
        }
    }

    return $null
}

function Resolve-DotNet {
    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in @(
            (Join-Path $env:ProgramFiles 'dotnet\dotnet.exe'),
            (Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'),
            (Join-Path $env:USERPROFILE '.dotnet\dotnet.exe'))) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Show-ToolchainHelp {
    param([string]$Missing)

    Write-Host ''
    Write-Host "未找到 $Missing，无法继续。" -ForegroundColor Yellow
    Write-Host '可选方案：'
    Write-Host '  1) 安装构建工具（本机，需管理员权限）：'
    Write-Host '     winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended"'
    Write-Host '  2) 在有 Visual Studio + .NET SDK 的机器上执行本脚本，然后把整个输出目录拷过来。'
    Write-Host '  3) 只想重建启动器（不需要 C++ 工具链，但需要 .NET 8 SDK）：'
    Write-Host '     .\build-release.ps1 -SkipPlugin'
    Write-Host ''
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

$dotnetPath = Resolve-DotNet
if ($null -eq $dotnetPath) {
    Show-ToolchainHelp 'dotnet（.NET 8 SDK）'
    throw 'dotnet was not found. Install the .NET 8 SDK (or Visual Studio 2022 with the .NET desktop workload).'
}

$msbuildPath = $null
if (-not $SkipPlugin) {
    $msbuildPath = Resolve-MSBuild -ExplicitPath $MsBuildPath
    if ($null -eq $msbuildPath) {
        Show-ToolchainHelp 'MSBuild（Visual Studio 2022 / Build Tools）'
        throw 'MSBuild.exe was not found.'
    }
}

"Configuration: $Configuration"
"dotnet: $dotnetPath"
if ($null -ne $msbuildPath) {
    "MSBuild: $msbuildPath"
}

if (-not $SkipPlugin) {
    "Building x64 plugin DLL..."
    Invoke-NativeCommand { & $msbuildPath $pluginProjectPath /restore /m /p:Configuration=$Configuration /p:Platform=x64 }
}
else {
    "Skipped plugin build (-SkipPlugin)."
}

"Building x86 app EXE..."
Invoke-NativeCommand { & $dotnetPath build $appProjectPath -c $Configuration -p:Platform=x86 }

"Building x64 app EXE..."
Invoke-NativeCommand { & $dotnetPath build $appProjectPath -c $Configuration -p:Platform=x64 }

$pluginDll = Join-Path $root "src\GrimDawnTeleporter.Plugin\bin\x64\$Configuration\GrimDawnTeleporter.Plugin.dll"
$x64Output = Join-Path $root "src\GrimDawnTeleporter\bin\x64\$Configuration\net8.0-windows\win-x64"

if (-not (Test-Path -LiteralPath $pluginDll)) {
    if ($SkipPlugin) {
        Write-Host "警告：未找到插件 DLL（$pluginDll），将只构建启动器。插件功能需要手动放入插件文件。" -ForegroundColor Yellow
    }
    else {
        throw "Plugin DLL was not generated: $pluginDll"
    }
}

if (Test-Path -LiteralPath $pluginDll) {
    # 兼容性校验：插件必须静态链接 CRT，否则在虚拟机/旧运行库机器上注入会导致游戏闪退。
    "Checking plugin runtime dependencies..."
    $pluginBytes = [System.IO.File]::ReadAllBytes($pluginDll)
    $pluginText = [System.Text.Encoding]::ASCII.GetString($pluginBytes)
    $dynamicCrt = @()
    foreach ($runtimeDll in @('MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')) {
        if ($pluginText.Contains($runtimeDll)) {
            $dynamicCrt += $runtimeDll
        }
    }

    if ($dynamicCrt.Count -gt 0) {
        $message = "插件仍然动态依赖 $($dynamicCrt -join '、')，在运行库版本较旧的机器（如虚拟机）上会导致游戏闪退。请确认 GrimDawnTeleporter.Plugin.vcxproj 中 RuntimeLibrary 为 MultiThreaded / MultiThreadedDebug 后重新编译。"
        if ($SkipPlugin) {
            Write-Host "警告：$message" -ForegroundColor Yellow
            Write-Host '（当前使用 -SkipPlugin，已跳过插件构建；新的启动器会自动阻止这类插件注入。）' -ForegroundColor Yellow
        }
        else {
            throw $message
        }
    }
}

if (-not (Test-Path -LiteralPath $x64Output)) {
    throw "x64 output directory was not found: $x64Output"
}

if (Test-Path -LiteralPath $pluginDll) {
    Copy-Item -LiteralPath $pluginDll -Destination $x64Output -Force
}

$x86Exe = Join-Path $root "src\GrimDawnTeleporter\bin\x86\$Configuration\net8.0-windows\win-x86\GrimDawnTeleporter.exe"
$x64Exe = Join-Path $x64Output 'GrimDawnTeleporter.exe'
$x64PluginDll = Join-Path $x64Output 'GrimDawnTeleporter.Plugin.dll'

"Build completed."
"x86 EXE: $x86Exe"
"x64 EXE: $x64Exe"
"x64 plugin DLL: $x64PluginDll"

if ($SkipPublish) {
    "Publish skipped."
    return
}

"Publishing self-contained x64 package..."
$publishDir = Join-Path $root "dist\GrimDawnTeleporter-x64"
if (Test-Path -LiteralPath $publishDir) {
    Remove-Item -LiteralPath $publishDir -Recurse -Force
}

Invoke-NativeCommand { & $dotnetPath publish $appProjectPath -c $Configuration -p:Platform=x64 -p:SelfContained=true -p:PublishSingleFile=false -p:DebugType=none --nologo -o $publishDir }

if (Test-Path -LiteralPath $pluginDll) {
    Copy-Item -LiteralPath $pluginDll -Destination $publishDir -Force
}

$readmePath = Join-Path $publishDir '使用说明.txt'
$readmeContent = @'
GrimDawnTeleporter x64 自包含版本

1. 本目录包含完整 .NET 运行时，目标机器无需安装 .NET。
2. 请整体复制或移动本目录，不要只复制 GrimDawnTeleporter.exe。
3. 双击 GrimDawnTeleporter.exe 运行，程序会请求管理员权限（注入插件需要）。
4. 首次使用请检查 data\MemoryConfig.json 中的游戏路径配置。
5. 插件仅支持 x64 游戏进程，请以 x64 模式启动 Grim Dawn。
6. 默认不自动注入插件（兼容模式），需要插件功能时点击“附加插件”。
   插件与系统 VC 运行库版本不匹配时工具会拒绝注入，避免游戏闪退。
7. 通过工具“设置与调试”页启动的游戏，会在关闭工具时一并结束。

数据文件位于 data\ 目录：
  MemoryConfig.json   游戏路径与坐标指针链配置
  TeleportPoints.json 传送点列表（运行时生成）
'@
[System.IO.File]::WriteAllText($readmePath, $readmeContent, [System.Text.UTF8Encoding]::new($false))

"Publish completed: $publishDir"
"Publish EXE: $(Join-Path $publishDir 'GrimDawnTeleporter.exe')"
