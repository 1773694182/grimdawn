# API 查找工作总结

## ✅ 已完成的工作

### 1. 代码实现

我已经扩展了插件的诊断功能，添加了以下新特性:

#### A. 扩展的符号探测列表 (`ResolveCoreSymbols`)

新增了 8 个秒杀怪物和一击必杀相关的 API 探测:

```cpp
// 伤害相关
{ "ApplyDamage", "?ApplyDamage@Character@GAME@@QEAAXPEAV3_MI@Z" }
{ "TakeDamage", "?TakeDamage@Character@GAME@@MAEXM_N@Z" }
{ "DesignerCalculateOffensiveAbility", "?DesignerCalculateOffensiveAbility@Designer@GAME@@YA?AMPAVCharacter@2_MPAPAE0_N@Z" }

// 实体系统 (可能名称不同)
{ "GetEntityCount", "?GetEntityCount@EntitySystem@GAME@@QEBAIXZ" }
{ "GetEntity", "?GetEntity@EntitySystem@GAME@@QEBAPEAVEntity@2_H@Z" }
{ "IsMonster", "?IsMonster@Entity@GAME@@QEB_NXZ" }
{ "GetEntityType", "?GetEntityType@Entity@GAME@@QEBA?AW4EntityType@2@XZ" }
```

#### B. 编译状态

✅ **编译成功** - 所有警告均为未使用参数 (占位实现预期行为)

---

## 📋 如何查找 API

### 方法 1: 自动探测 (推荐首先尝试)

**命令**: `resolve_core`

**作用**: 通过游戏导出表查找所有已导出的函数

**步骤**:
1. 启动 Grim Dawn (x64)
2. 启动 GrimDawnTeleporter.exe
3. 检测进程
4. 发送 `resolve_core` 命令
5. 查看返回的 JSON 结果

**结果解读**:
- ✅ 非零地址 = API 存在且可用
- ❌ `0x0` = API 未通过导出表暴露

### 方法 2: 字符串搜索

**命令**: `diagnose_symbols`

**作用**: 在 Game.dll 中搜索字符串引用

**步骤**:
1. 发送 `diagnose_symbols` 命令
2. 查看哪些字符串有引用 (vaRefs > 0 或 ripRefs > 0)
3. 记录第一个 RIP 引用地址用于进一步分析

### 方法 3: 特征码扫描

**命令**: `scan_pattern:Game.dll|<hex>`

**作用**: 扫描特定的字节模式

**步骤**:
1. 使用 Cheat Engine 或其他工具找到特征码
2. 发送扫描命令
3. 验证特征码是否匹配

### 方法 4: IDA Pro 手动分析 (最可靠)

**步骤**:
1. 打开 IDA Pro
2. 加载 Grim Dawn.exe 或 Game.dll
3. 按 F12 打开字符串窗口
4. 搜索关键词:
   - EntitySystem
   - ApplyDamage
   - GetEntity
   - IsMonster
   - PlayerManagerClient
5. 右键 → Follow to → Cross-references
6. 记录函数地址和完整的 mangled name

---

## 🔍 关键发现

### 已确认存在的 API (从现有代码推断)

✅ 这些 API 已经在使用中:
- `gGameEngine@GAME@@3PEAVGameEngine@1@EA`
- `GetMainPlayer@GameEngine@GAME@@QEBAPEAVCharacter@2@XZ`
- `GetCurrentMoney@Character@GAME@@QEBA?BIXZ`
- `AddMoney@Character@GAME@@QEAAXI@Z`
- `SubtractMoney@Character@GAME@@QEAA?BII@Z`

### 待确认的 API

⚠️ 需要通过上述方法查找:

#### P0 - 核心 API (必须找到)

1. **GetPlayerManagerClient** - 获取玩家管理器
2. **GetPlayerId** - 获取玩家 ID
3. **GetPlayerLocation** - 获取玩家位置
4. **ApplyDamage** - 施加伤害

#### P1 - 辅助 API (重要)

5. **GetEntityCount** - 获取实体总数
6. **GetEntity** - 根据索引获取实体
7. **IsMonster** - 判断是否为怪物

---

## 💡 如果找不到怎么办？

### 场景 1: 导出表中没有

**原因**: C++ 虚函数通常不通过导出表暴露

**解决方案**:
- 使用字符串搜索 (`diagnose_symbols`)
- 使用 Pattern Scanner
- 手动逆向 (IDA Pro)

### 场景 2: 字符串也不存在

**原因**: 
- 名称可能被优化
- 使用了不同的命名约定
- 在另一个模块中

**解决方案**:
- 扩大搜索范围 (Engine.dll, Core.dll 等)
- 搜索部分字符串
- 查看相邻函数的调用模式

### 场景 3: 完全找不到

**原因**: 
- 函数名被混淆
- 使用了 vtable 间接调用
- 需要动态生成

**解决方案**:
- 参考 DPYes 项目
- 查看社区资源
- 进行深度逆向分析

---

## 🎯 下一步行动清单

### 立即执行

- [ ] 运行 `resolve_core` 并记录结果
- [ ] 检查哪些 API 返回非零地址
- [ ] 对返回 0x0 的 API 使用其他方法

### 短期目标 (1-2 天)

- [ ] 完成所有 P0 API 的查找
- [ ] 至少找到 3 个 P0 API 的地址
- [ ] 更新代码中的地址

### 中期目标 (3-5 天)

- [ ] 实现基础的实体遍历
- [ ] 实现简单的距离计算
- [ ] 测试最小可行版本

### 长期目标 (1-2 周)

- [ ] 完整实现秒杀怪物功能
- [ ] 完整实现一击必杀功能
- [ ] 添加配置选项
- [ ] 性能优化

---

## 📊 技术细节

### 推测的函数签名

```cpp
// PlayerManagerClient
void* GetPlayerManagerClient(void* gameEngine);

// PlayerId
unsigned int GetPlayerId(void* playerManager);

// PlayerLocation  
void GetPlayerLocation(void* playerManager, void* out_location, unsigned int playerId);

// ApplyDamage
void ApplyDamage(
    void* target,           // Monster pointer
    void* attacker,         // Player pointer
    float damageAmount,     // Max damage (e.g., 999999.0f)
    unsigned int damageType // Damage type enum
);

// Entity System
int GetEntityCount(void* entitySystem);
void* GetEntity(void* entitySystem, int index);
bool IsMonster(void* entity);
```

### 可能的类结构

```cpp
struct Character {
    float health;          // 偏移量待确定
    float maxHealth;       // 偏移量待确定
    unsigned int strength; // 偏移量待确定
    unsigned int agility;  // 偏移量待确定
    // ...
};

struct Entity {
    Vec3 position;
    EntityType type;
    // ...
};

struct EntitySystem {
    // Internal data structures
};
```

---

## 🛠️ 工具推荐

### 必备工具

1. **IDA Pro** - 静态分析
2. **Cheat Engine** - 动态调试和特征码提取
3. **x64dbg** - 调试器
4. **Ghidra** - 免费逆向工具

### 辅助工具

1. **Dependencies** - 查看 DLL 依赖和导出
2. **DLL Export Viewer** - 浏览导出表
3. **Process Hacker** - 进程和内存管理

---

## 📚 参考资料

- [API 查找指南](docs/API-FINDING-GUIDE.md) - 详细步骤
- [秒杀怪物实现指南](docs/insta-kill-implementation.md) - 完整技术方案
- [DPYes 逆向分析报告](../逆向/DPYes 逆向分析报告.md) - 参考研究

---

## ⚠️ 注意事项

1. **游戏版本**: 确保使用的游戏版本与逆向分析的版本一致
2. **更新影响**: 游戏更新可能导致地址变化
3. **反作弊**: 在线模式使用可能导致封号
4. **备份存档**: 修改前务必备份

---

**最后更新**: 2026-09-19  
**作者**: AI Assistant  
**状态**: 代码已就绪，等待 API 地址填充
