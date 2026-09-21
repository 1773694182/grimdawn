# DPYes 逆向分析报告

> 分析对象：`逆向/DPYes.exe`、`逆向/DPYes.dll`
> 分析方式：纯静态分析（PE 结构解析、导入/导出表、字符串提取、RTTI/符号名提取）
> 分析日期：2026-09-19
> 说明：本报告没有运行、注入或动态调试目标程序，所有结论均来自静态特征；对存在歧义的内部引用会明确标注。

---

## 一、总体概述

DPYes 是一套面向《Grim Dawn》（恐怖黎明）的 64 位游戏内叠加层（Overlay）工具，版本字符串为 `DPYes 18p`。它由两个文件组成：

| 文件 | 角色 | 大小 | SHA256 |
|---|---|---:|---|
| `DPYes.exe` | 启动器 / 注入器 | 265,216 字节 | `909E4FC11638C056C3A741DC5813B9D2165E34684F01C320B65491F311FF0003` |
| `DPYes.dll` | 注入游戏后的主功能模块 | 2,993,664 字节 | `D92714DDC8605C74C05D35808A998B53D9892DC0E26E6039330E8B7916179D38` |

核心特征：

- 两者均为 x64 PE（`Machine=0x8664`），均未进行数字签名。
- 编译时间：EXE 为 `2026-08-11 11:08:01 UTC`，DLL 为 `2026-08-11 11:08:00 UTC`。
- 目标进程为 `x64/Grim Dawn.exe`，通过 Hook `Game.dll` 与 `Engine.dll` 实现功能。
- 界面采用 Dear ImGui + Direct3D 11 实现，仅支持 `direct3d11` 渲染器。
- Hook 采用类似 Microsoft Detours 的框架（存在 `.detourc`、`.detourd` 自定义节区）。
- 配置数据使用 `DPYes.ini`，其余数据使用 JSON（nlohmann/json 3.12.0）。
- 支持多语言本地化：`en`、`zh_CN`、`ru-RU`（Gettext `.po` 格式）。
- 前身为经典 Grim Dawn 插件 GrimInternals 的部分思路（兼容其传送列表文件）。

---

## 二、DPYes.exe 功能详解

`DPYes.exe` 是启动器和注入器，本身不包含游戏功能逻辑，职责如下。

### 2.1 启动与注入

- 以 `x64/Grim Dawn.exe` 为目标启动游戏进程。
- 将 `DPYes.dll` 注入到游戏进程，注入手段为经典的远程内存写入：
  - `OpenProcess` / `CreateProcessA` 创建或附加目标进程；
  - `VirtualAllocEx` 在目标进程分配内存；
  - `WriteProcessMemory` 写入 DLL 路径与加载代码；
  - `ResumeThread` 恢复线程执行注入逻辑。
- 注入前会检查 `IsWow64Process`，确保目标为纯 64 位进程。

**相关字符串：**

```
DPYes: running x64/Grim Dawn.exe with DPYes.dll...
Failed to start x64/Grim Dawn.exe
```

### 2.2 目录与文件校验

- 要求 `DPYes.exe` 与 `DPYes.dll` 位于 Grim Dawn 游戏根目录。
- 失败时通过 `MessageBoxA` 弹出提示。

**相关字符串：**

```
Are DPYes.exe and DPYes.dll in the root directory of Grim Dawn?
failed!
```

### 2.3 DLL 临时目录中转

- 为避免文件占用或权限问题，注入前可先把 `DPYes.dll` 复制到系统临时目录下的 `dpyes_tmpdll` 子目录。
- 使用 `CopyFileA`、`GetTempPath2W`、`CreateDirectoryW` 等 API。

**相关字符串：**

```
copying DPYes.dll to a temporary directory first
tmpdir=%ls
outPath=%s
/dpyes_tmpdll
```

### 2.4 Steam 环境设置

- 设置环境变量 `SteamAppId=219990`，这是《Grim Dawn》的 Steam 应用 ID，用于让游戏以 Steam 模式启动。

### 2.5 错误处理与诊断

- 使用 `GetLastError` / `FormatMessageA` 输出系统错误描述。
- 使用 `OutputDebugStringA` 输出调试信息。
- 提供调试器检测（`IsDebuggerPresent`）与控制器检查。

---

## 三、DPYes.dll 功能详解

`DPYes.dll` 是完整的功能实现，仅导出 1 个符号：

| 导出函数 | 说明 |
|---|---|
| `ImguiHook::GetDllState` | 供注入方获取 DLL 内部状态（ImGui Hook 状态） |

### 3.1 注入/Hook 框架

| 能力 | 说明 | 依据 |
|---|---|---|
| 函数 Hook（Detour） | 动态挂钩游戏函数并保存原始函数指针 | `HookFunction(%s, %p, %p)`、`HookFunction(%p, %p)`、`.detourc`/`.detourd` 节区 |
| 虚表 Hook | 通过替换虚表项挂钩对象方法 | `HookVFTable(%p, %p, %d)`、`Hook function not found in vtable` |
| 字节特征扫描 | 通过特征码定位游戏版本相关的函数地址 | 大量形如 `48 8B 81 ? ? ? ? ...` 的带通配符特征串 |
| 补丁应用/移除 | 直接对游戏代码打补丁并支持撤销 | `HookManager::ApplyPatch`、`HookManager::RemovePatch`、`Attempt to unpatch a patch that wasn't applied?` |
| 虚表偏移定位 | 通过 RTTI 类名与虚表定位版本差异偏移 | `Finding vtable offsets for GAME::SkillBuff/Item/Weapon`、`??_7ItemEquipment@GAME@@6BObject@1@@` |
| 符号加载 | 调用 `dbghelp.dll` 加载模块符号辅助定位 | `SymInitialize`、`SymLoadModule64`、`SymGetModuleInfo64` |
| 延迟 Hook | 部分 Hook 在游戏初始化完成后才提交 | `Performing late initialization`、`Failed to commit late hook` |
| 失败诊断 | Hook 失败时报告缺失符号并提示可能崩溃 | `Missing symbols during hook process:`、`failed to attach to Grim Dawn!` |

**Hook 的主要游戏模块：** `Game.dll`、`Engine.dll`。

**已确认被 Hook/调用的游戏内部函数（节选）：**

| 游戏函数 | 用途 |
|---|---|
| `Engine@GAME::ProcessUserInput` | 处理用户输入，用于按键/移动拦截 |
| `Engine@GAME::PreDeviceReset` / `PostDeviceReset` | 设备重置前后处理，用于 D3D11 重置兼容 |
| `Engine@GAME::Initialize` | 引擎初始化 |
| `Engine@GAME::Log` / `InternalLog` | 引擎日志捕获 |
| `Engine@GAME::GetInputDevice` | 输入设备访问 |
| `LocalizationManager@GAME::Load` / `LoadTags` | 本地化加载拦截 |
| `GraphicsMTRenderer@GAME::Render` | 渲染阶段拦截 |
| `GameEngine@GAME::InitiatePlayerTeleport` | 传送调用 |
| `GameEngine@GAME::ExitPlayingMode` / `EnableGameEngine` | 游戏模式控制 |
| `AmbianceManager@GAME::SetTime/SetTimeEnabled/GetTime/SetDebug` | 时间与昼夜控制 |
| `GAME::SetTimeScale` / `GetTimeScale` | 游戏速度控制 |
| `GameCamera@GAME::SetMovementExtents` | 相机边界控制 |
| `WorldCamera@GAME::SetCameraPitch/SetCameraYaw/SetCameraFarPlane` | 相机角度控制 |
| `Character@GAME::SetGod` / `SetInvincible` | 无敌/上帝模式（见 3.15 Cheats 说明） |
| `CombatManager@GAME::ApplyDamage` | 伤害结算拦截（DPS 统计核心） |
| `Character@GAME::SubtractLife` | 生命扣除拦截（受击统计） |
| `Item@GAME::ToggleLootBeam` | 掉落光柱控制 |
| `ControllerCharacter@GAME::SendEquipAttachAction/SendEquipDetachAction` | 装备操作（幻化功能） |
| `ControllerCharacter@GAME::SendTransmuteItemsCmd` | 幻化指令 |
| `ControllerPlayerState@GAME::DefaultRequestEvadeAction` | 闪避动作（WASD 相关） |
| `ControllerPlayerState@GAME::DefaultRequestMoveAction` | 移动动作（WASD 相关） |

### 3.2 渲染与叠加层

| 能力 | 说明 |
|---|---|
| Direct3D 11 渲染 | 仅支持 `direct3d11` 渲染器，其他渲染器会提示不支持并禁用叠加层 |
| SwapChain 初始化检测 | `ImguiHook::CheckSwapChainInit` 处理延迟初始化 |
| 设备重置处理 | 处理 D3D11 设备丢失/重置场景 |
| Dear ImGui 界面 | 内置完整 Dear ImGui（含 Demo 窗口）、`imgui_impl_win32`、`imgui_impl_dx11` |
| 着色器编译 | 使用 `D3DCompile` 动态编译着色器 |

**相关字符串：**

```
DPYes does not currently support the renderer GrimDawn is using: %s
Supported renderers:
  direct3d11
The overlay won't be rendered.
Unsupported Renderer
```

**界面主标题：** `DPYes - v18p###DPYes Config`

### 3.3 DPS 统计（核心功能）

这是 DPYes 最主要的用户功能，提供多窗口实时伤害统计。

| 统计面板 | 内容 |
|---|---|
| Dealt damage | 玩家主角色造成的伤害 |
| Incoming damage | 玩家主角色受到的伤害 |
| Pet damage | 玩家宠物造成的伤害 |
| Other damage | 其他来源伤害（不含上述类别与环境伤害） |
| Skill DPS | 按技能拆分的伤害统计 |
| Damage History | 伤害实例历史 |
| All meters | 汇总视图 |

**可配置项：**

| 配置键 | 含义 |
|---|---|
| `dpsMeterOpacity` | 面板背景不透明度 |
| `dpsMeterShowTotal` | 显示累计总量 |
| `dpsMeterSnapshotRetentionPeriod` | 快照保留时长（秒） |
| `dpsMeterSnapshotMinPeriod` | 允许进入快照的最小平均时长 |
| `dpsMeterShowBestAvg` | 显示最佳平均值快照 |
| `dpsMeterDamageTypeWidth` | 伤害类型列宽 |
| `dpsMeterUseMitigated` | 统计减伤后（实际承受）伤害 |
| `showTrueEnvironmentDamageType` | 显示真实环境伤害类型 |
| `showDamageInstanceHistory` | 显示伤害实例历史 |

**统计逻辑要点：**

- 通过 Hook `CombatManager::ApplyDamage` 拦截所有伤害结算。
- 可区分：直接伤害（`DIRECT`）与持续伤害（`DAMAGE_OVER_TIME`）。
- 可区分：玩家、玩家宠物、环境伤害、其他来源。
- 可追踪"目标实际承受的伤害"（减免后）与"减免前伤害"。
- 支持按 `GameTime`（游戏时间）与 `RealTime`（真实时间）双维度统计。
- 支持重置快照（`Reset DPS`）。
- 可将历史导出为 `dpyes_damage_history.json`（`DpsHistory::save`）。

**伤害类型覆盖（完整列表）：**

```
PhysicalPierce, Physical, PierceRatio, Pierce, Cold, Fire, Acid, Lightning,
Vitality, Chaos, Aether, ManaBurn, Disruption, PercentCurrentLife, Bleeding,
TotalSpeed, AttackSpeed, SpellCastSpeed, RunSpeed, LifeLeech, ManaLeech,
OffensiveAbility, DefensiveAbility, OffensiveReduction, DefensiveReduction,
Fumble, ProjectileFumble, TotalDamageReductionPercent, TotalDamageReductionAbsolute,
PhysicalDamageReductionPercent, ElementalDamageReductionPercent,
TotalResistanceReductionPercent, TotalResistanceReductionAbsolute,
PhysicalResistanceReductionPercent, PhysicalResistanceReductionAbsolute,
ElementalResistanceReductionPercent, ElementalResistanceReductionAbsolute,
AbsorptionProtection, Absorption, Protection, ArmorRating, Stun, Sleep, Trap,
Freeze, Petrify, Immobilize, Knockdown, TakeHit, Taunt, Convert, Fear, Confusion,
BlockModifier, BlockAmountModifier, Reflect, ElementalGroup, CritDamageModifier,
TotalDamageModifier, Unknown, ManaBurnRatio, Retaliation
```

### 3.4 自动拾取（Auto Loot）

| 能力 | 说明 |
|---|---|
| 开关 | `autoLootEnabled` |
| 拾取半径 | `autoLootRadius` |
| 单次拾取模式 | `autoLootOneShot` |
| 拾取类别 | 文本/传说物品（`autoLootLoreItems`）、蓝图（`autoLootBlueprints`）、遗物/组件（`autoLootRelics`）、任务物品（`autoLootQuestItems`）、稀有物品（`autoLootRares`） |
| 与掉落过滤联动 | 启用掉落过滤时仅拾取通过过滤的物品（`Use loot filter`） |

**UI 文案：** `Enable autoloot`、`Automatically loot:`、`Lore items`、`Components/relics`、`Iron bits, etc`、`Quest items`、`Blueprints`、`Rare items`

> 注：`Iron bits, etc` 表示把铁币等货币类掉落纳入拾取类别，属于"拾取货币"而不是"修改货币数量"。

### 3.5 掉落过滤器（Loot Filter）

| 能力 | 说明 |
|---|---|
| 规则列表 | 每条规则可指定稀有度、物品类型、前后缀 |
| 显隐动作 | `Hide` / `Show` / `Always show` |
| 稀有度选项 | `Any rarity` 等 |
| 类型选项 | `Any type`、`%d item types` |
| 词缀匹配 | 从游戏数据库读取词缀表（`Loaded %d affixes`、`%zd affixes`） |
| 规则排序 | 自下而上处理，支持拖拽排序（`Drag to re-order entries`） |
| 战斗中隐藏 | 战斗中隐藏掉落，可配置检测模式与残留时间 |
| 持久化 | `DPYes_lootfilter.json`（`LootFilter::RuleList::save/load`） |

**战斗隐藏相关配置：**

| 配置键 | 含义 |
|---|---|
| `lootFilterHideItemsInCombat` | 战斗中隐藏物品 |
| `lootFilterHideItemsInCombatMode` | 战斗检测模式（角色状态 / 战斗管理器 / 两者） |
| `lootFilterHideItemsInCombatResidualTime` | 战斗结束后继续隐藏的残留时间 |

**战斗中显示临时解除：** 短按"显示物品（无过滤）"键（默认 `Alt`）可在 10 秒内临时显示被隐藏物品。

### 3.6 物品显示与信息增强

| 能力 | 说明 | 配置键 |
|---|---|---|
| 显示掉落物品 | 地面上显示物品信息 | `showDroppedItems` |
| 显示物品 DBR | 显示物品的数据库记录路径 | `showItemRecord` |
| 记录样式 | 物品 DBR 的显示样式 | `itemRecordStyle` |
| 标记缺失幻化 | 未解锁幻化的物品前缀 `*` | `showItemMissingIllusion` |
| 掉落光柱控制 | 控制物品掉落光柱（`ToggleLootBeam`） | - |

**相关字符串：**

```
Items you're missing the illusion for will be prefixed with an asterisk (*)
Show item dbr
```

### 3.7 幻化与外观功能

| 能力 | 说明 | 配置键 |
|---|---|---|
| 装备时转移幻化 | 从背包右键装备时，自动把幻化转移到新装备 | `autoSwapIllusions` |
| 幻化兼容性检查 | 检查两件物品是否可交换幻化 | `Both items weren't transmute compatible :(` |
| 幻化交换执行 | 交换两件物品的幻化外观 | `Will swap transmutes on %d and %d` |
| 游戏接口 | 读取/设置物品幻化（`GetTransmute`/`AddTransmute`/`RemoveTransmute`/`HasTransmute`/`IsTransmuteCompatible`） | - |
| 外观匹配 | 检查物品是否拥有匹配外观（`HasMatchingAppearance`） | - |

**UI 文案：** `Transfer illusions on equip`、`Transfer illusions to newly equipped gear.`、`Only works when equipping to slot from inventory via right click (or equivalent)`、`Mark missing illusions`

### 3.8 传送系统（Teleport）

| 能力 | 说明 | 配置键 |
|---|---|---|
| 传送列表 | 从 `GrimInternals_TeleportList.txt` 读取传送点 | - |
| 列表管理界面 | 新建、编辑、删除、重载、搜索传送点 | - |
| 双击传送 | 需要双击才执行传送 | `teleportRequireDoubleClick` |
| 传送后关闭 | 传送后自动关闭窗口 | `teleportDismissOnTeleport` |
| 受限区域传送 | 允许在受限区域创建/保存传送点 | `alwaysAllowTeleportSaving` |
| 传送执行 | 调用 `GameEngine::InitiatePlayerTeleport` | - |
| 区域查询 | 通过 `ZoneManager` 获取区域名与区域列表 | - |
| 原子替换列表文件 | 使用 `ReplaceFileA` 与临时文件 `.swp_dpyes` 安全写回 | - |

**UI 文案：** `Teleport`、`New teleport location`、`Edit teleport location`、`Name`、`Update`、`Edit`、`Delete (no confirm)`、`Add location`、`Reload list`、`Search...`、`No results.`、`Entries are loaded from %s`

**错误处理：**

```
Failed to open teleport list: %s
Failed to parse teleport list entry: %s
Failed to find item referenced by TeleportIndex
Failure while swapping teleport list %s with %s: %d
Exceeded depth limit of TeleportIndex!
No teleport entries loaded!
```

### 3.9 GrimCam 相机控制

DPYes 内置了 GrimCam 风格的相机控制（原版需要一个独立 `GrimCam.dll`，缺失时会提示）。

| 能力 | 配置键 |
|---|---|
| 相机启用 | `enabled` |
| 俯仰角范围 | `MinPitch` / `MaxPitch` |
| 视野角 | `FOV` |
| 远裁剪面 | `FarClip` |
| 水平/垂直灵敏度 | `HSensitivity` / `VSensitivity` |
| 距离范围 | `MinDistance` / `MaxDistance` |
| 深度雾限制 | `DepthFogClamp` |
| 目标偏移 | `TargetOffsetX` / `TargetOffsetY` / `TargetOffsetZ` |
| 独家俯仰控制 | `ExclusivePitch`（阻止游戏改变俯仰角） |
| 独家偏航控制 | `ExclusiveYaw`（阻止游戏改变偏航角） |
| 热键 | `Key` |
| 手柄支持 | `JoystickEnabled` / `JoystickHSensitivity` / `JoystickVSensitivity` / `JoystickZoomInsteadOfPitch` |
| Steam 手柄支持 | `SteamControllerEnabled` / `SteamControllerHSensitivity` / `SteamControllerVSensitivity` / `SteamControllerZoomInsteadOfPitch` |

**相关说明文案：**

```
Prevent Grim Dawn from changing the main camera's pitch (up/down) angle
Prevent Grim Dawn from changing the main camera's yaw (left/right) angle
Disable this to control camera rotation using Grim Dawn's keybinds
When this is disabled, moving the camera using GrimCam can cause wild camera rotation!
GrimCam.dll missing!
```

**相机调试：** `Camera Debug`、`CameraInfo`

### 3.10 虔诚树缩放（Devotion Tree Zoom）

| 能力 | 配置键 | 默认值 |
|---|---|---|
| 缩小上限 | `devotionZoomMin` | 0.5 |
| 放大上限 | `devotionZoomMax` | 1 |

**说明：** 通过字节特征动态定位缩放限制地址；失败时提示 `Error: DPYes failed to dynamically locate the necessary addresses for devotion zoom.`

**UI 文案：** `How far you can zoom out the devotion tree (default: 0.5)`、`How far you can zoom in the devotion tree (default: 1)`

### 3.11 游戏速度覆盖

| 能力 | 配置键 |
|---|---|
| 游戏速度开关 | `timeScaleOverrideEnabled` |
| 速度倍率 | `timeScaleOverride`（`Scale`） |

**实现：** Hook `GAME::SetTimeScale` / `GAME::GetTimeScale`。

### 3.12 昼夜与环境时间控制

| 能力 | 配置键 |
|---|---|
| 覆盖昼夜循环 | `timeOfDayOverrideEnabled` |
| 时间点设置 | `timeOfDayOverride` |
| 调试模式 | `AmbianceManager debug` |

**实现：** 调用 `AmbianceManager::SetTime` / `SetTimeEnabled` / `GetTime` / `GetEnvironmentEffects` / `SetDebug`。

**提示文案：** `If these options don't work, make sure the day/night cycle is also enabled in the game settings!`

### 3.13 WASD 移动（Alpha 功能）

| 能力 | 配置键 |
|---|---|
| 启用 WASD 移动 | `wasdMovementEnabled` |
| 闪避到鼠标位置 | `wasdEvadeToCursor` |

**方向键位配置：** `Forward`、`Left`、`Backward`、`Right`

**实现：** Hook `ControllerPlayerState::DefaultRequestMoveAction` / `DefaultRequestEvadeAction`。

**提示文案：**

```
WASD Movement (Alpha)
See Grim Dawn's native WASD movement controls in the options instead!
```

### 3.14 OA/DA 与击杀统计

| 能力 | 说明 | 配置键 |
|---|---|---|
| OA/DA 显示 | 显示玩家进攻能力/防御能力及命中/暴击概率 | `showOADAStats` |
| 更新频率 | OA/DA 统计刷新间隔 | `oadaStatUpdateFrequency` |
| 击杀敌人统计 | 显示已击杀敌人列表/数量 | `showKilledEnemies` |

**OA/DA 格式：**

```
OA: %5.f% (PTH /PTC : %5.1f%%/%5.1f%%)
DA: %5.f% (PTBH/PTBC: %5.1f%%/%5.1f%%)
```

### 3.15 Cheats 分类

DLL 中存在名为 `Cheats` 的配置分区，并明确提示：

```
When disabled, any active cheats will remain until restart
```

当前从 UI/配置层可确认的 Cheats 项：

| 项目 | 说明 | 配置键 |
|---|---|---|
| Ranged retaliation | 远程反击（远程武器触发反击效果） | `rangedRetaliation` |

**另有大量游戏内部函数引用，可能是 Cheats 或调试功能的实现基础：**

| 内部函数 | 潜在能力 | 确定性 |
|---|---|---|
| `Character::SetGod` | 上帝模式（免疫伤害） | 不能确认是否对用户开放 |
| `Character::SetInvincible` | 无敌 | 不能确认是否对用户开放 |
| `GameEngine::CreateItemCopy` | 在指定坐标创建物品 | 不能确认是否对用户开放 |
| `ControllerAI::SetCausesAnger` | 设置 AI 是否被激怒 | 不能确认是否对用户开放 |
| `Character::CompleteInventoryRelics` | 补全遗物 | 不能确认是否对用户开放 |
| `Item::IsPickupOk` / `ControllerCharacter::PickupItem` | 强制拾取判定 | 自动拾取使用 |
| `Item::PassLootFilter` | 绕过/通过掉落过滤 | 掉落过滤使用 |
| `Monster::DropIfNotBroken` / `SendDropItemRandom` | 掉落控制 | 不能确认是否对用户开放 |

> 重要说明：上述"不能确认"项仅是从字符串中提取到的游戏内部函数名。它们可能用于读取状态、也可能是 Hook 目标或调试残留，静态分析无法区分其是否被实际调用以及是否暴露给最终用户。

### 3.16 怪物信息显示（Enemy Info）

| 能力 | 说明 | 配置键 |
|---|---|---|
| 显示开关 | 显示怪物分类信息 | `monsterInfoNameShow` |
| 最低显示等级 | 怪物分级阈值 | `monsterInfoMinClass` |
| 字号 | 显示字号 | `monsterInfoNameSize` |
| 字体样式 | 显示样式 | `monsterInfoNameStyle` |

**怪物分级文案：** `Common`、`Champion`、`Hero`、`Boss`、`Quest`

**实现接口：** `Monster::GetMonsterClassification`、`Actor::GetDescriptionTag`、`GetObjectName`。

### 3.17 字体系统

| 能力 | 说明 | 配置键 |
|---|---|---|
| 字体选择 | 从系统字体中选择 | `font` |
| 字号 | 字体大小 | `fontSize` |
| 描边粗细 | 文字描边 | `strokeThickness` |
| 抗锯齿提示 | 内置多种 hinting 选项 | `NoHinting` / `NoAutoHint` / `ForceAutoHint` / `LightHinting` / `MonoHinting` |
| 样式 | 粗体/斜体/单色 | `Bold` / `Oblique` / `Monochrome` |

**内置候选字体（含中文字体）：**

```
c:\Windows\Fonts\cour.ttf      c:\Windows\Fonts\consola.ttf
c:\Windows\Fonts\Arial.ttf     c:\Windows\Fonts\Calibri.ttf
c:\Windows\Fonts\TAHOMA.TTF    c:\Windows\Fonts\MEIRYO.TTC
c:\Windows\Fonts\SIMSUN.TTC    c:\Windows\Fonts\segoeui.ttf
c:\Windows\Fonts\MINGLIU.TTC   c:\Windows\Fonts\GULIM.TTC
c:\Windows\Fonts\MSGOTHIC.TTC  c:\Windows\Fonts\MSJH.TTC
c:\Windows\Fonts\YUGOTHM.TTC   c:\Windows\Fonts\SEGUISYM.TTC
```

### 3.18 本地化系统

| 能力 | 说明 |
|---|---|
| 语言选择 | `language` / `languageType` |
| 语言文件目录 | `DPYes/locales` |
| 已内置语言 | `locales/en.po`、`locales/zh_CN.po`、`locales/ru-RU.po` |
| 导出模板 | `locales/template.pot` |
| 导出语言文件 | `Export language files`（会覆盖 `DPYes/locales` 中已有文件） |
| 重载语言 | `Reload language files` |
| 游戏文本本地化 | Hook `LocalizationManager::Load` / `LoadTags`，支持读取游戏标签、去掉颜色标记、性别化文本等 |

**相关字符串：**

```
Loaded localisation with %d messages type=%d name=%s path=%s
Locales directory not found: %s
Only necessary if you wish to modify an existing translation file or as a template for a new translation
Will overwrite existing files in DPYes/locales
```

### 3.19 配置系统

| 能力 | 说明 |
|---|---|
| 主配置文件 | `DPYes.ini` |
| 配置分区 | `DPYesConfig`、`GrimCam`、`Hotkeys`、`Font`、`Cheats`、`Loot`、`Advanced` 等 |
| 写入方式 | 原子替换（`ReplaceFileA`），避免配置损坏 |
| 版本记录 | `last_version=%s`，用于升级提示 |

**完整配置键列表（从 DLL 提取）：**

```
dpsMeterOpacity=%f                dpsMeterShowTotal=%d
dpsMeterSnapshotRetentionPeriod=%f   dpsMeterDamageTypeWidth=%f
dpsMeterShowBestAvg=%d            showTrueEnvironmentDamageType=%d
timeScaleOverride=%f              dpsMeterSnapshotMinPeriod=%f
dpsMeterUseMitigated=%d           timeOfDayOverrideEnabled=%d
altMenuKeyEnabled=%d              timeScaleOverrideEnabled=%d
timeOfDayOverride=%f              autoLootRadius=%f
autoLootEnabled=%d                altMenuDisableDefault=%d
altMenuKey=%d                     autoLootOneShot=%d
autoLootLoreItems=%d              autoLootBlueprints=%d
autoLootRelics=%d                 lootFilterEnabled=%d
lootFilterHideItemsInCombat=%d    autoLootQuestItems=%d
autoLootRares=%d                  autoSwapIllusions=%d
showItemMissingIllusion=%d        lootFilterHideItemsInCombatMode=%d
lootFilterHideItemsInCombatResidualTime=%f    showKilledEnemies=%d
showDroppedItems=%d               showItemRecord=%d
itemRecordStyle=%d                monsterInfoNameShow=%d
monsterInfoMinClass=%d            showOADAStats=%d
oadaStatUpdateFrequency=%d        wasdMovementEnabled=%d
wasdEvadeToCursor=%d              monsterInfoNameSize=%d
monsterInfoNameStyle=%d           languageType=%d
language=%s                       teleportRequireDoubleClick=%d
teleportDismissOnTeleport=%d      devotionZoomMax=%f
last_version=%s                   devotionZoomMin=%f
font=%s                           fontSize=%d
strokeThickness=%f                validFonts=%d
MaxPitch=%f                       FOV=%f
MinPitch=%f                       VSensitivity=%f
MinDistance=%f                    FarClip=%f
HSensitivity=%f                   TargetOffsetX=%f
TargetOffsetY=%f                  MaxDistance=%f
DepthFogClamp=%f                  JoystickVSensitivity=%f
JoystickZoomInsteadOfPitch=%d     TargetOffsetZ=%f
JoystickHSensitivity=%f           SteamControllerVSensitivity=%f
SteamControllerZoomInsteadOfPitch=%d   JoystickEnabled=%d
SteamControllerHSensitivity=%f    ExclusivePitch=%d
SteamControllerEnabled=%d         ExclusiveYaw=%d
rangedRetaliation=%d              alwaysAllowTeleportSaving=%d
```

### 3.20 热键系统

| 能力 | 说明 |
|---|---|
| 默认配置菜单键 | `F5` |
| 禁用默认键 | `Disable default hotkey (F5)` |
| 替代菜单键 | `Enable alternate config hotkey` + `altMenuKey`（虚拟键码） |
| 按键重映射 | `Remap w/ Ctrl+Shift: click anywhere to select new mouse button.`（支持鼠标侧键等） |
| 键位名称解析 | `GetKeyNameTextA`、`GetKeyboardLayout` |
| API | `SetHotkey(%d, %d)`、`ResetHotkey(%d)` |

### 3.21 引擎日志捕获

| 能力 | 说明 |
|---|---|
| 捕获开关 | `Capture engine logs` |
| 实现 | Hook `Engine::Log` / `Engine::InternalLog` |
| 日志格式 | `Engine log: (%d, %d): %s` |

### 3.22 调试与开发功能

| 功能 | 说明 | 配置/字符串 |
|---|---|---|
| 测试窗口 | 显示指针、坐标等内部状态 | `Show test window`、`Test Window` |
| 玩家坐标 | 显示玩家世界坐标 | `PlayerCoords: %.2f, %.2f, %.2f` |
| 内部指针 | 显示引擎与对象指针 | `pGameEngine`、`pEngine`、`pObjectManager`、`MainPlayer`、`DatabaseArchive` |
| 怪物命中/暴击概率 | 显示当前目标的命中/暴击计算 | `lastMonsterChanceToBeCrit`、`lastMonsterChanceToBeHit`、`lastMonsterChanceToHit`、`lastMonsterChanceToCrit` |
| 导航调试 | 显示导航网格数据 | `NavManager::Debug`、`VisualizeNavData`、`IsPointOnPathMesh`、`FindClosestPointOnPathMesh` |
| 调试渲染 | 包围盒、命中盒、球体、3D 文本 | `RenderBoundingBox`、`RenderHitBoxes`、`RenderSphere`、`RenderText`、`RenderText3d` |
| 角色状态 | 显示动作状态、AI 执行状态 | `GetActionStateAsText`、`ExecutingStateName`、`InputMode` |
| 战斗状态 | 显示战斗管理器状态、是否中毒等 | `GetCombatState`、`IsPoisoned`、`GetDamageData` |
| ImGui Demo | 内置完整 ImGui 演示窗口 | `Show Dear ImGui Demo` |

### 3.23 运行环境检查

| 能力 | 说明 |
|---|---|
| MSVC Runtime 检查 | 检查所需 VC++ 运行库版本 |
| 自动提示下载 | 版本过旧时提供微软官方下载地址 |
| 启动欢迎提示 | 首次启动提示 F5 快捷键与 DPS 面板拖动说明 |

**相关字符串：**

```
DPYes compiled with MSC %d
Failed to check MSVC Runtime Version
Failed to check MSVC Runtime Build
The currently installed MSVC redistributable may be too old!
https://aka.ms/vs/17/release/vc_redist.x64.exe
Welcome to DPYes version %s!
The hotkey to open the config menu is F5
The position of each DPS overlay can be adjusted either in the config menu or by dragging it while the config menu is open.
```

### 3.24 游戏版本适配

| 能力 | 说明 |
|---|---|
| Steam 版识别 | `Looks like the steam version of the game?`、`Game version: Steam` |
| GOG/非 Steam 版识别 | `GOG game version?`、`Game version: Non-steam` |
| DLC 检测 | 通过 `Expansion1FilesExist` / `Expansion2FilesExist` / `Expansion3FilesExist` 等检测资料片，含生存模式（`HasSurvivalDLC`） |
| 数据库加载 | 支持主数据库、资料片数据库、自定义地图数据库、Mod 数据库 |
| Mod 支持 | `GetModBaseFolder`、`SetModName`、`GetModName` |

### 3.25 数值与战斗信息读取（支撑功能）

DLL 中引用了大量游戏内部读取接口，用于支撑上述功能：

| 领域 | 典型接口 |
|---|---|
| 玩家/角色 | `GetMainPlayer`、`GetPlayerInfo`、`GetPlayStats`、`GetInventoryItems`、`GetSkillList`、`GetActiveSkillList` |
| 战斗 | `GetCombatEnemy`、`GetAttackerId`、`GetMasterAttacker`、`DesignerCalculateOffensiveAbility`、`DesignerCalculateDefensiveAbility`、`DesignerCalculateCriticalChance`、`DesignerCalculateProbabilityToHit` |
| 物品 | `GetItemType`、`GetItemClassification`、`GetPrefixClassification`、`GetSuffixClassification`、`GetItemCost`、`GetItemReplicaInfo`、`IsOfInterest`、`IncludeInMinimap` |
| 世界/区域 | `GetRegion`、`GetRegionContainingPoint`、`GetRegionById`、`GetRegionName`、`GetDistance`、`GetEntitiesInSphere`、`GetEntitiesInFrustum` |
| 技能 | `GetSkillProfile`、`GetActiveAuraName`、`TrackableRemainingTime`、`TrackableTotalTime` |
| 词缀/掉落 | `LootRandomizerTable`、`AttributeRange`、`LoadAffix`、`CreateText` |
| 数据库 | `ObjectManager::CreateObjectFromFile`、`LoadTableFile`、`GetLoadTable`、`TableDepot::LoadFile` |

---

## 四、文件与数据清单

| 路径 | 类型 | 用途 |
|---|---|---|
| `DPYes.ini` | 配置 | 主配置文件 |
| `DPYes_lootfilter.json` | 数据 | 掉落过滤规则 |
| `dpyes_damage_history.json` | 数据 | 伤害历史导出 |
| `GrimInternals_TeleportList.txt` | 数据 | 传送点列表（兼容 GrimInternals 格式） |
| `.swp_dpyes` | 临时 | 传送列表原子写回临时文件 |
| `DPYes/locales/` | 目录 | 本地化文件目录 |
| `locales/en.po` | 语言 | 英文本地化 |
| `locales/zh_CN.po` | 语言 | 简体中文本地化 |
| `locales/ru-RU.po` | 语言 | 俄文本地化 |
| `locales/template.pot` | 模板 | 新语言翻译模板 |
| `dpyes_tmpdll` | 目录 | EXE 注入前复制 DLL 的临时目录 |

---

## 五、技术架构

| 层面 | 技术 |
|---|---|
| 注入方式 | CreateProcess + VirtualAllocEx + WriteProcessMemory + 远程线程 |
| Hook 框架 | Detours 风格（`.detourc` / `.detourd` 节区），支持函数 Hook、虚表 Hook、字节补丁 |
| 版本适配 | 字节特征扫描 + RTTI 类名/虚表偏移定位 + dbghelp 符号 |
| 渲染 | Direct3D 11 + Dear ImGui（含 win32 与 dx11 后端） |
| 输入 | Win32 消息 + XInput + Steam Input |
| 配置 | INI 文件（自研解析） |
| 数据序列化 | nlohmann/json 3.12.0 |
| 文本编码 | UTF-8 / UTF-16 转换，支持中日韩字体与 Gettext 本地化 |
| 编译产物 | MSVC x64，无签名 |
| 依赖库 | dbghelp.dll、D3DCOMPILER_47.dll、SHLWAPI.dll、IMM32.dll（输入法支持） |

**支持的渲染器：** 仅 `direct3d11`。

**兼容的游戏版本：** Steam 与 GOG/非 Steam 版本，支持资料片检测与 Mod。

---

## 六、功能汇总表

| 序号 | 功能模块 | 是否确认对用户开放 | 说明 |
|---:|---|---|---|
| 1 | 启动并注入游戏 | 是 | EXE 职责 |
| 2 | DLL 临时目录中转注入 | 是 | EXE 职责 |
| 3 | Steam AppId 环境设置 | 是 | EXE 职责 |
| 4 | 游戏内 ImGui 叠加层 | 是 | 仅 D3D11 |
| 5 | F5 配置菜单 | 是 | 可改键 |
| 6 | 玩家输出 DPS 统计 | 是 | - |
| 7 | 玩家受到伤害统计 | 是 | - |
| 8 | 宠物伤害统计 | 是 | - |
| 9 | 其他来源伤害统计 | 是 | - |
| 10 | 按技能 DPS 统计 | 是 | - |
| 11 | 伤害实例历史 | 是 | - |
| 12 | 伤害历史 JSON 导出 | 是 | - |
| 13 | 减伤前后伤害区分 | 是 | `dpsMeterUseMitigated` |
| 14 | 环境伤害类型显示 | 是 | - |
| 15 | 自动拾取 | 是 | 可配半径与类别 |
| 16 | 货币类掉落拾取 | 是 | `Iron bits, etc` |
| 17 | 掉落过滤器 | 是 | JSON 规则 |
| 18 | 战斗中隐藏掉落 | 是 | Alt 临时解除 |
| 19 | 掉落物信息显示 | 是 | - |
| 20 | 物品 DBR 显示 | 是 | - |
| 21 | 缺失幻化标记 | 是 | - |
| 22 | 装备时转移幻化 | 是 | - |
| 23 | 传送列表管理 | 是 | 兼容 GrimInternals |
| 24 | 受限区域传送点保存 | 是 | - |
| 25 | GrimCam 相机控制 | 是 | 含手柄支持 |
| 26 | 虔诚树缩放调整 | 是 | - |
| 27 | 游戏速度覆盖 | 是 | - |
| 28 | 昼夜循环覆盖 | 是 | - |
| 29 | WASD 移动（Alpha） | 是 | - |
| 30 | 闪避到鼠标位置 | 是 | - |
| 31 | OA/DA 统计显示 | 是 | - |
| 32 | 击杀敌人统计 | 是 | - |
| 33 | 怪物分类信息显示 | 是 | - |
| 34 | 远程反击（Cheats） | 是 | `rangedRetaliation` |
| 35 | 字体选择与字号 | 是 | 含中文字体 |
| 36 | 多语言支持 | 是 | 中/英/俄 |
| 37 | 语言文件导出/重载 | 是 | - |
| 38 | 引擎日志捕获 | 是 | - |
| 39 | 游戏版本识别 | 是 | Steam/GOG |
| 40 | MSVC 运行库检查 | 是 | 含下载链接 |
| 41 | 调试/测试窗口 | 是 | - |
| 42 | 伤害实例历史窗口 | 是 | - |
| 43 | ImGui Demo 窗口 | 是 | - |
| 44 | 上帝模式 | 不确定 | 仅见 `SetGod` 符号 |
| 45 | 无敌 | 不确定 | 仅见 `SetInvincible` 符号 |
| 46 | 创建物品 | 不确定 | 仅见 `CreateItemCopy` 符号 |
| 47 | AI 怒气控制 | 不确定 | 仅见 `SetCausesAnger` 符号 |
| 48 | 补全遗物 | 不确定 | 仅见 `CompleteInventoryRelics` 符号 |
| 49 | 直接修改货币数量 | 未发现 | 无 SetGold/AddMoney 等符号 |
| 50 | 修改角色属性数值 | 未发现 | 仅有读取/计算类接口 |

---

## 七、关于货币功能的专项结论

针对"是否有控制金币、银币、铁币等货币数量的功能"：

**结论：没有发现修改货币数量的功能。**

依据：

1. 货币相关字符串只有 `Iron bits, etc`（自动拾取类别）和 `currency`（分类名）。
2. 没有 `SetGold`、`AddGold`、`SetMoney`、`AddMoney`、`SetCurrency`、`AddCurrency`、`SetIronBits` 等写入型符号。
3. 与金钱相关的只有读取类接口：`GetItemCost`（读取物品价值）。
4. `money_get` / `money_put` / `moneypunct` 属于 C++ 标准库本地化符号，与游戏金钱无关。
5. 存在的相关能力仅为：把货币类掉落纳入自动拾取范围、以及掉落过滤规则匹配。

---

## 八、风险与局限性说明

### 8.1 风险特征

- 未签名二进制，编译时间被设置为 2026 年（晚于常规版本发布时间，可能被人为修改）。
- 具备完整的进程注入与内存补丁能力。
- Hook 游戏核心模块 `Game.dll`、`Engine.dll`。
- 存在直接读写游戏内存、修改代码段的 API 调用。
- 存在大量游戏内部高权限函数引用，其中部分（`SetGod`、`SetInvincible`、`CreateItemCopy`）可能对应作弊能力。

### 8.2 分析局限性

- 本报告为纯静态分析，未执行任何动态验证。
- 字符串中出现的符号名不等于实际被调用的功能，部分可能是 Hook 目标、状态读取或调试残留。
- 未对代码段进行反汇编级别的数据流分析，无法确认某些内部功能的实际调用条件。
- 无法确认网络行为（字符串中未发现 HTTP/套接字相关功能，导入表中也无网络 API）。
- 未能确认 `.rsrc` 资源中的图标/清单具体内容。

---

*报告结束*
