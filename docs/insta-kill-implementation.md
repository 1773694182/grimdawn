# 一击必杀和秒杀怪物功能实现指南

## 概述

本文档详细说明如何实现 Grim Dawn 的一击必杀 (One-Shot) 和秒杀怪物 (Insta-Kill) 功能。

---

## 一、秒杀怪物功能 (AOE Damage)

### 1.1 功能描述
在玩家周围指定半径内，瞬间杀死所有怪物。

### 1.2 需要的逆向工作

#### 1.2.1 必须找到的 API

**A. 玩家位置获取**
```cpp
// 可能的函数签名
void* GetPlayerManagerClient(void* gameEngine);
unsigned int GetPlayerId(void* playerManager);
void GetPlayerLocation(void* playerManager, void* out_location, unsigned int playerId);
Vec3 GetWorldPosition(void* location);
```

**B. 实体遍历系统**
```cpp
// 可能的函数签名
int GetEntityCount(void* entitySystem);
void* GetEntity(void* entitySystem, int index);
bool IsValidEntity(void* entity);
```

**C. 怪物识别**
```cpp
// 可能的函数签名
EntityType GetEntityType(void* entity);  // 返回 Player/Monster/NPC/Item 等
bool IsMonster(void* entity);
DWORD64 GetMonsterClass(void* entity);  // 获取怪物类型 ID
```

**D. 伤害应用**
```cpp
// 可能的函数签名
void ApplyDamage(
    void* target,           // 怪物指针
    void* attacker,         // 玩家指针  
    float damageAmount,     // 伤害值 (使用最大值)
    unsigned int damageType, // 伤害类型 (物理/火焰/闪电等)
    bool ignoreDefense,     // 是否忽略防御
    bool ignoreImmunity     // 是否忽略免疫
);
```

### 1.3 实现步骤

#### 步骤 1: 找到并解析符号
使用 `ResolveGameExport` 或模式扫描找到上述函数地址。

#### 步骤 2: 实现实体遍历
```cpp
bool InstaKillMonstersInternal(DWORD64 gGameEngineAddress, 
                               DWORD64 getMainPlayerAddress, 
                               float killRadius)
{
    using GetMainPlayerFn = void* (__fastcall*)(void*);
    using GetPlayerManagerClientFn = void* (__fastcall*)(void*);
    using GetPlayerIdFn = unsigned int(__fastcall*)(void*);
    using GetPlayerLocationFn = void(__fastcall*)(void*, void*, unsigned int);
    using GetWorldPositionFn = void(__fastcall*)(void*, Vec3*);
    using GetEntityCountFn = int(__fastcall*)(void*);
    using GetEntityFn = void* (__fastcall*)(void*, int);
    using IsMonsterFn = bool(__fastcall*)(void*);
    using GetEntityPositionFn = void(__fastcall*)(void*, void*);
    using ApplyDamageFn = void(__fastcall*)(void*, void*, float, unsigned int);
    
    __try
    {
        // 1. 获取游戏引擎和玩家
        auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
        if (!gameEngine) return false;
        
        const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
        auto* player = getMainPlayer(gameEngine);
        if (!player) return false;
        
        // 2. 获取玩家位置
        auto* playerManager = getPlayerManagerClient(gameEngine);
        if (!playerManager) return false;
        
        const auto playerId = getPlayerId(playerManager);
        alignas(16) unsigned char worldVec3[128]{};
        getPlayerLocation(playerManager, worldVec3, playerId);
        
        Vec3 playerPos{};
        getWorldPosition(worldVec3, &playerPos);
        
        // 3. 获取实体系统并遍历
        auto* entitySystem = GetEntitySystem(gameEngine);  // TODO: 找到此函数
        
        const int entityCount = getEntityCount(entitySystem);
        float radiusSquared = killRadius * killRadius;
        int killedCount = 0;
        
        for (int i = 0; i < entityCount; i++)
        {
            auto* entity = getEntity(entitySystem, i);
            if (!entity || !IsMonster(entity)) continue;
            
            // 4. 检查距离
            alignas(16) unsigned char entityVec3[128]{};
            getEntityPosition(entity, entityVec3);
            
            Vec3 monsterPos{};
            getWorldPosition(entityVec3, &monsterPos);
            
            float dx = monsterPos.x - playerPos.x;
            float dy = monsterPos.y - playerPos.y;
            float dz = monsterPos.z - playerPos.z;
            float distanceSquared = dx*dx + dy*dy + dz*dz;
            
            if (distanceSquared > radiusSquared) continue;
            
            // 5. 施加伤害
            const float maxDamage = 999999.0f;
            const unsigned int damageType = 0;  // 物理伤害
            
            applyDamage(entity, player, maxDamage, damageType, true, true);
            killedCount++;
        }
        
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
```

---

## 二、一击必杀功能 (One-Shot Mode)

### 2.1 功能描述
开启后，玩家的所有攻击都会造成最大伤害，一击必杀普通怪物。

### 2.2 实现方案

#### 方案 A: Hook 伤害计算函数 (推荐)

**目标函数**: `DesignerCalculateOffensiveAbility` 或类似名称

```cpp
// 原始函数签名 (推测)
float DesignerCalculateOffensiveAbility(
    void* character,      // 攻击者
    void* ability,        // 技能
    void* target,         // 目标
    bool isCritical       // 是否暴击
);

// Hook 后的逻辑
float OneShotCalculateDamage(...)
{
    // 调用原始函数获取基础伤害
    float baseDamage = OriginalFunction(...);
    
    // 返回最大伤害 (或基于角色属性的最大值)
    return CalculateMaxDamage(character);
}
```

**实现步骤**:
1. 使用 Pattern Scanner 在 Game.dll 中找到 `DesignerCalculateOffensiveAbility`
2. 保存原始函数字节码
3. 创建 Hook 函数
4. 使用 trampoline 技术替换原始函数

#### 方案 B: 修改伤害应用逻辑

**目标函数**: `ApplyDamage` 或 `Character::TakeDamage`

```cpp
// 直接修改目标血量到 1
void OneShotTakeDamage(void* character, float damage, ...)
{
    // 如果是怪物，直接设置血量为 1
    if (IsMonster(character))
    {
        WriteMemory<float>((char*)character + healthOffset, 1.0f);
        return;
    }
    
    // 正常处理玩家受伤
    OriginalTakeDamage(character, damage, ...);
}
```

### 2.3 需要找到的偏移量

```cpp
// Character 类结构 (推测)
struct Character {
    // ... 其他字段
    float health;          // 偏移量？
    float maxHealth;       // 偏移量？
    // ... 其他字段
};

// Monster 类扩展
struct Monster : Character {
    // ... 怪物特定字段
};
```

---

## 三、逆向工具和方法

### 3.1 使用 IDA Pro

1. **打开 Grim Dawn.exe**
   - 加载 Game.dll 和 Engine.dll
   - 分析所有导出表

2. **搜索关键字符串**
   ```
   "PlayerManagerClient"
   "EntitySystem"
   "ApplyDamage"
   "TakeDamage"
   "GetEntity"
   ```

3. **查找交叉引用**
   - 从已知函数 (如 SetGod) 开始
   - 追踪调用链找到相关 API

### 3.2 使用 Cheat Engine

1. **扫描内存**
   - 搜索浮点数类型的伤害值
   - 找到伤害计算函数

2. **查找数组**
   - 搜索实体列表的指针
   - 追踪遍历循环

### 3.3 使用 DPYes 源码参考

DPYes 项目可能包含类似的实现:
- 查看其伤害修改逻辑
- 参考其实体遍历方法

---

## 四、测试验证

### 4.1 单元测试

```cpp
// 测试 1: 符号解析
TEST(InstaKillTest, ResolveSymbols)
{
    SymbolResolver resolver;
    ASSERT_TRUE(resolver.Initialize());
    
    auto addr = resolver.Resolve("GetPlayerManagerClient@GameEngine@GAME");
    ASSERT_NE(addr, 0ULL);
}

// 测试 2: 距离计算
TEST(InstaKillTest, DistanceCalculation)
{
    Vec3 p1{0, 0, 0};
    Vec3 p2{30, 0, 0};
    float dist = CalculateDistance(p1, p2);
    ASSERT_FLOAT_EQ(dist, 30.0f);
}
```

### 4.2 游戏内测试

1. **准备测试环境**
   - 进入有怪物的地图
   - 确保怪物密度足够

2. **测试秒杀功能**
   - 使用 F9 快捷键
   - 观察怪物是否死亡
   - 检查日志输出

3. **测试一击必杀**
   - 开启一击必杀模式
   - 攻击普通怪物
   - 验证是否能一击击杀

---

## 五、安全注意事项

### 5.1 异常保护
所有内存访问必须在 `__try/__except` 块中。

### 5.2 边界检查
- 检查所有指针有效性
- 验证实体索引范围
- 防止除零错误

### 5.3 性能考虑
- 避免在每帧遍历所有实体
- 限制秒杀半径上限
- 添加冷却时间

---

## 六、下一步行动

### 优先级 1: 符号发现
使用现有工具找出以下函数的地址:
- [ ] GetPlayerManagerClient
- [ ] GetEntityCount
- [ ] GetEntity
- [ ] ApplyDamage

### 优先级 2: 原型实现
编写最小可行代码，仅实现核心逻辑。

### 优先级 3: 完整实现
添加错误处理、日志记录和优化。

### 优先级 4: 测试调试
在游戏中充分测试，修复所有问题。

---

## 七、参考资料

1. **DPYes 逆向分析报告** - 查看已有研究
2. **Grim Dawn SDK** - 官方文档 (如有)
3. **ARPG 游戏逆向通用技术** - 博客文章和研究论文

---

**作者注**: 本功能仅供学习和研究使用。在游戏在线模式下使用作弊可能导致账号封禁。
