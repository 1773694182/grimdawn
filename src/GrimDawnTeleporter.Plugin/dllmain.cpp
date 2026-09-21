#include <windows.h>
#include <psapi.h>

#include <dbghelp.h>

#include <atomic>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>
#include <sstream>
#include <string>

namespace
{
    DWORD64 ResolveGameExport(const char* name);
    DWORD64 ResolveModuleExport(const wchar_t* moduleName, const char* name);

    std::atomic_bool g_stop{ false };
    HMODULE g_module{};
    HANDLE g_worker{};
    HANDLE g_process{ GetCurrentProcess() };
    std::atomic_bool g_symbolsInitialized{ false };
    
    // Cheat function status
    std::atomic_bool g_godModeEnabled{ false };
    std::atomic_bool g_oneShotEnabled{ false };
    std::atomic<uintptr_t> g_worldAddress{ 0 };
    std::atomic<uintptr_t> g_controllerAddress{ 0 };
    std::atomic<unsigned int> g_playerObjectId{ 0 };
    std::atomic<uintptr_t> g_cachedWorldAddress{ 0 };
    std::atomic<int> g_autoKillTick{ 0 };
    std::atomic<int> g_autoKillLastQuery{ 0 };
    std::atomic<int> g_autoKillLastKilled{ 0 };
    std::atomic<int> g_autoKillLastError{ 0 };
    std::mutex g_entityQueryMutex;

    struct ModuleInfo
    {
        std::wstring name;
        uintptr_t base{};
        size_t size{};
    };

    struct SectionInfo
    {
        std::string name;
        uintptr_t base{};
        size_t size{};
    };

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    std::optional<ModuleInfo> GetModuleInfoByName(const std::wstring& moduleName);
    std::optional<SectionInfo> GetSectionInfo(const ModuleInfo& module, const char* sectionName);
    std::string ToHex(uintptr_t value);
    void Log(const std::wstring& message);

    bool ReadPositionInternal(DWORD64 gGameEngineAddress, DWORD64 getPlayerManagerClientAddress, DWORD64 getPlayerIdAddress, DWORD64 getPlayerLocationAddress, DWORD64 getWorldPositionAddress, Vec3* position)
    {
        using GetPlayerManagerClientFn = void* (__fastcall*)(void*);
        using GetPlayerIdFn = unsigned int(__fastcall*)(void*);
        using GetPlayerLocationFn = void* (__fastcall*)(void*, void*, unsigned int);
        using GetWorldPositionFn = void* (__fastcall*)(void*, Vec3*);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getPlayerManagerClient = reinterpret_cast<GetPlayerManagerClientFn>(static_cast<uintptr_t>(getPlayerManagerClientAddress));
            const auto getPlayerId = reinterpret_cast<GetPlayerIdFn>(static_cast<uintptr_t>(getPlayerIdAddress));
            const auto getPlayerLocation = reinterpret_cast<GetPlayerLocationFn>(static_cast<uintptr_t>(getPlayerLocationAddress));
            const auto getWorldPosition = reinterpret_cast<GetWorldPositionFn>(static_cast<uintptr_t>(getWorldPositionAddress));

            auto* playerManager = getPlayerManagerClient(gameEngine);
            if (!playerManager)
            {
                return false;
            }

            const auto playerId = getPlayerId(gameEngine);
            alignas(16) unsigned char worldVec3[128]{};
            getPlayerLocation(playerManager, worldVec3, playerId);
            getWorldPosition(worldVec3, position);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TeleportInternal(DWORD64 gGameEngineAddress, DWORD64 initiatePlayerTeleportAddress, const Vec3& position)
    {
        using InitiatePlayerTeleportFn = void(__fastcall*)(void*, int, int, int, int, bool);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto initiatePlayerTeleport = reinterpret_cast<InitiatePlayerTeleportFn>(static_cast<uintptr_t>(initiatePlayerTeleportAddress));
            initiatePlayerTeleport(
                gameEngine,
                static_cast<int>(position.x),
                static_cast<int>(position.y),
                static_cast<int>(position.z),
                1,
                true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TeleportToWorldPositionInternal(
        DWORD64 gGameEngineAddress,
        DWORD64 getMainPlayerAddress,
        DWORD64 getPlayerManagerClientAddress,
        DWORD64 getPlayerIdAddress,
        DWORD64 getPlayerLocationAddress,
        DWORD64 getRegionAddress,
        DWORD64 setFromWorldPositionAddress,
        DWORD64 worldCoordsCtorAddress,
        DWORD64 setOriginAddress,
        DWORD64 teleportToLocationAddress,
        const Vec3& target)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        using GetPlayerManagerClientFn = void* (__fastcall*)(void*);
        using GetPlayerIdFn = unsigned int(__fastcall*)(void*);
        using GetPlayerLocationFn = void* (__fastcall*)(void*, void*, unsigned int);
        using GetRegionFn = void* (__fastcall*)(void*);
        using SetFromWorldPositionFn = bool(__fastcall*)(void*, const Vec3*, void*);
        using WorldCoordsCtorFn = void(__fastcall*)(void*);
        using SetOriginFn = void(__fastcall*)(void*, const void*);
        using TeleportToLocationFn = void(__fastcall*)(void*, const void*);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
            auto* player = getMainPlayer(gameEngine);
            if (!player)
            {
                return false;
            }

            const auto getPlayerManagerClient = reinterpret_cast<GetPlayerManagerClientFn>(static_cast<uintptr_t>(getPlayerManagerClientAddress));
            auto* playerManager = getPlayerManagerClient(gameEngine);
            if (!playerManager)
            {
                return false;
            }

            const auto getPlayerId = reinterpret_cast<GetPlayerIdFn>(static_cast<uintptr_t>(getPlayerIdAddress));
            const auto playerId = getPlayerId(gameEngine);

            alignas(16) unsigned char worldVec3[128]{};
            const auto getPlayerLocation = reinterpret_cast<GetPlayerLocationFn>(static_cast<uintptr_t>(getPlayerLocationAddress));
            getPlayerLocation(playerManager, worldVec3, playerId);

            const auto getRegion = reinterpret_cast<GetRegionFn>(static_cast<uintptr_t>(getRegionAddress));
            void* region = getRegion(worldVec3);

            const auto setFromWorldPosition = reinterpret_cast<SetFromWorldPositionFn>(static_cast<uintptr_t>(setFromWorldPositionAddress));
            if (!setFromWorldPosition(worldVec3, &target, region))
            {
                return false;
            }

            alignas(16) unsigned char worldCoords[256]{};
            const auto worldCoordsCtor = reinterpret_cast<WorldCoordsCtorFn>(static_cast<uintptr_t>(worldCoordsCtorAddress));
            worldCoordsCtor(worldCoords);

            const auto setOrigin = reinterpret_cast<SetOriginFn>(static_cast<uintptr_t>(setOriginAddress));
            setOrigin(worldCoords, worldVec3);

            const auto teleportToLocation = reinterpret_cast<TeleportToLocationFn>(static_cast<uintptr_t>(teleportToLocationAddress));
            teleportToLocation(player, worldCoords);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryTeleportWorld(const std::string& command)
    {
        const auto separator = command.find(':');
        if (separator == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"teleport_world format is teleport_world:x,y,z\"}\n";
        }

        Vec3 position{};
        if (sscanf_s(command.c_str() + separator + 1, "%f,%f,%f", &position.x, &position.y, &position.z) != 3)
        {
            return "{\"type\":\"error\",\"message\":\"invalid teleport_world coordinates\"}\n";
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getPlayerManagerClientAddress = ResolveGameExport("?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ");
        const auto getPlayerIdAddress = ResolveGameExport("?GetPlayerId@GameEngine@GAME@@QEBAIXZ");
        const auto getPlayerLocationAddress = ResolveGameExport("?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z");
        const auto getRegionAddress = ResolveGameExport("?GetRegion@WorldVec3@GAME@@QEBAPEAVRegion@2@XZ");
        const auto setFromWorldPositionAddress = ResolveGameExport("?SetFromWorldPosition@WorldVec3@GAME@@QEAA_NAEBVVec3@2@PEAVRegion@2@@Z");
        const auto worldCoordsCtorAddress = ResolveGameExport("??0WorldCoords@GAME@@QEAA@XZ");
        const auto setOriginAddress = ResolveGameExport("?SetOrigin@WorldCoords@GAME@@QEAAXAEBVWorldVec3@2@@Z");
        const auto teleportToLocationAddress = ResolveGameExport("?TeleportToLocation@Character@GAME@@UEAAXAEBVWorldCoords@2@@Z");

        if (!gGameEngineAddress || !getMainPlayerAddress || !getPlayerManagerClientAddress || !getPlayerIdAddress
            || !getPlayerLocationAddress || !getRegionAddress || !setFromWorldPositionAddress
            || !worldCoordsCtorAddress || !setOriginAddress || !teleportToLocationAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required teleport_world symbols not resolved\"}\n";
        }

        if (!TeleportToWorldPositionInternal(
                gGameEngineAddress,
                getMainPlayerAddress,
                getPlayerManagerClientAddress,
                getPlayerIdAddress,
                getPlayerLocationAddress,
                getRegionAddress,
                setFromWorldPositionAddress,
                worldCoordsCtorAddress,
                setOriginAddress,
                teleportToLocationAddress,
                position))
        {
            return "{\"type\":\"error\",\"message\":\"teleport_world failed\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"teleport_world\",\"x\":" << position.x << ",\"y\":" << position.y << ",\"z\":" << position.z << "}\n";
        return json.str();
    }

    bool TryReadPlayerPointers(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, void** gameEngine, void** player)
    {
        __try
        {
            *gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (*gameEngine)
            {
                using GetMainPlayerFn = void* (__fastcall*)(void*);
                *player = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress))(*gameEngine);
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryReadBoolMemberFunction(DWORD64 address, void* instance, bool* result)
    {
        using BoolMemberFn = bool(__fastcall*)(void*);

        __try
        {
            *result = reinterpret_cast<BoolMemberFn>(static_cast<uintptr_t>(address))(instance);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryCanTeleport()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto canUseAddress = ResolveGameExport("?MainPlayerCanUsePersonalTeleport@GameEngine@GAME@@QEBA_NXZ");
        const auto hasAddress = ResolveGameExport("?MainPlayerHasPersonalTeleport@GameEngine@GAME@@QEBA_NXZ");
        const auto isTeleportingAddress = ResolveGameExport("?IsTeleporting@Character@GAME@@QEBA_NXZ");
        if (!gGameEngineAddress || !getMainPlayerAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required teleport capability symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadPlayerPointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player))
        {
            return "{\"type\":\"error\",\"message\":\"exception while reading player pointers\"}\n";
        }

        if (!gameEngine)
        {
            return "{\"type\":\"error\",\"message\":\"gGameEngine is null\"}\n";
        }

        bool canUse = false;
        bool has = false;
        bool teleporting = false;
        const auto canUseOk = canUseAddress && TryReadBoolMemberFunction(canUseAddress, gameEngine, &canUse);
        const auto hasOk = hasAddress && TryReadBoolMemberFunction(hasAddress, gameEngine, &has);
        const auto teleportingOk = player && isTeleportingAddress && TryReadBoolMemberFunction(isTeleportingAddress, player, &teleporting);

        std::ostringstream json;
        json << "{\"type\":\"can_teleport\"";
        json << ",\"mainPlayerCanUsePersonalTeleport\":" << (canUseOk ? (canUse ? "true" : "false") : "null");
        json << ",\"mainPlayerHasPersonalTeleport\":" << (hasOk ? (has ? "true" : "false") : "null");
        json << ",\"isTeleporting\":" << (teleportingOk ? (teleporting ? "true" : "false") : "null");
        json << "}\n";
        return json.str();
    }

    bool ReadPointerSafe(uintptr_t address, uintptr_t* value)
    {
        __try
        {
            *value = *reinterpret_cast<const uintptr_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsCodeAddress(uintptr_t value, const ModuleInfo& first, const ModuleInfo& second)
    {
        return (value >= first.base && value < first.base + first.size)
            || (value >= second.base && value < second.base + second.size);
    }

    struct WorldSearchResult
    {
        uintptr_t world{};
        uintptr_t knownRegion{};
        int scanSources{};
        int totalCandidates{};
        int inlineProbes{};
        int matchCount{};
        int matchedIndex{ -1 };
        uintptr_t matches[8]{};
    };

    struct WorldVerificationContext
    {
        uintptr_t knownRegion{};
        uintptr_t getRegionAddress{};
        uintptr_t getRegionContainingPointAddress{};
        uintptr_t getEntitiesInSphereAddress{};
        int worldPoint[3]{};
        int regionPoint[3]{};
        float center[3]{};
        int maxIndex{ 64 };
    };

    bool VerifyWorldCandidate(uintptr_t candidate, const WorldVerificationContext& context, int* matchedIndex)
    {
        using GetRegionFn = void* (__fastcall*)(void*, int);
        using GetRegionContainingPointFn = void* (__fastcall*)(void*, const void*);
        using GetEntitiesInSphereFn = void(__fastcall*)(void*, void*, void*, const void*, bool, int);

        struct IntVec3
        {
            int x;
            int y;
            int z;
        };

        struct Sphere
        {
            float x;
            float y;
            float z;
            float radius;
        };

        struct RawVector
        {
            uintptr_t begin;
            uintptr_t end;
            uintptr_t capacity;
        };

        __try
        {
            auto regionMatched = false;
            if (context.getRegionContainingPointAddress)
            {
                const auto getRegionContainingPoint = reinterpret_cast<GetRegionContainingPointFn>(context.getRegionContainingPointAddress);

                IntVec3 worldPoint{ context.worldPoint[0], context.worldPoint[1], context.worldPoint[2] };
                if (reinterpret_cast<uintptr_t>(getRegionContainingPoint(reinterpret_cast<void*>(candidate), &worldPoint)) == context.knownRegion)
                {
                    *matchedIndex = -2;
                    regionMatched = true;
                }

                if (!regionMatched)
                {
                    IntVec3 regionPoint{ context.regionPoint[0], context.regionPoint[1], context.regionPoint[2] };
                    if (reinterpret_cast<uintptr_t>(getRegionContainingPoint(reinterpret_cast<void*>(candidate), &regionPoint)) == context.knownRegion)
                    {
                        *matchedIndex = -3;
                        regionMatched = true;
                    }
                }
            }

            if (!regionMatched && context.getRegionAddress)
            {
                const auto getRegion = reinterpret_cast<GetRegionFn>(context.getRegionAddress);
                for (int i = 0; i < context.maxIndex; ++i)
                {
                    if (reinterpret_cast<uintptr_t>(getRegion(reinterpret_cast<void*>(candidate), i)) == context.knownRegion)
                    {
                        *matchedIndex = i;
                        regionMatched = true;
                        break;
                    }
                }
            }

            if (!regionMatched)
            {
                return false;
            }

            if (context.getEntitiesInSphereAddress)
            {
                RawVector entityVector{};
                Sphere sphere{ context.center[0], context.center[1], context.center[2], 1.0f };
                reinterpret_cast<GetEntitiesInSphereFn>(context.getEntitiesInSphereAddress)(
                    reinterpret_cast<void*>(candidate),
                    &entityVector,
                    reinterpret_cast<void*>(context.knownRegion),
                    &sphere,
                    false,
                    0);
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsObjectPointerCandidate(uintptr_t value)
    {
        return value >= 0x10000 && value <= 0x00007FFFFFFFFFFF;
    }

    void ScanForWorldPointers(uintptr_t scanBase, size_t scanSize, const WorldVerificationContext& context, WorldSearchResult* result)
    {
        result->scanSources++;

        const auto alignedBase = scanBase & ~static_cast<uintptr_t>(7);
        const auto adjust = static_cast<size_t>(scanBase - alignedBase);
        if (scanSize <= adjust || !context.knownRegion)
        {
            return;
        }

        const auto available = scanSize - adjust;
        for (size_t offset = 0; offset + sizeof(uintptr_t) <= available; offset += sizeof(uintptr_t))
        {
            uintptr_t candidate = 0;
            if (!ReadPointerSafe(alignedBase + offset, &candidate))
            {
                continue;
            }

            if (!IsObjectPointerCandidate(candidate))
            {
                continue;
            }

            result->totalCandidates++;

            int matchedIndex = -1;
            if (VerifyWorldCandidate(candidate, context, &matchedIndex))
            {
                if (result->matchCount < 8)
                {
                    result->matches[result->matchCount] = candidate;
                }

                result->matchCount++;
                if (!result->world)
                {
                    result->world = candidate;
                    result->matchedIndex = matchedIndex;
                }
            }
        }
    }

    void ScanForInlineWorld(uintptr_t objectBase, size_t maxOffset, const WorldVerificationContext& context, WorldSearchResult* result)
    {
        if (!objectBase || !context.knownRegion)
        {
            return;
        }

        for (size_t offset = 0; offset < maxOffset; offset += sizeof(uintptr_t))
        {
            result->inlineProbes++;

            int matchedIndex = -1;
            if (VerifyWorldCandidate(objectBase + offset, context, &matchedIndex))
            {
                if (result->matchCount < 8)
                {
                    result->matches[result->matchCount] = objectBase + offset;
                }

                result->matchCount++;
                if (!result->world)
                {
                    result->world = objectBase + offset;
                    result->matchedIndex = matchedIndex;
                }

                return;
            }
        }
    }

    bool TryReadPlayerPositionsAndRegion(
        DWORD64 gGameEngineAddress,
        DWORD64 getMainPlayerAddress,
        DWORD64 getPlayerManagerClientAddress,
        DWORD64 getPlayerIdAddress,
        DWORD64 getPlayerLocationAddress,
        DWORD64 getWorldPositionAddress,
        DWORD64 getRegionPositionAddress,
        DWORD64 getWorldVec3RegionAddress,
        uintptr_t* player,
        uintptr_t* playerManager,
        Vec3* worldPosition,
        Vec3* regionPosition,
        uintptr_t* region)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        using GetPlayerManagerClientFn = void* (__fastcall*)(void*);
        using GetPlayerIdFn = unsigned int(__fastcall*)(void*);
        using GetPlayerLocationFn = void* (__fastcall*)(void*, void*, unsigned int);
        using GetWorldPositionFn = void* (__fastcall*)(void*, Vec3*);
        using GetRegionPositionFn = const Vec3* (__fastcall*)(void*);
        using GetRegionFn = void* (__fastcall*)(void*);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            *player = reinterpret_cast<uintptr_t>(reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress))(gameEngine));
            *playerManager = reinterpret_cast<uintptr_t>(reinterpret_cast<GetPlayerManagerClientFn>(static_cast<uintptr_t>(getPlayerManagerClientAddress))(gameEngine));

            if (!*playerManager || !getPlayerIdAddress || !getPlayerLocationAddress)
            {
                return false;
            }

            const auto playerId = reinterpret_cast<GetPlayerIdFn>(static_cast<uintptr_t>(getPlayerIdAddress))(gameEngine);
            alignas(16) unsigned char worldVec3[128]{};
            reinterpret_cast<GetPlayerLocationFn>(static_cast<uintptr_t>(getPlayerLocationAddress))(reinterpret_cast<void*>(*playerManager), worldVec3, playerId);

            if (getWorldPositionAddress)
            {
                reinterpret_cast<GetWorldPositionFn>(static_cast<uintptr_t>(getWorldPositionAddress))(worldVec3, worldPosition);
            }

            if (getRegionPositionAddress)
            {
                const auto regionRef = reinterpret_cast<GetRegionPositionFn>(static_cast<uintptr_t>(getRegionPositionAddress))(worldVec3);
                if (regionRef)
                {
                    *regionPosition = *regionRef;
                }
            }

            if (getWorldVec3RegionAddress)
            {
                *region = reinterpret_cast<uintptr_t>(reinterpret_cast<GetRegionFn>(static_cast<uintptr_t>(getWorldVec3RegionAddress))(worldVec3));
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FindWorldFull(WorldSearchResult* result)
    {
        const auto getRegionContainingPointAddress = ResolveGameExport("?GetRegionContainingPoint@World@GAME@@QEBAPEAVRegion@2@AEBVIntVec3@2@@Z");
        const auto getRegionAddress = ResolveGameExport("?GetRegion@World@GAME@@QEAAPEAVRegion@2@H@Z");
        if (!getRegionContainingPointAddress && !getRegionAddress)
        {
            return false;
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getPlayerManagerClientAddress = ResolveGameExport("?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ");
        const auto getPlayerIdAddress = ResolveGameExport("?GetPlayerId@GameEngine@GAME@@QEBAIXZ");
        const auto getPlayerLocationAddress = ResolveGameExport("?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z");
        const auto getWorldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");
        const auto getRegionPositionAddress = ResolveGameExport("?GetRegionPosition@WorldVec3@GAME@@QEBAAEBVVec3@2@XZ");
        const auto getWorldVec3RegionAddress = ResolveGameExport("?GetRegion@WorldVec3@GAME@@QEBAPEAVRegion@2@XZ");

        uintptr_t gameEngine = 0;
        uintptr_t player = 0;
        uintptr_t playerManager = 0;
        uintptr_t region = 0;
        if (gGameEngineAddress)
        {
            ReadPointerSafe(gGameEngineAddress, &gameEngine);
        }

        Vec3 worldPosition{};
        Vec3 regionPosition{};
        if (gameEngine && getWorldPositionAddress && getPlayerManagerClientAddress)
        {
            TryReadPlayerPositionsAndRegion(
                gGameEngineAddress,
                getMainPlayerAddress,
                getPlayerManagerClientAddress,
                getPlayerIdAddress,
                getPlayerLocationAddress,
                getWorldPositionAddress,
                getRegionPositionAddress,
                getWorldVec3RegionAddress,
                &player,
                &playerManager,
                &worldPosition,
                &regionPosition,
                &region);
        }

        result->knownRegion = region;
        if (!region)
        {
            return false;
        }

        WorldVerificationContext context{};
        context.knownRegion = region;
        context.getRegionAddress = getRegionAddress;
        context.getRegionContainingPointAddress = getRegionContainingPointAddress;
        context.getEntitiesInSphereAddress = ResolveGameExport("?GetEntitiesInSphere@World@GAME@@QEBAXAEAV?$vector@PEAVEntity@GAME@@@mem@@PEAVRegion@2@AEBVSphere@2@_NW4EntityListType@2@@Z");
        context.worldPoint[0] = static_cast<int>(worldPosition.x);
        context.worldPoint[1] = static_cast<int>(worldPosition.y);
        context.worldPoint[2] = static_cast<int>(worldPosition.z);
        context.regionPoint[0] = static_cast<int>(regionPosition.x);
        context.regionPoint[1] = static_cast<int>(regionPosition.y);
        context.regionPoint[2] = static_cast<int>(regionPosition.z);
        context.center[0] = regionPosition.x;
        context.center[1] = regionPosition.y;
        context.center[2] = regionPosition.z;

        if (region)
        {
            ScanForWorldPointers(region - 0x2000, 0x4000, context, result);
        }

        if (!result->world && playerManager)
        {
            ScanForWorldPointers(playerManager - 0x4000, 0x8000, context, result);
        }

        if (!result->world && player)
        {
            ScanForWorldPointers(player - 0x4000, 0x8000, context, result);
        }

        if (!result->world && gameEngine)
        {
            ScanForWorldPointers(gameEngine - 0x10000, 0x20000, context, result);
        }

        if (!result->world && gameEngine)
        {
            ScanForInlineWorld(gameEngine, 0x4000, context, result);
        }

        if (!result->world && region)
        {
            ScanForInlineWorld(region, 0x2000, context, result);
        }

        if (result->world)
        {
            g_worldAddress.store(result->world);
        }

        return result->world != 0;
    }

    std::string TryFindWorld()
    {
        WorldSearchResult result{};
        if (!FindWorldFull(&result))
        {
            std::ostringstream error;
            error << "{\"type\":\"error\",\"message\":\"world not located\"";
            error << ",\"knownRegion\":\"" << ToHex(result.knownRegion) << "\"";
            error << ",\"scanSources\":" << result.scanSources;
            error << ",\"totalCandidates\":" << result.totalCandidates;
            error << ",\"inlineProbes\":" << result.inlineProbes;
            error << "}\n";
            return error.str();
        }

        std::ostringstream json;
        json << "{\"type\":\"find_world\"";
        json << ",\"world\":\"" << ToHex(result.world) << "\"";
        json << ",\"knownRegion\":\"" << ToHex(result.knownRegion) << "\"";
        json << ",\"matchedIndex\":" << result.matchedIndex;
        json << ",\"scanSources\":" << result.scanSources;
        json << ",\"totalCandidates\":" << result.totalCandidates;
        json << ",\"inlineProbes\":" << result.inlineProbes;
        json << ",\"matchCount\":" << result.matchCount;
        json << ",\"matches\":[";
        const auto limit = result.matchCount < 8 ? result.matchCount : 8;
        for (int i = 0; i < limit; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "\"" << ToHex(result.matches[i]) << "\"";
        }

        json << "]}";
        return json.str();
    }

    struct GameVectorLayout
    {
        uintptr_t begin;
        uintptr_t end;
        uintptr_t capacity;
    };

    bool CallGetEntitiesInSphere(
        uintptr_t world,
        uintptr_t functionAddress,
        uintptr_t region,
        const Vec3& center,
        float radius,
        int listType,
        bool useBool,
        int layout,
        int* count,
        uintptr_t* firstEntity,
        uintptr_t* firstVtable,
        uintptr_t* secondEntity)
    {
        using GetEntitiesInSphereFn = void(__fastcall*)(void*, void*, void*, const void*, bool, int);

        __try
        {
            alignas(16) unsigned char sphereBuffer[64]{};
            if (layout == 0)
            {
                struct SphereA
                {
                    Vec3 center;
                    float radius;
                };

                auto* sphere = reinterpret_cast<SphereA*>(sphereBuffer);
                sphere->center = center;
                sphere->radius = radius;
            }
            else if (layout == 1)
            {
                struct SphereB
                {
                    float radius;
                    Vec3 center;
                };

                auto* sphere = reinterpret_cast<SphereB*>(sphereBuffer);
                sphere->radius = radius;
                sphere->center = center;
            }
            else
            {
                struct SphereC
                {
                    float x;
                    float y;
                    float z;
                    float w;
                    float radius;
                    float padding[3];
                };

                auto* sphere = reinterpret_cast<SphereC*>(sphereBuffer);
                sphere->x = center.x;
                sphere->y = center.y;
                sphere->z = center.z;
                sphere->w = 0.0f;
                sphere->radius = radius;
            }

            GameVectorLayout vector{};

            reinterpret_cast<GetEntitiesInSphereFn>(static_cast<uintptr_t>(functionAddress))(
                reinterpret_cast<void*>(world),
                &vector,
                reinterpret_cast<void*>(region),
                sphereBuffer,
                useBool,
                listType);

            if (!vector.begin || vector.end <= vector.begin)
            {
                *count = 0;
                return true;
            }

            *count = static_cast<int>((vector.end - vector.begin) / sizeof(void*));

            const auto first = *reinterpret_cast<uintptr_t*>(vector.begin);
            if (first)
            {
                *firstEntity = first;
                ReadPointerSafe(first, firstVtable);
            }

            if (*count > 1)
            {
                *secondEntity = *(reinterpret_cast<uintptr_t*>(vector.begin) + 1);
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *count = -1;
            return false;
        }
    }

    bool ReadPlayerPositionAndRegion(
        DWORD64 gGameEngineAddress,
        DWORD64 getPlayerManagerClientAddress,
        DWORD64 getPlayerIdAddress,
        DWORD64 getPlayerLocationAddress,
        DWORD64 getWorldPositionAddress,
        DWORD64 getRegionAddress,
        Vec3* position,
        uintptr_t* region)
    {
        using GetPlayerManagerClientFn = void* (__fastcall*)(void*);
        using GetPlayerIdFn = unsigned int(__fastcall*)(void*);
        using GetPlayerLocationFn = void* (__fastcall*)(void*, void*, unsigned int);
        using GetWorldPositionFn = void* (__fastcall*)(void*, Vec3*);
        using GetRegionFn = void* (__fastcall*)(void*);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            auto* playerManager = reinterpret_cast<GetPlayerManagerClientFn>(static_cast<uintptr_t>(getPlayerManagerClientAddress))(gameEngine);
            if (!playerManager)
            {
                return false;
            }

            const auto playerId = reinterpret_cast<GetPlayerIdFn>(static_cast<uintptr_t>(getPlayerIdAddress))(gameEngine);
            alignas(16) unsigned char worldVec3[128]{};
            reinterpret_cast<GetPlayerLocationFn>(static_cast<uintptr_t>(getPlayerLocationAddress))(playerManager, worldVec3, playerId);
            reinterpret_cast<GetWorldPositionFn>(static_cast<uintptr_t>(getWorldPositionAddress))(worldVec3, position);

            if (getRegionAddress)
            {
                *region = reinterpret_cast<uintptr_t>(reinterpret_cast<GetRegionFn>(static_cast<uintptr_t>(getRegionAddress))(worldVec3));
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryProbeEntities(const std::string& command)
    {
        float radius = 500.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        auto world = g_worldAddress.load();
        if (!world)
        {
            WorldSearchResult result{};
            if (!FindWorldFull(&result) || !result.world)
            {
                return "{\"type\":\"error\",\"message\":\"world not located\"}\n";
            }

            world = result.world;
        }

        const auto getEntitiesInSphereAddress = ResolveGameExport("?GetEntitiesInSphere@World@GAME@@QEBAXAEAV?$vector@PEAVEntity@GAME@@@mem@@PEAVRegion@2@AEBVSphere@2@_NW4EntityListType@2@@Z");
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getPlayerManagerClientAddress = ResolveGameExport("?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ");
        const auto getPlayerIdAddress = ResolveGameExport("?GetPlayerId@GameEngine@GAME@@QEBAIXZ");
        const auto getPlayerLocationAddress = ResolveGameExport("?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z");
        const auto getWorldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");
        const auto getRegionPositionAddress = ResolveGameExport("?GetRegionPosition@WorldVec3@GAME@@QEBAAEBVVec3@2@XZ");
        const auto getRegionAddress = ResolveGameExport("?GetRegion@WorldVec3@GAME@@QEBAPEAVRegion@2@XZ");

        if (!getEntitiesInSphereAddress || !gGameEngineAddress || !getPlayerManagerClientAddress
            || !getPlayerIdAddress || !getPlayerLocationAddress || !getWorldPositionAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required entity probe symbols not resolved\"}\n";
        }

        uintptr_t player = 0;
        uintptr_t playerManager = 0;
        uintptr_t region = 0;
        Vec3 worldPosition{};
        Vec3 regionPosition{};
        if (!TryReadPlayerPositionsAndRegion(
                gGameEngineAddress,
                getMainPlayerAddress,
                getPlayerManagerClientAddress,
                getPlayerIdAddress,
                getPlayerLocationAddress,
                getWorldPositionAddress,
                getRegionPositionAddress,
                getRegionAddress,
                &player,
                &playerManager,
                &worldPosition,
                &regionPosition,
                &region))
        {
            return "{\"type\":\"error\",\"message\":\"failed to read player position\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"probe_entities\"";
        json << ",\"world\":\"" << ToHex(world) << "\"";
        json << ",\"region\":\"" << ToHex(region) << "\"";
        json << ",\"worldPosition\":{\"x\":" << worldPosition.x << ",\"y\":" << worldPosition.y << ",\"z\":" << worldPosition.z << "}";
        json << ",\"regionPosition\":{\"x\":" << regionPosition.x << ",\"y\":" << regionPosition.y << ",\"z\":" << regionPosition.z << "}";
        json << ",\"radius\":" << radius;
        json << ",\"hits\":[";

        auto totalCalls = 0;
        auto exceptionCount = 0;
        auto firstHit = true;
        const Vec3 centers[2] = { regionPosition, worldPosition };
        for (int centerMode = 0; centerMode <= 1; ++centerMode)
        for (int layout = 0; layout <= 2; ++layout)
        {
            for (int useBoolIndex = 0; useBoolIndex <= 1; ++useBoolIndex)
            {
                const auto useBool = useBoolIndex == 1;
                for (int regionMode = 0; regionMode <= 1; ++regionMode)
                {
                    const auto regionArg = regionMode == 0 ? region : 0;
                    for (int listType = 0; listType <= 7; ++listType)
                    {
                        int count = 0;
                        uintptr_t firstEntity = 0;
                        uintptr_t firstVtable = 0;
                        uintptr_t secondEntity = 0;
                        CallGetEntitiesInSphere(
                            world,
                            getEntitiesInSphereAddress,
                            regionArg,
                            centers[centerMode],
                            radius,
                            listType,
                            useBool,
                            layout,
                            &count,
                            &firstEntity,
                            &firstVtable,
                            &secondEntity);
                        totalCalls++;

                        if (count < 0)
                        {
                            exceptionCount++;
                            continue;
                        }

                        if (count == 0)
                        {
                            continue;
                        }

                        if (!firstHit)
                        {
                            json << ",";
                        }

                        firstHit = false;
                        json << "{\"centerMode\":" << centerMode
                             << ",\"layout\":" << layout
                             << ",\"useBool\":" << (useBool ? "true" : "false")
                             << ",\"useRegion\":" << (regionMode == 0 ? "true" : "false")
                             << ",\"listType\":" << listType
                             << ",\"count\":" << count;
                        if (firstEntity)
                        {
                            json << ",\"firstEntity\":\"" << ToHex(firstEntity) << "\",\"firstVtable\":\"" << ToHex(firstVtable) << "\"";
                        }

                        if (secondEntity)
                        {
                            json << ",\"secondEntity\":\"" << ToHex(secondEntity) << "\"";
                        }

                        json << "}";
                    }
                }
            }
        }

        json << "],\"totalCalls\":" << totalCalls << ",\"exceptionCount\":" << exceptionCount << "}\n";
        return json.str();
    }

    uintptr_t TryGetEntityClassInfo(uintptr_t entity)
    {
        using GetClassInfoFn = const void* (__fastcall*)(void*);

        __try
        {
            const auto vtable = *reinterpret_cast<uintptr_t*>(entity);
            if (!vtable)
            {
                return 0;
            }

            const auto function = *reinterpret_cast<uintptr_t*>(vtable);
            if (!function)
            {
                return 0;
            }

            return reinterpret_cast<uintptr_t>(reinterpret_cast<GetClassInfoFn>(function)(reinterpret_cast<void*>(entity)));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool TryReadEntityClassName(uintptr_t classInfo, char* buffer, size_t bufferSize)
    {
        __try
        {
            const auto namePointer = *reinterpret_cast<const char* const*>(classInfo + 8);
            if (!namePointer)
            {
                return false;
            }

            size_t index = 0;
            while (index + 1 < bufferSize && namePointer[index] != '\0')
            {
                buffer[index] = namePointer[index];
                ++index;
            }

            buffer[index] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsMonsterClassInfo(uintptr_t classInfo, uintptr_t monsterClassInfo)
    {
        __try
        {
            for (int depth = 0; classInfo && depth < 24; ++depth)
            {
                if (classInfo == monsterClassInfo)
                {
                    return true;
                }

                classInfo = *reinterpret_cast<uintptr_t*>(classInfo + 16);
            }

            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct EntityTypeEntry
    {
        char name[48];
        int count;
    };

    struct EntityTypeSummary
    {
        int total;
        int readable;
        int monsterCount;
        int entryCount;
        EntityTypeEntry entries[40];
    };

    bool AnalyzeEntityVector(uintptr_t vectorBegin, uintptr_t vectorEnd, uintptr_t monsterClassInfo, EntityTypeSummary* summary)
    {
        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                summary->total++;
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!classInfo)
                {
                    continue;
                }

                summary->readable++;
                if (IsMonsterClassInfo(classInfo, monsterClassInfo))
                {
                    summary->monsterCount++;
                }

                char name[48]{};
                if (!TryReadEntityClassName(classInfo, name, sizeof(name)))
                {
                    continue;
                }

                bool found = false;
                for (int i = 0; i < summary->entryCount; ++i)
                {
                    bool same = true;
                    for (int c = 0; c < 48; ++c)
                    {
                        if (summary->entries[i].name[c] != name[c])
                        {
                            same = false;
                            break;
                        }
                    }

                    if (same)
                    {
                        summary->entries[i].count++;
                        found = true;
                        break;
                    }
                }

                if (!found && summary->entryCount < 40)
                {
                    auto& entry = summary->entries[summary->entryCount++];
                    for (int c = 0; c < 48; ++c)
                    {
                        entry.name[c] = name[c];
                    }

                    entry.count = 1;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return true;
    }

    int KillMonstersInVector(uintptr_t vectorBegin, uintptr_t vectorEnd, uintptr_t monsterClassInfo, uintptr_t subtractLifeAddress, int* monsterFound)
    {
        int killed = 0;
        if (monsterFound)
        {
            *monsterFound = 0;
        }

        using SubtractLifeFn = void(__fastcall*)(void*, float, const void*, bool, bool);

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!IsMonsterClassInfo(classInfo, monsterClassInfo))
                {
                    continue;
                }

                if (monsterFound)
                {
                    (*monsterFound)++;
                }

                alignas(8) unsigned char damageType[64]{};
                reinterpret_cast<SubtractLifeFn>(static_cast<uintptr_t>(subtractLifeAddress))(
                    reinterpret_cast<void*>(entity),
                    1.0e9f,
                    damageType,
                    false,
                    false);
                killed++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return killed;
    }

    struct EntityQueryContext
    {
        uintptr_t world{};
        uintptr_t region{};
        Vec3 center{};
        Vec3 worldPosition{};
        uintptr_t getEntitiesInSphereAddress{};
        uintptr_t subtractLifeAddress{};
        uintptr_t monsterClassInfoAddress{};
    };

    GameVectorLayout& GetSharedEntityVector()
    {
        static thread_local GameVectorLayout shared{};
        return shared;
    }

    bool QueryEntitiesSharedInner(
        uintptr_t world,
        uintptr_t functionAddress,
        uintptr_t region,
        const Vec3& center,
        float radius,
        GameVectorLayout* vector)
    {
        using GetEntitiesInSphereFn = void(__fastcall*)(void*, void*, void*, const void*, bool, int);

        __try
        {
            struct Sphere
            {
                Vec3 center;
                float radius;
            };

            Sphere sphere{ center, radius };
            auto* shared = &GetSharedEntityVector();
            shared->end = shared->begin;

            reinterpret_cast<GetEntitiesInSphereFn>(static_cast<uintptr_t>(functionAddress))(
                reinterpret_cast<void*>(world),
                shared,
                reinterpret_cast<void*>(region),
                &sphere,
                false,
                0);

            *vector = *shared;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *vector = GameVectorLayout{};
            return false;
        }
    }

    bool QueryEntitiesShared(const EntityQueryContext& context, float radius, GameVectorLayout* vector)
    {
        std::lock_guard<std::mutex> guard(g_entityQueryMutex);
        return QueryEntitiesSharedInner(
            context.world,
            context.getEntitiesInSphereAddress,
            context.region,
            context.center,
            radius,
            vector);
    }

    bool PrepareEntityQuery(EntityQueryContext* context, std::string* error)
    {
        auto world = g_worldAddress.load();
        if (!world)
        {
            WorldSearchResult result{};
            if (!FindWorldFull(&result) || !result.world)
            {
                *error = "{\"type\":\"error\",\"message\":\"world not located\"}\n";
                return false;
            }

            world = result.world;
        }

        context->world = world;
        context->getEntitiesInSphereAddress = ResolveGameExport("?GetEntitiesInSphere@World@GAME@@QEBAXAEAV?$vector@PEAVEntity@GAME@@@mem@@PEAVRegion@2@AEBVSphere@2@_NW4EntityListType@2@@Z");
        context->subtractLifeAddress = ResolveGameExport("?SubtractLife@Character@GAME@@QEAAXMAEBUPlayStatsDamageType@2@_N_N@Z");
        context->monsterClassInfoAddress = ResolveGameExport("?classInfo@Monster@GAME@@1VRTTI_ClassInfo@2@B");

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getPlayerManagerClientAddress = ResolveGameExport("?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ");
        const auto getPlayerIdAddress = ResolveGameExport("?GetPlayerId@GameEngine@GAME@@QEBAIXZ");
        const auto getPlayerLocationAddress = ResolveGameExport("?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z");
        const auto getWorldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");
        const auto getRegionPositionAddress = ResolveGameExport("?GetRegionPosition@WorldVec3@GAME@@QEBAAEBVVec3@2@XZ");
        const auto getRegionAddress = ResolveGameExport("?GetRegion@WorldVec3@GAME@@QEBAPEAVRegion@2@XZ");

        if (!context->getEntitiesInSphereAddress || !context->subtractLifeAddress || !context->monsterClassInfoAddress
            || !gGameEngineAddress || !getMainPlayerAddress || !getPlayerManagerClientAddress
            || !getPlayerIdAddress || !getPlayerLocationAddress || !getWorldPositionAddress)
        {
            *error = "{\"type\":\"error\",\"message\":\"required entity symbols not resolved\"}\n";
            return false;
        }

        uintptr_t player = 0;
        uintptr_t playerManager = 0;
        Vec3 worldPosition{};
        Vec3 regionPosition{};
        if (!TryReadPlayerPositionsAndRegion(
                gGameEngineAddress,
                getMainPlayerAddress,
                getPlayerManagerClientAddress,
                getPlayerIdAddress,
                getPlayerLocationAddress,
                getWorldPositionAddress,
                getRegionPositionAddress,
                getRegionAddress,
                &player,
                &playerManager,
                &worldPosition,
                &regionPosition,
                &context->region))
        {
            *error = "{\"type\":\"error\",\"message\":\"failed to read player state\"}\n";
            return false;
        }

        context->center = regionPosition;
        context->worldPosition = worldPosition;
        return true;
    }

    std::string TryListEntityTypes(const std::string& command)
    {
        float radius = 100.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        EntityTypeSummary summary{};
        if (vector.begin && vector.end > vector.begin)
        {
            AnalyzeEntityVector(vector.begin, vector.end, context.monsterClassInfoAddress, &summary);
        }

        std::ostringstream json;
        json << "{\"type\":\"list_entity_types\"";
        json << ",\"world\":\"" << ToHex(context.world) << "\"";
        json << ",\"region\":\"" << ToHex(context.region) << "\"";
        json << ",\"radius\":" << radius;
        json << ",\"center\":{\"x\":" << context.center.x << ",\"y\":" << context.center.y << ",\"z\":" << context.center.z << "}";
        json << ",\"total\":" << summary.total;
        json << ",\"readable\":" << summary.readable;
        json << ",\"monsterCount\":" << summary.monsterCount;
        json << ",\"types\":[";
        for (int i = 0; i < summary.entryCount; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"name\":\"" << summary.entries[i].name << "\",\"count\":" << summary.entries[i].count << "}";
        }

        json << "]}\n";
        return json.str();
    }

    std::string TryKillMonsters(const std::string& command)
    {
        float radius = 50.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        auto killed = 0;
        auto monsterFound = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            killed = KillMonstersInVector(vector.begin, vector.end, context.monsterClassInfoAddress, context.subtractLifeAddress, &monsterFound);
        }

        std::ostringstream json;
        json << "{\"type\":\"kill_monsters\"";
        json << ",\"radius\":" << radius;
        json << ",\"found\":" << monsterFound;
        json << ",\"killed\":" << killed;
        json << "}\n";
        return json.str();
    }
    bool ReadMoneyInternal(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, DWORD64 getCurrentMoneyAddress, unsigned int* money)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        using GetCurrentMoneyFn = unsigned int(__fastcall*)(void*);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
            const auto getCurrentMoney = reinterpret_cast<GetCurrentMoneyFn>(static_cast<uintptr_t>(getCurrentMoneyAddress));
            auto* player = getMainPlayer(gameEngine);
            if (!player)
            {
                return false;
            }

            *money = getCurrentMoney(player);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetMoneyInternal(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, DWORD64 getCurrentMoneyAddress, DWORD64 addMoneyAddress, DWORD64 subtractMoneyAddress, unsigned int targetMoney, unsigned int* finalMoney)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        using GetCurrentMoneyFn = unsigned int(__fastcall*)(void*);
        using AddMoneyFn = void(__fastcall*)(void*, unsigned int);
        using SubtractMoneyFn = bool(__fastcall*)(void*, unsigned int, unsigned int);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
            const auto getCurrentMoney = reinterpret_cast<GetCurrentMoneyFn>(static_cast<uintptr_t>(getCurrentMoneyAddress));
            auto* player = getMainPlayer(gameEngine);
            if (!player)
            {
                return false;
            }

            const auto currentMoney = getCurrentMoney(player);
            if (targetMoney > currentMoney)
            {
                const auto addMoney = reinterpret_cast<AddMoneyFn>(static_cast<uintptr_t>(addMoneyAddress));
                addMoney(player, targetMoney - currentMoney);
            }
            else if (targetMoney < currentMoney)
            {
                const auto subtractMoney = reinterpret_cast<SubtractMoneyFn>(static_cast<uintptr_t>(subtractMoneyAddress));
                if (!subtractMoney(player, currentMoney - targetMoney, 0))
                {
                    return false;
                }
            }

            *finalMoney = getCurrentMoney(player);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    
    bool SetGodModeInternal(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, DWORD64 setGodAddress, DWORD64 setInvincibleAddress, bool enable)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        using SetFlagFn = void(__fastcall*)(void*, bool);

        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
            auto* player = getMainPlayer(gameEngine);
            if (!player)
            {
                return false;
            }

            const auto setGod = reinterpret_cast<SetFlagFn>(static_cast<uintptr_t>(setGodAddress));
            const auto setInvincible = reinterpret_cast<SetFlagFn>(static_cast<uintptr_t>(setInvincibleAddress));
            setGod(player, enable);
            setInvincible(player, enable);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    
    std::string TrySetGodMode(const std::string& command)
    {
        const auto separator = command.find(':');
        if (separator == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"god_mode format is god_mode:<true|false>\"}\n";
        }

        const auto value = command.substr(separator + 1);
        const bool enable = (value == "true" || value == "1" || value == "yes");

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto setGodAddress = ResolveGameExport("?SetGod@Character@GAME@@QEAAX_N@Z");
        const auto setInvincibleAddress = ResolveGameExport("?SetInvincible@Character@GAME@@QEAAX_N@Z");
        if (!gGameEngineAddress || !getMainPlayerAddress || !setGodAddress || !setInvincibleAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required god mode symbols not resolved\"}\n";
        }

        if (!SetGodModeInternal(gGameEngineAddress, getMainPlayerAddress, setGodAddress, setInvincibleAddress, enable))
        {
            return "{\"type\":\"error\",\"message\":\"failed to apply god mode through game api\"}\n";
        }

        g_godModeEnabled.store(enable);

        std::ostringstream json;
        json << "{\"type\":\"god_mode\",\"enabled\":" << (enable ? "true" : "false") << "}\n";
        return json.str();
    }
    
    std::string TryGetGodModeStatus()
    {
        std::ostringstream json;
        json << "{\"type\":\"god_mode\",\"enabled\":" << (g_godModeEnabled.load() ? "true" : "false") << "}\n";
        return json.str();
    }
    
    bool InstaKillMonstersInternal(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, float killRadius)
    {
        using GetMainPlayerFn = void* (__fastcall*)(void*);
        
        __try
        {
            auto* gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (!gameEngine)
            {
                return false;
            }

            const auto getMainPlayer = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress));
            auto* player = getMainPlayer(gameEngine);
            if (!player)
            {
                return false;
            }
            
            // 已确认可用的真实符号（见逆向/DPYes逆向分析报告.md 与 Game.dll 导出表验证）：
            //   World::GetEntitiesInSphere(vector<Entity*>&, Region*, const Sphere&, bool, EntityListType)
            //   Monster::GetMonsterClassification
            //   Character::SubtractLife(float, const PlayStatsDamageType&, bool, bool)
            //   CombatManager::ApplyDamage(float, const PlayStatsDamageType&, CombatAttributeType, const vector<uint>&)
            //
            // 当前阻塞点：没有直接获取 World* 的导出符号，需要先定位 GameEngine 中 World 的成员偏移
            // 或通过 RTTI/vtable 扫描定位 World 实例。World 定位完成后即可：
            // 1. 用 World::GetEntitiesInSphere 取玩家周围实体
            // 2. 通过 Monster::classInfo RTTI 判断实体类型
            // 3. 对怪物调用 Character::SubtractLife 或 CombatManager::ApplyDamage
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    
    std::string TryInstaKill(const std::string& command)
    {
        const auto separator = command.find(':');
        float radius = 50.0f; // 默认半径
        
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }
        
        // 阻塞点：World* 尚未定位（无导出符号），实体遍历与伤害施加逻辑待 World 定位后实装。
        
        if (!InstaKillMonstersInternal(0, 0, radius))
        {
            return "{\"type\":\"error\",\"message\":\"failed to insta-kill monsters\"}\n";
        }
        
        std::ostringstream json;
        json << "{\"type\":\"insta_kill\",\"radius\":" << radius << ",\"status\":\"executed\"}\n";
        return json.str();
    }
    
    void AutoKillNearbyMonsters()
    {
        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            g_autoKillLastError.store(1);
            if (g_autoKillTick.load() % 8 == 0)
            {
                Log(L"auto_kill: prepare failed");
            }

            return;
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, 25.0f, &vector))
        {
            g_autoKillLastError.store(2);
            if (g_autoKillTick.load() % 8 == 0)
            {
                Log(L"auto_kill: entity query failed");
            }

            return;
        }

        const auto count = vector.begin && vector.end > vector.begin
            ? static_cast<int>((vector.end - vector.begin) / sizeof(void*))
            : 0;
        g_autoKillLastQuery.store(count);

        auto killed = 0;
        if (count > 0)
        {
            killed = KillMonstersInVector(vector.begin, vector.end, context.monsterClassInfoAddress, context.subtractLifeAddress, nullptr);
        }

        g_autoKillLastKilled.store(killed);
        g_autoKillLastError.store(0);

        if (g_autoKillTick.load() % 13 == 0)
        {
            std::wstringstream stream;
            stream << L"auto_kill: entities=" << count << L" killed=" << killed
                   << L" world=0x" << std::hex << context.world << std::dec
                   << L" region=0x" << std::hex << context.region;
            Log(stream.str());
        }
    }

    DWORD WINAPI AutoKillThread(LPVOID)
    {
        while (!g_stop.load())
        {
            if (g_oneShotEnabled.load())
            {
                g_autoKillTick.fetch_add(1);
                AutoKillNearbyMonsters();
            }

            Sleep(150);
        }

        return 0;
    }

    std::string TrySetOneShot(const std::string& command)
    {
        const auto separator = command.find(':');
        if (separator == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"one_shot format is one_shot:<true|false>\"}\n";
        }

        const auto value = command.substr(separator + 1);
        const bool enable = (value == "true" || value == "1" || value == "yes");

        g_oneShotEnabled.store(enable);

        std::ostringstream json;
        json << "{\"type\":\"one_shot\",\"enabled\":" << (enable ? "true" : "false") << ",\"mode\":\"auto_kill_nearby\"}\n";
        return json.str();
    }

    std::string TryAutoKillStatus()
    {
        std::ostringstream json;
        json << "{\"type\":\"auto_kill_status\"";
        json << ",\"enabled\":" << (g_oneShotEnabled.load() ? "true" : "false");
        json << ",\"world\":\"" << ToHex(g_worldAddress.load()) << "\"";
        json << ",\"tick\":" << g_autoKillTick.load();
        json << ",\"lastQuery\":" << g_autoKillLastQuery.load();
        json << ",\"lastKilled\":" << g_autoKillLastKilled.load();
        json << ",\"lastError\":" << g_autoKillLastError.load();
        json << "}\n";
        return json.str();
    }

    bool TryCallVoidMemberFunction(DWORD64 address, void* instance)
    {
        using VoidMemberFn = void(__fastcall*)(void*);

        __try
        {
            reinterpret_cast<VoidMemberFn>(static_cast<uintptr_t>(address))(instance);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryReadGameStatePointers(DWORD64 gGameEngineAddress, DWORD64 getMainPlayerAddress, void** gameEngine, void** player)
    {
        __try
        {
            *gameEngine = *reinterpret_cast<void**>(static_cast<uintptr_t>(gGameEngineAddress));
            if (*gameEngine)
            {
                using GetMainPlayerFn = void* (__fastcall*)(void*);
                *player = reinterpret_cast<GetMainPlayerFn>(static_cast<uintptr_t>(getMainPlayerAddress))(*gameEngine);
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryIsInGame()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto isGameLoadingAddress = ResolveGameExport("?IsGameLoading@GameEngine@GAME@@QEBA_NXZ");

        if (!gGameEngineAddress || !getMainPlayerAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required game state symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player))
        {
            return "{\"type\":\"error\",\"message\":\"exception while reading game state\"}\n";
        }

        bool loading = false;
        if (gameEngine && isGameLoadingAddress)
        {
            TryReadBoolMemberFunction(isGameLoadingAddress, gameEngine, &loading);
        }

        std::ostringstream json;
        json << "{\"type\":\"is_in_game\"";
        json << ",\"gameEngine\":" << (gameEngine ? "true" : "false");
        json << ",\"player\":" << (player ? "true" : "false");
        json << ",\"loading\":" << (loading ? "true" : "false");
        json << ",\"inGame\":" << (player ? "true" : "false");
        json << "}\n";
        return json.str();
    }

    std::string TryExitToMenu()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto exitPlayingModeAddress = ResolveGameExport("?ExitPlayingMode@GameEngine@GAME@@QEAAXXZ");
        if (!gGameEngineAddress || !exitPlayingModeAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required exit symbols not resolved\"}\n";
        }

        uintptr_t gameEngineValue = 0;
        if (!ReadPointerSafe(static_cast<uintptr_t>(gGameEngineAddress), &gameEngineValue) || !gameEngineValue)
        {
            return "{\"type\":\"error\",\"message\":\"game engine is null\"}\n";
        }

        if (!TryCallVoidMemberFunction(exitPlayingModeAddress, reinterpret_cast<void*>(gameEngineValue)))
        {
            return "{\"type\":\"error\",\"message\":\"ExitPlayingMode call failed\"}\n";
        }

        return "{\"type\":\"exit_to_menu\",\"status\":\"requested\"}\n";
    }

    bool TryGetItemClassification(uintptr_t entity, DWORD64 address, int* value)
    {
        using GetClassificationFn = int(__fastcall*)(void*, bool);

        __try
        {
            *value = reinterpret_cast<GetClassificationFn>(static_cast<uintptr_t>(address))(reinterpret_cast<void*>(entity), false);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryGetItemTagText(uintptr_t entity, DWORD64 getItemTextTagAddress, char* buffer, size_t bufferSize)
    {
        using GetTextTagFn = const void* (__fastcall*)(void*);

        __try
        {
            const auto tagString = reinterpret_cast<GetTextTagFn>(static_cast<uintptr_t>(getItemTextTagAddress))(reinterpret_cast<void*>(entity));
            if (!tagString)
            {
                return false;
            }

            const auto* bytes = reinterpret_cast<const unsigned char*>(tagString);
            const auto size = *reinterpret_cast<const size_t*>(bytes + 16);
            if (size == 0 || size > 512)
            {
                return false;
            }

            const auto capacity = *reinterpret_cast<const size_t*>(bytes + 24);
            const char* data = capacity >= 16
                ? *reinterpret_cast<const char* const*>(bytes)
                : reinterpret_cast<const char*>(bytes);
            if (!data)
            {
                return false;
            }

            size_t index = 0;
            while (index + 1 < bufferSize && index < size)
            {
                buffer[index] = data[index];
                ++index;
            }

            buffer[index] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct ItemProbeEntry
    {
        uintptr_t entity;
        int classification;
        char tag[128];
    };

    bool ProcessOneItem(
        uintptr_t entity,
        uintptr_t itemClassInfo,
        DWORD64 classificationAddress,
        DWORD64 tagAddress,
        ItemProbeEntry* entry)
    {
        const auto classInfo = TryGetEntityClassInfo(entity);
        if (!IsMonsterClassInfo(classInfo, itemClassInfo))
        {
            return false;
        }

        entry->entity = entity;
        entry->classification = -1;
        entry->tag[0] = '\0';

        if (classificationAddress)
        {
            int value = 0;
            if (TryGetItemClassification(entity, classificationAddress, &value))
            {
                entry->classification = value;
            }
        }

        if (tagAddress && !TryGetItemTagText(entity, tagAddress, entry->tag, sizeof(entry->tag)))
        {
            entry->tag[0] = '\0';
        }

        return true;
    }

    int ProbeItemsInVector(
        uintptr_t vectorBegin,
        uintptr_t vectorEnd,
        uintptr_t itemClassInfo,
        DWORD64 classificationAddress,
        DWORD64 tagAddress,
        ItemProbeEntry* entries,
        int maxEntries)
    {
        int count = 0;

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd; position += sizeof(uintptr_t))
            {
                if (count >= maxEntries)
                {
                    break;
                }

                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                if (ProcessOneItem(entity, itemClassInfo, classificationAddress, tagAddress, &entries[count]))
                {
                    count++;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    std::string TryProbeItems(const std::string& command)
    {
        float radius = 60.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto itemClassInfoAddress = ResolveGameExport("?classInfo@Item@GAME@@1VRTTI_ClassInfo@2@B");
        const auto classificationAddress = ResolveGameExport("?GetItemClassification@Item@GAME@@UEBA?AW4ItemClassification@2@_N@Z");
        const auto tagAddress = ResolveGameExport("?GetItemTextTag@Item@GAME@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ");
        if (!itemClassInfoAddress)
        {
            return "{\"type\":\"error\",\"message\":\"Item class info not resolved\"}\n";
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ItemProbeEntry entries[24]{};
        auto count = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            count = ProbeItemsInVector(vector.begin, vector.end, itemClassInfoAddress, classificationAddress, tagAddress, entries, 24);
        }

        std::ostringstream json;
        json << "{\"type\":\"probe_items\"";
        json << ",\"radius\":" << radius;
        json << ",\"count\":" << count;
        json << ",\"tagAvailable\":" << (tagAddress ? "true" : "false");
        json << ",\"classificationAvailable\":" << (classificationAddress ? "true" : "false");
        json << ",\"items\":[";
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"entity\":\"" << ToHex(entries[i].entity) << "\""
                 << ",\"classification\":" << entries[i].classification
                 << ",\"tag\":\"" << entries[i].tag << "\"}";
        }

        json << "]}\n";
        return json.str();
    }

    bool TryCallGetItemReplicaInfo(uintptr_t entity, DWORD64 functionAddress, unsigned char* buffer, size_t bufferSize)
    {
        using GetReplicaInfoFn = void(__fastcall*)(void*, void*);

        __try
        {
            memset(buffer, 0, bufferSize);
            reinterpret_cast<GetReplicaInfoFn>(static_cast<uintptr_t>(functionAddress))(reinterpret_cast<void*>(entity), buffer);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryCallGetUIDisplayText(uintptr_t entity, DWORD64 functionAddress, void* player, GameVectorLayout* vector)
    {
        using GetDisplayTextFn = void(__fastcall*)(void*, void*, void*, bool);

        __try
        {
            GameVectorLayout local{};
            reinterpret_cast<GetDisplayTextFn>(static_cast<uintptr_t>(functionAddress))(
                reinterpret_cast<void*>(entity),
                player,
                &local,
                false);
            *vector = local;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct PointerStringHit
    {
        int pointerOffset;
        int wide;
        char text[192];
    };

    bool TryReadStringCandidate(uintptr_t address, PointerStringHit* hit)
    {
        __try
        {
            char asciiBuffer[128]{};
            memcpy(asciiBuffer, reinterpret_cast<const void*>(address), sizeof(asciiBuffer) - 1);

            int printable = 0;
            int length = 0;
            for (int i = 0; i < 127 && asciiBuffer[i] != 0; ++i)
            {
                if (asciiBuffer[i] >= 0x20 && asciiBuffer[i] < 0x7f && asciiBuffer[i] != '"' && asciiBuffer[i] != '\\')
                {
                    printable++;
                }

                length++;
            }

            if (length >= 3 && printable == length && length < static_cast<int>(sizeof(hit->text)))
            {
                memcpy(hit->text, asciiBuffer, length);
                hit->text[length] = '\0';
                hit->wide = 0;
                return true;
            }

            wchar_t wideBuffer[64]{};
            memcpy(wideBuffer, reinterpret_cast<const void*>(address), sizeof(wideBuffer) - sizeof(wchar_t));

            printable = 0;
            length = 0;
            for (int i = 0; i < 63 && wideBuffer[i] != 0; ++i)
            {
                const auto ch = wideBuffer[i];
                if ((ch >= 0x20 && ch < 0x7f) || (ch >= 0x4e00 && ch <= 0x9fff) || (ch >= 0x3000 && ch <= 0x30ff))
                {
                    printable++;
                }

                length++;
            }

            if (length >= 3 && printable * 10 >= length * 8)
            {
                const auto converted = WideCharToMultiByte(CP_UTF8, 0, wideBuffer, length, hit->text, sizeof(hit->text) - 1, nullptr, nullptr);
                if (converted > 0)
                {
                    hit->text[converted] = '\0';
                    hit->wide = 1;
                    return true;
                }
            }

            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    int ScanBufferForPointerStrings(const unsigned char* buffer, size_t bufferSize, int maxHits, PointerStringHit* hits)
    {
        int count = 0;
        for (size_t offset = 0; offset + sizeof(uintptr_t) <= bufferSize && count < maxHits; offset += sizeof(uintptr_t))
        {
            const auto candidate = *reinterpret_cast<const uintptr_t*>(buffer + offset);
            if (candidate < 0x10000 || candidate > 0x00007FFFFFFFFFFF)
            {
                continue;
            }

            if (TryReadStringCandidate(candidate, &hits[count]))
            {
                hits[count].pointerOffset = static_cast<int>(offset);
                count++;
            }
        }

        return count;
    }

    bool TryReadStdStringAt(const unsigned char* buffer, size_t bufferSize, size_t offset, char* text, size_t textSize)
    {
        __try
        {
            if (offset + 32 > bufferSize)
            {
                return false;
            }

            const auto length = *reinterpret_cast<const size_t*>(buffer + offset + 16);
            if (length == 0 || length > 160)
            {
                return false;
            }

            const auto capacity = *reinterpret_cast<const size_t*>(buffer + offset + 24);
            const char* data = capacity >= 16
                ? *reinterpret_cast<const char* const*>(buffer + offset)
                : reinterpret_cast<const char*>(buffer + offset);
            if (!data)
            {
                return false;
            }

            size_t index = 0;
            while (index + 1 < textSize && index < length)
            {
                text[index] = data[index];
                ++index;
            }

            text[index] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct ItemDetailEntry
    {
        uintptr_t entity;
        unsigned int itemId;
        unsigned int altId178;
        unsigned int altId17c;
        int replicaValid;
        int displayValid;
        size_t displayBytes;
        int replicaStringCount;
        int displayStringCount;
        char basePath[160];
        char prefixPath[160];
        char suffixPath[160];
        char displayName[192];
        unsigned char replicaBuffer[512];
        unsigned char displayBuffer[256];
        PointerStringHit replicaStrings[6];
        PointerStringHit displayStrings[6];
    };

    int ProbeItemDetailsInVector(
        uintptr_t vectorBegin,
        uintptr_t vectorEnd,
        uintptr_t itemClassInfo,
        DWORD64 replicaInfoAddress,
        DWORD64 displayTextAddress,
        void* player,
        ItemDetailEntry* entries,
        int maxEntries)
    {
        int count = 0;

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd && count < maxEntries; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!IsMonsterClassInfo(classInfo, itemClassInfo))
                {
                    continue;
                }

                auto& entry = entries[count];
                entry.entity = entity;
                entry.itemId = 0;
                entry.altId178 = 0;
                entry.altId17c = 0;
                entry.replicaValid = 0;
                entry.displayValid = 0;
                entry.displayBytes = 0;
                entry.replicaStringCount = 0;
                entry.displayStringCount = 0;
                entry.basePath[0] = '\0';
                entry.prefixPath[0] = '\0';
                entry.suffixPath[0] = '\0';
                entry.displayName[0] = '\0';

                if (replicaInfoAddress)
                {
                    if (TryCallGetItemReplicaInfo(entity, replicaInfoAddress, entry.replicaBuffer, sizeof(entry.replicaBuffer)))
                    {
                        entry.replicaValid = 1;
                        entry.itemId = *reinterpret_cast<const unsigned int*>(entry.replicaBuffer);
                        TryReadStdStringAt(entry.replicaBuffer, sizeof(entry.replicaBuffer), 8, entry.basePath, sizeof(entry.basePath));
                        TryReadStdStringAt(entry.replicaBuffer, sizeof(entry.replicaBuffer), 40, entry.prefixPath, sizeof(entry.prefixPath));
                        TryReadStdStringAt(entry.replicaBuffer, sizeof(entry.replicaBuffer), 72, entry.suffixPath, sizeof(entry.suffixPath));
                        entry.replicaStringCount = ScanBufferForPointerStrings(entry.replicaBuffer, 256, 6, entry.replicaStrings);
                    }
                }

                uintptr_t altValue = 0;
                if (ReadPointerSafe(entity + 0x178, &altValue))
                {
                    entry.altId178 = static_cast<unsigned int>(altValue);
                }

                if (ReadPointerSafe(entity + 0x17c, &altValue))
                {
                    entry.altId17c = static_cast<unsigned int>(altValue);
                }

                if (displayTextAddress && player)
                {
                    GameVectorLayout textVector{};
                    if (TryCallGetUIDisplayText(entity, displayTextAddress, player, &textVector)
                        && textVector.begin
                        && textVector.end > textVector.begin)
                    {
                        const auto totalBytes = static_cast<size_t>(textVector.end - textVector.begin);
                        const auto copyBytes = totalBytes < sizeof(entry.displayBuffer) ? totalBytes : sizeof(entry.displayBuffer);
                        memcpy(entry.displayBuffer, reinterpret_cast<const void*>(textVector.begin), copyBytes);
                        entry.displayValid = 1;
                        entry.displayBytes = totalBytes;
                        entry.displayStringCount = ScanBufferForPointerStrings(entry.displayBuffer, copyBytes < 128 ? copyBytes : 128, 6, entry.displayStrings);

                        for (int hitIndex = 0; hitIndex < entry.displayStringCount; ++hitIndex)
                        {
                            if (entry.displayStrings[hitIndex].wide)
                            {
                                strncpy_s(entry.displayName, sizeof(entry.displayName), entry.displayStrings[hitIndex].text, _TRUNCATE);
                                break;
                            }
                        }
                    }
                }

                count++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    void AppendPointerStringHits(std::ostringstream& json, const PointerStringHit* hits, int count)
    {
        json << "[";
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"offset\":" << hits[i].pointerOffset
                 << ",\"wide\":" << (hits[i].wide ? "true" : "false")
                 << ",\"text\":\"" << hits[i].text << "\"}";
        }

        json << "]";
    }

    std::string TryProbeItemDetail(const std::string& command)
    {
        float radius = 80.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto itemClassInfoAddress = ResolveGameExport("?classInfo@Item@GAME@@1VRTTI_ClassInfo@2@B");
        const auto replicaInfoAddress = ResolveGameExport("?GetItemReplicaInfo@Item@GAME@@UEBAXAEAUItemReplicaInfo@2@@Z");
        const auto displayTextAddress = ResolveGameExport("?GetUIDisplayText@Item@GAME@@UEBAXPEBVCharacter@2@AEAV?$vector@UGameTextLine@GAME@@@mem@@_N@Z");
        if (!itemClassInfoAddress)
        {
            return "{\"type\":\"error\",\"message\":\"Item class info not resolved\"}\n";
        }

        void* player = nullptr;
        void* gameEngine = nullptr;
        if (gGameEngineAddress && getMainPlayerAddress)
        {
            TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player);
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ItemDetailEntry entries[3]{};
        auto count = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            count = ProbeItemDetailsInVector(
                vector.begin,
                vector.end,
                itemClassInfoAddress,
                replicaInfoAddress,
                displayTextAddress,
                player,
                entries,
                3);
        }

        std::ostringstream json;
        json << "{\"type\":\"probe_item_detail\"";
        json << ",\"radius\":" << radius;
        json << ",\"displayTextAvailable\":" << (displayTextAddress ? "true" : "false");
        json << ",\"player\":\"" << ToHex(reinterpret_cast<uintptr_t>(player)) << "\"";
        json << ",\"count\":" << count;
        json << ",\"items\":[";

        for (int i = 0; i < count; ++i)
        {
            const auto& entry = entries[i];
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"entity\":\"" << ToHex(entry.entity) << "\"";

            if (entry.replicaValid)
            {
                json << ",\"replicaHex\":\"";
                for (size_t b = 0; b + 1 < 128; b += 2)
                {
                    char byteText[8]{};
                    sprintf_s(byteText, "%02x%02x", entry.replicaBuffer[b], entry.replicaBuffer[b + 1]);
                    json << byteText;
                }

                json << "\"";
                json << ",\"replicaTexts\":";
                AppendPointerStringHits(json, entry.replicaStrings, entry.replicaStringCount);
            }

            if (entry.displayValid)
            {
                json << ",\"displayTotalBytes\":" << entry.displayBytes;
                json << ",\"displayHex\":\"";
                const auto dumpBytes = entry.displayBytes < 192 ? entry.displayBytes : static_cast<size_t>(192);
                for (size_t b = 0; b + 1 < dumpBytes; b += 2)
                {
                    char byteText[8]{};
                    sprintf_s(byteText, "%02x%02x", entry.displayBuffer[b], entry.displayBuffer[b + 1]);
                    json << byteText;
                }

                json << "\"";
                json << ",\"displayTexts\":";
                AppendPointerStringHits(json, entry.displayStrings, entry.displayStringCount);
            }

            json << "}";
        }

        json << "]}\n";
        return json.str();
    }
    bool VtableContainsFunction(uintptr_t vtableAddress, uintptr_t functionAddress, int maxSlots)
    {
        for (int slot = 0; slot < maxSlots; ++slot)
        {
            uintptr_t value = 0;
            if (!ReadPointerSafe(vtableAddress + static_cast<size_t>(slot) * sizeof(uintptr_t), &value))
            {
                return false;
            }

            if (value == functionAddress)
            {
                return true;
            }
        }

        return false;
    }

    uintptr_t FindControllerForPlayer(uintptr_t player, uintptr_t pickupItemFunction)
    {
        const auto gameModule = GetModuleInfoByName(L"Game.dll");
        const auto engineModule = GetModuleInfoByName(L"Engine.dll");
        if (!player || !pickupItemFunction || !gameModule || !engineModule)
        {
            return 0;
        }

        const auto scanBase = player - 0x4000;
        for (size_t offset = 0; offset < 0x8000; offset += sizeof(uintptr_t))
        {
            uintptr_t candidate = 0;
            if (!ReadPointerSafe(scanBase + offset, &candidate))
            {
                continue;
            }

            if (!IsObjectPointerCandidate(candidate))
            {
                continue;
            }

            uintptr_t vtable = 0;
            if (!ReadPointerSafe(candidate, &vtable) || !IsCodeAddress(vtable, *gameModule, *engineModule))
            {
                continue;
            }

            if (VtableContainsFunction(vtable, pickupItemFunction, 512))
            {
                return candidate;
            }
        }

        return 0;
    }

    bool TryCallPickupItem(DWORD64 address, uintptr_t controller, unsigned int itemId)
    {
        using PickupItemFn = void(__fastcall*)(void*, unsigned int);

        __try
        {
            reinterpret_cast<PickupItemFn>(static_cast<uintptr_t>(address))(reinterpret_cast<void*>(controller), itemId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ContainsAnyKeyword(const ItemDetailEntry& entry, const std::string& keywords)
    {
        if (keywords.empty())
        {
            return true;
        }

        size_t start = 0;
        while (start <= keywords.size())
        {
            const auto end = keywords.find(',', start);
            const auto keyword = keywords.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!keyword.empty())
            {
                if ((entry.displayName[0] != '\0' && strstr(entry.displayName, keyword.c_str()) != nullptr)
                    || (entry.basePath[0] != '\0' && strstr(entry.basePath, keyword.c_str()) != nullptr)
                    || (entry.prefixPath[0] != '\0' && strstr(entry.prefixPath, keyword.c_str()) != nullptr)
                    || (entry.suffixPath[0] != '\0' && strstr(entry.suffixPath, keyword.c_str()) != nullptr))
                {
                    return true;
                }
            }

            if (end == std::string::npos)
            {
                break;
            }

            start = end + 1;
        }

        return false;
    }

    struct ObjectIdSearchResult
    {
        unsigned int playerId;
        unsigned int controllerId;
        int scannedBuckets;
        int scannedNodes;
    };

    bool SearchObjectManagerIds(uintptr_t player, uintptr_t controller, ObjectIdSearchResult* result)
    {
        const auto getSingleton = ResolveModuleExport(L"Engine.dll", "?Get@?$Singleton@VObjectManager@GAME@@@GAME@@SAPEAVObjectManager@2@XZ");
        if (!getSingleton)
        {
            return false;
        }

        __try
        {
            using GetSingletonFn = void* (__fastcall*)();
            auto* objectManager = reinterpret_cast<GetSingletonFn>(getSingleton)();
            if (!objectManager)
            {
                return false;
            }

            const auto* bytes = reinterpret_cast<const unsigned char*>(objectManager);
            const auto buckets = *reinterpret_cast<const uintptr_t*>(bytes + 0x48);
            const auto mask = *reinterpret_cast<const size_t*>(bytes + 0x60);
            const auto sentinel = *reinterpret_cast<const uintptr_t*>(bytes + 0x38);
            if (!buckets || mask == 0 || mask > 0x400000)
            {
                return false;
            }

            for (size_t bucket = 0; bucket <= mask; ++bucket)
            {
                auto node = *reinterpret_cast<const uintptr_t*>(buckets + bucket * 16);
                int guard = 0;
                while (node && node != sentinel && guard++ < 1000)
                {
                    result->scannedNodes++;

                    const auto id = *reinterpret_cast<const unsigned int*>(node + 0x10);
                    const auto object = *reinterpret_cast<const uintptr_t*>(node + 0x18);
                    if (object == player && result->playerId == 0)
                    {
                        result->playerId = id;
                    }

                    if (object == controller && result->controllerId == 0)
                    {
                        result->controllerId = id;
                    }

                    const auto next = *reinterpret_cast<const uintptr_t*>(node);
                    if (next == node)
                    {
                        break;
                    }

                    node = next;
                }

                result->scannedBuckets++;
            }

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryWritePlayerIdField(uintptr_t controller, unsigned int value)
    {
        __try
        {
            *reinterpret_cast<unsigned int*>(controller + 0x30) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    std::string TryLootItems(const std::string& command)
    {
        float radius = 60.0f;
        std::string keywords;
        const auto firstColon = command.find(':');
        if (firstColon != std::string::npos)
        {
            const auto secondColon = command.find(':', firstColon + 1);
            if (secondColon == std::string::npos)
            {
                sscanf_s(command.c_str() + firstColon + 1, "%f", &radius);
            }
            else
            {
                const auto radiusText = command.substr(firstColon + 1, secondColon - firstColon - 1);
                sscanf_s(radiusText.c_str(), "%f", &radius);
                keywords = command.substr(secondColon + 1);
            }
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto itemClassInfoAddress = ResolveGameExport("?classInfo@Item@GAME@@1VRTTI_ClassInfo@2@B");
        const auto replicaInfoAddress = ResolveGameExport("?GetItemReplicaInfo@Item@GAME@@UEBAXAEAUItemReplicaInfo@2@@Z");
        const auto displayTextAddress = ResolveGameExport("?GetUIDisplayText@Item@GAME@@UEBAXPEBVCharacter@2@AEAV?$vector@UGameTextLine@GAME@@@mem@@_N@Z");
        const auto pickupItemAddress = ResolveGameExport("?PickupItem@ControllerCharacter@GAME@@UEAAXI@Z");
        if (!itemClassInfoAddress || !pickupItemAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required loot symbols not resolved\"}\n";
        }

        void* player = nullptr;
        void* gameEngine = nullptr;
        if (gGameEngineAddress && getMainPlayerAddress)
        {
            TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player);
        }

        if (context.world != g_cachedWorldAddress.load())
        {
            g_controllerAddress.store(0);
            g_playerObjectId.store(0);
            g_cachedWorldAddress.store(context.world);
        }

        auto controller = g_controllerAddress.load();
        if (controller)
        {
            uintptr_t controllerVtable = 0;
            if (!ReadPointerSafe(controller, &controllerVtable)
                || !VtableContainsFunction(controllerVtable, pickupItemAddress, 512))
            {
                controller = 0;
                g_controllerAddress.store(0);
            }
        }

        if (!controller && player)
        {
            controller = FindControllerForPlayer(reinterpret_cast<uintptr_t>(player), pickupItemAddress);
            if (controller)
            {
                g_controllerAddress.store(controller);
            }
        }

        auto originalFieldValue = unsigned int{ 0 };
        if (controller)
        {
            uintptr_t rawField = 0;
            if (ReadPointerSafe(controller + 0x30, &rawField))
            {
                originalFieldValue = static_cast<unsigned int>(rawField);
            }
        }

        if (controller && player)
        {
            auto playerId = g_playerObjectId.load();
            if (playerId == 0)
            {
                ObjectIdSearchResult search{};
                if (SearchObjectManagerIds(reinterpret_cast<uintptr_t>(player), controller, &search) && search.playerId != 0)
                {
                    playerId = search.playerId;
                    g_playerObjectId.store(playerId);
                }
            }

            if (playerId != 0)
            {
                TryWritePlayerIdField(controller, playerId);
            }
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ItemDetailEntry entries[32]{};
        auto found = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            found = ProbeItemDetailsInVector(
                vector.begin,
                vector.end,
                itemClassInfoAddress,
                replicaInfoAddress,
                displayTextAddress,
                player,
                entries,
                32);
        }

        auto matched = 0;
        auto looted = 0;
        for (int i = 0; i < found; ++i)
        {
            if (!ContainsAnyKeyword(entries[i], keywords))
            {
                continue;
            }

            matched++;
            if (controller && entries[i].itemId != 0
                && TryCallPickupItem(pickupItemAddress, controller, entries[i].itemId))
            {
                looted++;
            }
        }

        if (controller && originalFieldValue != 0)
        {
            TryWritePlayerIdField(controller, originalFieldValue);
        }

        std::ostringstream json;
        json << "{\"type\":\"loot_items\"";
        json << ",\"radius\":" << radius;
        json << ",\"keywords\":\"" << keywords << "\"";
        json << ",\"found\":" << found;
        json << ",\"matched\":" << matched;
        json << ",\"looted\":" << looted;
        json << ",\"controller\":\"" << ToHex(controller) << "\"";
        json << ",\"samples\":[";
        const auto sampleCount = found < 5 ? found : 5;
        for (int i = 0; i < sampleCount; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"id\":" << entries[i].itemId
                 << ",\"alt178\":" << entries[i].altId178
                 << ",\"alt17c\":" << entries[i].altId17c
                 << ",\"name\":\"" << entries[i].displayName << "\""
                 << ",\"prefix\":\"" << entries[i].prefixPath << "\""
                 << ",\"suffix\":\"" << entries[i].suffixPath << "\"}";
        }

        json << "]}\n";
        return json.str();
    }

    bool TryCallGetPlayerFromController(DWORD64 address, uintptr_t controller, uintptr_t* result)
    {
        using Fn = void* (__fastcall*)(void*);

        __try
        {
            *result = reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(static_cast<uintptr_t>(address))(reinterpret_cast<void*>(controller)));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryCallGlobalGetter(DWORD64 address, uintptr_t* result)
    {
        using Fn = void* (__fastcall*)();

        __try
        {
            *result = reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(static_cast<uintptr_t>(address))());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryCallGetObjectById(DWORD64 address, uintptr_t table, unsigned int id, uintptr_t* result)
    {
        using Fn = void* (__fastcall*)(void*, unsigned int);

        __try
        {
            *result = reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(static_cast<uintptr_t>(address))(reinterpret_cast<void*>(table), id));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryProbePickup()
    {
        const auto gameModule = GetModuleInfoByName(L"Game.dll");
        if (!gameModule)
        {
            return "{\"type\":\"error\",\"message\":\"Game.dll not found\"}\n";
        }

        const auto base = gameModule->base;
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto pickupItemAddress = ResolveGameExport("?PickupItem@ControllerCharacter@GAME@@UEAAXI@Z");
        if (!gGameEngineAddress || !getMainPlayerAddress || !pickupItemAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player) || !player)
        {
            return "{\"type\":\"error\",\"message\":\"player not available\"}\n";
        }

        const auto controller = FindControllerForPlayer(reinterpret_cast<uintptr_t>(player), pickupItemAddress);

        auto playerFromController = uintptr_t{ 0 };
        auto playerLookupOk = false;
        if (controller)
        {
            playerLookupOk = TryCallGetPlayerFromController(base + 0x78B10, controller, &playerFromController);
        }

        uintptr_t globalGetter = 0;
        ReadPointerSafe(base + 0x5F3098, &globalGetter);

        auto table = uintptr_t{ 0 };
        auto tableOk = false;
        const auto engineModule = GetModuleInfoByName(L"Engine.dll");
        if (globalGetter && engineModule && IsCodeAddress(globalGetter, *gameModule, *engineModule))
        {
            tableOk = TryCallGlobalGetter(globalGetter, &table);
        }

        std::ostringstream json;
        json << "{\"type\":\"probe_pickup\"";
        json << ",\"player\":\"" << ToHex(reinterpret_cast<uintptr_t>(player)) << "\"";
        json << ",\"controller\":\"" << ToHex(controller) << "\"";
        json << ",\"getPlayerOk\":" << (playerLookupOk ? "true" : "false");
        json << ",\"playerFromController\":\"" << ToHex(playerFromController) << "\"";
        json << ",\"globalGetter\":\"" << ToHex(globalGetter) << "\"";
        json << ",\"tableOk\":" << (tableOk ? "true" : "false");
        json << ",\"table\":\"" << ToHex(table) << "\"";

        json << ",\"idLookups\":[";
        const unsigned int ids[] = { 91697, 189749, 100310, 202547, 0, 1, 2, 3 };
        for (int i = 0; i < 8; ++i)
        {
            auto object = uintptr_t{ 0 };
            auto ok = false;
            if (tableOk && table)
            {
                ok = TryCallGetObjectById(base + 0x19D20, table, ids[i], &object);
            }

            if (i > 0)
            {
                json << ",";
            }

            json << "{\"id\":" << ids[i] << ",\"ok\":" << (ok ? "true" : "false") << ",\"object\":\"" << ToHex(object) << "\"}";
        }

        json << "]}\n";
        return json.str();
    }


    std::string TryFixPickup()
    {
        const auto gameModule = GetModuleInfoByName(L"Game.dll");
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto pickupItemAddress = ResolveGameExport("?PickupItem@ControllerCharacter@GAME@@UEAAXI@Z");
        if (!gameModule || !gGameEngineAddress || !getMainPlayerAddress || !pickupItemAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player) || !player)
        {
            return "{\"type\":\"error\",\"message\":\"player not available\"}\n";
        }

        const auto controller = FindControllerForPlayer(reinterpret_cast<uintptr_t>(player), pickupItemAddress);
        if (!controller)
        {
            return "{\"type\":\"error\",\"message\":\"controller not found\"}\n";
        }

        ObjectIdSearchResult search{};
        const auto searchOk = SearchObjectManagerIds(
            reinterpret_cast<uintptr_t>(player),
            controller,
            &search);

        std::ostringstream json;
        json << "{\"type\":\"fix_pickup\"";
        json << ",\"player\":\"" << ToHex(reinterpret_cast<uintptr_t>(player)) << "\"";
        json << ",\"controller\":\"" << ToHex(controller) << "\"";
        json << ",\"searchOk\":" << (searchOk ? "true" : "false");
        json << ",\"scannedBuckets\":" << search.scannedBuckets;
        json << ",\"scannedNodes\":" << search.scannedNodes;
        json << ",\"playerId\":" << search.playerId;
        json << ",\"controllerId\":" << search.controllerId;
        json << ",\"note\":\"read-only diagnostic; field is only modified during loot\"";
        json << "}\n";
        return json.str();
    }

    std::string TryPickupId(const std::string& command)
    {
        const auto firstColon = command.find(':');
        if (firstColon == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"format: pickup_id:<entityHex>:<id>\"}\n";
        }

        const auto secondColon = command.find(':', firstColon + 1);
        if (secondColon == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"format: pickup_id:<entityHex>:<id>\"}\n";
        }

        const auto entityText = command.substr(firstColon + 1, secondColon - firstColon - 1);
        const auto idText = command.substr(secondColon + 1);

        uintptr_t entity = 0;
        unsigned int id = 0;
        try
        {
            entity = static_cast<uintptr_t>(std::stoull(entityText, nullptr, 16));
            id = static_cast<unsigned int>(std::stoul(idText, nullptr, 10));
        }
        catch (...)
        {
            return "{\"type\":\"error\",\"message\":\"invalid entity or id\"}\n";
        }

        const auto gameModule = GetModuleInfoByName(L"Game.dll");
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto pickupItemAddress = ResolveGameExport("?PickupItem@ControllerCharacter@GAME@@UEAAXI@Z");
        if (!gameModule || !gGameEngineAddress || !getMainPlayerAddress || !pickupItemAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player) || !player)
        {
            return "{\"type\":\"error\",\"message\":\"player not available\"}\n";
        }

        auto controller = g_controllerAddress.load();
        if (!controller)
        {
            controller = FindControllerForPlayer(reinterpret_cast<uintptr_t>(player), pickupItemAddress);
        }

        if (!controller)
        {
            return "{\"type\":\"error\",\"message\":\"controller not found\"}\n";
        }

        auto originalFieldValue = unsigned int{ 0 };
        {
            uintptr_t rawField = 0;
            if (ReadPointerSafe(controller + 0x30, &rawField))
            {
                originalFieldValue = static_cast<unsigned int>(rawField);
            }
        }

        ObjectIdSearchResult search{};
        if (SearchObjectManagerIds(reinterpret_cast<uintptr_t>(player), controller, &search) && search.playerId != 0)
        {
            TryWritePlayerIdField(controller, search.playerId);
        }

        const auto ok = TryCallPickupItem(pickupItemAddress, controller, id);

        if (originalFieldValue != 0)
        {
            TryWritePlayerIdField(controller, originalFieldValue);
        }

        std::ostringstream json;
        json << "{\"type\":\"pickup_id\"";
        json << ",\"entity\":\"" << ToHex(entity) << "\"";
        json << ",\"id\":" << id;
        json << ",\"controller\":\"" << ToHex(controller) << "\"";
        json << ",\"called\":" << (ok ? "true" : "false");
        json << "}\n";
        return json.str();
    }

    std::string TryResetCache()
    {
        g_controllerAddress.store(0);
        g_playerObjectId.store(0);
        g_worldAddress.store(0);
        g_cachedWorldAddress.store(0);
        return "{\"type\":\"reset_cache\",\"status\":\"ok\"}\n";
    }

    bool TryCallGetStaticClassInfo(DWORD64 address, uintptr_t* result)
    {
        using Fn = const void* (__fastcall*)();

        __try
        {
            *result = reinterpret_cast<uintptr_t>(reinterpret_cast<Fn>(static_cast<uintptr_t>(address))());
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct ChestProbeEntry
    {
        uintptr_t entity;
        float x;
        float y;
        float z;
        int hasPosition;
    };

    int ProbeChestsInVector(
        uintptr_t vectorBegin,
        uintptr_t vectorEnd,
        uintptr_t chestClassInfo,
        DWORD64 moveToPointAddress,
        DWORD64 worldPositionAddress,
        ChestProbeEntry* entries,
        int maxEntries)
    {
        int count = 0;
        using GetMoveToPointFn = void(__fastcall*)(void*, void*, unsigned int);
        using GetWorldPositionFn = void(__fastcall*)(void*, Vec3*);

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd && count < maxEntries; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!IsMonsterClassInfo(classInfo, chestClassInfo))
                {
                    continue;
                }

                auto& entry = entries[count];
                entry.entity = entity;
                entry.x = 0.0f;
                entry.y = 0.0f;
                entry.z = 0.0f;
                entry.hasPosition = 0;

                if (moveToPointAddress && worldPositionAddress)
                {
                    alignas(16) unsigned char worldVec3[128]{};
                    reinterpret_cast<GetMoveToPointFn>(static_cast<uintptr_t>(moveToPointAddress))(
                        reinterpret_cast<void*>(entity),
                        worldVec3,
                        0);

                    Vec3 worldPosition{};
                    reinterpret_cast<GetWorldPositionFn>(static_cast<uintptr_t>(worldPositionAddress))(worldVec3, &worldPosition);
                    entry.x = worldPosition.x;
                    entry.y = worldPosition.y;
                    entry.z = worldPosition.z;
                    entry.hasPosition = 1;
                }

                count++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    std::string TryFindChest()
    {
        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto staticClassInfoAddress = ResolveGameExport("?GetStaticClassInfo@FixedItemContainer@GAME@@SAAEBVRTTI_ClassInfo@2@XZ");
        const auto moveToPointAddress = ResolveGameExport("?GetMoveToPoint@FixedItemContainer@GAME@@UEBA?BVWorldVec3@2@I@Z");
        const auto worldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");
        if (!staticClassInfoAddress || !moveToPointAddress || !worldPositionAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required chest symbols not resolved\"}\n";
        }

        uintptr_t chestClassInfo = 0;
        if (!TryCallGetStaticClassInfo(staticClassInfoAddress, &chestClassInfo) || !chestClassInfo)
        {
            return "{\"type\":\"error\",\"message\":\"failed to get FixedItemContainer class info\"}\n";
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, 200.0f, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ChestProbeEntry entries[8]{};
        auto count = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            count = ProbeChestsInVector(
                vector.begin,
                vector.end,
                chestClassInfo,
                moveToPointAddress,
                worldPositionAddress,
                entries,
                8);
        }

        std::ostringstream json;
        json << "{\"type\":\"find_chest\"";
        json << ",\"count\":" << count;
        json << ",\"chests\":[";
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"entity\":\"" << ToHex(entries[i].entity) << "\"";
            if (entries[i].hasPosition)
            {
                json << ",\"x\":" << entries[i].x << ",\"y\":" << entries[i].y << ",\"z\":" << entries[i].z;
            }

            json << "}";
        }

        json << "]}\n";
        return json.str();
    }

    uintptr_t FindChestController(uintptr_t chestEntity, uintptr_t touchedByActorAddress)
    {
        (void)touchedByActorAddress;

        uintptr_t controller = 0;
        if (!ReadPointerSafe(chestEntity + 0x608, &controller))
        {
            return 0;
        }

        return IsObjectPointerCandidate(controller) ? controller : 0;
    }

    bool TryCallTouchedByActor(DWORD64 address, uintptr_t controller)
    {
        using Fn = void(__fastcall*)(void*);

        __try
        {
            reinterpret_cast<Fn>(static_cast<uintptr_t>(address))(reinterpret_cast<void*>(controller));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryReadChestState(uintptr_t controller, unsigned int* state)
    {
        __try
        {
            *state = *reinterpret_cast<unsigned int*>(controller + 0x180);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct ChestOpenEntry
    {
        uintptr_t entity;
        uintptr_t controller;
        unsigned int stateBefore;
        unsigned int stateAfter;
        int called;
    };

    int OpenChestsInVector(
        uintptr_t vectorBegin,
        uintptr_t vectorEnd,
        uintptr_t chestClassInfo,
        DWORD64 touchedByActorAddress,
        ChestOpenEntry* entries,
        int maxEntries)
    {
        int count = 0;

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd && count < maxEntries; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!IsMonsterClassInfo(classInfo, chestClassInfo))
                {
                    continue;
                }

                auto& entry = entries[count];
                entry.entity = entity;
                entry.controller = FindChestController(entity, touchedByActorAddress);
                entry.stateBefore = 0;
                entry.stateAfter = 0;
                entry.called = 0;

                if (entry.controller)
                {
                    if (TryReadChestState(entry.controller, &entry.stateBefore) && entry.stateBefore == 1)
                    {
                        if (TryCallTouchedByActor(touchedByActorAddress, entry.controller))
                        {
                            entry.called = 1;
                        }

                        TryReadChestState(entry.controller, &entry.stateAfter);
                    }
                }

                count++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    std::string TryOpenChest(const std::string& command)
    {
        (void)command;

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto staticClassInfoAddress = ResolveGameExport("?GetStaticClassInfo@FixedItemContainer@GAME@@SAAEBVRTTI_ClassInfo@2@XZ");
        const auto touchedByActorAddress = ResolveGameExport("?TouchedByActor@FixedItemContainerController@GAME@@MEAAXXZ");
        if (!staticClassInfoAddress || !touchedByActorAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required chest symbols not resolved\"}\n";
        }

        uintptr_t chestClassInfo = 0;
        if (!TryCallGetStaticClassInfo(staticClassInfoAddress, &chestClassInfo) || !chestClassInfo)
        {
            return "{\"type\":\"error\",\"message\":\"failed to get chest class info\"}\n";
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, 200.0f, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ChestOpenEntry entries[8]{};
        auto count = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            count = OpenChestsInVector(
                vector.begin,
                vector.end,
                chestClassInfo,
                touchedByActorAddress,
                entries,
                8);
        }

        std::ostringstream json;
        json << "{\"type\":\"open_chest\",\"chests\":[";
        auto opened = 0;
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"entity\":\"" << ToHex(entries[i].entity) << "\"";
            json << ",\"controller\":\"" << ToHex(entries[i].controller) << "\"";
            json << ",\"stateBefore\":" << entries[i].stateBefore;
            json << ",\"called\":" << (entries[i].called ? "true" : "false");
            json << ",\"stateAfter\":" << entries[i].stateAfter;
            json << "}";
            if (entries[i].called)
            {
                opened++;
            }
        }

        json << "],\"count\":" << count << ",\"called\":" << opened << "}\n";
        return json.str();
    }

    struct ContainerBreakEntry
    {
        uintptr_t entity;
        float life;
        float maxLife;
        float x;
        float y;
        float z;
        int broken;
    };

    int BreakContainersInVector(
        uintptr_t vectorBegin,
        uintptr_t vectorEnd,
        uintptr_t destructibleClassInfo,
        DWORD64 moveToPointAddress,
        DWORD64 worldPositionAddress,
        DWORD64 breakApartAddress,
        DWORD64 getLifeAddress,
        DWORD64 getMaxLifeAddress,
        ContainerBreakEntry* entries,
        int maxEntries)
    {
        int count = 0;
        using GetMoveToPointFn = void(__fastcall*)(void*, void*, unsigned int, unsigned int);
        using GetWorldPositionFn = void(__fastcall*)(void*, Vec3*);
        using BreakApartFn = void(__fastcall*)(void*, const Vec3*, float, bool);
        using GetLifeFn = float(__fastcall*)(void*);

        __try
        {
            for (auto position = vectorBegin; position + sizeof(uintptr_t) <= vectorEnd && count < maxEntries; position += sizeof(uintptr_t))
            {
                const auto entity = *reinterpret_cast<uintptr_t*>(position);
                if (!entity)
                {
                    continue;
                }

                const auto classInfo = TryGetEntityClassInfo(entity);
                if (!IsMonsterClassInfo(classInfo, destructibleClassInfo))
                {
                    continue;
                }

                auto& entry = entries[count];
                entry.entity = entity;
                entry.life = 0.0f;
                entry.maxLife = 0.0f;
                entry.x = 0.0f;
                entry.y = 0.0f;
                entry.z = 0.0f;
                entry.broken = 0;

                if (getLifeAddress)
                {
                    entry.life = reinterpret_cast<GetLifeFn>(static_cast<uintptr_t>(getLifeAddress))(reinterpret_cast<void*>(entity));
                }

                if (getMaxLifeAddress)
                {
                    entry.maxLife = reinterpret_cast<GetLifeFn>(static_cast<uintptr_t>(getMaxLifeAddress))(reinterpret_cast<void*>(entity));
                }

                Vec3 worldPosition{};
                if (moveToPointAddress && worldPositionAddress)
                {
                    alignas(16) unsigned char worldVec3[128]{};
                    reinterpret_cast<GetMoveToPointFn>(static_cast<uintptr_t>(moveToPointAddress))(
                        reinterpret_cast<void*>(entity),
                        worldVec3,
                        0,
                        0);

                    reinterpret_cast<GetWorldPositionFn>(static_cast<uintptr_t>(worldPositionAddress))(worldVec3, &worldPosition);
                }

                entry.x = worldPosition.x;
                entry.y = worldPosition.y;
                entry.z = worldPosition.z;

                if (breakApartAddress)
                {
                    reinterpret_cast<BreakApartFn>(static_cast<uintptr_t>(breakApartAddress))(
                        reinterpret_cast<void*>(entity),
                        &worldPosition,
                        1.0f,
                        true);
                    entry.broken = 1;
                }

                count++;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        return count;
    }

    std::string TryBreakContainers(const std::string& command)
    {
        float radius = 60.0f;
        const auto separator = command.find(':');
        if (separator != std::string::npos)
        {
            sscanf_s(command.c_str() + separator + 1, "%f", &radius);
        }

        EntityQueryContext context{};
        std::string error;
        if (!PrepareEntityQuery(&context, &error))
        {
            return error;
        }

        const auto staticClassInfoAddress = ResolveGameExport("?GetStaticClassInfo@Destructible@GAME@@SAAEBVRTTI_ClassInfo@2@XZ");
        const auto moveToPointAddress = ResolveGameExport("?GetMoveToPoint@Destructible@GAME@@UEBA?BVWorldVec3@2@II@Z");
        const auto worldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");
        const auto breakApartAddress = ResolveGameExport("?BreakApart@Destructible@GAME@@QEAAXAEBVVec3@2@M_N@Z");
        const auto getLifeAddress = ResolveGameExport("?GetLife@Destructible@GAME@@QEBAMXZ");
        const auto getMaxLifeAddress = ResolveGameExport("?GetMaxLife@Destructible@GAME@@QEBAMXZ");
        if (!staticClassInfoAddress || !breakApartAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required destructible symbols not resolved\"}\n";
        }

        uintptr_t destructibleClassInfo = 0;
        if (!TryCallGetStaticClassInfo(staticClassInfoAddress, &destructibleClassInfo) || !destructibleClassInfo)
        {
            return "{\"type\":\"error\",\"message\":\"failed to get destructible class info\"}\n";
        }

        GameVectorLayout vector{};
        if (!QueryEntitiesShared(context, radius, &vector))
        {
            return "{\"type\":\"error\",\"message\":\"entity query failed\"}\n";
        }

        ContainerBreakEntry entries[16]{};
        auto count = 0;
        if (vector.begin && vector.end > vector.begin)
        {
            count = BreakContainersInVector(
                vector.begin,
                vector.end,
                destructibleClassInfo,
                moveToPointAddress,
                worldPositionAddress,
                breakApartAddress,
                getLifeAddress,
                getMaxLifeAddress,
                entries,
                16);
        }

        std::ostringstream json;
        json << "{\"type\":\"break_containers\",\"radius\":" << radius << ",\"containers\":[";
        auto broken = 0;
        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
            {
                json << ",";
            }

            json << "{\"entity\":\"" << ToHex(entries[i].entity) << "\"";
            json << ",\"life\":" << entries[i].life;
            json << ",\"maxLife\":" << entries[i].maxLife;
            json << ",\"x\":" << entries[i].x << ",\"y\":" << entries[i].y << ",\"z\":" << entries[i].z;
            json << ",\"broken\":" << (entries[i].broken ? "true" : "false");
            json << "}";
            if (entries[i].broken)
            {
                broken++;
            }
        }

        json << "],\"count\":" << count << ",\"broken\":" << broken << "}\n";
        return json.str();
    }

    std::string TryFindController()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto pickupItemAddress = ResolveGameExport("?PickupItem@ControllerCharacter@GAME@@UEAAXI@Z");
        if (!gGameEngineAddress || !getMainPlayerAddress || !pickupItemAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadGameStatePointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player) || !player)
        {
            return "{\"type\":\"error\",\"message\":\"player not available\"}\n";
        }

        const auto controller = FindControllerForPlayer(reinterpret_cast<uintptr_t>(player), pickupItemAddress);
        if (controller)
        {
            g_controllerAddress.store(controller);
        }

        std::ostringstream json;
        json << "{\"type\":\"find_controller\"";
        json << ",\"player\":\"" << ToHex(reinterpret_cast<uintptr_t>(player)) << "\"";
        json << ",\"pickupItem\":\"" << ToHex(pickupItemAddress) << "\"";
        json << ",\"controller\":\"" << ToHex(controller) << "\"";
        json << "}\n";
        return json.str();
    }
    
    std::string TryModifyAttribute(const std::string& command)
    {
        // 格式：modify_attribute:<attribute_type>:<value>
        // attribute_type: strength, agility, intellect, oa, da, health, mana
        const auto pos1 = command.find(':');
        if (pos1 == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"format: modify_attribute:<type>:<value>\"}\n";
        }
        
        const auto pos2 = command.find(':', pos1 + 1);
        if (pos2 == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"missing attribute value\"}\n";
        }
        
        const auto attrType = command.substr(pos1 + 1, pos2 - pos1 - 1);
        int value = 0;
        sscanf_s(command.c_str() + pos2 + 1, "%d", &value);
        
        // TODO: 实现属性修改逻辑
        // 需要找到 Character 类成员变量偏移
        
        std::ostringstream json;
        json << "{\"type\":\"attribute\",\"attribute\":\"" << attrType << "\",\"value\":" << value << ",\"status\":\"pending_implementation\"}\n";
        return json.str();
    }

    std::string ListModules()
    {
        DWORD cbNeeded = 0;
        HMODULE* modulesArray = nullptr;

        if (!EnumProcessModules(GetCurrentProcess(), nullptr, 0, &cbNeeded))
        {
            return "{\"type\":\"error\",\"message\":\"EnumProcessModules failed\"}\n";
        }

        if (cbNeeded == 0)
        {
            return "{\"type\":\"modules\",\"count\":0}\n";
        }

        modulesArray = new HMODULE[cbNeeded / sizeof(HMODULE)];
        if (!EnumProcessModules(GetCurrentProcess(), modulesArray, cbNeeded, &cbNeeded))
        {
            delete[] modulesArray;
            return "{\"type\":\"error\",\"message\":\"EnumProcessModules failed\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"modules\",\"count\":" << (cbNeeded / sizeof(HMODULE));
        
        for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); i++)
        {
            wchar_t moduleName[MAX_PATH] = {};
            GetModuleFileNameExW(GetCurrentProcess(), modulesArray[i], moduleName, MAX_PATH);

            MODULEINFO info = {};
            if (GetModuleInformation(GetCurrentProcess(), modulesArray[i], &info, sizeof(info)))
            {
                json << ",\"module\"[" << i << "]:{\"name\":\"";
                
                // 直接输出 Unicode 路径，JSON 会自动处理转义
                // 对于包含中文的路径，使用 UTF-8 编码
                std::wstring wName(moduleName);
                int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string utf8Name(utf8Size, 0);
                WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), -1, &utf8Name[0], utf8Size, nullptr, nullptr);
                
                json << utf8Name << "\"";
                json << ",\"base\":\"" << std::hex << reinterpret_cast<uintptr_t>(info.lpBaseOfDll) << "\"}";
            }
        }

        json << "}\n";
        delete[] modulesArray;
        return json.str();
    }

    std::wstring GetLogPath()
    {
        wchar_t tempPath[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempPath);
        std::wstringstream stream;
        stream << tempPath << L"GrimDawnTeleporter.Plugin." << GetCurrentProcessId() << L".log";
        return stream.str();
    }

    void Log(const std::wstring& message)
    {
        static std::mutex logMutex;
        std::lock_guard<std::mutex> guard(logMutex);

        std::wofstream log(GetLogPath(), std::ios::app);
        if (log)
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            log << L"[" << now.wHour << L":" << now.wMinute << L":" << now.wSecond << L"] " << message << L"\n";
        }
    }

    struct SymbolResolver
    {
        bool Initialize()
        {
            if (g_symbolsInitialized.load())
            {
                return true;
            }

            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_EXACT_SYMBOLS);
            if (!SymInitialize(g_process, nullptr, TRUE))
            {
                const auto error = GetLastError();
                if (error != ERROR_INVALID_PARAMETER)
                {
                    std::wstringstream stream;
                    stream << L"SymInitialize failed: " << error;
                    Log(stream.str());
                    return false;
                }
            }

            auto gameModule = GetModuleHandleW(nullptr);
            if (gameModule)
            {
                wchar_t path[MAX_PATH]{};
                if (GetModuleFileNameW(gameModule, path, MAX_PATH) > 0)
                {
                    char narrow[MAX_PATH]{};
                    WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, MAX_PATH, nullptr, nullptr);
                    SymLoadModule64(g_process, nullptr, narrow, nullptr, reinterpret_cast<DWORD64>(gameModule), 0);
                }
            }

            g_symbolsInitialized.store(true);
            return true;
        }

        DWORD64 Resolve(const wchar_t* name)
        {
            if (!g_symbolsInitialized.load())
            {
                return 0;
            }

            alignas(SYMBOL_INFOW) unsigned char buffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(wchar_t)]{};
            auto* symbol = reinterpret_cast<PSYMBOL_INFOW>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
            symbol->MaxNameLen = MAX_SYM_NAME;
            if (!SymFromNameW(g_process, name, symbol))
            {
                return 0;
            }

            return symbol->Address;
        }
    };

    struct PatternScanner
    {
        static bool ParsePattern(const std::string& text, std::vector<int>& bytes)
        {
            std::istringstream stream(text);
            std::string token;
            while (stream >> token)
            {
                if (token == "??" || token == "?")
                {
                    bytes.push_back(-1);
                    continue;
                }

                int value = 0;
                if (sscanf_s(token.c_str(), "%x", &value) != 1)
                {
                    return false;
                }

                bytes.push_back(value & 0xFF);
            }

            return !bytes.empty();
        }

        static uintptr_t Find(const uint8_t* base, size_t size, const std::vector<int>& pattern)
        {
            if (pattern.empty() || size < pattern.size())
            {
                return 0;
            }

            for (size_t i = 0; i <= size - pattern.size(); ++i)
            {
                bool matched = true;
                for (size_t j = 0; j < pattern.size(); ++j)
                {
                    if (pattern[j] != -1 && base[i + j] != static_cast<uint8_t>(pattern[j]))
                    {
                        matched = false;
                        break;
                    }
                }

                if (matched)
                {
                    return reinterpret_cast<uintptr_t>(base + i);
                }
            }

            return 0;
        }
    };

    std::optional<ModuleInfo> GetModuleInfoByName(const std::wstring& moduleName)
    {
        HMODULE module = nullptr;
        if (moduleName.empty() || moduleName == L"Grim Dawn.exe" || moduleName == L"Grim Dawn")
        {
            module = GetModuleHandleW(nullptr);
        }
        else
        {
            module = GetModuleHandleW(moduleName.c_str());
        }

        if (!module)
        {
            return std::nullopt;
        }

        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
        {
            return std::nullopt;
        }

        return ModuleInfo{ moduleName, reinterpret_cast<uintptr_t>(info.lpBaseOfDll), static_cast<size_t>(info.SizeOfImage) };
    }

    std::optional<SectionInfo> GetSectionInfo(const ModuleInfo& module, const char* sectionName)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return std::nullopt;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module.base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return std::nullopt;
        }

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            char name[9]{};
            memcpy(name, section->Name, 8);
            if (strcmp(name, sectionName) == 0)
            {
                return SectionInfo{ name, module.base + section->VirtualAddress, section->Misc.VirtualSize };
            }
        }

        return std::nullopt;
    }

    std::wstring GetPipeName()
    {
        std::wstringstream stream;
        stream << L"\\\\.\\pipe\\GrimDawnTeleporter.Plugin." << GetCurrentProcessId();
        return stream.str();
    }

    std::wstring NarrowToWide(const std::string& text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::string WideToNarrow(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }

        const auto length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::string ToHex(uintptr_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value;
        return stream.str();
    }

    DWORD64 ResolveModuleExport(const wchar_t* moduleName, const char* name)
    {
        const auto module = GetModuleHandleW(moduleName);
        if (!module)
        {
            return 0;
        }

        return reinterpret_cast<DWORD64>(GetProcAddress(module, name));
    }

    DWORD64 ResolveGameExport(const char* name)
    {
        if (const auto address = ResolveModuleExport(L"Game.dll", name); address != 0)
        {
            return address;
        }

        return ResolveModuleExport(L"Engine.dll", name);
    }

    std::vector<uintptr_t> ScanAsciiStringInModule(const ModuleInfo& module, const std::string& needle, size_t maxResults = 16)
    {
        std::vector<uintptr_t> results;
        if (needle.empty() || module.size < needle.size())
        {
            return results;
        }

        const auto* base = reinterpret_cast<const uint8_t*>(module.base);
        for (size_t i = 0; i <= module.size - needle.size(); ++i)
        {
            if (memcmp(base + i, needle.data(), needle.size()) == 0)
            {
                results.push_back(module.base + i);
                if (results.size() >= maxResults)
                {
                    break;
                }
            }
        }

        return results;
    }

    std::pair<size_t, size_t> CountDirectReferences(const ModuleInfo& module, uintptr_t address)
    {
        size_t vaRefs = 0;
        size_t rvaRefs = 0;
        const auto* base = reinterpret_cast<const uint8_t*>(module.base);
        const auto rva = static_cast<uint32_t>(address - module.base);

        for (size_t i = 0; i + sizeof(uint64_t) <= module.size; ++i)
        {
            if (*reinterpret_cast<const uint64_t*>(base + i) == static_cast<uint64_t>(address))
            {
                ++vaRefs;
            }
        }

        for (size_t i = 0; i + sizeof(uint32_t) <= module.size; ++i)
        {
            if (*reinterpret_cast<const uint32_t*>(base + i) == rva)
            {
                ++rvaRefs;
            }
        }

        return { vaRefs, rvaRefs };
    }

    std::pair<size_t, uintptr_t> CountRipRelativeReferences(const ModuleInfo& module, uintptr_t target)
    {
        size_t count = 0;
        uintptr_t first = 0;
        const auto* base = reinterpret_cast<const uint8_t*>(module.base);

        for (size_t i = 0; i + sizeof(int32_t) <= module.size; ++i)
        {
            const auto displacement = *reinterpret_cast<const int32_t*>(base + i);
            const auto candidate = module.base + i + sizeof(int32_t) + displacement;
            if (candidate == target)
            {
                ++count;
                if (first == 0)
                {
                    first = module.base + i;
                }
            }
        }

        return { count, first };
    }

    size_t CountByteSequence(uintptr_t baseAddress, size_t size, const uint8_t* sequence, size_t sequenceSize)
    {
        if (!baseAddress || !sequence || sequenceSize == 0 || size < sequenceSize)
        {
            return 0;
        }

        size_t count = 0;
        const auto* base = reinterpret_cast<const uint8_t*>(baseAddress);
        for (size_t i = 0; i <= size - sequenceSize; ++i)
        {
            if (memcmp(base + i, sequence, sequenceSize) == 0)
            {
                ++count;
            }
        }

        return count;
    }

    uintptr_t FindFirstByteSequence(uintptr_t baseAddress, size_t size, const uint8_t* sequence, size_t sequenceSize)
    {
        if (!baseAddress || !sequence || sequenceSize == 0 || size < sequenceSize)
        {
            return 0;
        }

        const auto* base = reinterpret_cast<const uint8_t*>(baseAddress);
        for (size_t i = 0; i <= size - sequenceSize; ++i)
        {
            if (memcmp(base + i, sequence, sequenceSize) == 0)
            {
                return baseAddress + i;
            }
        }

        return 0;
    }

    std::string BytesToHex(const uint8_t* bytes, size_t size)
    {
        std::ostringstream stream;
        stream << std::hex;
        for (size_t i = 0; i < size; ++i)
        {
            if (i > 0)
            {
                stream << ' ';
            }
            stream.width(2);
            stream.fill('0');
            stream << static_cast<int>(bytes[i]);
        }

        return stream.str();
    }

    std::string DumpContext(const ModuleInfo& module, uintptr_t address, size_t bytesBefore = 16, size_t bytesAfter = 32)
    {
        if (!address || address < module.base || address >= module.base + module.size)
        {
            return "";
        }

        const auto* base = reinterpret_cast<const uint8_t*>(module.base);
        const auto offset = address - module.base;
        const auto start = offset > bytesBefore ? offset - bytesBefore : 0;
        const auto end = (offset + bytesAfter < module.size) ? offset + bytesAfter : module.size;
        if (end <= start)
        {
            return "";
        }

        return BytesToHex(base + start, end - start);
    }

    std::string DiagnoseAobPatterns()
    {
        const auto module = GetModuleInfoByName(L"Game.dll");
        if (!module)
        {
            return "{\"type\":\"error\",\"message\":\"Game.dll module not found\"}\n";
        }

        const auto textSection = GetSectionInfo(*module, ".text");
        if (!textSection)
        {
            return "{\"type\":\"error\",\"message\":\".text section not found\"}\n";
        }

        struct PatternProbe
        {
            const char* name;
            const char* pattern;
        };

        const PatternProbe probes[] = {
            { "resolver_call_1", "4C 8B CF 48 89 4C 24 20 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??" },
            { "resolver_call_2", "48 8D 0D ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B D0" },
            { "resolver_call_3", "48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??" },
        };

        std::ostringstream json;
        json << "{\"type\":\"aob_diagnose\"";
        json << ",\"textSection\":\"" << ToHex(textSection->base) << "\"";

        for (const auto& probe : probes)
        {
            std::vector<int> pattern;
            if (!PatternScanner::ParsePattern(probe.pattern, pattern))
            {
                json << ",\"" << probe.name << "\":{\"error\":\"invalid pattern\"}";
                continue;
            }

            const auto address = PatternScanner::Find(reinterpret_cast<const uint8_t*>(textSection->base), textSection->size, pattern);
            json << ",\"" << probe.name << "\":{\"address\":\"" << ToHex(address) << "\"";
            if (address != 0)
            {
                json << ",\"context\":\"" << DumpContext(*module, address) << "\"";
            }
            json << "}";
        }

        const char* symbols[] = {
            "?gGameEngine@GAME@@3PEAVGameEngine@1@EA",
            "?InitiatePlayerTeleport@GameEngine@GAME@@QEAAXHHHW4TeleportEffect@2@_N@Z"
        };

        for (const auto* needle : symbols)
        {
            const auto results = ScanAsciiStringInModule(*module, needle, 1);
            json << ",\"" << needle << "\":";
            if (results.empty())
            {
                json << "{\"address\":\"0x0\",\"ripRefs\":0,\"firstRipRef\":\"0x0\"}";
                continue;
            }

            const auto refs = CountRipRelativeReferences(*module, results[0]);
            json << "{\"address\":\"" << ToHex(results[0]) << "\",\"ripRefs\":" << refs.first << ",\"firstRipRef\":\"" << ToHex(refs.second) << "\"";
            if (refs.second != 0)
            {
                json << ",\"firstRipContext\":\"" << DumpContext(*module, refs.second) << "\"";
            }
            json << "}";
        }

        json << "}\n";
        return json.str();
    }

    std::string DiagnoseGameSymbolStrings()
    {
        const auto module = GetModuleInfoByName(L"Game.dll");
        if (!module)
        {
            return "{\"type\":\"error\",\"message\":\"Game.dll module not found\"}\n";
        }

        const char* needles[] = {
            "gGameEngine@GAME",
            "GetMainPlayer@GameEngine@GAME",
            "GetPlayerManagerClient@GameEngine@GAME",
            "GetPlayerId@GameEngine@GAME",
            "GetPlayerLocation@PlayerManagerClient@GAME",
            "GetWorldPosition@WorldVec3@GAME",
            "InitiatePlayerTeleport@GameEngine@GAME",
            "SetGod@Character@GAME",
            "SetInvincible@Character@GAME",
            "SubtractLife@Character@GAME",
            "ApplyDamage@CombatManager@GAME",
            "GetEntitiesInSphere@World@GAME",
            "GetCenterOfMass@Actor@GAME",
            "GetMonsterClassification@Monster@GAME"
        };

        std::ostringstream json;
        json << "{\"type\":\"symbol_strings\"";
        const auto bindSection = GetSectionInfo(*module, ".bind");
        json << ",\"bindSection\":\"" << (bindSection ? ToHex(bindSection->base) : "0x0") << "\"";
        for (const auto* needle : needles)
        {
            const auto results = ScanAsciiStringInModule(*module, needle, 1);
            json << ",\"" << needle << "\":";
            if (results.empty())
            {
                json << "{\"address\":\"0x0\",\"vaRefs\":0,\"rvaRefs\":0}";
            }
            else
            {
                const auto refs = CountDirectReferences(*module, results[0]);
                const auto ripRefs = CountRipRelativeReferences(*module, results[0]);
                const auto* prefix2 = reinterpret_cast<const uint8_t*>(results[0] - 2);
                const auto* prefix3 = reinterpret_cast<const uint8_t*>(results[0] - 3);
                const auto* prefix4 = reinterpret_cast<const uint8_t*>(results[0] - 4);
                const auto prefix2BindRefs = bindSection ? CountByteSequence(bindSection->base, bindSection->size, prefix2, 2) : 0;
                const auto prefix3BindRefs = bindSection ? CountByteSequence(bindSection->base, bindSection->size, prefix3, 3) : 0;
                const auto prefix4BindRefs = bindSection ? CountByteSequence(bindSection->base, bindSection->size, prefix4, 4) : 0;
                const auto firstPrefix2BindRef = bindSection ? FindFirstByteSequence(bindSection->base, bindSection->size, prefix2, 2) : 0;
                json << "{\"address\":\"" << ToHex(results[0]) << "\",\"prefix2\":\"" << BytesToHex(prefix2, 2) << "\",\"prefix3\":\"" << BytesToHex(prefix3, 3) << "\",\"prefix4\":\"" << BytesToHex(prefix4, 4) << "\",\"vaRefs\":" << refs.first << ",\"rvaRefs\":" << refs.second << ",\"ripRefs\":" << ripRefs.first << ",\"firstRipRef\":\"" << ToHex(ripRefs.second) << "\",\"prefix2BindRefs\":" << prefix2BindRefs << ",\"prefix3BindRefs\":" << prefix3BindRefs << ",\"prefix4BindRefs\":" << prefix4BindRefs << ",\"firstPrefix2BindRef\":\"" << ToHex(firstPrefix2BindRef) << "\"}";
            }
        }

        json << "}\n";
        return json.str();
    }

    std::string ResolveCoreSymbols()
    {
        struct SymbolProbe
        {
            const char* key;
            const char* name;
        };

        const SymbolProbe probes[] = {
            { "gGameEngine", "?gGameEngine@GAME@@3PEAVGameEngine@1@EA" },
            { "GetMainPlayer", "?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ" },
            { "GetPlayerManagerClient", "?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ" },
            { "GetPlayerId", "?GetPlayerId@GameEngine@GAME@@QEBAIXZ" },
            { "GetPlayerLocation", "?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z" },
            { "GetWorldPosition", "?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ" },
            { "InitiatePlayerTeleport", "?InitiatePlayerTeleport@GameEngine@GAME@@QEAAXHHHW4TeleportEffect@2@_N@Z" },
            { "CtoS_StartTeleportInbound", "?CtoS_StartTeleportInbound@GameEngine@GAME@@QEAAXAEBHIHHHMMW4TeleportEffect@2@@Z" },
            { "StoC_StartTeleportInbound", "?StoC_StartTeleportInbound@GameEngine@GAME@@QEAAXIHHHMMW4TeleportEffect@2@@Z" },
            { "GetCurrentMoney", "?GetCurrentMoney@Character@GAME@@QEBA?BIXZ" },
            { "AddMoney", "?AddMoney@Character@GAME@@QEAAXI@Z" },
            { "SubtractMoney", "?SubtractMoney@Character@GAME@@QEAA?BII@Z" },
            { "SetGod", "?SetGod@Character@GAME@@QEAAX_N@Z" },
            { "SetInvincible", "?SetInvincible@Character@GAME@@QEAAX_N@Z" },
            { "SubtractLife", "?SubtractLife@Character@GAME@@QEAAXMAEBUPlayStatsDamageType@2@_N_N@Z" },
            { "ApplyDamage", "?ApplyDamage@CombatManager@GAME@@QEAA_NMAEBUPlayStatsDamageType@2@W4CombatAttributeType@2@AEBV?$vector@I@mem@@@Z" },
            { "GetEntitiesInSphere", "?GetEntitiesInSphere@World@GAME@@QEBAXAEAV?$vector@PEAVEntity@GAME@@@mem@@PEAVRegion@2@AEBVSphere@2@_NW4EntityListType@2@@Z" },
            { "GetEntitiesInFrustum", "?GetEntitiesInFrustum@World@GAME@@QEBAXAEAV?$vector@PEAVEntity@GAME@@@mem@@AEBVWorldFrustum@2@_NW4EntityListType@2@22@Z" },
            { "GetCenterOfMass", "?GetCenterOfMass@Actor@GAME@@UEBA?AVVec3@2@XZ" },
            { "GetFootCoords", "?GetFootCoords@Character@GAME@@MEAA?AVWorldCoords@2@_N@Z" },
            { "GetCoords", "?GetCoords@Entity@GAME@@QEBA?AVWorldCoords@2@XZ" },
            { "GetRegionContainingPoint", "?GetRegionContainingPoint@World@GAME@@QEBAPEAVRegion@2@AEBVIntVec3@2@@Z" },
            { "GetRegionName", "?GetRegionName@World@GAME@@QEBAPEBDH@Z" },
            { "GetMonsterClassification", "?GetMonsterClassification@Monster@GAME@@QEBA?AW4MonsterClassification@2@XZ" },
            { "DesignerCalculateOffensiveAbility", "?DesignerCalculateOffensiveAbility@Character@GAME@@QEAAMM@Z" },
            { "DesignerCalculateDefensiveAbility", "?DesignerCalculateDefensiveAbility@Character@GAME@@QEAAMM@Z" },
            { "GetPlayStats", "?GetPlayStats@Character@GAME@@QEBAAEBVPlayStats@2@XZ" },
            { "GetGameController", "?GetGameController@GameEngine@GAME@@QEAAAEAVGameController@2@XZ" }
        };

        std::ostringstream json;
        json << "{\"type\":\"core_symbols\",\"module\":\"Game.dll/Engine.dll\"";
        for (const auto& probe : probes)
        {
            const auto address = ResolveGameExport(probe.name);
            json << ",\"" << probe.key << "\":\"" << ToHex(static_cast<uintptr_t>(address)) << "\"";
        }
        json << "}\n";
        return json.str();
    }

    std::string TryGetPosition()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getPlayerManagerClientAddress = ResolveGameExport("?GetPlayerManagerClient@GameEngine@GAME@@QEBAPEAVPlayerManagerClient@2@XZ");
        const auto getPlayerIdAddress = ResolveGameExport("?GetPlayerId@GameEngine@GAME@@QEBAIXZ");
        const auto getPlayerLocationAddress = ResolveGameExport("?GetPlayerLocation@PlayerManagerClient@GAME@@QEBA?AVWorldVec3@2@I@Z");
        const auto getWorldPositionAddress = ResolveGameExport("?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ");

        if (!gGameEngineAddress || !getPlayerManagerClientAddress || !getPlayerIdAddress || !getPlayerLocationAddress || !getWorldPositionAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required symbols not resolved\"}\n";
        }

        Vec3 position{};
        if (!ReadPositionInternal(gGameEngineAddress, getPlayerManagerClientAddress, getPlayerIdAddress, getPlayerLocationAddress, getWorldPositionAddress, &position))
        {
            return "{\"type\":\"error\",\"message\":\"failed to read position through game api\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"position\",\"x\":" << position.x << ",\"y\":" << position.y << ",\"z\":" << position.z << "}\n";
        return json.str();
    }

    bool TryReadCenterOfMass(DWORD64 address, void* player, Vec3* out)
    {
        using GetCenterOfMassFn = void(__fastcall*)(void*, Vec3*);

        __try
        {
            reinterpret_cast<GetCenterOfMassFn>(static_cast<uintptr_t>(address))(player, out);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::string TryProbePlayer()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        if (!gGameEngineAddress || !getMainPlayerAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required player symbols not resolved\"}\n";
        }

        void* gameEngine = nullptr;
        void* player = nullptr;
        if (!TryReadPlayerPointers(gGameEngineAddress, getMainPlayerAddress, &gameEngine, &player))
        {
            return "{\"type\":\"error\",\"message\":\"exception while reading player pointers\"}\n";
        }

        if (!gameEngine)
        {
            return "{\"type\":\"error\",\"message\":\"gGameEngine is null (not in game world?)\"}\n";
        }

        if (!player)
        {
            return "{\"type\":\"error\",\"message\":\"main player is null (character not loaded?)\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"probe_player\"";
        json << ",\"gameEngine\":\"" << ToHex(reinterpret_cast<uintptr_t>(gameEngine)) << "\"";
        json << ",\"player\":\"" << ToHex(reinterpret_cast<uintptr_t>(player)) << "\"";

        const auto getCenterOfMassAddress = ResolveGameExport("?GetCenterOfMass@Actor@GAME@@UEBA?AVVec3@2@XZ");
        if (getCenterOfMassAddress)
        {
            Vec3 centerOfMass{};
            if (TryReadCenterOfMass(getCenterOfMassAddress, player, &centerOfMass))
            {
                json << ",\"centerOfMass\":{\"x\":" << centerOfMass.x << ",\"y\":" << centerOfMass.y << ",\"z\":" << centerOfMass.z << "}";
            }
            else
            {
                json << ",\"centerOfMass\":null";
            }
        }

        json << "}\n";
        return json.str();
    }

    std::string TryTeleport(const std::string& command)
    {
        const auto separator = command.find(':');
        if (separator == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"teleport format is teleport:x,y,z\"}\n";
        }

        Vec3 position{};
        if (sscanf_s(command.c_str() + separator + 1, "%f,%f,%f", &position.x, &position.y, &position.z) != 3)
        {
            return "{\"type\":\"error\",\"message\":\"invalid teleport coordinates\"}\n";
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto initiatePlayerTeleportAddress = ResolveGameExport("?InitiatePlayerTeleport@GameEngine@GAME@@QEAAXHHHW4TeleportEffect@2@_N@Z");
        if (!gGameEngineAddress || !initiatePlayerTeleportAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required teleport symbols not resolved\"}\n";
        }

        if (!TeleportInternal(gGameEngineAddress, initiatePlayerTeleportAddress, position))
        {
            return "{\"type\":\"error\",\"message\":\"failed to teleport through game api\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"teleport\",\"x\":" << position.x << ",\"y\":" << position.y << ",\"z\":" << position.z << "}\n";
        return json.str();
    }

    std::string TryGetMoney()
    {
        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getCurrentMoneyAddress = ResolveGameExport("?GetCurrentMoney@Character@GAME@@QEBA?BIXZ");
        if (!gGameEngineAddress || !getMainPlayerAddress || !getCurrentMoneyAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required money symbols not resolved\"}\n";
        }

        unsigned int money = 0;
        if (!ReadMoneyInternal(gGameEngineAddress, getMainPlayerAddress, getCurrentMoneyAddress, &money))
        {
            return "{\"type\":\"error\",\"message\":\"failed to read money through game api\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"money\",\"value\":" << money << "}\n";
        return json.str();
    }

    std::string TrySetMoney(const std::string& command)
    {
        const auto separator = command.find(':');
        if (separator == std::string::npos)
        {
            return "{\"type\":\"error\",\"message\":\"set_money format is set_money:value\"}\n";
        }

        unsigned int targetMoney = 0;
        if (sscanf_s(command.c_str() + separator + 1, "%u", &targetMoney) != 1)
        {
            return "{\"type\":\"error\",\"message\":\"invalid money value\"}\n";
        }

        const auto gGameEngineAddress = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        const auto getMainPlayerAddress = ResolveGameExport("?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ");
        const auto getCurrentMoneyAddress = ResolveGameExport("?GetCurrentMoney@Character@GAME@@QEBA?BIXZ");
        const auto addMoneyAddress = ResolveGameExport("?AddMoney@Character@GAME@@QEAAXI@Z");
        const auto subtractMoneyAddress = ResolveGameExport("?SubtractMoney@Character@GAME@@QEAA?BII@Z");
        if (!gGameEngineAddress || !getMainPlayerAddress || !getCurrentMoneyAddress || !addMoneyAddress || !subtractMoneyAddress)
        {
            return "{\"type\":\"error\",\"message\":\"required money symbols not resolved\"}\n";
        }

        unsigned int finalMoney = 0;
        if (!SetMoneyInternal(gGameEngineAddress, getMainPlayerAddress, getCurrentMoneyAddress, addMoneyAddress, subtractMoneyAddress, targetMoney, &finalMoney))
        {
            return "{\"type\":\"error\",\"message\":\"failed to set money through game api\"}\n";
        }

        std::ostringstream json;
        json << "{\"type\":\"money\",\"value\":" << finalMoney << "}\n";
        return json.str();
    }

    std::string HandleCommand(const std::string& command)
    {
        if (command.find("ping") != std::string::npos)
        {
            return "{\"type\":\"pong\",\"plugin\":\"GrimDawnTeleporter.Plugin\"}\n";
        }

        if (command.find("get_status") != std::string::npos)
        {
            return "{\"type\":\"status\",\"ready\":false,\"message\":\"plugin loaded; symbol resolver not fully implemented\"}\n";
        }

        if (command.find("resolve_core") != std::string::npos)
        {
            return ResolveCoreSymbols();
        }

        if (command.find("diagnose_symbols") != std::string::npos)
        {
            return DiagnoseGameSymbolStrings();
        }

        if (command.find("diagnose_aob") != std::string::npos)
        {
            return DiagnoseAobPatterns();
        }

        if (command.rfind("resolve_symbol:", 0) == 0)
        {
            auto name = command.substr(15);
            while (!name.empty() && (name.back() == '\r' || name.back() == '\n'))
            {
                name.pop_back();
            }

            auto address = ResolveGameExport(name.c_str());
            if (address == 0)
            {
                SymbolResolver resolver;
                if (!resolver.Initialize())
                {
                    return "{\"type\":\"error\",\"message\":\"symbol resolver init failed\"}\n";
                }

                const auto wide = NarrowToWide(name);
                address = resolver.Resolve(wide.c_str());
            }
            std::wstringstream stream;
            stream << L"{\"type\":\"resolve_symbol\",\"address\":\"0x" << std::hex << address << L"\"}\n";
            const auto text = stream.str();
            return WideToNarrow(text);
        }

        if (command.rfind("get_module:", 0) == 0)
        {
            auto name = command.substr(11);
            while (!name.empty() && (name.back() == '\r' || name.back() == '\n'))
            {
                name.pop_back();
            }

            const auto module = GetModuleInfoByName(NarrowToWide(name));
            if (!module)
            {
                return "{\"type\":\"error\",\"message\":\"module not found\"}\n";
            }

            std::wstringstream stream;
            stream << L"{\"type\":\"module\",\"base\":\"0x" << std::hex << module->base << L"\",\"size\":\"0x" << std::hex << module->size << L"\"}\n";
            const auto text = stream.str();
            return WideToNarrow(text);
        }

        if (command.rfind("scan_pattern:", 0) == 0)
        {
            const auto sep = command.find('|');
            if (sep == std::string::npos)
            {
                return "{\"type\":\"error\",\"message\":\"scan_pattern format is scan_pattern:<module>|<hex bytes>\"}\n";
            }

            const auto moduleName = command.substr(13, sep - 13);
            auto patternText = command.substr(sep + 1);
            while (!patternText.empty() && (patternText.back() == '\r' || patternText.back() == '\n'))
            {
                patternText.pop_back();
            }

            const auto module = GetModuleInfoByName(NarrowToWide(moduleName));
            if (!module)
            {
                return "{\"type\":\"error\",\"message\":\"module not found\"}\n";
            }

            std::vector<int> pattern;
            if (!PatternScanner::ParsePattern(patternText, pattern))
            {
                return "{\"type\":\"error\",\"message\":\"invalid pattern\"}\n";
            }

            const auto address = PatternScanner::Find(reinterpret_cast<const uint8_t*>(module->base), module->size, pattern);
            std::wstringstream stream;
            stream << L"{\"type\":\"scan_pattern\",\"address\":\"0x" << std::hex << address << L"\"}\n";
            const auto text = stream.str();
            return WideToNarrow(text);
        }

        if (command.find("get_position") != std::string::npos)
        {
            return TryGetPosition();
        }

        if (command.find("probe_player") != std::string::npos)
        {
            return TryProbePlayer();
        }

        if (command.find("find_world") != std::string::npos)
        {
            return TryFindWorld();
        }

        if (command.rfind("probe_entities", 0) == 0)
        {
            return TryProbeEntities(command);
        }

        if (command.rfind("probe_items", 0) == 0)
        {
            return TryProbeItems(command);
        }

        if (command.rfind("probe_item_detail", 0) == 0)
        {
            return TryProbeItemDetail(command);
        }

        if (command.rfind("loot_items", 0) == 0)
        {
            return TryLootItems(command);
        }

        if (command.rfind("find_controller", 0) == 0)
        {
            return TryFindController();
        }

        if (command.rfind("probe_pickup", 0) == 0)
        {
            return TryProbePickup();
        }

        if (command.rfind("fix_pickup", 0) == 0)
        {
            return TryFixPickup();
        }

        if (command.rfind("reset_cache", 0) == 0)
        {
            return TryResetCache();
        }

        if (command.rfind("find_chest", 0) == 0)
        {
            return TryFindChest();
        }

        if (command.rfind("open_chest", 0) == 0)
        {
            return TryOpenChest(command);
        }

        if (command.rfind("pickup_id", 0) == 0)
        {
            return TryPickupId(command);
        }

        if (command.find("is_in_game") != std::string::npos)
        {
            return TryIsInGame();
        }

        if (command.find("exit_to_menu") != std::string::npos)
        {
            return TryExitToMenu();
        }

        if (command.find("auto_kill_status") != std::string::npos)
        {
            return TryAutoKillStatus();
        }

        if (command.rfind("list_entity_types", 0) == 0)
        {
            return TryListEntityTypes(command);
        }

        if (command.rfind("kill_monsters", 0) == 0)
        {
            return TryKillMonsters(command);
        }

        if (command.rfind("break_containers", 0) == 0)
        {
            return TryBreakContainers(command);
        }

        if (command.find("get_money") != std::string::npos)
        {
            return TryGetMoney();
        }

        if (command.rfind("set_money:", 0) == 0)
        {
            return TrySetMoney(command);
        }

        if (command.find("can_teleport") != std::string::npos)
        {
            return TryCanTeleport();
        }

        if (command.rfind("teleport_world:", 0) == 0)
        {
            return TryTeleportWorld(command);
        }

        if (command.find("teleport") != std::string::npos)
        {
            return TryTeleport(command);
        }
        
        // 作弊功能命令
        if (command.rfind("god_mode:", 0) == 0)
        {
            return TrySetGodMode(command);
        }

        if (command.find("get_god_mode") != std::string::npos)
        {
            return TryGetGodModeStatus();
        }
        
        if (command.rfind("insta_kill:", 0) == 0)
        {
            return TryInstaKill(command);
        }
        
        if (command.rfind("one_shot:", 0) == 0)
        {
            return TrySetOneShot(command);
        }
        
        if (command.rfind("modify_attribute:", 0) == 0)
        {
            return TryModifyAttribute(command);
        }

        // Test commands
        if (command.find("list_modules") != std::string::npos)
        {
            return ListModules();
        }

        return "{\"type\":\"error\",\"message\":\"unknown command\"}\n";
    }

    void RunPipeServer()
    {
        const auto pipeName = GetPipeName();
        Log(L"pipe server starting: " + pipeName);

        while (!g_stop.load())
        {
            HANDLE pipe = CreateNamedPipeW(
                pipeName.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1,
                4096,
                4096,
                1000,
                nullptr);

            if (pipe == INVALID_HANDLE_VALUE)
            {
                Log(L"CreateNamedPipeW failed");
                Sleep(1000);
                continue;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
            {
                CloseHandle(pipe);
                continue;
            }

            char buffer[2048]{};
            DWORD read = 0;
            if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0)
            {
                buffer[read] = '\0';
                std::string command(buffer, read);
                while (!command.empty() && (command.back() == '\r' || command.back() == '\n' || command.back() == ' ' || command.back() == '\t'))
                {
                    command.pop_back();
                }

                const auto response = HandleCommand(command);
                DWORD written = 0;
                WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }

        Log(L"pipe server stopped");
    }

    DWORD WINAPI WorkerThread(LPVOID)
    {
        Log(L"plugin loaded");
        const auto engine = ResolveGameExport("?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
        if (engine != 0)
        {
            std::wstringstream stream;
            stream << L"gGameEngine export resolved: 0x" << std::hex << engine;
            Log(stream.str());
        }
        else
        {
            Log(L"gGameEngine export not resolved");
        }
        RunPipeServer();
        FreeLibraryAndExitThread(g_module, 0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        g_worker = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, AutoKillThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_stop.store(true);
    }

    return TRUE;
}
