# Grim Dawn 工具 - 开发计划与功能完善文档

## 📋 项目概述

本文档基于 DPYes 逆向分析报告，为 Grim Dawn 坐标传送工具制定功能扩展和 UI 优化计划。

**当前时间**: 2026 年 9 月 19 日  
**版本**: v1.0 (初步实现)

---

## ✅ 已完成功能

### 1. 无敌模式 (God Mode)
- **状态**: ⚠️ 占位实现（开关状态已可记录，游戏内调用未实装）
- **实现位置**: 
  - 插件端：`dllmain.cpp` - `SetGodModeInternal()`（TODO：调用 `Character::SetGod` / `SetInvincible`）, `TrySetGodMode()`, `TryGetGodModeStatus()`
  - 主程序端：`MainWindow.xaml.cs` - `GodModeToggle_Click()`, `GodModeStatus_Click()`
- **快捷键**: F8
- **使用说明**:
  ```
  命令格式：god_mode:true|false
  待实装游戏 API: Character@GAME::SetGod, Character@GAME::SetInvincible
  ```

### 2. 货币修改
- **状态**: ⚠️ 命令链完整，依赖游戏导出符号解析，待实机验证
- **功能**: 读取/设置游戏货币数量
- **API**: `Character@GAME::GetCurrentMoney`, `AddMoney`, `SubtractMoney`

### 3. 秒杀怪物 (占位实现)
- **状态**: ⚠️ 占位实现，需要进一步逆向
- **实现位置**: 
  - 插件端：`InstaKillMonstersInternal()`, `TryInstaKill()`
  - 主程序端：`InstaKill_Click()`
- **当前限制**: 仅记录请求，未实际遍历怪物和施加伤害
- **待解决问题**:
  - 需要找到怪物列表遍历接口 `GetEntitiesInSphere()`
  - 需要找到伤害施加接口 `ApplyDamage()` 或 `SubtractLife()`

---

## 🎯 待开发功能

### 阶段 1: 核心作弊功能完善 (优先级：高)

#### 1.1 一击必杀 (One-Shot)
**目标**: 玩家攻击造成固定巨额伤害

**技术路线**:
```cpp
// 方案 A: Hook DesignerCalculateOffensiveAbility
// 修改攻击力计算，返回固定最大值

// 方案 B: Hook ApplyDamage
// 拦截伤害结算，强制暴击并设置最大伤害值
```

**需要逆向的符号**:
- `?DesignerCalculateOffensiveAbility@CombatManager@GAME@@?AUNvec3_N0VPlayer@2@VCharacter@2@AEAVMonster@2@M@Z`
- `?ApplyDamage@CombatManager@GAME@@UEBAXNvec3_AUVActor@2@@Z`

**UI 设计**:
```xml
<Button Content="一击必杀" Click="OneShotToggle_Click" ToolTip="切换一击必杀 (F9)" />
<TextBlock x:Name="OneShotStatus" Text="关闭" Foreground="Red" />
```

#### 1.2 属性修改
**目标**: 修改角色基础属性

**需要查找的内存地址**:
```
Character 类成员变量偏移:
- 力量 (Strength): ?mStrength@Character@GAME@@IEAA@Z
- 敏捷 (Agility): ?mAgility@Character@GAME@@IEAA@Z  
- 智力 (Intellect): ?mIntellect@Character@GAME@@IEAA@Z
- OA (Outmaneuver): ?mOffensiveAbility@Character@GAME@@MAEAA@Z
- DA (Defense Ability): ?mDefensiveAbility@Character@GAME@@MAEAA@Z
- 生命最大值: ?mMaxHealth@Character@GAME@@MAEAA@Z
- 法力最大值: ?mMaxMana@Character@GAME@@MAEAA@Z
```

**UI 设计**:
```xml
<GroupBox Header="属性修改">
    <StackPanel>
        <TextBlock Text="力量:" />
        <TextBox x:Name="StrengthBox" Text="100" />
        <Button Content="应用" Click="ModifyAttribute_Click" />
    </StackPanel>
</GroupBox>
```

#### 1.3 无限资源
**目标**: 无限体力、耐力、技能冷却

**待查找接口**:
- 体力/耐力管理：`StaminaManager`, `EnduranceManager`
- 技能冷却：`SkillManager::ResetCooldown`

---

### 阶段 2: UI 重构与优化 (优先级：中)

#### 2.1 多标签页设计
**当前问题**: 所有功能堆叠在一个界面，显得杂乱

**设计方案**:
```xml
<TabControl>
    <TabItem Header="📍 传送点管理">
        <!-- 现有传送点功能 -->
    </TabItem>
    
    <TabItem Header="⚔️ 作弊工具">
        <!-- 新增作弊功能 -->
        - 无敌模式
        - 一击必杀
        - 属性修改
        - 货币管理
    </TabItem>
    
    <TabItem Header="🔧 调试信息">
        <!-- 原有高级功能 -->
        - 进程检测
        - 插件状态
        - 坐标读取
    </TabItem>
</TabControl>
```

#### 2.2 现代化配色方案
```css
/* 主色调 */
--primary-color: #1E293B;      /* 深蓝灰 */
--accent-color: #3B82F6;       /* 蓝色 */
--success-color: #10B981;      /* 绿色 */
--warning-color: #F59E0B;      /* 橙色 */
--danger-color: #EF4444;       /* 红色 */

/* 背景色 */
--bg-dark: #0F172A;            /* 深色背景 */
--bg-light: #1E293B;           /* 浅色背景 */

/* 文字色 */
--text-primary: #F8FAFC;       /* 白色 */
--text-secondary: #94A3B8;     /* 灰色 */
```

#### 2.3 分组侧边栏优化
**当前问题**: 分组列表不够直观

**改进方案**:
- 使用树形结构展示分组层级
- 添加分组图标
- 支持拖拽排序
- 显示每个分组的传送点数量徽章

---

### 阶段 3: 用户体验提升 (优先级：低)

#### 3.1 全局配置系统
**功能**:
- 快捷键自定义
- 自动保存/加载配置
- 主题切换 (深色/浅色)

#### 3.2 操作日志
**功能**:
- 记录所有操作历史
- 支持导出为 JSON
- 错误提示优化

#### 3.3 首次使用引导
**功能**:
- 新手教程弹窗
- 功能说明 Tooltip
- 常见问题 FAQ

---

## 🔬 逆向工程待办事项

### 高优先级符号解析

#### 1. 怪物相关接口
```cpp
// 需要找到以下符号的实现地址
"?GetEntitiesInSphere@ZoneManager@GAME@@?A??AVVector3@2@VCharacter@2@MAMAVQEBUActor@2@@Z"
"?ApplyDamage@CombatManager@GAME@@UEBAXNvec3_AUVActor@2@@Z"
"?SubtractLife@Character@GAME@@QEBA_NM@Z"
```

**搜索策略**:
1. 在 `Game.dll` 中扫描 ASCII 字符串 "GetEntitiesInSphere"
2. 使用 Pattern Scanner 查找调用模式
3. 分析 RIP 相对引用确定函数位置

#### 2. 属性管理接口
```cpp
// Character 类属性访问器
"?GetStrength@Character@GAME@@QEBAHXZ"
"?SetStrength@Character@GAME@@QEAAXH@Z"
"?GetAgility@Character@GAME@@QEBAHXZ"
"?SetAgility@Character@GAME@@QEAAXH@Z"
```

#### 3. 技能与冷却
```cpp
// Skill Manager
"?GetSkillManager@GameEngine@GAME@@QEBAPEAVSkillManager@2@XZ"
"?ResetAllCooldowns@SkillManager@GAME@@QEAAXXZ"
```

### 逆向工具建议

#### Pattern Scanner 增强
```cpp
// 添加更多预定义模式
const char* MONSTER_LIST_PATTERN = "48 89 5C 24 ?? 57 48 83 EC 20 48 8B F9...";
const char* DAMAGE_API_PATTERN = "48 89 5C 24 ?? 57 48 83 EC 50 48 8B D9...";
```

#### Symbol Resolver 优化
```cpp
// 支持模糊匹配
DWORD64 ResolveSymbolPartial(const char* partialName);
std::vector<DWORD64> ResolveSymbolsByPattern(const char* pattern);
```

---

## 📊 功能完成度统计

| 功能模块 | 完成度 | 备注 |
|---------|--------|------|
| 传送功能 | ⚠️ 待实机验证 | UI 与命令链完整；x64 插件依赖导出符号解析，需用 `resolve_core` 实测 |
| 无敌模式 | ⚠️ 20% | 开关状态记录完成；`SetGodModeInternal` 未实际调用 `SetGod`/`SetInvincible` |
| 货币修改 | ⚠️ 60% | AddMoney/SubtractMoney 命令链完整，依赖导出符号解析，待实机验证 |
| 秒杀怪物 | ⚠️ 10% | 仅命令与半径参数框架，实体遍历与伤害接口未实现 |
| 一击必杀 | ⚠️ 10% | 仅命令框架，伤害 Hook 未实现 |
| 属性修改 | ❌ 5% | 仅命令框架，Character 属性偏移未定位 |
| 无限资源 | ❌ 0% | 待实现 |
| UI 重构 | ✅ 90% | 已完成三标签页深色主题重构（传送点管理 / 作弊工具 / 设置与调试） |
| 构建打包 | ✅ 100% | `build-release.ps1` 输出自包含 x64 包到 `dist\` |

---

## 🛠️ 下一步行动计划

### 立即可执行 (无需额外逆向)
1. ✅ 编译测试无敌模式和货币功能
2. ⏭️ 添加一键构建脚本
3. ⏭️ 更新 README.md 文档

### 需要逆向工作 (中等优先级)
1. 🔍 搜索怪物列表遍历接口
2. 🔍 解析伤害施加 API
3. 🔍 查找属性修改函数

### UI 优化 (后期)
1. 🎨 实现多标签页布局
2. 🎨 应用现代化配色方案
3. 🎨 优化交互体验

---

## 📝 注意事项

### 安全警告
⚠️ **重要提醒**:
- 本工具**仅用于本地单人模式**
- **严禁**在多人游戏中使用
- 使用可能导致存档损坏或角色异常
- 使用前请务必备份存档

### 兼容性说明
- ✅ x64 版本：支持完整功能
- ⚠️ x86 版本：仅支持传送功能
- ❌ 多人联机：不支持且禁止使用

### 游戏版本
- 当前支持：Grim Dawn 最新正式版
- 更新后可能需要重新逆向指针链

---

## 📞 联系方式与贡献

如有问题或贡献代码，请通过以下方式联系:
- GitHub Issues: [项目仓库链接]
- Email: [开发者邮箱]
- Discord: [社区频道]

---

**文档更新时间**: 2026 年 9 月 19 日  
**维护者**: Grim Dawn 工具开发团队
