// celestialrayone.cast-on-cast
//
// Port of ESR's D2R 2.4 cast-on-cast patch set to D2R 3.2. Every RVA is an
// offset into the D2RLoader process image and was read from that image. Each
// site, and every layout fact the handlers rely on, is fingerprinted before a
// single byte is written; on any mismatch the plugin installs nothing.
//
// 2.4 entry                                       3.2 equivalent
// CastOnCast_doactive_hook 298A12 + cave 2C0810   relay at 43AF1D, skill do-handler sub_14043ACB0
// SrvmissileProc_skip_a6a7_block 36538D           byte patch at 589C6F, item-effect caster sub_140589930
// CltCastExec_skip_itemproc_coordblock 1732AB     byte patch at 216DBB, client cast executor sub_140216D20
// CltProcCast_reassert_target 1792FC + 2C0A00     relay at 2312E3, client item-effect cast sub_1402310B0
// CltMissile_tickrewind 1716E7 + 2C0878           relay at 213F28, client missile builder sub_140213D80
//
// The 2.4 caves lived in .text. Here the relay code lives in one page the
// plugin allocates within rel32 reach of the image, and the game sites jump
// into it. Relays never move rsp, so each relay reuses the unwind data of the
// game function it was entered from and stack walks pass through it cleanly.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr const char* PluginIdText = "celestialrayone.cast-on-cast";

constexpr const char DefaultToml[] = R"TOML(# celestialrayone.cast-on-cast
#
# Cast on Cast: fires the "doactive" event every time a character finishes
# casting a skill, so an item stat wired to doactive can cast another skill
# ("X% chance to cast level Y skill Z when casting a skill").
#
# Data side, itemstatcost.txt row of the stat:
#   itemevent1     = doactive
#   itemeventfunc1 = 20
# The property then behaves like att-skill / hit-skill: the item chooses the
# skill to cast, the chance and the level.
#
# A cast triggers doactive only when all of these hold:
#   - the caster is in the cast animation (mode 10)
#   - the skill is not an aura
#   - it is a real cast that pays its cost, so item procs and aura pulses
#     never trigger it and a proc cannot chain into itself
# The proc aims at the unit the cast was aimed at, or at the ground point
# when there is no unit.
#
# Port of the ESR 2.4 patch set to D2R 3.2 (D2RLoader image). Every part
# checks the exact game code it touches before changing anything, and the
# plugin installs nothing on a build that does not match. The console
# command "castoncast" shows what is installed and how often each part ran.

[celestialrayone.cast-on-cast]

# Master switch for everything below.
enabled = true

# Server: fire doactive after a real cast. This is the feature itself.
cast_trigger = true

# Server: an item proc builds its missile exactly like a normal cast, so it
# leaves the caster and flies at the target instead of the wrong way.
# Applies to every item proc (on hit, on attack, when struck, on kill,
# on death, level up and cast on cast), not only to cast on cast.
server_proc_aim = true

# Client: the proc keeps the target the server sent instead of the caster's
# facing, and its client missile is built like a normal cast. Visual only.
client_proc_aim = true

# Client: missiles from the standard client missile builder are pulled back
# by one movement step when created, so a proc missile is first drawn at the
# caster instead of one step ahead of it. Visual only.
client_missile_rewind = true
)TOML";

// ---------------------------------------------------------------------------
// Layout facts (3.2), each proven from this image
// ---------------------------------------------------------------------------
// Unit+0x00 type, +0x0C animation mode, +0x38 path: the engine's own inline
// code reads them this way (sub_140213D80, sub_140216D20 and sub_14043ACB0 all
// test *unit == 2 || *unit - 4 < 2 before reading the path at +0x38).
constexpr std::size_t UnitTypeOffset = 0x00;
constexpr std::size_t UnitModeOffset = 0x0C;
constexpr std::size_t UnitPathOffset = 0x38;
constexpr std::uint32_t PlayerUnitType = 0;
constexpr std::uint32_t MonsterUnitType = 1;
constexpr std::uint32_t MissileUnitType = 3;
constexpr std::uint32_t CastMode = 10;

// DynamicPath: precision X/Y dwords at +0x00/+0x04 (PATH_GetX/GetY return
// their high words), first point words at +0x10/+0x12 and target unit at +0x70
// (the set-target-point and get-target-unit accessors), per-tick step at
// +0x96/+0x9A (sub_140380FD0 stores (direction * velocity) >> 8 there, with
// velocity at +0xA0). Identical to 2.4.
constexpr std::size_t PathPrecisionXOffset = 0x00;
constexpr std::size_t PathPrecisionYOffset = 0x04;
constexpr std::size_t PathFirstPointXOffset = 0x10;
constexpr std::size_t PathFirstPointYOffset = 0x12;
constexpr std::size_t PathTargetUnitOffset = 0x70;
constexpr std::size_t PathStepXOffset = 0x96;
constexpr std::size_t PathStepYOffset = 0x9A;

// skills.txt row flags start at +0x24 in the same bit order as 2.4: the
// do-handler tests bit 0 (decquant) and bit 1 (lob) of that byte, so the aura
// flag is still bit 5.
constexpr std::size_t SkillRowFlagsOffset = 0x24;
constexpr std::uint8_t SkillRowAuraBit = 0x20;

constexpr std::int32_t DoActiveEvent = 4;

// ---------------------------------------------------------------------------
// Witnesses and patch sites
// ---------------------------------------------------------------------------
struct Witness {
    std::uint64_t rva;
    const std::uint8_t* bytes;
    std::uint32_t size;
    const char* what;
};

// Skill do-handler sub_14043ACB0(game, unit, skillId, level, consumeResources,
// itemCast, itemEffect). 7 pushes + sub rsp,80h put argument 5 at [rsp+0xE0].
constexpr std::uint64_t DoHandlerRva = 0x43ACB0;
constexpr auto DoHandlerPrologue = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x10,0x44,0x89,0x4C,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x55,
    0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,0x00});
// if (consumeResources) at 43B0EC reads the same slot the relay passes on.
constexpr std::uint64_t DoHandlerConsumeTestRva = 0x43B0EC;
constexpr auto DoHandlerConsumeTest = std::to_array<std::uint8_t>({
    0x83,0xBC,0x24,0xE0,0x00,0x00,0x00,0x00,0x0F,0x84,0xAA,0x01,0x00,0x00});
// srvdofunc call: rcx = rdi game, rdx = rbx unit, result into r12d, then the
// two instructions the relay replaces and the resume instruction at 43AF25.
constexpr std::uint64_t DoHandlerCallRva = 0x43AF06;
constexpr auto DoHandlerCall = std::to_array<std::uint8_t>({
    0x44,0x8B,0x8C,0x24,0xD8,0x00,0x00,0x00,0x45,0x8B,0xC5,0x48,0x8B,0xD3,0x48,0x8B,
    0xCF,0x41,0xFF,0xD2,0x44,0x8B,0xE0,0x44,0x0F,0xB6,0x76,0x24,0x40,0x32,0xED,0x44,
    0x23,0x35,0xA4,0xE7,0x95,0x01});
constexpr std::uint64_t ServerHookRva = 0x43AF1D;
constexpr std::uint64_t ServerResumeRva = 0x43AF25;
constexpr auto ServerHookExpected = std::to_array<std::uint8_t>({
    0x44,0x0F,0xB6,0x76,0x24,0x40,0x32,0xED});

// Event fire wrapper sub_14044D570(game, eventId, unit, otherUnit, damage):
// builds the 3.x event context and walks unit+0xE0 through sub_1405881E0.
constexpr std::uint64_t DispatchUnitEventRva = 0x44D570;
constexpr auto DispatchUnitEventPrologue = std::to_array<std::uint8_t>({
    0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05,0x46,0xDD,
    0x57,0x02,0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x58});

// Item-effect caster sub_140589930 (2.4 PROC_FireSkill): the 12th argument is
// loaded from [rsp+0xF8] and stored as argument 7 (itemEffect) of the call to
// the do-handler at 589C93.
constexpr std::uint64_t ItemEffectAimRva = 0x589C6F;
constexpr auto ItemEffectAimWitness = std::to_array<std::uint8_t>({
    0x8B,0x84,0x24,0xF8,0x00,0x00,0x00,0x44,0x8B,0xCE,0x89,0x44,0x24,0x30,0x44,0x8B,
    0xC5,0xC7,0x44,0x24,0x28,0x01,0x00,0x00,0x00,0x48,0x8B,0xD3,0x49,0x8B,0xCF,0x44,
    0x89,0x6C,0x24,0x20,0xE8,0x18,0x10,0xEB,0xFF});
constexpr auto ItemEffectAimExpected = std::to_array<std::uint8_t>({
    0x8B,0x84,0x24,0xF8,0x00,0x00,0x00});
constexpr auto ItemEffectAimBytes = std::to_array<std::uint8_t>({
    0x31,0xC0,0x90,0x90,0x90,0x90,0x90});

// Client cast executor sub_140216D20(unit, skillId, level, itemProc, flag):
// if (itemProc) offsets the client missile coordinates. je -> jmp, same target.
constexpr std::uint64_t ClientExecutorWitnessRva = 0x216DB3;
constexpr auto ClientExecutorWitness = std::to_array<std::uint8_t>({
    0x44,0x39,0xAC,0x24,0xB8,0x00,0x00,0x00,0x0F,0x84,0xA7,0x00,0x00,0x00});
constexpr std::uint64_t ClientProcBlockRva = 0x216DBB;
constexpr auto ClientProcBlockExpected = std::to_array<std::uint8_t>({
    0x0F,0x84,0xA7,0x00,0x00,0x00});
constexpr auto ClientProcBlockBytes = std::to_array<std::uint8_t>({
    0xE9,0xA8,0x00,0x00,0x00,0x90});

// Client item-effect cast sub_1402310B0(unit, skillId, level, targetUnit,
// targetX, targetY, itemProc). 7 pushes + sub rsp,70h put the packet target
// at [rsp+0xD0]/[rsp+0xD8] and itemProc at [rsp+0xE0].
constexpr std::uint64_t ClientProcCastRva = 0x2310B0;
constexpr auto ClientProcCastPrologue = std::to_array<std::uint8_t>({
    0x44,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,
    0x83,0xEC,0x70});
constexpr std::uint64_t ClientProcCastPositionRva = 0x2311AC;
constexpr auto ClientProcCastPosition = std::to_array<std::uint8_t>({
    0x44,0x8B,0x84,0x24,0xD8,0x00,0x00,0x00,0x8B,0x94,0x24,0xD0,0x00,0x00,0x00,0xE8,
    0x10,0xE1,0x11,0x00});
// itemProc load right before the executor call: rbx = caster, rcx/rdx/r8 and
// the fifth argument are all set after this instruction.
constexpr std::uint64_t ClientProcCastCallRva = 0x2312E3;
constexpr auto ClientProcCastCall = std::to_array<std::uint8_t>({
    0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00,0x45,0x8B,0xC7,0x41,0x8B,0xD6,0x40,
    0x88,0x74,0x24,0x20,0x48,0x8B,0xCB,0xE8,0x21,0x5A,0xFE,0xFF});
constexpr std::uint64_t ClientTargetHookRva = 0x2312E3;
constexpr std::uint64_t ClientTargetResumeRva = 0x2312EC;
constexpr auto ClientTargetHookExpected = std::to_array<std::uint8_t>({
    0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00});

// Client missile builder sub_140213D80 (flags 0x21, the 2.4 "fl=21" builder):
// rcx = command, edx = 0, call the client missile creator sub_1401B7760.
constexpr std::uint64_t ClientMissileBuildRva = 0x213F13;
constexpr auto ClientMissileBuild = std::to_array<std::uint8_t>({
    0x83,0x7C,0x24,0x6C,0x00,0x74,0x15,0x83,0x7C,0x24,0x70,0x00,0x74,0x0E,0x33,0xD2,
    0x48,0x8D,0x4C,0x24,0x40,0xE8,0x33,0x38,0xFA,0xFF,0xEB,0x02,0x33,0xC0});
constexpr std::uint64_t ClientRewindHookRva = 0x213F28;
constexpr std::uint64_t ClientRewindResumeRva = 0x213F2D;
constexpr std::uint64_t ClientMissileCreateRva = 0x1B7760;
constexpr auto ClientRewindHookExpected = std::to_array<std::uint8_t>({
    0xE8,0x33,0x38,0xFA,0xFF});

// Path accessors proving the layout above.
constexpr auto PathGetXBytes = std::to_array<std::uint8_t>({0x0F,0xB7,0x41,0x02,0xC3});
constexpr auto PathGetYBytes = std::to_array<std::uint8_t>({0x0F,0xB7,0x41,0x06,0xC3});
constexpr auto PathGetTargetUnitBytes = std::to_array<std::uint8_t>({
    0x48,0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0x48,0x8B,0x41,0x70,0xC3});
constexpr auto PathSetTargetPointBytes = std::to_array<std::uint8_t>({
    0x48,0x85,0xC9,0x74,0x11,0x66,0x89,0x51,0x10,0x66,0x44,0x89,0x41,0x12,0x48,0xC7,
    0x41,0x70,0x00,0x00,0x00,0x00,0xC3});

constexpr std::array<Witness, 16> Witnesses{{
    {DoHandlerRva, DoHandlerPrologue.data(), static_cast<std::uint32_t>(DoHandlerPrologue.size()), "skill do-handler prologue"},
    {DoHandlerConsumeTestRva, DoHandlerConsumeTest.data(), static_cast<std::uint32_t>(DoHandlerConsumeTest.size()), "skill do-handler consume-resources test"},
    {DoHandlerCallRva, DoHandlerCall.data(), static_cast<std::uint32_t>(DoHandlerCall.size()), "skill do-handler srvdofunc call"},
    {DispatchUnitEventRva, DispatchUnitEventPrologue.data(), static_cast<std::uint32_t>(DispatchUnitEventPrologue.size()), "unit event wrapper prologue"},
    {ItemEffectAimRva, ItemEffectAimWitness.data(), static_cast<std::uint32_t>(ItemEffectAimWitness.size()), "item-effect caster do-handler call"},
    {ClientExecutorWitnessRva, ClientExecutorWitness.data(), static_cast<std::uint32_t>(ClientExecutorWitness.size()), "client cast executor item-proc block"},
    {ClientProcCastRva, ClientProcCastPrologue.data(), static_cast<std::uint32_t>(ClientProcCastPrologue.size()), "client item-effect cast prologue"},
    {ClientProcCastPositionRva, ClientProcCastPosition.data(), static_cast<std::uint32_t>(ClientProcCastPosition.size()), "client item-effect cast target point"},
    {ClientProcCastCallRva, ClientProcCastCall.data(), static_cast<std::uint32_t>(ClientProcCastCall.size()), "client item-effect cast executor call"},
    {ClientMissileBuildRva, ClientMissileBuild.data(), static_cast<std::uint32_t>(ClientMissileBuild.size()), "client missile builder creator call"},
    {0x341A20, PathGetXBytes.data(), static_cast<std::uint32_t>(PathGetXBytes.size()), "PATH_GetX"},
    {0x341A30, PathGetYBytes.data(), static_cast<std::uint32_t>(PathGetYBytes.size()), "PATH_GetY"},
    {0x341A40, PathGetTargetUnitBytes.data(), static_cast<std::uint32_t>(PathGetTargetUnitBytes.size()), "PATH_GetTargetUnit"},
    {0x342A50, PathSetTargetPointBytes.data(), static_cast<std::uint32_t>(PathSetTargetPointBytes.size()), "PATH_SetTargetPoint"},
    {ServerHookRva, ServerHookExpected.data(), static_cast<std::uint32_t>(ServerHookExpected.size()), "server hook site"},
    {ClientRewindHookRva, ClientRewindHookExpected.data(), static_cast<std::uint32_t>(ClientRewindHookExpected.size()), "client rewind hook site"},
}};

// ---------------------------------------------------------------------------
// Relay stubs (keystone-assembled, capstone and unicorn verified)
// ---------------------------------------------------------------------------
// Server, entered at 43AF1D with rdi game, rbx caster, rsi skills.txt row:
//   mov rcx,rdi / mov rdx,rbx / mov r8,rsi / mov r9d,[rsp+E0h]
//   call [OnServerSkillDo] / movzx r14d,byte [rsi+24h] / xor bpl,bpl / jmp [43AF25]
// Client target, entered at 2312E3 with rbx caster:
//   mov rcx,rbx / mov edx,[rsp+D0h] / mov r8d,[rsp+D8h] / call [OnClientProcCastTarget]
//   movzx r9d,word [rsp+E0h] / jmp [2312EC]
// Client rewind, entered at 213F28 with rcx command, edx 0:
//   call [sub_1401B7760] / mov [rsp+20h],rax / mov rcx,rax / call [OnClientMissileCreated]
//   mov rax,[rsp+20h] / jmp [213F2D]
// None of them moves rsp. The callees use the host frame's home space, which is
// scratch at all three sites; [rsp+20h] in the rewind host is its outgoing
// argument slot, unused after the creator call.
constexpr std::array<std::uint8_t, 56> ServerRelayCode{0x48,0x89,0xF9,0x48,0x89,0xDA,0x49,0x89,0xF0,0x44,0x8B,0x8C,0x24,0xE0,0x00,0x00,0x00,0xFF,0x15,0x11,0x00,0x00,0x00,0x44,0x0F,0xB6,0x76,0x24,0x40,0x32,0xED,0xFF,0x25,0x0B,0x00,0x00,0x00,0xCC,0xCC,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
constexpr std::size_t ServerRelayCode_HandlerSlot = 0x28;
constexpr std::size_t ServerRelayCode_ResumeSlot = 0x30;
constexpr std::array<std::uint8_t, 56> ClientTargetRelayCode{0x48,0x89,0xD9,0x8B,0x94,0x24,0xD0,0x00,0x00,0x00,0x44,0x8B,0x84,0x24,0xD8,0x00,0x00,0x00,0xFF,0x15,0x10,0x00,0x00,0x00,0x44,0x0F,0xB7,0x8C,0x24,0xE0,0x00,0x00,0x00,0xFF,0x25,0x09,0x00,0x00,0x00,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
constexpr std::size_t ClientTargetRelayCode_HandlerSlot = 0x28;
constexpr std::size_t ClientTargetRelayCode_ResumeSlot = 0x30;
constexpr std::array<std::uint8_t, 56> ClientRewindRelayCode{0xFF,0x15,0x1A,0x00,0x00,0x00,0x48,0x89,0x44,0x24,0x20,0x48,0x89,0xC1,0xFF,0x15,0x14,0x00,0x00,0x00,0x48,0x8B,0x44,0x24,0x20,0xFF,0x25,0x11,0x00,0x00,0x00,0xCC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
constexpr std::size_t ClientRewindRelayCode_CreatorSlot = 0x20;
constexpr std::size_t ClientRewindRelayCode_HandlerSlot = 0x28;
constexpr std::size_t ClientRewindRelayCode_ResumeSlot = 0x30;

constexpr std::size_t RelayPageSize = 0x1000;
constexpr std::size_t RelayBlockSize = 0x200;
constexpr std::size_t RelayCodeOffset = 0x100;
constexpr std::size_t ServerBlock = 0;
constexpr std::size_t ClientTargetBlock = 1;
constexpr std::size_t ClientRewindBlock = 2;
constexpr std::uint64_t LowestHookRva = 0x213F28;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class PluginState : std::uint8_t { NotLoaded, DisabledByConfig, UnsupportedBuild, Active, PartiallyActive };
enum class PartState : std::uint8_t { Disabled, Installed, Failed };

struct Config {
    bool enabled = true;
    bool castTrigger = true;
    bool serverProcAim = true;
    bool clientProcAim = true;
    bool clientMissileRewind = true;
};

using DispatchUnitEventFn = std::int32_t(*)(void* game, std::int32_t eventId, void* unit, void* otherUnit, void* damage) noexcept;

const D2RL::PluginContext* g_ctx = nullptr;
std::uintptr_t g_base = 0;
Config g_config{};
PluginState g_state = PluginState::NotLoaded;
PartState g_castTrigger = PartState::Disabled;
PartState g_serverProcAim = PartState::Disabled;
PartState g_clientProcAim = PartState::Disabled;
PartState g_clientMissileRewind = PartState::Disabled;
DispatchUnitEventFn g_dispatchUnitEvent = nullptr;
std::uint8_t* g_relayPage = nullptr;
std::array<RUNTIME_FUNCTION, 3> g_relayUnwind{};
bool g_unwindRegistered = false;

std::atomic<std::uint64_t> g_triggersFired{0};
std::atomic<std::uint64_t> g_targetsReasserted{0};
std::atomic<std::uint64_t> g_missilesRewound{0};

void Log(int level, const char* format, ...) noexcept {
    if (!g_ctx) return;
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (level == 0) g_ctx->LogInfo(buffer);
    else if (level == 1) g_ctx->LogWarn(buffer);
    else g_ctx->LogError(buffer);
}

template <typename T>
T Read(const void* base, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

bool HasDynamicPath(const void* unit) noexcept {
    const auto type = Read<std::uint32_t>(unit, UnitTypeOffset);
    return type == PlayerUnitType || type == MonsterUnitType || type == MissileUnitType;
}

// ---------------------------------------------------------------------------
// Handlers called from the relays
// ---------------------------------------------------------------------------

// 2.4 cave 2C0810: fire doactive for a real cast. Gate is unchanged from 2.4:
// cast mode, not an aura, and a cast that pays its cost (item procs call the
// do-handler with consumeResources 0, which is what stops recursion). The other
// unit is the caster's path target, so EventFunc20 casts on that unit, or at
// the path's first point when there is none.
void OnServerSkillDo(void* game, void* caster, const std::uint8_t* skillRow, std::int32_t consumeResources) noexcept {
    if (!game || !caster || !skillRow || !g_dispatchUnitEvent) return;
    if (consumeResources == 0) return;
    if (Read<std::uint32_t>(caster, UnitModeOffset) != CastMode) return;
    if ((skillRow[SkillRowFlagsOffset] & SkillRowAuraBit) != 0) return;
    void* target = nullptr;
    if (HasDynamicPath(caster)) {
        if (void* path = Read<void*>(caster, UnitPathOffset)) {
            target = Read<void*>(path, PathTargetUnitOffset);
        }
    }
    g_triggersFired.fetch_add(1, std::memory_order_relaxed);
    g_dispatchUnitEvent(game, DoActiveEvent, caster, target, nullptr);
}

// 2.4 cave 2C0A00: the client start function overwrites the path's first point
// with the caster's facing before the executor runs. Write the packet's target
// point back right before the executor call.
void OnClientProcCastTarget(void* caster, std::uint32_t targetX, std::uint32_t targetY) noexcept {
    if (!caster || !HasDynamicPath(caster)) return;
    void* path = Read<void*>(caster, UnitPathOffset);
    if (!path) return;
    Write<std::uint16_t>(path, PathFirstPointXOffset, static_cast<std::uint16_t>(targetX));
    Write<std::uint16_t>(path, PathFirstPointYOffset, static_cast<std::uint16_t>(targetY));
    g_targetsReasserted.fetch_add(1, std::memory_order_relaxed);
}

// 2.4 cave 2C0878: step the new missile back by exactly one movement tick. The
// step vector was stored by the path setup during creation, so the rewind
// scales with the missile's own speed and direction.
void OnClientMissileCreated(void* missile) noexcept {
    if (!missile || Read<std::uint32_t>(missile, UnitTypeOffset) != MissileUnitType) return;
    void* path = Read<void*>(missile, UnitPathOffset);
    if (!path) return;
    const auto x = Read<std::uint32_t>(path, PathPrecisionXOffset);
    const auto y = Read<std::uint32_t>(path, PathPrecisionYOffset);
    const auto stepX = Read<std::uint32_t>(path, PathStepXOffset);
    const auto stepY = Read<std::uint32_t>(path, PathStepYOffset);
    Write<std::uint32_t>(path, PathPrecisionXOffset, x - stepX);
    Write<std::uint32_t>(path, PathPrecisionYOffset, y - stepY);
    g_missilesRewound.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
std::string_view Trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t position = 0;
    std::size_t lineNumber = 0;
    while (position < text.size()) {
        auto end = text.find('\n', position);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(position, end - position);
        position = end + 1;
        ++lineNumber;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty() || line.front() == '[') continue;
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            Log(1, "cast-on-cast: config line %zu ignored, expected key = value.", lineNumber);
            continue;
        }
        const auto key = Trim(line.substr(0, equals));
        const auto value = Trim(line.substr(equals + 1));
        bool flag = false;
        if (value == "true") flag = true;
        else if (value != "false") {
            Log(1, "cast-on-cast: config line %zu ignored, value must be true or false.", lineNumber);
            continue;
        }
        if (key == "enabled") g_config.enabled = flag;
        else if (key == "cast_trigger") g_config.castTrigger = flag;
        else if (key == "server_proc_aim") g_config.serverProcAim = flag;
        else if (key == "client_proc_aim") g_config.clientProcAim = flag;
        else if (key == "client_missile_rewind") g_config.clientMissileRewind = flag;
        else Log(1, "cast-on-cast: config line %zu has an unknown key.", lineNumber);
    }
}

void LoadConfig() noexcept {
    g_config = Config{};
    if (!g_ctx->EnsureConfig(DefaultToml)) {
        Log(1, "cast-on-cast: could not create the default config, using built-in defaults.");
    }
    std::string buffer(16384, '\0');
    std::uint32_t required = 0;
    if (!g_ctx->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required > buffer.size()) {
            buffer.assign(required, '\0');
            if (!g_ctx->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
                Log(1, "cast-on-cast: could not read the config, using built-in defaults.");
                return;
            }
        } else {
            Log(1, "cast-on-cast: could not read the config, using built-in defaults.");
            return;
        }
    }
    ParseConfig(std::string_view(buffer.c_str()));
}

// ---------------------------------------------------------------------------
// Relay page
// ---------------------------------------------------------------------------
std::uint8_t* AllocateRelayPage() noexcept {
    const auto* image = reinterpret_cast<const std::uint8_t*>(g_base);
    const auto ntHeaders = Read<std::int32_t>(image, 0x3C);
    const auto sizeOfImage = Read<std::uint32_t>(image, static_cast<std::size_t>(ntHeaders) + 0x50);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) { return (value + granularity - 1) & ~(granularity - 1); };
    const std::uintptr_t limit = g_base + LowestHookRva + 0x7FF00000;
    std::uintptr_t address = alignUp(g_base + sizeOfImage);
    while (address + RelayPageSize < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageSize <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

void PlaceRelay(std::size_t block, const std::uint8_t* code, std::size_t size) noexcept {
    std::memcpy(g_relayPage + block * RelayBlockSize + RelayCodeOffset, code, size);
}

void SetRelaySlot(std::size_t block, std::size_t slot, std::uint64_t value) noexcept {
    std::memcpy(g_relayPage + block * RelayBlockSize + RelayCodeOffset + slot, &value, sizeof(value));
}

std::uint64_t RelayRva(std::size_t block) noexcept {
    return reinterpret_cast<std::uintptr_t>(g_relayPage) + block * RelayBlockSize + RelayCodeOffset - g_base;
}

// Each relay block is described with the unwind data of the game function the
// relay is entered from. Code starts 0x100 into its block, past any prolog
// length, so the whole host frame is always unwound.
void RegisterRelayUnwind(const std::array<bool, 3>& used) noexcept {
    constexpr std::array<std::uint64_t, 3> hostSites{ServerHookRva, ClientTargetHookRva, ClientRewindHookRva};
    DWORD count = 0;
    for (std::size_t block = 0; block < hostSites.size(); ++block) {
        if (!used[block]) continue;
        DWORD64 imageBase = 0;
        const auto* host = RtlLookupFunctionEntry(g_base + hostSites[block], &imageBase, nullptr);
        if (!host || imageBase != g_base || (host->UnwindData & 1U) != 0) {
            Log(1, "cast-on-cast: no unwind data for relay block %zu, stack walks will stop there.", block);
            continue;
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(g_relayPage) + block * RelayBlockSize;
        auto& entry = g_relayUnwind[count++];
        entry.BeginAddress = static_cast<DWORD>(begin - g_base);
        entry.EndAddress = static_cast<DWORD>(begin + RelayBlockSize - g_base);
        entry.UnwindData = host->UnwindData;
    }
    if (count != 0) {
        g_unwindRegistered = RtlAddFunctionTable(g_relayUnwind.data(), count, g_base) != 0;
        if (!g_unwindRegistered) Log(1, "cast-on-cast: RtlAddFunctionTable refused the relay table.");
    }
}

bool BuildRelays(bool server, bool clientTarget, bool clientRewind) noexcept {
    g_relayPage = AllocateRelayPage();
    if (!g_relayPage) {
        Log(2, "cast-on-cast: no free page within jump range of the game image.");
        return false;
    }
    std::memset(g_relayPage, 0xCC, RelayPageSize);

    PlaceRelay(ServerBlock, ServerRelayCode.data(), ServerRelayCode.size());
    SetRelaySlot(ServerBlock, ServerRelayCode_HandlerSlot, reinterpret_cast<std::uint64_t>(&OnServerSkillDo));
    SetRelaySlot(ServerBlock, ServerRelayCode_ResumeSlot, g_base + ServerResumeRva);

    PlaceRelay(ClientTargetBlock, ClientTargetRelayCode.data(), ClientTargetRelayCode.size());
    SetRelaySlot(ClientTargetBlock, ClientTargetRelayCode_HandlerSlot, reinterpret_cast<std::uint64_t>(&OnClientProcCastTarget));
    SetRelaySlot(ClientTargetBlock, ClientTargetRelayCode_ResumeSlot, g_base + ClientTargetResumeRva);

    PlaceRelay(ClientRewindBlock, ClientRewindRelayCode.data(), ClientRewindRelayCode.size());
    SetRelaySlot(ClientRewindBlock, ClientRewindRelayCode_CreatorSlot, g_base + ClientMissileCreateRva);
    SetRelaySlot(ClientRewindBlock, ClientRewindRelayCode_HandlerSlot, reinterpret_cast<std::uint64_t>(&OnClientMissileCreated));
    SetRelaySlot(ClientRewindBlock, ClientRewindRelayCode_ResumeSlot, g_base + ClientRewindResumeRva);

    DWORD oldProtect = 0;
    if (!VirtualProtect(g_relayPage, RelayPageSize, PAGE_EXECUTE_READ, &oldProtect)) {
        Log(2, "cast-on-cast: could not make the relay page executable.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), g_relayPage, RelayPageSize);
    RegisterRelayUnwind({server, clientTarget, clientRewind});
    return true;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
bool ValidateBuild() noexcept {
    bool ok = true;
    for (const auto& witness : Witnesses) {
        if (!g_ctx->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            Log(2, "cast-on-cast: %s does not match at RVA 0x%llX.", witness.what, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

PartState Result(bool installed, const char* part) noexcept {
    if (!installed) Log(2, "cast-on-cast: %s could not be installed.", part);
    return installed ? PartState::Installed : PartState::Failed;
}

void InstallParts() noexcept {
    const bool relaysNeeded = g_config.castTrigger || g_config.clientProcAim || g_config.clientMissileRewind;
    const bool relaysReady = relaysNeeded && BuildRelays(g_config.castTrigger, g_config.clientProcAim, g_config.clientMissileRewind);

    if (g_config.serverProcAim) {
        g_serverProcAim = Result(g_ctx->PatchBytes(ItemEffectAimRva,
            ItemEffectAimExpected.data(), static_cast<std::uint32_t>(ItemEffectAimExpected.size()),
            ItemEffectAimBytes.data(), static_cast<std::uint32_t>(ItemEffectAimBytes.size())), "server_proc_aim");
    }
    if (g_config.clientProcAim) {
        const bool installed = relaysReady
            && g_ctx->PatchBytes(ClientProcBlockRva,
                ClientProcBlockExpected.data(), static_cast<std::uint32_t>(ClientProcBlockExpected.size()),
                ClientProcBlockBytes.data(), static_cast<std::uint32_t>(ClientProcBlockBytes.size()))
            && g_ctx->PatchJmpRel32(ClientTargetHookRva,
                ClientTargetHookExpected.data(), static_cast<std::uint32_t>(ClientTargetHookExpected.size()),
                RelayRva(ClientTargetBlock), static_cast<std::uint32_t>(ClientTargetHookExpected.size()));
        g_clientProcAim = Result(installed, "client_proc_aim");
    }
    if (g_config.clientMissileRewind) {
        const bool installed = relaysReady
            && g_ctx->PatchJmpRel32(ClientRewindHookRva,
                ClientRewindHookExpected.data(), static_cast<std::uint32_t>(ClientRewindHookExpected.size()),
                RelayRva(ClientRewindBlock), static_cast<std::uint32_t>(ClientRewindHookExpected.size()));
        g_clientMissileRewind = Result(installed, "client_missile_rewind");
    }
    if (g_config.castTrigger) {
        const bool installed = relaysReady
            && g_ctx->PatchJmpRel32(ServerHookRva,
                ServerHookExpected.data(), static_cast<std::uint32_t>(ServerHookExpected.size()),
                RelayRva(ServerBlock), static_cast<std::uint32_t>(ServerHookExpected.size()));
        g_castTrigger = Result(installed, "cast_trigger");
    }

    const std::array<PartState, 4> parts{g_castTrigger, g_serverProcAim, g_clientProcAim, g_clientMissileRewind};
    const bool anyFailed = std::any_of(parts.begin(), parts.end(), [](PartState s) { return s == PartState::Failed; });
    const bool anyInstalled = std::any_of(parts.begin(), parts.end(), [](PartState s) { return s == PartState::Installed; });
    g_state = anyFailed ? PluginState::PartiallyActive : PluginState::Active;

    if (anyInstalled) {
        // The relays call into this DLL for the rest of the process lifetime.
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&OnServerSkillDo), &self);
    }
}

const char* StateName(PluginState state) noexcept {
    switch (state) {
    case PluginState::NotLoaded: return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing installed (see the plugin log)";
    case PluginState::Active: return "active";
    case PluginState::PartiallyActive: return "partially active, a part failed to install (see the plugin log)";
    }
    return "unknown";
}

const char* PartName(PartState state) noexcept {
    switch (state) {
    case PartState::Disabled: return "off";
    case PartState::Installed: return "installed";
    case PartState::Failed: return "FAILED";
    }
    return "unknown";
}

D2RL::ConsoleCommandResult StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char line[256];
    std::snprintf(line, sizeof(line), "cast-on-cast: %s", StateName(g_state));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  cast_trigger: %s, fired %llu", PartName(g_castTrigger),
        static_cast<unsigned long long>(g_triggersFired.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  server_proc_aim: %s", PartName(g_serverProcAim));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  client_proc_aim: %s, targets restored %llu", PartName(g_clientProcAim),
        static_cast<unsigned long long>(g_targetsReasserted.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  client_missile_rewind: %s, missiles rewound %llu", PartName(g_clientMissileRewind),
        static_cast<unsigned long long>(g_missilesRewound.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  relay unwind data: %s", g_unwindRegistered ? "registered" : "not registered");
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "celestialrayone.cast-on-cast",
    .name = "Cast on Cast",
    .version = "1.0.0",
    .author = "CelestialRayOne",
    .description = "Fires doactive when a skill is cast, so items can cast skills on cast. Port of the ESR 2.4 patch set.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    g_ctx = context;
    if (!g_ctx || g_ctx->exeBase == 0) return false;
    g_base = g_ctx->exeBase;

    if (!g_ctx->RegisterConsoleCommand("castoncast", StatusCommand, "Show Cast on Cast install state and counters.")) {
        Log(1, "cast-on-cast: console command could not be registered.");
    }

    LoadConfig();
    if (!g_config.enabled) {
        g_state = PluginState::DisabledByConfig;
        Log(0, "cast-on-cast: disabled by config.");
        return true;
    }
    if (!ValidateBuild()) {
        g_state = PluginState::UnsupportedBuild;
        Log(2, "cast-on-cast: this D2R build does not match, nothing was installed.");
        return true;
    }
    g_dispatchUnitEvent = reinterpret_cast<DispatchUnitEventFn>(g_base + DispatchUnitEventRva);
    InstallParts();
    Log(0, "cast-on-cast: %s (%s id %s).", StateName(g_state), "plugin", PluginIdText);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
}
