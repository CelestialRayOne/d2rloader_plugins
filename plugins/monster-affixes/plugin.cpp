// Monster affix rework (monumod.txt unique monster mods).
//
// See celestialrayone.monster-affixes.toml for the behaviour write-up.
//
// Since 1.4.0 the plugin no longer writes the affix pointer tables. Every
// change is made in code: inline hooks on the affix dispatchers and spawn
// routines, and one-byte `ret` patches on the vanilla routines it removes.
//
//   1. SKILLS CAST WHEN STRUCK (Fire / Cold / Lightning Enchanted, Teleportation,
//      Stone Skin). SUNITDMG_FinalizeDamage 0x44A9B0 calls the "struck" affix
//      dispatcher 0x498EE0 after the get-hit mode change (0x44AD08) and on hits
//      that do not flinch or are soft hits (0x44AD39). It is hooked: the vanilla
//      dispatch runs first, then the plugin casts the configured skills for the
//      struck monster's affixes, through the native item-skill casters the game
//      uses for "chance to cast when struck" (EventFunc21 0x5837F0). Lightning's
//      vanilla bolt routine 0x4A0020 is patched to return at once when a
//      lightning skill is set.
//
//   2. FIRE / COLD ENCHANTED DEATH EFFECTS: the corpse explosion 0x49F650 and the
//      cold nova 0x49F5B0 are only reachable through their death event; both
//      start with `ret` now.
//
//   3. SPECTRAL HIT: the random-element routine 0x4A11E0 and its missile copy
//      0x4A0F80 start with `ret`. The mode-change dispatcher 0x498F80 (called by
//      D2GAME_ModeChange 0x4475C0 after the new mode is set) is hooked; on A1,
//      A2, SC and S1-S4 the plugin casts one of up to four skills, each with its
//      own localdelay lockout. A skill that is locked is not picked.
//
//   4. CURSED: the "landed a hit" dispatcher 0x4990C0 (called by
//      SUNITDMG_ExecuteEvents 0x44CE80) is replaced by the same loop, except that
//      Cursed is handled by the plugin: a random skill from the pool, cast at the
//      unit that was hit, minions included. Monsters with rank flag 800h still go
//      to whatever routine the affix table holds for Cursed (D2RLoader's own).
//
//   5. SPAWN BONUSES: Extra Strong 0x496180 is replaced, Extra Fast 0x496300 is
//      wrapped (vanilla walk speed, then attack and cast rate), Mana Burn's spawn
//      routine 0x496CE0 starts with `ret` (the drain happens per hit in
//      FinalizeDamage instead, at most once per cooldown for each player).
//
//   6. LOCKOUT: after a cast the affix is locked for the cast skill's localdelay,
//      a calc at skill record +0x238 evaluated through 0x3B5160, or for the
//      configured frame count (Teleportation). Item-style casts never apply
//      localdelay on their own (the do handler 0x43ACB0 only applies it when
//      spending resources).
//
//   7. RESISTANCE: every affix resist bonus lives in one routine, 0x38E430 (spawn
//      entry of affixes 8, 27 and 28, tail-called by the spawn entries of 9, 17,
//      18, 23 and 25). It is replaced: only Magic Resistant does anything, with
//      the new rules. Ghostly's own physical resist (0x4957F2) is untouched.
//
//   8. MAGIC RESISTANT ON MINIONS: SpawnMinions 0x49EAD0 copies the owner's
//      xfer affixes into each minion and sets rank flag 10h through 0x38E710 at
//      0x49ED68, but never runs their spawn entries. The plugin hooks 0x38E710
//      and, for that one call site (return address 0x49ED6D), runs Magic
//      Resistant on the new minion. A minion that went inactive is rebuilt by
//      0x499210, which does run the spawn entries; the resist routine accepts
//      minions there too.
//
//   9. AURA ENCHANTED: with aura_enchanted_selection = "3.1" in d2rloader.toml,
//      D2RCore!ApplyAuraEnchanted runs the game's own routine 0x495CD0 (unit,
//      mod, flag). Once any aura key is in the TOML that routine is replaced
//      by the same steps, with the TOML's skills:
//        - monstats row 704 (Uber Mephisto): skill 123 at level 20 and nothing
//          else (0x495D3E);
//        - superuniques row 37 (Lord De Seis): record 5 of the main list
//          (0x495DCF);
//        - every other monster rolls the main list on its name seed (0x38E370):
//          state (seed, 666), one step seed * 6AC690C5h + 666, the low 32 bits
//          modulo the total weight of the records at or under its level;
//        - the skill goes in at (mlvl + offset) * multiplier / divisor, kept
//          in 1..99 (0x433910, mode 1) and becomes the right-hand skill
//          (0x438A70), which starts the aura;
//        - a herald (rank flag 200h) rolls the herald list the same way and
//          gets that skill as stat 151 with the skill as layer, in its stat
//          list flagged 10000h (0x2F5810, else 0x2F7300 and 0x2F2CB0; the write
//          is 0x2F7C00).
//      The lists are 24-byte records (min level, weight, level offset,
//      multiplier, divisor, skill): main at 0x1D1DE70 (7), herald at 0x1D1DF20
//      (4). A list the TOML does not set is read from the game's table at roll
//      time, so a static patch on it still counts. The lists in use are
//      published as the "aura-enchanted" service for Monster Display.
//
//  10. HIGH LEVEL MONSTERS: past high_level_start, every monster level (stat
//      12) raises Extra Strong's two bonuses and Magic Resistant's +50 by a
//      percent of their own value and shortens the Mana Burn cooldown by a
//      percent. Extra Strong and Magic Resistant read the level at spawn,
//      Mana Burn reads the burning monster's level at the hit.
//
//  11. EXTRA FAST FRAMES: at spawn an Extra Fast monster past
//      high_level_start gets fast_frames_percent_per_level percent per level
//      (at most 100) in the itemstatcost.txt stat fast_frames_stat, which is
//      sent to clients (Send Other). UNITS_UpdateAnimRateAndVelocity 0x350B40
//      ends every path by handing the unit to 0x980E0 (the call at 0x350C87,
//      after the 0x350C73 call frenzy-fpa redirects). 0x980E0 is hooked and
//      acts only on the call that returns to 0x350C8C. For a monster with the
//      stat in GH, A1, A2, BL, SC, S1-S4 or SQ the action gets that percent
//      fewer frames, never fewer than fast_frames_minimum: the rate word at
//      unit+68h (and the sequence rate at +54h when it holds the same value)
//      becomes the slowest rate that reaches that many frames. Frames are
//      ceil(256 * frames per direction / rate) - 1, frames per direction from
//      the animation record (unit+70h, +8). SQ uses the sequence model
//      (frenzy-fpa: length unit+48h, ceil(length / rate) frames) and keeps
//      every hit of the sequence. 0x350B40 is shared code, so server units
//      (timing, hits) and client units (animation) follow the same rule from
//      the same stat; that is why the plugin is Shared.
//
// Built against D2R 3.3 in the D2RLoader 1.3.1 process image (base 0x140000000),
// PluginSDK 0.3.0. Every hook site and patch site is checked byte for byte before
// anything is written; nothing is installed for a feature whose check fails.

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Hook sites
// ---------------------------------------------------------------------------

// Affix resist routine (unit, affixId, unique). push rbx / push rdi /
// sub rsp 38h / mov ebx,edx / mov rdi,rcx: twelve bytes, no relative operands.
constexpr std::uint64_t ResistBonusRva = 0x0038E430;
constexpr std::uint8_t  ResistBonusPrologue[] { 0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };

// Monster rank flag setter (unit, flag, set). mov [rsp+10h],rbx /
// mov [rsp+18h],rsi / push rdi / sub rsp 20h: fifteen bytes.
constexpr std::uint64_t SetRankFlagRva = 0x0038E710;
constexpr std::uint8_t  SetRankFlagPrologue[] {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20 };

// SpawnMinions 0x49EAD0, end of the per-minion loop: lea edx,[rdi+10h] /
// mov r8b,1 / mov rcx,r15 / call 0x38E710. The call returns to 0x49ED6D.
constexpr std::uint64_t MinionFlagSiteRva   = 0x0049ED5F;
constexpr std::uint64_t MinionFlagReturnRva = 0x0049ED6D;
constexpr std::uint8_t  MinionFlagSiteBytes[] {
	0x8D, 0x57, 0x10, 0x41, 0xB0, 0x01, 0x49, 0x8B, 0xCF, 0xE8, 0xA3, 0xF9, 0xEE, 0xFF };

// SUNITDMG_FinalizeDamage (game, attacker, defender, damage). push rbx, rbp,
// rdi, r14 / sub rsp 88h: thirteen bytes.
constexpr std::uint64_t FinalizeDamageRva = 0x0044A9B0;
constexpr std::uint8_t  FinalizeDamagePrologue[] { 0x40, 0x53, 0x55, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 };

// SUNITDMG_ExecuteEvents (game, attacker, defender, bMissile, damage). Six
// register pushes: nine bytes.
constexpr std::uint64_t ExecuteEventsRva = 0x0044CE80;
constexpr std::uint8_t  ExecuteEventsPrologue[] { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57 };

// The three affix dispatchers the plugin hooks, all (game, unit) and all with
// the same prologue: mov [rsp+20h],rbp / push rsi / push rdi / push r14 /
// sub rsp 20h, thirteen bytes.
//   0x498F80 every mode change (slot 1), from D2GAME_ModeChange
//   0x4990C0 the monster landed a hit (slot 3), from ExecuteEvents
//   0x498EE0 the monster was struck (slot 4), from FinalizeDamage
constexpr std::uint64_t ModeDispatchRva   = 0x00498F80;
constexpr std::uint64_t LandedDispatchRva = 0x004990C0;
constexpr std::uint64_t StruckDispatchRva = 0x00498EE0;
constexpr std::uint8_t  DispatchPrologue[] { 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };

// Spawn routines. Extra Strong: push rbp / push rsi / sub rsp 38h /
// mov ebp,r8d / mov rsi,rcx, thirteen bytes. Extra Fast: push rdi /
// sub rsp 20h / mov rdi,rcx, nine bytes.
constexpr std::uint64_t StrongSpawnRva = 0x00496180;
constexpr std::uint8_t  StrongSpawnPrologue[] { 0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x41, 0x8B, 0xE8, 0x48, 0x8B, 0xF1 };
constexpr std::uint64_t FastSpawnRva   = 0x00496300;
constexpr std::uint8_t  FastSpawnPrologue[] { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9 };

// Vanilla routines that start with `ret` once patched. Each one is reachable
// only for its own affix (checked: no other code calls them).
struct ReturnPatch {
	std::uint64_t       rva;
	const std::uint8_t* expected;
	const std::uint8_t* replacement;
	std::uint32_t       size;
	const char*         name;
};
constexpr std::uint8_t RetOverPushRex[]      { 0x40, 0x55 };              // push rbp (REX)
constexpr std::uint8_t RetOverPushRexRbx[]   { 0x40, 0x53 };              // push rbx (REX)
constexpr std::uint8_t RetOverTestR9[]       { 0x45, 0x85, 0xC9 };        // test r9d,r9d
constexpr std::uint8_t RetOverStoreRbx[]     { 0x48, 0x89, 0x5C, 0x24, 0x08 };  // mov [rsp+8],rbx
constexpr std::uint8_t Ret2[] { 0xC3, 0x90 };
constexpr std::uint8_t Ret3[] { 0xC3, 0x90, 0x90 };
constexpr std::uint8_t Ret5[] { 0xC3, 0x90, 0x90, 0x90, 0x90 };

constexpr ReturnPatch FireDeathPatch     { 0x0049F650, RetOverPushRex, Ret2, 2, "Fire Enchanted corpse explosion 0x49F650" };
constexpr ReturnPatch ColdDeathPatch     { 0x0049F5B0, RetOverTestR9, Ret3, 3, "Cold Enchanted death nova 0x49F5B0" };
constexpr ReturnPatch LightningBoltPatch { 0x004A0020, RetOverTestR9, Ret3, 3, "Lightning Enchanted charged bolts 0x4A0020" };
constexpr ReturnPatch SpectralElementPatch { 0x004A11E0, RetOverStoreRbx, Ret5, 5, "Spectral Hit random element 0x4A11E0" };
constexpr ReturnPatch SpectralMissilePatch { 0x004A0F80, RetOverStoreRbx, Ret5, 5, "Spectral Hit missile element 0x4A0F80" };
constexpr ReturnPatch ManaBurnSpawnPatch { 0x00496CE0, RetOverPushRexRbx, Ret2, 2, "Mana Burn spawn routine 0x496CE0" };

// The end of UNITS_UpdateAnimRateAndVelocity 0x350B40 (unit in rsi), past the
// 0x350C73 call frenzy-fpa redirects: lea rdx,[rbp+18h] / mov [rbp+18h],rsi /
// lea rcx,[rax+168h] / call 0x980E0. 0x980E0 gets the updated unit by address
// in rdx. Its entry, push r12 / push r15 / sub rsp,0A8h: eleven bytes, no
// relative operands. Only the call returning to 0x350C8C is acted on.
constexpr std::uint64_t RateNotifyRva       = 0x000980E0;
constexpr std::uint8_t  RateNotifyPrologue[] { 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00 };
constexpr std::uint64_t RateNotifyCallRva   = 0x00350C78;
constexpr std::uint64_t RateNotifyReturnRva = 0x00350C8C;
constexpr std::uint8_t  RateNotifyCallBytes[] {
	0x48, 0x8D, 0x55, 0x18, 0x48, 0x89, 0x75, 0x18, 0x48, 0x8D, 0x88, 0x68, 0x01, 0x00, 0x00, 0xE8, 0x54, 0x74, 0xD4, 0xFF };

// Affix callback table, read only: 45 rows (monumod.txt id) x 6 pointers.
// The replacement landed-hit dispatcher reads slot 3 from it.
constexpr std::uint64_t AffixTableRva = 0x02396210;
constexpr std::uint32_t AffixSlots    = 6;
constexpr std::uint32_t SlotLandedHit = 3;

// The game's Aura Enchanted routine (unit, mod, flag), reached from
// D2RCore!ApplyAuraEnchanted at aura_enchanted_selection "3.1". push rbp /
// push rbx / push rdi / mov rbp,rsp / sub rsp,70h / mov ebx,r8d / mov rdi,rcx:
// seventeen bytes, no relative operands.
constexpr std::uint64_t AuraEnchantedRva = 0x00495CD0;
constexpr std::uint8_t  AuraEnchantedPrologue[] {
	0x40, 0x55, 0x53, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70, 0x41, 0x8B, 0xD8, 0x48, 0x8B, 0xF9 };

// Its body, checked before it is replaced: every step the replacement repeats.
// 0x495D3E  row 704: skill 123 at level 20, nothing else
constexpr std::uint8_t AuraUberBody[] {
	0xB9, 0xC0, 0x02, 0x00, 0x00, 0x66, 0x3B, 0xC1, 0x48, 0x8B, 0xCF, 0x75, 0x32, 0xBA, 0x7B, 0x00,
	0x00, 0x00, 0x44, 0x8D, 0x4A, 0x86, 0x44, 0x8D, 0x42, 0x99 };
// 0x495D97  monster level (stat 12), at least 1
constexpr std::uint8_t AuraLevelBody[] {
	0x41, 0x8D, 0x50, 0x0C, 0xE8, 0x80, 0xF2, 0xE5, 0xFF, 0x85, 0xC0, 0x8B, 0xF0, 0x41, 0xBE, 0x01,
	0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0x41, 0x0F, 0x4E, 0xF6 };
// 0x495DB1  name seed and superunique row
constexpr std::uint8_t AuraSeedBody[] {
	0xE8, 0xBA, 0x85, 0xEF, 0xFF, 0x48, 0x8B, 0xCF, 0x44, 0x0F, 0xB7, 0xF8, 0xE8, 0x0E, 0x86, 0xEF,
	0xFF };
// 0x495DCF  superunique 37 takes record 5
constexpr std::uint8_t AuraDeSeisBody[] {
	0x66, 0x83, 0xF8, 0x25, 0x75, 0x08, 0x8D, 0x4B, 0x05 };
// 0x495DDD  roll state (seed, 666)
constexpr std::uint8_t AuraStateBody[] {
	0x48, 0x8D, 0x4D, 0x20, 0xE8, 0x1A, 0x13, 0xED, 0xFF, 0x41, 0x8B, 0xD7, 0x48, 0x8D, 0x4D, 0x20,
	0xE8, 0x1E, 0x13, 0xED, 0xFF };
// 0x495E00  main list at 1D1DE70: min level, weight
constexpr std::uint8_t AuraMainReadBody[] {
	0x48, 0x8D, 0x04, 0x49, 0x41, 0x39, 0xB4, 0xC4, 0x70, 0xDE, 0xD1, 0x01, 0x7F, 0x0F, 0x41, 0x8B,
	0x84, 0xC4, 0x74, 0xDE, 0xD1, 0x01 };
// 0x495E1D  main list: 7 records
constexpr std::uint8_t AuraMainCountBody[] {
	0x48, 0xFF, 0xC1, 0x48, 0x83, 0xF9, 0x07, 0x72, 0xDA };
// 0x495E3B  roll step 6AC690C5h
constexpr std::uint8_t AuraStepBody[] {
	0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6, 0x6A };
// 0x495EF2  main list: the picked record
constexpr std::uint8_t AuraRecordBody[] {
	0x41, 0x0F, 0x10, 0x94, 0xCC, 0x70, 0xDE, 0xD1, 0x01, 0xF2, 0x41, 0x0F, 0x10, 0x84, 0xCC, 0x80,
	0xDE, 0xD1, 0x01 };
// 0x495F46  skill added, mode 1
constexpr std::uint8_t AuraAddSkillBody[] {
	0x45, 0x8B, 0xCE, 0x48, 0xC1, 0xEA, 0x20, 0x48, 0x8B, 0xCF, 0xE8, 0xBB, 0xD9, 0xF9, 0xFF };
// 0x495F59  skill made the right-hand skill
constexpr std::uint8_t AuraSelectBody[] {
	0x41, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x07, 0x2B, 0xFA, 0xFF };
// 0x495F69  herald rank flag 200h
constexpr std::uint8_t AuraHeraldFlagBody[] {
	0xBA, 0x00, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xFA, 0x88, 0xEF, 0xFF, 0x84, 0xC0, 0x0F,
	0x84, 0xD2, 0x01, 0x00, 0x00 };
// 0x495FA0  herald list at 1D1DF20: min level, weight
constexpr std::uint8_t AuraHeraldReadBody[] {
	0x48, 0x8D, 0x04, 0x49, 0x41, 0x39, 0xB4, 0xC4, 0x20, 0xDF, 0xD1, 0x01, 0x7F, 0x0F, 0x41, 0x8B,
	0x84, 0xC4, 0x24, 0xDF, 0xD1, 0x01 };
// 0x495FBD  herald list: 4 records
constexpr std::uint8_t AuraHeraldCountBody[] {
	0x48, 0xFF, 0xC1, 0x48, 0x83, 0xF9, 0x04, 0x72, 0xDA };
// 0x4960D2  herald stat list flagged 10000h
constexpr std::uint8_t AuraListFindBody[] {
	0xBA, 0x00, 0x00, 0x01, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0x31, 0xF7, 0xE5, 0xFF };
// 0x4960E7  new stat list (0, 0, type, id)
constexpr std::uint8_t AuraListNewBody[] {
	0x41, 0xB8, 0xF2, 0x05, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x9C, 0x7E, 0x88, 0x01, 0x48, 0x8B, 0xCF,
	0xE8, 0x34, 0x42, 0xEB, 0xFF, 0x48, 0x8B, 0xCF, 0x8B, 0xD8, 0xE8, 0xCA, 0x58, 0xEB, 0xFF, 0x44,
	0x8B, 0xCB, 0x44, 0x8B, 0xC0, 0x33, 0xD2, 0x33, 0xC9, 0xE8, 0xEB, 0x11, 0xE6, 0xFF };
// 0x49611D  stat list attached, update 1
constexpr std::uint8_t AuraListAttachBody[] {
	0x45, 0x8B, 0xC6, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCF, 0xE8, 0x85, 0xCB, 0xE5, 0xFF };
// 0x49612B  stat 151, the skill word as layer
constexpr std::uint8_t AuraStatWriteBody[] {
	0x48, 0x8B, 0xCF, 0xE8, 0xAD, 0x3F, 0xEB, 0xFF, 0x0F, 0xB7, 0x4D, 0xDC, 0x44, 0x8B, 0xCE, 0x90,
	0x89, 0x4C, 0x24, 0x20, 0x41, 0xB8, 0x97, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0xC8, 0x48, 0x8B, 0xD3,
	0xE8, 0xB0, 0x1A, 0xE6, 0xFF };

// ---------------------------------------------------------------------------
// Game functions
// ---------------------------------------------------------------------------

constexpr std::uint64_t GetUnitStatRva     = 0x002F5020;  // loader thunk, (unit, stat, layer)
constexpr std::uint64_t SetUnitStatRva     = 0x002F7D10;  // loader thunk, (unit, stat, value, layer)
constexpr std::uint64_t AddUnitStatRva     = 0x002F34F0;  // loader thunk, (unit, stat, value, layer)
constexpr std::uint64_t UnitTypeRva        = 0x0034B9D0;  // (unit)
constexpr std::uint64_t UnitIdRva          = 0x0034A330;  // (unit, file, line)
constexpr std::uint64_t UnitModeRva        = 0x0034AB60;  // (unit), mode at unit+0Ch
constexpr std::uint64_t UnitSeedRva        = 0x0034A1E0;  // (unit) -> seed pair at unit+28h
constexpr std::uint64_t UnitGameRva        = 0x0048FF00;  // (unit) -> game at unit+D8h
constexpr std::uint64_t SetUnitFlagRva     = 0x0034E140;  // (unit, flag, set)
constexpr std::uint64_t PathPointXRva      = 0x0034B8F0;  // (unit)
constexpr std::uint64_t PathPointYRva      = 0x0034B940;  // (unit)
constexpr std::uint64_t TargetUnitRva      = 0x0048FE20;  // (game, unit)
constexpr std::uint64_t MissileOwnerRva    = 0x00490300;  // (game, missile), may return null
constexpr std::uint64_t AffixListRva       = 0x0038E310;  // (unit) -> 9 affix bytes
constexpr std::uint64_t HasRankFlagRva     = 0x0038E870;  // (unit, mask) -> uint8
constexpr std::uint64_t AffixConstantRva   = 0x00498E10;  // (dataContext, row) -> monumod.txt constants
constexpr std::uint64_t SkillsRecordRva    = 0x00097790;  // (dataContext, skillId)
constexpr std::uint64_t EvaluateRva        = 0x003B5160;  // (dataContext, unit, calc, skillId, level)
constexpr std::uint64_t CastOnTargetRva    = 0x005896E0;  // (caster, skillId, level, target, flag)
constexpr std::uint64_t CastAtPositionRva  = 0x00589820;  // (caster, skillId, level, x, y, flag)

// Entry witnesses for the helpers whose bytes are stable. The two casters are
// not witnessed: other plugins (whirlwind-follow-cursor) hook their entries.
constexpr std::uint8_t LoaderThunkBytes[] { 0xFF, 0x25 };
constexpr std::uint8_t UnitTypeBytes[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };
constexpr std::uint8_t UnitIdBytes[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30 };
constexpr std::uint8_t MissileOwnerBytes[] {
	0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1,
	0x48, 0x85, 0xD2, 0x75, 0x20 };
constexpr std::uint8_t SkillsRecordBytes[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2 };
constexpr std::uint8_t EvaluateBytes[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
	0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00 };

using GetUnitStatFn    = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer) noexcept;
using SetUnitStatFn    = std::uint8_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer) noexcept;
using AddUnitStatFn    = std::uint8_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer) noexcept;
using UnitTypeFn       = std::uint32_t(__fastcall*)(void* unit) noexcept;
using UnitIdFn         = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line) noexcept;
using UnitModeFn       = std::uint32_t(__fastcall*)(void* unit) noexcept;
using UnitSeedFn       = std::uint32_t*(__fastcall*)(void* unit) noexcept;
using UnitGameFn       = void*(__fastcall*)(void* unit) noexcept;
using SetUnitFlagFn    = void(__fastcall*)(void* unit, std::uint32_t flag, std::int32_t set) noexcept;
using PathPointFn      = std::int32_t(__fastcall*)(void* unit) noexcept;
using TargetUnitFn     = void*(__fastcall*)(void* game, void* unit) noexcept;
using MissileOwnerFn   = void*(__fastcall*)(void* game, void* missile) noexcept;
using AffixListFn      = const std::uint8_t*(__fastcall*)(void* unit) noexcept;
using HasRankFlagFn    = std::uint8_t(__fastcall*)(void* unit, std::uint32_t mask) noexcept;
using AffixConstantFn  = std::int32_t(__fastcall*)(std::uint8_t context, std::uint32_t row) noexcept;
using SkillsRecordFn   = const std::uint8_t*(__fastcall*)(std::uint8_t context, std::int32_t skillId) noexcept;
using EvaluateFn       = std::int32_t(__fastcall*)(std::uint8_t context, void* unit, std::uint32_t calc,
                                                   std::int32_t skillId, std::int32_t level) noexcept;
using CastOnTargetFn   = std::int32_t(__fastcall*)(void* caster, std::int32_t skillId, std::int32_t level,
                                                   void* target, std::int32_t flag) noexcept;
using CastAtPositionFn = std::int32_t(__fastcall*)(void* caster, std::int32_t skillId, std::int32_t level,
                                                   std::int32_t x, std::int32_t y, std::int32_t flag) noexcept;
using FinalizeDamageFn = void(__fastcall*)(void* game, void* attacker, void* defender, void* damage) noexcept;
using ExecuteEventsFn  = void(__fastcall*)(void* game, void* attacker, void* defender, std::int32_t missile,
                                           void* damage) noexcept;
using SetRankFlagFn    = std::uint64_t(__fastcall*)(void* unit, std::uint32_t flag, std::uint32_t set) noexcept;
using DispatchFn       = std::uint64_t(__fastcall*)(void* game, void* unit) noexcept;
using AffixCallbackFn  = std::uint64_t(__fastcall*)(void* game, void* unit, std::uint64_t affix, std::uint64_t unique) noexcept;
using SpawnFn          = void(__fastcall*)(void* unit, std::int32_t affix, std::int32_t unique) noexcept;
using RateNotifyFn     = std::uint64_t(__fastcall*)(void* container, void* unitSlot, void* r8, void* r9) noexcept;

GetUnitStatFn    GetUnitStat    = nullptr;
SetUnitStatFn    SetUnitStat    = nullptr;
AddUnitStatFn    AddUnitStat    = nullptr;
UnitTypeFn       UnitType       = nullptr;
UnitIdFn         UnitId         = nullptr;
UnitModeFn       UnitMode       = nullptr;
UnitSeedFn       UnitSeed       = nullptr;
UnitGameFn       UnitGame       = nullptr;
SetUnitFlagFn    SetUnitFlag    = nullptr;
PathPointFn      PathPointX     = nullptr;
PathPointFn      PathPointY     = nullptr;
TargetUnitFn     TargetUnit     = nullptr;
MissileOwnerFn   MissileOwner   = nullptr;
AffixListFn      AffixList      = nullptr;
HasRankFlagFn    HasRankFlag    = nullptr;
AffixConstantFn  AffixConstant  = nullptr;
SkillsRecordFn   SkillsRecord   = nullptr;
EvaluateFn       Evaluate       = nullptr;
CastOnTargetFn   CastOnTarget   = nullptr;
CastAtPositionFn CastAtPosition = nullptr;

FinalizeDamageFn OriginalFinalizeDamage = nullptr;
ExecuteEventsFn  OriginalExecuteEvents  = nullptr;
SetRankFlagFn    OriginalSetRankFlag    = nullptr;
DispatchFn       OriginalModeDispatch   = nullptr;
DispatchFn       OriginalStruckDispatch = nullptr;
SpawnFn          OriginalFastSpawn      = nullptr;
RateNotifyFn     OriginalRateNotify     = nullptr;

// ---------------------------------------------------------------------------
// Aura Enchanted natives and lists
// ---------------------------------------------------------------------------

constexpr std::uint64_t ClassIdRva        = 0x00349860;  // (unit, file, line) -> unit+4, the monstats.txt row
constexpr std::uint64_t NameSeedRva       = 0x0038E370;  // (unit) -> word at MonsterData+18h
constexpr std::uint64_t SuperUniqueRva    = 0x0038E3D0;  // (unit) -> word at MonsterData+2Ah, FFFFh for none
constexpr std::uint64_t SetSkillRva       = 0x00433910;  // (unit, skill, level, mode)
constexpr std::uint64_t SelectSkillRva    = 0x00438A70;  // (unit, left, skill, owner)
constexpr std::uint64_t FindStatListRva   = 0x002F5810;  // (unit, flags)
constexpr std::uint64_t AllocStatListRva  = 0x002F7300;  // (flags, end frame, owner type, owner id)
constexpr std::uint64_t AttachStatListRva = 0x002F2CB0;  // loader thunk, (unit, list, update)
constexpr std::uint64_t DataContextRva    = 0x0034A0E0;  // (unit) -> byte at unit+1BDh
constexpr std::uint64_t SetListStatRva    = 0x002F7C00;  // loader thunk, (context, list, stat, value, layer)

constexpr std::uint8_t ClassIdBytes[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
	0x24, 0x30, 0xE8, 0x49, 0xCB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF,
	0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x04 };
constexpr std::uint8_t NameSeedBytes[] {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x4D,
	0xD6, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19 };
constexpr std::uint8_t SuperUniqueBytes[] {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0xED,
	0xD5, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19 };
constexpr std::uint8_t SetSkillBytes[] {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
	0x50, 0x45, 0x8B, 0xE1, 0x4C, 0x63, 0xF2, 0x45, 0x8B, 0xF8, 0x48, 0x8B, 0xE9 };
constexpr std::uint8_t SelectSkillBytes[] {
	0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02, 0x00, 0x00, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55,
	0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40 };
constexpr std::uint8_t FindStatListBytes[] {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48,
	0x85, 0xC9, 0x75, 0x13 };
constexpr std::uint8_t AllocStatListBytes[] {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x89,
	0x4C, 0x24, 0x08, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
constexpr std::uint8_t DataContextBytes[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
	0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83,
	0xC4, 0x28, 0xC3, 0x0F, 0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00 };

using AuraEnchantedFn  = void(__fastcall*)(void* unit, std::uint32_t mod, std::uint32_t flag) noexcept;
using ClassIdFn        = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line) noexcept;
using UnitWordFn       = std::uint16_t(__fastcall*)(void* unit) noexcept;
using SetSkillFn       = void*(__fastcall*)(void* unit, std::int32_t skillId, std::int32_t level, std::int32_t mode) noexcept;
using SelectSkillFn    = void(__fastcall*)(void* unit, std::int32_t left, std::int32_t skillId, std::int32_t owner) noexcept;
using FindStatListFn   = void*(__fastcall*)(void* unit, std::uint32_t flags) noexcept;
using AllocStatListFn  = void*(__fastcall*)(std::uint32_t flags, std::int32_t endFrame, std::uint32_t ownerType,
                                            std::uint32_t ownerId) noexcept;
using AttachStatListFn = void(__fastcall*)(void* unit, void* list, std::int32_t update) noexcept;
using DataContextFn    = std::uint8_t(__fastcall*)(void* unit) noexcept;
using SetListStatFn    = void(__fastcall*)(std::uint8_t context, void* list, std::int32_t stat, std::int32_t value,
                                           std::int32_t layer) noexcept;

ClassIdFn        ClassId        = nullptr;
UnitWordFn       NameSeed       = nullptr;
UnitWordFn       SuperUnique    = nullptr;
SetSkillFn       SetSkill       = nullptr;
SelectSkillFn    SelectSkill    = nullptr;
FindStatListFn   FindStatList   = nullptr;
AllocStatListFn  AllocStatList  = nullptr;
AttachStatListFn AttachStatList = nullptr;
DataContextFn    DataContext    = nullptr;
SetListStatFn    SetListStat    = nullptr;
AuraEnchantedFn  OriginalAuraEnchanted = nullptr;

// One record of the game's lists, 24 bytes, the skill at +14h.
struct AuraRecord {
	std::int32_t minLevel;
	std::int32_t weight;
	std::int32_t levelOffset;
	std::int32_t multiplier;
	std::int32_t divisor;
	std::int32_t skill;
};
static_assert(sizeof(AuraRecord) == 24 && offsetof(AuraRecord, skill) == 0x14);

constexpr std::size_t MaxAuraEntries = 64;

struct AuraList {
	std::array<AuraRecord, MaxAuraEntries> records {};
	std::uint32_t                          count      = 0;
	bool                                   configured = false;  // false: the game's own table
};

constexpr std::uint64_t MainAuraTableRva      = 0x01D1DE70;
constexpr std::uint64_t HeraldAuraTableRva    = 0x01D1DF20;
constexpr std::uint32_t MainAuraRecords       = 7;
constexpr std::uint32_t HeraldAuraRecords     = 4;
constexpr std::uint64_t AuraFlagMaskRva       = 0x01D996E4;  // tested against skills.txt record byte +24h (0x438A70)
constexpr std::size_t   SkillAuraFlagOffset   = 0x24;
constexpr std::uint32_t UberMephistoClass     = 704;
constexpr std::uint16_t LordDeSeisSuperUnique = 37;
constexpr std::int32_t  StockUberAuraSkill    = 123;
constexpr std::int32_t  StockUberAuraLevel    = 20;
constexpr std::uint32_t StockLordDeSeisRecord = 5;
constexpr std::uint32_t HeraldRank            = 0x0200;
constexpr std::uint32_t HeraldStatListFlags   = 0x10000;
constexpr std::int32_t  StatItemAura          = 151;
constexpr std::int32_t  DefaultAuraDivisor    = 6;
constexpr std::uint32_t AuraSeedMultiplier    = 0x6AC690C5U;
constexpr std::uint32_t AuraSeedHigh          = 666;

// ---------------------------------------------------------------------------
// Layouts and ids
// ---------------------------------------------------------------------------

constexpr std::size_t UnitMonsterDataOffset  = 0x10;   // Unit+10h -> MonsterData
constexpr std::size_t MonsterRankFlagsOffset = 0x1A;   // MonsterData+1Ah, uint16
constexpr std::size_t GameDifficultyOffset   = 0x104;  // Game+104h, uint8
constexpr std::size_t GameDataContextOffset  = 0x106;  // Game+106h, uint8
constexpr std::size_t GameFrameOffset        = 0x170;  // Game+170h, int32
constexpr std::size_t DamageResultFlags      = 0x04;   // D2Damage+4, result flags
constexpr std::size_t SkillLocalDelayCalc    = 0x238;  // skills.txt localdelay calc, +568
constexpr std::size_t AffixListLength        = 9;

// Unit animation fields (frenzy-fpa section 1, UNITS_UpdateAnimRateAndVelocity
// 0x350B40).
constexpr std::size_t UnitModeOffset        = 0x0C;
constexpr std::size_t UnitSequenceRows      = 0x40;   // 6-byte rows, event at +5
constexpr std::size_t UnitSequenceLength    = 0x48;   // steps << 8
constexpr std::size_t UnitSequencePosition  = 0x4C;
constexpr std::size_t UnitSequenceRate      = 0x54;
constexpr std::size_t UnitSequenceCountdown = 0x64;
constexpr std::size_t UnitAnimRate          = 0x68;   // int16
constexpr std::size_t UnitAnimData          = 0x70;   // animation record
constexpr std::size_t AnimDataFrames        = 0x08;   // frames per direction

constexpr std::uint32_t UnitTypePlayer   = 0;
constexpr std::uint32_t UnitTypeMonster  = 1;
constexpr std::uint32_t UnitTypeMissile  = 3;
constexpr std::uint32_t UnitTypeItem     = 4;
constexpr std::uint16_t ExcludedRanks    = 0x0410;      // minion 10h and 400h, as the vanilla routines test
constexpr std::uint16_t MinionRank       = 0x0010;
constexpr std::uint16_t UniqueRank       = 0x0008;
constexpr std::uint16_t Rank400          = 0x0400;      // vanilla's other exclusion, kept
constexpr std::uint16_t MagicResistExcludedRanks = Rank400;
constexpr std::uint16_t CursedSpecialRank = 0x0800;     // D2RLoader's Cursed routine treats these monsters apart
constexpr std::uint32_t ItemCastFlag     = 0x10000000;  // set around the cast exactly as EventFunc21 does
constexpr std::uint16_t ResultSuccessfulHit = 0x0001;

constexpr std::int32_t AffixExtraStrong        = 5;
constexpr std::int32_t AffixExtraFast          = 6;
constexpr std::int32_t AffixCursed             = 7;
constexpr std::int32_t AffixMagicResistant     = 8;
constexpr std::int32_t AffixFireEnchanted      = 9;
constexpr std::int32_t AffixLightningEnchanted = 17;
constexpr std::int32_t AffixColdEnchanted      = 18;
constexpr std::int32_t AffixManaBurn           = 25;
constexpr std::int32_t AffixTeleportation      = 26;
constexpr std::int32_t AffixSpectralHit        = 27;
constexpr std::int32_t AffixStoneSkin          = 28;

constexpr std::int32_t StatMana          = 8;
constexpr std::int32_t StatMaxMana       = 9;
constexpr std::int32_t StatLevel         = 12;
constexpr std::int32_t StatDamagePercent = 25;
constexpr std::int32_t StatAttackRate    = 68;

// Spell damage: passive_fire/ltng/cold/pois_mastery, passive_mag2_mastery,
// passive_wind_mastery.
constexpr std::array<std::int32_t, 6> SpellMasteryStats { 329, 330, 331, 332, 482, 483 };

// monumod.txt constants row D2RLoader's Cursed routine rolls its proc chance on.
constexpr std::uint32_t CursedChanceRow = 34;
// Amplify Damage, the skill the stock Cursed routine casts. Used when
// cursed_skills is empty.
constexpr std::int32_t  StockCurseSkill = 66;

// Monster animation modes that start an attack or a cast: A1, A2, SC, S1-S4.
constexpr std::uint32_t ModeAttack1 = 4;
constexpr std::uint32_t ModeAttack2 = 5;
constexpr std::uint32_t ModeCast    = 7;
constexpr std::uint32_t ModeSkill1  = 8;
constexpr std::uint32_t ModeSkill4  = 11;
constexpr std::uint32_t ModeGetHit  = 3;
constexpr std::uint32_t ModeBlock   = 6;
constexpr std::uint32_t ModeSequence = 14;

constexpr std::int32_t  NoSkill         = -1;
constexpr std::int32_t  MaxSkillId      = 0xFFFE;
constexpr std::int32_t  UseLocalDelay   = -1;
constexpr std::int32_t  UseMonsterLevel = 0;
constexpr std::uint32_t WholeAffix      = 0xFFFF;   // lockout shared by every skill of the affix
constexpr std::size_t   MaxCursedSkills = 16;

// Magic Resistant: +50 to each resist, capped at 95. What the cap leaves unused
// is converted: one point per two for absorb / dodge-avoid-evade, one per one
// for poison length reduction. A resist already at 95 or more converts all 50.
constexpr std::int32_t MagicResistBoost = 50;
constexpr std::int32_t MagicResistCap   = 95;

struct ResistConversion {
	std::int32_t                resistStat;
	std::array<std::int32_t, 3> outputs;
	std::int32_t                outputCount;
	std::int32_t                pointsPerBonus;
};

constexpr std::array<ResistConversion, 6> MagicResistTable { {
	{ 39, { 142, 0, 0 }, 1, 2 },        // fireresist      -> item_absorbfire_percent
	{ 43, { 148, 0, 0 }, 1, 2 },        // coldresist      -> item_absorbcold_percent
	{ 41, { 144, 0, 0 }, 1, 2 },        // lightresist     -> item_absorblight_percent
	{ 37, { 146, 0, 0 }, 1, 2 },        // magicresist     -> item_absorbmagic_percent
	{ 36, { 338, 339, 340 }, 3, 2 },    // damageresist    -> passive_dodge, passive_avoid, passive_evade
	{ 45, { 110, 0, 0 }, 1, 1 },        // poisonresist    -> item_poisonlengthresist
} };

// ---------------------------------------------------------------------------
// Settings and state
// ---------------------------------------------------------------------------

struct Settings {
	std::int32_t                fireSkill      = NoSkill;
	std::int32_t                coldSkill      = NoSkill;
	std::int32_t                lightningSkill = NoSkill;
	std::int32_t                stoneSkinSkill = NoSkill;
	std::array<std::int32_t, 4> spectralSkills { NoSkill, NoSkill, NoSkill, NoSkill };

	std::int32_t strongDamagePercent      = 150;
	std::int32_t strongSpellDamagePercent = 100;

	std::array<std::int32_t, 3> fastAttackRate { 25, 50, 75 };
	std::array<std::int32_t, 3> fastCastRate { 25, 50, 75 };
	std::int32_t                fastCastRateStat = 105;

	std::array<std::int32_t, MaxCursedSkills> cursedSkills {};
	std::uint32_t                             cursedCount = 0;

	std::int32_t manaBurnPercent        = 10;
	std::int32_t manaBurnCooldownFrames = 25;

	std::int32_t highLevelStart                  = 100;
	std::int32_t strongPercentPerLevel           = 5;
	std::int32_t magicResistPercentPerLevel      = 5;
	std::int32_t manaBurnCooldownPercentPerLevel = 5;
	std::int32_t fastFramesStat                  = -1;
	std::int32_t fastFramesPercentPerLevel       = 5;
	std::int32_t fastFramesMinimum               = 5;

	std::int32_t teleportSkill          = 184;
	std::int32_t teleportChance         = 25;
	std::int32_t teleportCooldownFrames = 25;

	AuraList     auraMain;
	AuraList     auraHerald;
	std::int32_t uberMephistoSkill = NoSkill;  // NoSkill: the game's skill 123
	std::int32_t uberMephistoLevel = 20;
	std::int32_t lordDeSeisSkill   = NoSkill;  // NoSkill: record 5 of the game's main list
	std::int32_t lordDeSeisDivisor = 8;
};

Settings Config {};
bool     ConfigFileRead = false;

std::array<std::int32_t, 4> SpectralPool {};
std::uint32_t               SpectralPoolSize = 0;

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

bool ResistRoutineInstalled = false;
bool MinionMagicResist      = false;
bool StruckCaptureInstalled = false;
bool HitCaptureInstalled    = false;
bool StruckHookInstalled    = false;
bool ModeHookInstalled      = false;
bool LandedHookInstalled    = false;
bool StrongHookInstalled    = false;
bool FastHookInstalled      = false;
bool FireDeathPatched       = false;
bool ColdDeathPatched       = false;
bool LightningBoltsPatched  = false;
bool SpectralElementPatched = false;
bool SpectralMissilePatched = false;
bool ManaBurnPatched        = false;

// What actually runs, decided once at load from the settings and what got
// installed.
bool FireActive      = false;
bool ColdActive      = false;
bool LightningActive = false;
bool StoneSkinActive = false;
bool TeleportActive  = false;
bool SpectralActive  = false;
bool CursedActive    = false;
bool ManaBurnActive  = false;
bool AuraHookInstalled = false;
bool RateHookInstalled = false;
bool FastFramesActive  = false;

std::array<std::int32_t, MaxCursedSkills> CursedPool {};
std::uint32_t                             CursedPoolSize = 0;

struct AffixCounters {
	std::atomic<std::uint64_t> casts { 0 };
	std::atomic<std::uint64_t> lockedOut { 0 };
	std::atomic<std::uint64_t> missingSkill { 0 };
};

AffixCounters FireCounters;
AffixCounters ColdCounters;
AffixCounters LightningCounters;
AffixCounters SpectralCounters;
AffixCounters CursedCounters;
AffixCounters TeleportCounters;
AffixCounters StoneSkinCounters;
std::atomic<std::uint64_t> MagicResistApplied { 0 };
std::atomic<std::uint64_t> MinionMagicResistApplied { 0 };
std::atomic<std::uint64_t> CursedDelegated { 0 };
std::atomic<std::uint64_t> CursedMinionCasts { 0 };
std::atomic<std::uint64_t> SpectralRerolls { 0 };
std::atomic<std::uint64_t> ManaBurnHits { 0 };
std::atomic<std::uint64_t> ManaBurnBlocked { 0 };
std::atomic<std::uint64_t> AuraMainGiven { 0 };
std::atomic<std::uint64_t> AuraHeraldGiven { 0 };
std::atomic<std::uint64_t> AuraFixedGiven { 0 };
std::atomic<std::uint64_t> AuraNoEntry { 0 };
std::atomic<std::uint64_t> AuraBadSkill { 0 };
std::atomic<std::uint64_t> FastFramesGiven { 0 };
std::atomic<std::uint64_t> FastFramesChanged { 0 };
std::atomic<std::uint64_t> FastFramesHeldBack { 0 };

// The struck and landed-hit dispatchers do not receive the other unit;
// FinalizeDamage and ExecuteEvents do. Saved and restored around every call,
// so nested damage keeps its own pair.
thread_local void* HitAttacker = nullptr;
thread_local void* HitDefender = nullptr;

// A skill cast by an affix must not trigger another affix cast synchronously.
thread_local std::int32_t AffixCastDepth = 0;

struct LockoutEntry {
	void*        game;
	std::int32_t setFrame;
	std::int32_t expireFrame;
};

std::mutex                                      LockoutMutex;
std::unordered_map<std::uint64_t, LockoutEntry> Lockouts;
constexpr std::size_t                           LockoutPruneSize = 4096;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
	return reinterpret_cast<T>(Base + rva);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

template <typename T>
auto ReadField(const void* base, std::size_t offset) noexcept -> T {
	T value {};
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void WriteField(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

void LogInfo(const char* message) noexcept {
	if (Context != nullptr) Context->LogInfo(message);
}

void LogWarn(const char* message) noexcept {
	if (Context != nullptr) Context->LogWarn(message);
}

void LogError(const char* message) noexcept {
	if (Context != nullptr) Context->LogError(message);
}

auto RankFlags(void* unit) noexcept -> std::uint16_t {
	const void* monsterData = ReadField<void*>(unit, UnitMonsterDataOffset);
	return monsterData != nullptr ? ReadField<std::uint16_t>(monsterData, MonsterRankFlagsOffset) : 0;
}

auto HasAffix(void* unit, std::int32_t affix) noexcept -> bool {
	const std::uint8_t* list = AffixList(unit);
	if (list == nullptr) return false;
	for (std::size_t i = 0; i < AffixListLength && list[i] != 0; ++i) {
		if (list[i] == affix) return true;
	}
	return false;
}

// Same LCG step as the game's own rolls on the unit seed (EventFunc21).
auto NextRoll(void* unit) noexcept -> std::uint32_t {
	std::uint32_t* seed = UnitSeed(unit);
	if (seed == nullptr) return 0;
	const std::uint64_t next = static_cast<std::uint64_t>(seed[1]) + 0x6AC690C5ULL * static_cast<std::uint64_t>(seed[0]);
	std::memcpy(seed, &next, sizeof(next));
	return static_cast<std::uint32_t>(next);
}

auto IsAttackOrCastMode(std::uint32_t mode) noexcept -> bool {
	return mode == ModeAttack1 || mode == ModeAttack2 || mode == ModeCast
	    || (mode >= ModeSkill1 && mode <= ModeSkill4);
}

auto CountersFor(std::int32_t affix) noexcept -> AffixCounters* {
	switch (affix) {
	case AffixFireEnchanted: return &FireCounters;
	case AffixColdEnchanted: return &ColdCounters;
	case AffixLightningEnchanted: return &LightningCounters;
	case AffixSpectralHit: return &SpectralCounters;
	case AffixCursed: return &CursedCounters;
	case AffixTeleportation: return &TeleportCounters;
	case AffixStoneSkin: return &StoneSkinCounters;
	default: return nullptr;
	}
}

auto GameFrame(void* game) noexcept -> std::int32_t {
	return ReadField<std::int32_t>(game, GameFrameOffset);
}

// ---------------------------------------------------------------------------
// High level monsters
// ---------------------------------------------------------------------------

// Monster levels (stat 12) past high_level_start; 0 at or below it.
auto LevelsAbove(void* unit) noexcept -> std::int32_t {
	if (unit == nullptr) return 0;
	const std::int32_t level = GetUnitStat(unit, StatLevel, 0);
	return level > Config.highLevelStart ? level - Config.highLevelStart : 0;
}

// value * (100 + percentPerLevel * levels) / 100, kept inside 32 bits.
auto RaiseByLevels(std::int32_t value, std::int32_t percentPerLevel, std::int32_t levels) noexcept -> std::int32_t {
	if (levels <= 0 || percentPerLevel <= 0) return value;
	const std::int64_t scaled = static_cast<std::int64_t>(value) * (100 + static_cast<std::int64_t>(percentPerLevel) * levels) / 100;
	if (scaled > std::numeric_limits<std::int32_t>::max()) return std::numeric_limits<std::int32_t>::max();
	if (scaled < std::numeric_limits<std::int32_t>::min()) return std::numeric_limits<std::int32_t>::min();
	return static_cast<std::int32_t>(scaled);
}

// value * (100 - percentPerLevel * levels) / 100, never below 0.
auto LowerByLevels(std::int32_t value, std::int32_t percentPerLevel, std::int32_t levels) noexcept -> std::int32_t {
	if (levels <= 0 || percentPerLevel <= 0) return value;
	const std::int64_t factor = 100 - static_cast<std::int64_t>(percentPerLevel) * levels;
	return factor <= 0 ? 0 : static_cast<std::int32_t>(static_cast<std::int64_t>(value) * factor / 100);
}

// ---------------------------------------------------------------------------
// Lockout, keyed by game, monster GUID, affix and (Spectral Hit) skill.
// Mana Burn is the one exception: its cooldown is keyed by the PLAYER's GUID
// under affix 25, which never keys a monster lockout (Mana Burn casts
// nothing), so player and monster GUIDs cannot meet in one key.
// ---------------------------------------------------------------------------

auto LockoutKey(std::uint32_t guid, std::int32_t affix, std::uint32_t sub) noexcept -> std::uint64_t {
	return (static_cast<std::uint64_t>(guid) << 24) | (static_cast<std::uint64_t>(affix & 0xFF) << 16)
	     | static_cast<std::uint64_t>(sub & 0xFFFF);
}

auto IsLockedOut(void* game, std::uint32_t guid, std::int32_t affix, std::uint32_t sub, std::int32_t frame) noexcept
	-> bool {
	const std::lock_guard<std::mutex> guard(LockoutMutex);
	const auto it = Lockouts.find(LockoutKey(guid, affix, sub));
	if (it == Lockouts.end()) return false;

	const LockoutEntry& entry = it->second;
	// A different game, or a frame counter that went backwards, means the entry
	// belongs to a game that has ended. It never blocks.
	if (entry.game != game || frame < entry.setFrame) {
		Lockouts.erase(it);
		return false;
	}
	return frame < entry.expireFrame;
}

void SetLockout(void* game, std::uint32_t guid, std::int32_t affix, std::uint32_t sub, std::int32_t frame,
                std::int32_t frames) noexcept {
	try {
		const std::lock_guard<std::mutex> guard(LockoutMutex);
		if (Lockouts.size() >= LockoutPruneSize) {
			for (auto it = Lockouts.begin(); it != Lockouts.end();) {
				const LockoutEntry& entry = it->second;
				if (entry.game != game || frame < entry.setFrame || frame >= entry.expireFrame) {
					it = Lockouts.erase(it);
				} else {
					++it;
				}
			}
		}
		Lockouts[LockoutKey(guid, affix, sub)] = LockoutEntry { game, frame, frame + frames };
	} catch (...) {
		// Out of memory: the cast already happened, only its lockout is lost.
	}
}

auto LockoutCount() noexcept -> std::size_t {
	const std::lock_guard<std::mutex> guard(LockoutMutex);
	return Lockouts.size();
}

// ---------------------------------------------------------------------------
// The cast, shared by every affix
// ---------------------------------------------------------------------------

// Casts skillId for the monster the way EventFunc21 casts a "when struck" item
// skill: flag 10000000h on, unit target when there is one, path point when
// there is not, flag off. The lockout starts with the cast and lasts the
// skill's localdelay (evaluated for this monster at the cast level) unless a
// fixed frame count is given. lockoutSub is WholeAffix, or the skill id for a
// per-skill lockout.
void CastAffixSkill(void* game, void* unit, std::int32_t affix, std::int32_t skillId, void* target, std::int32_t level,
                    std::int32_t fixedLockout, std::uint32_t lockoutSub) noexcept {
	AffixCounters* counters = CountersFor(affix);

	const std::int32_t  frame = GameFrame(game);
	const std::uint32_t guid  = UnitId(unit, __FILE__, __LINE__);
	if (IsLockedOut(game, guid, affix, lockoutSub, frame)) {
		if (counters != nullptr) counters->lockedOut.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	const std::uint8_t  context = ReadField<std::uint8_t>(game, GameDataContextOffset);
	const std::uint8_t* record  = SkillsRecord(context, skillId);
	if (record == nullptr) {
		if (counters != nullptr) counters->missingSkill.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	if (level == UseMonsterLevel) level = GetUnitStat(unit, StatLevel, 0);
	if (level < 1) level = 1;

	std::int32_t lockout = fixedLockout;
	if (lockout == UseLocalDelay) {
		lockout = Evaluate(context, unit, ReadField<std::uint32_t>(record, SkillLocalDelayCalc), skillId, level);
	}

	if (target != nullptr) {
		const std::uint32_t targetType = UnitType(target);
		if (target == unit || targetType == UnitTypeMissile || targetType == UnitTypeItem) {
			target = nullptr;
		}
	}

	++AffixCastDepth;
	SetUnitFlag(unit, ItemCastFlag, 1);
	if (target != nullptr) {
		CastOnTarget(unit, skillId, level, target, 0);
	} else {
		const std::int32_t y = PathPointY(unit);
		const std::int32_t x = PathPointX(unit);
		CastAtPosition(unit, skillId, level, x, y, 0);
	}
	SetUnitFlag(unit, ItemCastFlag, 0);
	--AffixCastDepth;

	if (counters != nullptr) counters->casts.fetch_add(1, std::memory_order_relaxed);
	if (lockout > 0) SetLockout(game, guid, affix, lockoutSub, frame, lockout);
}

// ---------------------------------------------------------------------------
// Affix behaviour
// ---------------------------------------------------------------------------

// Struck: Fire / Cold / Lightning Enchanted, Stone Skin, Teleportation.
// Uniques and superuniques only, like vanilla Lightning Enchanted.
void ProcessStruck(void* game, void* unit) noexcept {
	if (game == nullptr || unit == nullptr || AffixCastDepth > 0) return;
	if (HasRankFlag(unit, UniqueRank) == 0) return;

	const std::uint8_t* list = AffixList(unit);
	if (list == nullptr) return;
	void* attacker = HitDefender == unit ? HitAttacker : nullptr;

	for (std::size_t i = 0; i < AffixListLength && list[i] != 0; ++i) {
		const std::int32_t affix = list[i];
		switch (affix) {
		case AffixFireEnchanted:
			if (FireActive) {
				CastAffixSkill(game, unit, affix, Config.fireSkill, attacker, UseMonsterLevel, UseLocalDelay, WholeAffix);
			}
			break;
		case AffixColdEnchanted:
			if (ColdActive) {
				CastAffixSkill(game, unit, affix, Config.coldSkill, attacker, UseMonsterLevel, UseLocalDelay, WholeAffix);
			}
			break;
		case AffixLightningEnchanted:
			if (LightningActive) {
				CastAffixSkill(game, unit, affix, Config.lightningSkill, attacker, UseMonsterLevel, UseLocalDelay,
				               WholeAffix);
			}
			break;
		case AffixStoneSkin:
			if (StoneSkinActive) {
				CastAffixSkill(game, unit, affix, Config.stoneSkinSkill, attacker, UseMonsterLevel, UseLocalDelay,
				               WholeAffix);
			}
			break;
		case AffixTeleportation: {
			if (!TeleportActive) break;
			// Roll only while the cooldown is over, so a blocked hit leaves the seed alone.
			const std::uint32_t guid = UnitId(unit, __FILE__, __LINE__);
			if (IsLockedOut(game, guid, affix, WholeAffix, GameFrame(game))) {
				TeleportCounters.lockedOut.fetch_add(1, std::memory_order_relaxed);
				break;
			}
			if (static_cast<std::int32_t>(NextRoll(unit) % 100U) >= Config.teleportChance) break;
			CastAffixSkill(game, unit, affix, Config.teleportSkill, attacker, UseMonsterLevel,
			               Config.teleportCooldownFrames, WholeAffix);
			break;
		}
		default: break;
		}
	}
}

// Spectral Hit, at the start of A1, A2, SC and S1-S4. Same eligibility as the
// vanilla routine: uniques, or anything that is not a minion. Every skill has
// its own lockout; a locked skill is never picked, and nothing is cast when all
// of them are locked.
void ProcessSpectral(void* game, void* unit) noexcept {
	if (game == nullptr || unit == nullptr || AffixCastDepth > 0) return;
	if (!IsAttackOrCastMode(UnitMode(unit))) return;
	if (!HasAffix(unit, AffixSpectralHit)) return;
	if (HasRankFlag(unit, UniqueRank) == 0 && (RankFlags(unit) & ExcludedRanks) != 0) return;

	const std::int32_t  frame = GameFrame(game);
	const std::uint32_t guid  = UnitId(unit, __FILE__, __LINE__);

	std::array<std::int32_t, 4> ready {};
	std::uint32_t               readyCount = 0;
	for (std::uint32_t i = 0; i < SpectralPoolSize; ++i) {
		const std::int32_t skill = SpectralPool[i];
		if (!IsLockedOut(game, guid, AffixSpectralHit, static_cast<std::uint32_t>(skill), frame)) {
			ready[readyCount++] = skill;
		}
	}
	if (readyCount == 0) {
		SpectralCounters.lockedOut.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (readyCount < SpectralPoolSize) SpectralRerolls.fetch_add(1, std::memory_order_relaxed);

	const std::uint32_t index = readyCount > 1 ? NextRoll(unit) % readyCount : 0;
	const std::int32_t  skill = ready[index];
	CastAffixSkill(game, unit, AffixSpectralHit, skill, TargetUnit(game, unit), UseMonsterLevel, UseLocalDelay,
	               static_cast<std::uint32_t>(skill));
}

// Cursed, when the monster lands a hit. Monsters with rank flag 800h go to the
// routine the affix table holds (D2RLoader's own Amplify Damage routine).
// Everyone else, minions included, follows that routine's rules (chance from
// monumod.txt constants row 34, level = monster level / 5 + 1) but casts a
// random skill from the pool at the unit that was hit. Units with rank flag
// 400h are skipped, as vanilla skips them.
void ProcessCursed(void* game, void* unit, std::uint64_t affix, std::uint64_t unique) noexcept {
	const std::uint16_t ranks = RankFlags(unit);
	if ((ranks & CursedSpecialRank) != 0) {
		CursedDelegated.fetch_add(1, std::memory_order_relaxed);
		const std::uint64_t slot = ReadField<std::uint64_t>(
			reinterpret_cast<const void*>(Base),
			AffixTableRva + (static_cast<std::uint64_t>(AffixCursed) * AffixSlots + SlotLandedHit) * sizeof(std::uint64_t));
		if (slot != 0) reinterpret_cast<AffixCallbackFn>(static_cast<std::uintptr_t>(slot))(game, unit, affix, unique);
		return;
	}
	if ((ranks & Rank400) != 0 || AffixCastDepth > 0 || CursedPoolSize == 0) return;

	const std::uint8_t context = ReadField<std::uint8_t>(game, GameDataContextOffset);
	const std::int32_t chance  = AffixConstant(context, CursedChanceRow);
	if (static_cast<std::int32_t>(NextRoll(unit) % 100U) >= chance) return;

	const std::uint32_t index = CursedPoolSize > 1 ? NextRoll(unit) % CursedPoolSize : 0;
	const std::int32_t  level = GetUnitStat(unit, StatLevel, 0) / 5 + 1;
	void* target = HitAttacker == unit && HitDefender != nullptr ? HitDefender : TargetUnit(game, unit);
	if ((ranks & MinionRank) != 0) CursedMinionCasts.fetch_add(1, std::memory_order_relaxed);
	CastAffixSkill(game, unit, AffixCursed, CursedPool[index], target, level, UseLocalDelay, WholeAffix);
}

// Burns mana_burn_percent of the player's maximum mana. The cooldown belongs to
// the player, not to the monster: after a burn, no Mana Burn hit burns that
// player again until mana_burn_cooldown_frames have passed, whichever monster
// lands it. Multi-hit missiles (many hits a second) and packs of burners get
// one burn per window. The window is fixed from the burn; hits inside it do
// not extend it. A hit that finds no mana burns nothing and starts nothing.
// Blessed Vials (Alchemy plugin, celestialrayone.alchemy): a player whose
// perk is learned and whose Mana Potion effect is running cannot be burned.
// Alchemy publishes the "mana-burn-immunity" service; it is looked up on the
// first burn and again every 64 burns until found, so the load order of the
// two plugins does not matter. Without Alchemy every burn goes through.
struct AlchemyServiceTable {
	std::uint32_t serviceSize;
	std::uint32_t serviceVersion;
	bool(__cdecl* manaBurnImmune)(void* player) noexcept;
};

const AlchemyServiceTable* AlchemyService = nullptr;
std::uint32_t              AlchemyLookups = 0;

auto AlchemyImmune(void* player) noexcept -> bool {
	if (AlchemyService == nullptr && (AlchemyLookups++ % 64) == 0) {
		const D2RL::PluginCommunicationService* communication = nullptr;
		if (Context->QueryService(&communication) == D2RL::ServiceQueryResult::Success
		    && D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
			const D2RL::PluginCommunication::AcquireServiceRequest request {
				.structSize       = D2RL::PluginCommunication::AcquireServiceRequestSize,
				.flags            = 0,
				.providerPluginId = "celestialrayone.alchemy",
				.name             = "mana-burn-immunity",
				.minimumVersion   = 1,
				.minimumTableSize = static_cast<std::uint32_t>(sizeof(AlchemyServiceTable)),
			};
			D2RL::PluginCommunication::AcquiredService acquired {};
			acquired.structSize = D2RL::PluginCommunication::AcquiredServiceSize;
			if (communication->acquireService(Context, &request, &acquired) == D2RL::PluginCommunication::Result::Success
			    && acquired.table != nullptr) {
				AlchemyService = static_cast<const AlchemyServiceTable*>(acquired.table);
				LogInfo("Mana Burn: Alchemy found; Blessed Vials can stop burns.");
			}
		}
	}
	return AlchemyService != nullptr && AlchemyService->manaBurnImmune != nullptr
	    && AlchemyService->manaBurnImmune(player);
}

void ApplyManaBurn(void* game, void* attacker, void* defender) noexcept {
	if (game == nullptr || attacker == nullptr || defender == nullptr) return;
	if (UnitType(defender) != UnitTypePlayer) return;

	void* source = attacker;
	if (UnitType(source) == UnitTypeMissile) source = MissileOwner(game, source);
	if (source == nullptr || UnitType(source) != UnitTypeMonster || !HasAffix(source, AffixManaBurn)) return;

	const std::int64_t maxMana = GetUnitStat(defender, StatMaxMana, 0);
	if (maxMana <= 0) return;
	const std::int64_t mana = GetUnitStat(defender, StatMana, 0);
	if (mana <= 0) return;
	if (AlchemyImmune(defender)) {
		ManaBurnBlocked.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	const bool          cooldown = Config.manaBurnCooldownFrames > 0;
	const std::int32_t  frame    = GameFrame(game);
	const std::uint32_t player   = UnitId(defender, __FILE__, __LINE__);
	if (cooldown && IsLockedOut(game, player, AffixManaBurn, WholeAffix, frame)) {
		ManaBurnBlocked.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	std::int64_t left = mana - maxMana * Config.manaBurnPercent / 100;
	if (left < 0) left = 0;
	SetUnitStat(defender, StatMana, static_cast<std::int32_t>(left), 0);
	ManaBurnHits.fetch_add(1, std::memory_order_relaxed);
	if (cooldown) {
		// Shorter for every level the burning monster has past the start.
		const std::int32_t frames = LowerByLevels(Config.manaBurnCooldownFrames, Config.manaBurnCooldownPercentPerLevel,
		                                          LevelsAbove(source));
		if (frames > 0) SetLockout(game, player, AffixManaBurn, WholeAffix, frame, frames);
	}
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

auto LiveQword(std::uint64_t rva) noexcept -> std::uint64_t {
	return ReadField<std::uint64_t>(reinterpret_cast<const void*>(Base), rva);
}

auto SlotRva(std::uint64_t affix, std::uint32_t slot) noexcept -> std::uint64_t {
	return AffixTableRva + (affix * AffixSlots + slot) * sizeof(std::uint64_t);
}

void __fastcall HookFinalizeDamage(void* game, void* attacker, void* defender, void* damage) noexcept {
	void* const savedAttacker = HitAttacker;
	void* const savedDefender = HitDefender;
	HitAttacker = attacker;
	HitDefender = defender;
	OriginalFinalizeDamage(game, attacker, defender, damage);
	HitAttacker = savedAttacker;
	HitDefender = savedDefender;

	if (ManaBurnActive && damage != nullptr
	    && (ReadField<std::uint16_t>(damage, DamageResultFlags) & ResultSuccessfulHit) != 0) {
		ApplyManaBurn(game, attacker, defender);
	}
}

void __fastcall HookExecuteEvents(void* game, void* attacker, void* defender, std::int32_t missile, void* damage) noexcept {
	void* const savedAttacker = HitAttacker;
	void* const savedDefender = HitDefender;
	HitAttacker = attacker;
	HitDefender = defender;
	OriginalExecuteEvents(game, attacker, defender, missile, damage);
	HitAttacker = savedAttacker;
	HitDefender = savedDefender;
}

// 0x498EE0, the struck dispatcher: the vanilla affix callbacks first, then the
// plugin's when-struck skills.
std::uint64_t __fastcall HookStruckDispatch(void* game, void* unit) noexcept {
	const std::uint64_t result = OriginalStruckDispatch(game, unit);
	ProcessStruck(game, unit);
	return result;
}

// 0x498F80, the mode-change dispatcher: the vanilla affix callbacks first (the
// new mode is already set), then Spectral Hit.
std::uint64_t __fastcall HookModeDispatch(void* game, void* unit) noexcept {
	const std::uint64_t result = OriginalModeDispatch(game, unit);
	if (SpectralActive) ProcessSpectral(game, unit);
	return result;
}

// Replaces 0x4990C0, the landed-hit dispatcher. Same loop as the original
// (unique flag from rank 8h, all nine affix bytes, slot 3 of each row, null
// slots skipped) except that Cursed is handled by the plugin.
std::uint64_t __fastcall HookLandedDispatch(void* game, void* unit) noexcept {
	const std::uint64_t unique = HasRankFlag(unit, UniqueRank);
	const std::uint8_t* list   = AffixList(unit);
	if (list == nullptr || list[0] == 0) return 0;

	for (std::size_t i = 0; i < AffixListLength; ++i) {
		const std::uint64_t affix = list[i];
		if (affix == static_cast<std::uint64_t>(AffixCursed) && CursedActive) {
			ProcessCursed(game, unit, affix, unique);
			continue;
		}
		const std::uint64_t slot = LiveQword(SlotRva(affix, SlotLandedHit));
		if (slot != 0) reinterpret_cast<AffixCallbackFn>(static_cast<std::uintptr_t>(slot))(game, unit, affix, unique);
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Extra Fast frames
// ---------------------------------------------------------------------------

constexpr std::int32_t StepLength       = 256;
constexpr std::int32_t MaxSequenceSteps = 256;
constexpr std::size_t  SequenceRowSize  = 6;
constexpr std::size_t  SequenceRowEvent = 5;
constexpr std::int64_t MaxRate          = 0x7FFF;

// The percent given at spawn: fast_frames_percent_per_level for every level
// past the start, at most 100.
auto FastFramesPercent(void* unit) noexcept -> std::int32_t {
	const std::int64_t percent = static_cast<std::int64_t>(Config.fastFramesPercentPerLevel) * LevelsAbove(unit);
	return percent <= 0 ? 0 : (percent > 100 ? 100 : static_cast<std::int32_t>(percent));
}

auto IsFastFramesMode(std::uint32_t mode) noexcept -> bool {
	return mode == ModeGetHit || mode == ModeAttack1 || mode == ModeAttack2 || mode == ModeBlock || mode == ModeCast
	    || (mode >= ModeSkill1 && mode <= ModeSkill4) || mode == ModeSequence;
}

// An animation moves by rate (1/256 frame) every game frame and its mode ends
// the frame before the next step would reach the length.
auto ActionFrames(std::int64_t length, std::int64_t rate) noexcept -> std::int64_t {
	return (length + rate - 1) / rate - 1;
}

// A sequence ends when its countdown, lowered by rate every frame, reaches 0.
auto SequenceFrames(std::int64_t length, std::int64_t rate) noexcept -> std::int64_t {
	return (length + rate - 1) / rate;
}

// Plays a sequence frame by frame the way the server does (frenzy-fpa
// section 2) and tells whether every step carrying an event gets it reported.
auto EveryHitLands(const std::uint8_t* events, std::int32_t steps, std::int32_t rate) noexcept -> bool {
	if (steps <= 0 || steps > MaxSequenceSteps || rate <= 0) return false;
	const std::int32_t length = steps * StepLength;
	bool reported[MaxSequenceSteps] {};
	if (events[0] != 0) reported[0] = true;  // the installer's first step: position 0 to 0
	std::int32_t position  = 0;
	std::int64_t countdown = length;
	while (countdown > 0) {
		const std::int32_t old   = position;
		const std::int64_t moved = static_cast<std::int64_t>(old) + rate;
		position = static_cast<std::int32_t>(moved >= length ? moved % length : moved);
		countdown -= rate;
		if (position == old) {
			if (events[position / StepLength] != 0) reported[position / StepLength] = true;
			continue;
		}
		std::int32_t last = -1;
		for (std::int32_t step = old / StepLength + 1; step <= position / StepLength; ++step) {
			if (events[step] != 0) last = step;
		}
		if (last >= 0) reported[last] = true;
	}
	for (std::int32_t step = 0; step < steps; ++step) {
		if (events[step] != 0 && !reported[step]) return false;
	}
	return true;
}

// SQ: the sequence model, only for a rate the attack or cast path stored
// (the dword at +54h equals the word at +68h) and a sequence that has just
// started (position + countdown == length).
void ApplyFastFramesSequence(void* unit, std::int32_t percent) noexcept {
	const auto* rows = ReadField<const std::uint8_t*>(unit, UnitSequenceRows);
	if (rows == nullptr) return;
	const std::int32_t rate = ReadField<std::int32_t>(unit, UnitSequenceRate);
	if (rate <= 0 || rate != static_cast<std::int32_t>(ReadField<std::int16_t>(unit, UnitAnimRate))) return;
	const std::int32_t length = ReadField<std::int32_t>(unit, UnitSequenceLength);
	if (length <= 0 || length % StepLength != 0 || length / StepLength > MaxSequenceSteps) return;
	if (static_cast<std::int64_t>(ReadField<std::int32_t>(unit, UnitSequencePosition))
	        + ReadField<std::int32_t>(unit, UnitSequenceCountdown) != length) {
		return;
	}
	const std::int32_t steps = length / StepLength;
	std::uint8_t       events[MaxSequenceSteps] {};
	for (std::int32_t step = 0; step < steps; ++step) {
		events[step] = rows[static_cast<std::size_t>(step) * SequenceRowSize + SequenceRowEvent];
	}

	const std::int64_t game    = SequenceFrames(length, rate);
	const std::int64_t minimum = Config.fastFramesMinimum;
	if (game <= minimum) return;
	std::int64_t wanted = game * (100 - percent) / 100;
	if (wanted < minimum) wanted = minimum;
	for (std::int64_t frames = wanted; frames < game; ++frames) {
		std::int64_t faster = (length + frames - 1) / frames;  // the slowest rate that ends within `frames`
		if (faster > MaxRate) faster = MaxRate;
		if (faster <= rate) break;
		if (!EveryHitLands(events, steps, static_cast<std::int32_t>(faster))) continue;
		WriteField<std::int32_t>(unit, UnitSequenceRate, static_cast<std::int32_t>(faster));
		WriteField<std::int16_t>(unit, UnitAnimRate, static_cast<std::int16_t>(faster));
		FastFramesChanged.fetch_add(1, std::memory_order_relaxed);
		if (frames > wanted) FastFramesHeldBack.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	FastFramesHeldBack.fetch_add(1, std::memory_order_relaxed);
}

// Called with the unit 0x350B40 has just given its rate.
void ApplyFastFrames(void* unit) noexcept {
	if (unit == nullptr || ReadField<std::uint32_t>(unit, 0) != UnitTypeMonster) return;
	const std::uint32_t mode = ReadField<std::uint32_t>(unit, UnitModeOffset);
	if (!IsFastFramesMode(mode)) return;
	std::int32_t percent = GetUnitStat(unit, Config.fastFramesStat, 0);
	if (percent <= 0) return;
	if (percent > 100) percent = 100;
	if (mode == ModeSequence) {
		ApplyFastFramesSequence(unit, percent);
		return;
	}

	const std::int32_t rate = ReadField<std::int16_t>(unit, UnitAnimRate);
	const void*        anim = ReadField<const void*>(unit, UnitAnimData);
	if (rate <= 0 || anim == nullptr) return;
	const std::int32_t directionFrames = ReadField<std::int32_t>(anim, AnimDataFrames);
	if (directionFrames <= 0 || directionFrames > 0xFFFF) return;
	const std::int64_t length  = static_cast<std::int64_t>(directionFrames) * StepLength;
	const std::int64_t game    = ActionFrames(length, rate);
	const std::int64_t minimum = Config.fastFramesMinimum;
	if (game <= minimum) return;
	std::int64_t wanted = game * (100 - percent) / 100;
	if (wanted < minimum) wanted = minimum;
	if (wanted >= game) return;
	std::int64_t faster = (length + wanted) / (wanted + 1);  // the slowest rate that ends within `wanted` frames
	if (faster > MaxRate) faster = MaxRate;
	if (faster <= rate) return;
	WriteField<std::int16_t>(unit, UnitAnimRate, static_cast<std::int16_t>(faster));
	if (ReadField<std::int32_t>(unit, UnitSequenceRate) == rate) {
		WriteField<std::int32_t>(unit, UnitSequenceRate, static_cast<std::int32_t>(faster));
	}
	FastFramesChanged.fetch_add(1, std::memory_order_relaxed);
}

// 0x496180, Extra Strong at spawn: physical and spell damage, no attack
// rating, both raised for every level past high_level_start. Replaces the
// vanilla routine.
void __fastcall HookStrongSpawn(void* unit, std::int32_t, std::int32_t) noexcept {
	if (unit == nullptr || UnitType(unit) != UnitTypeMonster) return;
	const std::int32_t levels = LevelsAbove(unit);
	const std::int32_t damage = RaiseByLevels(Config.strongDamagePercent, Config.strongPercentPerLevel, levels);
	const std::int32_t spell  = RaiseByLevels(Config.strongSpellDamagePercent, Config.strongPercentPerLevel, levels);
	if (damage != 0) AddUnitStat(unit, StatDamagePercent, damage, 0);
	if (spell != 0) {
		for (const std::int32_t stat : SpellMasteryStats) AddUnitStat(unit, stat, spell, 0);
	}
}

// 0x496300, Extra Fast at spawn: the vanilla walk speed routine, then attack
// and cast rate by difficulty.
void __fastcall HookFastSpawn(void* unit, std::int32_t affix, std::int32_t unique) noexcept {
	OriginalFastSpawn(unit, affix, unique);
	if (unit == nullptr || UnitType(unit) != UnitTypeMonster) return;

	void* game = UnitGame(unit);
	if (game == nullptr) return;
	std::uint32_t difficulty = ReadField<std::uint8_t>(game, GameDifficultyOffset);
	if (difficulty > 2) difficulty = 2;

	const std::int32_t attackRate = Config.fastAttackRate[difficulty];
	const std::int32_t castRate   = Config.fastCastRate[difficulty];
	if (attackRate != 0) AddUnitStat(unit, StatAttackRate, attackRate, 0);
	if (castRate != 0) AddUnitStat(unit, Config.fastCastRateStat, castRate, 0);

	// Fewer frames past high_level_start, carried to clients by the stat.
	if (FastFramesActive) {
		const std::int32_t percent = FastFramesPercent(unit);
		if (percent > 0) {
			SetUnitStat(unit, Config.fastFramesStat, percent, 0);
			FastFramesGiven.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

void ApplyMagicResistant(void* unit) noexcept {
	// +50, raised for every level past high_level_start.
	const std::int32_t boost = RaiseByLevels(MagicResistBoost, Config.magicResistPercentPerLevel, LevelsAbove(unit));
	for (const ResistConversion& entry : MagicResistTable) {
		const std::int32_t current = GetUnitStat(unit, entry.resistStat, 0);

		std::int32_t gain = 0;
		if (current < MagicResistCap) {
			gain = MagicResistCap - current;
			if (gain > boost) gain = boost;
		}
		if (gain > 0) AddUnitStat(unit, entry.resistStat, gain, 0);

		const std::int32_t bonus = (boost - gain) / entry.pointsPerBonus;
		if (bonus <= 0) continue;
		for (std::int32_t i = 0; i < entry.outputCount; ++i) {
			AddUnitStat(unit, entry.outputs[static_cast<std::size_t>(i)], bonus, 0);
		}
	}
	MagicResistApplied.fetch_add(1, std::memory_order_relaxed);
}

// Replaces the affix resist routine. Only Magic Resistant does anything;
// Stone Skin no longer doubles defense. Vanilla skipped units with rank flag
// 10h (minion) or 400h. 400h stays skipped; minions get Magic Resistant too
// once the minion spawn hook is in place (otherwise they stay excluded, so
// minions are never treated two different ways). A missing MonsterData
// passes, as it does in the vanilla routine.
void __fastcall HookResistBonus(void* unit, std::int32_t affix, std::int32_t) noexcept {
	if (unit == nullptr || UnitType(unit) != UnitTypeMonster || affix != AffixMagicResistant) return;
	const std::uint16_t ranks = RankFlags(unit);
	if ((ranks & MagicResistExcludedRanks) != 0) return;
	if ((ranks & MinionRank) != 0 && !MinionMagicResist) return;
	ApplyMagicResistant(unit);
}

#if defined(_MSC_VER)
#define AFFIX_RETURN_ADDRESS() _ReturnAddress()
#else
#define AFFIX_RETURN_ADDRESS() __builtin_return_address(0)
#endif

// Rank flag setter. SpawnMinions sets flag 10h on each new minion right after
// copying the owner's xfer affixes into it; at that one call site, a minion
// that received Magic Resistant gets it applied, since SpawnMinions never runs
// affix spawn entries. Every other call passes straight through.
std::uint64_t __fastcall HookSetRankFlag(void* unit, std::uint32_t flag, std::uint32_t set) noexcept {
	const void* const caller = AFFIX_RETURN_ADDRESS();
	const std::uint64_t result = OriginalSetRankFlag(unit, flag, set);

	if (caller == reinterpret_cast<const void*>(Base + MinionFlagReturnRva) && (flag & 0xFFFFU) == MinionRank
	    && (set & 0xFFU) != 0 && unit != nullptr && UnitType(unit) == UnitTypeMonster
	    && (RankFlags(unit) & MagicResistExcludedRanks) == 0 && HasAffix(unit, AffixMagicResistant)) {
		ApplyMagicResistant(unit);
		MinionMagicResistApplied.fetch_add(1, std::memory_order_relaxed);
	}
	return result;
}

// 0x980E0, handed the unit by 0x350B40 after every rate update (the call
// returning to 0x350C8C); every other call passes straight through.
std::uint64_t __fastcall HookRateNotify(void* container, void* unitSlot, void* r8, void* r9) noexcept {
	if (FastFramesActive && unitSlot != nullptr
	    && AFFIX_RETURN_ADDRESS() == reinterpret_cast<const void*>(Base + RateNotifyReturnRva)) {
		ApplyFastFrames(*static_cast<void* const*>(unitSlot));
	}
	return OriginalRateNotify(container, unitSlot, r8, r9);
}

// ---------------------------------------------------------------------------
// Aura Enchanted
// ---------------------------------------------------------------------------

// One record of the game's own table, as it is now.
auto TableRecord(std::uint64_t tableRva, std::uint32_t index) noexcept -> AuraRecord {
	AuraRecord record {};
	std::memcpy(&record, reinterpret_cast<const std::uint8_t*>(Base + tableRva) + index * sizeof(AuraRecord),
	            sizeof(record));
	return record;
}

// The list a roll uses: the TOML's, or the game's own table read now (a static
// patch on it still counts).
auto EffectiveList(const AuraList& configured, std::uint64_t tableRva, std::uint32_t records, AuraList& stock) noexcept
	-> const AuraList& {
	if (configured.configured) return configured;
	stock.count      = 0;
	stock.configured = false;
	for (std::uint32_t i = 0; i < records && i < MaxAuraEntries; ++i) stock.records[stock.count++] = TableRecord(tableRva, i);
	return stock;
}

// The game's own pick (0x495E00..0x495EC0): records at or under the monster
// level with a positive weight count; state (seed, 666), one step, the low 32
// bits modulo the total weight, walked through the records in order.
auto PickAura(const AuraList& list, std::uint16_t seed, std::int32_t monsterLevel) noexcept -> const AuraRecord* {
	std::uint32_t total = 0;
	for (std::uint32_t i = 0; i < list.count; ++i) {
		const AuraRecord& record = list.records[i];
		if (record.minLevel <= monsterLevel && record.weight > 0) total += static_cast<std::uint32_t>(record.weight);
	}
	if (total == 0) return nullptr;

	std::uint32_t pick = (static_cast<std::uint32_t>(seed) * AuraSeedMultiplier + AuraSeedHigh) % total;
	for (std::uint32_t i = 0; i < list.count; ++i) {
		const AuraRecord& record = list.records[i];
		if (record.minLevel > monsterLevel || record.weight <= 0) continue;
		if (pick < static_cast<std::uint32_t>(record.weight)) return &record;
		pick -= static_cast<std::uint32_t>(record.weight);
	}
	return nullptr;
}

// (monster level + offset) * multiplier / divisor, kept between 1 and 99.
auto AuraLevel(const AuraRecord& record, std::int32_t monsterLevel) noexcept -> std::int32_t {
	const std::int64_t divisor = record.divisor != 0 ? record.divisor : 1;
	const std::int64_t level   = (static_cast<std::int64_t>(monsterLevel) + record.levelOffset) * record.multiplier / divisor;
	return level < 1 ? 1 : (level > 99 ? 99 : static_cast<std::int32_t>(level));
}

// Per skills.txt id: 0 not checked yet, 1 given, 2 not a row (never given).
// Each problem is logged once.
std::array<std::atomic<std::uint8_t>, 0x10000> AuraSkillState {};

auto AuraSkillUsable(void* unit, std::int32_t skill) noexcept -> bool {
	if (skill < 0 || skill > MaxSkillId) {
		AuraBadSkill.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	std::atomic<std::uint8_t>& state = AuraSkillState[static_cast<std::size_t>(skill)];
	const std::uint8_t         known = state.load(std::memory_order_relaxed);
	if (known == 1) return true;
	if (known == 2) {
		AuraBadSkill.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	char                message[192] {};
	const std::uint8_t* record = SkillsRecord(DataContext(unit), skill);
	if (record == nullptr) {
		state.store(2, std::memory_order_relaxed);
		AuraBadSkill.fetch_add(1, std::memory_order_relaxed);
		std::snprintf(message, sizeof(message),
		              "Aura Enchanted: skill %d is not a skills.txt row, so it is never given. Fix the TOML.", skill);
		LogError(message);
		return false;
	}
	const std::uint8_t mask = ReadField<std::uint8_t>(reinterpret_cast<const void*>(Base + AuraFlagMaskRva), 0);
	if (mask != 0 && (record[SkillAuraFlagOffset] & mask) == 0) {
		std::snprintf(message, sizeof(message),
		              "Aura Enchanted: skill %d has aura = 0 in skills.txt; monsters get it, but no aura runs.", skill);
		LogWarn(message);
	}
	state.store(1, std::memory_order_relaxed);
	return true;
}

// Main list and fixed auras: the skill joins the skill list (0x495F50), then
// becomes the right-hand skill (0x495F64), which starts the aura.
void GiveSkillAura(void* unit, std::int32_t skill, std::int32_t level) noexcept {
	SetSkill(unit, skill, level, 1);
	SelectSkill(unit, 0, skill, -1);
}

// Herald extra: stat 151 (item_aura) with the skill word as layer, in the stat
// list flagged 10000h, made when missing (0x4960D2..0x49614B).
void GiveHeraldAura(void* unit, std::int32_t skill, std::int32_t level) noexcept {
	void* list = FindStatList(unit, HeraldStatListFlags);
	if (list == nullptr) {
		const std::uint32_t id   = UnitId(unit, __FILE__, __LINE__);
		const std::uint32_t type = UnitType(unit);
		list                     = AllocStatList(0, 0, type, id);
		if (list == nullptr) return;
		AttachStatList(unit, list, 1);
	}
	SetListStat(DataContext(unit), list, StatItemAura, level, skill & 0xFFFF);
}

// Replaces 0x495CD0. Same steps, same order, the skills from the TOML.
void __fastcall HookAuraEnchanted(void* unit, std::uint32_t mod, std::uint32_t flag) noexcept {
	// What the routine refuses or asserts on (no unit, not a monster, flag 0)
	// goes to it unchanged.
	if (unit == nullptr || flag == 0 || UnitType(unit) != UnitTypeMonster) {
		OriginalAuraEnchanted(unit, mod, flag);
		return;
	}

	if ((ClassId(unit, __FILE__, __LINE__) & 0xFFFFU) == UberMephistoClass) {
		const bool         own   = Config.uberMephistoSkill != NoSkill;
		const std::int32_t skill = own ? Config.uberMephistoSkill : StockUberAuraSkill;
		if (AuraSkillUsable(unit, skill)) {
			GiveSkillAura(unit, skill, own ? Config.uberMephistoLevel : StockUberAuraLevel);
			AuraFixedGiven.fetch_add(1, std::memory_order_relaxed);
		}
		return;
	}

	std::int32_t monsterLevel = GetUnitStat(unit, StatLevel, 0);
	if (monsterLevel <= 0) monsterLevel = 1;
	const std::uint16_t seed = NameSeed(unit);

	AuraList stock {};
	if (SuperUnique(unit) == LordDeSeisSuperUnique) {
		const AuraRecord record = Config.lordDeSeisSkill != NoSkill
		                            ? AuraRecord { 0, 1, 0, 1, Config.lordDeSeisDivisor, Config.lordDeSeisSkill }
		                            : TableRecord(MainAuraTableRva, StockLordDeSeisRecord);
		if (AuraSkillUsable(unit, record.skill)) {
			GiveSkillAura(unit, record.skill, AuraLevel(record, monsterLevel));
			AuraFixedGiven.fetch_add(1, std::memory_order_relaxed);
		}
	} else {
		const AuraList&   list   = EffectiveList(Config.auraMain, MainAuraTableRva, MainAuraRecords, stock);
		const AuraRecord* picked = PickAura(list, seed, monsterLevel);
		if (picked == nullptr) {
			AuraNoEntry.fetch_add(1, std::memory_order_relaxed);
		} else if (AuraSkillUsable(unit, picked->skill)) {
			GiveSkillAura(unit, picked->skill, AuraLevel(*picked, monsterLevel));
			AuraMainGiven.fetch_add(1, std::memory_order_relaxed);
		}
	}

	if (HasRankFlag(unit, HeraldRank) == 0) return;
	const AuraList&   heraldList = EffectiveList(Config.auraHerald, HeraldAuraTableRva, HeraldAuraRecords, stock);
	const AuraRecord* extra      = PickAura(heraldList, seed, monsterLevel);
	if (extra != nullptr && AuraSkillUsable(unit, extra->skill)) {
		GiveHeraldAura(unit, extra->skill, AuraLevel(*extra, monsterLevel));
		AuraHeraldGiven.fetch_add(1, std::memory_order_relaxed);
	}
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr auto IsBlank(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r';
}

// Finds `key = value` at the start of a line. The config is one flat table, so
// comments and section headers are simply skipped.
auto FindConfigValue(const char* text, const char* key, const char*& value, std::size_t& length) noexcept -> bool {
	const std::size_t keyLength = std::strlen(key);
	for (const char* line = text; *line != '\0';) {
		const char* cursor = line;
		while (IsBlank(*cursor)) ++cursor;

		if (std::strncmp(cursor, key, keyLength) == 0) {
			const char* after = cursor + keyLength;
			while (IsBlank(*after)) ++after;
			if (*after == '=') {
				++after;
				while (IsBlank(*after)) ++after;
				const char* end = after;
				while (*end != '\0' && *end != '\n' && *end != '#') ++end;
				while (end > after && IsBlank(end[-1])) --end;
				value  = after;
				length = static_cast<std::size_t>(end - after);
				return true;
			}
		}

		while (*line != '\0' && *line != '\n') ++line;
		if (*line == '\n') ++line;
	}
	return false;
}

// Parses an optionally negative whole number. Returns false for anything else.
auto ParseInteger(const char* value, std::size_t length, std::int64_t& result) noexcept -> bool {
	std::size_t i        = 0;
	bool        negative = false;
	if (length > 0 && value[0] == '-') {
		negative = true;
		i        = 1;
	}
	if (i >= length) return false;

	std::int64_t number = 0;
	for (; i < length; ++i) {
		const char c = value[i];
		if (c == '_') continue;
		if (c < '0' || c > '9') return false;
		number = number * 10 + (c - '0');
		if (number > 1'000'000'000) return false;
	}
	result = negative ? -number : number;
	return true;
}

// Reads a whole number in [minimum, maximum]. Missing keys keep the fallback;
// bad values keep it and log why.
auto ReadNumber(const char* text, const char* key, std::int32_t minimum, std::int32_t maximum,
                std::int32_t fallback) noexcept -> std::int32_t {
	const char* value  = nullptr;
	std::size_t length = 0;
	if (!FindConfigValue(text, key, value, length) || length == 0) return fallback;

	std::int64_t number = 0;
	if (!ParseInteger(value, length, number) || number < minimum || number > maximum) {
		char message[192] {};
		std::snprintf(message, sizeof(message), "%s must be a whole number from %d to %d; using %d.", key, minimum,
		              maximum, fallback);
		LogWarn(message);
		return fallback;
	}
	return static_cast<std::int32_t>(number);
}

auto ReadSkillId(const char* text, const char* key, std::int32_t fallback) noexcept -> std::int32_t {
	return ReadNumber(text, key, NoSkill, MaxSkillId, fallback);
}

// Reads `key = [a, b, c]`: skills.txt ids, -1 entries skipped.
void ReadSkillList(const char* text, const char* key, Settings& settings) noexcept {
	const char* value  = nullptr;
	std::size_t length = 0;
	settings.cursedCount = 0;
	if (!FindConfigValue(text, key, value, length) || length < 2 || value[0] != '[' || value[length - 1] != ']') {
		if (length != 0) {
			char message[160] {};
			std::snprintf(message, sizeof(message), "%s must be a list such as [66, 72]; no skills used.", key);
			LogWarn(message);
		}
		return;
	}

	std::size_t i = 1;
	while (i < length - 1) {
		while (i < length - 1 && (IsBlank(value[i]) || value[i] == ',')) ++i;
		if (i >= length - 1) break;
		std::size_t end = i;
		while (end < length - 1 && value[end] != ',' && !IsBlank(value[end])) ++end;

		std::int64_t number = 0;
		if (!ParseInteger(value + i, end - i, number) || number < NoSkill || number > MaxSkillId) {
			char message[160] {};
			std::snprintf(message, sizeof(message), "%s holds an entry that is not a skill id; it is skipped.", key);
			LogWarn(message);
		} else if (number != NoSkill) {
			if (settings.cursedCount < MaxCursedSkills) {
				settings.cursedSkills[settings.cursedCount++] = static_cast<std::int32_t>(number);
			} else {
				LogWarn("cursed_skills holds more than 16 skills; the rest are ignored.");
				break;
			}
		}
		i = end;
	}
}

// Finds `key = [` at the start of a line and the `]` that closes it; the list
// may span lines and hold # comments. False when the key is missing. begin is
// null when the key is there but its value is not a closed list; end points at
// the closing `]`.
auto FindConfigList(const char* text, const char* key, const char*& begin, const char*& end) noexcept -> bool {
	const char* value  = nullptr;
	std::size_t length = 0;
	begin              = nullptr;
	end                = nullptr;
	if (!FindConfigValue(text, key, value, length)) return false;
	if (*value != '[') return true;
	for (const char* p = value + 1; *p != '\0'; ++p) {
		if (*p == '#') {
			while (p[1] != '\0' && p[1] != '\n') ++p;
			continue;
		}
		if (*p == '[') return true;
		if (*p == ']') {
			begin = value;
			end   = p;
			return true;
		}
	}
	return true;
}

auto LineOf(const char* text, const char* at) noexcept -> std::int32_t {
	std::int32_t line = 1;
	for (const char* p = text; p < at && *p != '\0'; ++p) {
		if (*p == '\n') ++line;
	}
	return line;
}

auto KeyIs(const char* word, std::size_t length, const char* key) noexcept -> bool {
	return std::strlen(key) == length && std::strncmp(word, key, length) == 0;
}

// Reads a whole number such as -1 or 1_000 at p and moves p past it.
auto ScanInteger(const char*& p, const char* end, std::int64_t& value) noexcept -> bool {
	const char* start = p;
	if (p < end && *p == '-') ++p;
	while (p < end && ((*p >= '0' && *p <= '9') || *p == '_')) ++p;
	return ParseInteger(start, static_cast<std::size_t>(p - start), value);
}

// One aura list entry: a bare skills.txt *Id, or { skill = N, divisor = N,
// min_level = N }. A skill of -1 comes back as NoSkill, which is skipped.
auto ParseAuraEntry(const char*& p, const char* end, AuraRecord& record, const char*& error) noexcept -> bool {
	std::int64_t skill    = NoSkill;
	std::int64_t divisor  = DefaultAuraDivisor;
	std::int64_t minLevel = 0;
	if (*p == '{') {
		++p;
		bool haveSkill = false;
		for (;;) {
			while (p < end && (IsBlank(*p) || *p == '\n')) ++p;
			if (p >= end) {
				error = "an entry has no closing }";
				return false;
			}
			if (*p == '}') {
				++p;
				break;
			}
			const char* word = p;
			while (p < end && ((*p >= 'a' && *p <= 'z') || *p == '_')) ++p;
			const std::size_t wordLength = static_cast<std::size_t>(p - word);
			while (p < end && IsBlank(*p)) ++p;
			if (wordLength == 0 || p >= end || *p != '=') {
				error = "expected skill, divisor or min_level = a number inside { }";
				return false;
			}
			++p;
			while (p < end && IsBlank(*p)) ++p;
			std::int64_t number = 0;
			if (!ScanInteger(p, end, number)) {
				error = "expected a whole number inside { }";
				return false;
			}
			if (KeyIs(word, wordLength, "skill")) {
				skill     = number;
				haveSkill = true;
			} else if (KeyIs(word, wordLength, "divisor")) {
				divisor = number;
			} else if (KeyIs(word, wordLength, "min_level")) {
				minLevel = number;
			} else {
				error = "unknown key inside { } (use skill, divisor, min_level)";
				return false;
			}
			while (p < end && (IsBlank(*p) || *p == '\n')) ++p;
			if (p < end && *p == ',') {
				++p;
				continue;
			}
			if (p < end && *p == '}') {
				++p;
				break;
			}
			error = "expected , or } inside { }";
			return false;
		}
		if (!haveSkill) {
			error = "an entry has no skill";
			return false;
		}
	} else if (!ScanInteger(p, end, skill)) {
		error = "each entry is a skill id or { skill = N, divisor = N, min_level = N }";
		return false;
	}

	if (skill != NoSkill && (skill < 1 || skill > MaxSkillId)) {
		error = "skill must be a skills.txt *Id from 1 to 65534, or -1";
		return false;
	}
	if (divisor < 1 || divisor > 9'999) {
		error = "divisor must be from 1 to 9999";
		return false;
	}
	if (minLevel < 0 || minLevel > 9'999) {
		error = "min_level must be from 0 to 9999";
		return false;
	}
	record = AuraRecord { static_cast<std::int32_t>(minLevel), 1, 0, 1, static_cast<std::int32_t>(divisor),
	                      static_cast<std::int32_t>(skill) };
	return true;
}

// Reads `key = [ ... ]` into list. A missing key, an empty list or one that
// cannot be read leaves list.configured false: the game's own table is used.
void ReadAuraList(const char* text, const char* key, AuraList& list) noexcept {
	list.count      = 0;
	list.configured = false;

	const char* begin = nullptr;
	const char* end   = nullptr;
	if (!FindConfigList(text, key, begin, end)) return;

	char message[256] {};
	if (begin == nullptr) {
		std::snprintf(message, sizeof(message), "%s must be a list, %s = [ ... ]; the game's own list is used.", key,
		              key);
		LogWarn(message);
		return;
	}

	const char* p = begin + 1;
	for (;;) {
		while (p < end && (IsBlank(*p) || *p == '\n' || *p == ',' || *p == '#')) {
			if (*p == '#') {
				while (p < end && *p != '\n') ++p;
			} else {
				++p;
			}
		}
		if (p >= end) break;

		AuraRecord  record {};
		const char* error = nullptr;
		if (!ParseAuraEntry(p, end, record, error)) {
			std::snprintf(message, sizeof(message), "%s, line %d: %s; the game's own list is used.", key,
			              LineOf(text, p), error);
			LogWarn(message);
			list.count = 0;
			return;
		}
		if (record.skill == NoSkill) continue;
		if (list.count >= MaxAuraEntries) {
			std::snprintf(message, sizeof(message), "%s holds more than %u skills; the game's own list is used.", key,
			              static_cast<unsigned>(MaxAuraEntries));
			LogWarn(message);
			list.count = 0;
			return;
		}
		list.records[list.count++] = record;
	}

	if (list.count == 0) {
		std::snprintf(message, sizeof(message), "%s is empty; the game's own list is used.", key);
		LogInfo(message);
		return;
	}
	list.configured = true;
}

constexpr char DefaultConfigToml[] = R"toml(# celestialrayone.monster-affixes
#
# Monster affix rework for the unique monster mods in monumod.txt.
# Changes are read when the plugin loads. Restart the game after editing.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3).
# Console command: monsteraffixes (what is installed, casts, lockouts).
#
# An affix only spawns when its monumod.txt row has enabled = 1 and a pick
# weight (upick). Nothing here changes which affixes can roll.
#
# Every skill below is a skills.txt *Id. -1 turns that entry off.
#
# HOW THE SKILLS ARE CAST
#   The monster casts the skill the way the game casts a "chance to cast when
#   struck" item skill: at a unit, or at the point it is moving or aiming
#   toward when there is no unit. The cast costs nothing and plays no
#   animation. Skill level = the monster's level, except for Cursed.
#
# WHEN STRUCK
#   Fire, Cold and Lightning Enchanted, Teleportation and Stone Skin trigger
#   every time the monster is struck by a hit that makes it flinch, a hit
#   that does not, or a soft hit. The killing blow triggers nothing. The
#   skill is aimed at the attacker. Only uniques and superuniques trigger;
#   minions that inherit the affix (monumod.txt xfer = 1) do not.
#
# LOCKOUT
#   After a cast, that affix cannot cast again on that monster for the cast
#   skill's localdelay in skills.txt, in frames (25 = 1 second). localdelay is
#   evaluated as a formula for the monster at the cast level, so lvl and other
#   calc terms work. Blank or 0 means no lockout. Teleportation uses its own
#   cooldown below (with -1 there, localdelay too). Spectral Hit locks each
#   of its skills separately.

# FIRE / COLD / LIGHTNING ENCHANTED (when struck)
#   Fire Enchanted no longer explodes on death and Cold Enchanted no longer
#   releases its nova on death (always, not configurable).
#   Lightning Enchanted: setting a skill replaces its vanilla charged bolts;
#   with -1 the vanilla bolts stay.
#   What else these affixes still give: extra fire / lightning / cold damage
#   on the monster's attacks at spawn (monlvl.txt DM / L-DM by difficulty x
#   monumod.txt constants), plus cold length for Cold Enchanted.
#
# NOTE ON MINIONS: the game copies the boss's affixes (xfer = 1) into its
#   minions but does not run the affix spawn bonuses for them when they first
#   spawn. A minion only gets spawn bonuses (enchanted damage, Extra Strong,
#   Extra Fast) after it goes inactive and the game rebuilds it. Magic
#   Resistant is the exception: this plugin applies it to minions right away.
#   Cursed and Mana Burn work on minions from the start (they act when the
#   monster lands a hit, not at spawn).
fire_enchanted_skill = -1
cold_enchanted_skill = -1
lightning_enchanted_skill = -1

# STONE SKIN (when struck)
#   Stone Skin no longer doubles defense or adds resistance.
stone_skin_skill = -1

# TELEPORTATION (when struck)
#   The affix still gives the monster its teleport skill (skills.txt Id 184)
#   as before. On top of that, each struck hit has teleport_chance percent to
#   cast teleport_skill at the attacker. After a cast nothing fires for
#   teleport_cooldown_frames (25 = 1 second). -1 uses the teleport skill's
#   skills.txt localdelay instead, evaluated for the monster at its level like
#   the other affix lockouts (blank or 0 there means no cooldown). While the
#   cooldown runs no roll is made. -1 for teleport_skill turns this part off.
teleport_skill = 184
teleport_chance = 25
teleport_cooldown_frames = 25

# SPECTRAL HIT (attack and cast animations)
#   Each time the monster starts an attack or cast animation (A1, A2, SC,
#   S1 to S4), it casts one of the skills below at its target, picked at
#   random among the ones that are ready. Every skill has its own lockout
#   (its own localdelay): a skill still locked is never picked, another ready
#   one is picked instead, and when all of them are locked nothing is cast.
#   Only the entries that are not -1 are used. Uniques only, as before.
#   The vanilla routine (a random element added to the monster's damage and to
#   every missile it fires) is always removed, even with all four set to -1.
spectral_hit_skill_1 = -1
spectral_hit_skill_2 = -1
spectral_hit_skill_3 = -1
spectral_hit_skill_4 = -1

# CURSED (when the monster lands a hit)
#   Same trigger and chance as before: monumod.txt constants row 34 (Proc
#   chance for the curse mod), rolled each time the monster lands a hit. On a
#   success it casts one skill picked at random from this list, at the unit it
#   hit, at level = monster level / 5 + 1. The picked skill's localdelay
#   applies as a lockout.
#   Minions that inherit the affix (monumod.txt xfer = 1) curse too.
#   An empty list means Amplify Damage (skills.txt Id 66), cast the same way.
#   Monsters with the special rank flag 800h (bound demons) keep the stock
#   routine untouched: Amplify Damage with its own chance.
cursed_skills = []

# HIGH LEVEL MONSTERS
#   Extra Strong, Extra Fast, Mana Burn and Magic Resistant get stronger for
#   every monster level past high_level_start (monster level = stat 12: at
#   spawn for Extra Strong, Extra Fast and Magic Resistant, the burning
#   monster's level at the hit for Mana Burn). Each affix has its own percent
#   per level below; 0 turns that part off. The growth is linear: 5 percent
#   per level with the start at 100 is +35% at level 107 (with the start at
#   99 the same level counts 8 levels, +40%).
high_level_start = 100

# EXTRA STRONG (at spawn)
#   Flat bonuses, the same for every difficulty. No attack rating bonus any
#   more. (Minions: see NOTE ON MINIONS above.)
#   strong_damage_percent: damagepercent (stat 25)
#   strong_spell_damage_percent: added to each of passive_fire_mastery,
#   passive_ltng_mastery, passive_cold_mastery, passive_pois_mastery,
#   passive_mag2_mastery and passive_wind_mastery (stats 329 to 332, 482, 483)
#   strong_percent_per_level: both bonuses grow by this percent of their own
#   value for every level past high_level_start (5: level 107 gets 135% of
#   them, 202% damage and 135% spell damage).
strong_damage_percent = 150
strong_spell_damage_percent = 100
strong_percent_per_level = 5

# EXTRA FAST (at spawn)
#   Walk speed is unchanged. On top of it, flat attack rate (stat 68,
#   attackrate) and cast rate, per difficulty. (Minions: see NOTE ON MINIONS
#   above.)
#   fast_cast_rate_stat is the itemstatcost.txt id the cast rate goes to:
#   105 is item_fastercastrate (93 is item_fasterattackrate).
fast_attack_rate_normal = 25
fast_attack_rate_nightmare = 50
fast_attack_rate_hell = 75
fast_cast_rate_normal = 25
fast_cast_rate_nightmare = 50
fast_cast_rate_hell = 75
fast_cast_rate_stat = 105

# EXTRA FAST FRAMES (high level monsters)
#   An Extra Fast monster past high_level_start plays its actions in fewer
#   frames: fast_frames_percent_per_level percent fewer for every level past
#   the start (at most 100%), never fewer than fast_frames_minimum frames;
#   an action already at or below that minimum is left alone. It covers
#   attack (A1, A2), skill (S1 to S4), cast (SC), block (BL), get hit (GH)
#   and sequence (SQ) animations, on top of every other speed bonus. A
#   sequence keeps every hit it has: when the shorter length would lose one,
#   the closest length that keeps them all is used. Two hits of one attack
#   animation that land on the same frame count once, as with any fast
#   animation in the game.
#   Walk and run are not changed: their animation speed and the ground speed
#   come from the same movement percent, so a shorter walk cycle would only
#   make the legs slide. Extra Fast keeps the game's own movement bonus.
#   The amount reaches every client in a stat, so the animation and the
#   game's timing match:
#     fast_frames_stat  itemstatcost.txt *ID of a stat added for this, with
#                       Send Other = 1, Send Bits = 8, UpdateAnimRate = 1
#                       and no Save Bits. -1 turns this part off.
#   (Minions: see NOTE ON MINIONS above.)
fast_frames_stat = -1
fast_frames_percent_per_level = 5
fast_frames_minimum = 5

# MANA BURN (when the monster lands a hit)
#   No mana drain damage any more. Instead a successful hit on a player
#   burns mana_burn_percent of that player's maximum mana from the current
#   mana. Missiles count, and minions that inherit the affix burn too.
#   -1 keeps the stock mana drain (and ignores the cooldown below).
#   mana_burn_cooldown_frames: after a burn, that player's mana cannot be
#   burned again for this many frames (25 = 1 second), whichever Mana Burn
#   monster lands the hit. Hits in between burn nothing and do not extend
#   the wait. So multi-hit missiles and a whole pack of burners take at most
#   one burn per window. 0 = every successful hit burns.
mana_burn_percent = 10
#   mana_burn_cooldown_percent_per_level: the cooldown gets this percent
#   shorter for every level the burning monster has past high_level_start
#   (5: a level 110 monster burns once every 12 frames). It cannot go below
#   0, which burns on every hit.
mana_burn_cooldown_frames = 25
mana_burn_cooldown_percent_per_level = 5

# AURA ENCHANTED (at spawn)
#   Replaces the game's own Aura Enchanted roll, the one D2RLoader runs while
#   d2rloader.toml has d2rcore.game_rules.aura_enchanted_selection = "3.1".
#   At "3.2" D2RLoader rolls its own built-in list and none of this applies.
#   With none of the keys below set, the game's roll runs untouched.
#
#   List entries are { skill = N, divisor = N, min_level = N } or a bare
#   skills.txt *Id (divisor 6, min_level 0). -1 entries are skipped.
#     skill      skills.txt *Id of the aura
#     divisor    aura level = monster level / divisor, kept between 1 and 99
#     min_level  the entry only counts for monsters of at least this level
#   Every entry that counts is equally likely; list a skill twice to make it
#   twice as likely. The pick comes from the monster's name seed, as before.
#   A list that is missing, empty or cannot be read uses the game's own:
#     aura list    98, 368, 108, 365, 123, 122 and 369 (369 from level 20)
#     herald list  113, 115 and 428 (or what a static patch put there)
#   Those are vanilla 3.3 rows; ESR has other skills at most of them.
#   A skill that is not a skills.txt row is never given. One whose aura column
#   is 0 is given, but runs no aura. The plugin log names each such skill once.
#   The console command lists both lists and counts each kind of roll.

# Every Aura Enchanted monster rolls one aura from this list.
aura_enchanted_skills = [
    { skill = 540, divisor = 6 },                  # Might
    { skill = 535, divisor = 6 },                  # Holy Fire
    { skill = 529, divisor = 5 },                  # Blessed Aim
    { skill = 544, divisor = 7 },                  # Holy Freeze
    { skill = 555, divisor = 8 },                  # Conviction
    { skill = 554, divisor = 8 },                  # Fanaticism
    { skill = 548, divisor = 8, min_level = 20 },  # Holy Shock
]

# Heralds roll one more aura from this list, given the way an item gives an
# aura (stat 151, item_aura).
herald_aura_skills = [
    { skill = 543, divisor = 6 },                  # Concentration
    { skill = 545, divisor = 6 },                  # Vigor
    { skill = 555, divisor = 10 },                 # Conviction
]

# Fixed auras that skip the roll. -1 keeps the game's own.
#   Uber Mephisto (monstats.txt row 704): the game gives skill 123 at level
#   20. He gets no herald aura.
#   Lord De Seis (superuniques.txt row 37): the game gives the 6th entry of
#   its own aura list (skill 122, level = monster level / 8).
uber_mephisto_aura_skill = 555
uber_mephisto_aura_level = 20
lord_de_seis_aura_skill = 554
lord_de_seis_aura_divisor = 8

# RESISTANCE (always on, nothing to configure)
#   No affix grants resistance any more. Removed: Fire / Cold / Lightning
#   Enchanted (+75 to their element), Poison (+75 poison), Mana Burn
#   (+20 magic), Spectral Hit (+20 fire, cold, lightning), Stone Skin
#   (+50 physical) and the old Magic Resistant (+40 fire, cold, lightning).
#   Ghostly champions keep their 80% physical resist (a separate routine).
#
# MAGIC RESISTANT (always on)
#   At spawn, each resist separately gets +50, capped at 95.
#   What the cap leaves unused is converted:
#     fire, cold, lightning, magic: 1% absorb of that element per 2 points
#     physical: 1 dodge, 1 avoid and 1 evade per 2 points
#     poison: 1% poison length reduction per point
#   A resist already at 95 or more gets no boost and converts all 50.
#   Applied once at spawn, never re-evaluated. The boss gets it, and so do its
#   minions that inherit the affix (monumod.txt xfer = 1); vanilla gave the
#   minions nothing.
#   magic_resistant_percent_per_level: the +50 grows by this percent for
#   every level past high_level_start (5: level 107 gets +67). The 95 cap and
#   the conversion rates stay, so the extra points mostly become absorb,
#   dodge, avoid, evade and poison length reduction.
magic_resistant_percent_per_level = 5
)toml";

void LoadSettings() noexcept {
	if (!Context->EnsureConfig(DefaultConfigToml)) {
		LogWarn("Could not create celestialrayone.monster-affixes.toml; built-in defaults are used.");
	} else {
		std::array<char, 24'576> buffer {};
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()))) {
			LogWarn("Could not read celestialrayone.monster-affixes.toml; built-in defaults are used.");
		} else {
			buffer.back()  = '\0';
			ConfigFileRead = true;

			const char* text = buffer.data();
			Settings&   c    = Config;

			c.fireSkill         = ReadSkillId(text, "fire_enchanted_skill", c.fireSkill);
			c.coldSkill         = ReadSkillId(text, "cold_enchanted_skill", c.coldSkill);
			c.lightningSkill    = ReadSkillId(text, "lightning_enchanted_skill", c.lightningSkill);
			c.stoneSkinSkill    = ReadSkillId(text, "stone_skin_skill", c.stoneSkinSkill);
			c.spectralSkills[0] = ReadSkillId(text, "spectral_hit_skill_1", c.spectralSkills[0]);
			c.spectralSkills[1] = ReadSkillId(text, "spectral_hit_skill_2", c.spectralSkills[1]);
			c.spectralSkills[2] = ReadSkillId(text, "spectral_hit_skill_3", c.spectralSkills[2]);
			c.spectralSkills[3] = ReadSkillId(text, "spectral_hit_skill_4", c.spectralSkills[3]);

			c.teleportSkill          = ReadSkillId(text, "teleport_skill", c.teleportSkill);
			c.teleportChance         = ReadNumber(text, "teleport_chance", 0, 100, c.teleportChance);
			c.teleportCooldownFrames = ReadNumber(text, "teleport_cooldown_frames", UseLocalDelay, 1'000'000,
			                                      c.teleportCooldownFrames);

			ReadSkillList(text, "cursed_skills", c);

			c.strongDamagePercent      = ReadNumber(text, "strong_damage_percent", -1'000'000, 1'000'000,
			                                        c.strongDamagePercent);
			c.strongSpellDamagePercent = ReadNumber(text, "strong_spell_damage_percent", -1'000'000, 1'000'000,
			                                        c.strongSpellDamagePercent);

			c.fastAttackRate[0] = ReadNumber(text, "fast_attack_rate_normal", -1'000, 1'000, c.fastAttackRate[0]);
			c.fastAttackRate[1] = ReadNumber(text, "fast_attack_rate_nightmare", -1'000, 1'000, c.fastAttackRate[1]);
			c.fastAttackRate[2] = ReadNumber(text, "fast_attack_rate_hell", -1'000, 1'000, c.fastAttackRate[2]);
			c.fastCastRate[0]   = ReadNumber(text, "fast_cast_rate_normal", -1'000, 1'000, c.fastCastRate[0]);
			c.fastCastRate[1]   = ReadNumber(text, "fast_cast_rate_nightmare", -1'000, 1'000, c.fastCastRate[1]);
			c.fastCastRate[2]   = ReadNumber(text, "fast_cast_rate_hell", -1'000, 1'000, c.fastCastRate[2]);
			c.fastCastRateStat  = ReadNumber(text, "fast_cast_rate_stat", 0, 0xFFFF, c.fastCastRateStat);

			c.manaBurnPercent        = ReadNumber(text, "mana_burn_percent", -1, 100, c.manaBurnPercent);
			c.manaBurnCooldownFrames = ReadNumber(text, "mana_burn_cooldown_frames", 0, 1'000'000,
			                                      c.manaBurnCooldownFrames);

			c.highLevelStart                  = ReadNumber(text, "high_level_start", 0, 100'000, c.highLevelStart);
			c.strongPercentPerLevel           = ReadNumber(text, "strong_percent_per_level", 0, 10'000, c.strongPercentPerLevel);
			c.magicResistPercentPerLevel      = ReadNumber(text, "magic_resistant_percent_per_level", 0, 10'000,
			                                               c.magicResistPercentPerLevel);
			c.manaBurnCooldownPercentPerLevel = ReadNumber(text, "mana_burn_cooldown_percent_per_level", 0, 100,
			                                               c.manaBurnCooldownPercentPerLevel);
			c.fastFramesStat                  = ReadNumber(text, "fast_frames_stat", -1, 0xFFFF, c.fastFramesStat);
			c.fastFramesPercentPerLevel       = ReadNumber(text, "fast_frames_percent_per_level", 0, 100,
			                                               c.fastFramesPercentPerLevel);
			c.fastFramesMinimum               = ReadNumber(text, "fast_frames_minimum", 1, 1'000, c.fastFramesMinimum);

			ReadAuraList(text, "aura_enchanted_skills", c.auraMain);
			ReadAuraList(text, "herald_aura_skills", c.auraHerald);
			c.uberMephistoSkill = ReadSkillId(text, "uber_mephisto_aura_skill", c.uberMephistoSkill);
			c.uberMephistoLevel = ReadNumber(text, "uber_mephisto_aura_level", 1, 99, c.uberMephistoLevel);
			c.lordDeSeisSkill   = ReadSkillId(text, "lord_de_seis_aura_skill", c.lordDeSeisSkill);
			c.lordDeSeisDivisor = ReadNumber(text, "lord_de_seis_aura_divisor", 1, 9'999, c.lordDeSeisDivisor);
		}
	}

	SpectralPoolSize = 0;
	for (const std::int32_t skill : Config.spectralSkills) {
		if (skill != NoSkill) SpectralPool[SpectralPoolSize++] = skill;
	}

	CursedPoolSize = 0;
	for (std::uint32_t i = 0; i < Config.cursedCount; ++i) CursedPool[CursedPoolSize++] = Config.cursedSkills[i];
	if (CursedPoolSize == 0) CursedPool[CursedPoolSize++] = StockCurseSkill;
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

auto CheckWitnesses() noexcept -> bool {
	struct Witness {
		std::uint64_t       rva;
		const std::uint8_t* bytes;
		std::uint32_t       size;
		const char*         name;
	};
	const Witness witnesses[] {
		{ GetUnitStatRva, LoaderThunkBytes, ByteCount(LoaderThunkBytes), "stat read" },
		{ SetUnitStatRva, LoaderThunkBytes, ByteCount(LoaderThunkBytes), "stat write" },
		{ AddUnitStatRva, LoaderThunkBytes, ByteCount(LoaderThunkBytes), "stat add" },
		{ UnitTypeRva, UnitTypeBytes, ByteCount(UnitTypeBytes), "unit type" },
		{ UnitIdRva, UnitIdBytes, ByteCount(UnitIdBytes), "unit id" },
		{ MissileOwnerRva, MissileOwnerBytes, ByteCount(MissileOwnerBytes), "missile owner" },
		{ SkillsRecordRva, SkillsRecordBytes, ByteCount(SkillsRecordBytes), "skills.txt record" },
		{ EvaluateRva, EvaluateBytes, ByteCount(EvaluateBytes), "skill formula" },
	};

	bool ok = true;
	for (const Witness& witness : witnesses) {
		if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
			char message[192] {};
			std::snprintf(message, sizeof(message), "Entry of the %s helper at 0x%llX does not match; wrong game build.",
			              witness.name, static_cast<unsigned long long>(witness.rva));
			LogError(message);
			ok = false;
		}
	}
	return ok;
}

void ResolveFunctions() noexcept {
	GetUnitStat    = At<GetUnitStatFn>(GetUnitStatRva);
	SetUnitStat    = At<SetUnitStatFn>(SetUnitStatRva);
	AddUnitStat    = At<AddUnitStatFn>(AddUnitStatRva);
	UnitType       = At<UnitTypeFn>(UnitTypeRva);
	UnitId         = At<UnitIdFn>(UnitIdRva);
	UnitMode       = At<UnitModeFn>(UnitModeRva);
	UnitSeed       = At<UnitSeedFn>(UnitSeedRva);
	UnitGame       = At<UnitGameFn>(UnitGameRva);
	SetUnitFlag    = At<SetUnitFlagFn>(SetUnitFlagRva);
	PathPointX     = At<PathPointFn>(PathPointXRva);
	PathPointY     = At<PathPointFn>(PathPointYRva);
	TargetUnit     = At<TargetUnitFn>(TargetUnitRva);
	MissileOwner   = At<MissileOwnerFn>(MissileOwnerRva);
	AffixList      = At<AffixListFn>(AffixListRva);
	HasRankFlag    = At<HasRankFlagFn>(HasRankFlagRva);
	AffixConstant  = At<AffixConstantFn>(AffixConstantRva);
	SkillsRecord   = At<SkillsRecordFn>(SkillsRecordRva);
	Evaluate       = At<EvaluateFn>(EvaluateRva);
	CastOnTarget   = At<CastOnTargetFn>(CastOnTargetRva);
	CastAtPosition = At<CastAtPositionFn>(CastAtPositionRva);
}

// Installs an inline hook and logs the outcome. original may be null for a
// hook that replaces the routine outright.
template <std::size_t Size>
auto Hook(std::uint64_t rva, const std::uint8_t (&prologue)[Size], void* hook, void** original, const char* name) noexcept
	-> bool {
	const bool ok = Context->InstallInlineHook(rva, prologue, ByteCount(prologue), hook, original)
	             && (original == nullptr || *original != nullptr);
	char message[224] {};
	if (ok) {
		std::snprintf(message, sizeof(message), "Hooked %s at 0x%llX.", name, static_cast<unsigned long long>(rva));
		LogInfo(message);
	} else {
		std::snprintf(message, sizeof(message),
		              "Inline hook on %s at 0x%llX failed: wrong game build, or its first bytes are not the "
		              "expected ones.",
		              name, static_cast<unsigned long long>(rva));
		LogError(message);
	}
	return ok;
}

auto Patch(const ReturnPatch& patch) noexcept -> bool {
	const bool ok = Context->PatchBytes(patch.rva, patch.expected, patch.size, patch.replacement, patch.size);
	char message[224] {};
	if (ok) {
		std::snprintf(message, sizeof(message), "Disabled %s (now returns at once).", patch.name);
		LogInfo(message);
	} else {
		std::snprintf(message, sizeof(message),
		              "Patching %s failed: wrong game build, or its first bytes are not the expected ones.", patch.name);
		LogError(message);
	}
	return ok;
}

void InstallResistRoutine() noexcept {
	ResistRoutineInstalled = Hook(ResistBonusRva, ResistBonusPrologue, reinterpret_cast<void*>(&HookResistBonus),
	                              nullptr, "the affix resist routine");
}

void InstallMinionMagicResist() noexcept {
	if (!ResistRoutineInstalled) return;
	if (!Context->CheckExpectedBytes(MinionFlagSiteRva, MinionFlagSiteBytes, ByteCount(MinionFlagSiteBytes))) {
		LogError("Minion flag call site at 0x49ED5F (SpawnMinions) does not match; minions keep vanilla Magic "
		         "Resistant (none).");
		return;
	}
	MinionMagicResist = Hook(SetRankFlagRva, SetRankFlagPrologue, reinterpret_cast<void*>(&HookSetRankFlag),
	                         reinterpret_cast<void**>(&OriginalSetRankFlag), "the rank flag setter");
}

// "aura-enchanted", version 1, read by Monster Display: the skills the roll can
// give. kind 0 = the ones put in the skill list (main list, Uber Mephisto, Lord
// De Seis), kind 1 = the herald extras (stat 151 layers). Writes up to capacity
// ids and returns how many it wrote. The lists are fixed after load; a list
// the TOML does not set is read from the game's table.
struct AuraEnchantedService {
	using CandidatesFn = std::uint32_t(__cdecl*)(std::uint32_t kind, std::uint16_t* skills, std::uint32_t capacity) noexcept;

	std::uint32_t serviceSize;
	std::uint32_t serviceVersion;
	CandidatesFn  candidates;
};
static_assert(sizeof(AuraEnchantedService) == 16 && offsetof(AuraEnchantedService, candidates) == 8);

auto __cdecl ServiceAuraCandidates(std::uint32_t kind, std::uint16_t* skills, std::uint32_t capacity) noexcept
	-> std::uint32_t {
	if (skills == nullptr || capacity == 0) return 0;
	std::uint32_t count = 0;
	const auto    add   = [&](std::int32_t skill) noexcept {
        if (skill <= 0 || skill > MaxSkillId || count >= capacity) return;
        skills[count++] = static_cast<std::uint16_t>(skill);
	};
	AuraList stock {};
	if (kind == 0) {
		const AuraList& list = EffectiveList(Config.auraMain, MainAuraTableRva, MainAuraRecords, stock);
		for (std::uint32_t i = 0; i < list.count; ++i) add(list.records[i].skill);
		add(Config.uberMephistoSkill != NoSkill ? Config.uberMephistoSkill : StockUberAuraSkill);
		add(Config.lordDeSeisSkill != NoSkill ? Config.lordDeSeisSkill
		                                      : TableRecord(MainAuraTableRva, StockLordDeSeisRecord).skill);
	} else if (kind == 1) {
		const AuraList& list = EffectiveList(Config.auraHerald, HeraldAuraTableRva, HeraldAuraRecords, stock);
		for (std::uint32_t i = 0; i < list.count; ++i) add(list.records[i].skill & 0xFFFF);
	}
	return count;
}

constexpr AuraEnchantedService AuraServiceTable {
	.serviceSize    = sizeof(AuraEnchantedService),
	.serviceVersion = 1,
	.candidates     = &ServiceAuraCandidates,
};

const D2RL::PluginCommunicationService* Communication = nullptr;

void PublishAuraService() noexcept {
	if (Context->QueryService(&Communication) != D2RL::ServiceQueryResult::Success
	    || !D2RL::HasPluginCommunicationServiceField(Communication, D2RL::PluginCommunicationServiceRequiredSize)) {
		Communication = nullptr;
		LogWarn("Aura Enchanted: no plugin communication service; Monster Display looks for the game's lists.");
		return;
	}
	const D2RL::PluginCommunication::PublishServiceRequest request {
		.structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
		.flags          = 0,
		.name           = "aura-enchanted",
		.serviceVersion = 1,
		.tableSize      = static_cast<std::uint32_t>(sizeof(AuraServiceTable)),
		.table          = &AuraServiceTable,
	};
	if (Communication->publishService(Context, &request) != D2RL::PluginCommunication::Result::Success) {
		LogWarn("Aura Enchanted: the aura-enchanted service was refused; Monster Display looks for the game's lists.");
	}
}

auto AuraWanted() noexcept -> bool {
	return Config.auraMain.configured || Config.auraHerald.configured || Config.uberMephistoSkill != NoSkill
	    || Config.lordDeSeisSkill != NoSkill;
}

void InstallAuraEnchanted() noexcept {
	if (!AuraWanted()) {
		LogInfo("Aura Enchanted: no aura key in the TOML; the game's own roll runs.");
		return;
	}

	struct AuraWitness {
		std::uint64_t       rva;
		const std::uint8_t* bytes;
		std::uint32_t       size;
		const char*         name;
	};
	const AuraWitness witnesses[] {
		{ 0x00495D3E, AuraUberBody, ByteCount(AuraUberBody), "Aura Enchanted, row 704 branch" },
		{ 0x00495D97, AuraLevelBody, ByteCount(AuraLevelBody), "Aura Enchanted, monster level" },
		{ 0x00495DB1, AuraSeedBody, ByteCount(AuraSeedBody), "Aura Enchanted, seed and superunique" },
		{ 0x00495DCF, AuraDeSeisBody, ByteCount(AuraDeSeisBody), "Aura Enchanted, superunique 37" },
		{ 0x00495DDD, AuraStateBody, ByteCount(AuraStateBody), "Aura Enchanted, roll state" },
		{ 0x00495E00, AuraMainReadBody, ByteCount(AuraMainReadBody), "Aura Enchanted, main list" },
		{ 0x00495E1D, AuraMainCountBody, ByteCount(AuraMainCountBody), "Aura Enchanted, main list size" },
		{ 0x00495E3B, AuraStepBody, ByteCount(AuraStepBody), "Aura Enchanted, roll step" },
		{ 0x00495EF2, AuraRecordBody, ByteCount(AuraRecordBody), "Aura Enchanted, picked record" },
		{ 0x00495F46, AuraAddSkillBody, ByteCount(AuraAddSkillBody), "Aura Enchanted, skill added" },
		{ 0x00495F59, AuraSelectBody, ByteCount(AuraSelectBody), "Aura Enchanted, right-hand skill" },
		{ 0x00495F69, AuraHeraldFlagBody, ByteCount(AuraHeraldFlagBody), "Aura Enchanted, herald flag" },
		{ 0x00495FA0, AuraHeraldReadBody, ByteCount(AuraHeraldReadBody), "Aura Enchanted, herald list" },
		{ 0x00495FBD, AuraHeraldCountBody, ByteCount(AuraHeraldCountBody), "Aura Enchanted, herald list size" },
		{ 0x004960D2, AuraListFindBody, ByteCount(AuraListFindBody), "Aura Enchanted, herald stat list" },
		{ 0x004960E7, AuraListNewBody, ByteCount(AuraListNewBody), "Aura Enchanted, new stat list" },
		{ 0x0049611D, AuraListAttachBody, ByteCount(AuraListAttachBody), "Aura Enchanted, stat list attach" },
		{ 0x0049612B, AuraStatWriteBody, ByteCount(AuraStatWriteBody), "Aura Enchanted, stat 151 write" },
		{ ClassIdRva, ClassIdBytes, ByteCount(ClassIdBytes), "monstats row helper" },
		{ NameSeedRva, NameSeedBytes, ByteCount(NameSeedBytes), "name seed helper" },
		{ SuperUniqueRva, SuperUniqueBytes, ByteCount(SuperUniqueBytes), "superunique row helper" },
		{ SetSkillRva, SetSkillBytes, ByteCount(SetSkillBytes), "set skill" },
		{ SelectSkillRva, SelectSkillBytes, ByteCount(SelectSkillBytes), "right-hand skill" },
		{ FindStatListRva, FindStatListBytes, ByteCount(FindStatListBytes), "stat list lookup" },
		{ AllocStatListRva, AllocStatListBytes, ByteCount(AllocStatListBytes), "stat list allocation" },
		{ AttachStatListRva, LoaderThunkBytes, ByteCount(LoaderThunkBytes), "stat list attach" },
		{ DataContextRva, DataContextBytes, ByteCount(DataContextBytes), "data context" },
		{ SetListStatRva, LoaderThunkBytes, ByteCount(LoaderThunkBytes), "stat list write" },
	};
	for (const AuraWitness& witness : witnesses) {
		if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
			char message[224] {};
			std::snprintf(message, sizeof(message), "%s at 0x%llX does not match; Aura Enchanted keeps the game's own roll.",
			              witness.name, static_cast<unsigned long long>(witness.rva));
			LogError(message);
			return;
		}
	}

	ClassId        = At<ClassIdFn>(ClassIdRva);
	NameSeed       = At<UnitWordFn>(NameSeedRva);
	SuperUnique    = At<UnitWordFn>(SuperUniqueRva);
	SetSkill       = At<SetSkillFn>(SetSkillRva);
	SelectSkill    = At<SelectSkillFn>(SelectSkillRva);
	FindStatList   = At<FindStatListFn>(FindStatListRva);
	AllocStatList  = At<AllocStatListFn>(AllocStatListRva);
	AttachStatList = At<AttachStatListFn>(AttachStatListRva);
	DataContext    = At<DataContextFn>(DataContextRva);
	SetListStat    = At<SetListStatFn>(SetListStatRva);

	AuraHookInstalled = Hook(AuraEnchantedRva, AuraEnchantedPrologue, reinterpret_cast<void*>(&HookAuraEnchanted),
	                         reinterpret_cast<void**>(&OriginalAuraEnchanted), "the Aura Enchanted routine");
	if (!AuraHookInstalled) return;
	PublishAuraService();

	char message[384] {};
	std::snprintf(message, sizeof(message),
	              "Aura Enchanted: main list %s (%u), herald list %s (%u), Uber Mephisto skill %d at level %d, Lord De "
	              "Seis skill %d. Used while d2rloader.toml has aura_enchanted_selection = \"3.1\".",
	              Config.auraMain.configured ? "from the TOML" : "the game's", Config.auraMain.count,
	              Config.auraHerald.configured ? "from the TOML" : "the game's", Config.auraHerald.count,
	              Config.uberMephistoSkill != NoSkill ? Config.uberMephistoSkill : StockUberAuraSkill,
	              Config.uberMephistoSkill != NoSkill ? Config.uberMephistoLevel : StockUberAuraLevel,
	              Config.lordDeSeisSkill != NoSkill ? Config.lordDeSeisSkill
	                                                : TableRecord(MainAuraTableRva, StockLordDeSeisRecord).skill);
	LogInfo(message);
}

void InstallAll() noexcept {
	const bool struckWanted = Config.fireSkill != NoSkill || Config.coldSkill != NoSkill
	                       || Config.lightningSkill != NoSkill || Config.stoneSkinSkill != NoSkill
	                       || (Config.teleportSkill != NoSkill && Config.teleportChance > 0);
	const bool manaBurnWanted = Config.manaBurnPercent >= 0;

	InstallResistRoutine();
	InstallMinionMagicResist();

	// Who hit whom, for the struck skills, Mana Burn and Cursed.
	if (struckWanted || manaBurnWanted) {
		StruckCaptureInstalled = Hook(FinalizeDamageRva, FinalizeDamagePrologue,
		                              reinterpret_cast<void*>(&HookFinalizeDamage),
		                              reinterpret_cast<void**>(&OriginalFinalizeDamage), "FinalizeDamage");
	}
	HitCaptureInstalled = Hook(ExecuteEventsRva, ExecuteEventsPrologue, reinterpret_cast<void*>(&HookExecuteEvents),
	                           reinterpret_cast<void**>(&OriginalExecuteEvents), "ExecuteEvents");

	// Death effects and the Spectral Hit element routines go in every case.
	FireDeathPatched       = Patch(FireDeathPatch);
	ColdDeathPatched       = Patch(ColdDeathPatch);
	SpectralElementPatched = Patch(SpectralElementPatch);
	SpectralMissilePatched = Patch(SpectralMissilePatch);

	// When struck.
	if (struckWanted) {
		StruckHookInstalled = Hook(StruckDispatchRva, DispatchPrologue, reinterpret_cast<void*>(&HookStruckDispatch),
		                           reinterpret_cast<void**>(&OriginalStruckDispatch), "the struck affix dispatcher");
	}
	if (StruckHookInstalled) {
		FireActive      = Config.fireSkill != NoSkill;
		ColdActive      = Config.coldSkill != NoSkill;
		StoneSkinActive = Config.stoneSkinSkill != NoSkill;
		TeleportActive  = Config.teleportSkill != NoSkill && Config.teleportChance > 0;
		if (Config.lightningSkill != NoSkill) {
			// Without the bolt patch the monster would do both, so the skill waits for it.
			LightningBoltsPatched = Patch(LightningBoltPatch);
			LightningActive       = LightningBoltsPatched;
		}
	}

	// Spectral Hit.
	if (SpectralPoolSize > 0) {
		ModeHookInstalled = Hook(ModeDispatchRva, DispatchPrologue, reinterpret_cast<void*>(&HookModeDispatch),
		                         reinterpret_cast<void**>(&OriginalModeDispatch), "the mode-change affix dispatcher");
		SpectralActive = ModeHookInstalled;
	}

	// Cursed.
	LandedHookInstalled = Hook(LandedDispatchRva, DispatchPrologue, reinterpret_cast<void*>(&HookLandedDispatch),
	                           nullptr, "the landed-hit affix dispatcher");
	CursedActive = LandedHookInstalled;

	// Spawn bonuses.
	StrongHookInstalled = Hook(StrongSpawnRva, StrongSpawnPrologue, reinterpret_cast<void*>(&HookStrongSpawn), nullptr,
	                           "the Extra Strong spawn routine");
	FastHookInstalled = Hook(FastSpawnRva, FastSpawnPrologue, reinterpret_cast<void*>(&HookFastSpawn),
	                         reinterpret_cast<void**>(&OriginalFastSpawn), "the Extra Fast spawn routine");

	// Mana Burn: the stock drain goes only once the per-hit drain is in place.
	if (manaBurnWanted) {
		if (StruckCaptureInstalled) {
			ManaBurnPatched = Patch(ManaBurnSpawnPatch);
			ManaBurnActive  = ManaBurnPatched;
		} else {
			LogError("Mana Burn needs the FinalizeDamage hook, which is not installed; the stock mana drain stays.");
		}
	}

	// Extra Fast frames: the rate update hands every unit it updates to 0x980E0.
	if (Config.fastFramesStat >= 0 && Config.fastFramesPercentPerLevel > 0) {
		if (!Context->CheckExpectedBytes(RateNotifyCallRva, RateNotifyCallBytes, ByteCount(RateNotifyCallBytes))) {
			LogError("The rate update call at 0x350C78 does not match; Extra Fast frames stay off.");
		} else {
			RateHookInstalled = Hook(RateNotifyRva, RateNotifyPrologue, reinterpret_cast<void*>(&HookRateNotify),
			                         reinterpret_cast<void**>(&OriginalRateNotify), "the rate update notifier");
		}
		FastFramesActive = RateHookInstalled;
	}

	// Aura Enchanted.
	InstallAuraEnchanted();

	char message[512] {};
	std::snprintf(message, sizeof(message),
	              "Struck skills: fire %d (%s), cold %d (%s), lightning %d (%s), stone skin %d (%s), teleport %d at "
	              "%d%%, lockout %d frames, -1 = localdelay (%s). Spectral Hit: %u skill%s (%s). Cursed: %u skill%s (%s). Mana Burn %d%%, "
	              "once per %d frames per player (%s). Death effects: fire %s, cold %s.",
	              Config.fireSkill, FireActive ? "on" : "off", Config.coldSkill, ColdActive ? "on" : "off",
	              Config.lightningSkill, LightningActive ? "on" : "off", Config.stoneSkinSkill,
	              StoneSkinActive ? "on" : "off", Config.teleportSkill, Config.teleportChance,
	              Config.teleportCooldownFrames, TeleportActive ? "on" : "off", SpectralPoolSize,
	              SpectralPoolSize == 1 ? "" : "s", SpectralActive ? "on" : "off", CursedPoolSize,
	              CursedPoolSize == 1 ? "" : "s", CursedActive ? "on" : "off", Config.manaBurnPercent,
	              Config.manaBurnCooldownFrames, ManaBurnActive ? "on" : "off", FireDeathPatched ? "removed" : "STOCK",
	              ColdDeathPatched ? "removed" : "STOCK");
	LogInfo(message);

	std::snprintf(message, sizeof(message),
	              "High level monsters past level %d, per level: Extra Strong +%d%%, Magic Resistant +%d%%, Mana Burn "
	              "cooldown -%d%%, Extra Fast frames -%d%% (stat %d, at least %d frames, %s).",
	              Config.highLevelStart, Config.strongPercentPerLevel, Config.magicResistPercentPerLevel,
	              Config.manaBurnCooldownPercentPerLevel, Config.fastFramesPercentPerLevel, Config.fastFramesStat,
	              Config.fastFramesMinimum, FastFramesActive ? "on" : "off");
	LogInfo(message);
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto OnOff(bool value) noexcept -> const char* {
	return value ? "on" : "OFF";
}

// "name (TOML|game): skill/divisor ..., with "from N" for a minimum level.
void WriteAuraList(const D2RL::PluginContext* plugin, const char* name, const AuraList& configured,
                   std::uint64_t tableRva, std::uint32_t records) noexcept {
	AuraList        stock {};
	const AuraList& list = EffectiveList(configured, tableRva, records, stock);
	char            line[1024] {};
	int             used = std::snprintf(line, sizeof(line), "%s (%s):", name, configured.configured ? "TOML" : "game");
	for (std::uint32_t i = 0; i < list.count; ++i) {
		if (used < 0 || static_cast<std::size_t>(used) >= sizeof(line)) break;
		const AuraRecord& record = list.records[i];
		if (record.weight <= 0) continue;
		const std::size_t left = sizeof(line) - static_cast<std::size_t>(used);
		used += record.minLevel > 0
		          ? std::snprintf(line + used, left, " %d/%d from %d", record.skill, record.divisor, record.minLevel)
		          : std::snprintf(line + used, left, " %d/%d", record.skill, record.divisor);
	}
	plugin->WriteConsoleMessage(line);
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
	const D2RL::PluginContext* plugin = command->plugin;

	char line[512] {};
	std::snprintf(line, sizeof(line),
	              "Monster Affixes 1.7.0: config %s | hooks: resist %s, minion flag %s, finalize %s, events %s, "
	              "struck %s, mode %s, landed %s, strong %s, fast %s",
	              ConfigFileRead ? "loaded" : "NOT READ (defaults)", OnOff(ResistRoutineInstalled),
	              OnOff(MinionMagicResist), OnOff(StruckCaptureInstalled), OnOff(HitCaptureInstalled),
	              OnOff(StruckHookInstalled), OnOff(ModeHookInstalled), OnOff(LandedHookInstalled),
	              OnOff(StrongHookInstalled), OnOff(FastHookInstalled));
	plugin->WriteConsoleMessage(line);

	std::snprintf(line, sizeof(line),
	              "disabled routines: fire death %s, cold death %s, spectral element %s, spectral missile %s, "
	              "lightning bolts %s, mana burn spawn %s",
	              OnOff(FireDeathPatched), OnOff(ColdDeathPatched), OnOff(SpectralElementPatched),
	              OnOff(SpectralMissilePatched), LightningBoltsPatched ? "on" : "stock",
	              ManaBurnPatched ? "on" : "stock");
	plugin->WriteConsoleMessage(line);

	std::snprintf(line, sizeof(line),
	              "magic resistant applied %llu (minions at spawn %llu) | mana burn hits %llu, blocked by the %d-frame "
	              "cooldown %llu | lockouts held %llu",
	              static_cast<unsigned long long>(MagicResistApplied.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(MinionMagicResistApplied.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(ManaBurnHits.load(std::memory_order_relaxed)),
	              Config.manaBurnCooldownFrames,
	              static_cast<unsigned long long>(ManaBurnBlocked.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(LockoutCount()));
	plugin->WriteConsoleMessage(line);

	std::snprintf(line, sizeof(line),
	              "high level past %d | per level: strong +%d%%, magic resistant +%d%%, mana burn cooldown -%d%%, fast "
	              "frames -%d%% (stat %d, minimum %d, %s) | fast frames given %llu, rates shortened %llu, sequences held "
	              "back %llu",
	              Config.highLevelStart, Config.strongPercentPerLevel, Config.magicResistPercentPerLevel,
	              Config.manaBurnCooldownPercentPerLevel, Config.fastFramesPercentPerLevel, Config.fastFramesStat,
	              Config.fastFramesMinimum, FastFramesActive ? "on" : "off",
	              static_cast<unsigned long long>(FastFramesGiven.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(FastFramesChanged.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(FastFramesHeldBack.load(std::memory_order_relaxed)));
	plugin->WriteConsoleMessage(line);

	struct Row {
		const char*          name;
		std::int32_t         skill;
		bool                 active;
		const AffixCounters* counters;
	};
	const Row rows[] {
		{ "fire", Config.fireSkill, FireActive, &FireCounters },
		{ "cold", Config.coldSkill, ColdActive, &ColdCounters },
		{ "lightning", Config.lightningSkill, LightningActive, &LightningCounters },
		{ "stone skin", Config.stoneSkinSkill, StoneSkinActive, &StoneSkinCounters },
		{ "teleport", Config.teleportSkill, TeleportActive, &TeleportCounters },
		{ "spectral", SpectralPoolSize > 0 ? SpectralPool[0] : NoSkill, SpectralActive, &SpectralCounters },
		{ "cursed", CursedPoolSize > 0 ? CursedPool[0] : NoSkill, CursedActive, &CursedCounters },
	};
	for (const Row& row : rows) {
		std::snprintf(line, sizeof(line), "%s: skill %d %s | casts %llu | locked out %llu | skill missing %llu",
		              row.name, row.skill, row.active ? "on" : "off",
		              static_cast<unsigned long long>(row.counters->casts.load(std::memory_order_relaxed)),
		              static_cast<unsigned long long>(row.counters->lockedOut.load(std::memory_order_relaxed)),
		              static_cast<unsigned long long>(row.counters->missingSkill.load(std::memory_order_relaxed)));
		plugin->WriteConsoleMessage(line);
	}

	std::snprintf(line, sizeof(line),
	              "spectral pool %d %d %d %d (picked past a locked skill %llu) | cursed pool %u, minion casts %llu, "
	              "bound demons sent to the stock routine %llu",
	              Config.spectralSkills[0], Config.spectralSkills[1], Config.spectralSkills[2], Config.spectralSkills[3],
	              static_cast<unsigned long long>(SpectralRerolls.load(std::memory_order_relaxed)), CursedPoolSize,
	              static_cast<unsigned long long>(CursedMinionCasts.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(CursedDelegated.load(std::memory_order_relaxed)));
	plugin->WriteConsoleMessage(line);

	std::snprintf(line, sizeof(line),
	              "aura enchanted %s | given: main list %llu, herald %llu, fixed %llu | no entry %llu | skill missing "
	              "%llu | uber mephisto %d at level %d | lord de seis %d",
	              AuraHookInstalled ? "on" : (AuraWanted() ? "OFF (see the log)" : "stock (no aura key)"),
	              static_cast<unsigned long long>(AuraMainGiven.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraHeraldGiven.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraFixedGiven.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraNoEntry.load(std::memory_order_relaxed)),
	              static_cast<unsigned long long>(AuraBadSkill.load(std::memory_order_relaxed)),
	              Config.uberMephistoSkill != NoSkill ? Config.uberMephistoSkill : StockUberAuraSkill,
	              Config.uberMephistoSkill != NoSkill ? Config.uberMephistoLevel : StockUberAuraLevel,
	              Config.lordDeSeisSkill != NoSkill ? Config.lordDeSeisSkill
	                                                : TableRecord(MainAuraTableRva, StockLordDeSeisRecord).skill);
	plugin->WriteConsoleMessage(line);
	WriteAuraList(plugin, "aura list", Config.auraMain, MainAuraTableRva, MainAuraRecords);
	WriteAuraList(plugin, "herald list", Config.auraHerald, HeraldAuraTableRva, HeraldAuraRecords);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo MonsterAffixesInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.monster-affixes",
	.name        = "Monster Affixes",
	.version     = "1.7.0",
	.author      = "CelestialRayOne",
	.description = "Monster affix rework: skills cast when struck (enchanted, Stone Skin, Teleportation), no enchanted "
	               "death effects, Spectral Hit skills on attack and cast with per-skill lockouts, Cursed skill pool "
	               "shared with minions, new Extra Strong, Extra Fast and Mana Burn bonuses, localdelay lockouts, no "
	               "affix resistances, a new Magic Resistant that minions share, Aura Enchanted auras from TOML lists, and "
	               "level scaling past level 100 for Extra Strong, Extra Fast, Mana Burn and Magic Resistant.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &MonsterAffixesInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) return false;
	Context = context;
	Base    = context->exeBase;
	if (Base == 0) {
		LogError("exeBase is zero; cannot resolve game functions.");
		return false;
	}

	if (!CheckWitnesses()) {
		LogError("Helper functions do not match this game build; nothing installed.");
		return false;
	}
	ResolveFunctions();
	LoadSettings();
	InstallAll();

	if (!Context->RegisterConsoleCommand("monsteraffixes", &StatusCommand,
	                                     "Monster Affixes status: installed parts, skills, cast and lockout counters.")) {
		LogWarn("Could not register the monsteraffixes console command.");
	}

	if (!ResistRoutineInstalled && !FireDeathPatched && !ColdDeathPatched && !StrongHookInstalled && !FastHookInstalled
	    && !LandedHookInstalled) {
		LogError("Nothing could be installed; unloading.");
		return false;
	}
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	FireActive      = false;
	ColdActive      = false;
	LightningActive = false;
	StoneSkinActive = false;
	TeleportActive  = false;
	SpectralActive  = false;
	CursedActive    = false;
	ManaBurnActive  = false;
	MinionMagicResist      = false;
	OriginalFinalizeDamage = nullptr;
	OriginalExecuteEvents  = nullptr;
	OriginalSetRankFlag    = nullptr;
	OriginalModeDispatch   = nullptr;
	OriginalStruckDispatch = nullptr;
	OriginalFastSpawn      = nullptr;
	FastFramesActive       = false;
	RateHookInstalled      = false;
	OriginalRateNotify     = nullptr;
	AuraHookInstalled      = false;
	OriginalAuraEnchanted  = nullptr;
	Context                = nullptr;
}
