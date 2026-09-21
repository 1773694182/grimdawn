# API 查找指南

## 概述

本文档说明如何使用插件的诊断功能来查找秒杀怪物和一击必杀所需的 API。

---

## 方法一：使用 resolve_core 命令 (推荐)

### 步骤

1. **启动游戏** - 确保 Grim Dawn 正在运行
2. **启动工具** - 运行 `GrimDawnTeleporter.exe` (x64 版本)
3. **点击"检测进程"** - 确认工具识别到游戏进程
4. **打开调试控制台** (如果有的话) 或通过 Named Pipe 发送命令

### 发送命令

```
resolve_core
```

### 预期输出

```json
{
  "type": "core_symbols",
  "module": "Game.dll/Engine.dll",
  "gGameEngine": "0x140abc123",
  "GetMainPlayer": "0x140def456",
  "GetPlayerManagerClient": "0x140ghi789",
  "GetPlayerId": "0x140jkl012",
  "GetPlayerLocation": "0x140mno345",
  "GetWorldPosition": "0x140pqr678",
  "InitiatePlayerTeleport": "0x140stu901",
  "CtoS_StartTeleportInbound": "0x140vwx234",
  "StoC_StartTeleportInbound": "0x140yza567",
  "GetCurrentMoney": "0x140bcd890",
  "AddMoney": "0x140efg123",
  "SubtractMoney": "0x140hij456",
  "ApplyDamage": "0x0",                    // ⚠️ 可能未找到
  "TakeDamage": "0x0",                     // ⚠️ 可能未找到
  "DesignerCalculateOffensiveAbility": "0x0", // ⚠️ 可能未找到
  "GetEntityCount": "0x0",                 // ⚠️ 可能未找到
  "GetEntity": "0x0",                      // ⚠️ 可能未找到
  "IsMonster": "0x0",                      // ⚠️ 可能未找到
  "GetEntityType": "0x0"                   // ⚠️ 可能未找到
}
```

### 结果分析

- ✅ **非零地址**: API 已找到，可以使用
- ❌ **0x0**: API 未通过导出表找到，需要其他方法

---

## 方法二：使用 diagnose_symbols 命令

### 发送命令

```
diagnose_symbols
```

### 作用

在 Game.dll 中搜索字符串引用，帮助定位相关代码位置。

### 预期输出

```json
{
  "type": "symbol_strings",
  "bindSection": "0x140fff000",
  "gGameEngine@GAME": {
    "address": "0x140aaa111",
    "prefix2": "48 8D",
    "prefix3": "48 8B",
    "vaRefs": 5,
    "ripRefs": 3,
    ...
  },
  "GetPlayerManagerClient@GameEngine@GAME": {
    "address": "0x140bbb222",
    "prefix2": "48 8D",
    "prefix3": "48 8B",
    "vaRefs": 2,
    "ripRefs": 1,
    ...
  },
  ...
}
```

### 结果分析

- **vaRefs > 0**: 有直接调用引用
- **ripRefs > 0**: 有 RIP 相对引用 (x64 常见)
- **firstRipRef**: 第一个 RIP 引用的地址，可用于进一步分析

---

## 方法三：使用 scan_pattern 命令

### 发送命令

```
scan_pattern:Game.dll|<hex_pattern>
```

### 示例：搜索 ApplyDamage 特征码

如果你有 Cheat Engine 或其他工具找到的特征码:

```
scan_pattern:Game.dll|C7 41 ?? ?? ?? ?? ?? C3 55 8B EC 83 EC 10
```

### 作用

在 Game.dll 中扫描特定的字节模式。

---

## 方法四：IDA Pro 手动搜索 (最可靠)

### 步骤

1. **打开 IDA Pro**
2. **加载文件**: `Grim Dawn.exe` 或 `Game.dll`
3. **分析所有模块**
4. **使用 Search → Text sequence patterns**
5. **搜索以下字符串**:

```
EntitySystem
GetEntity
GetEntityCount
IsMonster
ApplyDamage
TakeDamage
DesignerCalculateOffensiveAbility
PlayerManagerClient
```

6. **查看交叉引用**
7. **记录函数地址**

### 导出符号名称

在 IDA 中，按 `N` 键可以重命名符号，然后查看完整的 mangled name。

---

## 关键 API 查找状态

### P0 - 必须找到

| API | 推测名称 | 导出表 | 字符串引用 | 优先级 |
|-----|---------|--------|-----------|-------|
| GetPlayerManagerClient | ?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ | 可能 | 可能 | P0 |
| GetPlayerId | ?GetPlayerId@GameEngine@GAME@@QEBAIXZ | 可能 | 可能 | P0 |
| GetPlayerLocation | ?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z | 可能 | 可能 | P0 |
| ApplyDamage | ?ApplyDamage@Character@GAME@@QEAAXPEAV3_MI@Z | 不确定 | 不确定 | P0 |

### P1 - 重要

| API | 推测名称 | 导出表 | 字符串引用 | 优先级 |
|-----|---------|--------|-----------|-------|
| GetEntityCount | ?GetEntityCount@EntitySystem@GAME@@QEBAIXZ | 不确定 | 不确定 | P1 |
| GetEntity | ?GetEntity@EntitySystem@GAME@@QEBAPEAVEntity@2_H@Z | 不确定 | 不确定 | P1 |
| IsMonster | ?IsMonster@Entity@GAME@@QEB_NXZ | 不确定 | 不确定 | P1 |

---

## 如果找不到怎么办？

### 方案 A: Pattern Scanner

1. 使用 Cheat Engine 附加游戏进程
2. 搜索字符串 "EntitySystem" 或 "ApplyDamage"
3. 找到后查看相邻的指令
4. 提取特征码
5. 使用 `scan_pattern` 命令验证

### 方案 B: 手动逆向

1. 用 IDA Pro 打开 Game.dll
2. 搜索相关字符串
3. 追踪调用链
4. 分析函数签名
5. 记录正确的 mangled name

### 方案 C: 参考 DPYes

检查 DPYes 项目是否已经实现了类似功能:
- 查看其源码
- 对比符号名称
- 复用已找到的地址

---

## 下一步行动

1. ✅ 编译包含新诊断命令的代码
2. 🔄 运行 `resolve_core` 查看哪些 API 可用
3. 📋 记录所有找到的地址
4. 🔍 对未找到的 API 使用其他方法
5. 💻 实现核心功能
6. 🧪 测试验证

---

## 实用脚本

### PowerShell 发送命令

```powershell
# 假设有一个控制台或管道工具
$pipeName = "\\.\pipe\GrimDawnTeleporter.Plugin.XXXXX"
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $pipeName, Direction::Out)
$pipe.Connect()
$writer = New-Object System.IO.StreamWriter $pipe
$writer.WriteLine("resolve_core")
$writer.Flush()
$reader = New-Object System.IO.StreamReader $pipe
$response = $reader.ReadToEnd()
Write-Host $response
$pipe.Close()
```

---

**最后更新**: 2026-09-19  
**版本**: v1.0
