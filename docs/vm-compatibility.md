# 虚拟机 / 低版本运行库兼容性说明（游戏闪退问题）

## 现象

在 VMware / VirtualBox 等虚拟机（或未更新 VC 运行库的系统）中：

1. 游戏本身可以正常进入主菜单；
2. 启动 `GrimDawnTeleporter.exe` 后，工具会自动检测 x64 游戏进程并注入 `GrimDawnTeleporter.Plugin.dll`；
3. 注入完成后几秒内游戏直接闪退（进程消失，出现 `错误日志\<guid>\minidump.dmp`）。

## 根因（基于崩溃转储分析）

对 `错误日志\fede5bce-55a1-42b7-ad33-2e88360374dd\minidump.dmp` 的分析结果：

| 项目 | 值 |
| --- | --- |
| 异常码 | `0xC0000005`（访问冲突，读地址 `0x0`） |
| 异常地址 | `msvcp140.dll + 0x13278`（`mov rax, [rax]`，空指针解引用） |
| 崩溃线程 | 插件在 `DllMain` 中创建的 `WorkerThread` |
| 调用链 | `BaseThreadInitThunk` → 插件 `WorkerThread` → 插件 `Log()`（`0xF7D0`）→ `msvcp140!_Mtx_lock`（`+0x131DC`）→ 崩溃 |
| 崩溃对象 | 插件里 `Log()` 的函数级 `static std::mutex` |

`Log()` 的原始实现：

```cpp
void Log(const std::wstring& message)
{
    static std::mutex logMutex;               // ← 崩溃点
    std::lock_guard<std::mutex> guard(logMutex);
    std::wofstream log(GetLogPath(), std::ios::app);
    ...
}
```

关键事实：

- 插件由 **MSVC 工具链 14.43**（VS 2022 17.13）编译（PE LinkerVersion = 14.43）。
- 该虚拟机里的 `C:\Windows\System32\msvcp140.dll` 版本为 **14.32.31326.0**（旧）。
- 14.4x 的 STL 中 `std::mutex` 已被常量初始化（文件镜像中即为 `type=2`、内部临界区未初始化），而 14.32 的 `_Mtx_lock` 仍把对象 `+8` 偏移当作“带虚表的临界区指针”，读到空指针后直接调用 → `0xC0000005`。
- 崩溃发生在**游戏进程内部**，Windows 默认异常处理会结束整个进程，于是表现为“游戏闪退”。

> 简单说：**用新 STL 编译的插件 + 旧 msvcp140.dll → 注入即崩**。宿主机器（运行库较新）不会触发，所以同一份插件在物理机上正常、在虚拟机里必崩。

## 已做的兼容性改造

### 插件（`src\GrimDawnTeleporter.Plugin`）

1. **静态链接 CRT（`/MT`）**，插件不再导入 `MSVCP140.dll` / `VCRUNTIME140.dll`，与系统运行库版本彻底解耦。
   - 见 `GrimDawnTeleporter.Plugin.vcxproj` 中的 `<RuntimeLibrary>`。
2. **日志改为纯 Win32 实现**：`CreateFileW` + `WriteFile` + `SRWLOCK`，不再使用 `std::mutex` / `std::wofstream`，并且整个写入过程带 `__try/__except`，日志失败不会影响游戏。
3. **线程入口增加 SEH 保护**：`WorkerThread`、`AutoKillThread` 的结构化异常只会终止插件自己的线程并记录日志，不会再穿透到游戏进程。
4. **插件禁用开关**（用于排查问题，无需重新编译）：
   - 在插件 DLL 同目录放置 `GrimDawnTeleporter.Plugin.dll.disabled` 文件；或
   - 设置环境变量 `GRIMDAWN_TELEPORTER_DISABLE_PLUGIN=1`。
5. `ping` 命令新增字段：`{"type":"pong","plugin":"...","runtime":"static-crt","compat":2}`，便于工具侧确认加载的是新版插件。

### 工具（`src\GrimDawnTeleporter`）

1. **默认不再自动注入插件**（`MemoryConfig.AutoAttachPlugin` 默认为 `false`）。
   - 顶部栏新增勾选项“启动时自动注入插件”，默认不勾选。
   - 默认走“外部内存读取/写入”模式，传送功能仍可用（需要坐标指针链或自动扫描地址）。
2. **注入前运行库兼容性检查**（`PluginCompatibility.cs`）：
   - 解析插件 PE 的 LinkerVersion 与导入表；
   - 如果插件仍依赖 `MSVCP140.dll`，则与系统 `msvcp140.dll` 版本比对；
   - 版本不满足时**拒绝注入**并给出中文说明，避免“注入即闪退”。
   - 如需强制注入可在 `data\MemoryConfig.json` 中设置 `"AllowIncompatiblePlugin": true`（不建议）。
3. **传送/读坐标自动降级**：x64 进程未注入插件时，自动改用外部内存指针链（`TeleportService`），状态栏会标注“兼容模式”。
4. **虚拟机提示**：检测到虚拟机环境时在状态栏给出兼容性提醒。

## 重新构建

```powershell
./build-release.ps1
```

要求：Visual Studio 2022（含 C++ 桌面开发工作负载）+ .NET 8 SDK。

构建后请确认新插件不再依赖 `MSVCP140.dll`（PowerShell 快速检查）：

```powershell
$dll = "dist\GrimDawnTeleporter-x64\GrimDawnTeleporter.Plugin.dll"
$bytes = [IO.File]::ReadAllBytes($dll)
[Text.Encoding]::ASCII.GetString($bytes) -match 'MSVCP140'   # 期望输出 False
```

也可以直接把插件 DLL 拖进 `dumpbin /dependents` 或 https://github.com/lucasg/Dependencies 查看依赖。

## 在虚拟机中验证

1. 启动 x64 游戏，进入主菜单；
2. 启动 `GrimDawnTeleporter.exe`（默认兼容模式，不会注入）；
3. 点击“检测进程”→ 状态栏应显示“兼容模式”；
4. 点击“附加插件”：
   - 若插件为旧版（依赖 msvcp140 且本机版本过低），会弹出明确的兼容性提示并中止注入，**游戏不闪退**；
   - 若插件为新版（静态 CRT），注入成功，状态栏显示“已注入插件”，`%TEMP%\GrimDawnTeleporter.Plugin.<pid>.log` 中可以看到 `plugin loaded`。

若仍然闪退，请：

- 用 `GrimDawnTeleporter.Plugin.dll.disabled` 禁用插件，确认闪退消失；
- 检查 `%TEMP%\GrimDawnTeleporter.Plugin.*.log` 与游戏 `错误日志` 目录下的新转储；
- 把新的 `minidump.dmp` 交给开发者做同样方式的栈回溯分析。
