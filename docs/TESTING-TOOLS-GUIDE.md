# 测试和探查功能使用指南

## 概述

在"高级/调试/作弊"面板中新增了测试和探查工具，用于查找和诊断游戏 API。

---

## 访问方式

1. **启动工具** - 运行 `GrimDawnTeleporter.exe` (x64 版本)
2. **检测进程** - 点击"检测进程"按钮
3. **附加插件** - 确保插件已成功注入
4. **展开高级选项** - 点击"高级/调试/作弊"折叠面板

---

## 可用功能

### 1. 列出模块 (List Modules)

**功能**: 列出所有已加载的游戏模块及其地址

**使用方法**:
- 点击"列出模块"按钮
- 查看输出窗口显示的 JSON 结果

**输出示例**:
```json
{
  "type": "modules",
  "count": 15,
  "module[0]":{"name":"Grim Dawn.exe","base":"0x7ff6a1230000"},
  "module[1]":{"name":"Game.dll","base":"0x7ff7b2340000"},
  ...
}
```

**用途**: 
- 确认 Game.dll 是否已加载
- 获取模块基址用于进一步分析

---

### 2. 扩展命令 (Expand Commands)

**功能**: 显示更多测试命令选项

**使用方法**:
- 点击"扩展命令"按钮
- 展开后可以看到自定义命令输入框

---

### 3. 自定义命令 (Custom Commands)

**功能**: 发送任意诊断命令到插件

**可用命令**:

| 命令 | 描述 | 返回内容 |
|------|------|---------|
| `resolve_core` | 解析核心符号地址 | 包含所有关键 API 的地址 |
| `diagnose_symbols` | 诊断字符串引用 | Game.dll 中的字符串位置 |
| `get_position` | 获取玩家位置 | 当前坐标 XYZ |
| `get_money` | 获取当前货币 | 当前金币数量 |
| `list_modules` | 列出所有模块 | 已加载模块列表 |
| `insta_kill:50` | 秒杀怪物 (半径 50) | 执行状态 |
| `one_shot:true` | 开启一击必杀 | 模式切换状态 |

**使用方法**:
1. 在"输入自定义命令"框中输入命令
2. 点击"发送命令"按钮
3. 查看输出窗口的响应

**示例**:
```
输入：resolve_core
输出：{"type":"core_symbols","gGameEngine":"0x140abc123","GetPlayerManagerClient":"0x0",...}
```

---

## 工作流程

### 完整诊断流程

1. **基础检查**
   ```
   点击"列出模块" → 确认 Game.dll 已加载
   ```

2. **符号解析**
   ```
   输入：resolve_core
   发送命令 → 查看所有 API 地址
   ```

3. **字符串搜索**
   ```
   输入：diagnose_symbols
   发送命令 → 查找相关字符串引用
   ```

4. **功能验证**
   ```
   输入：get_position
   发送命令 → 验证插件通信正常
   ```

---

## 结果解读

### resolve_core 输出

```json
{
  "type": "core_symbols",
  "module": "Game.dll/Engine.dll",
  "gGameEngine": "0x140abc123",      // ✅ 已找到
  "GetPlayerManagerClient": "0x0",    // ❌ 未找到
  "ApplyDamage": "0x0",               // ❌ 未找到
  ...
}
```

**说明**:
- **非零地址** (如 `0x140abc123`) = API 存在且可通过导出表访问
- **0x0** = API 未通过导出表暴露，需要其他方法查找

---

### diagnose_symbols 输出

```json
{
  "type": "symbol_strings",
  "GetPlayerManagerClient@GameEngine@GAME": {
    "found": true,
    "address": "0x140def456",
    "vaRefs": 5,
    "ripRefs": 3
  }
}
```

**说明**:
- **found: true** = 在 Game.dll 中找到该字符串
- **vaRefs > 0** = 有直接调用引用
- **ripRefs > 0** = 有 RIP 相对引用 (x64 平台常见)

---

## 实用技巧

### 技巧 1: 批量测试

可以连续发送多个诊断命令:
```
resolve_core
diagnose_symbols  
get_position
```

每个命令的结果都会追加到输出窗口。

### 技巧 2: 保存结果

输出窗口支持复制:
1. 选中需要的内容
2. 右键复制或使用 Ctrl+C
3. 粘贴到文本文件保存

### 技巧 3: 快速常用命令

输出窗口下方列出了常用命令，双击或参考快速输入。

---

## 注意事项

⚠️ **重要提醒**:
- 测试功能仅用于开发和调试
- 请在单人模式下使用
- 不要在多人游戏中使用任何修改功能
- 使用前建议备份存档

---

## 故障排除

### Q1: 点击"列出模块"无响应

**可能原因**: 插件未正确附加

**解决方法**:
1. 重新点击"附加插件"
2. 等待插件加载完成
3. 再次尝试

### Q2: 返回错误信息

**可能原因**: 
- 游戏版本不匹配
- 插件版本过旧

**解决方法**:
1. 确保使用最新版本的工具和插件
2. 检查游戏是否为支持的版本

### Q3: 找不到某些 API

**说明**: 这是正常现象

**下一步**:
1. 使用 IDA Pro 手动搜索
2. 参考 [API 查找指南](docs/API-FINDING-GUIDE.md)
3. 使用特征码扫描

---

## 相关文件

- [API 查找指南](docs/API-FINDING-GUIDE.md) - 详细查找步骤
- [秒杀怪物实现指南](docs/insta-kill-implementation.md) - 完整技术方案
- [API 查找总结](API-FINDING-SUMMARY.md) - 技术细节

---

**最后更新**: 2026-09-19  
**版本**: v1.0
