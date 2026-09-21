# 快速参考：一击必杀和秒杀怪物实现

## 🎯 核心目标

1. **秒杀怪物**: AOE 伤害，瞬间杀死周围所有怪物
2. **一击必杀**: 玩家攻击造成最大伤害

---

## 📋 必须找到的 API (按优先级)

### P0 - 核心 API

```
✅ 已找到:
- gGameEngine@GAME@@3PEAVGameEngine@1@EA  [已知]
- GetMainPlayer@GameEngine@GAME@@QEBAPEAVCharacter@2@XZ  [已知]

❌ 待查找:
- GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ
- GetPlayerId@GameEngine@GAME@@QEBAIXZ
- GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z
- ApplyDamage@Character@GAME@@QEAAXPEAVCharacter@2_MI@Z
- GetEntitySystem@GameEngine@GAME@@QEBAPEAVEntitySystem@2@XZ
```

### P1 - 辅助 API

```
- GetEntityCount@EntitySystem@GAME@@QEBAIXZ
- GetEntity@EntitySystem@GAME@@QEBAPEAVEntity@2_H@Z
- IsMonster@Entity@GAME@@QEB_NXZ
- GetEntityPosition@Entity@GAME@@QEBA?AVWorldVec3@2@XZ
```

---

## 🔧 实现步骤速查

### 秒杀怪物流程

```
1. 获取 GameEngine 指针 ✅
2. 获取 PlayerManagerClient ⬜
3. 获取玩家 ID ⬜
4. 获取玩家坐标 ⬜
5. 获取 EntitySystem ⬜
6. 遍历所有实体 ⬜
7. 判断是否为怪物 ⬜
8. 计算距离 ⬜
9. 施加伤害 ⬜
```

### 一击必杀流程

```
方案 A (Hook):
1. 找到 DesignerCalculateOffensiveAbility ⬜
2. 分析函数签名 ⬜
3. 创建 Hook 函数 ⬜
4. 替换原始函数 ⬜

方案 B (改血量):
1. 找到 Character 类基址 ⬜
2. 确定 health 偏移 ⬜
3. 遍历附近怪物 ⬜
4. 写入 health = 1.0f ⬜
```

---

## 💻 代码位置

### 插件端 (C++)
```
src/GrimDawnTeleporter.Plugin/dllmain.cpp
├── InstaKillMonstersInternal()     [第 244 行]
├── SetOneShotModeInternal()        [第 310 行]
├── TryInstaKill()                  [第 279 行]
└── TrySetOneShot()                 [第 342 行]
```

### 主程序端 (C#)
```
src/GrimDawnTeleporter/MainWindow.xaml.cs
├── GodModeToggle_Click()           [无敌模式]
├── OneShotToggle_Click()           [一击必杀]
├── ModifyStrength_Click()          [力量修改]
└── GetStatusInfo()                 [状态查询]
```

---

## 🛠️ 逆向工具

### IDA Pro
```
1. 打开 Grim Dawn.exe
2. 加载 Game.dll
3. 搜索字符串 "PlayerManagerClient"
4. 查看交叉引用
5. 导出函数地址
```

### Cheat Engine
```
1. 附加到游戏进程
2. 扫描浮点数 "999999.0"
3. 查找访问此地址的指令
4. 追踪调用链
```

### 现有诊断工具
```
命令：resolve_symbol:GetPlayerManagerClient@GameEngine@GAME
命令：diagnose_symbols
命令：scan_pattern:Game.dll|<pattern>
```

---

## 📊 调试技巧

### 打印日志
```cpp
// 在关键位置添加
Log(L"[INSTAKILL] Entity count: " + std::to_wstring(count));
Log(L"[INSTAKILL] Player pos: " + std::to_wstring(playerPos.x));
```

### 验证 API
```cpp
// 测试函数是否可用
if (applyDamageAddress != 0) {
    Log(L"[OK] ApplyDamage resolved");
} else {
    Log(L"[FAIL] ApplyDamage not found");
}
```

### 内存检查
```cpp
// 读取怪物血量验证
float currentHealth = ReadFloat(monsterAddress + healthOffset);
Log(L"[HEALTH] Monster HP: " + std::to_wstring(currentHealth));
```

---

## ⚡ 快速测试

### 单元测试
```cpp
TEST(InstaKill, DistanceCalculation) {
    Vec3 p1{0, 0, 0};
    Vec3 p2{30, 0, 0};
    float dist = Sqrt(pow(p2.x-p1.x,2) + pow(p2.y-p1.y,2) + pow(p2.z-p1.z,2));
    ASSERT_FLOAT_EQ(dist, 30.0f);
}
```

### 游戏内测试
```
1. 进入有怪物的地图
2. 站在开阔区域
3. 开启秒杀功能
4. 观察怪物死亡
5. 检查日志输出
```

---

## 🚨 常见问题

### Q1: 找不到某个 API 怎么办？
**A**: 使用 Pattern Scanner 搜索特征码

### Q2: 崩溃了怎么处理？
**A**: 检查所有指针是否在 __try/__except 块中

### Q3: 功能不生效？
**A**: 
1. 确认 API 地址正确
2. 检查调用约定 (fastcall)
3. 验证参数顺序

### Q4: 如何知道找到了正确的函数？
**A**: 
1. 对比 DPYes 的实现
2. 查看字符串引用
3. 分析汇编代码逻辑

---

## 📖 相关文档

- [完整实现指南](docs/insta-kill-implementation.md)
- [实现总结](IMPLEMENTATION-SUMMARY.md)
- [DPYes 逆向报告](../逆向/DPYes 逆向分析报告.md)

---

**最后更新**: 2026-09-19  
**版本**: v1.0
