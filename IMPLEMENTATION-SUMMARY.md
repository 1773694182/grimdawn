# 一击必杀和秒杀怪物功能实现总结

## 当前状态

✅ **已完成**:
1. 代码框架搭建完成
2. 命令协议设计完成
3. 占位函数实现完成
4. UI 界面集成完成
5. 编译测试通过

⚠️ **待完成**:
1. 逆向找到必要的游戏 API 地址
2. 实现实体遍历逻辑
3. 实现伤害应用逻辑
4. 完整测试验证

---

## 技术实现详情

### 1. 秒杀怪物 (Insta-Kill)

#### 1.1 已实现部分

**命令接口**:
```cpp
// 格式：insta_kill:<radius>
// radius: 秒杀半径，默认 50.0f
std::string TryInstaKill(const std::string& command)
```

**内部函数框架**:
```cpp
bool InstaKillMonstersInternal(
    DWORD64 gGameEngineAddress, 
    DWORD64 getMainPlayerAddress, 
    float killRadius)
```

**实现逻辑** (待完善):
1. ✅ 获取游戏引擎指针
2. ✅ 获取玩家对象
3. ⚠️ 获取玩家世界坐标 (需要 API)
4. ⚠️ 获取实体系统 (需要 API)
5. ⚠️ 遍历所有实体 (需要 API)
6. ⚠️ 判断实体类型 (需要 API)
7. ⚠️ 计算距离 (框架完成)
8. ⚠️ 施加伤害 (需要 API)

#### 1.2 需要的 API

根据 ARPG 游戏通用架构，以下 API 必须找到:

| API 名称 | 推测签名 | 用途 | 优先级 |
|---------|---------|------|-------|
| GetPlayerManagerClient | `void* (__fastcall*)(void*)` | 从 GameEngine 获取 PlayerManager | P0 |
| GetPlayerId | `unsigned int(__fastcall*)(void*)` | 获取当前玩家 ID | P0 |
| GetPlayerLocation | `void(__fastcall*)(void*, void*, unsigned int)` | 获取玩家世界坐标 | P0 |
| GetEntitySystem | `void* (__fastcall*)(void*)` | 获取实体管理系统 | P0 |
| GetEntityCount | `int(__fastcall*)(void*)` | 获取实体总数 | P1 |
| GetEntity | `void* (__fastcall*)(void*, int)` | 根据索引获取实体 | P1 |
| IsMonster | `bool(__fastcall*)(void*)` | 判断实体是否为怪物 | P1 |
| GetEntityPosition | `void(__fastcall*)(void*, void*)` | 获取实体坐标 | P1 |
| ApplyDamage | `void(__fastcall*)(void*, void*, float, unsigned int)` | 施加伤害 | P0 |

#### 1.3 参考实现伪代码

```cpp
// 伪代码 - 待转换为实际 API
auto* playerManager = GetPlayerManagerClient(gameEngine);
auto playerId = GetPlayerId(playerManager);
alignas(16) unsigned char worldVec3[128]{};
GetPlayerLocation(playerManager, worldVec3, playerId);

Vec3 playerPos{};
GetWorldPosition(worldVec3, &playerPos);

auto* entitySystem = GetEntitySystem(gameEngine);
const int count = GetEntityCount(entitySystem);

for (int i = 0; i < count; i++) {
    auto* entity = GetEntity(entitySystem, i);
    if (!IsMonster(entity)) continue;
    
    alignas(16) unsigned char entityVec3[128]{};
    GetEntityPosition(entity, entityVec3);
    
    Vec3 monsterPos{};
    GetWorldPosition(entityVec3, &monsterPos);
    
    // 计算距离
    float dx = monsterPos.x - playerPos.x;
    float dy = monsterPos.y - playerPos.y;
    float dz = monsterPos.z - playerPos.z;
    float distSq = dx*dx + dy*dy + dz*dz;
    
    if (distSq <= radius*radius) {
        // 施加最大伤害
        ApplyDamage(entity, player, 999999.0f, 0, true, true);
    }
}
```

---

### 2. 一击必杀 (One-Shot Mode)

#### 2.1 已实现部分

**命令接口**:
```cpp
// 格式：one_shot:<true|false>
std::string TrySetOneShot(const std::string& command)
```

**内部函数框架**:
```cpp
bool SetOneShotModeInternal(
    DWORD64 gGameEngineAddress, 
    DWORD64 getMainPlayerAddress, 
    bool enable)
```

#### 2.2 实现方案对比

##### 方案 A: Hook 伤害计算函数 (推荐)

**目标函数**: `DesignerCalculateOffensiveAbility` 或类似

**优点**:
- 影响所有伤害来源
- 符合游戏原意
- 不容易被检测

**缺点**:
- 需要精确找到函数地址
- 需要处理函数签名
- 可能受游戏更新影响

**实现步骤**:
1. 使用 Pattern Scanner 在 Game.dll 中搜索
2. 分析函数调用约定和参数
3. 创建 Hook 函数
4. 使用 trampoline 或直接替换

##### 方案 B: 修改怪物血量

**目标**: 直接写入 Character::health 字段

**优点**:
- 实现简单
- 不需要 Hook

**缺点**:
- 只适用于近战攻击
- 需要知道准确的内存偏移
- 可能被反作弊检测

**实现步骤**:
1. 找到 Character 类结构
2. 确定 health 字段偏移
3. 遍历附近怪物
4. 写入 health = 1.0f

##### 方案 C: 修改伤害类型免疫

**目标**: 让怪物对所有伤害免疫失效

**优点**:
- 一次性生效
- 影响范围广

**缺点**:
- 可能需要修改多个地方
- 实现复杂

#### 2.3 推荐实现路径

基于可行性和稳定性，推荐使用 **方案 A + 方案 B 混合**:

1. **短期方案**: 方案 B (直接改血量)
   - 快速实现可用功能
   - 代码简单易懂
   - 容易调试

2. **长期方案**: 方案 A (Hook 伤害计算)
   - 更优雅的实现
   - 更符合游戏逻辑
   - 维护成本更低

---

### 3. 属性修改 (Attribute Modification)

#### 3.1 已实现部分

**命令接口**:
```cpp
// 格式：modify_attribute:<type>:<value>
// type: strength, agility, intellect, oa, da, health, mana
std::string TryModifyAttribute(const std::string& command)
```

#### 3.2 需要的信息

**Character 类结构推测**:
```cpp
struct Character {
    // ... 基础字段
    float health;          // 当前生命值
    float maxHealth;       // 最大生命值
    float mana;            // 当前法力值
    float maxMana;         // 最大法力值
    
    // 属性值
    unsigned int strength;     // 力量
    unsigned int agility;      // 敏捷
    unsigned int intellect;    // 智力
    unsigned int oa;           // 命中
    unsigned int da;           // 闪避
    // ... 其他字段
};
```

**实现思路**:
1. 找到 Character 类基地址
2. 确定各属性字段的偏移量
3. 直接写入新值
4. 触发 UI 更新

---

## 逆向工作清单

### P0 - 必须找到 (核心功能)

- [ ] GetPlayerManagerClient 地址
- [ ] GetPlayerId 地址  
- [ ] GetPlayerLocation 地址
- [ ] ApplyDamage 地址
- [ ] Character::health 偏移

### P1 - 重要功能

- [ ] GetEntitySystem 地址
- [ ] GetEntityCount 地址
- [ ] GetEntity 地址
- [ ] IsMonster 地址
- [ ] GetEntityPosition 地址
- [ ] Character::strength 偏移
- [ ] Character::agility 偏移

### P2 - 增强功能

- [ ] DesignerCalculateOffensiveAbility 地址 (用于 One-Shot Hook)
- [ ] Entity 类型判断完整列表
- [ ] DamageType 枚举定义
- [ ] Character 完整结构

---

## 下一步行动计划

### 第一阶段：符号发现 (预计 2-4 小时)

1. 打开 IDA Pro，加载 Grim Dawn.exe 和 Game.dll
2. 搜索已知字符串 (PlayerManagerClient, EntitySystem 等)
3. 使用现有工具 DiagnoseSymbols 辅助定位
4. 记录找到的所有相关地址

### 第二阶段：原型实现 (预计 4-6 小时)

1. 实现最基础的实体遍历
2. 实现简单的距离计算
3. 实现最简单的伤害应用 (如果能找到 API)
4. 在测试服验证功能

### 第三阶段：完整实现 (预计 6-8 小时)

1. 完善错误处理
2. 添加日志输出
3. 优化性能 (避免每帧遍历)
4. 添加配置选项 (半径、伤害类型等)

### 第四阶段：测试优化 (预计 2-4 小时)

1. 压力测试 (大量怪物场景)
2. 边界测试 (无怪物、单个怪物等)
3. 兼容性测试 (不同地图、不同版本)
4. 性能分析 (内存占用、CPU 使用率)

---

## 风险提示

### 安全风险
- ⚠️ 在线模式使用可能导致封号
- ⚠️ 可能被反作弊软件检测
- ⚠️ 修改内存可能导致游戏崩溃

### 技术风险
- ⚠️ 游戏更新后 API 可能变化
- ⚠️ 错误的内存访问会导致崩溃
- ⚠️ Hook 可能破坏游戏稳定性

### 缓解措施
- ✅ 仅用于本地单人模式
- ✅ 所有内存访问加异常保护
- ✅ 使用前备份存档
- ✅ 详细记录实现的每个步骤

---

## 参考资料

1. **[秒杀怪物实现指南](docs/insta-kill-implementation.md)** - 详细技术文档
2. **DPYes 逆向分析报告** - 参考已有研究
3. **Grim Dawn Modding Wiki** - 社区资源
4. **Cheat Engine 论坛** - 逆向技巧分享

---

**最后更新**: 2026-09-19  
**版本**: v1.0  
**作者**: AI Assistant
