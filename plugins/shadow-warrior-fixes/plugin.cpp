// Shadow Warrior AI
//
// See celestialrayone.shadow-warrior-ai.toml for the behaviour write-up.
//
// Shadow warriors (every monster whose monstats.txt AI is ShadowWarrior) follow
// the same two skill rules as a player: skills.txt localdelay and globaldelay,
// and the skill's weapon types (itypea1-3, itypeb1-3, etypea1-2, etypeb1-2). A
// skill that breaks a rule is turned down, and the AI falls back exactly as it
// does for any skill it turns down.
//
// Written for D2R 3.3 on D2RLoader 1.3.1, PluginSDK 0.3.0. Every address and
// byte below was read out of the live D2RLoader.exe process image
// (md5 8af7a47ce9077de0378c69af2930854d) and disassembled before being relied
// on. Every site is checked byte for byte at load; nothing is installed when a
// check fails, and the game runs stock.
//
// ---------------------------------------------------------------------------
// Why shadows ignored both rules
// ---------------------------------------------------------------------------
//   Cooldowns are player-only in three places:
//     0x43ACB0  the skill do-handler (game, unit, skillId, level, consume,
//               itemCast, itemEffect). Its cooldown block (globaldelay calc at
//               skill record +234h, localdelay calc at +238h) only runs when the
//               byte at [rsp+0C0h] is set, and 0x43AD17 sets that byte from
//               UnitType == player.
//     0x3404A0  the "is this skill on cooldown" gate answers "free" at once for
//               every non-player.
//     0x339D70  the global delay setter refuses every non-player.
//   The Shadow Warrior AI 0x5CEEA0 (AI table 0x2396E90, row 105) asks none of
//   them. It keeps a pacing timer of its own (AI control +44h): after every
//   skill it turns down every skill but Attack for globaldelay / 3 + 18 frames,
//   and it never reads localdelay. That timer is left as it is.
//
//   Weapon types: players are checked by the use-state routine 0x33F360, whose
//   weapon part is 0x33F1F0 (unit, skill). The AI never calls either. 0x33F1F0
//   reads the unit's own inventory and both hands, so it works for a shadow as
//   it is.
//
// ---------------------------------------------------------------------------
// What is changed
// ---------------------------------------------------------------------------
//   1. 0x5CF420  the AI's skill check (game, unit, owner, skill, skillId,
//                combat, tick), called only by 0x5CEEA0. Inline hook: a skill
//                that breaks a rule is refused before the native checks run, so
//                no random roll is spent and the AI's skill fatigue counter (AI
//                control +48h) does not move. The AI then tries its other
//                skill, then Attack.
//   2. 0x4A7BC0  AITACTICS_UseSkill (game, unit, mode, skillId, target, x, y,
//                flag). Inline hook, shadows only. The AI uses its Attack
//                fallback without asking 0x5CF420, so the final pick is checked
//                here as well. A refused skill gets what the game does when its
//                own mode change refuses a skill (0x4A7CC2): 10 frames standing
//                in neutral through 0x4A6D10, then 0.
//   3. 0x43B152  the do-handler's player test, cmp byte [rsp+0C0h],0 /
//                je 0x43B1C4. Replaced by a jump through a trampoline in the
//                int3 padding at 0x4A37C6 to a relay. Players continue at
//                0x43B15C into the untouched native block. Every other unit
//                calls OnSkillDo(game rdi, unit rbx, skill r13d, level
//                [rsp+0D8h]) and continues at 0x43B1C4, where the native je
//                went. OnSkillDo starts the two cooldowns for shadows only, at
//                the level the do-handler was given, with the same calls in the
//                same order as the native player block. That covers every path
//                a player gets a cooldown from: casts (0x436B10), periodic
//                pulses (0x437460) and aura starts (0x43B680).
//
//   Shadow cooldowns are kept by the plugin, keyed by game and unit GUID the
//   way monster-affixes keeps its lockouts. A different game or a frame counter
//   that went backwards drops an entry. The check mirrors 0x3404A0: a running
//   localdelay blocks its own skill; a running globaldelay blocks every skill
//   whose globaldelay or localdelay evaluates above 0. A newer globaldelay
//   replaces the running one, as 0x2F7AB0 does for players.
//
//   charstats.txt MinimumCastingDelay is not applied: the native setters look
//   it up by class id (0x0FCFC0, charstats.txt row +50h), which only finds a row
//   for player classes.
//
// ---------------------------------------------------------------------------
// Unit, game and record fields read here
// ---------------------------------------------------------------------------
//   unit +00h type (1 = monster), +04h class id (monstats.txt hcIdx),
//   +08h GUID (0x34A330 is mov eax,[rcx+8]), +10h MonsterData.
//   MonsterData +00h -> monstats record, record +52h AI index (int16): the
//   game's own AI table lookup 0x4A36C0 reads exactly these.
//   game +106h data context (byte), +170h frame counter.
//   skills.txt record +234h globaldelay calc, +238h localdelay calc.
//
// ---------------------------------------------------------------------------
// Coexistence (checked against every plugin in d2rloader_plugins)
// ---------------------------------------------------------------------------
//   crossbow-charges witnesses 0x43B163..0x43B19B and redirects the call at
//   0x43B18D. srcdam-calc-field hooks the entry of 0x43ACB0. dynamic-etype
//   redirects the call at 0x43ACFC. cast-on-cast patches 0x43AF1D and uses the
//   padding at 0x43ACA1. None of them writes 0x43B152..0x43B15B, the Shadow
//   Warrior AI, AITACTICS_UseSkill or the padding at 0x4A37C6, and no witness
//   here covers a byte any of them writes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CelestialRayOne::ShadowWarriorAi {
namespace {

// ---------------------------------------------------------------------------
// Addresses (RVAs into D2RLoader.exe)
// ---------------------------------------------------------------------------

constexpr std::uint64_t CheckUseSkillRva      = 0x5CF420;   // Shadow Warrior AI skill check
constexpr std::uint64_t UseSkillRva           = 0x4A7BC0;   // AITACTICS_UseSkill
constexpr std::uint64_t UseSkillRefusalRva    = 0x4A7CC2;   // its "mode change refused" path
constexpr std::uint64_t DoPlayerFlagRva       = 0x43AD0D;   // do-handler: player byte written
constexpr std::uint64_t DoPlayerTestRva       = 0x43B152;   // do-handler: player byte tested
constexpr std::uint64_t DoPlayerResumeRva     = 0x43B15C;   // native cooldown block
constexpr std::uint64_t DoLocalDelayRva       = 0x43B192;   // its localdelay half
constexpr std::uint64_t DoSkipResumeRva       = 0x43B1C4;   // where the native je goes
constexpr std::uint64_t TrampolinePaddingRva  = 0x4A37C5;   // ret, then ten int3
constexpr std::uint64_t TrampolineRva         = 0x4A37C6;
constexpr std::uint64_t ShadowWarriorThinkRva = 0x5CEEA0;
constexpr std::uint64_t AiTableRva            = 0x2396E90;

constexpr std::uint64_t SkillsRecordRva       = 0x097790;   // (dataContext, skillId)
constexpr std::uint64_t EvaluateRva           = 0x3B5160;   // (dataContext, unit, calc, skillId, level)
constexpr std::uint64_t UnitIdRva             = 0x34A330;   // read only: proves the GUID offset
constexpr std::uint64_t SkillByIdRva          = 0x33DCD0;   // (unit, skillId, ownerGuid)
constexpr std::uint64_t SkillLevelRva         = 0x33D1E0;   // loader thunk, (unit, skill, 1, 0)
constexpr std::uint64_t WeaponCheckRva        = 0x33F1F0;   // (unit, skill) -> bool
constexpr std::uint64_t IdleRva               = 0x4A6D10;   // (game, unit, frames)

constexpr std::uint32_t DoPlayerTestSize = 10;
static_assert(DoPlayerResumeRva == DoPlayerTestRva + DoPlayerTestSize);
static_assert(TrampolineRva == TrampolinePaddingRva + 1);

// ---------------------------------------------------------------------------
// Witnesses
// ---------------------------------------------------------------------------

// 0x5CF420  mov [rsp+8],rbx / mov [rsp+10h],rbp / mov [rsp+18h],rsi /
//           push rdi, r12, r13, r14, r15 / sub rsp,30h. No branch, no
//           RIP-relative operand.
constexpr std::uint8_t CheckUseSkillEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30 };

// 0x4A7BC0  mov [rsp+10h],rbp / mov [rsp+20h],rsi / push rdi / sub rsp,50h /
//           mov ebp,r9d (skill id) / mov rdi,rdx (unit) / mov rsi,rcx (game)
constexpr std::uint8_t UseSkillEntry[]{
    0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC,
    0x50, 0x41, 0x8B, 0xE9, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1 };

// 0x4A7CC2  mov r8d,0Ah / mov rdx,rdi / mov rcx,rsi / call 0x4A6D10 /
//           mov rbp,[rsp+68h] / xor eax,eax. Read only: the refusal the hook
//           copies.
constexpr std::uint8_t UseSkillRefusal[]{
    0x41, 0xB8, 0x0A, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCE,
    0xE8, 0x3D, 0xF0, 0xFF, 0xFF, 0x48, 0x8B, 0x6C, 0x24, 0x68, 0x33, 0xC0 };

// 0x43AD0D  mov rcx,rbx / call 0x34B9D0 (UnitType) / test eax,eax /
//           sete byte [rsp+0C0h]. Read only: the byte tested at 0x43B152 is
//           "the unit is a player".
constexpr std::uint8_t DoPlayerFlag[]{
    0x48, 0x8B, 0xCB, 0xE8, 0xBB, 0x0C, 0xF1, 0xFF, 0x85, 0xC0,
    0x0F, 0x94, 0x84, 0x24, 0xC0, 0x00, 0x00, 0x00 };

// 0x43B152  cmp byte [rsp+0C0h],0 / je 0x43B1C4. The patched window.
constexpr std::uint8_t DoPlayerTest[]{
    0x80, 0xBC, 0x24, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x74, 0x68 };
static_assert(sizeof(DoPlayerTest) == DoPlayerTestSize);

// 0x43B15C..0x43B18C  the native block the relay hands players back to:
//   mov ebp,[rsp+0D8h] (level) / mov r9d,r13d (skill id) /
//   mov r8d,[rsi+234h] (globaldelay calc) / mov rdx,rbx (unit) /
//   movzx ecx,byte [rdi+106h] (data context of the game in rdi) /
//   mov [rsp+20h],ebp / call 0x3B5160 / test eax,eax / jle /
//   mov r8d,eax / mov rdx,rbx / mov rcx,rdi
// Read only. It stops right before crossbow-charges' call at 0x43B18D.
constexpr std::uint8_t DoPlayerBlock[]{
    0x8B, 0xAC, 0x24, 0xD8, 0x00, 0x00, 0x00, 0x45, 0x8B, 0xCD, 0x44, 0x8B, 0x86, 0x34,
    0x02, 0x00, 0x00, 0x48, 0x8B, 0xD3, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x89,
    0x6C, 0x24, 0x20, 0xE8, 0xE0, 0x9F, 0xF7, 0xFF, 0x85, 0xC0, 0x7E, 0x0E, 0x44, 0x8B,
    0xC0, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF };

// 0x43B192..0x43B1BE  its localdelay half: mov r8d,[rsi+238h] / mov r9d,r13d /
//   movzx ecx,byte [rdi+106h] / mov rdx,rbx / mov [rsp+20h],ebp /
//   call 0x3B5160 / test eax,eax / jle 0x43B1C4 / mov r9d,r13d / mov r8d,eax /
//   mov rdx,rbx / mov rcx,rdi. Read only; stops before the call at 0x43B1BF.
constexpr std::uint8_t DoLocalDelay[]{
    0x44, 0x8B, 0x86, 0x38, 0x02, 0x00, 0x00, 0x45, 0x8B, 0xCD, 0x0F, 0xB6, 0x8F, 0x06,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0xD3, 0x89, 0x6C, 0x24, 0x20, 0xE8, 0xB1, 0x9F, 0xF7,
    0xFF, 0x85, 0xC0, 0x7E, 0x11, 0x45, 0x8B, 0xCD, 0x44, 0x8B, 0xC0, 0x48, 0x8B, 0xD3,
    0x48, 0x8B, 0xCF };

// 0x43B1C4  mov rcx,rbx / call [rip+39EEFE3h]: where the native je lands. It
// overwrites rcx and calls at once, so no scratch register is live there and
// the relay's own call cannot disturb it. Read only.
constexpr std::uint8_t DoSkip[]{ 0x48, 0x8B, 0xCB, 0xFF, 0x15, 0xE3, 0xEF, 0x9E, 0x03 };

// 0x4A37C5  the ret that ends 0x4A36C0, then its ten int3 before 0x4A37D0.
// The trampoline takes the first five.
constexpr std::uint8_t TrampolinePadding[]{
    0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
constexpr std::uint8_t TrampolineSlot[]{ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

// Entries of the game functions called here.
constexpr std::uint8_t SkillsRecordEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC,
    0x30, 0x48, 0x63, 0xF2 };
constexpr std::uint8_t EvaluateEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00 };
// sub rsp,28h / test rcx,rcx / jnz / (null: assert, -1) / mov eax,[rcx+8]
constexpr std::uint8_t UnitIdBody[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48,
    0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x08 };
constexpr std::uint8_t SkillByIdEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xFA };
constexpr std::uint8_t LoaderThunk[]{ 0xFF, 0x25 };
constexpr std::uint8_t WeaponCheckEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56,
    0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0xF2, 0x48, 0x85, 0xD2, 0x75, 0x28 };
constexpr std::uint8_t IdleEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x70, 0x45, 0x85, 0xC0, 0x48, 0x8B, 0xF9 };

// ---------------------------------------------------------------------------
// Relay (keystone-assembled, capstone and unicorn verified)
// ---------------------------------------------------------------------------
// Entered from 0x43B152 through the trampoline, with the do-handler's frame and
// registers untouched (two jmps):
//   +00h  cmp byte [rsp+0C0h],0          ; the native player test
//   +08h  jne +27h                       ; player: back to 0x43B15C
//   +0Ah  mov rcx,rdi                    ; game
//   +0Dh  mov rdx,rbx                    ; unit
//   +10h  mov r8d,r13d                   ; skill id
//   +13h  mov r9d,[rsp+0D8h]             ; level
//   +1Bh  call [rip+0Fh]                 ; OnSkillDo, slot +30h
//   +21h  jmp [rip+11h]                  ; 0x43B1C4, slot +38h
//   +27h  jmp [rip+13h]                  ; 0x43B15C, slot +40h
// rsp is never moved: the do-handler body keeps it 16-byte aligned, so the
// call enters OnSkillDo exactly as a normal call would, and OnSkillDo's home
// space is the do-handler's outgoing home area, scratch at this point.
constexpr std::array<std::uint8_t, 0x48> RelayCode{
    0x80, 0xBC, 0x24, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x1D, 0x48, 0x89, 0xF9, 0x48,
    0x89, 0xDA, 0x45, 0x89, 0xE8, 0x44, 0x8B, 0x8C, 0x24, 0xD8, 0x00, 0x00, 0x00, 0xFF,
    0x15, 0x0F, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x11, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x13,
    0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00 };
constexpr std::size_t RelayHandlerSlot = 0x30;
constexpr std::size_t RelaySkipSlot    = 0x38;
constexpr std::size_t RelayPlayerSlot  = 0x40;

constexpr std::size_t RelayPageSize   = 0x1000;
constexpr std::size_t RelayBlockSize  = 0x200;
// The relay is described with the do-handler's own unwind data. Its code starts
// 100h into the block, past any prolog length, so the unwinder always treats it
// as the do-handler's body and unwinds the whole host frame.
constexpr std::size_t RelayCodeOffset = 0x100;
static_assert(RelayCodeOffset + RelayCode.size() <= RelayBlockSize);
constexpr std::uint32_t JumpSize = 5;

// ---------------------------------------------------------------------------
// Layouts and ids
// ---------------------------------------------------------------------------

constexpr std::size_t UnitTypeOffset             = 0x00;
constexpr std::size_t UnitClassIdOffset          = 0x04;
constexpr std::size_t UnitGuidOffset             = 0x08;
constexpr std::size_t UnitMonsterDataOffset      = 0x10;
constexpr std::size_t MonsterDataMonStatsOffset  = 0x00;
constexpr std::size_t MonStatsAiOffset           = 0x52;
constexpr std::size_t GameDataContextOffset      = 0x106;
constexpr std::size_t GameFrameOffset            = 0x170;
constexpr std::size_t SkillGlobalDelayCalcOffset = 0x234;
constexpr std::size_t SkillLocalDelayCalcOffset  = 0x238;
constexpr std::size_t AiTableRowSize             = 0x20;
constexpr std::size_t AiTableThinkOffset         = 0x10;

constexpr std::uint32_t UnitTypeMonster      = 1;
constexpr std::int16_t  ShadowWarriorAiIndex = 105;
constexpr std::int32_t  RefusedIdleFrames    = 10;          // the game's own, 0x4A7CC2
constexpr std::uint32_t AnyOwnerGuid         = 0xFFFFFFFFu; // the unit's own skill
constexpr std::uint32_t GlobalDelaySlot      = 0xFFFFFFFFu; // never a skill id
constexpr std::size_t   CooldownPruneSize    = 4096;

// ---------------------------------------------------------------------------
// Native signatures (Windows x64 ABI)
// ---------------------------------------------------------------------------

using CheckUseSkillFn = std::uint32_t (*)(void* game, void* unit, void* owner, void* skill,
                                          std::int32_t skillId, std::int32_t combat, void* tick) noexcept;
using UseSkillFn      = std::uint32_t (*)(void* game, void* unit, std::uint8_t mode, std::int32_t skillId,
                                          void* target, std::int32_t x, std::int32_t y, std::uint8_t flag) noexcept;
using SkillsRecordFn  = const std::uint8_t* (*)(std::uint8_t context, std::int32_t skillId) noexcept;
using EvaluateFn      = std::int32_t (*)(std::uint8_t context, void* unit, std::uint32_t calc,
                                         std::int32_t skillId, std::int32_t level) noexcept;
using SkillByIdFn     = void* (*)(void* unit, std::int32_t skillId, std::uint32_t ownerGuid) noexcept;
using SkillLevelFn    = std::int32_t (*)(void* unit, void* skill, std::int32_t withBonus, std::int32_t zero) noexcept;
using WeaponCheckFn   = std::uint8_t (*)(void* unit, void* skill) noexcept;
using IdleFn          = void (*)(void* game, void* unit, std::int32_t frames) noexcept;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char        ConfigSection[]    = "shadow_warrior_ai";
constexpr std::size_t InitialConfigBytes = 16'384;
constexpr std::size_t MaximumConfigBytes = 1'048'576;

constexpr char DefaultConfigToml[] =
"# Shadow Warrior AI\n"
    "#\n"
    "# Shadow warriors follow the same two skill rules as a player. Covered: every\n"
    "# monster whose monstats.txt AI column is ShadowWarrior (the Assassin's Shadow\n"
    "# Warrior, and any other monster you give that AI). Shadow Master runs a\n"
    "# different AI and is not changed.\n"
    "#\n"
    "# 1. Cooldowns, skills.txt localdelay and globaldelay\n"
    "#    - localdelay: after the shadow casts the skill, that one skill is locked\n"
    "#      for the shadow for that many frames (25 frames = 1 second).\n"
    "#    - globaldelay: after the shadow casts the skill, every skill of the shadow\n"
    "#      with a globaldelay or a localdelay above 0 is locked for that many\n"
    "#      frames. A newer globaldelay replaces the running one, as for players.\n"
    "#    - Both are evaluated at the shadow's own skill level and start the moment\n"
    "#      the skill fires, the same moment a player's do. Each shadow has its own.\n"
    "#    - charstats.txt MinimumCastingDelay does not apply: the game only has rows\n"
    "#      for player classes, so it never applies to monsters.\n"
    "#\n"
    "# 2. Weapon types, skills.txt itypea1-3, itypeb1-3, etypea1-2 and etypeb1-2\n"
    "#    The shadow must hold what the skill asks for. It is checked against the\n"
    "#    shadow's own equipped weapons with the exact test the game uses for\n"
    "#    players. Example: a trap whose itypea1 asks for throwing knives is only\n"
    "#    used by a shadow holding throwing knives.\n"
    "#\n"
    "# What the shadow does instead\n"
    "#    A skill that breaks a rule is turned down, and the AI does what it already\n"
    "#    does for any skill it turns down: it tries its other skill, then a plain\n"
    "#    attack. If the attack is blocked as well, the shadow stands for 10 frames\n"
    "#    (the game's own pause after a refused skill) and thinks again.\n"
    "#\n"
    "# Unchanged: the AI's skill choice, its leash to the Assassin, and its own\n"
    "# pacing (after any skill it waits globaldelay / 3 + 18 frames before its next\n"
    "# skill other than the attack).\n"
    "#\n"
    "# Server side only. In single player the game hosts its own server, so it\n"
    "# applies there too.\n"
    "#\n"
    "# Console command:\n"
    "#   shadowai             status and counters\n"
    "#   shadowai debug on    write every refused skill and every started cooldown\n"
    "#                        to the plugin log\n"
    "#   shadowai debug off   stop writing them\n"
    "\n"
    "[shadow_warrior_ai]\n"
    "\n"
    "# Master switch. false installs nothing at all.\n"
    "enabled = true\n"
    "\n"
    "# Rule 1: skills.txt localdelay and globaldelay.\n"
    "cooldowns = true\n"
    "\n"
    "# Rule 2: skills.txt itypea1-3, itypeb1-3, etypea1-2 and etypeb1-2.\n"
    "weapon_types = true\n"
    "\n"
    "# Writes every refused skill (with the rule it broke) and every started\n"
    "# cooldown to the plugin log. Same as \"shadowai debug on\". Leave off in play.\n"
    "debug = false\n";

struct Settings {
    bool enabled     = true;
    bool cooldowns   = true;
    bool weaponTypes = true;
    bool debug       = false;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class InstallState : std::uint8_t {
    NotAttempted,
    DisabledByConfig,
    NothingEnabled,
    UnsupportedBuild,
    InstallFailed,
    Partial,
    Installed,
};

enum class Rule : std::uint8_t { None, WeaponTypes, LocalDelay, GlobalDelay };
enum class Site : std::uint8_t { SkillCheck, UseSkill };

constexpr std::size_t RuleCount = 3;   // WeaponTypes, LocalDelay, GlobalDelay
constexpr std::size_t SiteCount = 2;

struct Verdict {
    Rule         rule       = Rule::None;
    std::int32_t framesLeft = 0;
};

struct CooldownEntry {
    void*        game        = nullptr;
    std::int32_t setFrame    = 0;
    std::int32_t expireFrame = 0;
};

const D2RL::PluginContext*  Context{};
std::uintptr_t              Base{};
Settings                    Config{};
bool                        ConfigFileWasRead{};
std::atomic<InstallState>   State{ InstallState::NotAttempted };
std::atomic<bool>           DebugLog{ false };

bool CheckHookInstalled{};
bool UseHookInstalled{};
bool RelayInstalled{};
bool UnwindRegistered{};

CheckUseSkillFn OriginalCheckUseSkill{};
UseSkillFn      OriginalUseSkill{};
SkillsRecordFn  SkillsRecord{};
EvaluateFn      Evaluate{};
SkillByIdFn     SkillById{};
SkillLevelFn    SkillLevel{};
WeaponCheckFn   WeaponCheck{};
IdleFn          Idle{};

std::array<std::array<std::atomic<std::uint64_t>, RuleCount>, SiteCount> Refused{};
std::atomic<std::uint64_t> ShadowCasts{};
std::atomic<std::uint64_t> StartedGlobal{};
std::atomic<std::uint64_t> StartedLocal{};

std::mutex                                       CooldownMutex;
std::unordered_map<std::uint64_t, CooldownEntry> Cooldowns;

std::uint8_t*                     RelayPage{};
std::array<RUNTIME_FUNCTION, 1>   RelayUnwind{};
std::array<std::uint8_t, JumpSize> TrampolineJump{};
bool                              TrampolineWritten{};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <typename T>
auto ReadField(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(Size);
}

auto GameFrame(const void* game) noexcept -> std::int32_t {
    return ReadField<std::int32_t>(game, GameFrameOffset);
}

auto DataContext(const void* game) noexcept -> std::uint8_t {
    return ReadField<std::uint8_t>(game, GameDataContextOffset);
}

auto UnitGuid(const void* unit) noexcept -> std::uint32_t {
    return ReadField<std::uint32_t>(unit, UnitGuidOffset);
}

auto UnitClassId(const void* unit) noexcept -> std::uint32_t {
    return ReadField<std::uint32_t>(unit, UnitClassIdOffset);
}

// A monster whose monstats.txt AI is ShadowWarrior: the same fields the
// game's AI table lookup 0x4A36C0 reads.
auto IsShadowWarriorUnit(const void* unit) noexcept -> bool {
    if (unit == nullptr) return false;
    if (ReadField<std::uint32_t>(unit, UnitTypeOffset) != UnitTypeMonster) return false;
    const void* monsterData = ReadField<const void*>(unit, UnitMonsterDataOffset);
    if (monsterData == nullptr) return false;
    const void* monStats = ReadField<const void*>(monsterData, MonsterDataMonStatsOffset);
    if (monStats == nullptr) return false;
    return ReadField<std::int16_t>(monStats, MonStatsAiOffset) == ShadowWarriorAiIndex;
}

auto EvaluateCalc(std::uint8_t context, void* unit, const std::uint8_t* record, std::size_t calcOffset,
                  std::int32_t skillId, std::int32_t level) noexcept -> std::int32_t {
    return Evaluate(context, unit, ReadField<std::uint32_t>(record, calcOffset), skillId, level);
}

// ---------------------------------------------------------------------------
// Shadow cooldowns, keyed by unit GUID and skill id (GlobalDelaySlot for the
// globaldelay). Each entry remembers its game and the frame it was set on.
// ---------------------------------------------------------------------------

auto CooldownKey(std::uint32_t guid, std::uint32_t slot) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(guid) << 32) | slot;
}

auto IsStale(const CooldownEntry& entry, const void* game, std::int32_t frame) noexcept -> bool {
    // A different game, a frame counter that went backwards (a new game that
    // reuses the address) or a lapsed cooldown.
    return entry.game != game || frame < entry.setFrame || frame >= entry.expireFrame;
}

auto FramesLeft(void* game, std::uint32_t guid, std::uint32_t slot, std::int32_t frame) noexcept -> std::int32_t {
    const std::lock_guard<std::mutex> guard(CooldownMutex);
    const auto it = Cooldowns.find(CooldownKey(guid, slot));
    if (it == Cooldowns.end()) return 0;
    if (IsStale(it->second, game, frame)) {
        Cooldowns.erase(it);
        return 0;
    }
    return it->second.expireFrame - frame;
}

void StartCooldown(void* game, std::uint32_t guid, std::uint32_t slot, std::int32_t frame,
                   std::int32_t frames) noexcept {
    const std::int64_t expire = static_cast<std::int64_t>(frame) + frames;
    const std::int32_t expireFrame = expire > INT32_MAX ? INT32_MAX : static_cast<std::int32_t>(expire);
    try {
        const std::lock_guard<std::mutex> guard(CooldownMutex);
        if (Cooldowns.size() >= CooldownPruneSize) {
            for (auto it = Cooldowns.begin(); it != Cooldowns.end();) {
                if (IsStale(it->second, game, frame)) {
                    it = Cooldowns.erase(it);
                } else {
                    ++it;
                }
            }
        }
        // Overwrites a running one, as the native setters do for players.
        Cooldowns[CooldownKey(guid, slot)] = CooldownEntry{ game, frame, expireFrame };
    } catch (...) {
        // Out of memory: the cast already happened, only its cooldown is lost.
    }
}

auto CooldownsHeld() noexcept -> std::size_t {
    const std::lock_guard<std::mutex> guard(CooldownMutex);
    return Cooldowns.size();
}

// ---------------------------------------------------------------------------
// The two rules
// ---------------------------------------------------------------------------

// The player's rules for this skill: weapon types through the game's own test,
// then the cooldown gate the way 0x3404A0 decides it.
auto Judge(void* game, void* unit, void* skill, std::int32_t skillId) noexcept -> Verdict {
    if (Config.weaponTypes && skill != nullptr && WeaponCheck(unit, skill) == 0) {
        return { Rule::WeaponTypes, 0 };
    }
    if (!Config.cooldowns) return {};

    const std::int32_t  frame = GameFrame(game);
    const std::uint32_t guid  = UnitGuid(unit);

    if (const std::int32_t left = FramesLeft(game, guid, static_cast<std::uint32_t>(skillId), frame); left > 0) {
        return { Rule::LocalDelay, left };
    }

    const std::int32_t left = FramesLeft(game, guid, GlobalDelaySlot, frame);
    if (left <= 0) return {};

    // A running globaldelay blocks every skill that has a globaldelay or a
    // localdelay, evaluated at the skill's current level.
    const std::uint8_t  context = DataContext(game);
    const std::uint8_t* record  = SkillsRecord(context, skillId);
    if (record == nullptr) return {};
    const std::int32_t level = skill != nullptr ? std::max(SkillLevel(unit, skill, 1, 0), 0) : 0;
    if (EvaluateCalc(context, unit, record, SkillGlobalDelayCalcOffset, skillId, level) > 0
            || EvaluateCalc(context, unit, record, SkillLocalDelayCalcOffset, skillId, level) > 0) {
        return { Rule::GlobalDelay, left };
    }
    return {};
}

auto RuleIndex(Rule rule) noexcept -> std::size_t {
    return static_cast<std::size_t>(rule) - 1;
}

auto SiteText(Site site) noexcept -> const char* {
    return site == Site::SkillCheck ? "skill check" : "final use";
}

void Report(Site site, const void* unit, std::int32_t skillId, const Verdict& verdict) noexcept {
    Refused[static_cast<std::size_t>(site)][RuleIndex(verdict.rule)].fetch_add(1, std::memory_order_relaxed);
    if (!DebugLog.load(std::memory_order_relaxed) || Context == nullptr) return;

    switch (verdict.rule) {
    case Rule::WeaponTypes:
        D2RL::LogInfoF(Context,
            "ShadowWarriorAI: unit %u (hcIdx %u) skill %d refused at the %s: the weapons it holds do not "
            "match the skill's itypes/etypes.",
            UnitGuid(unit), UnitClassId(unit), skillId, SiteText(site));
        break;
    case Rule::LocalDelay:
        D2RL::LogInfoF(Context,
            "ShadowWarriorAI: unit %u (hcIdx %u) skill %d refused at the %s: its localdelay has %d frames left.",
            UnitGuid(unit), UnitClassId(unit), skillId, SiteText(site), verdict.framesLeft);
        break;
    case Rule::GlobalDelay:
        D2RL::LogInfoF(Context,
            "ShadowWarriorAI: unit %u (hcIdx %u) skill %d refused at the %s: a globaldelay has %d frames left.",
            UnitGuid(unit), UnitClassId(unit), skillId, SiteText(site), verdict.framesLeft);
        break;
    case Rule::None:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// 1. The Shadow Warrior AI's own skill check. Refusing here, before the native
//    body, is what lets the AI fall back to its other skill and then Attack.
auto HookCheckUseSkill(void* game, void* unit, void* owner, void* skill, std::int32_t skillId,
                       std::int32_t combat, void* tick) noexcept -> std::uint32_t {
    if (game != nullptr && skill != nullptr && IsShadowWarriorUnit(unit)) {
        const Verdict verdict = Judge(game, unit, skill, skillId);
        if (verdict.rule != Rule::None) {
            Report(Site::SkillCheck, unit, skillId, verdict);
            return 0;
        }
    }
    const CheckUseSkillFn original = OriginalCheckUseSkill;
    return original != nullptr ? original(game, unit, owner, skill, skillId, combat, tick) : 0;
}

// 2. Every skill a shadow is told to use, the unchecked Attack fallback
//    included. A refusal is the game's own: 10 frames in neutral, then 0.
auto HookUseSkill(void* game, void* unit, std::uint8_t mode, std::int32_t skillId, void* target,
                  std::int32_t x, std::int32_t y, std::uint8_t flag) noexcept -> std::uint32_t {
    if (game != nullptr && IsShadowWarriorUnit(unit)) {
        void* skill = SkillById(unit, skillId, AnyOwnerGuid);
        if (skill != nullptr) {
            const Verdict verdict = Judge(game, unit, skill, skillId);
            if (verdict.rule != Rule::None) {
                Report(Site::UseSkill, unit, skillId, verdict);
                Idle(game, unit, RefusedIdleFrames);
                return 0;
            }
        }
    }
    const UseSkillFn original = OriginalUseSkill;
    return original != nullptr ? original(game, unit, mode, skillId, target, x, y, flag) : 0;
}

// 3. Reached from the relay for every non-player do-event that spends
//    resources: the moment the native block would start a player's cooldowns.
//    Same calls, same order, shadows only.
void OnSkillDo(void* game, void* unit, std::int32_t skillId, std::int32_t level) noexcept {
    if (!Config.cooldowns || game == nullptr || !IsShadowWarriorUnit(unit)) return;

    const std::uint8_t  context = DataContext(game);
    const std::uint8_t* record  = SkillsRecord(context, skillId);
    if (record == nullptr) return;
    ShadowCasts.fetch_add(1, std::memory_order_relaxed);

    const std::int32_t  frame = GameFrame(game);
    const std::uint32_t guid  = UnitGuid(unit);

    const std::int32_t global = EvaluateCalc(context, unit, record, SkillGlobalDelayCalcOffset, skillId, level);
    if (global > 0) {
        StartCooldown(game, guid, GlobalDelaySlot, frame, global);
        StartedGlobal.fetch_add(1, std::memory_order_relaxed);
    }
    const std::int32_t local = EvaluateCalc(context, unit, record, SkillLocalDelayCalcOffset, skillId, level);
    if (local > 0) {
        StartCooldown(game, guid, static_cast<std::uint32_t>(skillId), frame, local);
        StartedLocal.fetch_add(1, std::memory_order_relaxed);
    }

    if ((global > 0 || local > 0) && DebugLog.load(std::memory_order_relaxed) && Context != nullptr) {
        D2RL::LogInfoF(Context,
            "ShadowWarriorAI: unit %u (hcIdx %u) cast skill %d at level %d: globaldelay %d, localdelay %d frames.",
            guid, UnitClassId(unit), skillId, level, global > 0 ? global : 0, local > 0 ? local : 0);
    }
}

// ---------------------------------------------------------------------------
// Config parsing (small TOML subset, no external dependency)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r'
                || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

void ParseConfig(std::string_view text, Settings& settings) noexcept {
    std::string_view section;
    std::size_t      cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;
        if (line.empty()) continue;

        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = Trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != ConfigSection) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));

        bool* target = nullptr;
        if (key == "enabled") {
            target = &settings.enabled;
        } else if (key == "cooldowns") {
            target = &settings.cooldowns;
        } else if (key == "weapon_types") {
            target = &settings.weaponTypes;
        } else if (key == "debug") {
            target = &settings.debug;
        } else {
            if (Context != nullptr) {
                D2RL::LogWarnF(Context, "ShadowWarriorAI: unknown setting \"%.*s\" ignored.",
                    static_cast<int>(key.size()), key.data());
            }
            continue;
        }
        if (!ParseBool(value, *target) && Context != nullptr) {
            D2RL::LogWarnF(Context, "ShadowWarriorAI: %.*s must be true or false; keeping %s.",
                static_cast<int>(key.size()), key.data(), *target ? "true" : "false");
        }
    }
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    try {
        std::string   buffer(InitialConfigBytes, '\0');
        std::uint32_t required = 0;
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            if (required < buffer.size() || required >= MaximumConfigBytes) return false;
            buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
            if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
                return false;
            }
        }
        buffer.resize(std::strlen(buffer.c_str()));
        out = std::move(buffer);
        return true;
    } catch (...) {
        return false;
    }
}

void ReadConfiguration() noexcept {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("ShadowWarriorAI: the config file could not be created; using the embedded defaults.");
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("ShadowWarriorAI: the config file could not be read; using the embedded defaults.");
    } else {
        ConfigFileWasRead = true;
    }
    if (!ConfigFileWasRead) {
        ParseConfig(DefaultConfigToml, Config);
    } else {
        ParseConfig(text, Config);
    }
    DebugLog.store(Config.debug, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Relay page and trampoline
// ---------------------------------------------------------------------------

auto AllocateRelayPage() noexcept -> std::uint8_t* {
    const auto* image       = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntHeaders   = ReadField<std::int32_t>(image, 0x3C);
    const auto  sizeOfImage = ReadField<std::uint32_t>(image, static_cast<std::size_t>(ntHeaders) + 0x50);

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity =
        systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) { return (value + granularity - 1) & ~(granularity - 1); };

    // The trampoline's rel32 reaches 2 GB past itself; stay well inside.
    const std::uintptr_t limit   = Base + TrampolineRva + 0x7FF00000ULL;
    std::uintptr_t       address = alignUp(Base + sizeOfImage);
    while (address + RelayPageSize < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageSize <= regionEnd) {
            if (void* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageSize,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const std::uintptr_t next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

// The relay runs inside the do-handler's frame, so it borrows the do-handler's
// unwind data; stack walks and crash reports then see straight through it.
void RegisterRelayUnwind() noexcept {
    DWORD64    imageBase = 0;
    const auto host      = RtlLookupFunctionEntry(static_cast<DWORD64>(Base + DoPlayerTestRva), &imageBase, nullptr);
    if (host == nullptr || imageBase != static_cast<DWORD64>(Base) || (host->UnwindData & 1U) != 0) {
        Context->LogWarn("ShadowWarriorAI: no unwind data for the relay; stack walks stop there.");
        return;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(RelayPage);
    RelayUnwind[0].BeginAddress = static_cast<DWORD>(begin - Base);
    RelayUnwind[0].EndAddress   = static_cast<DWORD>(begin + RelayBlockSize - Base);
    RelayUnwind[0].UnwindData   = host->UnwindData;
    UnwindRegistered = RtlAddFunctionTable(RelayUnwind.data(), 1, static_cast<DWORD64>(Base)) != FALSE;
    if (!UnwindRegistered) Context->LogWarn("ShadowWarriorAI: RtlAddFunctionTable refused the relay.");
}

void WriteSlot(std::uint8_t* code, std::size_t slot, std::uint64_t value) noexcept {
    std::memcpy(code + slot, &value, sizeof(value));
}

auto BuildRelay() noexcept -> bool {
    RelayPage = AllocateRelayPage();
    if (RelayPage == nullptr) {
        Context->LogError("ShadowWarriorAI: no free page within jump range of the game image.");
        return false;
    }
    std::memset(RelayPage, 0xCC, RelayPageSize);
    std::uint8_t* code = RelayPage + RelayCodeOffset;
    std::memcpy(code, RelayCode.data(), RelayCode.size());
    WriteSlot(code, RelayHandlerSlot, reinterpret_cast<std::uint64_t>(&OnSkillDo));
    WriteSlot(code, RelaySkipSlot, static_cast<std::uint64_t>(Base + DoSkipResumeRva));
    WriteSlot(code, RelayPlayerSlot, static_cast<std::uint64_t>(Base + DoPlayerResumeRva));

    DWORD oldProtect = 0;
    if (!VirtualProtect(RelayPage, RelayPageSize, PAGE_EXECUTE_READ, &oldProtect)) {
        Context->LogError("ShadowWarriorAI: the relay page could not be made executable.");
        VirtualFree(RelayPage, 0, MEM_RELEASE);
        RelayPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageSize);
    RegisterRelayUnwind();
    return true;
}

auto WriteTrampoline() noexcept -> bool {
    const auto          from = static_cast<std::int64_t>(Base + TrampolineRva + JumpSize);
    const auto          to   = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(RelayPage + RelayCodeOffset));
    const std::int64_t  displacement = to - from;
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        Context->LogError("ShadowWarriorAI: the relay is out of reach of the trampoline at 0x4A37C6.");
        return false;
    }
    const auto rel32 = static_cast<std::int32_t>(displacement);
    TrampolineJump[0] = 0xE9;
    std::memcpy(TrampolineJump.data() + 1, &rel32, sizeof(rel32));
    TrampolineWritten = Context->PatchBytes(TrampolineRva, TrampolineSlot, ByteCount(TrampolineSlot),
        TrampolineJump.data(), JumpSize);
    if (!TrampolineWritten) Context->LogError("ShadowWarriorAI: the trampoline at 0x4A37C6 could not be written.");
    return TrampolineWritten;
}

void RestoreTrampoline() noexcept {
    if (!TrampolineWritten) return;
    if (Context->PatchBytes(TrampolineRva, TrampolineJump.data(), JumpSize, TrampolineSlot, ByteCount(TrampolineSlot))) {
        TrampolineWritten = false;
    }
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         what;
    bool                cooldownsOnly;
};

constexpr std::array<Witness, 15> Witnesses{{
    { CheckUseSkillRva,     CheckUseSkillEntry, ByteCount(CheckUseSkillEntry), "Shadow Warrior skill check entry", false },
    { UseSkillRva,          UseSkillEntry,      ByteCount(UseSkillEntry),      "AITACTICS_UseSkill entry",         false },
    { UseSkillRefusalRva,   UseSkillRefusal,    ByteCount(UseSkillRefusal),    "AITACTICS_UseSkill refusal",       false },
    { SkillsRecordRva,      SkillsRecordEntry,  ByteCount(SkillsRecordEntry),  "skills.txt record lookup",         false },
    { EvaluateRva,          EvaluateEntry,      ByteCount(EvaluateEntry),      "skills.txt formula evaluator",     false },
    { UnitIdRva,            UnitIdBody,         ByteCount(UnitIdBody),         "unit GUID getter",                 false },
    { SkillByIdRva,         SkillByIdEntry,     ByteCount(SkillByIdEntry),     "skill lookup",                     false },
    { SkillLevelRva,        LoaderThunk,        ByteCount(LoaderThunk),        "skill level thunk",                false },
    { WeaponCheckRva,       WeaponCheckEntry,   ByteCount(WeaponCheckEntry),   "skill weapon-type test",           false },
    { IdleRva,              IdleEntry,          ByteCount(IdleEntry),          "AITACTICS_IdleInNeutralMode",      false },
    { DoPlayerFlagRva,      DoPlayerFlag,       ByteCount(DoPlayerFlag),       "do-handler player flag",           true  },
    { DoPlayerTestRva,      DoPlayerTest,       ByteCount(DoPlayerTest),       "do-handler player test",           true  },
    { DoPlayerResumeRva,    DoPlayerBlock,      ByteCount(DoPlayerBlock),      "do-handler cooldown block",        true  },
    { DoLocalDelayRva,      DoLocalDelay,       ByteCount(DoLocalDelay),       "do-handler localdelay block",      true  },
    { DoSkipResumeRva,      DoSkip,             ByteCount(DoSkip),             "do-handler skip target",           true  },
}};

auto VerifyImage() noexcept -> bool {
    bool verified = true;
    for (const Witness& witness : Witnesses) {
        if (witness.cooldownsOnly && !Config.cooldowns) continue;
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context,
                "ShadowWarriorAI: the %s at 0x%llX does not match the verified D2R image, or another plugin "
                "already changed it.",
                witness.what, static_cast<unsigned long long>(witness.rva));
            verified = false;
        }
    }
    if (Config.cooldowns
            && !Context->CheckExpectedBytes(TrampolinePaddingRva, TrampolinePadding, ByteCount(TrampolinePadding))) {
        Context->LogError("ShadowWarriorAI: the int3 padding at 0x4A37C6 is not free.");
        verified = false;
    }
    return verified;
}

auto InstallRelay() noexcept -> bool {
    if (!BuildRelay()) return false;
    if (!WriteTrampoline()) return false;
    if (!Context->PatchJmpRel32(DoPlayerTestRva, DoPlayerTest, ByteCount(DoPlayerTest), TrampolineRva,
            DoPlayerTestSize)) {
        Context->LogError("ShadowWarriorAI: the do-handler player test at 0x43B152 could not be patched.");
        RestoreTrampoline();
        return false;
    }
    return true;
}

void Install() noexcept {
    if (!Config.enabled) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("ShadowWarriorAI: disabled in the config file; nothing installed.");
        return;
    }
    if (!Config.cooldowns && !Config.weaponTypes) {
        State.store(InstallState::NothingEnabled, std::memory_order_release);
        Context->LogInfo("ShadowWarriorAI: cooldowns and weapon_types are both off; nothing installed.");
        return;
    }
    if (!VerifyImage()) {
        State.store(InstallState::UnsupportedBuild, std::memory_order_release);
        Context->LogError("ShadowWarriorAI: nothing installed; the game runs stock.");
        return;
    }

    // Resolved before any hook goes live: every hook calls them.
    SkillsRecord = At<SkillsRecordFn>(SkillsRecordRva);
    Evaluate     = At<EvaluateFn>(EvaluateRva);
    SkillById    = At<SkillByIdFn>(SkillByIdRva);
    SkillLevel   = At<SkillLevelFn>(SkillLevelRva);
    WeaponCheck  = At<WeaponCheckFn>(WeaponCheckRva);
    Idle         = At<IdleFn>(IdleRva);

    CheckHookInstalled = Context->InstallInlineHook(CheckUseSkillRva, CheckUseSkillEntry,
                             ByteCount(CheckUseSkillEntry), &HookCheckUseSkill, &OriginalCheckUseSkill)
                      && OriginalCheckUseSkill != nullptr;
    if (!CheckHookInstalled) Context->LogError("ShadowWarriorAI: the skill check at 0x5CF420 could not be hooked.");

    UseHookInstalled = Context->InstallInlineHook(UseSkillRva, UseSkillEntry, ByteCount(UseSkillEntry),
                           &HookUseSkill, &OriginalUseSkill)
                    && OriginalUseSkill != nullptr;
    if (!UseHookInstalled) Context->LogError("ShadowWarriorAI: AITACTICS_UseSkill at 0x4A7BC0 could not be hooked.");

    if (Config.cooldowns) {
        RelayInstalled = InstallRelay();
        if (!RelayInstalled) Context->LogError("ShadowWarriorAI: shadow cooldowns will never start.");
    }

    const bool complete = CheckHookInstalled && UseHookInstalled && (!Config.cooldowns || RelayInstalled);
    const bool any      = CheckHookInstalled || UseHookInstalled || RelayInstalled;
    if (complete) {
        State.store(InstallState::Installed, std::memory_order_release);
        D2RL::LogInfoF(Context, "ShadowWarriorAI: active (cooldowns %s, weapon types %s).",
            Config.cooldowns ? "on" : "off", Config.weaponTypes ? "on" : "off");
    } else if (any) {
        State.store(InstallState::Partial, std::memory_order_release);
    } else {
        State.store(InstallState::InstallFailed, std::memory_order_release);
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateText(InstallState state) noexcept -> const char* {
    switch (state) {
    case InstallState::Installed:        return "active";
    case InstallState::Partial:          return "PARTLY ACTIVE, a part failed, see the log";
    case InstallState::DisabledByConfig: return "off in the config file";
    case InstallState::NothingEnabled:   return "off, cooldowns and weapon_types are both false";
    case InstallState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or a site is taken, see the log";
    case InstallState::InstallFailed:    return "NOT ACTIVE, the hooks failed, see the log";
    case InstallState::NotAttempted:
    default:                             return "NOT ACTIVE, never attempted";
    }
}

// Read live: the AI table is filled by the game, so this is only meaningful
// in a running game.
auto AiRowText() noexcept -> const char* {
    if (Base == 0) return "unknown";
    const auto* row   = reinterpret_cast<const std::uint8_t*>(Base + AiTableRva + ShadowWarriorAiIndex * AiTableRowSize);
    const auto  think = ReadField<std::uint64_t>(row, AiTableThinkOffset);
    if (think == 0) return "not filled yet";
    return think == Base + ShadowWarriorThinkRva ? "yes" : "NO, the AI table differs from the verified build";
}

void WriteRefusals(const D2RL::PluginContext* plugin, Site site, bool installed) noexcept {
    const auto& counters = Refused[static_cast<std::size_t>(site)];
    char line[224]{};
    std::snprintf(line, sizeof(line),
        "%s hook: %s | refused for weapon types %llu, localdelay %llu, globaldelay %llu",
        SiteText(site), installed ? "installed" : "not installed",
        static_cast<unsigned long long>(counters[RuleIndex(Rule::WeaponTypes)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(counters[RuleIndex(Rule::LocalDelay)].load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(counters[RuleIndex(Rule::GlobalDelay)].load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(line);
}

auto ConsoleCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    const std::string_view args = command->args != nullptr
        ? Trim(std::string_view(command->args, command->argsLength)) : std::string_view{};
    if (!args.empty()) {
        if (args == "debug on") {
            DebugLog.store(true, std::memory_order_relaxed);
            plugin->WriteConsoleMessage("Shadow Warrior AI: debug log on (plugin log).");
            return D2RL::ConsoleCommandResult::Handled;
        }
        if (args == "debug off") {
            DebugLog.store(false, std::memory_order_relaxed);
            plugin->WriteConsoleMessage("Shadow Warrior AI: debug log off.");
            return D2RL::ConsoleCommandResult::Handled;
        }
        plugin->WriteConsoleMessage("usage: shadowai | shadowai debug on | shadowai debug off");
        return D2RL::ConsoleCommandResult::InvalidArguments;
    }

    char line[256]{};
    std::snprintf(line, sizeof(line), "Shadow Warrior AI: %s | config %s | cooldowns %s, weapon types %s | debug %s",
        StateText(State.load(std::memory_order_acquire)),
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)",
        Config.cooldowns ? "on" : "off", Config.weaponTypes ? "on" : "off",
        DebugLog.load(std::memory_order_relaxed) ? "on" : "off");
    plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "AI table row %d runs the Shadow Warrior AI: %s",
        static_cast<int>(ShadowWarriorAiIndex), AiRowText());
    plugin->WriteConsoleMessage(line);

    WriteRefusals(plugin, Site::SkillCheck, CheckHookInstalled);
    WriteRefusals(plugin, Site::UseSkill, UseHookInstalled);

    std::snprintf(line, sizeof(line),
        "cooldown start relay: %s%s | shadow casts %llu | started globaldelay %llu, localdelay %llu | held now %llu",
        RelayInstalled ? "installed" : "not installed",
        RelayInstalled ? (UnwindRegistered ? " (unwind data registered)" : " (NO unwind data)") : "",
        static_cast<unsigned long long>(ShadowCasts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(StartedGlobal.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(StartedLocal.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(CooldownsHeld()));
    plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.shadow-warrior-ai",
    .name        = "Shadow Warrior AI",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "Shadow warriors respect skills.txt localdelay, globaldelay and weapon types (itypes/etypes) "
                   "the way players do.",
    // Server: monster AI and the skill do-handler only run on the server.
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base    = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it.
    if (!Context->RegisterConsoleCommand("shadowai", &ConsoleCommand,
            "Shadow Warrior AI status and counters; \"shadowai debug on|off\" logs every decision.")) {
        Context->LogWarn("ShadowWarriorAI: the console command was refused.");
    }

    ReadConfiguration();
    Install();
    return true;
}

// The loader removes the hooks and restores the patched bytes when the plugin
// unloads; the relay page is left in place, as nothing can reach it any more.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}

}  // namespace CelestialRayOne::ShadowWarriorAi
