// Crossbow Charges
//
// Crossbow skills fire from a pool of bolts instead of sharing one cooldown,
// the skill buttons show the bolts left and a shadow that clocks down the next
// bolt, and every hit that comes from a crossbow skill (or from anything that
// hit triggers) deals a stat-driven bonus on its final damage.
//
// Everything below was read out of the D2RLoader 1.3.0 process image and
// D2RCore.dll, and every entry, call site and register used here is checked
// byte for byte at load. Nothing is installed when a check fails.
//
// D2RLoader 1.3 runs item event functions inside D2RCore.dll. For event
// function 20 D2RCore registers its own handler, which casts through the
// game's 5896E0 -> 589930 but never calls the game's table slot 20, so the
// slot hook up to 1.2.1 never ran and procs from crossbow hits were never
// tagged. 1.2.2 puts the same hook into D2RCore's handler table instead (entry
// 20 of the table RegisterWideSkillEffect hands its registrar, read when an
// event is registered). D2RCore's handlers take the game's arguments in the
// game's order; its forwarders pass rcx, rdx, r8 and r9 straight through, so
// the hook's owner is still r8.
//
// D2RLoader 1.3.1: the loader's thunk table moved up by D4h. The proc caster
// at 589C37 / 589C41 now calls GenerateUnitCastId through 3E2B64C and
// SetUnitCastId through 3E2B652 (3E2B578 / 3E2B57E in 1.3.0). Every other
// site checked here is unchanged.
//
// 1.3.0: the damage bonus reached only hits whose damage the engine
// recalculated inside SUNITDMG_ExecuteEvents (the missile flag on). Damage a
// skill function calculated up front and applied later (the stored melee
// record, PrepareAndQueueCombatRecord 44B600 -> ConsumeMeleeCombatRecord
// 44B2B0) and every other hit applied with the flag off never got it, and
// missile damage applied without the engine's owner stamp (the area callback
// 464610, the general applier 436E90) was judged by whatever the player cast
// last. The bonus now sits where every hit passes (section 6), and a hit is
// judged by the missile the engine is resolving when it is the attacker's own
// section 5).
//
// 1.3.2: the damage bonus never applied to anything, in any version. Every
// server-side part of it (skill-start tags, the hit bonus, proc tags, missile
// links) first checked that the unit is a server unit with 34F8D0, which reads
// bit 21 of unit+124h. That bit marks async client-only units; it is clear on
// the player on both sides, so every one of those parts skipped the player.
// Server units are the ones carrying 4000000h in unit+128h, set by the server
// unit allocator 4905A0 (which also stores the game at unit+D8h) and by
// nothing on the client. Bolts never depended on the check, so they worked.
//
// 1.3.1: aura pulses never get the bonus. The aura events set flag 20000000h
// in unit+128h for exactly the length of the aura's do-handler call (skill
// auras 437460, item auras 437230, aura start 43B680), so a hit, a proc or a
// missile of a player carrying that flag is never a crossbow one, whatever the
// player cast last. Missiles that a skill fires under a new cast id each
// (523370, the multi-missile fire used by Mirrored Blades, 52353C) now belong
// to the cast that fired them; before, they matched no tagged cast and never
// got the bonus. A crossbow cast is also tagged when its bolt is spent, the
// moment the game itself applies its globaldelay with a crossbow held. Bolt
// pools exist only for players holding a crossbow (1.3.0 gave every player
// type unit the regeneration event reaches a pool, never removed). A debug log
// (TOML debug, or "crossbowcharges debug on") writes every decision with its
// reason, and the console command shows counters for each step.
//
// 1.4.0: bolts reload faster while the player stands still. Every server frame
// the player spends in a neutral mode, NU (1) or TN (5), counts
// standing_reload_bonus percent extra on the reload clock; every other mode
// (walking, running, attacking, casting, getting hit, ...) counts normally.
// The mode is read every frame, so the faster reload starts on the first frame
// the player stands still. The clock now adds up hundredths of a frame per
// frame instead of comparing frame numbers, which lets its speed change from
// one frame to the next; the button shadow follows it on the host (from the
// server pool) and on a joined client (from its own player's mode). The bonus
// sits on the clock, after globaldelay was worked out, so it goes past any
// limit inside the globaldelay formula.
//
// ---------------------------------------------------------------------------
// 1. Which skills are crossbow skills
// ---------------------------------------------------------------------------
//   A skill is a crossbow skill for a unit when the unit is a player holding a
//   crossbow and the skill's skills.txt globaldelay evaluates above 0:
//     weapon   INVENTORY_GetLeftHandWeapon 387690 (inventory)  the attack weapon
//     type     ITEMS_CheckItemTypeId 373890 (item, itemtype)   equivalence aware
//     record   97790 (dataContext, skillId), globaldelay calc at +564 (234h),
//              localdelay calc at +568 (238h)
//     formula  3B5160 (dataContext, unit, calc, skillId, level)
//     level    33D1E0 (unit, skill, 1, 0), D2RCore ReadWideSkillLevel
//   The itemtypes rows are found by code through the DataTables service every
//   time the tables load.
//
// ---------------------------------------------------------------------------
// 2. The cooldown gate, shared by client and server, 3404A0 (unit, skill)
// ---------------------------------------------------------------------------
//   3404A0  40 53 56 57 41 54 41 57 48 83 EC 30 48 8B DA 48 8B F9
//   Returns 0 when the skill is blocked. Its per-skill part is:
//     3351B0(unit, 185)                            state 185 present
//     2F5940(unit, 185)                            its stat list
//     2F5DF0(ctx, list, 359, layer = skill id)     > 0 means blocked
//   (the layer is the skill record's first word, movzx r9d, word [rax]).
//   After that it blocks on the shared cooldown, state 121. For crossbow skills
//   the hook runs the per-skill part unchanged, skips state 121 and blocks at
//   0 bolts. Every other skill and unit goes to the native gate.
//
// ---------------------------------------------------------------------------
// 3. Where globaldelay is applied
// ---------------------------------------------------------------------------
//   Client 215FC0 (unit, skillId, level), the whole function:
//     record; g = formula(+564); if (g > 0) 217B30(unit, g)
//             l = formula(+568); if (l > 0) 217B90(unit, l, skillId)
//   Server 436C70 (game, unit, skillId, level), same shape, ctx = [game+106h]:
//     439470(game, unit, g) and 439500(game, unit, l, skillId)
//   Server do-handler 43ACB0 has its own inline copy inside its
//   consume-resources block, gated only on the unit being a player: the byte
//   tested at 43B152 (cmp byte [rsp+0C0h],0 / je) is written at 43AD17 by
//   sete right after the unit-type call 34B9D0. There is no sequence check, so
//   the block runs on EVERY do-event, including every arrow of a sequence skill
//   like Strafe:
//     43B163  45 8B CD             mov  r9d, r13d          ; skill id
//     43B17B  E8 -> 3B5160         globaldelay
//     43B180  85 C0 7E 0E          test eax,eax / jle
//     43B184  44 8B C0 48 8B D3 48 8B CF
//     43B18D  E8 DE E2 FF FF       call 439470             ; this call
//     43B192  44 8B 86 38 02 00 00 mov  r8d, [rsi+238h]    ; localdelay next
//   For a crossbow cast the global part spends a bolt instead. 215FC0 and
//   436C70 are hooked and rebuilt with only that difference; the call at
//   43B18D is sent through a relay that adds r13d as a fourth argument.
//   Casts triggered by items reach 43ACB0 with consume-resources 0 (the proc
//   caster 589930 passes 0 at 589C93), so they never spend a bolt.
//
//   One cast, one bolt: a cast keeps the cast id it got at skill start
//   (34F430, unit+12Ch) for all of its do-events, so the server spends at most
//   one bolt per cast id. A joined client, whose client apply 215FC0 also runs
//   per do-event, predicts at most one spend per cast id of its own unit (the
//   client skill start 217650 gives each cast an id through 8D5D0).
//
// ---------------------------------------------------------------------------
// 4. The reload clock
// ---------------------------------------------------------------------------
//   The player stat-regeneration event 42E600 (game, unit, a3, a4) re-arms
//   itself every frame through EVENT_SetEvent 48B720 at [game+170h]+1, for
//   every player, dead or alive. The hook advances that player's pool first.
//   The current bolts are written to the current-bolts stat through 2F7D10
//   (D2RCore SetWideUnitStat); D2RCore's widened stat packets carry it to the
//   owning client, and a joined client reads it back through 2F5020. On a
//   host the client side reads the server pool directly.
//
//   Each tick adds 100 to the progress of the bolt reloading now, or
//   100 + standing_reload_bonus when the player's mode is NU (1) or TN (5);
//   the bolt is back once its progress reaches its reload frames * 100, and
//   what is left over goes to the next bolt. So at the normal speed a bolt
//   takes exactly its globaldelay frames, as before.
//     mode     the dword at unit+0Ch, the field UNITS_IsDead 34C2C0 compares
//              with 0 (DT) and 17 (DD) for players and with 0 and 12 for
//              monsters (witnessed at 34C2F2, an optional part: a mismatch
//              only turns the standing bonus off)
//     neutral  the player mode start table 1D18FB0 (24-byte rows, start
//              function first) gives modes 1 and 5 the same start function
//              429660, which picks 1, or 5 when the unit's room is in town
//              (2F0750), sets it (491130) and clears the used skill; walk,
//              run and town walk (2, 3, 6) share 4297E0
//   A joined client counts its copy of the front bolt in milliseconds the
//   same way, from its own player's mode, every time a button or the gate
//   reads it.
//
// ---------------------------------------------------------------------------
// 5. Which hits get the damage bonus: cast ids and the missile in progress
// ---------------------------------------------------------------------------
//   Every cast has an id, unit+12Ch (34B6C0 reads it). D2RCore's
//   GenerateUnitCastId is a per-game counter at game+16Ch, so an id names one
//   cast in the whole game; SetUnitCastId writes unit+12Ch. The game uses it
//   to track where things come from:
//     skill start   34F430 (unit, skill, castId) sets the used skill and the id;
//                   the player mode starts 42D2C0 / 42DE40 pass a fresh id
//                   only when a skill starts, so walking keeps the old one
//     missiles      537A17 in the creator 5371A0: a new missile takes the id in
//                   its creation params (+68h), else its owner's current id.
//                   Hit sub-missiles get their parent's id this way (45DF50
//                   writes the parent's 34B6C0 into +68h)
//     missile hit   462E40 stamps the owner with the missile's id around the
//                   damage and on-hit effects, then restores it
//     procs         589930 generates a fresh id (call at 589C37) while the
//                   owner still carries the id of the hit that triggered it
//     auras         437460 / 437230 / 43B680 stamp the aura's own id around
//                   every pulse and set flag 20000000h in unit+128h (34E140)
//     multi-missile 523370 gives every missile its own new id when the
//                   skill's record asks for it (52353C)
//   So the plugin tags cast ids, each with the GUID of the player it belongs
//   to:
//     - at skill start (34F430 hook), when the skill is a crossbow skill
//     - when the cast spends its bolt (the do-handler relay at 43B18D and
//       436C70), unless an aura pulse is running
//     - in item event function 20 (chance to cast on attack, on striking and
//       on kill; D2RCore's handler table entry 20), when the hit that
//       triggers it is a crossbow hit: the fresh id generated at 589C37 is
//       tagged too
//   Event function 21 (chance to cast when struck, 5837F0) is left alone, so
//   those never count, whatever the player cast last.
//
//   Not every missile hit carries the owner stamp: the area callback 464610
//   (handed out by the missile function 4576B0) and the general applier 436E90
//   hand damage to SUNITDMG_ExecuteEvents with the player's current id. So the
//   plugin also keeps, per server thread, the missiles the engine is
//   resolving, innermost last:
//     4639A0  MISSILES_ServerHitResolution (game, missile, target, forced)
//     466CE0  MISSILES_ServerDoDispatcher  (game, missile)
//     462E40  missile damage               (game, missile, target, damage)
//   Each pushes its missile on entry and pops it on return; 490300 (game,
//   unit) gives a missile's owner (unit+E8h type, +ECh GUID). A hit or a proc
//   of a player is a crossbow one when no aura pulse is running for the player
//   (flag above) and:
//     - the innermost missile in progress belongs to that player and belongs
//       to a tagged cast, or
//     - no missile of that player is in progress (skill functions, stored
//       melee records, procs outside missiles) and the player's current cast
//       id is tagged for that player.
//   A missile belongs to the cast its own id names, except (the call at
//   537A17 goes through a relay that also receives the creation params, r14,
//   owner at +8h, and links the new missile by GUID):
//     - created while one of the same owner's missiles is in progress under a
//       different id (its creator left +68h empty): the parent's cast
//     - created outside any missile under a newer id than the owner's current
//       cast (523370): the owner's current cast
//     - created during an aura pulse: no cast, never a crossbow missile
//   A link holds only while the missile still carries the id it was created
//   with. The game's own cast ids are never changed; missiles.txt LastCollide
//   keys its per-target dedup on them.
//
// ---------------------------------------------------------------------------
// 6. Where the bonus is applied
// ---------------------------------------------------------------------------
//   SUNITDMG_ExecuteEvents 44CE80 (game, attacker, defender, bMissile, damage)
//   recalculates only when bMissile is set, and every hit meets at 44CF98:
//     44CF83  85 DB                 test ebx,ebx               ; bMissile
//     44CF85  74 11                 je   44CF98
//     44CF87  4C 8B CF 4C 8B C6 49 8B D6 49 8B CF   r9 dmg, r8 def, rdx att, rcx game
//     44CF93  E8 78 0F 00 00        call 44DF10                ; CalculateTotalDamage
//     44CF98  0F B7 47 04           movzx eax, word [rdi+4]    ; every hit
//     44CF9C  A8 20                 test al,20h
//   Hits with bMissile clear were calculated before they got here (the
//   stored melee records calculate at 44B6AC). 44CF98..44CF9D becomes a call
//   to a relay plus a nop; the relay keeps every volatile register, hands
//   (r15 game, r14 attacker, rsi defender, rdi damage) to the hook, then
//   replays the two instructions, so eax and the flags are the game's own
//   when it returns. Nothing else branches to 44CF9C. After this point come
//   the damage events, leech, the life commit and the kill events, so the
//   bonus lands on the final numbers. Inside the calculation the bleed plugin
//   reads the physical damage at 44ECA5 and the poison immunity plugin builds
//   the life total at 44EC71, both before this point, so bleed sees the
//   unboosted hit and the poison rule is kept (the total is scaled as a
//   whole).
//   Scaled D2Damage fields: +18h physical, +20h fire, +24h burn, +2Ch
//   lightning, +30h magic, +34h cold, +38h poison, +134h life total, and the
//   damage of each per-source poison entry (pointer +40h, count +48h, 12-byte
//   entries, damage at +4). Crushing blow and open wounds are item events that
//   read life directly, so they are untouched.
//
// ---------------------------------------------------------------------------
// 7. The skill buttons
// ---------------------------------------------------------------------------
//   SkillSelectButtonWidget update 2399F0 (widget) runs every frame and calls
//   the refresh 239380, which leaves:
//     +2952 side (1 left, 2 right, controller buttons are right)
//     +3080 skill id (-1 without a skill), +3084 skill owner
//     +3056 QuantityText, +1820 current tint, +2956 normal tint,
//     +3004 cooldown tint (used for classifier code 8)
//   The number is written the way the refresh writes it:
//     visible through vtable +50h, text through 0CE510(text + 136, "%d", n),
//     or text + 512 when bytes +502 and +503 of the text box are both set.
//   The shadow child is found with 856220 (parent, name) and accepted only if
//   its type descriptor chain (vtable +58h, name at +8h, parent at +10h, as
//   built by the descriptor constructor 851030) contains "ImageWidget".
//   An image widget draws the frame at +90h (layout "frame"), with its
//   opacity in the float at +94h (layout "transparency", 1.0 = solid) and its
//   colour in the RGBA float tint at +9Ch (layout "tint"), per the
//   AbstractImageWidget property table at 2470090; 858C10 / 858C50 pass all
//   three to the draw. The tint's alpha does not make an image see-through,
//   the transparency field does.
//
//   The bolts and the shadow on a host (single player or the TCP/IP host) come
//   straight from the server pool in this process, so the shadow runs on the
//   real reload time of the bolt coming back next. A joined client rebuilds
//   the reload queue from the current-bolts stat and the globaldelay of its own
//   casts.
//   The skill node comes from 33DCD0 (unit, skillId, owner), the local player
//   from 9A480(8B2D0()).
//
// ---------------------------------------------------------------------------
// 8. Crossbow attack speed
// ---------------------------------------------------------------------------
//   UNITS_UpdateAnimRateAndVelocity 350B40 (unit) sets a unit's animation
//   rate whenever its mode or stats change. On the attack path, with rsi = the
//   unit, r12d = the animation speed ([[unit+70h]+0Ch]) and r15d = the rate
//   percent (attackrate + diminished IAS + passive bonus 213, -30 in mode 18,
//   clamped 15..175, 250 shapeshifted):
//     351482  41 8B CF B8 1F 85 EB 51 41 0F AF CC F7 E1 8B FA C1 EF 05
//             edi = rate% * AnimSpeed / 100
//     351597  BB FF 7F 00 00       mov ebx, 7FFFh        ; every path lands here
//     35159C  3B FB 0F 86 ...      cmp edi, ebx / jbe -> store at [unit+54h]
//   The wereform rescale in between (35149E..351596) also leaves its result
//   in edi and never writes rsi. The five-byte mov at 351597 becomes a call
//   to a relay that hands (unit, edi) to HookedAttackRate, takes the new rate
//   back into edi, redoes the mov and returns.
//   A frame of animation costs 256, so an attack of FramesPerDirection frames
//   ([[unit+70h]+8]) takes ceil(256 * FPD / rate) - 1 frames, the formula that
//   reproduces the published IAS tables. For the crossbow attack animation
//   (A1 = mode 7, mode at [unit+0Ch]) the rate becomes
//   ceil(256 * FPD / (N + 1)), which gives exactly N frames, with
//   N = default_attack_frames + the attack frames stat. The same function
//   runs on the server (timing) and on every client (animation), so both agree.
//   Everything else keeps the game's own speed: other sequence skills (mode
//   18), A2 (only the Assassin has an A2 crossbow animation) and the special
//   animations S1..S4, which skills such as Assassin traps use.
//   whirlwind-follow-cursor byte-checks 350B40's entry and faster-cast-rate-cap
//   patches the cast branch at 350C29; neither touches 351482..3515A3.
//
//   Mirrored Blades (skills.txt seqnum 25) is a sequence: the player stays in
//   mode 18 (SQ) and plays steps of another animation from a table compiled
//   into the exe (resolver 3CB890, table 2386650). Its crossbow slot (6) and
//   bow slot (5) share 18 steps of the cast animation (mode 10, frames 0..17,
//   hit event on step 12). Its speed comes through 351597 like A1: mode 18's
//   rate entry at 1D00518 sends skills with skills.txt UseAttackRate (record
//   byte +26h, bit 8h, tested at 351142) to the attack path, and a seqtrans
//   other than SC (record +32h, tested at 350CFB) keeps it off the cast path.
//     length   the installer 34D900 writes steps << 8 to unit+48h (3CB830) and
//              the countdown to unit+64h (3CB870 << 8); the mode start 34C880
//              calls it right before 350B40. Sequence 25's slots have equal
//              step counts, so both hold 18 << 8 with a crossbow.
//     advance  3476F1..347766 adds the rate at unit+54h to the position,
//              subtracts it from the countdown and flags the end (unit+128h
//              bit 1Bh) once the countdown reaches 0. A sequence therefore
//              lasts ceil(length / rate) frames, one frame more than a plain
//              animation with the same numbers.
//     skill    the used skill is [[unit+100h]+18h] (34BA40 -> 33E090), its id
//              the record's first word (33E080), as 350B40 reads it.
//   With that skill id equal to mirrored_blades_skill_id, the sequence
//   installed (unit+40h) and a crossbow held, the rate becomes
//   ceil(length / N): exactly N frames for the whole Mirrored Blades attack.
//
// ---------------------------------------------------------------------------
// Nothing calls into the game while the plugin loads
// ---------------------------------------------------------------------------
//   The UI type descriptors are built lazily, once, the first time their
//   getter runs. The ImageWidget getter 8599F0 looks its base type up by name
//   at that moment (853090("AbstractImageWidget")) and keeps whatever it got;
//   if the base is not registered yet it only asserts "Check the registration
//   order?" and builds ImageWidget with no parent. 1.0.0 called that getter at
//   plugin load, before the game had registered its widget types, so every
//   ImageWidget lost the Widget and AbstractImageWidget fields (rect, tint,
//   visibility and the rest) and the whole UI broke. Load now only checks
//   bytes and uses loader services; the type check reads descriptor names.
//
// ---------------------------------------------------------------------------
// Other plugins
// ---------------------------------------------------------------------------
//   No site here is touched by any plugin or static patch in the ESR set:
//   srcdam-calc-field hooks 43ACB0's entry, cast-on-cast patches 43AF06 to
//   43AF25, 43B0EC and 589C6F, soft-hits patches event slots 4 and 6, bleed
//   redirects 44ECA5, poison immunity patches inside 44EC71 to 44ECA4,
//   charge-aoe-hits hooks the entries of 5622C0 and 44B600, engine-stability
//   hooks 537B23 inside the missile creator and works inside the SrvHit
//   function 45FB50, critical-strike-damage works inside 44C030 and 465420.
//   whirlwind-follow-cursor calls 34F430 directly and reaches this hook.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr char PluginIdText[] = "celestialrayone.crossbow-charges";
constexpr char SourceFile[]   = "crossbow-charges";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.0 image)
// ---------------------------------------------------------------------------

// D2RCore.dll as shipped with D2RLoader 1.3.1: where event function 20 really runs.
constexpr wchar_t     CoreModuleName[]           = L"D2RCore.dll";
constexpr char        CoreRegisterExport[]       = "RegisterWideSkillEffect";
constexpr std::size_t CoreRegisterTableLeaOffset = 0x2C;  // lea rdi,[rip+disp32] -> handler table
constexpr std::size_t CoreEventFunc20Index       = 20;
// RegisterWideSkillEffect, 84 bytes up to its registrar call.
constexpr std::uint8_t CoreRegisterStub[]{
    0x56,0x57,0x48,0x83,0xEC,0x58,0x8B,0x84,0x24,0x90,0x00,0x00,0x00,0x44,0x8B,0x94,
    0x24,0x98,0x00,0x00,0x00,0x44,0x8B,0x9C,0x24,0xA0,0x00,0x00,0x00,0x8B,0xB4,0x24,
    0xA8,0x00,0x00,0x00,0x0F,0x28,0x84,0x24,0xB0,0x00,0x00,0x00,0x48,0x8D,0x3D,0xBD,
    0xCA,0xDF,0xFF,0x48,0x89,0x7C,0x24,0x50,0x0F,0x11,0x44,0x24,0x40,0x89,0x74,0x24,
    0x38,0x44,0x89,0x5C,0x24,0x30,0x44,0x89,0x54,0x24,0x28,0x89,0x44,0x24,0x20,0xE8,
    0xBC,0xEB,0xB9,0xFF,
};
// D2RCore's event function 20 handler, the whole 96 bytes.
constexpr std::uint8_t CoreEventFunc20Body[]{
    0x48,0x83,0xEC,0x48,0x4C,0x89,0xC0,0x49,0x89,0xC8,0x0F,0x28,0x44,0x24,0x70,0x48,
    0x8B,0x0D,0x0A,0xA1,0x32,0x00,0x48,0x31,0xE1,0x48,0x89,0x4C,0x24,0x40,0x0F,0x11,
    0x44,0x24,0x28,0x4C,0x89,0x4C,0x24,0x20,0x48,0x8D,0x4C,0x24,0x3F,0xBA,0x01,0x00,
    0x00,0x00,0x49,0x89,0xC1,0xE8,0x16,0xD5,0x00,0x00,0x48,0x8B,0x4C,0x24,0x40,0x48,
    0x31,0xE1,0x48,0x8B,0x15,0xD7,0xA0,0x32,0x00,0x48,0x39,0xCA,0x75,0x05,0x48,0x83,
    0xC4,0x48,0xC3,0x48,0x8B,0x4C,0x24,0x40,0x48,0x31,0xE1,0xE8,0x70,0x60,0x16,0x00,
};
static_assert(CoreRegisterStub[CoreRegisterTableLeaOffset] == 0x48 && CoreRegisterStub[CoreRegisterTableLeaOffset + 1] == 0x8D
    && CoreRegisterStub[CoreRegisterTableLeaOffset + 2] == 0x3D, "The table lea is not lea rdi,[rip+disp32].");

// Hooked
constexpr std::uint64_t CooldownGateRva       = 0x3404A0;
constexpr std::uint64_t ClientApplyRva        = 0x215FC0;
constexpr std::uint64_t ServerApplyRva        = 0x436C70;
constexpr std::uint64_t PlayerRegenRva        = 0x42E600;
constexpr std::uint64_t SetUsedSkillRva       = 0x34F430;
constexpr std::uint64_t ButtonUpdateRva       = 0x2399F0;
constexpr std::uint64_t MissileHitRva         = 0x4639A0;
constexpr std::uint64_t MissileDoRva          = 0x466CE0;
constexpr std::uint64_t MissileDamageRva      = 0x462E40;
// Redirected calls and table slot
constexpr std::uint64_t DoHandlerWindowRva    = 0x43B163;
constexpr std::uint64_t DoHandlerCallRva      = 0x43B18D;
constexpr std::uint64_t ProcCastIdWindowRva   = 0x589C34;
constexpr std::uint64_t ProcCastIdCallRva     = 0x589C37;
constexpr std::uint64_t HitBonusWindowRva     = 0x44CF78;
constexpr std::uint64_t HitBonusSiteRva       = 0x44CF98;
constexpr std::uint64_t MissileCastIdWindowRva = 0x5379F5;
constexpr std::uint64_t MissileCastIdCallRva  = 0x537A17;
constexpr std::uint64_t EventFuncSlot20Rva    = 0x238E660;
constexpr std::uint64_t AttackRateWindowRva   = 0x351482;
constexpr std::uint64_t AttackRateSiteRva     = 0x351597;
// Read only: prove the sequence fields the Mirrored Blades part reads
constexpr std::uint64_t SequenceGateRva        = 0x351102;
constexpr std::uint64_t SequenceModeEntryRva   = 0x1D00518;
constexpr std::uint64_t SequenceInstallRva     = 0x34D948;
constexpr std::uint64_t SequenceAdvanceRva     = 0x3476F1;
constexpr std::uint64_t UsedSkillRva           = 0x34BA61;
constexpr std::uint64_t UsedSkillFromListRva   = 0x33E090;
// Called
constexpr std::uint64_t ServerGlobalDelayRva  = 0x439470;
constexpr std::uint64_t ServerLocalDelayRva   = 0x439500;
constexpr std::uint64_t ClientGlobalDelayRva  = 0x217B30;
constexpr std::uint64_t ClientLocalDelayRva   = 0x217B90;
constexpr std::uint64_t MissileOwnerRva       = 0x490300;
constexpr std::uint64_t SetCastIdRva          = 0x3E2B652;  // loader thunk (3E2B57E in 1.3.0)
constexpr std::uint64_t EventFunc20Rva        = 0x583B30;
constexpr std::uint64_t GenerateCastIdRva     = 0x3E2B64C;  // loader thunk (3E2B578 in 1.3.0)
constexpr std::uint64_t SkillsRecordRva       = 0x097790;
constexpr std::uint64_t EvaluateFormulaRva    = 0x3B5160;
constexpr std::uint64_t DataContextRva        = 0x34A0E0;
constexpr std::uint64_t UnitTypeRva           = 0x34B9D0;
constexpr std::uint64_t UnitIdRva             = 0x34A330;
constexpr std::uint64_t InventoryRva          = 0x34A360;
constexpr std::uint64_t LeftHandWeaponRva     = 0x387690;
constexpr std::uint64_t CheckItemTypeRva      = 0x373890;
constexpr std::uint64_t GetUnitStatRva        = 0x2F5020;  // loader thunk
constexpr std::uint64_t SetUnitStatRva        = 0x2F7D10;  // loader thunk
constexpr std::uint64_t CheckStateRva         = 0x3351B0;
constexpr std::uint64_t StateStatListRva      = 0x2F5940;
constexpr std::uint64_t ReadListStatRva       = 0x2F5DF0;  // loader thunk
constexpr std::uint64_t SkillLevelRva         = 0x33D1E0;  // loader thunk
constexpr std::uint64_t SkillNodeRva          = 0x33DCD0;
constexpr std::uint64_t LocalPlayerIndexRva   = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva    = 0x09A480;
constexpr std::uint64_t FindChildWidgetRva    = 0x856220;
constexpr std::uint64_t StringFormatRva       = 0x0CE510;

// Game and unit layout
constexpr std::size_t GameFrameOffset       = 0x170;
constexpr std::size_t GameDataContextOffset = 0x106;
constexpr std::size_t UnitCastIdOffset      = 0x12C;
constexpr std::size_t UnitModeOffset        = 0x0C;
constexpr std::int32_t NeutralMode           = 1;   // NU
constexpr std::int32_t TownNeutralMode       = 5;   // TN
constexpr std::size_t UnitAnimRecordOffset  = 0x70;
constexpr std::size_t AnimRecordFramesOffset = 0x08;
constexpr std::size_t UnitSequenceRowsOffset   = 0x40;
constexpr std::size_t UnitSequenceLengthOffset = 0x48;
constexpr std::size_t UnitSkillListOffset      = 0x100;
constexpr std::size_t SkillListUsedOffset      = 0x18;
constexpr std::int32_t SequenceMode            = 18;
constexpr std::size_t SkillGlobalDelayCalc  = 564;
constexpr std::size_t SkillLocalDelayCalc   = 568;
constexpr std::int32_t LocalCooldownState   = 185;
constexpr std::int32_t LocalCooldownStat    = 359;
constexpr std::uint32_t MissileUnitType     = 3;
constexpr std::size_t CreateParamsOwnerOffset = 0x08;  // missile creation params: owner unit
constexpr std::size_t UnitClassOffset       = 0x04;  // 349860 returns it: missiles.txt / monstats row, player class
constexpr std::size_t UnitFlagsExOffset     = 0x128; // second flag word, written by 34E140 (unit, flag, set)
constexpr std::uint32_t AuraPulseFlag       = 0x20000000;  // set by the aura events around every pulse
constexpr std::uint32_t ServerUnitFlag      = 0x04000000;  // set by the server unit allocator 4905A0 only

// SkillSelectButtonWidget and child widgets
constexpr std::size_t ButtonSideOffset         = 2952;
constexpr std::size_t ButtonSkillIdOffset      = 3080;
constexpr std::size_t ButtonSkillOwnerOffset   = 3084;
constexpr std::size_t ButtonQuantityOffset     = 3056;
constexpr std::size_t ButtonTintOffset         = 1820;
constexpr std::size_t ButtonNormalTintOffset   = 2956;
constexpr std::size_t ButtonCooldownTintOffset = 3004;
constexpr std::size_t TextAltFlagA             = 502;
constexpr std::size_t TextAltFlagB             = 503;
constexpr std::size_t TextMainString           = 136;
constexpr std::size_t TextAltString            = 512;
constexpr std::size_t ImageFrameOffset         = 0x90;
constexpr std::size_t ImageTransparencyOffset  = 0x94;
constexpr std::size_t ImageTintOffset          = 0x9C;
constexpr std::size_t WidgetSetVisibleSlot     = 0x50 / 8;
constexpr std::size_t WidgetTypeDescSlot       = 0x58 / 8;
constexpr std::size_t TypeDescParentOffset     = 0x10;

// D2Damage
constexpr std::array<std::size_t, 8> DamageFields{ 0x18, 0x20, 0x24, 0x2C, 0x30, 0x34, 0x38, 0x134 };
constexpr std::size_t DamagePoisonEntries = 0x40;
constexpr std::size_t DamagePoisonCount   = 0x48;
constexpr std::size_t PoisonEntrySize     = 12;
constexpr std::size_t PoisonEntryDamage   = 4;

constexpr const char*   PluginVersion       = "1.4.0";

constexpr std::uint64_t MsPerFrame          = 40;
constexpr std::uint64_t PredictionTimeoutMs = 1500;
constexpr std::size_t   CallSize            = 5;

// ---------------------------------------------------------------------------
// Byte witnesses
// ---------------------------------------------------------------------------

constexpr std::uint8_t CooldownGateBytes[]{
    0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xDA,0x48,0x8B,0xF9 };
constexpr std::uint8_t ClientApplyBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30 };
constexpr std::uint8_t ServerApplyBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30 };
constexpr std::uint8_t PlayerRegenBytes[]{
    0x48,0x89,0x5C,0x24,0x20,0x55,0x48,0x83,0xEC,0x40,0x41,0x8B,0xC1,0xC7,0x44,0x24,
    0x30,0x00,0x00,0x00,0x00 };
constexpr std::uint8_t SetUsedSkillBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xF8,0x48,0x8B,0xF2,0x48,0x8B,0xD9 };
constexpr std::uint8_t ButtonUpdateBytes[]{
    0x40,0x53,0x57,0x41,0x56,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9 };

// 43B163..43B19B: skill id in r13d, globaldelay evaluated, then the call.
constexpr std::uint8_t DoHandlerWindow[]{
    0x45,0x8B,0xCD,0x44,0x8B,0x86,0x34,0x02,0x00,0x00,0x48,0x8B,0xD3,0x0F,0xB6,0x8F,
    0x06,0x01,0x00,0x00,0x89,0x6C,0x24,0x20,0xE8,0xE0,0x9F,0xF7,0xFF,0x85,0xC0,0x7E,
    0x0E,0x44,0x8B,0xC0,0x48,0x8B,0xD3,0x48,0x8B,0xCF,0xE8,0xDE,0xE2,0xFF,0xFF,0x44,
    0x8B,0x86,0x38,0x02,0x00,0x00,0x45,0x8B,0xCD };
constexpr std::size_t DoHandlerCallOffset = DoHandlerCallRva - DoHandlerWindowRva;

// 589C34..589C45: generate the proc's cast id, then set it on the caster.
// Both calls go through the loader's thunks at 3E2B64C and 3E2B652.
constexpr std::uint8_t ProcCastIdWindow[]{
    0x49,0x8B,0xCF,0xE8,0x10,0x1A,0x8A,0x03,0x8B,0xD0,0x48,0x8B,0xCB,0xE8,0x0C,0x1A,
    0x8A,0x03 };
constexpr std::size_t ProcCastIdCallOffset = ProcCastIdCallRva - ProcCastIdWindowRva;

// 44CF78..44CFA3: the pre-hit call (rdx attacker r14, rcx defender rsi), the
// bMissile test on ebx, the recalculation call with r15/r14/rsi/rdi, then the
// point every hit reaches: movzx eax,word [rdi+4] / test al,20h, whose six
// bytes are replaced, and the two branches that read those flags.
constexpr std::uint8_t HitBonusWindow[]{
    0x49,0x8B,0xD6,0x48,0x8B,0xCE,0xE8,0xDD,0x0B,0xF0,0xFF,0x85,0xDB,0x74,0x11,0x4C,
    0x8B,0xCF,0x4C,0x8B,0xC6,0x49,0x8B,0xD6,0x49,0x8B,0xCF,0xE8,0x78,0x0F,0x00,0x00,
    0x0F,0xB7,0x47,0x04,0xA8,0x20,0x75,0x61,0xA8,0x01,0x74,0x5D };
constexpr std::size_t HitBonusSiteOffset = HitBonusSiteRva - HitBonusWindowRva;
constexpr std::size_t HitBonusPatchSize  = 6;
static_assert(HitBonusSiteOffset == 0x20 && HitBonusWindow[HitBonusSiteOffset] == 0x0F
    && HitBonusWindow[HitBonusSiteOffset + 4] == 0xA8, "The hit bonus site is not movzx / test.");

// 5379F5..537A22 in the missile creator 5371A0 (r14 = creation params, r15 =
// the new missile): eax = params+68h, or the owner's (params+8h) current cast
// id through 34B6C0, or -1; then SetUnitCastId(missile, eax), the call that
// goes through the relay.
constexpr std::uint8_t MissileCastIdWindow[]{
    0x41,0x8B,0x46,0x68,0x85,0xC0,0x75,0x15,0x49,0x8B,0x4E,0x08,0x48,0x85,0xC9,0x74,
    0x07,0xE8,0xB5,0x3C,0xE1,0xFF,0xEB,0x05,0xB8,0xFF,0xFF,0xFF,0xFF,0x8B,0xD0,0x49,
    0x8B,0xCF,0xE8,0x36,0x3C,0x8F,0x03,0x41,0x8B,0x56,0x3C,0x49,0x8B,0xCF };
constexpr std::size_t MissileCastIdCallOffset = MissileCastIdCallRva - MissileCastIdWindowRva;
static_assert(MissileCastIdCallOffset == 0x22 && MissileCastIdWindow[MissileCastIdCallOffset] == 0xE8,
    "The missile cast id store is not a call.");

// 351482..35149D: edi = rate% (r15d) * animation speed (r12d) / 100, rsi = unit.
constexpr std::uint8_t AttackRateWindow[]{
    0x41,0x8B,0xCF,0xB8,0x1F,0x85,0xEB,0x51,0x41,0x0F,0xAF,0xCC,0xF7,0xE1,0x8B,0xFA,
    0xC1,0xEF,0x05,0x44,0x39,0x36,0x0F,0x85,0xF9,0x00,0x00,0x00 };
// 351597..3515A3: mov ebx,7FFFh / cmp edi,ebx / jbe store. The first five bytes are replaced.
constexpr std::uint8_t AttackRateSite[]{
    0xBB,0xFF,0x7F,0x00,0x00,0x3B,0xFB,0x0F,0x86,0xB8,0xF6,0xFF,0xFF };

// 351102..351151: mode 18's rate entry, then the UseAttackRate test on the used
// skill ([rsi+100h] -> 33E090 -> 33E080 -> skills.txt record, byte +26h & 8)
// that sends sequences to the attack path.
constexpr std::uint8_t SequenceGateBytes[]{
    0x44,0x39,0x70,0x0C,0x0F,0x85,0x33,0x01,0x00,0x00,0x44,0x39,0x70,0x08,0x74,0x47,
    0x48,0x8B,0x8E,0x00,0x01,0x00,0x00,0xE8,0x72,0xCF,0xFE,0xFF,0x48,0x85,0xC0,0x74,
    0x2F,0x41,0xB8,0x08,0x0E,0x00,0x00,0x49,0x8B,0xD5,0x48,0x8B,0xC8,0xE8,0x4C,0xCF,
    0xFE,0xFF,0x0F,0xB6,0x8E,0xBD,0x01,0x00,0x00,0x8B,0xD0,0xE8,0x4E,0x66,0xD4,0xFF,
    0x0F,0xB6,0x48,0x26,0x23,0x0D,0x90,0x85,0xA4,0x01,0x0F,0x85,0xED,0x00,0x00,0x00 };
// Rate entry of mode 18 in the player mode table 1D003B0 (20-byte rows): move
// test on, attack rate when the skill asks for it, no fixed speed.
constexpr std::uint8_t SequenceModeEntryBytes[]{
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00 };
// 34D948..34D97E: unit+48h = steps << 8, unit+54h = 256, unit+4Ch = 0,
// unit+64h = countdown << 8, then the first step.
constexpr std::uint8_t SequenceInstallBytes[]{
    0x48,0x83,0x7B,0x40,0x00,0x74,0x3A,0x48,0x8B,0xCB,0xE8,0xD9,0xDE,0x07,0x00,0x89,
    0x43,0x48,0x48,0x8B,0xCB,0x33,0xC0,0xC7,0x43,0x54,0x00,0x01,0x00,0x00,0x48,0x89,
    0x43,0x4C,0xE8,0x01,0xDF,0x07,0x00,0xC1,0xE0,0x08,0x33,0xD2,0x48,0x8B,0xCB,0x89,
    0x43,0x64,0xE8,0xF1,0x3F,0x00,0x00 };
// 3476F1..347766: position += rate (unit+54h), wrapped by unit+48h; countdown
// unit+64h -= rate; end flag (unit+128h bit 1Bh) once the countdown is <= 0.
constexpr std::uint8_t SequenceAdvanceBytes[]{
    0x48,0x83,0x7B,0x40,0x00,0xC6,0x43,0x6A,0x00,0x74,0x76,0x8B,0x4B,0x4C,0x66,0x0F,
    0x6E,0x43,0x54,0x81,0xA3,0x24,0x01,0x00,0x00,0xFF,0xBF,0xFF,0xFF,0x44,0x8B,0x4B,
    0x48,0x0F,0x5B,0xC0,0x89,0x4B,0x50,0xF3,0x0F,0x59,0xC6,0xF3,0x4C,0x0F,0x2C,0xC0,
    0x41,0x8D,0x04,0x08,0x89,0x43,0x4C,0x41,0x3B,0xC1,0x7C,0x07,0x99,0x41,0xF7,0xF9,
    0x89,0x53,0x4C,0x44,0x29,0x43,0x64,0x8B,0xD1,0x48,0x8B,0xCB,0xE8,0x2E,0xA2,0x00,
    0x00,0x85,0xC0,0x74,0x0A,0x81,0x8B,0x24,0x01,0x00,0x00,0x00,0x40,0x00,0x00,0x8B,
    0x8B,0x28,0x01,0x00,0x00,0x8B,0xC1,0x0F,0xBA,0xF0,0x1B,0x0F,0xBA,0xE9,0x1B,0x83,
    0x7B,0x64,0x00,0x0F,0x4F,0xC8 };
// 34BA61..34BA71: mov rcx,[rbx+100h] / add rsp,20h / pop rbx / jmp 33E090.
constexpr std::uint8_t UsedSkillBytes[]{
    0x48,0x8B,0x8B,0x00,0x01,0x00,0x00,0x48,0x83,0xC4,0x20,0x5B,0xE9,0x1E,0x26,0xFF,
    0xFF };
// 33E090..33E09C: test rcx,rcx / jnz / xor eax,eax / ret / mov rax,[rcx+18h] / ret.
constexpr std::uint8_t UsedSkillFromListBytes[]{
    0x48,0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0x48,0x8B,0x41,0x18,0xC3 };

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

constexpr std::uint8_t ServerGlobalDelayBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,
    0x8B,0xFA,0x41,0x8B,0xD8,0x48,0x8B,0xF1 };
constexpr std::uint8_t ServerLocalDelayBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
constexpr std::uint8_t ClientGlobalDelayBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA };
constexpr std::uint8_t ClientLocalDelayBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
// 34E161..34E173 in the unit flag setter 34E140 (unit, flag, set): the flag
// word is unit+128h (mov eax,[rcx+128h] / or eax,edx / mov [rcx+128h],eax).
constexpr std::uint64_t UnitFlagSetterRva = 0x34E161;
constexpr std::uint8_t UnitFlagSetterBytes[]{
    0x8B,0x81,0x28,0x01,0x00,0x00,0x45,0x85,0xC0,0x74,0x0D,0x0B,0xC2,0x89,0x81,0x28,
    0x01,0x00,0x00 };
// 43754A..437592 in the skill aura event 437460 (game, unit, customId, param,
// castId): 34E140(unit, 20000000h, 1), the aura's do-handler 43ACB0, then
// 34E140(unit, 20000000h, 0). The item aura event 437230 and the aura start
// 43B680 bracket their do-handler calls the same way; the proc caster 589930
// does not.
constexpr std::uint64_t AuraPulseWindowRva = 0x43754A;
constexpr std::uint8_t AuraPulseWindow[]{
    0xBA,0x00,0x00,0x00,0x20,0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0xE3,
    0x6B,0xF1,0xFF,0x45,0x33,0xF6,0x45,0x8B,0xCC,0x44,0x89,0x74,0x24,0x30,0x44,0x8B,
    0xC5,0x44,0x89,0x74,0x24,0x28,0x48,0x8B,0xD7,0x48,0x8B,0xCE,0xC7,0x44,0x24,0x20,
    0x01,0x00,0x00,0x00,0xE8,0x2D,0x37,0x00,0x00,0x45,0x33,0xC0,0xBA,0x00,0x00,0x00,
    0x20,0x48,0x8B,0xCF,0xE8,0xAD,0x6B,0xF1,0xFF };

// Hooked missile functions, position independent prologues up to an
// instruction boundary.
constexpr std::uint8_t MissileHitBytes[]{
    0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
constexpr std::uint8_t MissileDoBytes[]{
    0x40,0x53,0x56,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x8B,0xF1,0x48,
    0x8B,0xCB };
constexpr std::uint8_t MissileDamageBytes[]{
    0x40,0x53,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x38,0x49,0x8B,0xD9,0x4D,0x8B,
    0xF8,0x48,0x8B,0xFA,0x4C,0x8B,0xF1 };
// 490300 (game, unit): the owner through unit+E8h type / +ECh GUID.
constexpr std::uint8_t MissileOwnerBytes[]{
    0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xF1,
    0x48,0x85,0xD2,0x75,0x20 };
constexpr std::uint8_t EventFunc20Bytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x54,0x41,0x56 };
constexpr std::uint8_t LoaderThunkBytes[]{ 0xFF,0x25 };
constexpr std::uint8_t SkillsRecordBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x48,
    0x63,0xF2 };
constexpr std::uint8_t EvaluateFormulaBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,
    0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00 };
constexpr std::uint8_t DataContextBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30 };
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C };
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30 };
// 4906A4..4906D4 in the server unit allocator 4905A0 (game, params, ...),
// right after the common allocation: type, class, the game at unit+D8h, the
// data context, then 34E140(unit, 4000000h, 1). Only server units carry that
// flag; the client allocators (98DF0, 98FA0, ...) never set it.
constexpr std::uint64_t ServerUnitAllocRva = 0x4906A4;
constexpr std::uint8_t ServerUnitAllocBytes[]{
    0x44,0x89,0x30,0x48,0x8B,0xCF,0x44,0x89,0x68,0x04,0x4C,0x89,0xB8,0xD8,0x00,0x00,
    0x00,0x41,0x0F,0xB6,0x97,0x06,0x01,0x00,0x00,0xE8,0x1E,0xDB,0xEB,0xFF,0xBA,0x00,
    0x00,0x00,0x04,0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0x6B,0xDA,0xEB,
    0xFF };
// 34C2F2..34C327 in UNITS_IsDead 34C2C0: type 0 (player) jumps to 34C31C,
// mov eax,[rcx+0Ch] / test eax,eax / jz / cmp eax,11h; type 1 (monster)
// reads the same dword at 34C310 and compares it with 0Ch. Read only: proves
// the mode field the standing check reads.
constexpr std::uint64_t UnitModeWitnessRva = 0x34C2F2;
constexpr std::uint8_t UnitModeWitnessBytes[]{
    0x8B,0x11,0x85,0xD2,0x74,0x24,0x83,0xFA,0x01,0x74,0x13,0xF7,0x81,0x28,0x01,0x00,
    0x00,0x00,0x00,0x00,0x04,0x75,0x1F,0x33,0xC0,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,
    0x0C,0x85,0xC0,0x74,0x11,0x83,0xF8,0x0C,0xEB,0x0A,0x8B,0x41,0x0C,0x85,0xC0,0x74,
    0x05,0x83,0xF8,0x11,0x75,0xE1 };
constexpr std::uint8_t InventoryBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9 };
constexpr std::uint8_t LeftHandWeaponBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,
    0x75,0x24 };
constexpr std::uint8_t CheckItemTypeBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20 };
constexpr std::uint8_t CheckStateBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,
    0xDA,0x48,0x8B,0xF1 };
constexpr std::uint8_t StateStatListBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0x48,
    0x85,0xC9,0x75,0x13 };
constexpr std::uint8_t SkillNodeBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x41,0x8B,0xD8,0x8B,0xFA };
constexpr std::uint8_t LocalPlayerIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
constexpr std::uint8_t PlayerFromIndexBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08 };
constexpr std::uint8_t FindChildWidgetBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0x59,0x58 };
constexpr std::uint8_t StringFormatBytes[]{
    0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x4C,0x89,0x40,0x18,0x4C,0x89,0x48,0x20,0x53,
    0x55,0x56,0x41,0x56,0x48,0x83,0xEC,0x48 };

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name };
}

const std::array<Witness, 40> Witnesses{
    W(CooldownGateRva, CooldownGateBytes, "cooldown gate"),
    W(ClientApplyRva, ClientApplyBytes, "client cooldown step"),
    W(ServerApplyRva, ServerApplyBytes, "server cooldown step"),
    W(PlayerRegenRva, PlayerRegenBytes, "player per-frame event"),
    W(SetUsedSkillRva, SetUsedSkillBytes, "skill start"),
    W(DoHandlerWindowRva, DoHandlerWindow, "skill do-handler shared cooldown call"),
    W(ProcCastIdWindowRva, ProcCastIdWindow, "proc caster cast id"),
    W(HitBonusWindowRva, HitBonusWindow, "hit bonus site"),
    W(MissileCastIdWindowRva, MissileCastIdWindow, "missile cast id store"),
    W(MissileHitRva, MissileHitBytes, "missile hit resolution"),
    W(MissileDoRva, MissileDoBytes, "missile do dispatcher"),
    W(MissileDamageRva, MissileDamageBytes, "missile damage"),
    W(ServerGlobalDelayRva, ServerGlobalDelayBytes, "server shared cooldown"),
    W(ServerLocalDelayRva, ServerLocalDelayBytes, "server skill cooldown"),
    W(ClientGlobalDelayRva, ClientGlobalDelayBytes, "client shared cooldown"),
    W(ClientLocalDelayRva, ClientLocalDelayBytes, "client skill cooldown"),
    W(MissileOwnerRva, MissileOwnerBytes, "missile owner"),
    W(SetCastIdRva, LoaderThunkBytes, "cast id setter"),
    W(UnitFlagSetterRva, UnitFlagSetterBytes, "unit flag setter"),
    W(AuraPulseWindowRva, AuraPulseWindow, "aura pulse flag"),
    W(EventFunc20Rva, EventFunc20Bytes, "item event function 20"),
    W(GenerateCastIdRva, LoaderThunkBytes, "cast id generator"),
    W(SkillsRecordRva, SkillsRecordBytes, "skills.txt record"),
    W(EvaluateFormulaRva, EvaluateFormulaBytes, "skill formula"),
    W(DataContextRva, DataContextBytes, "data context"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(UnitIdRva, UnitIdBytes, "unit id"),
    W(ServerUnitAllocRva, ServerUnitAllocBytes, "server unit flag"),
    W(InventoryRva, InventoryBytes, "inventory"),
    W(LeftHandWeaponRva, LeftHandWeaponBytes, "attack weapon"),
    W(CheckItemTypeRva, CheckItemTypeBytes, "item type check"),
    W(GetUnitStatRva, LoaderThunkBytes, "stat read"),
    W(SetUnitStatRva, LoaderThunkBytes, "stat write"),
    W(CheckStateRva, CheckStateBytes, "state check"),
    W(StateStatListRva, StateStatListBytes, "state stat list"),
    W(ReadListStatRva, LoaderThunkBytes, "list stat read"),
    W(SkillLevelRva, LoaderThunkBytes, "skill level"),
    W(SkillNodeRva, SkillNodeBytes, "skill node"),
    W(LocalPlayerIndexRva, LocalPlayerIndexBytes, "local player index"),
    W(PlayerFromIndexRva, PlayerFromIndexBytes, "player from index"),
};

const std::array<Witness, 6> SequenceWitnesses{
    W(SequenceGateRva, SequenceGateBytes, "sequence attack rate gate"),
    W(SequenceModeEntryRva, SequenceModeEntryBytes, "sequence mode rate entry"),
    W(SequenceInstallRva, SequenceInstallBytes, "sequence install"),
    W(SequenceAdvanceRva, SequenceAdvanceBytes, "sequence advance"),
    W(UsedSkillRva, UsedSkillBytes, "used skill"),
    W(UsedSkillFromListRva, UsedSkillFromListBytes, "used skill from list"),
};

const std::array<Witness, 3> UiWitnesses{
    W(ButtonUpdateRva, ButtonUpdateBytes, "skill button update"),
    W(FindChildWidgetRva, FindChildWidgetBytes, "child widget lookup"),
    W(StringFormatRva, StringFormatBytes, "string format"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

using GateFn           = std::int32_t(__fastcall*)(void* unit, void* skill);
using ClientApplyFn    = void(__fastcall*)(void* unit, std::int32_t skillId, std::int32_t level);
using ServerApplyFn    = void(__fastcall*)(void* game, void* unit, std::int32_t skillId, std::int32_t level);
using PlayerRegenFn    = std::uint64_t(__fastcall*)(void* game, void* unit, std::int32_t a3, std::int32_t a4);
using SetUsedSkillFn   = std::uint64_t(__fastcall*)(void* unit, void* skill, std::uint32_t castId);
using ButtonUpdateFn   = std::uint64_t(__fastcall*)(void* widget);
using ServerGlobalFn   = std::uint64_t(__fastcall*)(void* game, void* unit, std::int32_t frames);
using ServerLocalFn    = std::uint64_t(__fastcall*)(void* game, void* unit, std::int32_t frames, std::int32_t skillId);
using ClientGlobalFn   = std::uint64_t(__fastcall*)(void* unit, std::int32_t frames);
using ClientLocalFn    = std::uint64_t(__fastcall*)(void* unit, std::int32_t frames, std::int32_t skillId);
using MissileHitFn     = std::uint64_t(__fastcall*)(void* game, void* missile, void* target, std::int32_t forced);
using MissileDoFn      = std::uint64_t(__fastcall*)(void* game, void* missile);
using MissileDamageFn  = std::uint64_t(__fastcall*)(void* game, void* missile, void* target, void* damage);
using MissileOwnerFn   = void*(__fastcall*)(void* game, void* unit);
using SetCastIdFn      = void(__fastcall*)(void* unit, std::uint32_t castId);
using EventFunc20Fn    = std::uint64_t(__fastcall*)(void* game, std::uint64_t event, void* owner, void* target,
                             void* damage, std::uint64_t packedStat, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9);
using GenerateCastIdFn = std::uint32_t(__fastcall*)(void* game);
using SkillsRecordFn   = void*(__fastcall*)(std::uint8_t context, std::int32_t skillId);
using EvaluateFn       = std::int32_t(__fastcall*)(std::uint8_t context, void* unit, std::uint32_t calc,
                             std::int32_t skillId, std::int32_t level);
using DataContextFn    = std::uint8_t(__fastcall*)(void* unit);
using UnitTypeFn       = std::uint32_t(__fastcall*)(void* unit);
using UnitIdFn         = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using InventoryFn      = void*(__fastcall*)(void* unit, const char* file, std::int32_t line);
using WeaponFn         = void*(__fastcall*)(void* inventory);
using CheckItemTypeFn  = std::int32_t(__fastcall*)(void* item, std::int32_t itemType);
using GetStatFn        = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using SetStatFn        = std::uint8_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);
using CheckStateFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t state);
using StateStatListFn  = void*(__fastcall*)(void* unit, std::int32_t state);
using ReadListStatFn   = std::int32_t(__fastcall*)(std::uint8_t context, void* list, std::int32_t stat, std::int32_t layer);
using SkillLevelFn     = std::int32_t(__fastcall*)(void* unit, void* skill, std::uint64_t bonus, std::uint64_t unused);
using SkillNodeFn      = void*(__fastcall*)(void* unit, std::int32_t skillId, std::uint32_t owner);
using PlayerIndexFn    = std::int32_t(__fastcall*)();
using PlayerFromFn     = void*(__fastcall*)(std::int32_t index);
using FindChildFn      = void*(__fastcall*)(void* parent, const char* name);
using StringFormatFn   = void*(*)(void* target, const char* format, ...);
using SetVisibleFn     = void(__fastcall*)(void* widget, std::uint64_t visible);
using TypeDescFn       = void*(__fastcall*)(void* widget);

GateFn          OriginalGate{};
ClientApplyFn   OriginalClientApply{};
ServerApplyFn   OriginalServerApply{};
PlayerRegenFn   OriginalPlayerRegen{};
SetUsedSkillFn  OriginalSetUsedSkill{};
ButtonUpdateFn  OriginalButtonUpdate{};
MissileHitFn    OriginalMissileHit{};
MissileDoFn     OriginalMissileDo{};
MissileDamageFn OriginalMissileDamage{};

ServerGlobalFn   ServerGlobalDelay{};
ServerLocalFn    ServerLocalDelay{};
ClientGlobalFn   ClientGlobalDelay{};
ClientLocalFn    ClientLocalDelay{};
MissileOwnerFn   MissileOwner{};
SetCastIdFn      SetUnitCastId{};
EventFunc20Fn    NativeEventFunc20{};
GenerateCastIdFn GenerateCastId{};
SkillsRecordFn   SkillsRecord{};
EvaluateFn       Evaluate{};
DataContextFn    DataContext{};
UnitTypeFn       UnitType{};
UnitIdFn         UnitId{};
InventoryFn      Inventory{};
WeaponFn         LeftHandWeapon{};
CheckItemTypeFn  CheckItemType{};
GetStatFn        GetStat{};
SetStatFn        SetStat{};
CheckStateFn     CheckState{};
StateStatListFn  StateStatList{};
ReadListStatFn   ReadListStat{};
SkillLevelFn     SkillLevel{};
SkillNodeFn      SkillNode{};
PlayerIndexFn    LocalPlayerIndex{};
PlayerFromFn     PlayerFromIndex{};
FindChildFn      FindChild{};
StringFormatFn   StringFormat{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PluginState { NotLoaded, DisabledByConfig, UnsupportedBuild, InstallFailed, Active };
PluginState State{ PluginState::NotLoaded };
std::atomic<bool> Active{};
bool UiActive{};

// Counters, shown by the console command and zeroed by "crossbowcharges reset".
std::atomic<std::uint64_t> BoltsSpent{};
std::atomic<std::uint64_t> BoltsRefused{};
std::atomic<std::uint64_t> RepeatStepsIgnored{};
std::atomic<std::uint64_t> BoltsReloaded{};
std::atomic<std::uint64_t> SkillStarts{};         // player skill starts on the server
std::atomic<std::uint64_t> CastsTagged{};         // ... that were crossbow skills
std::atomic<std::uint64_t> CastsTaggedAtSpend{};  // casts first tagged when their bolt was spent
std::atomic<std::uint64_t> ProcsSeen{};
std::atomic<std::uint64_t> ProcsTagged{};
std::atomic<std::uint64_t> PlayerMissiles{};
std::atomic<std::uint64_t> CrossbowMissiles{};
std::atomic<std::uint64_t> LinkedMissiles{};      // following a parent missile or their cast
std::atomic<std::uint64_t> PlayerHits{};
std::atomic<std::uint64_t> MissileHitsBoosted{};
std::atomic<std::uint64_t> OtherHitsBoosted{};
std::atomic<std::uint64_t> HitsSkippedAura{};
std::atomic<std::uint64_t> HitsSkippedMissile{};
std::atomic<std::uint64_t> HitsSkippedCast{};
std::atomic<std::uint64_t> MissileDepthLeaks{};
std::atomic<std::uint64_t> SequenceRatesSet{};

void ResetCounters() {
    for (std::atomic<std::uint64_t>* counter : { &BoltsSpent, &BoltsRefused, &RepeatStepsIgnored, &BoltsReloaded,
            &SkillStarts, &CastsTagged, &CastsTaggedAtSpend, &ProcsSeen, &ProcsTagged, &PlayerMissiles,
            &CrossbowMissiles, &LinkedMissiles, &PlayerHits, &MissileHitsBoosted, &OtherHitsBoosted,
            &HitsSkippedAura, &HitsSkippedMissile, &HitsSkippedCast, &MissileDepthLeaks, &SequenceRatesSet }) {
        counter->store(0, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Crossbow Charges\n"
    "#\n"
    "# Crossbow skills fire from a pool of bolts instead of sharing one cooldown.\n"
    "#\n"
    "# What counts as a crossbow skill\n"
    "#   A skill whose skills.txt `globaldelay` works out above 0 while the player\n"
    "#   holds a crossbow, meaning a weapon of one of the itemtypes.txt types listed\n"
    "#   in crossbow_item_types. Every other skill keeps the normal cooldowns.\n"
    "#\n"
    "# How the bolts work\n"
    "#   - The pool starts full when you enter a game and is not saved.\n"
    "#   - Casting a crossbow skill spends one bolt, however many projectiles it\n"
    "#     fires. A skill's own `localdelay` still applies on top.\n"
    "#   - Each spent bolt reloads for the `globaldelay` frames of the skill that\n"
    "#     spent it. Bolts come back one at a time, in the order they were spent.\n"
    "#   - Standing still reloads faster. While the player stands still (player\n"
    "#     mode 1, neutral, or 5, town neutral) bolts reload standing_reload_bonus\n"
    "#     percent faster. Walking, running, attacking, casting, getting hit and\n"
    "#     every other mode reload at the normal speed. The mode is checked every\n"
    "#     game frame (25 a second), so the faster reload starts on the first frame\n"
    "#     the player stands still and ends on the first frame he does anything\n"
    "#     else. It counts whatever weapon is held, like the rest of the reload.\n"
    "#   - The standing bonus speeds up the reload itself, after `globaldelay` was\n"
    "#     worked out, so it goes past any limit inside the `globaldelay` formula.\n"
    "#     At 25, a 1 second reload takes 0.8 seconds and a 0.8 second reload\n"
    "#     0.64 seconds while standing still the whole time.\n"
    "#   - With no bolts left, crossbow skills cannot be cast.\n"
    "#   - Maximum bolts is default_max_bolts plus the max bolts stat, at least 1.\n"
    "#   - The pool keeps reloading whatever weapon is held. Maximum bolts is only\n"
    "#     read while a crossbow is equipped; otherwise the last crossbow value is\n"
    "#     kept. A lower maximum drops the extra bolts. A higher maximum refills one\n"
    "#     bolt at a time with the last reload time, so swapping weapons can never\n"
    "#     refill anything.\n"
    "#   - Casts triggered by items (chance to cast) never spend bolts.\n"
    "#\n"
    "# Damage bonus\n"
    "#   Hits from crossbow skills, and from everything those hits trigger (chance\n"
    "#   to cast on attack, on striking and on kill, and what those trigger in\n"
    "#   turn), deal (100 + damage_base + damage stat) percent of their final\n"
    "#   damage, after resistances and absorbs. That includes the explosions and\n"
    "#   other missiles a crossbow missile spawns. Physical, fire, burn,\n"
    "#   lightning, cold, magic and poison scale. Crushing blow, open wounds and\n"
    "#   bleed do not. Chance to cast when struck and aura pulses never count,\n"
    "#   whatever the player cast last.\n"
    "#\n"
    "# Attack speed\n"
    "#   Crossbow attacks (the A1 attack animation) always take exactly\n"
    "#   default_attack_frames plus the attack frames stat, whatever IAS or skill\n"
    "#   attack speed the player has. Negative stat values make attacks faster. A\n"
    "#   total of 0 or less leaves the game's normal speed.\n"
    "#   Mirrored Blades with a crossbow takes the same number of frames for the\n"
    "#   whole attack. It is a sequence: 18 steps of the cast animation, bolt on\n"
    "#   step 12, so it never plays A1 and needs its own setting below. This only\n"
    "#   reaches it while its skills.txt row keeps UseAttackRate = 1 and a seqtrans\n"
    "#   other than SC (it ships with SQ); otherwise it keeps the game's speed.\n"
    "#   Other sequence skills and the special animations (S1 to S4, used by\n"
    "#   skills such as Assassin traps) keep the game's normal speed.\n"
    "#\n"
    "# Skill buttons\n"
    "#   Crossbow skills show the bolts left in the button's number box, 0 included.\n"
    "#   While any bolt is reloading, the shadow image on the button clocks down the\n"
    "#   bolt that comes back next, over that bolt's own reload time, then restarts\n"
    "#   for the one after it. It goes away when the pool is full. At 0 bolts the button keeps its normal colour; the\n"
    "#   shadow is the only sign. Standing still speeds the shadow up with the\n"
    "#   reload.\n"
    "#   The shadow is an ImageWidget child of the skill button, named as below, in\n"
    "#   hudpanelhd.json and controller/hudpanelhd.json. A button without that\n"
    "#   child still shows the number.\n"
    "#\n"
    "# Console command\n"
    "#   crossbowcharges            status, bolt pools and counters\n"
    "#   crossbowcharges debug on   start the debug log (see debug below); off stops it\n"
    "#   crossbowcharges reset      zero the counters, for a clean test\n"
    "\n"
    "[crossbow_charges]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# Debug log. Writes one line per player skill start, bolt spent, missile\n"
    "# created, hit and proc to d2rloader/logs/celestialrayone.crossbow-charges.log:\n"
    "# which cast it belongs to, whether that is a crossbow cast and why, and for\n"
    "# hits the damage before and after the bonus. At most 60 lines per second;\n"
    "# the rest are counted and reported. Also switchable in game (see above).\n"
    "# While a bolt reloads it also logs every switch between standing still and\n"
    "# not, with the player's mode and the reload speed.\n"
    "debug = false\n"
    "\n"
    "# itemtypes.txt codes that count as crossbows. Child types count too.\n"
    "crossbow_item_types = [\"xbow\"]\n"
    "\n"
    "# Bolts every crossbow user has. The max bolts stat adds to it.\n"
    "default_max_bolts = 1\n"
    "\n"
    "# Percent faster bolts reload while the player stands still (player modes 1\n"
    "# and 5). 25 turns a 1 second reload into 0.8 seconds. 0 turns it off.\n"
    "standing_reload_bonus = 25\n"
    "\n"
    "# Frames every crossbow attack takes. The attack frames stat adds to it.\n"
    "default_attack_frames = 7\n"
    "\n"
    "# skills.txt row id of Mirrored Blades. With a crossbow its whole attack takes\n"
    "# the frames above too. -1 leaves it at the game's speed.\n"
    "mirrored_blades_skill_id = 692\n"
    "\n"
    "# itemstatcost.txt rows.\n"
    "#   max bolts:     bolts added to default_max_bolts.\n"
    "#   current bolts: written by this plugin, read only for everything else.\n"
    "#   damage:        percent added to the crossbow damage bonus.\n"
    "#   attack frames: frames added to default_attack_frames.\n"
    "max_bolts_stat_id = 513\n"
    "current_bolts_stat_id = 514\n"
    "damage_stat_id = 515\n"
    "attack_frames_stat_id = 517\n"
    "\n"
    "# Percent always added to the bonus, before the damage stat.\n"
    "# 25 with a +50 item gives 175% damage.\n"
    "damage_base = 25\n"
    "\n"
    "# Name of the shadow ImageWidget inside each skill button.\n"
    "sweep_widget_name = \"CrossbowSweep\"\n"
    "\n"
    "# Frames in the shadow sprite. Frame 0 is the full shadow.\n"
    "sweep_frames = 64\n"
    "\n"
    "# Shadow colour (red, green, blue, 0 to 255) and opacity (percent, 0 is\n"
    "# invisible, 100 is solid). The sprite itself is plain white; these set it.\n"
    "shadow_color = [128, 128, 128]\n"
    "shadow_opacity = 50\n";

constexpr std::size_t MaximumConfigBytes = 64 * 1024;
constexpr std::size_t MaximumItemTypes   = 8;

struct Config {
    bool                       enabled{ true };
    std::vector<std::string>   itemTypes{ "xbow" };
    std::int32_t               defaultMaxBolts{ 1 };
    std::int32_t               standingReloadBonus{ 25 };
    std::int32_t               maxBoltsStat{ 513 };
    std::int32_t               currentBoltsStat{ 514 };
    std::int32_t               damageStat{ 515 };
    std::int32_t               damageBase{ 25 };
    std::int32_t               defaultAttackFrames{ 7 };
    std::int32_t               attackFramesStat{ 517 };
    std::int32_t               mirroredBladesSkill{ 692 };
    std::string                sweepName{ "CrossbowSweep" };
    std::int32_t               sweepFrames{ 64 };
    std::array<std::int32_t, 3> shadowColor{ 128, 128, 128 };
    std::int32_t               shadowOpacity{ 50 };
    bool                       debug{ false };
};

Config      Settings;
std::string ConfigProblems;

void Problem(std::string_view text) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(text);
}

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

// Drops a trailing # comment that is not inside a quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int32_t minimum, std::int32_t maximum, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    std::int64_t result = 0;
    std::size_t  i      = 0;
    bool negative = false;
    if (value[0] == '-' || value[0] == '+') {
        negative = value[0] == '-';
        i = 1;
    }
    if (i >= value.size()) return false;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') continue;
        if (value[i] < '0' || value[i] > '9') return false;
        result = result * 10 + (value[i] - '0');
        if (result > 1'000'000'000) return false;
    }
    if (negative) result = -result;
    if (result < minimum || result > maximum) return false;
    out = static_cast<std::int32_t>(result);
    return true;
}

auto ParseString(std::string_view value, std::string& out) -> bool {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.substr(1, value.size() - 2));
    return true;
}

// [a, b, c] -> items, each trimmed.
auto SplitArray(std::string_view value, std::vector<std::string_view>& items) -> bool {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    std::string_view inner = Trim(value.substr(1, value.size() - 2));
    items.clear();
    while (!inner.empty()) {
        const std::size_t comma = inner.find(',');
        const std::string_view item = Trim(inner.substr(0, comma));
        if (!item.empty()) items.push_back(item);
        if (comma == std::string_view::npos) break;
        inner = inner.substr(comma + 1);
    }
    return true;
}

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) {
    if (section != "crossbow_charges") return;
    char note[160];
    const auto bad = [&](const char* what) {
        std::snprintf(note, sizeof(note), "%.*s: %s, default kept",
            static_cast<int>(key.size()), key.data(), what);
        Problem(note);
    };
    if (key == "enabled") {
        if (!ParseBool(value, Settings.enabled)) bad("expected true or false");
    } else if (key == "crossbow_item_types") {
        std::vector<std::string_view> items;
        std::vector<std::string>      codes;
        bool ok = SplitArray(value, items) && !items.empty() && items.size() <= MaximumItemTypes;
        for (const std::string_view item : items) {
            std::string code;
            if (!ParseString(item, code) || code.empty() || code.size() > 4) { ok = false; break; }
            codes.push_back(code);
        }
        if (ok) Settings.itemTypes = codes;
        else bad("expected a list of 1 to 8 itemtypes.txt codes, each up to 4 characters");
    } else if (key == "default_max_bolts") {
        if (!ParseInt(value, 0, 1000, Settings.defaultMaxBolts)) bad("expected 0 to 1000");
    } else if (key == "standing_reload_bonus") {
        if (!ParseInt(value, 0, 1000, Settings.standingReloadBonus)) bad("expected 0 to 1000");
    } else if (key == "max_bolts_stat_id") {
        if (!ParseInt(value, 0, 32767, Settings.maxBoltsStat)) bad("expected 0 to 32767");
    } else if (key == "current_bolts_stat_id") {
        if (!ParseInt(value, 0, 32767, Settings.currentBoltsStat)) bad("expected 0 to 32767");
    } else if (key == "damage_stat_id") {
        if (!ParseInt(value, 0, 32767, Settings.damageStat)) bad("expected 0 to 32767");
    } else if (key == "default_attack_frames") {
        if (!ParseInt(value, 0, 1000, Settings.defaultAttackFrames)) bad("expected 0 to 1000");
    } else if (key == "attack_frames_stat_id") {
        if (!ParseInt(value, 0, 32767, Settings.attackFramesStat)) bad("expected 0 to 32767");
    } else if (key == "mirrored_blades_skill_id") {
        if (!ParseInt(value, -1, 32767, Settings.mirroredBladesSkill)) bad("expected -1 to 32767");
    } else if (key == "damage_base") {
        if (!ParseInt(value, -100, 100000, Settings.damageBase)) bad("expected -100 to 100000");
    } else if (key == "sweep_widget_name") {
        std::string name;
        if (ParseString(value, name) && !name.empty() && name.size() < 64) Settings.sweepName = name;
        else bad("expected a quoted widget name");
    } else if (key == "sweep_frames") {
        if (!ParseInt(value, 1, 1024, Settings.sweepFrames)) bad("expected 1 to 1024");
    } else if (key == "shadow_color") {
        std::vector<std::string_view> items;
        std::array<std::int32_t, 3>   color{};
        bool ok = SplitArray(value, items) && items.size() == 3;
        for (std::size_t i = 0; ok && i < 3; ++i) ok = ParseInt(items[i], 0, 255, color[i]);
        if (ok) Settings.shadowColor = color;
        else bad("expected [red, green, blue], each 0 to 255");
    } else if (key == "debug") {
        if (!ParseBool(value, Settings.debug)) bad("expected true or false");
    } else if (key == "shadow_opacity") {
        if (!ParseInt(value, 0, 100, Settings.shadowOpacity)) bad("expected 0 to 100");
    }
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        ApplyConfigLine(section, Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("CrossbowCharges: the config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("CrossbowCharges: the config file could not be read; using defaults.");
        }
    }
    if (!ConfigProblems.empty()) {
        D2RL::LogWarnF(Context, "CrossbowCharges: %s", ConfigProblems.c_str());
    }
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

auto Bytes(void* base, std::size_t offset) noexcept -> std::uint8_t* {
    return static_cast<std::uint8_t*>(base) + offset;
}

template <typename T>
auto Read(void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, Bytes(base, offset), sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(Bytes(base, offset), &value, sizeof(T));
}

auto IsPlayer(void* unit) noexcept -> bool {
    return unit != nullptr && UnitType(unit) == 0;
}

auto GuidOf(void* unit) noexcept -> std::uint32_t {
    return UnitId(unit, SourceFile, __LINE__);
}

auto CastIdOf(void* unit) noexcept -> std::uint32_t {
    return Read<std::uint32_t>(unit, UnitCastIdOffset);
}

auto SkillIdOf(void* skill) noexcept -> std::int32_t {
    void* record = Read<void*>(skill, 0);
    return record != nullptr ? static_cast<std::int32_t>(Read<std::uint16_t>(record, 0)) : -1;
}

auto ValidCastId(std::uint32_t castId) noexcept -> bool {
    return castId != 0 && castId != 0xFFFFFFFFU;
}

auto LocalPlayer() noexcept -> void* {
    return PlayerFromIndex(LocalPlayerIndex());
}

// ---------------------------------------------------------------------------
// Crossbows
// ---------------------------------------------------------------------------

const D2RL::DataTableService* DataTables{};

// Index 1..3 = DataTables bank = the unit's data context byte.
struct BankTypes {
    std::array<std::atomic<std::int32_t>, MaximumItemTypes> ids{};
    std::atomic<std::uint32_t> count{};
};
std::array<BankTypes, 4> CrossbowTypes{};

auto FourCc(const std::string& code) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto c = static_cast<std::uint8_t>(i < code.size() ? code[i] : ' ');
        value |= static_cast<std::uint32_t>(c) << (8 * i);
    }
    return value;
}

// Runs on every data-table load.
void ResolveCrossbowTypes(const D2RL::PluginContext* context) {
    if (DataTables == nullptr) return;
    std::string   report;
    std::uint32_t found = 0;
    for (std::uint32_t bank = 1; bank <= 3; ++bank) {
        std::uint32_t count = 0;
        for (const std::string& code : Settings.itemTypes) {
            D2RL::DataTables::RowView row{};
            row.structSize = D2RL::DataTables::RowViewSize;
            const auto result = DataTables->findRowByCode(context, static_cast<D2RL::DataTables::Bank>(bank),
                D2RL::DataTables::TableId::ItemTypes, FourCc(code), &row);
            if (result == D2RL::DataTables::Result::Success
                    && D2RL::DataTables::HasRowViewField(&row, D2RL::DataTables::RowViewRequiredSize)
                    && count < MaximumItemTypes) {
                CrossbowTypes[bank].ids[count].store(static_cast<std::int32_t>(row.rowIndex), std::memory_order_relaxed);
                ++count;
            }
        }
        CrossbowTypes[bank].count.store(count, std::memory_order_release);
        found += count;
        char part[48];
        std::snprintf(part, sizeof(part), "%sbank %u: %u of %zu", report.empty() ? "" : ", ",
            bank, count, Settings.itemTypes.size());
        report += part;
    }
    if (found == 0) {
        D2RL::LogErrorF(context, "CrossbowCharges: none of the crossbow item types were found in itemtypes.txt "
            "(%s); no skill counts as a crossbow skill.", report.c_str());
        return;
    }
    D2RL::LogInfoF(context, "CrossbowCharges: crossbow item types found (%s).", report.c_str());
}

auto MatchesBank(void* weapon, const BankTypes& bank) noexcept -> bool {
    const std::uint32_t count = bank.count.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < count && i < MaximumItemTypes; ++i) {
        if (CheckItemType(weapon, bank.ids[i].load(std::memory_order_relaxed)) != 0) return true;
    }
    return false;
}

auto HasCrossbow(void* unit) noexcept -> bool {
    void* inventory = Inventory(unit, SourceFile, __LINE__);
    if (inventory == nullptr) return false;
    void* weapon = LeftHandWeapon(inventory);
    if (weapon == nullptr) return false;
    const std::uint8_t context = DataContext(unit);
    if (context >= 1 && context <= 3) return MatchesBank(weapon, CrossbowTypes[context]);
    for (std::size_t bank = 1; bank <= 3; ++bank) {
        if (MatchesBank(weapon, CrossbowTypes[bank])) return true;
    }
    return false;
}

// globaldelay above 0 while a crossbow is held, with what decided it.
struct CrossbowSkillCheck {
    bool         held{};         // a crossbow is equipped
    bool         record{};       // the skills.txt row was found
    std::int32_t globaldelay{};  // evaluated globaldelay, frames
};

auto CheckCrossbowSkill(void* unit, void* skill) noexcept -> CrossbowSkillCheck {
    CrossbowSkillCheck check{};
    if (skill == nullptr) return check;
    check.held = HasCrossbow(unit);
    if (!check.held) return check;
    const std::int32_t skillId = SkillIdOf(skill);
    if (skillId < 0) return check;
    const std::uint8_t context = DataContext(unit);
    void* record = SkillsRecord(context, skillId);
    if (record == nullptr) return check;
    check.record = true;
    const std::int32_t level = SkillLevel(unit, skill, 1, 0);
    check.globaldelay = Evaluate(context, unit, Read<std::uint32_t>(record, SkillGlobalDelayCalc), skillId, level);
    return check;
}

auto IsCrossbowSkill(void* unit, void* skill) noexcept -> bool {
    const CrossbowSkillCheck check = CheckCrossbowSkill(unit, skill);
    return check.held && check.record && check.globaldelay > 0;
}

// default_max_bolts plus the max bolts stat, at least 1.
auto MaxBolts(void* unit) noexcept -> std::int32_t {
    return std::max(1, Settings.defaultMaxBolts + GetStat(unit, Settings.maxBoltsStat, 0));
}

// ---------------------------------------------------------------------------
// Debug log
// ---------------------------------------------------------------------------
// One line per event while on (TOML debug, or "crossbowcharges debug on"),
// written to the plugin log. At most DebugLinesPerSecond; the rest are
// counted and reported at the front of the next line that gets through.

std::atomic<bool>       DebugOn{};
constexpr std::uint32_t DebugLinesPerSecond = 60;
std::mutex              DebugMutex;
std::uint64_t           DebugWindowStart{};
std::uint32_t           DebugWindowLines{};
std::uint64_t           DebugDropped{};

void DebugLog(const char* format, ...) {
    if (!DebugOn.load(std::memory_order_relaxed) || Context == nullptr) return;
    std::uint64_t dropped = 0;
    {
        std::lock_guard lock(DebugMutex);
        const std::uint64_t now = GetTickCount64();
        if (now - DebugWindowStart >= 1000) {
            DebugWindowStart = now;
            DebugWindowLines = 0;
        }
        if (DebugWindowLines >= DebugLinesPerSecond) {
            ++DebugDropped;
            return;
        }
        ++DebugWindowLines;
        dropped      = DebugDropped;
        DebugDropped = 0;
    }
    char line[640];
    int  used = 0;
    if (dropped != 0) {
        used = std::snprintf(line, sizeof(line), "(%llu lines dropped) ", static_cast<unsigned long long>(dropped));
        if (used < 0 || used >= static_cast<int>(sizeof(line))) used = 0;
    }
    va_list args;
    va_start(args, format);
    std::vsnprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), format, args);
    va_end(args);
    Context->LogInfo(line);
}

auto UnitKind(std::uint32_t type) noexcept -> const char* {
    switch (type) {
    case 0:  return "player";
    case 1:  return "monster";
    case 2:  return "object";
    case 3:  return "missile";
    case 4:  return "item";
    default: return "unit";
    }
}

auto ClassOf(void* unit) noexcept -> std::uint32_t {
    return Read<std::uint32_t>(unit, UnitClassOffset);
}

// Server units only: the server allocator sets the flag, nothing clears it.
// (34F8D0, bit 21 of unit+124h, marks async client-only units instead and is
// clear on players on both sides.)
auto IsServerUnit(void* unit) noexcept -> bool {
    return unit != nullptr && (Read<std::uint32_t>(unit, UnitFlagsExOffset) & ServerUnitFlag) != 0;
}

// Set by the aura events (skill auras 437460, item auras 437230, aura start
// 43B680) for exactly the length of the aura's do-handler call.
auto InAuraPulse(void* unit) noexcept -> bool {
    return unit != nullptr && (Read<std::uint32_t>(unit, UnitFlagsExOffset) & AuraPulseFlag) != 0;
}

// The mode field matched this build (UnitModeWitnessBytes); without it the
// standing bonus stays off.
bool StandingPartVerified{};

auto ModeOf(void* unit) noexcept -> std::int32_t {
    return Read<std::int32_t>(unit, UnitModeOffset);
}

// Standing still: the neutral modes NU (1) and TN (5) only.
auto IsStandingStill(void* unit) noexcept -> bool {
    if (!StandingPartVerified || unit == nullptr) return false;
    const std::int32_t mode = ModeOf(unit);
    return mode == NeutralMode || mode == TownNeutralMode;
}

// Speed of the reload clock, percent of normal.
auto ReloadRate(bool standing) noexcept -> std::int64_t {
    return 100 + (standing ? static_cast<std::int64_t>(Settings.standingReloadBonus) : 0);
}

// ---------------------------------------------------------------------------
// Server pools (game thread)
// ---------------------------------------------------------------------------

struct Pool {
    bool                     initialized{};
    bool                     everSpent{};
    bool                     standing{};      // the last frame counted had the standing bonus
    bool                     tickStanding{};  // what the last tick saw (debug log only)
    std::int32_t             current{};
    std::int32_t             lastMax{ 1 };
    std::int32_t             lastDuration{ 1 };
    std::int32_t             clockFrame{};    // server frame the reload clock has counted up to
    std::int64_t             progress{};      // front bolt's reload done, hundredths of a frame
    std::uint64_t            clockMs{};       // wall clock of the last count
    std::int32_t             written{ -1 };
    std::uint32_t            lastCastId{};  // cast that spent the last bolt
    std::int32_t             tickFrame{};   // server frame of the last update
    std::deque<std::int32_t> reloads;   // frames, front = reloading now
};

// Crossbow cast ids, each with the GUID of the player it belongs to. Ids are
// unique per game, so the id alone is the key. Bounded: a tag goes once
// CastTagCapacity newer ones exist, long after anything it fired is gone.
constexpr std::size_t CastTagCapacity = 8192;
struct CastTag {
    std::uint32_t owner;
    std::uint32_t slot;
};

// Player missiles that belong to another cast than their own cast id, keyed by
// missile GUID and valid only while the missile still carries the cast id it
// was created with (a GUID the game hands out again starts clean):
//   follows the parent  created while one of its owner's missiles was being
//                       resolved, under a different cast id
//   follows the cast    created outside any missile under a newer id than its
//                       owner's current cast (523370 gives every missile its
//                       own new id, 52353C), so it belongs to that cast
//   follows nothing     created during an aura pulse: never a crossbow missile
constexpr std::size_t LinkCapacity = 8192;
struct MissileLink {
    std::uint32_t castId;   // the missile's own cast id when the link was made
    std::uint32_t follows;  // the cast it belongs to, 0 for none
    std::uint32_t slot;
};

std::mutex                                     ServerMutex;
void*                                          ServerGame{};
std::unordered_map<std::uint32_t, Pool>        Pools;
std::int32_t                                   LastPruneFrame{};
std::unordered_map<std::uint32_t, CastTag>     CastTags;
std::array<std::uint32_t, CastTagCapacity>     CastTagOrder{};
std::size_t                                    CastTagCount{};
std::uint32_t                                  LastCrossbowCast{};
std::unordered_map<std::uint32_t, MissileLink> MissileLinks;
std::array<std::uint32_t, LinkCapacity>        LinkOrder{};
std::size_t                                    LinkCount{};
std::atomic<bool>                              ResetRequested{};

void ResetServerIfNeeded(void* game) {
    const bool requested = ResetRequested.exchange(false);
    if (requested || (game != nullptr && game != ServerGame)) {
        Pools.clear();
        LastPruneFrame = 0;
        CastTags.clear();
        CastTagCount     = 0;
        LastCrossbowCast = 0;
        MissileLinks.clear();
        LinkCount = 0;
        if (game != nullptr) ServerGame = game;
    }
}

auto FrameOf(void* game) noexcept -> std::int32_t {
    return Read<std::int32_t>(game, GameFrameOffset);
}

// Maximum bolts follows the crossbow; without one the last value stays.
void RefreshCapacity(Pool& pool, void* unit, std::int32_t frame) {
    if (HasCrossbow(unit)) {
        pool.lastMax = MaxBolts(unit);
        if (!pool.initialized) {
            pool.initialized = true;
            pool.current     = pool.lastMax;
            pool.reloads.clear();
            pool.progress    = 0;
            pool.clockFrame  = frame;
        }
    }
    if (!pool.initialized) return;
    const std::int32_t max   = pool.lastMax;
    const std::int32_t total = pool.current + static_cast<std::int32_t>(pool.reloads.size());
    if (total > max) {
        while (!pool.reloads.empty() && pool.current + static_cast<std::int32_t>(pool.reloads.size()) > max) {
            pool.reloads.pop_back();
        }
        pool.current = std::min(pool.current, max);
    } else if (total < max) {
        if (!pool.everSpent) {
            pool.current += max - total;
        } else {
            if (pool.reloads.empty()) {
                pool.progress   = 0;
                pool.clockFrame = frame;
            }
            for (std::int32_t i = total; i < max; ++i) pool.reloads.push_back(pool.lastDuration);
        }
    }
}

// Counts the reload clock up to frame. Every frame adds 100 to the progress of
// the bolt reloading now, or 100 + standing_reload_bonus when the player stood
// still in it; the bolt is back once its progress reaches its reload frames *
// 100, and what is left over goes to the next bolt. The progress is 0 whenever
// nothing reloads.
void Advance(Pool& pool, std::int32_t frame, bool standing) {
    const std::int32_t elapsed = frame - pool.clockFrame;
    pool.clockFrame = frame;
    pool.clockMs    = GetTickCount64();
    pool.standing   = standing;
    if (elapsed > 0 && !pool.reloads.empty()) pool.progress += static_cast<std::int64_t>(elapsed) * ReloadRate(standing);
    while (!pool.reloads.empty()) {
        const std::int64_t needed = static_cast<std::int64_t>(std::max(1, pool.reloads.front())) * 100;
        if (pool.progress < needed) break;
        pool.progress -= needed;
        pool.reloads.pop_front();
        ++pool.current;
        BoltsReloaded.fetch_add(1, std::memory_order_relaxed);
    }
    if (pool.reloads.empty()) pool.progress = 0;
}

// Returns the value to write, or -1. The write itself happens after the lock
// is released, since it runs the game's stat-change callback.
auto Commit(Pool& pool) -> std::int32_t {
    if (pool.current == pool.written) return -1;
    pool.written = pool.current;
    return pool.current;
}

void WriteBolts(void* unit, std::int32_t value) {
    if (value >= 0) SetStat(unit, Settings.currentBoltsStat, value, 0);
}

// A pool whose unit the regeneration event has not ticked for this long is
// gone (a unit that left the game, or one that never held a crossbow).
constexpr std::int32_t PoolIdleFrames = 250;

void PrunePools(std::int32_t frame) {
    if (frame >= LastPruneFrame && frame - LastPruneFrame < PoolIdleFrames) return;
    LastPruneFrame = frame;
    for (auto it = Pools.begin(); it != Pools.end();) {
        const std::int32_t idle = frame - it->second.tickFrame;
        if (idle > PoolIdleFrames || idle < 0) {
            it = Pools.erase(it);
        } else {
            ++it;
        }
    }
}

void TickServer(void* game, void* unit) {
    const std::uint32_t guid     = GuidOf(unit);
    const bool          standing = IsStandingStill(unit);
    std::int32_t        write    = -1;
    bool                switched = false;
    std::int32_t        frame    = 0;
    std::int32_t        bolts    = 0;
    std::int32_t        max      = 0;
    std::int64_t        left     = 0;
    {
        std::lock_guard lock(ServerMutex);
        ResetServerIfNeeded(game);
        frame = FrameOf(game);
        PrunePools(frame);
        auto it = Pools.find(guid);
        if (it == Pools.end()) {
            if (!HasCrossbow(unit)) return;   // only crossbow users get a pool
            it = Pools.try_emplace(guid).first;
        }
        Pool& pool = it->second;
        RefreshCapacity(pool, unit, frame);
        if (!pool.initialized) return;
        switched          = standing != pool.tickStanding && !pool.reloads.empty();
        pool.tickStanding = standing;
        Advance(pool, frame, standing);
        pool.tickFrame = frame;
        write = Commit(pool);
        bolts = pool.current;
        max   = pool.lastMax;
        if (!pool.reloads.empty()) left = static_cast<std::int64_t>(std::max(1, pool.reloads.front())) * 100 - pool.progress;
    }
    WriteBolts(unit, write);
    if (switched) {
        const std::int64_t rate = ReloadRate(standing);
        DebugLog("[reload] player %u frame %d: mode %d, %s, reload at %lld%% speed, %d of %d bolts, next in %lld "
            "frames at this speed", guid, frame, ModeOf(unit), standing ? "standing still" : "not standing still",
            static_cast<long long>(rate), bolts, max, static_cast<long long>((std::max<std::int64_t>(0, left) + rate - 1) / rate));
    }
}

// The functions up to CastOfMissile expect ServerMutex to be held.
void TagCast(std::uint32_t castId, std::uint32_t owner) {
    if (!ValidCastId(castId)) return;
    LastCrossbowCast = castId;
    const auto it = CastTags.find(castId);
    if (it != CastTags.end()) {
        it->second.owner = owner;
        return;
    }
    const auto slot = static_cast<std::uint32_t>(CastTagCount % CastTagCapacity);
    if (CastTagCount >= CastTagCapacity) {
        const auto old = CastTags.find(CastTagOrder[slot]);
        if (old != CastTags.end() && old->second.slot == slot) CastTags.erase(old);
    }
    CastTagOrder[slot] = castId;
    CastTags.emplace(castId, CastTag{ owner, slot });
    ++CastTagCount;
}

auto IsCrossbowCast(std::uint32_t castId, std::uint32_t owner) -> bool {
    if (!ValidCastId(castId)) return false;
    const auto it = CastTags.find(castId);
    return it != CastTags.end() && it->second.owner == owner;
}

void RecordLink(std::uint32_t missileGuid, std::uint32_t castId, std::uint32_t follows) {
    const auto it = MissileLinks.find(missileGuid);
    if (it != MissileLinks.end()) {
        it->second.castId  = castId;
        it->second.follows = follows;
        return;
    }
    const auto slot = static_cast<std::uint32_t>(LinkCount % LinkCapacity);
    if (LinkCount >= LinkCapacity) {
        const auto old = MissileLinks.find(LinkOrder[slot]);
        if (old != MissileLinks.end() && old->second.slot == slot) MissileLinks.erase(old);
    }
    LinkOrder[slot] = missileGuid;
    MissileLinks.emplace(missileGuid, MissileLink{ castId, follows, slot });
    ++LinkCount;
}

// The cast a missile belongs to: its link while the link is valid, else its
// own cast id.
auto CastOfMissile(std::uint32_t missileGuid, std::uint32_t castId, bool* linked) -> std::uint32_t {
    const auto it    = MissileLinks.find(missileGuid);
    const bool valid = it != MissileLinks.end() && it->second.castId == castId;
    if (linked != nullptr) *linked = valid;
    return valid ? it->second.follows : castId;
}

// Called for crossbow skills only, by the do-handler relay and 436C70, right
// after the game evaluated the skill's globaldelay above 0 with a crossbow
// held. The unit's current cast id is that crossbow cast, so it is tagged here
// too; a cast the skill start already tagged is left alone.
void SpendBolt(void* game, void* unit, std::int32_t frames, std::int32_t skillId) {
    const std::uint32_t guid     = GuidOf(unit);
    const std::uint32_t castId   = CastIdOf(unit);
    const bool          aura     = InAuraPulse(unit);
    const bool          standing = IsStandingStill(unit);
    std::int32_t        write  = -1;
    const char*         outcome = "no pool";
    bool                tagged  = false;
    std::int32_t        left    = 0;
    std::int32_t        max     = 0;
    {
        std::lock_guard lock(ServerMutex);
        ResetServerIfNeeded(game);
        if (!aura && ValidCastId(castId) && !IsCrossbowCast(castId, guid)) {
            TagCast(castId, guid);
            tagged = true;
        }
        Pool& pool = Pools[guid];
        const std::int32_t frame = FrameOf(game);
        RefreshCapacity(pool, unit, frame);
        if (pool.initialized) {
            Advance(pool, frame, standing);
            if (ValidCastId(castId) && castId == pool.lastCastId) {
                // A later do-event of a cast that already spent (Strafe's next arrow).
                outcome = "repeat step, already spent";
                RepeatStepsIgnored.fetch_add(1, std::memory_order_relaxed);
            } else {
                pool.lastCastId = castId;
                if (pool.current <= 0) {
                    outcome = "refused, no bolts";
                    BoltsRefused.fetch_add(1, std::memory_order_relaxed);
                } else {
                    --pool.current;
                    const std::int32_t duration = std::max(1, frames);
                    pool.reloads.push_back(duration);   // starts at progress 0 when nothing reloaded
                    pool.everSpent    = true;
                    pool.lastDuration = duration;
                    pool.tickFrame    = frame;
                    write   = Commit(pool);
                    outcome = "bolt spent";
                    BoltsSpent.fetch_add(1, std::memory_order_relaxed);
                }
            }
            left = pool.current;
            max  = pool.lastMax;
        }
    }
    WriteBolts(unit, write);
    if (tagged) CastsTaggedAtSpend.fetch_add(1, std::memory_order_relaxed);
    DebugLog("[spend] player %u skill %d cast %u: %s, reload %d frames, %d of %d bolts left%s%s", guid, skillId, castId,
        outcome, frames, left, max, tagged ? "; cast tagged here, the skill start did not tag it" : "",
        aura ? "; aura pulse, not tagged" : "");
}

// A pool not started yet would be full.
auto ServerBolts(void* unit) -> std::int32_t {
    const std::uint32_t guid = GuidOf(unit);
    std::lock_guard lock(ServerMutex);
    ResetServerIfNeeded(nullptr);
    const auto it = Pools.find(guid);
    if (it == Pools.end() || !it->second.initialized) return MaxBolts(unit);
    return it->second.current;
}

// The missiles the engine is resolving on this thread (4639A0, 466CE0,
// 462E40), innermost last. Past MaxMissileFrames nothing is recorded and no
// missile counts as in progress.
struct MissileFrame {
    void* game;
    void* missile;
};
constexpr std::size_t MaxMissileFrames = 32;
thread_local std::array<MissileFrame, MaxMissileFrames> MissileFrames{};
thread_local std::size_t                                MissileDepth{};

class MissileScope {
public:
    MissileScope(void* game, void* missile) noexcept {
        if (MissileDepth < MaxMissileFrames) MissileFrames[MissileDepth] = { game, missile };
        ++MissileDepth;
    }
    ~MissileScope() { --MissileDepth; }
    MissileScope(const MissileScope&)            = delete;
    MissileScope& operator=(const MissileScope&) = delete;
};

// The innermost missile in progress when it belongs to unit, else null.
auto OwnMissileInProgress(void* unit) noexcept -> void* {
    if (unit == nullptr || MissileDepth == 0 || MissileDepth > MaxMissileFrames) return nullptr;
    const MissileFrame& frame = MissileFrames[MissileDepth - 1];
    if (frame.game == nullptr || frame.missile == nullptr || UnitType(frame.missile) != MissileUnitType) {
        return nullptr;
    }
    return MissileOwner(frame.game, frame.missile) == unit ? frame.missile : nullptr;
}

// Which cast a hit or proc of a player (a server unit) belongs to, and whether
// that is a crossbow cast. Aura pulses never are. Otherwise the player's own
// missile being resolved decides, else the player's current cast.
struct SourceDecision {
    bool          crossbow{};
    bool          aura{};
    bool          viaMissile{};
    bool          linked{};          // the missile belongs to another cast than its own id
    std::uint32_t missileGuid{};
    std::uint32_t missileRow{};
    std::uint32_t missileCast{};
    std::uint32_t cast{};            // the cast that decides
    std::uint32_t unitCast{};        // the player's current cast id
    std::uint32_t lastCrossbowCast{};
};

auto DecideSource(void* unit) -> SourceDecision {
    SourceDecision d{};
    d.unitCast = CastIdOf(unit);
    const std::uint32_t owner = GuidOf(unit);
    if (InAuraPulse(unit)) {
        d.aura = true;
    } else if (void* missile = OwnMissileInProgress(unit); missile != nullptr) {
        d.viaMissile  = true;
        d.missileGuid = GuidOf(missile);
        d.missileRow  = ClassOf(missile);
        d.missileCast = CastIdOf(missile);
    }
    std::lock_guard lock(ServerMutex);
    ResetServerIfNeeded(nullptr);
    d.lastCrossbowCast = LastCrossbowCast;
    if (d.aura) return d;
    d.cast     = d.viaMissile ? CastOfMissile(d.missileGuid, d.missileCast, &d.linked) : d.unitCast;
    d.crossbow = IsCrossbowCast(d.cast, owner);
    return d;
}

void DescribeSource(const SourceDecision& d, char* out, std::size_t size) {
    if (d.aura) {
        std::snprintf(out, size, "aura pulse (cast %u)", d.unitCast);
    } else if (d.viaMissile && d.linked) {
        std::snprintf(out, size, "missile %u (row %u, cast %u, belongs to cast %u)", d.missileGuid, d.missileRow,
            d.missileCast, d.cast);
    } else if (d.viaMissile) {
        std::snprintf(out, size, "missile %u (row %u, cast %u)", d.missileGuid, d.missileRow, d.missileCast);
    } else {
        std::snprintf(out, size, "cast %u", d.unitCast);
    }
}

// Set while item event function 20 runs for an owner carrying a tagged id.
thread_local bool          ProcFromCrossbow{};
thread_local std::uint32_t ProcOwner{};

// ---------------------------------------------------------------------------
// Client view (UI thread)
// ---------------------------------------------------------------------------
// On a host the server pool lives in this process and is read directly. A
// joined TCP/IP client only has the current-bolts stat, so it keeps its own
// copy of the reload queue: each drop of the stat is a fired bolt, reloading
// for the globaldelay the client evaluated for its own cast. The drop and the
// cast arrive in either order; a bolt whose cast has not been seen yet gets
// its time the moment the cast arrives. The front bolt's reload is counted in
// milliseconds at the speed its own player's mode gives, the way the server
// counts frames. No two locks are ever held together.

struct SweepView {
    std::int32_t bolts{};
    bool         reloading{};
    float        progress{};
};

auto HostView(std::uint32_t guid, std::uint64_t now, SweepView& view) -> bool {
    std::lock_guard lock(ServerMutex);
    const auto it = Pools.find(guid);
    if (it == Pools.end() || !it->second.initialized) return false;
    const Pool& pool = it->second;
    view.bolts     = pool.current;
    view.reloading = !pool.reloads.empty();
    view.progress  = 0.0f;
    if (view.reloading) {
        // The server's count, plus the time since it at the speed of the last frame counted.
        const double needed    = static_cast<double>(std::max(1, pool.reloads.front())) * 100.0;
        const double sinceTick = now > pool.clockMs
            ? static_cast<double>(now - pool.clockMs) / static_cast<double>(MsPerFrame) : 0.0;
        const double done      = static_cast<double>(pool.progress)
            + sinceTick * static_cast<double>(ReloadRate(pool.standing));
        view.progress = static_cast<float>(std::clamp(done / needed, 0.0, 0.999));
    }
    return true;
}

struct Prediction {
    std::int32_t  frames;
    std::uint64_t timeMs;
};

struct Reload {
    std::int32_t frames;
    bool         known;   // false until the client's own cast supplies the time
};

struct ClientPool {
    void*                  unit{};
    std::uint32_t          guid{ 0xFFFFFFFFU };
    bool                   synced{};
    bool                   everSpent{};
    std::int32_t           lastStat{};
    std::int32_t           lastMax{ 1 };
    std::int32_t           lastDuration{ 25 };
    double                 doneMs{};      // front bolt's reload done, milliseconds at the normal speed
    std::uint64_t          clockMs{};     // wall clock doneMs is counted up to
    std::deque<Reload>     reloads;
    std::deque<Prediction> predictions;   // casts the stat has not shown yet
    std::uint32_t          lastCastId{};  // client cast that made the last prediction
};

std::mutex ClientMutex;
ClientPool Client;

void UpdateRemote(void* unit, std::uint64_t now) {
    const std::uint32_t guid = GuidOf(unit);
    if (unit != Client.unit || guid != Client.guid) {
        Client      = ClientPool{};
        Client.unit = unit;
        Client.guid = guid;
    }
    if (!Client.reloads.empty() && Client.clockMs != 0 && now > Client.clockMs) {
        Client.doneMs += static_cast<double>(now - Client.clockMs)
            * static_cast<double>(ReloadRate(IsStandingStill(unit))) / 100.0;
    }
    Client.clockMs = now;
    while (!Client.predictions.empty() && now - Client.predictions.front().timeMs > PredictionTimeoutMs) {
        Client.predictions.pop_front();
    }
    const std::int32_t previousMax = Client.lastMax;
    if (HasCrossbow(unit)) Client.lastMax = MaxBolts(unit);
    const std::int32_t current = GetStat(unit, Settings.currentBoltsStat, 0);
    // A drop to exactly the new, lower maximum is the server clamping the pool
    // after a weapon change, not bolts being fired.
    const bool clamped = Client.lastMax < previousMax && current == Client.lastMax;

    if (!Client.synced) {
        Client.synced   = true;
        Client.lastStat = current;
        for (std::int32_t i = current; i < Client.lastMax; ++i) Client.reloads.push_back({ Client.lastDuration, false });
        Client.doneMs = 0.0;
    } else if (current < Client.lastStat && !clamped) {
        for (std::int32_t i = current; i < Client.lastStat; ++i) {
            Reload reload{ Client.lastDuration, false };
            if (!Client.predictions.empty()) {
                reload = { Client.predictions.front().frames, true };
                Client.predictions.pop_front();
                Client.lastDuration = reload.frames;
            }
            if (Client.reloads.empty()) Client.doneMs = 0.0;
            Client.reloads.push_back(reload);
            Client.everSpent = true;
        }
    } else if (current > Client.lastStat) {
        for (std::int32_t i = Client.lastStat; i < current && !Client.reloads.empty(); ++i) {
            Client.reloads.pop_front();
        }
        Client.doneMs = 0.0;
    }
    Client.lastStat = current;

    const std::int32_t total = current + static_cast<std::int32_t>(Client.reloads.size());
    if (total > Client.lastMax) {
        while (!Client.reloads.empty() && current + static_cast<std::int32_t>(Client.reloads.size()) > Client.lastMax) {
            Client.reloads.pop_back();
        }
    } else if (total < Client.lastMax && Client.everSpent) {
        if (Client.reloads.empty()) Client.doneMs = 0.0;
        for (std::int32_t i = total; i < Client.lastMax; ++i) Client.reloads.push_back({ Client.lastDuration, true });
    }
}

void PredictRemoteSpend(void* unit, std::int32_t frames, std::uint64_t now) {
    UpdateRemote(unit, now);
    frames = std::max(1, frames);
    for (Reload& reload : Client.reloads) {
        if (!reload.known) {
            reload              = { frames, true };
            Client.lastDuration = frames;
            return;
        }
    }
    Client.predictions.push_back({ frames, now });
}

// Expects UpdateRemote to have counted the clock up to now.
auto RemoteView() -> SweepView {
    SweepView view{};
    view.bolts     = std::max(0, Client.lastStat - static_cast<std::int32_t>(Client.predictions.size()));
    view.reloading = !Client.reloads.empty();
    if (view.reloading) {
        const auto durationMs = static_cast<double>(std::max(1, Client.reloads.front().frames)) * MsPerFrame;
        view.progress = static_cast<float>(std::clamp(Client.doneMs / durationMs, 0.0, 0.999));
    }
    return view;
}

// What the local player's buttons and client gate see.
auto LocalView(void* unit, std::uint64_t now) -> SweepView {
    SweepView view{};
    if (HostView(GuidOf(unit), now, view)) return view;
    std::lock_guard lock(ClientMutex);
    UpdateRemote(unit, now);
    return RemoteView();
}

// ---------------------------------------------------------------------------
// Hooks: cooldowns
// ---------------------------------------------------------------------------

std::int32_t __fastcall HookedGate(void* unit, void* skill) {
    if (!Active.load(std::memory_order_relaxed) || skill == nullptr || !IsPlayer(unit)
            || !IsCrossbowSkill(unit, skill)) {
        return OriginalGate(unit, skill);
    }
    const std::int32_t skillId = SkillIdOf(skill);
    if (CheckState(unit, LocalCooldownState) != 0) {
        void* list = StateStatList(unit, LocalCooldownState);
        if (list != nullptr && ReadListStat(DataContext(unit), list, LocalCooldownStat, skillId) > 0) return 0;
    }
    std::int32_t bolts = 0;
    if (IsServerUnit(unit)) {
        bolts = ServerBolts(unit);
    } else if (unit == LocalPlayer()) {
        bolts = LocalView(unit, GetTickCount64()).bolts;
    } else {
        bolts = GetStat(unit, Settings.currentBoltsStat, 0);
    }
    return bolts > 0 ? 1 : 0;
}

void __fastcall HookedClientApply(void* unit, std::int32_t skillId, std::int32_t level) {
    if (!Active.load(std::memory_order_relaxed) || !IsPlayer(unit) || unit != LocalPlayer() || !HasCrossbow(unit)) {
        OriginalClientApply(unit, skillId, level);
        return;
    }
    const std::uint8_t context = DataContext(unit);
    void* record = SkillsRecord(context, skillId);
    if (record == nullptr) {
        OriginalClientApply(unit, skillId, level);
        return;
    }
    const std::int32_t global = Evaluate(context, unit, Read<std::uint32_t>(record, SkillGlobalDelayCalc), skillId, level);
    if (global <= 0) {
        OriginalClientApply(unit, skillId, level);
        return;
    }
    const std::uint64_t now = GetTickCount64();
    SweepView host{};
    if (!HostView(GuidOf(unit), now, host)) {
        const std::uint32_t castId = CastIdOf(unit);
        std::lock_guard lock(ClientMutex);
        if (!ValidCastId(castId) || castId != Client.lastCastId) {
            PredictRemoteSpend(unit, global, now);
            Client.lastCastId = castId;
        }
    }
    const std::int32_t local = Evaluate(context, unit, Read<std::uint32_t>(record, SkillLocalDelayCalc), skillId, level);
    if (local > 0) ClientLocalDelay(unit, local, skillId);
}

void __fastcall HookedServerApply(void* game, void* unit, std::int32_t skillId, std::int32_t level) {
    if (!Active.load(std::memory_order_relaxed) || game == nullptr || !IsPlayer(unit) || !HasCrossbow(unit)) {
        OriginalServerApply(game, unit, skillId, level);
        return;
    }
    const std::uint8_t context = Read<std::uint8_t>(game, GameDataContextOffset);
    void* record = SkillsRecord(context, skillId);
    if (record == nullptr) {
        OriginalServerApply(game, unit, skillId, level);
        return;
    }
    const std::int32_t global = Evaluate(context, unit, Read<std::uint32_t>(record, SkillGlobalDelayCalc), skillId, level);
    if (global <= 0) {
        OriginalServerApply(game, unit, skillId, level);
        return;
    }
    SpendBolt(game, unit, global, skillId);
    const std::int32_t local = Evaluate(context, unit, Read<std::uint32_t>(record, SkillLocalDelayCalc), skillId, level);
    if (local > 0) ServerLocalDelay(game, unit, local, skillId);
}

// Reached from the relay in place of the call at 43B18D; r13d is the skill id.
void __fastcall HookedDoHandlerGlobal(void* game, void* unit, std::int32_t frames, std::int32_t skillId) {
    if (Active.load(std::memory_order_relaxed) && IsPlayer(unit) && HasCrossbow(unit)) {
        SpendBolt(game, unit, frames, skillId);
        return;
    }
    ServerGlobalDelay(game, unit, frames);
}

std::uint64_t __fastcall HookedPlayerRegen(void* game, void* unit, std::int32_t a3, std::int32_t a4) {
    if (Active.load(std::memory_order_relaxed) && game != nullptr && IsPlayer(unit)) {
        // The per-frame player event never runs inside a missile's processing.
        // A missile still recorded here means the tracking lost a pop.
        if (MissileDepth != 0 && MissileDepthLeaks.fetch_add(1, std::memory_order_relaxed) % 1000 == 0) {
            D2RL::LogWarnF(Context, "CrossbowCharges: missile tracking out of balance (depth %zu at the player "
                "tick); crossbow missile hits may be judged wrong.", MissileDepth);
        }
        TickServer(game, unit);
    }
    return OriginalPlayerRegen(game, unit, a3, a4);
}

// ---------------------------------------------------------------------------
// Hooks: cast ids and damage
// ---------------------------------------------------------------------------

std::uint64_t __fastcall HookedSetUsedSkill(void* unit, void* skill, std::uint32_t castId) {
    const std::uint64_t result = OriginalSetUsedSkill(unit, skill, castId);
    if (!Active.load(std::memory_order_relaxed) || skill == nullptr || !ValidCastId(castId)
            || !IsPlayer(unit) || !IsServerUnit(unit)) {
        return result;
    }
    SkillStarts.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t      guid    = GuidOf(unit);
    const std::int32_t       skillId = SkillIdOf(skill);
    const CrossbowSkillCheck check   = CheckCrossbowSkill(unit, skill);
    if (!check.held || !check.record || check.globaldelay <= 0) {
        if (!check.held || !check.record) {
            DebugLog("[start] player %u skill %d cast %u: not a crossbow skill (%s)", guid, skillId, castId,
                !check.held ? "no crossbow held" : "no skills.txt row");
        } else {
            DebugLog("[start] player %u skill %d cast %u: not a crossbow skill (globaldelay %d)", guid, skillId,
                castId, check.globaldelay);
        }
        return result;
    }
    {
        std::lock_guard lock(ServerMutex);
        ResetServerIfNeeded(nullptr);
        TagCast(castId, guid);
    }
    CastsTagged.fetch_add(1, std::memory_order_relaxed);
    DebugLog("[start] player %u skill %d cast %u: crossbow skill (globaldelay %d frames), cast tagged", guid, skillId,
        castId, check.globaldelay);
    return result;
}

std::uint64_t __fastcall HookedEventFunc20(void* game, std::uint64_t event, void* owner, void* target,
        void* damage, std::uint64_t packedStat, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9) {
    const bool          previousTagged = ProcFromCrossbow;
    const std::uint32_t previousOwner  = ProcOwner;
    bool          tagged = false;
    std::uint32_t guid   = 0;
    if (Active.load(std::memory_order_relaxed) && IsPlayer(owner) && IsServerUnit(owner)) {
        guid = GuidOf(owner);
        const SourceDecision d = DecideSource(owner);
        tagged = d.crossbow;
        ProcsSeen.fetch_add(1, std::memory_order_relaxed);
        if (DebugOn.load(std::memory_order_relaxed)) {
            char source[160];
            DescribeSource(d, source, sizeof(source));
            DebugLog("[proc] player %u event %llu via %s: %s", guid, static_cast<unsigned long long>(event), source,
                tagged ? "crossbow source, a proc cast here gets tagged" : "not a crossbow source");
        }
    }
    ProcFromCrossbow = tagged;
    ProcOwner        = guid;
    const std::uint64_t result = NativeEventFunc20(game, event, owner, target, damage, packedStat, a7, a8, a9);
    ProcFromCrossbow = previousTagged;
    ProcOwner        = previousOwner;
    return result;
}

// Reached from the relay in place of the call at 589C37.
std::uint32_t __fastcall HookedProcCastId(void* game) {
    const std::uint32_t castId = GenerateCastId(game);
    if (ProcFromCrossbow && ValidCastId(castId)) {
        {
            std::lock_guard lock(ServerMutex);
            TagCast(castId, ProcOwner);
        }
        ProcsTagged.fetch_add(1, std::memory_order_relaxed);
        DebugLog("[proc] player %u: proc cast %u tagged", ProcOwner, castId);
    }
    return castId;
}

auto Scale(std::int32_t value, std::int64_t numerator) noexcept -> std::int32_t {
    if (value <= 0) return value;
    const std::int64_t scaled = static_cast<std::int64_t>(value) * numerator / 100;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled, 0, std::numeric_limits<std::int32_t>::max()));
}

void BoostDamage(void* damage, std::int32_t percent) {
    const std::int64_t numerator = std::max<std::int64_t>(0, 100LL + percent);
    for (const std::size_t field : DamageFields) {
        Write<std::int32_t>(damage, field, Scale(Read<std::int32_t>(damage, field), numerator));
    }
    void*               entries = Read<void*>(damage, DamagePoisonEntries);
    const std::uint64_t count   = Read<std::uint64_t>(damage, DamagePoisonCount);
    if (entries != nullptr && count <= 4096) {
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::size_t at = static_cast<std::size_t>(i) * PoisonEntrySize + PoisonEntryDamage;
            Write<std::int32_t>(entries, at, Scale(Read<std::int32_t>(entries, at), numerator));
        }
    }
}

// The damage values of one hit, in 256ths of a life point.
struct DamageSnapshot {
    std::array<std::int32_t, std::size(DamageFields)> fields{};
};

auto TakeSnapshot(void* damage) -> DamageSnapshot {
    DamageSnapshot snapshot{};
    for (std::size_t i = 0; i < std::size(DamageFields); ++i) snapshot.fields[i] = Read<std::int32_t>(damage, DamageFields[i]);
    return snapshot;
}

// "life 12.00 -> 30.00 (phys 10.00 -> 25.00, fire 2.00 -> 5.00)"
void DescribeDamage(const DamageSnapshot& before, const DamageSnapshot& after, char* out, std::size_t size) {
    // DamageFields order: physical, fire, burn, lightning, magic, cold, poison, then the life total.
    static constexpr const char* Names[]{ "phys", "fire", "burn", "light", "magic", "cold", "poison" };
    constexpr std::size_t Life = std::size(DamageFields) - 1;
    static_assert(std::size(Names) == Life);
    int used = std::snprintf(out, size, "life %.2f -> %.2f", before.fields[Life] / 256.0, after.fields[Life] / 256.0);
    bool open = false;
    for (std::size_t i = 0; i < Life && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
        if (before.fields[i] == 0 && after.fields[i] == 0) continue;
        used += std::snprintf(out + used, size - static_cast<std::size_t>(used), "%s%s %.2f -> %.2f",
            open ? ", " : " (", Names[i], before.fields[i] / 256.0, after.fields[i] / 256.0);
        open = true;
    }
    if (open && used > 0 && static_cast<std::size_t>(used) + 1 < size) std::snprintf(out + used, size - static_cast<std::size_t>(used), ")");
}

// Reached from the relay at 44CF98, which every hit passes after the optional
// recalculation and before the damage events, leech and the life commit.
void __fastcall HookedHitBonus(void* game, void* attacker, void* defender, void* damage) {
    (void)game;
    if (!Active.load(std::memory_order_relaxed) || damage == nullptr || !IsPlayer(attacker)
            || !IsServerUnit(attacker)) {
        return;
    }
    PlayerHits.fetch_add(1, std::memory_order_relaxed);
    const SourceDecision d     = DecideSource(attacker);
    const bool           debug = DebugOn.load(std::memory_order_relaxed);
    char source[160]{};
    if (debug) DescribeSource(d, source, sizeof(source));
    const std::uint32_t guid         = GuidOf(attacker);
    const std::uint32_t defenderType = defender != nullptr ? UnitType(defender) : 0xFFFFFFFFu;
    const std::uint32_t defenderGuid = defender != nullptr ? GuidOf(defender) : 0;
    if (!d.crossbow) {
        if (d.aura) {
            HitsSkippedAura.fetch_add(1, std::memory_order_relaxed);
            DebugLog("[hit] player %u -> %s %u via %s: no bonus, aura pulses never count", guid, UnitKind(defenderType),
                defenderGuid, source);
        } else {
            (d.viaMissile ? HitsSkippedMissile : HitsSkippedCast).fetch_add(1, std::memory_order_relaxed);
            DebugLog("[hit] player %u -> %s %u via %s: no bonus, cast %u is not a crossbow cast (last crossbow cast %u)",
                guid, UnitKind(defenderType), defenderGuid, source, d.cast, d.lastCrossbowCast);
        }
        return;
    }
    const std::int32_t   stat    = GetStat(attacker, Settings.damageStat, 0);
    const std::int32_t   percent = Settings.damageBase + stat;
    const DamageSnapshot before  = debug ? TakeSnapshot(damage) : DamageSnapshot{};
    BoostDamage(damage, percent);
    (d.viaMissile ? MissileHitsBoosted : OtherHitsBoosted).fetch_add(1, std::memory_order_relaxed);
    if (debug) {
        char values[320];
        DescribeDamage(before, TakeSnapshot(damage), values, sizeof(values));
        DebugLog("[hit] player %u -> %s %u via %s: +%d%% (base %d, stat %d), %s", guid, UnitKind(defenderType),
            defenderGuid, source, percent, Settings.damageBase, stat, values);
    }
}

// Reached from the relay in place of the call at 537A17 in the missile creator
// 5371A0, with the creation params (r14) as the third argument. Every missile
// is created there; for a player's missile this decides which cast it belongs
// to (see MissileLink).
void __fastcall HookedMissileCastId(void* missile, std::uint32_t castId, void* params) {
    SetUnitCastId(missile, castId);
    if (!Active.load(std::memory_order_relaxed) || missile == nullptr || params == nullptr) return;
    void* owner = Read<void*>(params, CreateParamsOwnerOffset);
    if (!IsPlayer(owner) || !IsServerUnit(owner)) return;
    const bool          aura        = InAuraPulse(owner);
    void*               parent      = aura ? nullptr : OwnMissileInProgress(owner);
    if (parent == missile) parent = nullptr;
    const std::uint32_t ownerGuid   = GuidOf(owner);
    const std::uint32_t ownerCast   = CastIdOf(owner);
    const std::uint32_t missileGuid = GuidOf(missile);
    const std::uint32_t parentGuid  = parent != nullptr ? GuidOf(parent) : 0;
    const std::uint32_t parentCast  = parent != nullptr ? CastIdOf(parent) : 0;
    const char*         how         = "its own cast";
    std::uint32_t       follows     = castId;
    bool                crossbow    = false;
    {
        std::lock_guard lock(ServerMutex);
        ResetServerIfNeeded(nullptr);
        if (aura) {
            how     = "aura pulse";
            follows = 0;
            RecordLink(missileGuid, castId, 0);
        } else if (parent != nullptr) {
            how     = "spawned by its missile";
            follows = CastOfMissile(parentGuid, parentCast, nullptr);
            if (follows != castId) {
                RecordLink(missileGuid, castId, follows);
            } else {
                MissileLinks.erase(missileGuid);
            }
        } else if (ValidCastId(castId) && ValidCastId(ownerCast) && castId > ownerCast) {
            how     = "new id for one missile of the current cast";
            follows = ownerCast;
            RecordLink(missileGuid, castId, ownerCast);
        } else {
            MissileLinks.erase(missileGuid);
        }
        crossbow = IsCrossbowCast(follows, ownerGuid);
    }
    PlayerMissiles.fetch_add(1, std::memory_order_relaxed);
    if (crossbow) CrossbowMissiles.fetch_add(1, std::memory_order_relaxed);
    if (follows != castId) LinkedMissiles.fetch_add(1, std::memory_order_relaxed);
    if (parent != nullptr) {
        DebugLog("[missile] player %u missile %u (row %u) cast %u: %s %u (cast %u), belongs to cast %u, %s", ownerGuid,
            missileGuid, ClassOf(missile), castId, how, parentGuid, parentCast, follows,
            crossbow ? "crossbow" : "not crossbow");
    } else {
        DebugLog("[missile] player %u missile %u (row %u) cast %u: %s, belongs to cast %u (player cast %u), %s",
            ownerGuid, missileGuid, ClassOf(missile), castId, how, follows, ownerCast,
            crossbow ? "crossbow" : "not crossbow");
    }
}

// The missile in progress for everything these three reach: hits, do
// functions (areas, trails, expiry) and the stamped damage step.
std::uint64_t __fastcall HookedMissileHit(void* game, void* missile, void* target, std::int32_t forced) {
    MissileScope scope(game, missile);
    return OriginalMissileHit(game, missile, target, forced);
}

std::uint64_t __fastcall HookedMissileDo(void* game, void* missile) {
    MissileScope scope(game, missile);
    return OriginalMissileDo(game, missile);
}

std::uint64_t __fastcall HookedMissileDamage(void* game, void* missile, void* target, void* damage) {
    MissileScope scope(game, missile);
    return OriginalMissileDamage(game, missile, target, damage);
}

// ---------------------------------------------------------------------------
// Hook: skill buttons
// ---------------------------------------------------------------------------

constexpr std::size_t TypeDescNameOffset = 0x08;
constexpr char        ImageWidgetName[]  = "ImageWidget";

void SetVisible(void* widget, bool visible) {
    auto** vtable = Read<void**>(widget, 0);
    reinterpret_cast<SetVisibleFn>(vtable[WidgetSetVisibleSlot])(widget, visible ? 1U : 0U);
}

// Reads the descriptor names only; never runs a type getter.
auto IsImageWidget(void* widget) -> bool {
    auto** vtable = Read<void**>(widget, 0);
    void*  desc   = reinterpret_cast<TypeDescFn>(vtable[WidgetTypeDescSlot])(widget);
    for (int depth = 0; desc != nullptr && depth < 32; ++depth) {
        const auto* name = Read<const char*>(desc, TypeDescNameOffset);
        if (name != nullptr && std::strncmp(name, ImageWidgetName, sizeof(ImageWidgetName)) == 0) return true;
        desc = Read<void*>(desc, TypeDescParentOffset);
    }
    return false;
}

auto FindSweep(void* button) -> void* {
    void* child = FindChild(button, Settings.sweepName.c_str());
    return child != nullptr && IsImageWidget(child) ? child : nullptr;
}

void WriteQuantity(void* button, std::int32_t bolts) {
    void* text = Read<void*>(button, ButtonQuantityOffset);
    if (text == nullptr) return;
    SetVisible(text, true);
    const bool        alternate = Read<std::uint8_t>(text, TextAltFlagA) != 0 && Read<std::uint8_t>(text, TextAltFlagB) != 0;
    const std::size_t offset    = alternate ? TextAltString : TextMainString;
    StringFormat(Bytes(text, offset), "%d", bolts);
}

// Only the no-bolts reason is cleared; a skill's own cooldown stays red.
void ClearCooldownTint(void* button) {
    if (std::memcmp(Bytes(button, ButtonTintOffset), Bytes(button, ButtonCooldownTintOffset), 16) == 0) {
        std::memcpy(Bytes(button, ButtonTintOffset), Bytes(button, ButtonNormalTintOffset), 16);
    }
}

void DrawSweep(void* sweep, const SweepView& view) {
    if (!view.reloading) {
        SetVisible(sweep, false);
        return;
    }
    const std::int32_t frames = std::max(1, Settings.sweepFrames);
    const std::int32_t frame  = std::clamp(static_cast<std::int32_t>(view.progress * static_cast<float>(frames)), 0, frames - 1);
    Write<std::int32_t>(sweep, ImageFrameOffset, frame);
    Write<float>(sweep, ImageTransparencyOffset, static_cast<float>(Settings.shadowOpacity) / 100.0f);
    Write<float>(sweep, ImageTintOffset + 0, static_cast<float>(Settings.shadowColor[0]) / 255.0f);
    Write<float>(sweep, ImageTintOffset + 4, static_cast<float>(Settings.shadowColor[1]) / 255.0f);
    Write<float>(sweep, ImageTintOffset + 8, static_cast<float>(Settings.shadowColor[2]) / 255.0f);
    Write<float>(sweep, ImageTintOffset + 12, 1.0f);
    SetVisible(sweep, true);
}

std::uint64_t __fastcall HookedButtonUpdate(void* button) {
    const std::uint64_t result = OriginalButtonUpdate(button);
    if (!Active.load(std::memory_order_relaxed) || !UiActive || button == nullptr) return result;

    void* sweep = FindSweep(button);
    const std::int32_t side    = Read<std::int32_t>(button, ButtonSideOffset);
    const std::int32_t skillId = Read<std::int32_t>(button, ButtonSkillIdOffset);
    bool      crossbow = false;
    SweepView view{};
    if ((side == 1 || side == 2) && skillId >= 0) {
        void* player = LocalPlayer();
        if (player != nullptr) {
            void* skill = SkillNode(player, skillId, Read<std::uint32_t>(button, ButtonSkillOwnerOffset));
            if (skill != nullptr && IsCrossbowSkill(player, skill)) {
                crossbow = true;
                view     = LocalView(player, GetTickCount64());
            }
        }
    }
    if (!crossbow) {
        if (sweep != nullptr) SetVisible(sweep, false);
        return result;
    }
    WriteQuantity(button, view.bolts);
    if (view.bolts <= 0) ClearCooldownTint(button);
    if (sweep != nullptr) DrawSweep(sweep, view);
    return result;
}

// ---------------------------------------------------------------------------
// Hook: crossbow attack speed
// ---------------------------------------------------------------------------

bool SequencePartVerified{};

auto IsCrossbowAttackMode(std::int32_t mode) noexcept -> bool {
    return mode == 7;   // A1 only; S1..S4 carry non-attack skills such as traps
}

// The skill the unit is using, read the way 350B40 reads it: [[unit+100h]+18h].
auto UsedSkillId(void* unit) noexcept -> std::int32_t {
    void* skills = Read<void*>(unit, UnitSkillListOffset);
    if (skills == nullptr) return -1;
    void* skill = Read<void*>(skills, SkillListUsedOffset);
    return skill != nullptr ? SkillIdOf(skill) : -1;
}

// Mirrored Blades playing its sequence: mode 18 with the sequence installed.
auto IsMirroredBladesSequence(void* unit, std::int32_t mode) noexcept -> bool {
    return SequencePartVerified && mode == SequenceMode && Settings.mirroredBladesSkill >= 0
        && Read<void*>(unit, UnitSequenceRowsOffset) != nullptr
        && UsedSkillId(unit) == Settings.mirroredBladesSkill;
}

// Reached from the relay at 351597 with the unit and the rate the game just
// worked out; returns the rate to store.
std::int32_t __fastcall HookedAttackRate(void* unit, std::int32_t rate) {
    if (!Active.load(std::memory_order_relaxed) || !IsPlayer(unit)) return rate;
    const std::int32_t mode     = Read<std::int32_t>(unit, UnitModeOffset);
    const bool         sequence = IsMirroredBladesSequence(unit, mode);
    if ((!sequence && !IsCrossbowAttackMode(mode)) || !HasCrossbow(unit)) return rate;
    const std::int32_t frames = Settings.defaultAttackFrames + GetStat(unit, Settings.attackFramesStat, 0);
    if (frames <= 0) return rate;
    std::int64_t wanted = 0;
    if (sequence) {
        // The whole sequence ends once its countdown (steps << 8) is used up, so
        // it lasts ceil(length / rate) frames: ceil(length / N) gives exactly N.
        const std::int32_t length = Read<std::int32_t>(unit, UnitSequenceLengthOffset);
        if (length <= 0) return rate;
        wanted = (static_cast<std::int64_t>(length) + frames - 1) / frames;
        SequenceRatesSet.fetch_add(1, std::memory_order_relaxed);
    } else {
        void* record = Read<void*>(unit, UnitAnimRecordOffset);
        if (record == nullptr) return rate;
        const std::int32_t fpd = Read<std::int32_t>(record, AnimRecordFramesOffset);
        if (fpd <= 0) return rate;
        // ceil(256 * FPD / (N + 1)): the smallest rate that finishes in N frames.
        wanted = (256LL * fpd + frames) / (static_cast<std::int64_t>(frames) + 1);
    }
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(wanted, 1, 0x7FFF));
}

// ---------------------------------------------------------------------------
// Relay page and redirected calls
// ---------------------------------------------------------------------------

constexpr std::size_t RelayPageBytes   = 4'096;
constexpr std::size_t DoHandlerStub     = 0x00;  // 45 8B CD / FF 25 00000000 / dq
constexpr std::size_t ProcCastIdStub    = 0x20;  // FF 25 00000000 / dq
constexpr std::size_t MissileCastIdStub = 0x40;  // 4D 8B C6 / FF 25 00000000 / dq
constexpr std::size_t AttackRateStub    = 0x60;  // save, call HookedAttackRate(rsi, edi), mov ebx,7FFFh, ret
constexpr std::size_t HitBonusStub      = 0xA0;  // save, call HookedHitBonus(r15, r14, rsi, rdi), replay, ret

void* RelayPage{};
bool  DoHandlerPatched{};
bool  ProcCastIdPatched{};
bool  MissileCastIdPatched{};
bool  HitBonusPatched{};
bool  AttackRatePatched{};
bool  EventSlotPatched{};

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - static_cast<std::int64_t>(CallSize);
    return delta >= std::numeric_limits<std::int32_t>::min() && delta <= std::numeric_limits<std::int32_t>::max();
}

auto AllocateNear(std::uintptr_t low, std::uintptr_t high, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned     = high & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(low, candidate + size)) break;
        if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

void EncodeCall(std::uintptr_t site, std::uintptr_t target, std::uint8_t* out) noexcept {
    out[0] = 0xE8;
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + CallSize));
    std::memcpy(out + 1, &displacement, sizeof(displacement));
}

auto BuildRelayPage() -> bool {
    const std::uintptr_t low  = Base + AttackRateSiteRva;
    const std::uintptr_t high = Base + ProcCastIdCallRva;
    RelayPage = AllocateNear(low, high, RelayPageBytes);
    if (RelayPage == nullptr) return false;
    auto* page = static_cast<std::uint8_t*>(RelayPage);
    std::memset(page, 0xCC, RelayPageBytes);
    const auto put = [&](std::size_t at, const std::uint8_t* prefix, std::size_t prefixSize, const void* target) {
        if (prefixSize != 0) std::memcpy(page + at, prefix, prefixSize);
        const std::uint8_t jump[]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(page + at + prefixSize, jump, sizeof(jump));
        const auto address = reinterpret_cast<std::uint64_t>(target);
        std::memcpy(page + at + prefixSize + sizeof(jump), &address, sizeof(address));
    };
    const std::uint8_t moveSkillId[]{ 0x45, 0x8B, 0xCD };  // mov r9d, r13d
    put(DoHandlerStub, moveSkillId, sizeof(moveSkillId), reinterpret_cast<const void*>(&HookedDoHandlerGlobal));
    put(ProcCastIdStub, nullptr, 0, reinterpret_cast<const void*>(&HookedProcCastId));
    const std::uint8_t moveParams[]{ 0x4D, 0x8B, 0xC6 };  // mov r8, r14
    put(MissileCastIdStub, moveParams, sizeof(moveParams), reinterpret_cast<const void*>(&HookedMissileCastId));
    // Entered by the call at 351597, so [rsp] returns to 35159C. Seven pushes
    // plus the return address keep rsp 16-byte aligned for the inner call.
    const std::uint8_t attackRate[]{
        0x50,                                // push rax
        0x51,                                // push rcx
        0x52,                                // push rdx
        0x41, 0x50,                          // push r8
        0x41, 0x51,                          // push r9
        0x41, 0x52,                          // push r10
        0x41, 0x53,                          // push r11
        0x48, 0x83, 0xEC, 0x20,              // sub  rsp, 20h
        0x48, 0x8B, 0xCE,                    // mov  rcx, rsi        ; unit
        0x8B, 0xD7,                          // mov  edx, edi        ; rate
        0xFF, 0x15, 0x1E, 0x00, 0x00, 0x00,  // call [rip+1Eh]       ; slot at +38h
        0x48, 0x83, 0xC4, 0x20,              // add  rsp, 20h
        0x8B, 0xF8,                          // mov  edi, eax
        0x41, 0x5B,                          // pop  r11
        0x41, 0x5A,                          // pop  r10
        0x41, 0x59,                          // pop  r9
        0x41, 0x58,                          // pop  r8
        0x5A,                                // pop  rdx
        0x59,                                // pop  rcx
        0x58,                                // pop  rax
        0xBB, 0xFF, 0x7F, 0x00, 0x00,        // mov  ebx, 7FFFh      ; the replaced instruction
        0xC3,                                // ret
    };
    static_assert(sizeof(attackRate) == 0x31);
    std::memcpy(page + AttackRateStub, attackRate, sizeof(attackRate));
    const auto attackRateTarget = reinterpret_cast<std::uint64_t>(&HookedAttackRate);
    std::memcpy(page + AttackRateStub + 0x38, &attackRateTarget, sizeof(attackRateTarget));
    // Entered by the call at 44CF98, so [rsp] returns to the nop at 44CF9D.
    // Same frame shape as above. The site held no call, so every volatile
    // register is kept, and the stub ends by replaying the two instructions
    // the call replaced: eax and the flags leave exactly as the game made them.
    const std::uint8_t hitBonus[]{
        0x50,                                // push rax
        0x51,                                // push rcx
        0x52,                                // push rdx
        0x41, 0x50,                          // push r8
        0x41, 0x51,                          // push r9
        0x41, 0x52,                          // push r10
        0x41, 0x53,                          // push r11
        0x48, 0x83, 0xEC, 0x20,              // sub  rsp, 20h
        0x4C, 0x89, 0xF9,                    // mov  rcx, r15        ; game
        0x4C, 0x89, 0xF2,                    // mov  rdx, r14        ; attacker
        0x49, 0x89, 0xF0,                    // mov  r8, rsi         ; defender
        0x49, 0x89, 0xF9,                    // mov  r9, rdi         ; damage
        0xFF, 0x15, 0x17, 0x00, 0x00, 0x00,  // call [rip+17h]       ; slot at +38h
        0x48, 0x83, 0xC4, 0x20,              // add  rsp, 20h
        0x41, 0x5B,                          // pop  r11
        0x41, 0x5A,                          // pop  r10
        0x41, 0x59,                          // pop  r9
        0x41, 0x58,                          // pop  r8
        0x5A,                                // pop  rdx
        0x59,                                // pop  rcx
        0x58,                                // pop  rax
        0x0F, 0xB7, 0x47, 0x04,              // movzx eax, word [rdi+4]   ; replayed
        0xA8, 0x20,                          // test al, 20h              ; replayed
        0xC3,                                // ret
    };
    static_assert(sizeof(hitBonus) == 0x37);
    std::memcpy(page + HitBonusStub, hitBonus, sizeof(hitBonus));
    const auto hitBonusTarget = reinterpret_cast<std::uint64_t>(&HookedHitBonus);
    std::memcpy(page + HitBonusStub + 0x38, &hitBonusTarget, sizeof(hitBonusTarget));
    DWORD previous = 0;
    const bool sealed = VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);
    return sealed;
}

auto RedirectCall(std::uint64_t callRva, const std::uint8_t* original, std::size_t stub) -> bool {
    const std::uintptr_t site   = Base + callRva;
    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(RelayPage) + stub;
    if (!CanEncodeRel32(site, target)) return false;
    std::uint8_t call[CallSize]{};
    EncodeCall(site, target, call);
    return Context->PatchBytes(callRva, original, CallSize, call, CallSize);
}

void RestoreCall(std::uint64_t callRva, const std::uint8_t* original, std::size_t stub, bool& patched) {
    if (!patched) return;
    std::uint8_t current[CallSize]{};
    EncodeCall(Base + callRva, reinterpret_cast<std::uintptr_t>(RelayPage) + stub, current);
    if (Context->PatchBytes(callRva, current, CallSize, original, CallSize)) patched = false;
}

// 44CF98: movzx eax,word [rdi+4] / test al,20h -> call HitBonusStub / nop.
void EncodeHitBonusSite(std::uint8_t (&out)[HitBonusPatchSize]) noexcept {
    EncodeCall(Base + HitBonusSiteRva, reinterpret_cast<std::uintptr_t>(RelayPage) + HitBonusStub, out);
    out[CallSize] = 0x90;
}

auto PatchHitBonusSite() -> bool {
    if (!CanEncodeRel32(Base + HitBonusSiteRva, reinterpret_cast<std::uintptr_t>(RelayPage) + HitBonusStub)) return false;
    std::uint8_t patch[HitBonusPatchSize]{};
    EncodeHitBonusSite(patch);
    return Context->PatchBytes(HitBonusSiteRva, HitBonusWindow + HitBonusSiteOffset, HitBonusPatchSize,
        patch, HitBonusPatchSize);
}

void RestoreHitBonusSite() {
    if (!HitBonusPatched) return;
    std::uint8_t current[HitBonusPatchSize]{};
    EncodeHitBonusSite(current);
    if (Context->PatchBytes(HitBonusSiteRva, current, HitBonusPatchSize, HitBonusWindow + HitBonusSiteOffset,
            HitBonusPatchSize)) {
        HitBonusPatched = false;
    }
}

std::uint64_t* CoreEventSlot20{};  // D2RCore's handler table entry 20 while it holds the hook

auto IsReadable(const void* address, std::size_t size) -> bool {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT
            || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
    return reinterpret_cast<std::uintptr_t>(address) + size
        <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}

auto WriteCoreSlot(std::uint64_t* slot, std::uint64_t value) -> bool {
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return false;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(value));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    return *slot == value;
}

// Finds D2RCore's handler for event function 20 through RegisterWideSkillEffect,
// checks the export and the handler byte for byte, and binds the handler as the
// function the hook calls through.
auto LocateCoreEventFunc20() -> bool {
    const HMODULE core = GetModuleHandleW(CoreModuleName);
    if (core == nullptr) {
        Context->LogError("CrossbowCharges: D2RCore.dll is not loaded.");
        return false;
    }
    const auto* stub = reinterpret_cast<const std::uint8_t*>(GetProcAddress(core, CoreRegisterExport));
    if (stub == nullptr || !IsReadable(stub, sizeof(CoreRegisterStub))
            || std::memcmp(stub, CoreRegisterStub, sizeof(CoreRegisterStub)) != 0) {
        Context->LogError("CrossbowCharges: D2RCore's RegisterWideSkillEffect is not the verified D2RLoader 1.3.1 code.");
        return false;
    }
    std::int32_t disp = 0;
    std::memcpy(&disp, stub + CoreRegisterTableLeaOffset + 3, sizeof(disp));
    auto* table = reinterpret_cast<std::uint64_t*>(const_cast<std::uint8_t*>(stub + CoreRegisterTableLeaOffset + 7 + disp));
    if (!IsReadable(table, (CoreEventFunc20Index + 1) * sizeof(std::uint64_t))) {
        Context->LogError("CrossbowCharges: D2RCore's event handler table is not readable.");
        return false;
    }
    const auto* handler = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(table[CoreEventFunc20Index]));
    if (handler == nullptr || !IsReadable(handler, sizeof(CoreEventFunc20Body))
            || std::memcmp(handler, CoreEventFunc20Body, sizeof(CoreEventFunc20Body)) != 0) {
        Context->LogError("CrossbowCharges: D2RCore's event function 20 handler is not the verified D2RLoader 1.3.1 code.");
        return false;
    }
    NativeEventFunc20 = reinterpret_cast<EventFunc20Fn>(const_cast<std::uint8_t*>(handler));
    CoreEventSlot20   = &table[CoreEventFunc20Index];
    return true;
}

auto PatchEventSlot() -> bool {
    if (CoreEventSlot20 == nullptr || NativeEventFunc20 == nullptr) return false;
    return WriteCoreSlot(CoreEventSlot20, reinterpret_cast<std::uint64_t>(&HookedEventFunc20));
}

// Events registered while the hook was in keep calling it; the DLL is expected
// to stay resident, as with every other hook here.
void RestoreEventSlot() {
    if (!EventSlotPatched || CoreEventSlot20 == nullptr) return;
    if (WriteCoreSlot(CoreEventSlot20, reinterpret_cast<std::uint64_t>(NativeEventFunc20))) EventSlotPatched = false;
}

void RemovePatches() {
    RestoreCall(AttackRateSiteRva, AttackRateSite, AttackRateStub, AttackRatePatched);
    RestoreHitBonusSite();
    RestoreEventSlot();
    RestoreCall(MissileCastIdCallRva, MissileCastIdWindow + MissileCastIdCallOffset, MissileCastIdStub,
        MissileCastIdPatched);
    RestoreCall(ProcCastIdCallRva, ProcCastIdWindow + ProcCastIdCallOffset, ProcCastIdStub, ProcCastIdPatched);
    RestoreCall(DoHandlerCallRva, DoHandlerWindow + DoHandlerCallOffset, DoHandlerStub, DoHandlerPatched);
    // The relay page is kept: a thread may be inside it right now.
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto VerifyAll() -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "CrossbowCharges: %s at RVA 0x%llX does not match this build.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    if (!LocateCoreEventFunc20()) ok = false;
    return ok;
}

auto VerifyUi() -> bool {
    bool ok = true;
    for (const Witness& witness : UiWitnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "CrossbowCharges: %s at RVA 0x%llX does not match this build.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

void BindNatives() {
    ServerGlobalDelay = At<ServerGlobalFn>(ServerGlobalDelayRva);
    ServerLocalDelay  = At<ServerLocalFn>(ServerLocalDelayRva);
    ClientGlobalDelay = At<ClientGlobalFn>(ClientGlobalDelayRva);
    ClientLocalDelay  = At<ClientLocalFn>(ClientLocalDelayRva);
    MissileOwner      = At<MissileOwnerFn>(MissileOwnerRva);
    SetUnitCastId     = At<SetCastIdFn>(SetCastIdRva);
    GenerateCastId    = At<GenerateCastIdFn>(GenerateCastIdRva);
    SkillsRecord      = At<SkillsRecordFn>(SkillsRecordRva);
    Evaluate          = At<EvaluateFn>(EvaluateFormulaRva);
    DataContext       = At<DataContextFn>(DataContextRva);
    UnitType          = At<UnitTypeFn>(UnitTypeRva);
    UnitId            = At<UnitIdFn>(UnitIdRva);
    Inventory         = At<InventoryFn>(InventoryRva);
    LeftHandWeapon    = At<WeaponFn>(LeftHandWeaponRva);
    CheckItemType     = At<CheckItemTypeFn>(CheckItemTypeRva);
    GetStat           = At<GetStatFn>(GetUnitStatRva);
    SetStat           = At<SetStatFn>(SetUnitStatRva);
    CheckState        = At<CheckStateFn>(CheckStateRva);
    StateStatList     = At<StateStatListFn>(StateStatListRva);
    ReadListStat      = At<ReadListStatFn>(ReadListStatRva);
    SkillLevel        = At<SkillLevelFn>(SkillLevelRva);
    SkillNode         = At<SkillNodeFn>(SkillNodeRva);
    LocalPlayerIndex  = At<PlayerIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex   = At<PlayerFromFn>(PlayerFromIndexRva);
    FindChild         = At<FindChildFn>(FindChildWidgetRva);
    StringFormat      = At<StringFormatFn>(StringFormatRva);
}

auto InstallHooks() -> bool {
    return Context->InstallInlineHook(CooldownGateRva, CooldownGateBytes, sizeof(CooldownGateBytes), &HookedGate, &OriginalGate)
        && Context->InstallInlineHook(ClientApplyRva, ClientApplyBytes, sizeof(ClientApplyBytes), &HookedClientApply, &OriginalClientApply)
        && Context->InstallInlineHook(ServerApplyRva, ServerApplyBytes, sizeof(ServerApplyBytes), &HookedServerApply, &OriginalServerApply)
        && Context->InstallInlineHook(PlayerRegenRva, PlayerRegenBytes, sizeof(PlayerRegenBytes), &HookedPlayerRegen, &OriginalPlayerRegen)
        && Context->InstallInlineHook(SetUsedSkillRva, SetUsedSkillBytes, sizeof(SetUsedSkillBytes), &HookedSetUsedSkill, &OriginalSetUsedSkill)
        && Context->InstallInlineHook(MissileHitRva, MissileHitBytes, sizeof(MissileHitBytes), &HookedMissileHit, &OriginalMissileHit)
        && Context->InstallInlineHook(MissileDoRva, MissileDoBytes, sizeof(MissileDoBytes), &HookedMissileDo, &OriginalMissileDo)
        && Context->InstallInlineHook(MissileDamageRva, MissileDamageBytes, sizeof(MissileDamageBytes), &HookedMissileDamage, &OriginalMissileDamage);
}

auto InstallPatches() -> bool {
    if (!BuildRelayPage()) {
        Context->LogError("CrossbowCharges: no relay page could be placed within reach of the game code.");
        return false;
    }
    DoHandlerPatched = RedirectCall(DoHandlerCallRva, DoHandlerWindow + DoHandlerCallOffset, DoHandlerStub);
    if (!DoHandlerPatched) return false;
    ProcCastIdPatched = RedirectCall(ProcCastIdCallRva, ProcCastIdWindow + ProcCastIdCallOffset, ProcCastIdStub);
    if (!ProcCastIdPatched) return false;
    MissileCastIdPatched = RedirectCall(MissileCastIdCallRva, MissileCastIdWindow + MissileCastIdCallOffset,
        MissileCastIdStub);
    if (!MissileCastIdPatched) return false;
    EventSlotPatched = PatchEventSlot();
    if (!EventSlotPatched) return false;
    // Last, so the bonus only goes live once everything that judges a hit is in.
    HitBonusPatched = PatchHitBonusSite();
    if (!HitBonusPatched) return false;
    // Optional part: a mismatch here only turns the fixed attack speed off.
    if (Context->CheckExpectedBytes(AttackRateWindowRva, AttackRateWindow, sizeof(AttackRateWindow))
            && Context->CheckExpectedBytes(AttackRateSiteRva, AttackRateSite, sizeof(AttackRateSite))) {
        AttackRatePatched = RedirectCall(AttackRateSiteRva, AttackRateSite, AttackRateStub);
    }
    if (!AttackRatePatched) {
        Context->LogWarn("CrossbowCharges: the animation rate code does not match; crossbow attacks keep the game's own "
                         "speed.");
    } else {
        // Optional part: a mismatch here only leaves Mirrored Blades at the game's speed.
        SequencePartVerified = true;
        for (const Witness& witness : SequenceWitnesses) {
            if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
                D2RL::LogWarnF(Context, "CrossbowCharges: %s at RVA 0x%llX does not match; Mirrored Blades keeps the "
                    "game's own speed.", witness.name, static_cast<unsigned long long>(witness.rva));
                SequencePartVerified = false;
            }
        }
    }
    return true;
}

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing installed";
    case PluginState::InstallFailed:    return "install failed, nothing active";
    case PluginState::Active:           return "active";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    ResolveCrossbowTypes(context);
}

void __cdecl OnGameChanged(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ResetRequested.store(true);
    std::lock_guard lock(ClientMutex);
    Client = ClientPool{};
}

auto RegisterLifecycle() -> bool {
    if (Context->QueryService(&DataTables)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(DataTables, D2RL::DataTableServiceRequiredSize)) {
        DataTables = nullptr;
        Context->LogError("CrossbowCharges: the data table service is unavailable.");
        return false;
    }
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle)
            != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogError("CrossbowCharges: the lifecycle service is unavailable.");
        return false;
    }
    D2RL::Lifecycle::DataTablesLoadedListener tables{};
    tables.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize;
    tables.callback   = &OnDataTablesLoaded;
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerDataTablesLoadedListener(Context, &tables, &handle) != D2RL::Lifecycle::Result::Success
            || handle == D2RL::Lifecycle::InvalidHandle) {
        Context->LogError("CrossbowCharges: the data table listener was refused.");
        return false;
    }
    for (const D2RL::Lifecycle::GameplayEventKind kind :
            { D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::GameLeft }) {
        D2RL::Lifecycle::GameplayEventListener game{};
        game.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
        game.kind       = kind;
        game.callback   = &OnGameChanged;
        D2RL::Lifecycle::ListenerHandle gameHandle = D2RL::Lifecycle::InvalidHandle;
        if (lifecycle->registerGameplayEventListener(Context, &game, &gameHandle) != D2RL::Lifecycle::Result::Success) {
            Context->LogWarn("CrossbowCharges: a game join/leave listener was refused; pools are still reset when "
                             "a new game starts.");
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto CommandArgs(const D2RL::ConsoleCommandContext* command) -> std::string_view {
    if (command == nullptr || command->args == nullptr || command->argsLength == 0) return {};
    return Trim(std::string_view(command->args, command->argsLength));
}

auto Count(const std::atomic<std::uint64_t>& counter) -> unsigned long long {
    return static_cast<unsigned long long>(counter.load(std::memory_order_relaxed));
}

//   crossbowcharges              state, bolt pools, counters
//   crossbowcharges debug on|off the debug log (no argument toggles it)
//   crossbowcharges reset        zero the counters
auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    char line[320];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };
    const std::string_view args = CommandArgs(command);
    if (args.substr(0, 5) == "debug") {
        const std::string_view value = Trim(args.substr(5));
        bool on = !DebugOn.load();
        if (value == "on") {
            on = true;
        } else if (value == "off") {
            on = false;
        } else if (!value.empty()) {
            say("Usage: crossbowcharges debug [on|off]");
            return D2RL::ConsoleCommandResult::Handled;
        }
        DebugOn.store(on);
        if (on) {
            D2RL::LogInfoF(Context, "CrossbowCharges debug: on (damage base %d%%, damage stat %d). Lines: [start] skill "
                "starts, [spend] bolts, [reload] standing still on/off, [missile] player missiles, [hit] player hits, "
                "[proc] procs.",
                Settings.damageBase, Settings.damageStat);
        } else {
            Context->LogInfo("CrossbowCharges debug: off.");
        }
        say(on ? "Crossbow Charges: debug log on, see d2rloader/logs/celestialrayone.crossbow-charges.log."
               : "Crossbow Charges: debug log off.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (args == "reset") {
        ResetCounters();
        say("Crossbow Charges: counters reset.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (!args.empty()) {
        say("Usage: crossbowcharges [debug [on|off] | reset]");
        return D2RL::ConsoleCommandResult::Handled;
    }

    std::snprintf(line, sizeof(line), "Crossbow Charges %s: %s%s. Damage bonus %d%% + stat %d. Debug log %s.",
        PluginVersion, StateName(State),
        State == PluginState::Active && !UiActive ? " (skill button visuals off)" : "", Settings.damageBase,
        Settings.damageStat, DebugOn.load() ? "on" : "off");
    say(line);
    std::snprintf(line, sizeof(line), "  attack speed: %s, %d frames + stat %d; mirrored blades (skill %d) %s.",
        AttackRatePatched ? "fixed" : "game speed (not installed)", Settings.defaultAttackFrames,
        Settings.attackFramesStat, Settings.mirroredBladesSkill,
        Settings.mirroredBladesSkill < 0 ? "off in config"
            : (AttackRatePatched && SequencePartVerified) ? "fixed" : "game speed (not installed)");
    say(line);
    std::snprintf(line, sizeof(line), "  standing still (modes 1 and 5): %s.",
        !StandingPartVerified ? "no faster reload (the unit mode field does not match this build)"
            : Settings.standingReloadBonus == 0 ? "no faster reload (off in config)" : "bolts reload faster");
    say(line);
    if (StandingPartVerified && Settings.standingReloadBonus != 0) {
        std::snprintf(line, sizeof(line), "    %d%% faster: a 25 frame (1 second) reload takes %lld frames standing still.",
            Settings.standingReloadBonus, static_cast<long long>((2500 + ReloadRate(true) - 1) / ReloadRate(true)));
        say(line);
    }
    {
        std::lock_guard lock(ServerMutex);
        std::size_t shown = 0;
        for (const auto& [guid, pool] : Pools) {
            if (!pool.initialized) continue;
            if (++shown > 4) break;
            std::snprintf(line, sizeof(line), "  player %u: %d of %d bolts, %zu reloading%s", guid, pool.current,
                pool.lastMax, pool.reloads.size(), pool.reloads.empty() ? "." : ", next in ");
            if (!pool.reloads.empty()) {
                const std::int64_t left = std::max<std::int64_t>(0,
                    static_cast<std::int64_t>(std::max(1, pool.reloads.front())) * 100 - pool.progress);
                const std::int64_t fast = ReloadRate(true);
                const std::size_t  used = std::strlen(line);
                std::snprintf(line + used, sizeof(line) - used, "%lld frames, %lld standing still; reloading at %lld%% "
                    "now.", static_cast<long long>((left + 99) / 100), static_cast<long long>((left + fast - 1) / fast),
                    static_cast<long long>(ReloadRate(pool.standing)));
            }
            say(line);
        }
        if (shown == 0) say("  no player holds a crossbow in this game yet.");
    }
    std::snprintf(line, sizeof(line), "  casts: %llu player skill starts, %llu crossbow (tagged at start); %llu tagged "
        "only when their bolt was spent; procs %llu seen, %llu tagged.", Count(SkillStarts), Count(CastsTagged),
        Count(CastsTaggedAtSpend), Count(ProcsSeen), Count(ProcsTagged));
    say(line);
    std::snprintf(line, sizeof(line), "  missiles: %llu by players, %llu of crossbow casts, %llu following a parent "
        "missile or their cast.", Count(PlayerMissiles), Count(CrossbowMissiles), Count(LinkedMissiles));
    say(line);
    std::snprintf(line, sizeof(line), "  hits: %llu by players, %llu boosted (%llu by missiles, %llu other); no bonus: "
        "%llu aura pulses, %llu missiles and %llu casts that are not crossbow.", Count(PlayerHits),
        Count(MissileHitsBoosted) + Count(OtherHitsBoosted), Count(MissileHitsBoosted), Count(OtherHitsBoosted),
        Count(HitsSkippedAura), Count(HitsSkippedMissile), Count(HitsSkippedCast));
    say(line);
    std::snprintf(line, sizeof(line), "  bolts: %llu spent, %llu refused, %llu repeat steps, %llu reloaded; item types "
        "per bank %u / %u / %u.", Count(BoltsSpent), Count(BoltsRefused), Count(RepeatStepsIgnored),
        Count(BoltsReloaded), CrossbowTypes[1].count.load(), CrossbowTypes[2].count.load(),
        CrossbowTypes[3].count.load());
    say(line);
    if (Count(MissileDepthLeaks) != 0) {
        std::snprintf(line, sizeof(line), "  warning: missile tracking out of balance %llu times (see the log).",
            Count(MissileDepthLeaks));
        say(line);
    }
    {
        std::lock_guard lock(ClientMutex);
        if (Client.synced) {
            std::snprintf(line, sizeof(line), "  joined client copy: %d bolts, max %d, %zu reloading, %zu casts waiting.",
                Client.lastStat, Client.lastMax, Client.reloads.size(), Client.predictions.size());
            say(line);
        }
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Crossbow Charges",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Crossbow skills fire from a reloading pool of bolts with a skill-button shadow, and crossbow "
                   "hits deal a stat-driven damage bonus.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("crossbowcharges", &StatusCommand,
            "Crossbow Charges status and counters. \"crossbowcharges debug on|off\" writes the debug log, "
            "\"crossbowcharges reset\" zeroes the counters.")) {
        Context->LogWarn("CrossbowCharges: the console command could not be registered.");
    }

    ReadConfiguration();
    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("CrossbowCharges: disabled by config.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("CrossbowCharges: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    // Optional part: a mismatch here only leaves bolts at the normal reload
    // speed while standing still.
    StandingPartVerified = Context->CheckExpectedBytes(UnitModeWitnessRva, UnitModeWitnessBytes,
        sizeof(UnitModeWitnessBytes));
    if (!StandingPartVerified) {
        D2RL::LogWarnF(Context, "CrossbowCharges: the unit mode field at RVA 0x%llX does not match; standing still "
            "gives no faster reload.", static_cast<unsigned long long>(UnitModeWitnessRva));
    }
    UiActive = VerifyUi();
    if (!UiActive) {
        Context->LogWarn("CrossbowCharges: the skill button parts do not match; bolts and damage work, the buttons "
                         "show nothing extra.");
    }

    if (!RegisterLifecycle()) {
        State = PluginState::InstallFailed;
        return true;
    }
    if (!InstallHooks() || !InstallPatches()) {
        RemovePatches();
        State = PluginState::InstallFailed;
        Context->LogError("CrossbowCharges: a hook or patch could not be installed; the plugin unloads.");
        return false;
    }
    if (UiActive && !Context->InstallInlineHook(ButtonUpdateRva, ButtonUpdateBytes, sizeof(ButtonUpdateBytes),
            &HookedButtonUpdate, &OriginalButtonUpdate)) {
        UiActive = false;
        Context->LogWarn("CrossbowCharges: the skill button hook could not be installed; the buttons show nothing extra.");
    }
    DebugOn.store(Settings.debug);
    Active.store(true);
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "CrossbowCharges: active (max bolts stat %d, current bolts stat %d, damage stat %d, "
        "damage base %d%%, standing reload bonus %d%% %s, skill buttons %s, attack frames %d + stat %d %s, mirrored "
        "blades skill %d %s, debug log %s).",
        Settings.maxBoltsStat,
        Settings.currentBoltsStat, Settings.damageStat, Settings.damageBase, Settings.standingReloadBonus,
        StandingPartVerified ? "on" : "off", UiActive ? "on" : "off",
        Settings.defaultAttackFrames, Settings.attackFramesStat, AttackRatePatched ? "on" : "off",
        Settings.mirroredBladesSkill,
        (Settings.mirroredBladesSkill >= 0 && AttackRatePatched && SequencePartVerified) ? "on" : "off",
        Settings.debug ? "on" : "off");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    if (Context != nullptr) RemovePatches();
}
