// =============================================================================
//  Move Only Skill  -  celestialrayone.move-only-skill
//
//  D2R 3.3 under D2RLoader 1.3.1. A skill that makes the mouse button it sits
//  on move only. A click with it walks or runs, talks to NPCs, picks up items
//  and uses objects, portals and waypoints, the way a click with no skill
//  does, and the skill itself is never cast.
//
//  Every address below was read out of the dumped D2RLoader 1.3.1 image
//  (md5 8af7a47ce9077de0378c69af2930854d) and disassembled before being relied
//  on. Byte witnesses guard every instruction the plugin depends on, so a
//  different build refuses cleanly.
//
// -----------------------------------------------------------------------------
//  HOW THE GAME HANDLES A CLICK IN THE WORLD (client)
// -----------------------------------------------------------------------------
//  0x0FE3B0  the world click dispatcher builds a click command on its stack and
//            calls the click handler with it. A left press fills the skill
//            slot from the left skill (0x34A540) right before the call at
//            0x0FE69F; the left hold handler 0x1013B0 does the same before its
//            call at 0x101555. Command layout: +00h flags (1 left, 2 right,
//            4 press, 20h Shift, 400h controller), +08h player, +10h target
//            unit, +18h/+1Ch x/y, +20h mode of a move to a spot, +24h message
//            of a move toward a unit, +28h skill.
//  0x102490  the click handler(command). mov rdi,rcx at 0x102496; rdi stays the
//            command for the whole function.
//  0x101020  the handler's entry check, called first at 0x102499 with the real
//            skill. It sets +20h/+24h from the run setting, refuses a skill
//            that may not be used in town for Shift, right button and
//            controller clicks, settles the target, and returns 0 for a
//            command that has no skill, which drops the click entirely. That
//            is why the skill is cleared after this check and never before.
//  0x1025B9  call 0x33F790(player, skill), the skill's range class. r14 holds
//            &command.skill (0x102599). Right after the call the handler reads
//            the skill again from [r14] (0x1025BE), and every later read goes
//            through r14. With the skill cleared there, the handler takes its
//            no-skill paths:
//      ground, no Shift  0x101EF0 moves to the spot, as for every skill.
//      ground, Shift     0x1029AB..0x1029BA: no skill, the click ends.
//      unit              0x102753 asks 0x33F770, which returns 0 for no skill
//                        (Left Click Auras' relay there passes the same null
//                        skill on), then 0x1028F7 handles the click without a
//                        skill: Shift ends it, a unit that is not a monster
//                        goes to 0x101910 (talk, pick up, use), a monster
//                        press moves toward it through 0x0FABE0 with the +24h
//                        message, and an NPC is talked to.
//
// -----------------------------------------------------------------------------
//  THE CHANGE (client)
// -----------------------------------------------------------------------------
//  0x1025B9  call relay. The stub adds the command (rdi) as a third argument.
//            When the command's skill is the move only skill, the bridge clears
//            command.skill and returns 0, the range class of no skill. Every
//            other click calls 0x33F790 untouched.
//
// -----------------------------------------------------------------------------
//  FINDING THE SKILL BY NAME
// -----------------------------------------------------------------------------
//  charstats.txt Skill 1 to Skill 10 hold skills.txt names. The charstats
//  loader 0x30C560 resolves them through the skills.txt name table at
//  DataTables+0x11C8: 0x30C5F7 loads DataTables into rsi, 0x30C90A
//  lea rax,[rsi+11C8h] is the name table of StartSkill and Skill 1 to Skill 4,
//  and nothing in between writes rsi. The plugin asks the same table:
//      0x300A90  DataTables of a data context
//      0xA121F0  name -> row, the name lowercased first, -1 when missing
//      0xA12420  row -> name, used to check the answer
//      0x097790  the skills.txt record, whose id word must equal the row
//  The context is the player's (0x34A0E0, byte [unit+1BDh]). The answer is
//  kept until the data tables load again or the name table moves.
//
//  Console command "moveonlyskill" shows what is installed, the resolved skill
//  and the counter.
//  Settings: d2rloader/config/celestialrayone.move-only-skill.toml
// =============================================================================

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::MoveOnlySkill {
namespace {

constexpr char PluginVersion[] = "1.0.0";

// ---------------------------------------------------------------------------
//  Default configuration. EnsureConfig writes this text on first load, and it
//  is the plugin's documentation as shipped.
// ---------------------------------------------------------------------------
constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.move-only-skill
#
# Move Only Skill
#   A skill that makes the mouse button it sits on move only. With it on the
#   left button, a left click never casts anything: it walks or runs, talks to
#   NPCs, picks up items and uses objects, portals and waypoints, the same way
#   a click with no skill does.
#
# Changes are read when the plugin loads. Restart the game after editing.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). On any other build
# the byte checks fail and the plugin loads without changing the game.
#
# What a click does with the skill on the button
#   ground                     walk or run there; holding the button keeps
#                              moving toward the cursor
#   monster                    walk or run toward it
#   NPC                        talk
#   item                       pick up
#   object, portal, waypoint   use
#   Shift + click              nothing
#   Walk or run follows the game's run setting, as for any move. Clicks with
#   any other skill are unchanged.
#
# Data the skill needs
#   skills.txt     a row of its own with no charclass:
#                    leftskill 1, rightskill 0   left mouse button only. On the
#                                                right button a ground click
#                                                would do nothing.
#                    InGame 1                    otherwise the game treats the
#                                                skill as unusable
#                    InTown 1                    otherwise Shift + click in town
#                                                is refused, with the refusal
#                                                sound, before the plugin sees it
#                    aura 0, passive 0
#                    no srvstfunc, srvdofunc, cltstfunc, cltdofunc, missiles or
#                    mana, anim SC. The plugin keeps every mouse click from
#                    casting the skill; if anything else ever performs it, it
#                    only plays the animation.
#   skilldesc.txt  a row for its name and icon, as Kick and Throw have.
#   charstats.txt  the skill's name in a free Skill 1 to Skill 10 column of
#                  every class row.
#
#   Each time a character enters a game, the game gives it every charstats
#   Skill 1 to Skill 10 skill; the save file does not store them. Existing
#   characters therefore get the skill the next time they enter a game, new
#   characters when they are created.
#
#   When the skill is found, the log names its skill id and warns about any of
#   the skills.txt values above that would get in the way.
#
# Multiplayer
#   Client plugin. Every player needs it for their own clicks. The host needs
#   nothing, and it is not recorded with saved characters.
#
# Compatibility
#   Works alongside Left Click Auras and Whirlwind Follow Cursor; none of them
#   change the game code this plugin changes.
#   Mouse clicks only. Controller presses were not examined.
#
# Console command
#   moveonlyskill   what is installed, the skill id the name resolved to, and
#                   how many clicks were handled.

# Master switch. false loads the plugin without touching the game.
enabled = true

# The skills.txt "skill" column of the move only skill, the same name that goes
# into charstats.txt. Case does not matter. The name is looked up after the
# data tables load, so moving the row needs no change here.
skill = "Move Only"

# Writes a log line for the first 32 clicks handled as move only. Keep false
# for normal play.
diagnostics = false
)TOML";

// ---------------------------------------------------------------------------
//  Native layout (D2R 3.3, D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------
constexpr std::size_t   ClickCommandFlagsOffset    = 0x00;   // mov ebx,[rdi] at 0x1025A9
constexpr std::size_t   ClickCommandTargetOffset   = 0x10;   // mov r15,[rdi+10h] at 0x1025A2
constexpr std::size_t   ClickCommandSkillOffset    = 0x28;   // lea r14,[rdi+28h] at 0x102599
constexpr std::uint32_t ClickLeftButton            = 0x001;
constexpr std::uint32_t ClickRightButton           = 0x002;
constexpr std::uint32_t ClickPress                 = 0x004;
constexpr std::uint32_t ClickShift                 = 0x020;
constexpr std::uint32_t ClickController            = 0x400;
constexpr std::size_t   UnitTypeOffset             = 0x00;   // UNITS_GetUnitType 0x34B9D0
constexpr std::size_t   SkillNodeRecordOffset      = 0x00;   // mov rax,[rcx] at 0x33E088
constexpr std::size_t   SkillRecordIdOffset        = 0x00;   // movsx eax,word [rax] at 0x33E08B
constexpr std::size_t   SkillRecordFlags24         = 0x24;   // passive, aura (0x33F43B)
constexpr std::size_t   SkillRecordFlags25         = 0x25;   // InTown (0x101020), InGame (0x33F39B)
constexpr std::size_t   SkillRecordFlags27         = 0x27;   // leftskill (0x43902C)
constexpr std::size_t   SkillRecordFlags28         = 0x28;   // rightskill (0x43904B)
constexpr std::size_t   DataTablesSkillNamesOffset = 0x11C8; // lea rax,[rsi+11C8h] at 0x30C90A
constexpr std::uint32_t DataContextCount           = 4;      // cmp rax,4 at 0x300A9C

// The game tests a skills.txt flag as byte & mask, the masks being a table of
// dwords 1 << n at 0x1D996D0: n 0 InTown, 2 InGame (byte 25h), 4 passive,
// 5 aura (byte 24h), 6 leftskill (byte 27h), 7 rightskill (byte 28h). The
// bits match the skills.txt loader 0x302380 (InTown bit 8, InGame bit 10,
// leftskill bit 30, rightskill bit 39 of the flags at +24h).
constexpr std::uint64_t FlagMaskTableRva = 0x1D996D0;
constexpr std::uint32_t MaskInTown       = 0;
constexpr std::uint32_t MaskInGame       = 2;
constexpr std::uint32_t MaskPassive      = 4;
constexpr std::uint32_t MaskAura         = 5;
constexpr std::uint32_t MaskLeftSkill    = 6;
constexpr std::uint32_t MaskRightSkill   = 7;

// ---------------------------------------------------------------------------
//  Native RVAs
// ---------------------------------------------------------------------------
constexpr std::uint64_t ClickHandlerEntryRva     = 0x102490;
constexpr std::uint64_t ClickSkillWindowRva      = 0x102595;
constexpr std::uint64_t ClickRangeCallRva        = 0x1025B9;
constexpr std::uint64_t ClickNoSkillUnitRva      = 0x1028F7;
constexpr std::uint64_t ClickNoSkillGroundRva    = 0x102992;
constexpr std::uint64_t SkillNodeGateRva         = 0x33F770;
constexpr std::uint64_t SkillRangeClassRva       = 0x33F790;
constexpr std::uint64_t GetUnitDataContextRva    = 0x34A0E0;
constexpr std::uint64_t GetDataTablesRva         = 0x300A90;
constexpr std::uint64_t GetSkillsTxtRecordRva    = 0x097790;
constexpr std::uint64_t LinkerIndexByNameRva     = 0xA121F0;
constexpr std::uint64_t LinkerNameByIndexRva     = 0xA12420;
constexpr std::uint64_t CharStatsTablesLoadRva   = 0x30C5C4;
constexpr std::uint64_t CharStatsSkillNamesRva   = 0x30C90A;

// ---------------------------------------------------------------------------
//  Byte witnesses, read from the image
// ---------------------------------------------------------------------------
// 0x102490..0x1024A6, the click handler entry: push rdi / sub rsp,50h /
// mov rdi,rcx / call 0x101020 / test eax,eax / je.
constexpr auto ClickHandlerEntry = std::to_array<std::uint8_t>({
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9, 0xE8, 0x82, 0xEB,
    0xFF, 0xFF, 0x85, 0xC0, 0x0F, 0x84, 0x4D, 0x06, 0x00, 0x00,
});

// 0x102595..0x1025E7: player from [rdi+8], r14 = rdi+28h, rdx = [r14],
// r15 = target [rdi+10h], ebx = flags [rdi], the call 0x1025B9 -> 0x33F790,
// then mov rcx,[r14] and the record lookup 0x33CBC0 on it.
constexpr auto ClickSkillWindow = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x47, 0x08, 0x4C, 0x8D, 0x77, 0x28, 0x49, 0x8B, 0x16, 0x33,
    0xF6, 0x4C, 0x8B, 0x7F, 0x10, 0x48, 0x8B, 0xC8, 0x8B, 0x1F, 0x48, 0x89,
    0x6C, 0x24, 0x48, 0x48, 0x89, 0x44, 0x24, 0x70, 0x89, 0x74, 0x24, 0x68,
    0xE8, 0xD2, 0xD1, 0x23, 0x00, 0x49, 0x8B, 0x0E, 0x44, 0x8B, 0xE0, 0x44,
    0x8B, 0xEE, 0x48, 0x85, 0xC9, 0x74, 0x0D, 0xE8, 0xEF, 0xA5, 0x23, 0x00,
    0x4C, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x74, 0x4B, 0x4D, 0x85, 0xFF, 0x74,
    0x46, 0x4D, 0x85, 0xED, 0x0F, 0x84, 0x19, 0x01, 0x00, 0x00,
});

// 0x1028F7..0x102992, a unit click with no skill: Shift ends it, a unit that
// is not a monster goes to 0x101910, a monster press checks the monstats NPC
// flag (talk, 0x101910) or moves toward it (0x0FABE0, message [rdi+24h]).
constexpr auto ClickNoSkillUnit = std::to_array<std::uint8_t>({
    0x8B, 0xC3, 0x83, 0xE0, 0x20, 0x0F, 0x85, 0xCE, 0x01, 0x00, 0x00, 0x83,
    0xFD, 0x01, 0x74, 0x12, 0x44, 0x8B, 0xC6, 0x8B, 0xD5, 0x48, 0x8B, 0xCF,
    0xE8, 0xFC, 0xEF, 0xFF, 0xFF, 0xE9, 0xB7, 0x01, 0x00, 0x00, 0xF6, 0xC3,
    0x04, 0x0F, 0x84, 0xAE, 0x01, 0x00, 0x00, 0x41, 0xB8, 0x94, 0x0A, 0x00,
    0x00, 0x48, 0x8D, 0x15, 0x21, 0x49, 0xBC, 0x01, 0x49, 0x8B, 0xCF, 0xE8,
    0x29, 0x6F, 0x24, 0x00, 0x49, 0x8B, 0xCF, 0x0F, 0xBF, 0xD8, 0xE8, 0x9E,
    0x77, 0x24, 0x00, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0x94, 0x4D, 0xF9,
    0xFF, 0x48, 0x85, 0xC0, 0x74, 0x21, 0x0F, 0xB6, 0x40, 0x3D, 0x84, 0x05,
    0x79, 0x6D, 0xC9, 0x01, 0x74, 0x15, 0x44, 0x8B, 0xC6, 0xBA, 0x01, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xA3, 0xEF, 0xFF, 0xFF, 0xE9, 0x5E,
    0x01, 0x00, 0x00, 0x8B, 0x4F, 0x24, 0x44, 0x8B, 0xCE, 0x41, 0xB8, 0x01,
    0x00, 0x00, 0x00, 0x49, 0x8B, 0xD4, 0xE8, 0x5A, 0x82, 0xFF, 0xFF, 0xB1,
    0x01, 0xE8, 0x43, 0xB0, 0xF8, 0xFF, 0xE9, 0x3E, 0x01, 0x00, 0x00,
});

// 0x102992..0x1029C0, a Shift or right button click on the ground: local
// player only, and a command with no skill ([r14] null) ends the click.
constexpr auto ClickNoSkillGround = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x77, 0x08, 0xE8, 0x35, 0x89, 0xF8, 0xFF, 0x8B, 0xC8, 0xE8,
    0xDE, 0x7A, 0xF9, 0xFF, 0x48, 0x3B, 0xF0, 0x0F, 0x85, 0x9A, 0x00, 0x00,
    0x00, 0x4D, 0x85, 0xF6, 0x0F, 0x84, 0x1C, 0x01, 0x00, 0x00, 0x49, 0x8B,
    0x16, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x10, 0x01, 0x00, 0x00,
});

// The call at 0x1025B9, stock bytes.
constexpr auto ClickRangeCall = std::to_array<std::uint8_t>({
    0xE8, 0xD2, 0xD1, 0x23, 0x00,
});

// 0x33F770, the skill node gate: [node+10h], 0 for no player or no skill.
constexpr auto SkillNodeGateBody = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xC9, 0x74, 0x09, 0x48, 0x85, 0xD2, 0x74, 0x04, 0x8B, 0x42,
    0x10, 0xC3, 0x33, 0xC0, 0xC3,
});

// 0x33F790, the range class the relay calls through.
constexpr auto SkillRangeClassEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xD2, 0x75, 0x0F,
});

// 0x34A0E0, data context of a unit: byte [unit+1BDh].
constexpr auto GetUnitDataContextBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x0F,
    0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x300A90, DataTables of a data context: [0x2A9A580 + 16 * context].
constexpr auto GetDataTablesBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38,
    0x48, 0x83, 0xF8, 0x04, 0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x48,
    0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24, 0x40, 0xE8, 0x7A, 0xD3,
    0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x38,
    0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48, 0x03, 0xC0, 0x48, 0x8B,
    0x04, 0xC1, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x097790, the skills.txt record of a data context and skill id.
constexpr auto GetSkillsTxtRecordEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00,
});

// 0xA121F0, name table lookup: (table, name, warn) -> row or -1.
constexpr auto LinkerIndexByNameEntry = std::to_array<std::uint8_t>({
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC,
    0x78, 0x48, 0x8B, 0x05, 0xC4, 0x90, 0xFB, 0x01, 0x48, 0x33, 0xC4, 0x48,
    0x89, 0x45, 0xE8, 0x44, 0x89, 0x45, 0xAC,
});

// 0xA12420, name table reverse lookup: (table, row, mode) -> name.
constexpr auto LinkerNameByIndexEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x63, 0xDA, 0x48, 0x85, 0xC9,
    0x75, 0x20, 0x88, 0x4C, 0x24, 0x70, 0x48, 0x8D, 0x4C, 0x24, 0x70, 0xE8,
    0xD4, 0xDC, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
});

// 0x30C5C4..0x30C5FB in the charstats loader: lea rsi,[DataTables array],
// rax = 2 * context, mov rsi,[rsi+rax*8] = DataTables.
constexpr auto CharStatsTablesLoad = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x35, 0xB5, 0xDF, 0x78, 0x02, 0x48, 0x03, 0xC0, 0xC7, 0x45,
    0xC8, 0x01, 0x00, 0x00, 0x00, 0x45, 0x33, 0xF6, 0xC7, 0x45, 0xCC, 0x0F,
    0x00, 0x00, 0x00, 0x41, 0xBC, 0x3C, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x45,
    0xD0, 0x20, 0x00, 0x00, 0x00, 0x41, 0xBD, 0x3E, 0x00, 0x00, 0x00, 0x4C,
    0x89, 0x75, 0xD8, 0x48, 0x8B, 0x34, 0xC6,
});

// 0x30C90A..0x30C93B in the charstats loader: lea rax,[rsi+11C8h], stored as
// the name table of StartSkill and Skill 1 to Skill 4.
constexpr auto CharStatsSkillNames = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x86, 0xC8, 0x11, 0x00, 0x00, 0x48, 0x89, 0x8D, 0x60, 0x03,
    0x00, 0x00, 0x48, 0x8D, 0x0D, 0x69, 0xB8, 0x9B, 0x01, 0x48, 0x89, 0x85,
    0x18, 0x03, 0x00, 0x00, 0x48, 0x89, 0x85, 0x38, 0x03, 0x00, 0x00, 0x48,
    0x89, 0x85, 0x58, 0x03, 0x00, 0x00, 0x48, 0x89, 0x85, 0x78, 0x03, 0x00,
    0x00,
});

static_assert(ClickRangeCall[0] == 0xE8, "the redirected instruction is a call rel32");
static_assert(ClickRangeCallRva - ClickSkillWindowRva + ClickRangeCall.size() <= ClickSkillWindow.size(),
              "the redirected call lies inside its window");

// ---------------------------------------------------------------------------
//  Relay page. One page the plugin allocates within rel32 reach of the image;
//  nothing inside D2R's own code is used as a cave.
//
//      +0x000  UNWIND_INFO, no frame (the stub leaves rsp untouched)
//      +0x010  RUNTIME_FUNCTION of the stub
//      +0x100  mov r8,rdi / mov rax,<bridge> / jmp rax
//
//  The stub is reached by the redirected call, so the return address is on
//  the stack and rcx/rdx are the stock arguments; the bridge is entered as if
//  the game had called it, with the command in r8. r8 and rax are volatile
//  and 0x33F790 takes two arguments.
// ---------------------------------------------------------------------------
constexpr std::size_t RelayPageBytes           = 0x1000;
constexpr std::size_t RelayUnwindOffset        = 0x000;
constexpr std::size_t RelayFunctionTableOffset = 0x010;
constexpr std::size_t RelayStubOffset          = 0x100;
constexpr std::size_t RelayStubBytes           = 15;

// ---------------------------------------------------------------------------
//  Native signatures
// ---------------------------------------------------------------------------
using SkillRangeClassFn    = std::int32_t(__fastcall*)(void* player, void* skill) noexcept;
using GetUnitDataContextFn = std::uint8_t(__fastcall*)(void* unit) noexcept;
using GetDataTablesFn      = std::uint8_t*(__fastcall*)(std::uint32_t dataContext) noexcept;
using GetSkillsTxtRecordFn = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext, std::int32_t skillId) noexcept;
using LinkerIndexByNameFn  = std::int32_t(__fastcall*)(void* table, const char* name, std::int32_t warn) noexcept;
using LinkerNameByIndexFn  = const char*(__fastcall*)(void* table, std::int32_t row, std::int32_t mode) noexcept;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
struct Settings {
    bool        enabled{true};
    std::string skill{"Move Only"};
    bool        diagnostics{false};
};

enum class Lookup : std::uint8_t {
    NotYet,    // no click asked yet since the tables loaded
    Found,     // the name is a skills.txt row
    Missing,   // no skills.txt row has the name
    Mismatch,  // the name table answered, the check against skills.txt failed
};

struct Resolution {
    std::uint64_t generation;
    void*         names;
    Lookup        state;
    std::int32_t  skillId;
    std::uint8_t  leftSkill;
    std::uint8_t  rightSkill;
    std::uint8_t  inGame;
    std::uint8_t  inTown;
    std::uint8_t  aura;
    std::uint8_t  passive;
};

const D2RL::PluginContext*     Context{};
std::uintptr_t                 Base{};
Settings                       Config{};
char                           ConfigSource[48]{"built-in defaults"};
std::atomic<bool>              ClickActive{false};
bool                           TablesListener{};
std::atomic<std::uint64_t>     TablesGeneration{1};
std::array<Resolution, DataContextCount> Resolutions{};
SRWLOCK                        ResolutionLock = SRWLOCK_INIT;

SkillRangeClassFn    SkillRangeClass{};
GetUnitDataContextFn GetUnitDataContext{};
GetDataTablesFn      GetDataTables{};
GetSkillsTxtRecordFn GetSkillsTxtRecord{};
LinkerIndexByNameFn  LinkerIndexByName{};
LinkerNameByIndexFn  LinkerNameByIndex{};

std::uint8_t*               RelayPage{};
bool                        RelayUnwindRegistered{};
std::array<std::uint8_t, 5> RangeCallWritten{};
bool                        RangeCallPatched{};

std::atomic<std::uint64_t> MoveOnlyClicks{};
std::atomic<std::uint32_t> DiagnosticsBudget{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

auto ReadU8(const void* base, std::size_t offset) noexcept -> std::uint8_t {
    return static_cast<const std::uint8_t*>(base)[offset];
}

auto ReadI16(const void* base, std::size_t offset) noexcept -> std::int16_t {
    std::int16_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadPointer(const void* base, std::size_t offset) noexcept -> void* {
    void* value = nullptr;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

void WritePointer(void* base, std::size_t offset, void* value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(value));
}

auto PutU32(std::uint8_t* at, std::uint32_t value) noexcept -> void {
    std::memcpy(at, &value, sizeof(value));
}

auto PutU64(std::uint8_t* at, std::uint64_t value) noexcept -> void {
    std::memcpy(at, &value, sizeof(value));
}

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
void LogInfo(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogInfo(message);
}

void LogWarn(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogWarn(message);
}

void LogError(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogError(message);
}

auto TakeDiagnosticsLine() noexcept -> bool {
    if (!Config.diagnostics) {
        return false;
    }
    auto budget = DiagnosticsBudget.load(std::memory_order_relaxed);
    while (budget != 0) {
        if (DiagnosticsBudget.compare_exchange_weak(budget, budget - 1, std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------
auto Trim(std::string_view value) noexcept -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

// Cuts a trailing # comment that is not inside a double-quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (quoted && c == '\\') {
            ++index;
        } else if (c == '"') {
            quoted = !quoted;
        } else if (c == '#' && !quoted) {
            return line.substr(0, index);
        }
    }
    return line;
}

// A TOML basic string: "text", with \" and \\ escapes. False when malformed.
auto ParseString(std::string_view value, std::string& out) noexcept -> bool {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return false;
    }
    out.clear();
    for (std::size_t index = 1; index + 1 < value.size(); ++index) {
        char c = value[index];
        if (c == '\\') {
            if (index + 2 >= value.size()) {
                return false;
            }
            c = value[++index];
            if (c != '"' && c != '\\') {
                return false;
            }
        } else if (c == '"') {
            return false;
        }
        out.push_back(c);
    }
    return true;
}

void ParseConfig(std::string_view text) noexcept {
    std::size_t lineNumber = 0;
    for (std::size_t start = 0; start <= text.size();) {
        ++lineNumber;
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;

        line = Trim(StripComment(line));
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            LogWarn("MoveOnlySkill: config line %zu ignored, expected key = value.", lineNumber);
            continue;
        }
        const auto key   = Trim(line.substr(0, equals));
        const auto value = Trim(line.substr(equals + 1));

        bool* flag = nullptr;
        if (key == "enabled") {
            flag = &Config.enabled;
        } else if (key == "diagnostics") {
            flag = &Config.diagnostics;
        } else if (key == "skill") {
            std::string name;
            if (!ParseString(value, name)) {
                LogWarn("MoveOnlySkill: config line %zu ignored, skill must be a name in double quotes.", lineNumber);
            } else {
                Config.skill = std::string(Trim(name));
            }
            continue;
        } else {
            LogWarn("MoveOnlySkill: config line %zu has an unknown key.", lineNumber);
            continue;
        }

        if (value == "true") {
            *flag = true;
        } else if (value == "false") {
            *flag = false;
        } else {
            LogWarn("MoveOnlySkill: config line %zu ignored, the value must be true or false.", lineNumber);
        }
    }
}

void LoadConfig() noexcept {
    Config = Settings{};
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "built-in defaults");
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        LogWarn("MoveOnlySkill: could not create the default config, using built-in defaults.");
    }

    std::string buffer(16384, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required <= buffer.size()) {
            LogWarn("MoveOnlySkill: could not read the config, using built-in defaults.");
            return;
        }
        buffer.assign(required, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            LogWarn("MoveOnlySkill: could not read the config, using built-in defaults.");
            return;
        }
    }
    ParseConfig(std::string_view(buffer.c_str()));
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "config file");
}

// ---------------------------------------------------------------------------
//  Image checks
// ---------------------------------------------------------------------------
auto CheckExact(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }
    LogError("MoveOnlySkill: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
             static_cast<unsigned long long>(rva));
    return false;
}

// A function the plugin only calls may already carry another plugin's inline
// hook at its entry. The loader tracks those, and calling through one is fine.
auto CheckCallable(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }

    const D2RL::DiagnosticsService* diagnostics = nullptr;
    if (Context->QueryService(&diagnostics) == D2RL::ServiceQueryResult::Success
        && D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceRequiredSize)
        && diagnostics->queryHookStatus != nullptr) {
        D2RL::Diagnostics::HookQuery query{
            .structSize   = D2RL::Diagnostics::HookQuerySize,
            .flags        = 0,
            .rva          = rva,
            .expected     = bytes,
            .expectedSize = static_cast<std::uint32_t>(size),
            .reserved     = 0,
        };
        D2RL::Diagnostics::HookStatus status{
            .structSize = D2RL::Diagnostics::HookStatusSize,
        };
        if (diagnostics->queryHookStatus(Context, &query, &status) == D2RL::Diagnostics::Result::Success
            && status.state == D2RL::Diagnostics::ModificationState::Tracked
            && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook) {
            LogInfo("MoveOnlySkill: %s at RVA 0x%llX is hooked by another plugin, calling through it.", name,
                    static_cast<unsigned long long>(rva));
            return true;
        }
    }

    LogError("MoveOnlySkill: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
             static_cast<unsigned long long>(rva));
    return false;
}

auto ValidateImage() noexcept -> bool {
    return CheckExact(ClickHandlerEntryRva, ClickHandlerEntry.data(), ClickHandlerEntry.size(),
                      "click handler entry")
        && CheckExact(ClickSkillWindowRva, ClickSkillWindow.data(), ClickSkillWindow.size(),
                      "click handler skill read")
        && CheckExact(ClickNoSkillUnitRva, ClickNoSkillUnit.data(), ClickNoSkillUnit.size(),
                      "click handler unit click without a skill")
        && CheckExact(ClickNoSkillGroundRva, ClickNoSkillGround.data(), ClickNoSkillGround.size(),
                      "click handler ground click without a skill")
        && CheckExact(CharStatsTablesLoadRva, CharStatsTablesLoad.data(), CharStatsTablesLoad.size(),
                      "charstats loader tables")
        && CheckExact(CharStatsSkillNamesRva, CharStatsSkillNames.data(), CharStatsSkillNames.size(),
                      "charstats loader skill name table")
        && CheckCallable(SkillNodeGateRva, SkillNodeGateBody.data(), SkillNodeGateBody.size(), "skill node gate")
        && CheckCallable(SkillRangeClassRva, SkillRangeClassEntry.data(), SkillRangeClassEntry.size(),
                         "skill range class")
        && CheckCallable(GetUnitDataContextRva, GetUnitDataContextBody.data(), GetUnitDataContextBody.size(),
                         "unit data context")
        && CheckCallable(GetDataTablesRva, GetDataTablesBody.data(), GetDataTablesBody.size(),
                         "DataTables getter")
        && CheckCallable(GetSkillsTxtRecordRva, GetSkillsTxtRecordEntry.data(), GetSkillsTxtRecordEntry.size(),
                         "skills.txt record getter")
        && CheckCallable(LinkerIndexByNameRva, LinkerIndexByNameEntry.data(), LinkerIndexByNameEntry.size(),
                         "name table lookup")
        && CheckCallable(LinkerNameByIndexRva, LinkerNameByIndexEntry.data(), LinkerNameByIndexEntry.size(),
                         "name table reverse lookup");
}

// ---------------------------------------------------------------------------
//  The move only skill: name -> skill id, per data context
// ---------------------------------------------------------------------------
auto FlagMask(std::uint32_t index) noexcept -> std::uint8_t {
    return ReadU8(reinterpret_cast<const void*>(Base), static_cast<std::size_t>(FlagMaskTableRva) + 4 * index);
}

auto Flag(const std::uint8_t* record, std::size_t byte, std::uint32_t mask) noexcept -> std::uint8_t {
    return (record[byte] & FlagMask(mask)) != 0 ? 1 : 0;
}

auto EqualsIgnoreCase(const char* left, std::string_view right) noexcept -> bool {
    const auto lower = [](char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    std::size_t index = 0;
    for (; left[index] != '\0'; ++index) {
        if (index >= right.size() || lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return index == right.size();
}

// Skill id of a skill node, -1 for none (word [[node]], as 0x33E080 reads it).
auto NodeSkillId(void* node) noexcept -> std::int32_t {
    const auto* record = static_cast<const std::uint8_t*>(ReadPointer(node, SkillNodeRecordOffset));
    return record != nullptr ? ReadI16(record, SkillRecordIdOffset) : -1;
}

void ReportResolution(std::uint32_t context, const Resolution& found) noexcept {
    const char* name = Config.skill.c_str();
    switch (found.state) {
    case Lookup::Missing:
        LogWarn("MoveOnlySkill: skills.txt has no row named \"%s\" (data context %u); clicks stay stock.", name,
                context);
        return;
    case Lookup::Mismatch:
        LogError("MoveOnlySkill: the skills.txt name table answered \"%s\" with a row that does not check out "
                 "(data context %u); clicks stay stock.", name, context);
        return;
    case Lookup::NotYet:
    case Lookup::Found:
        break;
    }
    LogInfo("MoveOnlySkill: \"%s\" is skill %d (data context %u): leftskill %u, rightskill %u, InGame %u, "
            "InTown %u.", name, found.skillId, context, found.leftSkill, found.rightSkill, found.inGame,
            found.inTown);
    if (found.leftSkill == 0) {
        LogWarn("MoveOnlySkill: skills.txt leftskill is 0 for \"%s\", so it cannot go on the left mouse button.",
                name);
    }
    if (found.rightSkill != 0) {
        LogWarn("MoveOnlySkill: skills.txt rightskill is 1 for \"%s\". On the right mouse button a ground click "
                "with it does nothing.", name);
    }
    if (found.inGame == 0) {
        LogWarn("MoveOnlySkill: skills.txt InGame is 0 for \"%s\", so the game treats it as unusable.", name);
    }
    if (found.inTown == 0) {
        LogWarn("MoveOnlySkill: skills.txt InTown is 0 for \"%s\", so Shift + click in town is refused before "
                "this plugin sees it.", name);
    }
    if (found.aura != 0) {
        LogWarn("MoveOnlySkill: skills.txt aura is 1 for \"%s\"; Left Click Auras would run it as an aura.", name);
    }
    if (found.passive != 0) {
        LogWarn("MoveOnlySkill: skills.txt passive is 1 for \"%s\"; a passive cannot go on a mouse button.", name);
    }
}

// The move only skill's id in this data context, -1 when there is none. Runs
// on the thread that handles clicks; the console only reads the results.
auto MoveOnlySkillId(std::uint32_t context) noexcept -> std::int32_t {
    if (context >= DataContextCount) {
        return -1;
    }
    auto* tables = GetDataTables(context);
    if (tables == nullptr) {
        return -1;
    }
    void* names = ReadPointer(tables, DataTablesSkillNamesOffset);
    if (names == nullptr) {
        return -1;
    }
    const auto generation = TablesGeneration.load(std::memory_order_acquire);

    AcquireSRWLockShared(&ResolutionLock);
    const Resolution cached = Resolutions[context];
    ReleaseSRWLockShared(&ResolutionLock);
    if (cached.state != Lookup::NotYet && cached.generation == generation && cached.names == names) {
        return cached.state == Lookup::Found ? cached.skillId : -1;
    }

    Resolution found{};
    found.generation = generation;
    found.names      = names;
    found.state      = Lookup::Missing;
    found.skillId    = -1;
    const auto row = LinkerIndexByName(names, Config.skill.c_str(), 0);
    if (row >= 0) {
        const char* back   = LinkerNameByIndex(names, row, 0);
        const auto* record = GetSkillsTxtRecord(context, row);
        if (back != nullptr && EqualsIgnoreCase(back, Config.skill) && record != nullptr
            && ReadI16(record, SkillRecordIdOffset) == row) {
            found.state      = Lookup::Found;
            found.skillId    = row;
            found.leftSkill  = Flag(record, SkillRecordFlags27, MaskLeftSkill);
            found.rightSkill = Flag(record, SkillRecordFlags28, MaskRightSkill);
            found.inGame     = Flag(record, SkillRecordFlags25, MaskInGame);
            found.inTown     = Flag(record, SkillRecordFlags25, MaskInTown);
            found.aura       = Flag(record, SkillRecordFlags24, MaskAura);
            found.passive    = Flag(record, SkillRecordFlags24, MaskPassive);
        } else {
            found.state = Lookup::Mismatch;
        }
    }

    AcquireSRWLockExclusive(&ResolutionLock);
    Resolutions[context] = found;
    ReleaseSRWLockExclusive(&ResolutionLock);
    ReportResolution(context, found);
    return found.skillId;
}

// The data tables loaded again: every name is looked up afresh on the next click.
void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*,
                                void*) noexcept {
    TablesGeneration.fetch_add(1, std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
//  Relay bridge
// ---------------------------------------------------------------------------
auto TargetDescription(const std::uint8_t* command) noexcept -> const char* {
    const void* target = ReadPointer(command, ClickCommandTargetOffset);
    if (target == nullptr) {
        return "the ground";
    }
    switch (ReadU32(target, UnitTypeOffset)) {
    case 0:
        return "a player";
    case 1:
        return "a monster or NPC";
    case 2:
        return "an object";
    case 3:
        return "a missile";
    case 4:
        return "an item";
    case 5:
        return "an exit";
    default:
        return "a unit";
    }
}

void LogClick(const std::uint8_t* command, std::int32_t skillId) noexcept {
    const auto flags = ReadU32(command, ClickCommandFlagsOffset);
    const char* button = (flags & ClickLeftButton) != 0 ? "left" : (flags & ClickRightButton) != 0 ? "right" : "other";
    LogInfo("MoveOnlySkill: %s %s%s%s on %s with skill %d, handled with no skill.", button,
            (flags & ClickPress) != 0 ? "press" : "hold", (flags & ClickShift) != 0 ? " with Shift" : "",
            (flags & ClickController) != 0 ? " (controller)" : "", TargetDescription(command), skillId);
}

// 0x1025B9, the click handler asks for the range class of the command's skill.
std::int32_t __fastcall BridgeClickSkill(void* player, void* skill, std::uint8_t* command) noexcept {
    if (ClickActive.load(std::memory_order_relaxed) && player != nullptr && skill != nullptr && command != nullptr
        && ReadPointer(command, ClickCommandSkillOffset) == skill) {
        const auto skillId = NodeSkillId(skill);
        if (skillId >= 0 && skillId == MoveOnlySkillId(GetUnitDataContext(player))) {
            WritePointer(command, ClickCommandSkillOffset, nullptr);
            MoveOnlyClicks.fetch_add(1, std::memory_order_relaxed);
            if (TakeDiagnosticsLine()) {
                LogClick(command, skillId);
            }
            return 0;
        }
    }
    return SkillRangeClass(player, skill);
}

// ---------------------------------------------------------------------------
//  Installation
// ---------------------------------------------------------------------------
void ResolveNativeFunctions() noexcept {
    SkillRangeClass    = At<SkillRangeClassFn>(SkillRangeClassRva);
    GetUnitDataContext = At<GetUnitDataContextFn>(GetUnitDataContextRva);
    GetDataTables      = At<GetDataTablesFn>(GetDataTablesRva);
    GetSkillsTxtRecord = At<GetSkillsTxtRecordFn>(GetSkillsTxtRecordRva);
    LinkerIndexByName  = At<LinkerIndexByNameFn>(LinkerIndexByNameRva);
    LinkerNameByIndex  = At<LinkerNameByIndexFn>(LinkerNameByIndexRva);
}

auto AllocateRelayPage() noexcept -> std::uint8_t* {
    const auto* image     = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntOffset  = static_cast<std::size_t>(ReadU32(image, 0x3C));
    const auto  imageSize = ReadU32(image, ntOffset + 0x50);

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity =
        systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept {
        return (value + granularity - 1) & ~(granularity - 1);
    };

    // The page sits above the image, so the call site is the farthest point.
    const std::uintptr_t limit = Base + ClickRangeCallRva + 0x7FF00000ULL;
    std::uintptr_t address = alignUp(Base + imageSize);
    while (address + RelayPageBytes < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
            break;
        }
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageBytes <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageBytes, MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) {
            break;
        }
        address = next;
    }
    return nullptr;
}

void EmitRelay(std::uint8_t* page) noexcept {
    const std::uint8_t noFrameUnwind[4]{0x01, 0x00, 0x00, 0x00};
    std::memcpy(page + RelayUnwindOffset, noFrameUnwind, sizeof(noFrameUnwind));

    auto* stub = page + RelayStubOffset;
    stub[0] = 0x49;  // mov r8,rdi
    stub[1] = 0x89;
    stub[2] = 0xF8;
    stub[3] = 0x48;  // mov rax,imm64
    stub[4] = 0xB8;
    PutU64(stub + 5, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeClickSkill)));
    stub[13] = 0xFF;  // jmp rax
    stub[14] = 0xE0;

    auto* function = page + RelayFunctionTableOffset;
    PutU32(function, static_cast<std::uint32_t>(RelayStubOffset));
    PutU32(function + 4, static_cast<std::uint32_t>(RelayStubOffset + RelayStubBytes));
    PutU32(function + 8, static_cast<std::uint32_t>(RelayUnwindOffset));
}

auto EnsureRelay() noexcept -> bool {
    if (RelayPage != nullptr) {
        return true;
    }
    auto* page = AllocateRelayPage();
    if (page == nullptr) {
        LogError("MoveOnlySkill: no free page within call range of the game image.");
        return false;
    }
    std::memset(page, 0xCC, RelayPageBytes);
    std::memset(page, 0, RelayStubOffset);
    EmitRelay(page);

    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
        LogError("MoveOnlySkill: could not make the relay page executable.");
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

    RelayUnwindRegistered = RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(page + RelayFunctionTableOffset),
                                                1, static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(page)))
                         != FALSE;
    if (!RelayUnwindRegistered) {
        LogWarn("MoveOnlySkill: unwind data for the relay page was not registered; stack walks stop there.");
    }
    RelayPage = page;
    return true;
}

auto RestoreRangeCall() noexcept -> bool {
    if (!RangeCallPatched) {
        return true;
    }
    if (!Context->PatchBytes(ClickRangeCallRva, RangeCallWritten.data(), 5, ClickRangeCall.data(), 5)) {
        LogError("MoveOnlySkill: could not restore the call at RVA 0x%llX.",
                 static_cast<unsigned long long>(ClickRangeCallRva));
        return false;
    }
    RangeCallPatched = false;
    return true;
}

auto PatchRangeCall() noexcept -> bool {
    const auto from = Base + static_cast<std::uintptr_t>(ClickRangeCallRva) + 5;
    const auto to   = reinterpret_cast<std::uintptr_t>(RelayPage) + RelayStubOffset;
    const auto rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        LogError("MoveOnlySkill: the relay page is out of reach of RVA 0x%llX.",
                 static_cast<unsigned long long>(ClickRangeCallRva));
        return false;
    }
    RangeCallWritten[0] = 0xE8;
    const auto rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(RangeCallWritten.data() + 1, &rel32, sizeof(rel32));

    if (!Context->PatchBytes(ClickRangeCallRva, ClickRangeCall.data(), 5, RangeCallWritten.data(), 5)) {
        LogError("MoveOnlySkill: the loader refused the call at RVA 0x%llX.",
                 static_cast<unsigned long long>(ClickRangeCallRva));
        return false;
    }
    RangeCallPatched = true;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(ClickRangeCallRva)),
                    RangeCallWritten.data(), 5) != 0) {
        LogError("MoveOnlySkill: RVA 0x%llX does not hold the bytes that were written.",
                 static_cast<unsigned long long>(ClickRangeCallRva));
        RestoreRangeCall();
        return false;
    }
    return true;
}

void RegisterTablesListener() noexcept {
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        LogWarn("MoveOnlySkill: the lifecycle service is unavailable; the skill is looked up again only when the "
                "name table moves.");
        return;
    }
    const D2RL::Lifecycle::DataTablesLoadedListener tables{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
        .flags      = 0,
        .callback   = &OnDataTablesLoaded,
        .userData   = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    TablesListener = lifecycle->registerDataTablesLoadedListener(Context, &tables, &handle)
                  == D2RL::Lifecycle::Result::Success;
    if (!TablesListener) {
        LogWarn("MoveOnlySkill: the data table listener was refused; the skill is looked up again only when the "
                "name table moves.");
    }
}

void Install() noexcept {
    if (Config.skill.empty()) {
        LogError("MoveOnlySkill: skill is empty in the config; nothing is changed.");
        return;
    }
    if (!ValidateImage()) {
        LogError("MoveOnlySkill: the game image does not match; nothing is changed.");
        return;
    }
    if (!EnsureRelay()) {
        LogError("MoveOnlySkill: nothing is changed.");
        return;
    }
    RegisterTablesListener();
    if (!PatchRangeCall()) {
        LogError("MoveOnlySkill: the click handler call could not be changed; nothing is changed.");
        return;
    }
    ClickActive.store(true, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
//  Console
// ---------------------------------------------------------------------------
auto OnOff(bool value) noexcept -> const char* {
    return value ? "on" : "off";
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
    -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char line[512]{};
    std::snprintf(line, sizeof(line), "Move Only Skill %s (%s): click redirect %s; skill \"%s\"; table listener %s; "
                  "diagnostics %s.", PluginVersion, ConfigSource, OnOff(ClickActive.load(std::memory_order_relaxed)),
                  Config.skill.c_str(), OnOff(TablesListener), OnOff(Config.diagnostics));
    command->plugin->WriteConsoleMessage(line);

    std::array<Resolution, DataContextCount> snapshot{};
    AcquireSRWLockShared(&ResolutionLock);
    snapshot = Resolutions;
    ReleaseSRWLockShared(&ResolutionLock);

    const auto generation = TablesGeneration.load(std::memory_order_acquire);
    bool any = false;
    for (std::uint32_t context = 0; context < DataContextCount; ++context) {
        const auto& entry = snapshot[context];
        if (entry.state == Lookup::NotYet) {
            continue;
        }
        any = true;
        const char* age = entry.generation == generation ? "" : " (from before the last table load)";
        switch (entry.state) {
        case Lookup::Found:
            std::snprintf(line, sizeof(line), "Data context %u: skill %d, leftskill %u, rightskill %u, InGame %u, "
                          "InTown %u%s.", context, entry.skillId, entry.leftSkill, entry.rightSkill, entry.inGame,
                          entry.inTown, age);
            break;
        case Lookup::Missing:
            std::snprintf(line, sizeof(line), "Data context %u: skills.txt has no row with that name%s.", context, age);
            break;
        case Lookup::Mismatch:
        case Lookup::NotYet:
            std::snprintf(line, sizeof(line), "Data context %u: the name table answer did not check out%s.", context,
                          age);
            break;
        }
        command->plugin->WriteConsoleMessage(line);
    }
    if (!any) {
        command->plugin->WriteConsoleMessage("The skill is looked up on the first click after the data tables load.");
    }

    std::snprintf(line, sizeof(line), "Clicks handled as move only: %llu.",
                  static_cast<unsigned long long>(MoveOnlyClicks.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Config = Settings{};
    ClickActive.store(false, std::memory_order_relaxed);
    TablesListener = false;
    TablesGeneration.store(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&ResolutionLock);
    Resolutions.fill(Resolution{});
    ReleaseSRWLockExclusive(&ResolutionLock);
    MoveOnlyClicks.store(0, std::memory_order_relaxed);
    DiagnosticsBudget.store(32, std::memory_order_relaxed);
}

}  // namespace
}  // namespace CelestialRayOne::MoveOnlySkill

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.move-only-skill",
    .name        = "Move Only Skill",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "A skill that makes its mouse button move only: clicks walk, talk, pick up and use, never cast.",
    .flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::MoveOnlySkill;

    if (!D2RL::HasContext(context) || context->exeBase == 0) {
        return false;
    }
    Context = context;
    Base    = context->exeBase;
    ResetState();
    LoadConfig();

    if (!Config.enabled) {
        LogInfo("Move Only Skill %s loaded disabled by config; the game runs stock.", PluginVersion);
        return true;
    }

    ResolveNativeFunctions();
    Install();

    if (!context->RegisterConsoleCommand("moveonlyskill", StatusCommand, "Show Move Only Skill status and counter.")) {
        LogWarn("MoveOnlySkill: the moveonlyskill console command could not be registered.");
    }

    LogInfo("Move Only Skill %s by CelestialRayOne: click redirect %s, skill \"%s\", build %s.", PluginVersion,
            OnOff(ClickActive.load(std::memory_order_relaxed)), Config.skill.c_str(),
            context->buildName != nullptr ? context->buildName : "unknown");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::MoveOnlySkill;
    if (Context == nullptr) {
        return;
    }
    ClickActive.store(false, std::memory_order_relaxed);
    if (RestoreRangeCall() && RelayUnwindRegistered && RelayPage != nullptr) {
        RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(RelayPage + RelayFunctionTableOffset));
        RelayUnwindRegistered = false;
    }
}
