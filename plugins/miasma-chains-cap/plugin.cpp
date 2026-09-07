// chain-limit - caps the number of simultaneously active chain missiles.
//
// WHAT IT DOES
//   Intercepts missiles.txt pSrvDoFunc 45 (the per-frame step of "miasmachains")
//   by replacing its entry in the server do-func table. On every tick it works out
//   how many newer chains the same caster owns; once that reaches the limit, the
//   oldest chain is destroyed. Net effect: at most N chains alive per caster, and
//   casting past the cap drops the oldest instead of failing the cast.
//
//   The limit N comes from skills.txt calc4 on the skill that fired the missile,
//   evaluated per cast against the caster at the missile's skill level, exactly
//   like any other skill formula. calc4 blank or <= 0 means unlimited, so this
//   plugin is inert for every other missile that happens to use pSrvDoFunc 45.
//
// WHY A TABLE SWAP AND NOT A CODE HOOK
//   sub_140457B10 has no direct callers in the image. Its only inbound reference
//   is the table slot, so replacing the slot intercepts 100% of calls without
//   modifying a single instruction. Fully reversible on unload.
//
// VERIFIED AGAINST D2R 3.3 (image base 0x140000000)
//   2390E80  pSrvDoFunc table, entry N at base + 8N, count at 2391400 (= 53)
//   2390FE8  slot 45, must contain 457B10
//   457B10   sub_140457B10, psrvdofunc 45, called (pGame, pMissile); return 2 destroys
//   490300   SUNIT_GetOwner(pGame, pMissile)
//   3BC450   MISSILE_GetSkill(pMissile)
//   3BB470   MISSILE_GetLevel(pMissile)
//   97790    SKILLS_GetSkillsTxtRecord(gameVersion, skillId)   stride 748
//   3B5160   SKILLS_EvaluateFormula(gameVersion, pUnit, poolOffset, skillId, level)
//   +412     skills.txt calc4 in the skill record (calc1 400, calc2 404, calc3 408)
//   +262     gameVersion byte on pGame
//   +368     dwGameFrame on pGame

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace {

// ---------------------------------------------------------------- constants

constexpr uintptr_t kRvaDoFuncTable   = 0x2390E80;
constexpr int       kChainDoFuncIndex = 45;
constexpr uintptr_t kRvaChainDoFunc   = 0x457B10;

constexpr uintptr_t kRvaGetOwner        = 0x490300;
constexpr uintptr_t kRvaMissileGetSkill = 0x3BC450;
constexpr uintptr_t kRvaMissileGetLevel = 0x3BB470;
constexpr uintptr_t kRvaGetSkillRecord  = 0x97790;
constexpr uintptr_t kRvaEvaluateFormula = 0x3B5160;

constexpr int kSkillRecCalc4    = 412;
constexpr int kGameVersionByte  = 262;
constexpr int kGameFrameCounter = 368;

// A missile that has not ticked for this many frames is gone; drop its entry.
constexpr uint32_t kStaleFrames = 4;
// Panic valve so a long session with many casters cannot grow the map forever.
constexpr size_t kMaxTrackedOwners = 256;

// The dispatcher destroys the missile when the do-func returns 2.
constexpr int64_t kDestroyMissile = 2;

// ---------------------------------------------------------------- engine ABI

using ChainDoFunc     = int64_t(__fastcall*)(void* game, void* missile);
using GetOwnerFunc    = void*  (__fastcall*)(void* game, void* missile);
using MissileIntFunc  = int    (__fastcall*)(void* missile);
using GetSkillRecFunc = void*  (__fastcall*)(uint8_t version, int skillId);
using EvalFormulaFunc = int    (__fastcall*)(uint8_t version, void* unit, uint32_t poolOffset,
                                             int skillId, int level);

const D2RL::PluginContext* g_ctx = nullptr;
uintptr_t                  g_base = 0;
void**                     g_slot = nullptr;

ChainDoFunc     g_originalChainDo = nullptr;
GetOwnerFunc    g_getOwner        = nullptr;
MissileIntFunc  g_missileGetSkill = nullptr;
MissileIntFunc  g_missileGetLevel = nullptr;
GetSkillRecFunc g_getSkillRecord  = nullptr;
EvalFormulaFunc g_evaluateFormula = nullptr;

// ---------------------------------------------------------------- tracking

struct ChainEntry {
    uint32_t serial;     // 1-based creation order within this caster
    uint32_t lastFrame;  // last game frame this missile ticked
};

struct OwnerState {
    uint32_t counter = 0;  // monotonically increasing, never decremented
    std::unordered_map<const void*, ChainEntry> chains;
};

std::mutex g_mutex;
std::unordered_map<const void*, OwnerState> g_owners;
bool g_loggedFirstLimit = false;

void LogFmt(const char* fmt, ...) {
    if (!g_ctx) return;
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    va_end(args);
    g_ctx->LogInfo(buffer);
}

// ---------------------------------------------------------------- the hook

int64_t __fastcall ChainDoFuncHook(void* game, void* missile) noexcept {
    if (!game || !missile || !g_originalChainDo) {
        return g_originalChainDo ? g_originalChainDo(game, missile) : 0;
    }

    void* owner = g_getOwner(game, missile);
    if (!owner) return g_originalChainDo(game, missile);

    const uint8_t version = *reinterpret_cast<uint8_t*>(static_cast<char*>(game) + kGameVersionByte);
    const int     skillId = g_missileGetSkill(missile);

    void* record = g_getSkillRecord(version, skillId);
    if (!record) return g_originalChainDo(game, missile);

    // calc4 -> the cap. Blank column lands out of the formula pool and evaluates to 0.
    const uint32_t poolOffset = *reinterpret_cast<uint32_t*>(static_cast<char*>(record) + kSkillRecCalc4);
    const int      level      = g_missileGetLevel(missile);
    const int      limit      = g_evaluateFormula(version, owner, poolOffset, skillId, level);
    if (limit <= 0) return g_originalChainDo(game, missile);  // 0 = unlimited

    const uint32_t frame = *reinterpret_cast<uint32_t*>(static_cast<char*>(game) + kGameFrameCounter);
    bool destroy = false;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_owners.size() > kMaxTrackedOwners) g_owners.clear();

        OwnerState& state = g_owners[owner];
        auto it = state.chains.find(missile);

        if (it == state.chains.end()) {
            // First tick of this chain: hand it the next serial, then drop entries
            // for chains that have stopped ticking (expired, collided, zone change).
            const uint32_t serial = ++state.counter;
            for (auto p = state.chains.begin(); p != state.chains.end();) {
                p = (frame - p->second.lastFrame > kStaleFrames) ? state.chains.erase(p) : std::next(p);
            }
            it = state.chains.emplace(missile, ChainEntry{serial, frame}).first;

            if (!g_loggedFirstLimit) {
                g_loggedFirstLimit = true;
                LogFmt("chain-limit active: skill %d level %d calc4 -> limit %d", skillId, level, limit);
            }
        } else {
            it->second.lastFrame = frame;
        }

        // "limit" chains newer than me exist, so I am the one that falls off.
        destroy = (state.counter - it->second.serial) >= static_cast<uint32_t>(limit);
    }

    if (destroy) return kDestroyMissile;
    return g_originalChainDo(game, missile);
}

// ---------------------------------------------------------------- install

bool WriteSlot(void* value) noexcept {
    DWORD previous = 0;
    if (!VirtualProtect(g_slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    *g_slot = value;
    DWORD ignored = 0;
    VirtualProtect(g_slot, sizeof(void*), previous, &ignored);
    return true;
}

}  // namespace

extern "C" {

D2RL_PLUGIN_EXPORT const D2RL::PluginInfo* D2RLoaderGetPluginInfo() noexcept {
    static const D2RL::PluginInfo info{
        .infoSize    = D2RL::PluginInfoSize,
        .apiVersion  = D2RL_PLUGIN_API_VERSION,
        .id          = "celestialrayone.chain-limit",
        .name        = "Chain Limit",
        .version     = "1.0.0",
        .author      = "CelestialRayOne",
        .description = "Caps simultaneously active chain missiles (pSrvDoFunc 45) using the skill's calc4.",
        .flags       = D2RL::PluginFlags::Server,
    };
    return &info;
}

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const D2RL::PluginContext* ctx) noexcept {
    g_ctx = ctx;

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!g_base) {
        ctx->LogError("chain-limit: cannot resolve the D2R.exe module base");
        return false;
    }

    g_getOwner        = reinterpret_cast<GetOwnerFunc>(g_base + kRvaGetOwner);
    g_missileGetSkill = reinterpret_cast<MissileIntFunc>(g_base + kRvaMissileGetSkill);
    g_missileGetLevel = reinterpret_cast<MissileIntFunc>(g_base + kRvaMissileGetLevel);
    g_getSkillRecord  = reinterpret_cast<GetSkillRecFunc>(g_base + kRvaGetSkillRecord);
    g_evaluateFormula = reinterpret_cast<EvalFormulaFunc>(g_base + kRvaEvaluateFormula);

    g_slot = reinterpret_cast<void**>(g_base + kRvaDoFuncTable + sizeof(void*) * kChainDoFuncIndex);
    void* const expected = reinterpret_cast<void*>(g_base + kRvaChainDoFunc);

    if (*g_slot != expected) {
        char buffer[192];
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                    "chain-limit: pSrvDoFunc slot 45 holds %p, expected %p - wrong game build, refusing to load",
                    *g_slot, expected);
        ctx->LogError(buffer);
        return false;
    }

    g_originalChainDo = reinterpret_cast<ChainDoFunc>(*g_slot);
    if (!WriteSlot(reinterpret_cast<void*>(&ChainDoFuncHook))) {
        ctx->LogError("chain-limit: VirtualProtect failed on the pSrvDoFunc table");
        g_originalChainDo = nullptr;
        return false;
    }

    ctx->LogInfo("chain-limit: installed on pSrvDoFunc 45, limit source is skills.txt calc4");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (g_slot && g_originalChainDo) {
        WriteSlot(reinterpret_cast<void*>(g_originalChainDo));
        g_originalChainDo = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_owners.clear();
}

}  // extern "C"
