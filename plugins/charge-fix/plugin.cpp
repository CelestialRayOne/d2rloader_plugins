// ---------------------------------------------------------------------------
// Charge Pursuit  -  D2RLoader plugin (D2R 3.3, D2RLoader process image)
//
// Makes monster Charge keep going until it hits an enemy instead of stopping
// a few frames after it starts.
//
// Monster Charge is skills.txt srvstfunc 31 (the launch) plus srvdofunc 67
// (the per tick body). A monster whose monstats.txt skill mode for the charge
// names a sequence (seq_...) runs it in monster mode 14, and in 3.3 that mode
// ends a charge early in five independent ways:
//
//   1. The sequence runs out. Mode 14 ends when the sequence's remaining
//      length (unit+0x64) reaches 0. srvdofunc 67 tries to keep the charge
//      alive by "rewinding" its animation, but the rewind (0x4914E0) only
//      resets the plain-mode animation fields (unit+0x5C/+0x60); it never
//      touches a sequence. A sequence charge therefore lasts exactly one pass
//      of its monseq.txt rows: a 5 row sequence moves the monster 5 ticks.
//   2. The per tick body only runs on tagged rows. Mode 14 starts with a
//      recurring tick event, but the first time srvdofunc 67 runs it deletes
//      every tick event and books a single one for the next frame, which only
//      it re-books. The mode 14 handler (0x446770) only calls it when the
//      current row carries event 1, 2 or 4, so the first untagged tick (or
//      any tick a slowed animation spends on the same row) leaves nothing
//      booked and the monster freezes on the server.
//   3. Packmates block. The charge steps with the monster's walking mask,
//      which treats the "unit present" bit (0x1000) that every monster and
//      player stamps on its own cell as a wall.
//   4. The distance budget runs out. The launch hardcodes 20 steps (engine cap
//      77); each subtile moved costs one.
//   5. The hit never lands on short sequences. On contact srvdofunc 67 defers
//      the hit to "the next skill tick", which it expects its rewind to book
//      from the rows after frame 8. A sequence with no tagged row there books
//      nothing, so the mode ends without the hit.
//
// What this plugin changes
//   keep_sequence_alive  while charging, the sequence's remaining length is
//                        topped up every tick, so the sequence loops (its row
//                        index already wraps on its own).
//   skill_every_tick     the mode 14 handler's read of "this tick's action
//                        event" (call at 0x44680C) answers "action" for a
//                        charge in progress, so srvdofunc 67 runs every tick
//                        and keeps re-booking it, the way the attack/skill
//                        mode handler already treats a charge.
//   impact_on_contact    the two calls from the mode 14 handler into the skill
//                        body (0x446802 path ended, 0x44683B action tick) go
//                        through a wrapper: when a charge has just made
//                        contact and the engine has no follow-up tick booked,
//                        the deferred hit is delivered on the same tick.
//   pass_through_allies  while a hostile monster charges, its movement uses
//                        the walking mask minus 0x1000 plus the player bit
//                        0x80 (movement 0x3812D0, launch 0x564DC0).
//   unlimited_distance   while charging, the budget is refilled every tick.
//   client_mirror        the movement rules and the sequence top-up also apply
//                        to the client's predicted copy of the monster.
//
// Hook sites, all verified byte for byte before anything is written:
//   0x564DC0  srvstfunc 31 entry (inline hook)
//   0x3812D0  per tick path update entry, shared by server and client
//             (inline hook). Callers: server 0x491AC0, client 0x0F9AEA and
//             0x1B5512. Second argument is a float in xmm1.
//   0x44680C  call 0x349E30 (action event getter) inside the mode 14 handler
//   0x446802  call 0x436B10 (skill perform) inside it, path ended branch
//   0x44683B  call 0x436B10 inside it, action tick branch
//   The three call sites are retargeted through a relay page; the skill
//   perform entry itself is NOT hooked (dynamic-etype owns it), it is called,
//   so its hook still sees every perform. srvdofunc 67 (0x5622C0) is not
//   touched: charge-aoe-hits owns its entry and dynamic-etype patches inside.
//
// Functions called: 0x34BA40 UNITS_GetUsedSkill, 0x436B10 skill perform
// (game, unit), 0x48B230 earliest booked frame of a unit's events
// (game, unit, type, customId), 0 when nothing is booked.
//
// Layouts used, each read from the 3.3 binary:
//   unit   +0x00 type (1 monster), +0x04 class id, +0x0C mode, +0x38 path,
//          +0x40 sequence rows, +0x48 sequence length (x256), +0x54 sequence
//          speed, +0x64 sequence remaining (decremented by 0x3476B0),
//          +0x6A action event (byte), +0x100 skill list, +0x128 bit 27
//          animation finished
//   path   +0x02/+0x06 position, +0x10/+0x12 target, +0x30 point index,
//          +0x34 point count, +0x48 flags (0x10 client copy), +0x50 path type
//          (7 straight, set by the launch), +0x5C collision pattern, +0x64
//          move test mask, +0x68 collided-with mask, +0xBC distance budget
//   skill  +0x00 skills.txt record, +0x14 flags (bit 0 charging),
//          +0x2C/+0x30 target unit type / id (type 6 = none)
//   record +0x4E srvdofunc (word)
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uintptr_t SrvStTableRva        = 0x238E710;
constexpr std::uintptr_t SrvDoTableRva        = 0x238EA00;
constexpr std::uint32_t  ChargeStartSlot      = 31;
constexpr std::uint32_t  ChargeDoSlot         = 67;
constexpr std::uintptr_t ChargeStartRva       = 0x564DC0;
constexpr std::uintptr_t ChargeDoRva          = 0x5622C0;
constexpr std::uintptr_t PathUpdateRva        = 0x3812D0;
constexpr std::uintptr_t GetUsedSkillRva      = 0x34BA40;
constexpr std::uintptr_t GetSkillFlagsRva     = 0x33CC00;
constexpr std::uintptr_t ActionEventGetterRva = 0x349E30;
constexpr std::uintptr_t SkillPerformRva      = 0x436B10;
constexpr std::uintptr_t EarliestEventRva     = 0x48B230;
constexpr std::uintptr_t SequenceUpdateRva    = 0x446770;
constexpr std::uintptr_t PathEndWindowRva     = 0x4467FC;
constexpr std::uintptr_t PathEndCallRva       = 0x446802;
constexpr std::uintptr_t ActionEventWindowRva = 0x446809;
constexpr std::uintptr_t ActionEventCallRva   = 0x44680C;
constexpr std::uintptr_t ActionTickWindowRva  = 0x446835;
constexpr std::uintptr_t ActionTickCallRva    = 0x44683B;

// sub_140564DC0 prologue up to sub rsp,0A0h.
constexpr auto ChargeStartExpected = std::to_array<std::uint8_t>({
    0x44, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
});

// sub_1403812D0 prologue up to movaps [rsp+30h],xmm6.
constexpr auto PathUpdateExpected = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x40, 0x0F, 0x29, 0x74, 0x24, 0x30,
});

// UNITS_GetUsedSkill: push rbx / sub rsp,20h / mov rbx,rcx / test rcx,rcx / jnz.
constexpr auto GetUsedSkillExpected = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x13,
});

// SKILLS_GetFlags: mov eax,[rcx+14h]. Proves the flags offset read inline.
constexpr auto GetSkillFlagsExpected = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xC9, 0x74, 0x04, 0x8B, 0x41, 0x14, 0xC3, 0x33, 0xC0, 0xC3,
});

// Action event getter: prologue, and at +0x21 movzx eax,byte [rbx+6Ah].
// Proves the action event offset read inline.
constexpr auto ActionEventGetterExpected = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x1D,
});
constexpr std::uintptr_t ActionEventReadRva = 0x349E51;
constexpr auto ActionEventReadExpected = std::to_array<std::uint8_t>({
    0x0F, 0xB6, 0x43, 0x6A,
});

// Skill perform prologue up to sub rsp,40h (the same window dynamic-etype
// checks; only verified here, never patched).
constexpr auto SkillPerformExpected = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
    0x56, 0x48, 0x83, 0xEC, 0x40,
});

// Earliest booked event frame prologue up to sub rsp,30h.
constexpr auto EarliestEventExpected = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
    0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
});

// Mode 14 handler, path ended branch:
// mov rdx,rbx / mov rcx,rdi / call 0x436B10 / xor ebp,ebp
constexpr auto PathEndWindowExpected = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x09, 0x03, 0xFF, 0xFF, 0x33, 0xED,
});
constexpr auto PathEndCallExpected = std::to_array<std::uint8_t>({
    0xE8, 0x09, 0x03, 0xFF, 0xFF,
});

// Mode 14 handler, action event read:
// mov rcx,rbx / call 0x349E30 / mov r14,[rsp+40h] / mov esi,eax / cmp eax,4
constexpr auto ActionEventWindowExpected = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0xCB, 0xE8, 0x1F, 0x36, 0xF0, 0xFF, 0x4C, 0x8B,
    0x74, 0x24, 0x40, 0x8B, 0xF0, 0x83, 0xF8, 0x04,
});
constexpr auto ActionEventCallExpected = std::to_array<std::uint8_t>({
    0xE8, 0x1F, 0x36, 0xF0, 0xFF,
});

// Mode 14 handler, action tick branch:
// mov rdx,rbx / mov rcx,rdi / call 0x436B10 / movss xmm1,[rip+...]
constexpr auto ActionTickWindowExpected = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0xD0, 0x02, 0xFF, 0xFF,
    0xF3, 0x0F, 0x10, 0x0D,
});
constexpr auto ActionTickCallExpected = std::to_array<std::uint8_t>({
    0xE8, 0xD0, 0x02, 0xFF, 0xFF,
});

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

constexpr std::size_t UnitType            = 0x00;
constexpr std::size_t UnitClassId         = 0x04;
constexpr std::size_t UnitMode            = 0x0C;
constexpr std::size_t UnitPath            = 0x38;
constexpr std::size_t UnitSequence        = 0x40;
constexpr std::size_t UnitSequenceLength  = 0x48;
constexpr std::size_t UnitSequenceSpeed   = 0x54;
constexpr std::size_t UnitSequenceLeft    = 0x64;
constexpr std::size_t UnitActionEvent     = 0x6A;
constexpr std::size_t UnitSkillList       = 0x100;
constexpr std::size_t UnitStateFlags      = 0x128;
constexpr std::uint32_t UnitAnimationEnded = 1U << 27;
constexpr std::uint32_t UnitTypeMonster   = 1;
constexpr std::uint32_t ModeSequence      = 14;

constexpr std::size_t PathPosX            = 0x02;
constexpr std::size_t PathPosY            = 0x06;
constexpr std::size_t PathTargetX         = 0x10;
constexpr std::size_t PathTargetY         = 0x12;
constexpr std::size_t PathPointIndex      = 0x30;
constexpr std::size_t PathPointCount      = 0x34;
constexpr std::size_t PathFlags           = 0x48;
constexpr std::size_t PathType            = 0x50;
constexpr std::size_t PathPattern         = 0x5C;
constexpr std::size_t PathMoveMask        = 0x64;
constexpr std::size_t PathCollided        = 0x68;
constexpr std::size_t PathDistance        = 0xBC;
constexpr std::uint32_t PathClientFlag    = 0x10;
constexpr std::uint32_t PathTypeStraight  = 7;

constexpr std::size_t SkillRecord         = 0x00;
constexpr std::size_t SkillFlags          = 0x14;
constexpr std::size_t SkillTargetType     = 0x2C;
constexpr std::uint32_t SkillChargingBit  = 0x1;
constexpr std::int32_t  NoTargetType      = 6;
constexpr std::size_t RecordSrvDoFunc     = 0x4E;

constexpr std::int32_t  EventModeChange   = 0;

// Collision bits (D2Collision.h) and the two stock monster walking masks.
constexpr std::uint32_t MaskDoorOpener    = 0x3401;   // WALL|OBJECT|NO_PATH|PET
constexpr std::uint32_t MaskMonsterPath   = 0x3C01;   // the same plus DOOR
constexpr std::uint32_t CollideUnitHere   = 0x1000;   // NO_PATH, a unit's centre
constexpr std::uint32_t CollidePlayer     = 0x0080;   // a player's footprint

// Collision patterns that mark a pet footprint (D2Common_11281).
constexpr std::uint32_t PatternSmallPet   = 3;
constexpr std::uint32_t PatternBigPet     = 4;

// PATH_SetNewDistance (0x342860) clamps to 77: a path holds 78 points.
constexpr std::uint8_t  MaxDistance       = 77;

constexpr std::size_t   RelayBytes        = 4'096;
constexpr std::size_t   RelayStubBytes    = 12;       // mov rax,imm64 / jmp rax
constexpr std::size_t   MaximumConfigBytes = 16'384;
constexpr std::uint32_t MaximumLogLines   = 400;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    bool enabled           = true;
    bool keepSequenceAlive = true;
    bool skillEveryTick    = true;
    bool impactOnContact   = true;
    bool passThroughAllies = true;
    bool unlimitedDistance = true;
    bool clientMirror      = true;
    bool debugLog          = false;
};

constexpr char DefaultConfigToml[] =
    "# Charge Pursuit\n"
    "#\n"
    "# Monster Charge (skills.txt srvstfunc 31 + srvdofunc 67) keeps going\n"
    "# until it hits an enemy, instead of stopping a few frames in and jittering\n"
    "# on the spot.\n"
    "#\n"
    "# The first three switches are about monsters whose monstats.txt skill\n"
    "# mode for the charge names a sequence (seq_...). That puts the charge in\n"
    "# monster mode 14, and mode 14 ends a charge early in ways the plain modes\n"
    "# (A1, S1, ...) do not. With every switch on, a charge ends only when it\n"
    "# connects with an enemy, when its target dies or disappears, or when\n"
    "# terrain leaves no route at all.\n"
    "#\n"
    "# Players are never affected: everything here is monster only.\n"
    "# Console: type charge-pursuit for the live state and counters.\n"
    "\n"
    "[charge_pursuit]\n"
    "\n"
    "# Master switch. false installs nothing.\n"
    "enabled = true\n"
    "\n"
    "# The sequence running out.\n"
    "#   Cause    a mode 14 charge ends when its monseq.txt sequence has played\n"
    "#            once. The engine's own attempt to restart the animation only\n"
    "#            works for plain modes, so a 5 row sequence means 5 frames of\n"
    "#            charge, then the monster stops.\n"
    "#   Fix      while the charge lasts, the sequence keeps looping.\n"
    "#   Default  true\n"
    "keep_sequence_alive = true\n"
    "\n"
    "# The charge freezing on the server.\n"
    "#   Cause    once the charge's per frame logic has run, it alone books the\n"
    "#            monster's next frame, but mode 14 only runs it on rows whose\n"
    "#            monseq.txt event column is 1, 2 or 4. The first row with event\n"
    "#            0, or a slowed monster spending two frames on one row, leaves\n"
    "#            no next frame booked and the monster freezes in place.\n"
    "#   Fix      while the charge lasts, every frame counts as an action\n"
    "#            frame, so the charge logic runs every frame whatever the\n"
    "#            event column says.\n"
    "#   Default  true\n"
    "skill_every_tick = true\n"
    "\n"
    "# The hit not landing.\n"
    "#   Cause    on contact the charge hands its hit to a later frame that it\n"
    "#            expects the sequence's rows after frame 8 to book. A short\n"
    "#            sequence has no such rows, so the mode ends without the hit\n"
    "#            (and without anything the hit triggers, such as\n"
    "#            charge-aoe-hits' missiles).\n"
    "#   Fix      when the charge makes contact and nothing is booked to deliver\n"
    "#            the hit, it is delivered on the same frame.\n"
    "#   Default  true\n"
    "impact_on_contact = true\n"
    "\n"
    "# Packmates blocking the charge.\n"
    "#   Cause    the charge moves with the monster's walking rules, which treat\n"
    "#            every other monster as a wall. The first packmate on the line\n"
    "#            ends the charge.\n"
    "#   Fix      while a hostile monster charges, other hostile monsters no\n"
    "#            longer block it, the same way Whirlwind passes through units.\n"
    "#            Players, hirelings, summons, walls, objects and doors still\n"
    "#            stop it, and touching an enemy is what triggers the hit.\n"
    "#            Normal walking is untouched. Charging pets (bound demons,\n"
    "#            summons) keep the vanilla rules.\n"
    "#   Default  true\n"
    "pass_through_allies = true\n"
    "\n"
    "# The distance budget.\n"
    "#   Cause    the launch gives every monster charge a hardcoded budget of 20\n"
    "#            steps, one per subtile moved. When it runs out the charge\n"
    "#            cannot re-aim at a moving target and stops.\n"
    "#   Fix      the budget is refilled every frame while the charge lasts.\n"
    "#   Default  true\n"
    "unlimited_distance = true\n"
    "\n"
    "# Apply the movement rules and the sequence looping to the client's own copy\n"
    "# of the charging monster as well. The client predicts monster movement and\n"
    "# animation with the same code; left on the vanilla rules it would stop the\n"
    "# monster on screen while the server carries on.\n"
    "#   Default  true\n"
    "client_mirror = true\n"
    "\n"
    "# Writes one line per charge launch, per contact, and each time charge\n"
    "# movement ends (what it touched, where, and the remaining budget) to this\n"
    "# plugin's log. Touching mask 0x0080 (player) or 0x2000 (pet) is a normal\n"
    "# hit. Capped at 400 lines per session. Leave off for normal play.\n"
    "#   Default  false\n"
    "debug_log = false\n";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

using ChargeStartFn   = std::int64_t (*)(void* game, void* unit, std::int32_t skillId, std::int32_t skillLevel) noexcept;
using PathUpdateFn    = std::int64_t (*)(void* unit, float step, std::uint8_t computeVectors) noexcept;
using GetUsedSkillFn  = void* (*)(void* unit) noexcept;
using SkillPerformFn  = std::int64_t (*)(void* game, void* unit) noexcept;
using EarliestEventFn = std::uint32_t (*)(void* game, void* unit, std::int32_t type, std::int32_t customId) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t*              Base{};
Config                     Settings{};
void*                      RelayPage{};

ChargeStartFn   OriginalChargeStart{};
PathUpdateFn    OriginalPathUpdate{};
GetUsedSkillFn  GetUsedSkill{};
SkillPerformFn  SkillPerform{};
EarliestEventFn EarliestEvent{};

bool StartHookInstalled{};
bool PathHookInstalled{};
bool EveryTickInstalled{};
bool ImpactInstalled{};

struct Counters {
    std::atomic<std::uint32_t> launches{};
    std::atomic<std::uint32_t> serverTicks{};
    std::atomic<std::uint32_t> clientTicks{};
    std::atomic<std::uint32_t> serverStops{};
    std::atomic<std::uint32_t> clientStops{};
    std::atomic<std::uint32_t> serverRefills{};
    std::atomic<std::uint32_t> clientRefills{};
    std::atomic<std::uint32_t> forcedTicks{};
    std::atomic<std::uint32_t> contacts{};
    std::atomic<std::uint32_t> deliveredHits{};
    std::atomic<std::uint32_t> serverAnimationEnded{};
    std::atomic<std::uint32_t> clientAnimationEnded{};
    std::atomic<std::uint32_t> logLines{};
};
Counters Stats{};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

void Log(const char* message) noexcept {
    if (Context) Context->LogInfo(message);
}

auto TakeLogLine() noexcept -> bool {
    if (!Settings.debugLog) return false;
    return Stats.logLines.fetch_add(1, std::memory_order_relaxed) < MaximumLogLines;
}

// The walking mask a charge uses instead. Door openers are tested with the
// door bit while stepping (the step turns 0x3401 into 0x3C01), so the charge
// mask starts from that. Then "unit present" goes and "player footprint" comes
// in: allied monsters stop blocking, players keep blocking, and the pet bit
// 0x2000 that is already in the mask keeps hirelings and summons blocking.
auto ChargeMask(std::uint32_t walking) noexcept -> std::uint32_t {
    const std::uint32_t stepped = walking == MaskDoorOpener ? MaskMonsterPath : walking;
    return (stepped & ~CollideUnitHere) | CollidePlayer;
}

auto IsChargeSkill(void* skill) noexcept -> bool {
    if (!skill) return false;
    const auto* record = Read<const std::uint8_t*>(skill, SkillRecord);
    if (!record) return false;
    return Read<std::int16_t>(record, RecordSrvDoFunc) == static_cast<std::int16_t>(ChargeDoSlot);
}

auto UsedSkillOf(void* unit) noexcept -> void* {
    // GetUsedSkill dereferences unit+0x100 without a check, so the list
    // pointer is tested first.
    if (!GetUsedSkill || !Read<void*>(unit, UnitSkillList)) return nullptr;
    return GetUsedSkill(unit);
}

// Attack, cast, skill1..4 and sequence. Walking, running, neutral, death,
// get hit, block and knockback are never a charge in progress.
auto IsActionMode(std::uint32_t mode) noexcept -> bool {
    switch (mode) {
        case 4: case 5: case 7: case 8: case 9: case 10: case 11: case 14:
            return true;
        default:
            return false;
    }
}

auto IsPetFootprint(const void* path) noexcept -> bool {
    const auto pattern = Read<std::uint32_t>(path, PathPattern);
    return pattern == PatternSmallPet || pattern == PatternBigPet;
}

// A server monster in the middle of a charge: used skill is a srvdofunc 67
// row still flagged as charging (bit 0), on the straight path its launch set.
auto ServerChargeSkill(void* unit) noexcept -> void* {
    if (!unit || Read<std::uint32_t>(unit, UnitType) != UnitTypeMonster) return nullptr;
    const auto* path = Read<const std::uint8_t*>(unit, UnitPath);
    if (!path) return nullptr;
    if ((Read<std::uint32_t>(path, PathFlags) & PathClientFlag) != 0) return nullptr;
    if (Read<std::uint32_t>(path, PathType) != PathTypeStraight) return nullptr;
    void* skill = UsedSkillOf(unit);
    if (!IsChargeSkill(skill)) return nullptr;
    if ((Read<std::uint32_t>(skill, SkillFlags) & SkillChargingBit) == 0) return nullptr;
    return skill;
}

struct ChargeView {
    std::uint8_t* path{};
    bool          client{};
};

// Server: as ServerChargeSkill, in an action mode.
// Client: the client keeps no charge flags, so it is a monster on that
// straight path, in an action mode, whose used skill (when the client knows
// one) is a srvdofunc 67 row.
auto ClassifyCharge(void* unit, ChargeView& view) noexcept -> bool {
    if (!unit) return false;
    if (Read<std::uint32_t>(unit, UnitType) != UnitTypeMonster) return false;
    auto* path = Read<std::uint8_t*>(unit, UnitPath);
    if (!path) return false;
    if (Read<std::uint32_t>(path, PathType) != PathTypeStraight) return false;
    if (!IsActionMode(Read<std::uint32_t>(unit, UnitMode))) return false;

    const bool client = (Read<std::uint32_t>(path, PathFlags) & PathClientFlag) != 0;
    if (client) {
        if (!Settings.clientMirror) return false;
        void* skill = UsedSkillOf(unit);
        if (skill && !IsChargeSkill(skill)) return false;
    } else if (!ServerChargeSkill(unit)) {
        return false;
    }
    view.path = path;
    view.client = client;
    return true;
}

// Keeps a sequence from running out: the advance (0x3476B0) subtracts the
// sequence speed from the remaining length every frame and ends the mode at 0,
// while the row index wraps by itself. One more full pass is added whenever
// fewer than "one pass plus two frames" remain.
auto KeepSequenceAlive(void* unit) noexcept -> bool {
    if (!Read<void*>(unit, UnitSequence)) return false;
    const std::int32_t length = Read<std::int32_t>(unit, UnitSequenceLength);
    if (length <= 0) return false;
    std::int32_t speed = Read<std::int32_t>(unit, UnitSequenceSpeed);
    if (speed < 0) speed = 0;
    if (speed > length) speed = length;
    const std::int32_t floor = length + 2 * speed;
    std::int32_t left = Read<std::int32_t>(unit, UnitSequenceLeft);
    if (left >= floor) return false;
    if (left < 0) left = 0;
    while (left < floor) left += length;
    Write<std::int32_t>(unit, UnitSequenceLeft, left);
    return true;
}

void LogStop(void* unit, const ChargeView& view) noexcept {
    if (!TakeLogLine()) return;
    char message[320];
    std::snprintf(message, sizeof(message),
        "ChargePursuit: %s charge movement ended: class %u at (%u,%u) target (%u,%u) "
        "blocked by mask 0x%04X, points %d/%d, budget %u, walking mask 0x%04X",
        view.client ? "client" : "server",
        Read<std::uint32_t>(unit, UnitClassId),
        Read<std::uint16_t>(view.path, PathPosX), Read<std::uint16_t>(view.path, PathPosY),
        Read<std::uint16_t>(view.path, PathTargetX), Read<std::uint16_t>(view.path, PathTargetY),
        Read<std::uint16_t>(view.path, PathCollided),
        Read<std::int32_t>(view.path, PathPointIndex), Read<std::int32_t>(view.path, PathPointCount),
        Read<std::uint8_t>(view.path, PathDistance),
        Read<std::uint32_t>(view.path, PathMoveMask));
    Log(message);
}

void LogAnimationEnded(void* unit, bool client) noexcept {
    if (!TakeLogLine()) return;
    char message[192];
    std::snprintf(message, sizeof(message),
        "ChargePursuit: %s animation finished while still charging: class %u mode %u",
        client ? "client" : "server",
        Read<std::uint32_t>(unit, UnitClassId),
        Read<std::uint32_t>(unit, UnitMode));
    Log(message);
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// Per tick movement, server and client. Only a charging monster gets the
// charge mask, the budget refill and the sequence top-up, and the mask only
// for the duration of the call.
auto HookPathUpdate(void* unit, float step, std::uint8_t computeVectors) noexcept -> std::int64_t {
    ChargeView view{};
    if (!Settings.enabled || !ClassifyCharge(unit, view)) {
        return OriginalPathUpdate(unit, step, computeVectors);
    }

    if ((Read<std::uint32_t>(unit, UnitStateFlags) & UnitAnimationEnded) != 0) {
        (view.client ? Stats.clientAnimationEnded : Stats.serverAnimationEnded)
            .fetch_add(1, std::memory_order_relaxed);
        LogAnimationEnded(unit, view.client);
    }

    if (Settings.keepSequenceAlive && KeepSequenceAlive(unit)) {
        (view.client ? Stats.clientRefills : Stats.serverRefills).fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t walking = 0;
    std::uint32_t charging = 0;
    bool swapped = false;
    if (Settings.passThroughAllies && !IsPetFootprint(view.path)) {
        walking = Read<std::uint32_t>(view.path, PathMoveMask);
        charging = ChargeMask(walking);
        if (charging != walking) {
            Write<std::uint32_t>(view.path, PathMoveMask, charging);
            swapped = true;
        }
    }
    if (Settings.unlimitedDistance && Read<std::uint8_t>(view.path, PathDistance) < MaxDistance) {
        Write<std::uint8_t>(view.path, PathDistance, MaxDistance);
    }

    const std::int64_t moving = OriginalPathUpdate(unit, step, computeVectors);

    // Put the walking mask back unless something else rewrote it meanwhile.
    if (swapped && Read<std::uint32_t>(view.path, PathMoveMask) == charging) {
        Write<std::uint32_t>(view.path, PathMoveMask, walking);
    }

    (view.client ? Stats.clientTicks : Stats.serverTicks).fetch_add(1, std::memory_order_relaxed);
    if (!moving) {
        (view.client ? Stats.clientStops : Stats.serverStops).fetch_add(1, std::memory_order_relaxed);
        LogStop(unit, view);
    }
    return moving;
}

// srvstfunc 31. The launch builds the charge's first path itself, so the
// charge mask has to be in place for that build too.
auto HookChargeStart(void* game, void* unit, std::int32_t skillId, std::int32_t skillLevel) noexcept -> std::int64_t {
    if (!Settings.enabled || !Settings.passThroughAllies || !unit
            || Read<std::uint32_t>(unit, UnitType) != UnitTypeMonster) {
        return OriginalChargeStart(game, unit, skillId, skillLevel);
    }
    auto* path = Read<std::uint8_t*>(unit, UnitPath);
    if (!path || IsPetFootprint(path)) {
        return OriginalChargeStart(game, unit, skillId, skillLevel);
    }

    const std::uint32_t walking = Read<std::uint32_t>(path, PathMoveMask);
    const std::uint32_t charging = ChargeMask(walking);
    Write<std::uint32_t>(path, PathMoveMask, charging);

    const std::int64_t result = OriginalChargeStart(game, unit, skillId, skillLevel);

    if (Read<std::uint32_t>(path, PathMoveMask) == charging) {
        Write<std::uint32_t>(path, PathMoveMask, walking);
    }

    Stats.launches.fetch_add(1, std::memory_order_relaxed);
    if (TakeLogLine()) {
        char message[288];
        std::snprintf(message, sizeof(message),
            "ChargePursuit: launch class %u skill %d level %d result %lld, "
            "mask 0x%04X -> 0x%04X, first path %d points, path type %u, "
            "sequence %s length %d speed %d",
            Read<std::uint32_t>(unit, UnitClassId), skillId, skillLevel,
            static_cast<long long>(result), walking, charging,
            Read<std::int32_t>(path, PathPointCount), Read<std::uint32_t>(path, PathType),
            Read<void*>(unit, UnitSequence) ? "yes" : "no",
            Read<std::int32_t>(unit, UnitSequenceLength) >> 8,
            Read<std::int32_t>(unit, UnitSequenceSpeed));
        Log(message);
    }
    return result;
}

// Replaces the mode 14 handler's call to the action event getter (0x44680C).
// Returns the same byte the getter returns (unit+0x6A), except that a charge
// in progress always gets an action event, so the handler runs srvdofunc 67
// on every frame. The handler still applies its own "path still moving" and
// "skill body not already run" tests to the answer.
auto SequenceActionEvent(void* unit) noexcept -> std::uint32_t {
    const std::uint32_t actual = Read<std::uint8_t>(unit, UnitActionEvent);
    if (!Settings.enabled || !Settings.skillEveryTick) return actual;
    if (actual == 1 || actual == 2 || actual == 4) return actual;
    if (!ServerChargeSkill(unit)) return actual;
    Stats.forcedTicks.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// Runs the skill body once, then delivers the hit on the same frame when the
// call turned a charge into "contact made" (flags 0 with a target still set)
// and the engine booked no follow-up frame to deliver it. On the path ended
// branch the handler itself calls the body again this frame when the action
// event is 4, so the hit is left to that call.
auto PerformWithContact(void* game, void* unit, bool pathEndBranch) noexcept -> std::int64_t {
    void* skill = Settings.enabled && Settings.impactOnContact ? ServerChargeSkill(unit) : nullptr;
    const std::int64_t result = SkillPerform(game, unit);
    if (!skill) return result;

    if (UsedSkillOf(unit) != skill) return result;
    if (Read<std::uint32_t>(skill, SkillFlags) != 0) return result;          // still charging
    if (Read<std::int32_t>(skill, SkillTargetType) == NoTargetType) return result;
    if (Read<std::uint32_t>(unit, UnitMode) != ModeSequence) return result;
    Stats.contacts.fetch_add(1, std::memory_order_relaxed);

    const std::uint32_t booked = EarliestEvent(game, unit, EventModeChange, 0);
    const bool secondCallComing = pathEndBranch && Read<std::uint8_t>(unit, UnitActionEvent) == 4;
    const bool deliver = booked == 0 && !secondCallComing;
    if (TakeLogLine()) {
        char message[224];
        std::snprintf(message, sizeof(message),
            "ChargePursuit: contact class %u via %s branch, follow-up frame %u, %s",
            Read<std::uint32_t>(unit, UnitClassId), pathEndBranch ? "path ended" : "action",
            booked, deliver ? "hit delivered now" : "left to the engine");
        Log(message);
    }
    if (!deliver) return result;

    Stats.deliveredHits.fetch_add(1, std::memory_order_relaxed);
    SkillPerform(game, unit);
    return result;
}

auto PathEndPerform(void* game, void* unit) noexcept -> std::int64_t {
    return PerformWithContact(game, unit, true);
}

auto ActionTickPerform(void* game, void* unit) noexcept -> std::int64_t {
    return PerformWithContact(game, unit, false);
}

// ---------------------------------------------------------------------------
// Configuration parsing (small TOML subset: key = true/false)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t'
            || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'
            || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

void ParseBool(std::string_view key, std::string_view value) noexcept {
    bool parsed = false;
    if (value == "true") parsed = true;
    else if (value == "false") parsed = false;
    else {
        char message[160];
        std::snprintf(message, sizeof(message),
            "ChargePursuit: '%.*s' must be true or false; keeping the default.",
            static_cast<int>(key.size()), key.data());
        Context->LogWarn(message);
        return;
    }
    if (key == "enabled") Settings.enabled = parsed;
    else if (key == "keep_sequence_alive") Settings.keepSequenceAlive = parsed;
    else if (key == "skill_every_tick") Settings.skillEveryTick = parsed;
    else if (key == "impact_on_contact") Settings.impactOnContact = parsed;
    else if (key == "pass_through_allies") Settings.passThroughAllies = parsed;
    else if (key == "unlimited_distance") Settings.unlimitedDistance = parsed;
    else if (key == "client_mirror") Settings.clientMirror = parsed;
    else if (key == "debug_log") Settings.debugLog = parsed;
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t position = 0;
    while (position < text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(position, end - position);
        position = end + 1;
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        line = Trim(line);
        if (line.empty() || line.front() == '[') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ParseBool(Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
    }
}

auto ReadConfiguration() noexcept -> void {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("ChargePursuit: config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("ChargePursuit: config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

template <std::size_t Size>
auto Verify(std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected, const char* what) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected.data(), static_cast<std::uint32_t>(Size))) return true;
    char message[192];
    std::snprintf(message, sizeof(message),
        "ChargePursuit: unexpected bytes at 0x%llX (%s); refusing to install it.",
        static_cast<unsigned long long>(rva), what);
    Context->LogError(message);
    return false;
}

auto VerifyTableSlot(std::uintptr_t tableRva, std::uint32_t slot, std::uintptr_t functionRva, const char* what) noexcept -> bool {
    const auto entry = Read<std::uintptr_t>(Base + tableRva, slot * sizeof(std::uintptr_t));
    if (entry == reinterpret_cast<std::uintptr_t>(Base) + functionRva) return true;
    char message[192];
    std::snprintf(message, sizeof(message),
        "ChargePursuit: %s slot %u does not point at 0x%llX; refusing to install.",
        what, slot, static_cast<unsigned long long>(functionRva));
    Context->LogError(message);
    return false;
}

// ---------------------------------------------------------------------------
// Relay page for the three call sites in the mode 14 handler
// ---------------------------------------------------------------------------

auto CanReachRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - 5;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanReachRel32(hint, candidate)) break;
        if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

// mov rax, imm64 / jmp rax. Entered by the retargeted call, so the return
// address the handler pushed is still on the stack and the C++ target returns
// straight to the handler. rax is the return value register anyway.
void BuildJumpStub(std::uint8_t* code, const void* target) noexcept {
    const auto address = reinterpret_cast<std::uint64_t>(target);
    code[0] = 0x48;
    code[1] = 0xB8;
    std::memcpy(code + 2, &address, sizeof(address));
    code[10] = 0xFF;
    code[11] = 0xE0;
}

template <std::size_t Size>
auto RetargetCall(std::uintptr_t callRva, const std::array<std::uint8_t, Size>& expected,
        std::uintptr_t stubAddress, const char* what) noexcept -> bool {
    const auto image = reinterpret_cast<std::uintptr_t>(Base);
    const std::int64_t delta = static_cast<std::int64_t>(stubAddress)
        - static_cast<std::int64_t>(image + callRva + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        Context->LogError("ChargePursuit: relay page out of rel32 reach.");
        return false;
    }
    std::uint8_t call[5] = {0xE8, 0, 0, 0, 0};
    const auto rel = static_cast<std::int32_t>(delta);
    std::memcpy(call + 1, &rel, sizeof(rel));
    if (Context->PatchBytes(callRva, expected.data(), static_cast<std::uint32_t>(Size), call, sizeof(call))) {
        return true;
    }
    char message[192];
    std::snprintf(message, sizeof(message),
        "ChargePursuit: the %s call at 0x%llX could not be retargeted.",
        what, static_cast<unsigned long long>(callRva));
    Context->LogError(message);
    return false;
}

auto InstallSequenceRelays(bool everyTick, bool impact) noexcept -> void {
    if (!everyTick && !impact) return;

    if (!Verify(ActionEventWindowRva, ActionEventWindowExpected, "mode 14 action event read")
            || !Verify(PathEndWindowRva, PathEndWindowExpected, "mode 14 path ended call")
            || !Verify(ActionTickWindowRva, ActionTickWindowExpected, "mode 14 action tick call")) {
        return;
    }

    const auto image = reinterpret_cast<std::uintptr_t>(Base);
    RelayPage = AllocateNear(image + SequenceUpdateRva, RelayBytes);
    if (!RelayPage) {
        Context->LogError("ChargePursuit: no relay page within rel32 reach of the mode 14 handler.");
        return;
    }
    auto* relay = static_cast<std::uint8_t*>(RelayPage);
    const auto relayBase = reinterpret_cast<std::uintptr_t>(relay);
    const std::uintptr_t actionEventStub = relayBase;
    const std::uintptr_t pathEndStub = relayBase + 16;
    const std::uintptr_t actionTickStub = relayBase + 32;
    BuildJumpStub(relay, reinterpret_cast<const void*>(&SequenceActionEvent));
    BuildJumpStub(relay + 16, reinterpret_cast<const void*>(&PathEndPerform));
    BuildJumpStub(relay + 32, reinterpret_cast<const void*>(&ActionTickPerform));

    DWORD previous = 0;
    if (!VirtualProtect(relay, RelayBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("ChargePursuit: relay page protection could not be finalized.");
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, RelayBytes);

    if (everyTick) {
        EveryTickInstalled = RetargetCall(ActionEventCallRva, ActionEventCallExpected,
            actionEventStub, "action event");
    }
    if (impact) {
        const bool pathEnd = RetargetCall(PathEndCallRva, PathEndCallExpected, pathEndStub, "path ended perform");
        const bool actionTick = RetargetCall(ActionTickCallRva, ActionTickCallExpected, actionTickStub, "action tick perform");
        ImpactInstalled = pathEnd && actionTick;
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[448];
    std::snprintf(message, sizeof(message),
        "Charge Pursuit: %s | launch hook %s | movement hook %s | every tick %s | impact %s | "
        "sequence alive %s | pass through %s | unlimited distance %s | client mirror %s",
        Settings.enabled ? "on" : "off",
        StartHookInstalled ? "armed" : "off",
        PathHookInstalled ? "armed" : "off",
        EveryTickInstalled ? "armed" : "off",
        ImpactInstalled ? "armed" : "off",
        Settings.keepSequenceAlive ? "on" : "off",
        Settings.passThroughAllies ? "on" : "off",
        Settings.unlimitedDistance ? "on" : "off",
        Settings.clientMirror ? "on" : "off");
    command->plugin->WriteConsoleMessage(message);
    std::snprintf(message, sizeof(message),
        "Charge Pursuit: launches %u | charging ticks server %u client %u | sequence top-ups server %u client %u | "
        "forced action ticks %u",
        Stats.launches.load(), Stats.serverTicks.load(), Stats.clientTicks.load(),
        Stats.serverRefills.load(), Stats.clientRefills.load(), Stats.forcedTicks.load());
    command->plugin->WriteConsoleMessage(message);
    std::snprintf(message, sizeof(message),
        "Charge Pursuit: contacts %u | hits delivered by the plugin %u | movement ended server %u client %u | "
        "animation finished mid-charge server %u client %u",
        Stats.contacts.load(), Stats.deliveredHits.load(),
        Stats.serverStops.load(), Stats.clientStops.load(),
        Stats.serverAnimationEnded.load(), Stats.clientAnimationEnded.load());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "celestialrayone.charge-pursuit",
    .name = "Charge Pursuit",
    .version = "2.0.0",
    .author = "CelestialRayOne",
    .description =
        "Monster Charge keeps going until it hits an enemy: sequence charges "
        "loop, run every frame and land their hit; packmates no longer block "
        "and the 20 step budget no longer runs out.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);

    ReadConfiguration();
    if (!Settings.enabled) {
        Context->LogInfo("ChargePursuit: disabled by configuration.");
        return true;
    }

    // Everything shared is verified before the first byte is written.
    if (!VerifyTableSlot(SrvStTableRva, ChargeStartSlot, ChargeStartRva, "srvstfunc table")
            || !VerifyTableSlot(SrvDoTableRva, ChargeDoSlot, ChargeDoRva, "srvdofunc table")
            || !Verify(GetUsedSkillRva, GetUsedSkillExpected, "UNITS_GetUsedSkill")
            || !Verify(GetSkillFlagsRva, GetSkillFlagsExpected, "SKILLS_GetFlags")
            || !Verify(ActionEventGetterRva, ActionEventGetterExpected, "action event getter")
            || !Verify(ActionEventReadRva, ActionEventReadExpected, "action event offset")
            || !Verify(SkillPerformRva, SkillPerformExpected, "skill perform")
            || !Verify(EarliestEventRva, EarliestEventExpected, "event lookup")) {
        return false;
    }
    GetUsedSkill = reinterpret_cast<GetUsedSkillFn>(Base + GetUsedSkillRva);
    SkillPerform = reinterpret_cast<SkillPerformFn>(Base + SkillPerformRva);
    EarliestEvent = reinterpret_cast<EarliestEventFn>(Base + EarliestEventRva);

    const bool wantsMovement = Settings.passThroughAllies || Settings.unlimitedDistance
        || Settings.keepSequenceAlive;

    if (Settings.passThroughAllies) {
        StartHookInstalled = context->InstallInlineHook(ChargeStartRva, ChargeStartExpected.data(),
            static_cast<std::uint32_t>(ChargeStartExpected.size()), &HookChargeStart, &OriginalChargeStart);
        if (!StartHookInstalled) {
            Context->LogError("ChargePursuit: the launch hook at 0x564DC0 could not be installed.");
        }
    }

    if (wantsMovement) {
        PathHookInstalled = context->InstallInlineHook(PathUpdateRva, PathUpdateExpected.data(),
            static_cast<std::uint32_t>(PathUpdateExpected.size()), &HookPathUpdate, &OriginalPathUpdate);
        if (!PathHookInstalled) {
            Context->LogError("ChargePursuit: the movement hook at 0x3812D0 could not be installed.");
        }
    }

    InstallSequenceRelays(Settings.skillEveryTick, Settings.impactOnContact);

    // From here on the plugin never returns false: a hook may already be live,
    // and returning false would unload the DLL under it.
    if (!context->RegisterConsoleCommand("charge-pursuit", StatusCommand,
            "Show Charge Pursuit state and counters.")) {
        Context->LogWarn("ChargePursuit: console command was not registered.");
    }

    char message[320];
    std::snprintf(message, sizeof(message),
        "ChargePursuit 2.0.0: loaded. launch hook %s, movement hook %s, every tick %s, impact %s, "
        "sequence alive %s, pass through %s, unlimited distance %s, client mirror %s, debug log %s.",
        StartHookInstalled ? "armed" : "off",
        PathHookInstalled ? "armed" : "off",
        EveryTickInstalled ? "armed" : "off",
        ImpactInstalled ? "armed" : "off",
        Settings.keepSequenceAlive ? "on" : "off",
        Settings.passThroughAllies ? "on" : "off",
        Settings.unlimitedDistance ? "on" : "off",
        Settings.clientMirror ? "on" : "off",
        Settings.debugLog ? "on" : "off");
    Context->LogInfo(message);
    return true;
}
