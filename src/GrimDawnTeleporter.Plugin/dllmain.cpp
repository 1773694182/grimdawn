#include <windows.h>
#include <psapi.h>

#include <dbghelp.h>

#include <atomic>
#include <fstream>
#include <optional>
#include <vector>
#include <sstream>
#include <string>

namespace
{
    std::atomic_bool g_stop{ false };
    HMODULE g_module{};
    HANDLE g_worker{};
    HANDLE g_process{ GetCurrentProcess() };
    std::atomic_bool g_symbolsInitialized{ false };

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
            "InitiatePlayerTeleport@GameEngine@GAME"
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
            { "SubtractMoney", "?SubtractMoney@Character@GAME@@QEAA?BII@Z" }
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

        if (command.find("get_money") != std::string::npos)
        {
            return TryGetMoney();
        }

        if (command.rfind("set_money:", 0) == 0)
        {
            return TrySetMoney(command);
        }

        if (command.find("teleport") != std::string::npos)
        {
            return TryTeleport(command);
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
                const auto response = HandleCommand(buffer);
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
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_stop.store(true);
    }

    return TRUE;
}
