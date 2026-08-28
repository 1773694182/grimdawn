# x64 插件架构

当前开发路线改为 x64 Launcher + x64 Native Plugin。

## 运行流程

1. 通过 Steam 启动 x64 Grim Dawn，并在 Steam 启动项中加入 `/d3d9`。
2. 启动 x64 `GrimDawnTeleporter.exe`。
3. 点击“检测进程”。
4. 点击“附加插件”。
5. Launcher 使用 `LoadLibraryW` 注入 `GrimDawnTeleporter.Plugin.dll`。
6. Plugin 在游戏进程内启动后台线程。
7. Plugin 创建命名管道：`GrimDawnTeleporter.Plugin.<pid>`。
8. Launcher 通过命名管道发送 `ping`、`get_status` 等命令。

## 当前已实现

- x64 Native DLL 项目骨架。
- DLL 注入服务。
- Plugin 后台线程。
- Plugin 日志。
- Named Pipe 通信。
- `ping` / `get_status` / `get_position` / `teleport` 命令占位。

## 当前未实现

- PatternScanner。
- GameEngine 定位。
- GetMainPlayer 调用。
- WorldVec3 坐标读取。
- InitiatePlayerTeleport 调用。

## 日志位置

Plugin 日志位于系统临时目录：

```text
%TEMP%\GrimDawnTeleporter.Plugin.<pid>.log
```

## 构建

构建 Plugin：

```powershell
& "E:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "src\GrimDawnTeleporter.Plugin\GrimDawnTeleporter.Plugin.vcxproj" /p:Configuration=Release /p:Platform=x64
```

构建 Launcher：

```powershell
dotnet build "src\GrimDawnTeleporter\GrimDawnTeleporter.csproj" -c Release -p:Platform=x64
```

Launcher x64 输出目录会尝试复制：

```text
src\GrimDawnTeleporter.Plugin\bin\x64\Release\GrimDawnTeleporter.Plugin.dll
```
