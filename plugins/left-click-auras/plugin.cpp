// =============================================================================
//  Left Click Auras  -  celestialrayone.left-click-auras
//
//  D2R 3.3 under D2RLoader 1.3.1. An aura on the LEFT mouse button runs like an
//  aura on the right button, and both run at the same time. Port of ESR's 2.4
//  left click aura patch set, plus controller support:
//
//    1. LEFT SLOT AURAS. Putting an aura on the left button starts it; taking
//       it off, by any route, stops it and removes its state and bonuses.
//    2. SINGLE EMANATION. The same aura on both buttons emanates once.
//    3. MOVE ONLY CLICKS. A left click on a monster with an aura on the left
//       button moves toward it instead of doing nothing.
//    4. LIVE LEVEL. Every tick the left aura takes its level from the skill on
//       the left button, as the right button's aura does.
//    5. CONTROLLER AURAS. Auras can be switched on with a controller, and up to
//       two run at once, one per side.
//    6. AURA LEVEL REFRESH. An aura that comes back at a higher level keeps its
//       state instead of blinking off, on either button and for every unit.
//    7. ITEM REFRESH KEEPS AURAS. An aura granted by an item (oskill), on either
//       button, keeps running while the game re-checks the equipment, instead
//       of being switched off and started again.
//
//  Every address below was read out of the dumped D2RLoader 1.3.1 image
//  (md5 8af7a47ce9077de0378c69af2930854d) and disassembled before being relied
//  on. Byte witnesses guard every function the plugin changes or relies on, so
//  a different build refuses cleanly.
//
// -----------------------------------------------------------------------------
//  HOW THE GAME RUNS AN AURA (server)
// -----------------------------------------------------------------------------
//  0x438A70  AssignSkill(unit, side, skillId, ownerGuid); side != 0 is the left
//            button, side 0 the right one. rbp = unit, r15d = side, r14d = skill.
//      left   0x438AEF  SetLeftActiveSkill, then 0x438B96 test r15d,r15d /
//                       jnz 0x438C85 skips every aura step.
//      right  0x438AF6  old right node into r12, SetRightActiveSkill
//             0x438B9F  old right aura, any aura on the left ignored:
//                       0x438C02 GetStateStatList -> unlink, free
//                       0x438C33 ToggleState(unit, state, 0)
//                       0x438C52 delete (8, -1)
//             0x438C80  new right aura: AuraStart(game, unit, skillId)
//
//  0x43B680  AuraStart: skills.txt record, aura flag byte[rec+24h] & [1D996E4h],
//            aurastate word[rec+A0h] inside the state table, real level, new
//            cast id. State stat list (reused or allocated) gets stat 350 =
//            skill, 351 = level, 202 = cast id, state on. Then
//            0x43B8FD ArmPeriodicSkill(game, unit, skill, level, castId, 1).
//
//  0x43B2B0  ArmPeriodicSkill: event type 8 with custom id -1 when the last
//            argument is 1 (the right button's timer), else the skill id (a
//            timer of its own, level in customParam, cast id in arg7). It
//            deletes that (8, id) event before setting it, so arming again
//            never stacks timers. Ticks land on frames aligned to the period
//            (perdelay, at least 5), the same frames 0x434CC0 gives srvdofunc
//            65 for the duration of the state list it applies.
//
//  0x437460  the type 8 event(game, unit, customId, customParam, arg7)
//      id -1  the right button's timer. Reads the RIGHT button (0x437499),
//             needs the aura flag and a living unit (0x437503), takes the
//             level every tick (0x4374CE, ReadWideSkillLevel(unit, node, 1,
//             0)), sets the timer's cast id on the unit (0x437538 / 0x437545),
//             unit flag 20000000h around the perform (0x437558 / 0x43758E),
//             performs at 0x43757E (0x43ACB0: level, 1, 0, 0), re-arms with
//             the last argument 1 and puts the unit's cast id back. A right
//             skill that is no longer an aura, or a dead unit, deletes (8, -1).
//      id > 0 the timer of a cast skill. It checks the aura state's stat list,
//             not a button: state on (0x43767A), list present (0x437691), unit
//             still has the skill (0x4376C5), stat 350 on the list == skill
//             (0x4376ED); level from stat 351 of that list (0x4376B5). Any
//             failure calls 0x432450, which deletes (8, skill).
//
//  That stat list is SHARED. Everything that applies the same state to the
//  unit writes it: the srvdofunc 65 callback 0x5616F0 stamps 350 / 351 / 202 at
//  0x561D16 / 0x561D38 / 0x561D5A and resets the expiry when the list's owner
//  is another unit (0x5617FB). The applier's rules (D2MOO sub_6FD10EC0):
//      same skill, same level   list reused, expiry set by this application
//      same skill, list higher  this application is refused, nothing refreshed
//      same skill, list lower   list freed and rebuilt (see AURA LEVEL REFRESH)
//      other skill              list freed and rebuilt for the other skill
//  A list expires one frame after the tick of whoever applied it last.
//
//  WHY LEFT AURAS TURNED THEMSELVES OFF (1.1.0 and earlier): the left aura ran
//  on the id > 0 timer. A hireling, bound demon, party member or item with the
//  same aura at a higher level took the list over and refused the player's own
//  application; when that unit left range, died or lost the aura, the list
//  expired and the next left tick found the state off. Another skill row with
//  the same aurastate (an item or monster version of the aura) rebuilt the
//  list with its own stat 350. An item aura with the same state being removed
//  or refreshed cleared it. Every one of those failed the id > 0 checks, and
//  the aura stayed off until it was selected again. The right button's timer
//  never reads that list, so the same events only made a right aura blink.
//
// -----------------------------------------------------------------------------
//  THE FIX (server)
// -----------------------------------------------------------------------------
//  0x438AEF  call relay on the left branch's SetLeftActiveSkill (rcx unit,
//            edx skill, r8d owner guid; eax unused afterwards). The stock code
//            only gets there after 0x33DCD0 found the skill. Left skill before
//            and after the stock select:
//              - the old left aura is not the new left skill: remove its state
//                stat list and state (kept when the right button's aura uses
//                the same state and its timer runs), delete (8, old skill).
//              - the new left skill is an aura: nothing when it is the same
//                aura with this plugin's timer live and its state on;
//                otherwise AuraStart runs with its final arm switched to the
//                skill's own timer (next item).
//  0x43B8FD  call relay. Only for the AuraStart the relay above runs: the last
//            argument becomes 0, and the (unit, skill, cast id) of the timer is
//            remembered. Every other AuraStart runs untouched.
//  0x438C02,
//  0x438C33  call relays inside the right button's aura removal. When the left
//            button holds a running aura with that same state, the state stat
//            list is reported missing and the state stays on. The right timer
//            is still deleted.
//  0x437460  inline hook. A remembered left timer (unit, skill, exact cast id)
//            runs the right button's timer logic against the LEFT button and
//            never reaches the id > 0 checks: left button still holds the
//            skill, aura flag, living unit, level (live or the armed one),
//            cast id and unit flag exactly as the id -1 branch sets them,
//            perform through 0x43ACB0 (skipped for single_emanation), re-arm
//            through 0x43B2B0 with the last argument 0. Otherwise it stops
//            the aura: state stat list and state removed (kept when the right
//            button's running aura uses the same state), (8, skill) deleted.
//            Every other timer runs stock, so timers the game creates itself
//            (monsters, cast skills) are never touched.
//
// -----------------------------------------------------------------------------
//  MOVE ONLY CLICKS (client)
// -----------------------------------------------------------------------------
//  0x102490  world click handler(command). rdi = command; flags dword at +0
//            (1 left button, 2 right, 4 press, 20h shift), player +8, target
//            +10h, skill node +28h (r14 = command+28h).
//  0x102753  call 0x33F770(player, node), which returns dword [node+10h]:
//            nonzero sends the click into the skill branch. There 0x100970
//            asks the usability gate 0x100760 -> 0x21B170 -> classifier
//            0x33F360, which returns 6 for an aura (byte[rec+24h] &
//            [1D996E4h]); the gate lets only 0 and 5 through, so nothing
//            happens. Zero leaves the skill branch: a monster press is sent as
//            a move toward it (0x0FABE0 with the command's unit opcode), NPCs
//            and every other unit type go to 0x101910 (talk, interact, pick
//            up, use, with a move toward the target when out of range).
//            Call relay: lea r8,[r14-28h] hands the command over; a left button
//            click whose node is the player's left skill holding an aura
//            returns 0. Everything else calls 0x33F770.
//
// -----------------------------------------------------------------------------
//  CONTROLLER AURAS (client)
// -----------------------------------------------------------------------------
//  Controller bindings: 16 buttons per bank, 28-byte entries at 0x2A48A90 +
//  28 * (button + bank 0x1195C0()): +0 skill, +4 owner guid, +8 side (1 left,
//  0 right). 0x117DE0 returns the side, 0x117D20 the skill, 0x117CC0 the guid.
//  When a skill is bound, 0x238910 picks the side from skills.txt leftskill
//  (byte[rec+27h] & [1D996E8h]); there is no other choice on a controller.
//
//  0x12BB40  the client's handler for the server's skill confirmation packet
//            (+2 unit id, +6 side, +7 skill, +9 guid; rbx = packet). For the
//            local player in controller mode, a toggle skill (0x14A970, auras
//            through controllerskillsettings.json) must be bound on side 0:
//            0x12BBD7 mov r8d,r14d / xor edx,edx / mov rcx,rsi / call 0x14A930
//            (0x117BD0 binding lookup). When it is not, the client sends skill
//            0 (Attack) for that side (0x12BC03, message 3Ch) and selects it
//            locally. A leftskill aura is bound on side 1, so it is switched
//            back to Attack the moment the server confirms it: auras could not
//            be activated at all.
//            FIX, call relay at 0x12BBDF: the stub loads edx = (byte [rbx+6]
//            != 0), the side the server confirmed, then calls 0x14A930.
//
//  0x144AC0  the controller press processor; for a press it calls 0x14A5A0
//            (manager, player index, button) at 0x144B00, which reads the
//            button's side at 0x14A664 (0x117DE0), takes that side's current
//            skill (0x34A540 left / 0x34B400 right) and switches it to the
//            button's skill. Every later step of that press (cast command
//            0x14B1A0, confirmation check above) reads the same binding side.
//            FIX, call relay at 0x144B00: for a button holding an aura, the
//            binding's side byte is set first: the side already running that
//            aura; else the right side when no aura runs there; else the left
//            side when no aura runs there and the row has leftskill; else the
//            side whose aura was switched on earlier. Then 0x14A5A0 runs.
//
// -----------------------------------------------------------------------------
//  AURA LEVEL REFRESH (server, both buttons, every unit)
// -----------------------------------------------------------------------------
//  0x5616F0  the basic aura callback (srvdofunc 65), called for the caster
//            itself and for every unit in range. It fills a request on its
//            stack at [rbp-19h] (0x56171C..0x561774) and calls the state
//            applier at 0x561778, return address 0x56177D:
//              +00h caster    +08h target    +10h skill     +14h level
//              +18h duration  +1Ch stat (-1) +20h value (0) +24h state
//              +28h remove callback: the aura's own, else 0x566170 when the
//                   target is the caster, else none (the applier then
//                   uses 0x436240)
//  0x433D20  the state applier. D2RLoader turned its entry into a thunk,
//            jmp [rip+39F671Ah], so the code that runs is D2RLoader's
//            ApplyWideTimedStatEffect, reached through the pointer at
//            0x3E2A440. The game's own body is still in the image from
//            0x433D26:
//              list = curse state (0x335A40) ? the unit's curse list
//                     (0x2F5810, flag 20h) : the unit's list for the state
//                     (0x2F5940)
//              same state, same skill (list +28h), same level (list +2Ch):
//                     expiry refreshed (0x2F7AB0), the list is returned
//              same skill, lower level: refused, returns 0 (0x434076)
//              anything else, a HIGHER level included: unlink (0x2F7920) and
//                     free (0x2F4180) the old list, which removes every stat
//                     on it and runs its remove callback (state off); then
//                     state on (0x3354C0) and a new list (0x2F7300) with the
//                     caster as owner (list +08h type, +0Ch id) and the
//                     request's remove callback (list +80h)
//  So every time the same aura comes back one level higher, its bonuses are
//  removed and added again inside one frame, and the client sees its state
//  blink. When the torn down list belonged to another unit's aura, its remove
//  callback 0x436240 recounts the player's pet limits (0x5005A0, called at
//  0x4362CC) while the bonuses are missing: an aura that grants +skills
//  lowers every summon skill's petmax for that instant, and the game kills
//  the summons above it. Divinity raises its own level, so it climbs through
//  one rebuild per level, and a Divinity from another unit takes the list
//  over through the same rebuild.
//
//  FIX: the plugin points the thunk's pointer 0x3E2A440 at its own function.
//  No game code byte changes (celestialrayone.aura-zero-branch byte-checks
//  0x5616F0..0x5617A6, the applier call included). Calls from anywhere but
//  the aura callback (return address 0x56177D) go straight on to D2RLoader's
//  applier. For the aura callback:
//    1. It finds the list the applier will look at, the way the applier finds
//       it. When that list holds the same skill at a LOWER level than the
//       request, it raises the list's level to the request's (0x2F7A00).
//    2. The applier then takes its same-level path: it keeps the list and
//       refreshes its expiry. If the applier refuses for one of its own
//       reasons (its monster and curse checks), it returns 0 before it
//       touches the list, and the plugin puts the old level back.
//    3. When the list came back, the plugin finishes what the rebuild would
//       have changed, without removing the aura's bonuses:
//         same caster   a list without a remove callback (one AuraStart
//                       0x43B680 made) gets the one the rebuild would have
//                       given it (level_up_in_place)
//         other caster  owner (0x2F6CA0), other-caster record (0x2F6CB0) and
//                       remove callback move to the new caster; when the
//                       target owned the list, the passive stats its own
//                       application wrote (skills.txt passivestat1-14,
//                       record +0BAh, see 0x563FFC) are removed, as the
//                       rebuild removed them (takeover_in_place)
//         both          the state is turned on if it was off
//  Lower levels are still refused, and different skills that share a state
//  still replace each other, as in the unmodded game.
//
// -----------------------------------------------------------------------------
//  ITEM REFRESH KEEPS AURAS (server, both buttons)
// -----------------------------------------------------------------------------
//  0x470CA0  the equipment re-check (ITEMS_UpdateInventoryItems). It saves the
//            mouse selection (0x474C00), takes EVERY equipped item off with
//            unit flag 80000000h set around each detach (0x470E77..0x470EA8),
//            puts back the items whose requirements are met, the flag set
//            around each attach as well (0x470F79..0x470FAA), puts the saved
//            selection back through AssignSkill (0x474AA0, called at
//            0x47109E), deletes skills left without level, counts the pet
//            limits (0x5005A0, called at 0x4711C3) and settles the item auras
//            it kept back (0x4711C8..0x47121E, below). It runs after item moves
//            (0x471835), level ups (0x52DCA1), skill resets (0x436217) and in
//            every frame in which the player carries unit flag 200000h: the
//            per-frame player sync tests it at 0x486A6D and calls 0x470C90 ->
//            0x470CA0. The server stat callback sets that flag whenever
//            passive_mastery_item_req_percent (203) or
//            passive_mastery_item_level_req_percent (209) changes outside an
//            item operation (0x470998..0x4709AD).
//  0x46FE1B  the server stat callback, oskill case (item_nonclassskill 97,
//            item_singleskill 107, 0x4702AE..0x4702EE): when the stat drops to
//            0 and the skill has no base level, every mouse button holding the
//            skill gets Attack through AssignSkill (left 0x4702C9, right
//            0x4702E9). D2RLoader's core runs its own version of this callback
//            (see celestialrayone.aura-reactivation); the game's body above is
//            what it reproduces. Inside the re-check that happens to every
//            oskill for an instant. An oskill AURA was then switched off (the
//            right button's removal in AssignSkill, this plugin's left select)
//            and started again by the selection restore: the state blinked, its
//            bonuses were gone until its next tick and the pet limits were
//            counted without them.
//            The same callback already treats item auras (item_aura 151) as a
//            transaction while flag 80000000h is set: a removal goes to a
//            pending list (game +1A8F0h, count +1A8F8h), a grant that comes
//            back reuses its cast id, and what was not granted back is switched
//            off when the re-check ends (0x4711C8..0x47121E, 0x581AE0). Auras
//            on the mouse buttons had no such rule.
//  The loop seen with Divinity (oskill, raises item_allskills) and Levitation
//  Mastery (oskill passive, its passive_mastery_item_req_percent follows its
//  level): every restart changed item_allskills on the aura's next tick, that
//  changed Levitation Mastery's level and the modifier, the flag was set, the
//  re-check ran and restarted the aura again. On every tick the overlay
//  replayed and the bonuses were missing.
//
//  FIX: the item aura rule, for auras on the mouse buttons.
//    - While flag 80000000h is set (0x34FB00), an aura that runs on a button
//      that gets another skill keeps running and is remembered. Left: the left
//      select relay does not stop it. Right: the call relays at 0x438C02,
//      0x438C33 and 0x438C52 skip its stat list, state and timer removal.
//    - When its button gets it back, it carries on. Left: it counts as running.
//      Right: the AuraStart call at 0x438C80 is skipped. The same holds when
//      the button was changed without AssignSkill and gets the aura back
//      before its timer ticks: the timer is still live, the aura never
//      stopped.
//    - An aura its button did not get back is stopped when the re-check ends
//      (call relay at 0x4711C3, before the pet limits are counted), at the
//      next selection change outside an item operation, or by its own timer.
//    0x438B09  call relay on AssignSkill's SetRightActiveSkill (rcx unit, edx
//              skill, r8d owner guid; eax unused afterwards): the right
//              button's skill before and after.
//    0x438C52  call relay on the right timer delete (rcx game, rdx unit, r8d 8,
//              r9d -1).
//    0x438C80  call relay on the new right aura's AuraStart (rcx game, rdx
//              unit, r8d skill; its eax is AssignSkill's, unused by callers).
//    0x4711C3  call relay on the pet limit count (rcx game, rdx unit).
//
//  Console command "leftclickauras" shows what is installed and counters.
//  Settings: d2rloader/config/celestialrayone.left-click-auras.toml
// =============================================================================

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define LEFT_CLICK_AURAS_RETURN_ADDRESS() _ReturnAddress()
#else
#define LEFT_CLICK_AURAS_RETURN_ADDRESS() __builtin_return_address(0)
#endif

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::LeftClickAuras {
namespace {

constexpr char PluginVersion[] = "1.4.0";

// ---------------------------------------------------------------------------
//  Default configuration. EnsureConfig writes this text on first load, and it
//  is the plugin's documentation as shipped.
// ---------------------------------------------------------------------------
constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.left-click-auras
#
# Left Click Auras
#   An aura on the left mouse button runs the same way an aura on the right
#   mouse button does, and both can be active at the same time. With a
#   controller, auras can be switched on and two of them can run at once. An
#   aura that comes back at a higher level keeps its state instead of blinking
#   off, on either button. An aura granted by an item (oskill) keeps running
#   while the game re-checks your equipment, on either button.
#
# Changes are read when the plugin loads. Restart the game after editing.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). On any other build
# the byte checks fail and the plugin loads without changing the game.
#
# Data requirement
#   The game only lets a skill onto the left button when its skills.txt row
#   has leftskill = 1. Aura rows that should work on the left need it.
#
# Compatibility
#   Do not install together with yin's "Aura In Either Skill Slot (Single
#   Active Aura)" patch. It changes the same game code for a different design
#   (one aura at a time), and the byte checks of whichever loads second fail.

# Master switch. false loads the plugin without touching the game.
enabled = true

# -----------------------------------------------------------------------------
# left_slot_auras
#
# The problem
#   The game starts an aura only when it goes on the right mouse button. The
#   left button only records the selection, so an aura there never runs.
#
# What changes
#   Putting an aura on the left button starts it the way the right button
#   does: aura state, real skill level, a timer of its own. The left and right
#   auras keep separate timers, so two auras run side by side.
#
#   The left aura's timer works like the right button's timer: every tick it
#   checks the left button, casts the aura and sets itself again. It does not
#   depend on who else applies the same aura state to you. A hireling, a bound
#   demon, a party member or an item with the same aura, or another aura that
#   uses the same aurastate, can no longer switch it off. The same aura from
#   another unit does not even make it blink (takeover_in_place). Another
#   aura that uses the same aurastate can still make its bonuses blink until
#   its next tick, exactly as on the right button.
#
#   The left aura stops, and its state and bonuses are removed, when
#     - another skill goes on the left button,
#     - the aura is no longer on the left button for any other reason (an item
#       or weapon swap took the skill away, the game reset the button),
#     - the character is dead, or the skill is no longer an aura.
#
#   Selecting the same aura again keeps it running. After a resurrect, a
#   weapon swap back or entering a game, the game selects the left skill again
#   and the aura starts if it is not running.
#
#   When the left and right auras share an aura state, stopping one of them
#   leaves the state to the other.
#
# Where
#   skill assignment     call at 0x438AEF, where 0x438A70 selects the left
#                        skill
#   aura start           call at 0x43B8FD, arms the left aura's own timer
#   right aura removal   calls at 0x438C02 and 0x438C33, keep a state the left
#                        aura still uses
#   aura timer           0x437460, runs the left aura's timer
left_slot_auras = true

# -----------------------------------------------------------------------------
# single_emanation
#
# The problem
#   With the same aura on both buttons, both timers perform it and it
#   emanates twice as often.
#
# What changes
#   While the right button holds the same aura and its timer runs, the left
#   timer keeps itself alive without performing, so the aura emanates once.
#   If the right button changes, the left timer performs again on its own.
#
# Where
#   the left aura's timer at 0x437460
#
# Needs left_slot_auras.
single_emanation = true

# -----------------------------------------------------------------------------
# move_only_clicks
#
# The problem
#   With an aura on the left button, left clicking a monster does nothing. The
#   click is treated as a cast of the aura on that monster, and auras cannot
#   be cast.
#
# What changes
#   A left click on a monster walks or runs toward it, the same way a click on
#   the ground moves you. Clicks on anything else (NPCs, other players, items,
#   objects, exits) do what they do with no usable skill on the button: talk,
#   interact, pick up, use. Shift clicks and the right button are unchanged.
#
# Where
#   call at 0x102753 in the client's world click handler (0x102490)
move_only_clicks = true

# -----------------------------------------------------------------------------
# live_level
#
# The problem
#   Without this, the left aura keeps the level it had when it started, while
#   the right button's timer re-reads its skill level every tick. So on the
#   left:
#     - an aura that grants skill levels to itself never compounds (Divinity
#       gives its +all skills once and never counts them toward itself),
#     - gear or points added after the aura started never raise it, and gear
#       removed afterwards never lowers it.
#
# What changes
#   Every tick the left aura takes its level from the skill on the left
#   button, with every bonus, the same way the right button's aura does. An
#   aura that grants skill levels to itself compounds until it settles, as it
#   does on the right button: Divinity gives back 1 level for every 6 of its
#   own and settles within a few ticks. An aura that gives itself 1 level or
#   more per level of its own would keep climbing, on either button. If the
#   skill reports no level, the level of the previous tick is kept.
#
# Where
#   the left aura's timer at 0x437460
#
# Needs left_slot_auras.
live_level = true

# -----------------------------------------------------------------------------
# item_refresh_keeps_auras
#
# The problem
#   The game re-checks your equipment by taking every equipped item off and
#   putting back the ones whose requirements you meet. It does that when an
#   item is moved, put on or taken off, on a level up, and whenever an item
#   requirement modifier changes, such as passive_mastery_item_req_percent
#   from a mastery. Items you can use always go back, but for that instant a
#   skill an item grants (an oskill) has no level, so the game puts Attack on
#   its mouse button and puts the skill back at the end of the check.
#   An aura granted that way, on either button, was switched off and started
#   again: its state blinked, its bonuses were gone until its next tick, and
#   your summon limits were counted without them, which can kill summons.
#   An aura that changes skill levels (Divinity) together with an item that
#   grants a mastery whose requirement modifier follows its own level
#   (Levitation Mastery) turned this into a loop: each restart changed the
#   mastery's level on the aura's next tick, that changed the modifier, the
#   game re-checked the equipment and restarted the aura again. The aura's
#   visual effect replayed on every tick and its bonuses were missing.
#
# What changes
#   While the game re-checks the equipment, an aura running on a mouse button
#   keeps running when the check puts Attack on that button. When the check
#   puts the aura back, it carries on without a blink and with all its
#   bonuses. If the check does not put it back (its item no longer meets the
#   requirements), the aura stops when the check ends, before the summon
#   limits are counted, as it would have before. The game already handles
#   item auras (item_aura) this way; this gives auras on the mouse buttons the
#   same rule.
#
# Where
#   left button     the left select relay at 0x438AEF
#   right button    calls at 0x438B09 (select), 0x438C02, 0x438C33 and
#                   0x438C52 (removal) and 0x438C80 (start) in 0x438A70
#   end of check    call at 0x4711C3 in the equipment re-check 0x470CA0
#
# Needs left_slot_auras.
item_refresh_keeps_auras = true

# -----------------------------------------------------------------------------
# level_up_in_place
#
# The problem
#   An aura applies its state again on every tick. When the same unit's aura
#   comes back one or more levels higher than the state holds (Divinity
#   climbing, live_level following +skills gear, a skill point spent), the
#   game throws the state's stats away and builds them again. For that moment
#   the state is off and every bonus it gives is gone: the state visibly
#   blinks, and anything that looks at the character in that moment sees the
#   bonuses missing.
#
# What changes
#   The state keeps its stats. Its level is raised and the aura's new values
#   are written over the old ones, so each bonus goes straight from the old
#   value to the new one. This works on both mouse buttons and for every unit
#   that runs an aura, not only for left button auras. An aura at a lower
#   level than the state holds is still refused, as in the unmodded game. An
#   aurastatcalc that works out to 0 removes its stat only with
#   celestialrayone.aura-zero-branch installed, as before.
#
# Where
#   The aura callback 0x5616F0 calls the state applier at 0x561778. The
#   applier 0x433D20 is a D2RLoader thunk through the pointer at 0x3E2A440;
#   the plugin points it at its own function and passes every other caller
#   straight through. No game code byte changes, so the byte checks
#   celestialrayone.aura-zero-branch makes on the same callback still pass.
level_up_in_place = true

# -----------------------------------------------------------------------------
# takeover_in_place
#
# The problem
#   Another unit's copy of the same aura (same skills.txt row) reaches the
#   character at a higher level than the state holds: a hireling, a summon, a
#   bound demon or another player. The game throws the state's stats away
#   and builds them again for the new owner. When the state belonged to
#   another unit, the game also recounts the player's summon limits in that
#   moment, while the bonuses are missing. An aura that grants +skills, like
#   Divinity, lowers every summon skill's limit for that instant, and the
#   game kills the summons above it.
#
# What changes
#   The state keeps its stats and changes hands: the new owner's level and
#   values replace the old ones. When it was the character's own aura, that
#   aura's passive stats (skills.txt passivestat columns) are still removed,
#   because only the character's own aura writes them.
#   Two different auras (two skills.txt rows) that use the same state still
#   replace each other with a blink. Give them separate states if they should
#   not fight.
#
# Where
#   The same place as level_up_in_place.
takeover_in_place = true

# -----------------------------------------------------------------------------
# controller_aura_activation
#
# The problem
#   With a controller, every skill is bound to a side, and the side comes from
#   the skills.txt leftskill column: leftskill = 1 skills go to the left side.
#   Auras are toggle skills on a controller. Whenever the server confirms a
#   toggle skill on either side, the game checks whether that skill is bound
#   on the RIGHT side only. An aura with leftskill = 1 is bound on the left,
#   fails that check, and the game puts Attack back on that side at once. The
#   aura switches off the moment it switches on, so auras could not be
#   activated at all.
#
# What changes
#   The check looks at the side the server confirmed, so an aura bound on the
#   left stays on. The check only runs with a controller; mouse play is not
#   affected.
#
# Where
#   call at 0x12BBDF in the client's skill confirmation handler (0x12BB40)
controller_aura_activation = true

# -----------------------------------------------------------------------------
# controller_two_auras
#
# The problem
#   A controller has one skill slot per side, and the side of every skill is
#   fixed by leftskill. With leftskill = 1 on every aura, all of them land on
#   the left side, so only one can run at a time, however many are bound to
#   buttons.
#
# What changes
#   When you press a button that holds an aura:
#     - an aura that is already running stays as it is,
#     - otherwise it goes to the right side if no aura runs there, else to the
#       left side if no aura runs there,
#     - when both sides already run an aura, it replaces the one that was
#       switched on earlier.
#   So at most two auras run at once, one per side, the same limit as with the
#   mouse, no matter how many auras are bound to buttons. An aura only goes to
#   the left side when its row has leftskill = 1.
#
#   As in the unmodded game, using another skill bound to the same side as a
#   running aura takes the aura off that side while the skill is used and puts
#   it back afterwards.
#
# Where
#   call at 0x144B00, where the controller starts a button press (0x144AC0
#   calling 0x14A5A0); it sets the side stored with the button's binding
#
# Needs controller_aura_activation.
controller_two_auras = true

# Writes a line to the plugin log for the first 64 aura starts, stops, skipped
# emanations, left aura level changes, controller side choices, auras kept in
# place (level ups, takeovers), refused lower levels and two auras fighting
# over one state. Keep false for normal play. The console command
# "leftclickauras" always shows what is installed and the counters.
diagnostics = false
)TOML";

// ---------------------------------------------------------------------------
//  Native layout (D2R 3.3, D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------
constexpr std::size_t   GameDataContextOffset         = 0x106;  // movzx ecx,byte [rbp+106h] at 0x43B8C2
constexpr std::size_t   UnitIdOffset                  = 0x08;
constexpr std::size_t   SkillNodeRecordOffset         = 0x00;   // node -> skills.txt record (0x3402D0, 0x102674)
constexpr std::size_t   SkillNodeOwnerGuidOffset      = 0x4C;   // 0x14A7D4 copies it, 0x33DDA8 compares it with -1
constexpr std::size_t   SkillRecordIdOffset           = 0x00;   // int16 skill id
constexpr std::size_t   SkillRecordFlagsByteOffset    = 0x24;   // & aura mask, 0x438BD4
constexpr std::size_t   SkillRecordLeftSkillByteOffset = 0x27;  // & leftskill mask, 0x2389C0
constexpr std::size_t   SkillRecordAuraStateOffset    = 0xA0;   // int16, 0x438BE0
constexpr std::size_t   ClickCommandFlagsOffset       = 0x00;
constexpr std::uint32_t ClickCommandLeftButton        = 0x01;
constexpr std::int32_t  UnitTypePlayer                = 0;
constexpr std::uint32_t UnitPeriodicPerformFlag       = 0x20000000;  // 0x43754A / 0x437586
constexpr std::uint8_t  PeriodicSkillEventType        = 8;
constexpr std::uint32_t RightTimerCustomId            = 0xFFFFFFFFU;
constexpr std::uint32_t UnitItemOperationFlag         = 0x80000000;  // around each detach / attach, 0x470E77 / 0x470F79
constexpr std::size_t   UnitEventListOffset           = 0x148;  // 0x48FE50
constexpr std::size_t   EventTypeOffset               = 0x00;
constexpr std::size_t   EventFlagsOffset              = 0x02;
constexpr std::size_t   EventCustomIdOffset           = 0x18;
constexpr std::size_t   EventArg7Offset               = 0x20;   // cast id of a left timer, 0x48BF78
constexpr std::size_t   EventUnitNextOffset           = 0x38;
constexpr std::uint16_t EventDeleteDeferredFlag       = 0x0008;
constexpr std::size_t   MaximumUnitEvents             = 65536;
constexpr std::uint32_t ControllerPlayers             = 8;      // 0x14A5EA: cmp rdi,8
constexpr std::uint32_t ControllerHighestButton       = 15;     // 0x117DE9: cmp ebx,0Fh
constexpr std::size_t   BindingEntryBytes             = 28;     // 0x117E14: imul rcx,rax,1Ch
constexpr std::size_t   BindingSkillOffset            = 0x00;
constexpr std::size_t   BindingOwnerGuidOffset        = 0x04;
constexpr std::size_t   BindingSideOffset             = 0x08;   // 0x117E18: [2A48A98h + 1Ch * index]
constexpr std::uint8_t  BindingSideRight              = 0;
constexpr std::uint8_t  BindingSideLeft               = 1;

// The request the aura callback builds at [rbp-19h] (0x56171C..0x561774) for
// the state applier; the applier's own body (0x433D26) reads the same offsets.
struct StateEffectRequest {
    void*        caster;          // +00h  aura callback owner, [rcx+8]
    void*        target;          // +08h
    std::int32_t skillId;         // +10h  [args+4]
    std::int32_t skillLevel;      // +14h  [args+8]
    std::int32_t duration;        // +18h  [args+10h]
    std::int32_t statId;          // +1Ch  -1
    std::int32_t statValue;       // +20h  0
    std::int32_t state;           // +24h  [args]
    void*        removeCallback;  // +28h
};
static_assert(offsetof(StateEffectRequest, target) == 0x08);
static_assert(offsetof(StateEffectRequest, skillId) == 0x10);
static_assert(offsetof(StateEffectRequest, skillLevel) == 0x14);
static_assert(offsetof(StateEffectRequest, duration) == 0x18);
static_assert(offsetof(StateEffectRequest, state) == 0x24);
static_assert(offsetof(StateEffectRequest, removeCallback) == 0x28);

constexpr std::size_t   StatListRemoveCallbackOffset  = 0x80;   // 0x2F7A50 stores rdx at [rcx+80h]
constexpr std::uint32_t CurseStatListFlag             = 0x20;   // 0x433FE9: 0x2F5810(unit, 20h)
constexpr std::size_t   ItemStatCostCountOffset       = 0x1260; // [0x300A90(context) + 1260h], 0x5618C8
constexpr std::size_t   SkillRecordPassiveStatsOffset = 0xBA;   // lea r12,[r14+0BAh] at 0x564003
constexpr std::size_t   SkillRecordPassiveStatSlots   = 14;     // mov r14d,0Eh at 0x56400A

// ---------------------------------------------------------------------------
//  Native RVAs
// ---------------------------------------------------------------------------
constexpr std::uint64_t AssignSkillRva               = 0x438A70;
constexpr std::uint64_t LeftSelectCallRva            = 0x438AEF;
constexpr std::uint64_t SetLeftActiveSkillRva        = 0x33EC70;
constexpr std::uint64_t RightAuraStatListCallRva     = 0x438C02;
constexpr std::uint64_t RightAuraStateOffCallRva     = 0x438C33;
constexpr std::uint64_t PeriodicSkillEventRva        = 0x437460;
constexpr std::uint64_t ReadSkillLevelRva            = 0x33D1E0;  // loader thunk to ReadWideSkillLevel
constexpr std::uint64_t GetUnitCastIdRva             = 0x34B6C0;  // called at 0x437538
constexpr std::uint64_t SetUnitCastIdRva             = 0x3E2B652; // loader thunk, called at 0x437545
constexpr std::uint64_t SetUnitFlagRva               = 0x34E140;  // called at 0x437558
constexpr std::uint64_t IsUnitDeadRva                = 0x34C2C0;  // called at 0x437503
constexpr std::uint64_t AuraStartRva                 = 0x43B680;
constexpr std::uint64_t AuraStartArmWindowRva        = 0x43B8C2;
constexpr std::uint64_t AuraStartArmCallRva          = 0x43B8FD;
constexpr std::uint64_t ArmPeriodicSkillRva          = 0x43B2B0;
constexpr std::uint64_t ServerDoSkillRva             = 0x43ACB0;
constexpr std::uint64_t ClickHandlerRva              = 0x102490;
constexpr std::uint64_t ClickGateWindowRva           = 0x102700;
constexpr std::uint64_t ClickGateCallRva             = 0x102753;
constexpr std::uint64_t SkillNodeGateRva             = 0x33F770;
constexpr std::uint64_t GetLeftSkillRva              = 0x34A540;
constexpr std::uint64_t GetRightSkillRva             = 0x34B400;
constexpr std::uint64_t GetGameRva                   = 0x48FF00;
constexpr std::uint64_t CheckStateRva                = 0x3351B0;
constexpr std::uint64_t DeleteEventsByTypeRva        = 0x48B890;
constexpr std::uint64_t DeleteEventsWalkRva          = 0x48B8F0;
constexpr std::uint64_t GetUnitEventListRva          = 0x48FE50;
constexpr std::uint64_t SetEventStoreRva             = 0x48BF55;
constexpr std::uint64_t GetSkillsTxtRecordRva        = 0x097790;
constexpr std::uint64_t GetStateCountRva             = 0x2141A0;
constexpr std::uint64_t ToggleStateRva               = 0x3354C0;
constexpr std::uint64_t GetStateStatListRva          = 0x2F5940;
constexpr std::uint64_t UnlinkStatListRva            = 0x2F7920;
constexpr std::uint64_t FreeStatListRva              = 0x2F4180;
constexpr std::uint64_t GetUnitTypeRva               = 0x34B9D0;
constexpr std::uint64_t GetUnitDataContextRva        = 0x34A0E0;  // called by AssignSkill, byte [unit+1BDh]
constexpr std::uint64_t AuraFlagMaskRva              = 0x1D996E4; // test [rip+..],cl at 0x438BD8
constexpr std::uint64_t LeftSkillFlagMaskRva         = 0x1D996E8; // test [rip+..],al at 0x2389C4
constexpr std::uint64_t ControllerSyncRva            = 0x12BB40;
constexpr std::uint64_t ControllerSyncCallRva        = 0x12BBDF;
constexpr std::uint64_t ToggleBindingCheckRva        = 0x14A930;
constexpr std::uint64_t ControllerPressWindowRva     = 0x144AEB;
constexpr std::uint64_t ControllerPressCallRva       = 0x144B00;
constexpr std::uint64_t PressStartRva                = 0x14A5A0;
constexpr std::uint64_t PressSideReadWindowRva       = 0x14A642;
constexpr std::uint64_t BindingSideGetterRva         = 0x117DE0;
constexpr std::uint64_t IsControllerModeRva          = 0x077E10;
constexpr std::uint64_t BindingBankRva               = 0x1195C0;
constexpr std::uint64_t GetClientPlayerRva           = 0x09A480;
constexpr std::uint64_t ControllerBindingTableRva    = 0x2A48A90;
constexpr std::uint64_t AuraCallbackRequestRva       = 0x561707;
constexpr std::uint64_t AuraCallbackReturnRva        = 0x56177D;
constexpr std::uint64_t StateApplierThunkRva         = 0x433D20;
constexpr std::uint64_t StateApplierPointerRva       = 0x3E2A440; // 0x433D26 + 0x039F671A
constexpr std::uint64_t DefaultRemoveCallbackRva     = 0x436240;
constexpr std::uint64_t IsCurseStateRva              = 0x335A40;
constexpr std::uint64_t StatListByFlagRva            = 0x2F5810;
constexpr std::uint64_t StatListStateRva             = 0x2F6E40;
constexpr std::uint64_t StatListSkillRva             = 0x2F56A0;
constexpr std::uint64_t StatListLevelRva             = 0x2F56E0;
constexpr std::uint64_t SetStatListLevelRva          = 0x2F7A00;
constexpr std::uint64_t StatListOwnerTypeRva         = 0x2F6E30;
constexpr std::uint64_t StatListOwnerIdRva           = 0x2F6E20;
constexpr std::uint64_t SetStatListOwnerRva          = 0x2F6CA0;
constexpr std::uint64_t ClearOtherCasterRecordRva    = 0x2F6CB0;
constexpr std::uint64_t SetRemoveCallbackRva         = 0x2F7A50;
constexpr std::uint64_t GetUnitIdRva                 = 0x34A330;
constexpr std::uint64_t MarkStateChangedRva          = 0x3355A0;
constexpr std::uint64_t DataTablesRva                = 0x300A90;
constexpr std::uint64_t ReadListStatRva              = 0x2F5DF0;  // loader thunk to ReadWideListStat
constexpr std::uint64_t SetListStatRva               = 0x2F7C00;  // loader thunk to SetWideListStat
constexpr std::uint64_t PassiveStatSlotsRva          = 0x563FFC;
constexpr std::uint64_t RightSelectCallRva           = 0x438B09;
constexpr std::uint64_t SetRightActiveSkillRva       = 0x33EF10;
constexpr std::uint64_t RightAuraTimerDeleteCallRva  = 0x438C52;
constexpr std::uint64_t RightAuraStartCallRva        = 0x438C80;
constexpr std::uint64_t UnitFlagTestRva              = 0x34FB00;
constexpr std::uint64_t OskillButtonResetRva         = 0x4702AE;
constexpr std::uint64_t ItemRefreshDetachRva         = 0x470E77;
constexpr std::uint64_t ItemRefreshAttachRva         = 0x470F79;
constexpr std::uint64_t ItemRefreshRestoreRva        = 0x471096;
constexpr std::uint64_t ItemRefreshFinishWindowRva   = 0x4711BD;
constexpr std::uint64_t ItemRefreshFinishCallRva     = 0x4711C3;
constexpr std::uint64_t UpdatePetLimitsRva           = 0x5005A0;

// ---------------------------------------------------------------------------
//  Byte witnesses, read from the image
// ---------------------------------------------------------------------------
// 0x438A70..0x438CA3, AssignSkill, whole function. Its call bytes also prove
// the targets used below: 0x3351B0, 0x33EC70, 0x34B400, 0x34A0E0, 0x33E080,
// 0x097790, 0x2141A0, 0x2F5940, 0x2F7920, 0x2F4180, 0x3354C0, 0x48FF00,
// 0x48B890, 0x43B680.
constexpr auto AssignSkillBody = std::to_array<std::uint8_t>({
    0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02, 0x00, 0x00, 0x44, 0x89,
    0x4C, 0x24, 0x20, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
    0x48, 0x89, 0x5C, 0x24, 0x60, 0x45, 0x8B, 0xF0, 0x44, 0x8B, 0xFA, 0x48,
    0x8B, 0xE9, 0x85, 0xD2, 0x75, 0x1C, 0x41, 0x8D, 0x57, 0x55, 0xE8, 0x0D,
    0xC7, 0xEF, 0xFF, 0x85, 0xC0, 0x74, 0x0F, 0x45, 0x33, 0xC0, 0x41, 0x8D,
    0x57, 0x55, 0x48, 0x8B, 0xCD, 0xE8, 0x0A, 0xCA, 0xEF, 0xFF, 0x44, 0x8B,
    0x44, 0x24, 0x78, 0x41, 0x8B, 0xD6, 0x48, 0x8B, 0xCD, 0xE8, 0x0A, 0x52,
    0xF0, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xC2, 0x01,
    0x00, 0x00, 0x48, 0x89, 0x74, 0x24, 0x68, 0x48, 0x8B, 0xCD, 0x4C, 0x89,
    0x64, 0x24, 0x30, 0x45, 0x33, 0xE4, 0x45, 0x85, 0xFF, 0x74, 0x0F, 0x44,
    0x8B, 0x44, 0x24, 0x78, 0x41, 0x8B, 0xD6, 0xE8, 0x7C, 0x61, 0xF0, 0xFF,
    0xEB, 0x18, 0xE8, 0x05, 0x29, 0xF1, 0xFF, 0x44, 0x8B, 0x44, 0x24, 0x78,
    0x41, 0x8B, 0xD6, 0x48, 0x8B, 0xCD, 0x4C, 0x8B, 0xE0, 0xE8, 0x02, 0x64,
    0xF0, 0xFF, 0x45, 0x33, 0xC9, 0x48, 0x89, 0x7C, 0x24, 0x38, 0x45, 0x33,
    0xC0, 0xC7, 0x44, 0x24, 0x78, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x8D, 0x54,
    0x24, 0x78, 0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xCB, 0xE8, 0x69, 0x3F, 0xF0, 0xFF, 0x48, 0x8B, 0xCD, 0xE8, 0x91,
    0x2E, 0xF1, 0xFF, 0x85, 0xC0, 0x75, 0x48, 0x8B, 0x74, 0x24, 0x78, 0x48,
    0x8D, 0x15, 0x52, 0x08, 0x8E, 0x01, 0x41, 0xB8, 0x95, 0x0F, 0x00, 0x00,
    0x48, 0x8B, 0xCD, 0xE8, 0xD4, 0x17, 0xF1, 0xFF, 0x48, 0x8B, 0xCD, 0x8B,
    0xF8, 0xE8, 0x6A, 0x2E, 0xF1, 0xFF, 0x48, 0x8B, 0xCD, 0x8B, 0xD8, 0xE8,
    0x70, 0x72, 0x05, 0x00, 0x45, 0x8B, 0xCF, 0x89, 0x74, 0x24, 0x28, 0x44,
    0x8B, 0xC7, 0x66, 0x44, 0x89, 0x74, 0x24, 0x20, 0x0F, 0xB6, 0xD3, 0x48,
    0x8B, 0xC8, 0xE8, 0xF5, 0x64, 0x04, 0x00, 0x48, 0x8B, 0xCD, 0xE8, 0x4D,
    0x15, 0xF1, 0xFF, 0x0F, 0xB6, 0xF0, 0x45, 0x85, 0xFF, 0x0F, 0x85, 0xE6,
    0x00, 0x00, 0x00, 0x4D, 0x85, 0xE4, 0x0F, 0x84, 0xAF, 0x00, 0x00, 0x00,
    0x41, 0xB8, 0x9F, 0x0F, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xEB, 0x07, 0x8E,
    0x01, 0x49, 0x8B, 0xCC, 0xE8, 0xC3, 0x54, 0xF0, 0xFF, 0x8B, 0xD0, 0x40,
    0x0F, 0xB6, 0xCE, 0xE8, 0xC8, 0xEB, 0xC5, 0xFF, 0x48, 0x8B, 0xD8, 0x48,
    0x85, 0xC0, 0x0F, 0x84, 0x83, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x48, 0x24,
    0x84, 0x0D, 0x06, 0x0B, 0x96, 0x01, 0x74, 0x77, 0x0F, 0xB7, 0xB8, 0xA0,
    0x00, 0x00, 0x00, 0x66, 0x85, 0xFF, 0x79, 0x10, 0x40, 0x0F, 0xB6, 0xCE,
    0x0F, 0xBF, 0xFF, 0xE8, 0xA8, 0xB5, 0xDD, 0xFF, 0x3B, 0xF8, 0x7D, 0x3C,
    0x0F, 0xBF, 0xD7, 0x48, 0x8B, 0xCD, 0xE8, 0x39, 0xCD, 0xEB, 0xFF, 0x48,
    0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x74, 0x17, 0x48, 0x8B, 0xD0, 0x48, 0x8B,
    0xCD, 0xE8, 0x06, 0xED, 0xEB, 0xFF, 0x48, 0x8B, 0xD7, 0x40, 0x0F, 0xB6,
    0xCE, 0xE8, 0x5A, 0xB5, 0xEB, 0xFF, 0x0F, 0xBF, 0x93, 0xA0, 0x00, 0x00,
    0x00, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCD, 0xE8, 0x88, 0xC8, 0xEF, 0xFF,
    0x48, 0x8B, 0xCD, 0xE8, 0xC0, 0x72, 0x05, 0x00, 0x48, 0x8B, 0xC8, 0x41,
    0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0x41, 0xB8, 0x08, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xD5, 0xE8, 0x39, 0x2C, 0x05, 0x00, 0x41, 0x8B, 0xD6, 0x40, 0x0F,
    0xB6, 0xCE, 0xE8, 0x2D, 0xEB, 0xC5, 0xFF, 0x0F, 0xB6, 0x48, 0x24, 0x84,
    0x0D, 0x77, 0x0A, 0x96, 0x01, 0x74, 0x16, 0x48, 0x8B, 0xCD, 0xE8, 0x89,
    0x72, 0x05, 0x00, 0x48, 0x8B, 0xC8, 0x45, 0x8B, 0xC6, 0x48, 0x8B, 0xD5,
    0xE8, 0xFB, 0x29, 0x00, 0x00, 0x48, 0x8B, 0x7C, 0x24, 0x38, 0x48, 0x8B,
    0x74, 0x24, 0x68, 0x4C, 0x8B, 0x64, 0x24, 0x30, 0x48, 0x8B, 0x5C, 0x24,
    0x60, 0x48, 0x83, 0xC4, 0x40, 0x41, 0x5F, 0x41, 0x5E, 0x5D, 0xC3,
});

// 0x437460..0x4375AA, the type 8 event: entry, the right button's timer. Its
// call bytes prove 0x34B400, 0x33D1E0, 0x097790, 0x34C2C0, 0x34B6C0, the
// SetUnitCastId thunk 0x3E2B652 (1.3.1; 0x3E2B57E in 1.3.0), 0x34E140 and
// 0x43ACB0, which the left timer calls the same way.
constexpr auto PeriodicSkillEventWindow = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x89,
    0x4C, 0x24, 0x40, 0x45, 0x8B, 0xF9, 0x48, 0x89, 0x54, 0x24, 0x48, 0x41,
    0x8B, 0xE8, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x41, 0x83, 0xF8, 0xFF,
    0x0F, 0x85, 0x91, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCA, 0xE8, 0x62, 0x3F,
    0xF1, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x61, 0x01,
    0x00, 0x00, 0x41, 0xB8, 0x1B, 0x0E, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xE9,
    0x1E, 0x8E, 0x01, 0x48, 0x8B, 0xC8, 0xE8, 0xC1, 0x6B, 0xF0, 0xFF, 0x45,
    0x33, 0xC9, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0x8B, 0xE8, 0x45, 0x8D,
    0x41, 0x01, 0xE8, 0x0D, 0x5D, 0xF0, 0xFF, 0x0F, 0xB6, 0x8E, 0x06, 0x01,
    0x00, 0x00, 0x8B, 0xD5, 0x44, 0x8B, 0xE0, 0xE8, 0xAC, 0x02, 0xC6, 0xFF,
    0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x1B, 0x01, 0x00, 0x00,
    0x0F, 0xB6, 0x48, 0x24, 0x84, 0x0D, 0xEA, 0x21, 0x96, 0x01, 0x0F, 0x84,
    0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xB8, 0x4D, 0xF1, 0xFF,
    0x85, 0xC0, 0x0F, 0x85, 0x90, 0x00, 0x00, 0x00, 0x41, 0x8D, 0x47, 0x01,
    0xA9, 0xFE, 0xFF, 0xFF, 0xFF, 0x75, 0x1A, 0x48, 0x8D, 0x8C, 0x24, 0x90,
    0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00, 0x00, 0xE8,
    0xC0, 0xAE, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0xCF,
    0xE8, 0x83, 0x41, 0xF1, 0xFF, 0x41, 0x8B, 0xD7, 0x48, 0x8B, 0xCF, 0x8B,
    0xD8, 0xE8, 0x08, 0x41, 0x9F, 0x03, 0xBA, 0x00, 0x00, 0x00, 0x20, 0x41,
    0xB8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xE3, 0x6B, 0xF1,
    0xFF, 0x45, 0x33, 0xF6, 0x45, 0x8B, 0xCC, 0x44, 0x89, 0x74, 0x24, 0x30,
    0x44, 0x8B, 0xC5, 0x44, 0x89, 0x74, 0x24, 0x28, 0x48, 0x8B, 0xD7, 0x48,
    0x8B, 0xCE, 0xC7, 0x44, 0x24, 0x20, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x2D,
    0x37, 0x00, 0x00, 0x45, 0x33, 0xC0, 0xBA, 0x00, 0x00, 0x00, 0x20, 0x48,
    0x8B, 0xCF, 0xE8, 0xAD, 0x6B, 0xF1, 0xFF, 0xC7, 0x44, 0x24, 0x28, 0x01,
    0x00, 0x00, 0x00, 0xE9, 0x36, 0x02, 0x00, 0x00, 0x0F, 0xB6, 0x43, 0x29,
    0x84, 0x05, 0x3E, 0x21, 0x96, 0x01,
});

// 0x48BF55, EVENT_SetEvent node stores: customId +18h, customParam +1Ch, arg7 +20h, type +00h
constexpr auto SetEventStoreWitness = std::to_array<std::uint8_t>({
    0x8B, 0x4C, 0x24, 0x78, 0x44, 0x8B, 0xC5, 0x48, 0x8B, 0xD8, 0x89, 0x68,
    0x04, 0x89, 0x48, 0x18, 0x8B, 0x8C, 0x24, 0x80, 0x00, 0x00, 0x00, 0x89,
    0x48, 0x1C, 0x8B, 0x8C, 0x24, 0x88, 0x00, 0x00, 0x00, 0x89, 0x48, 0x20,
    0x48, 0x8B, 0x4C, 0x24, 0x70, 0x8B, 0x50, 0x14, 0x48, 0x89, 0x48, 0x48,
    0x48, 0x8B, 0xCE, 0x40, 0x88, 0x38,
});

// 0x097790
constexpr auto GetSkillsTxtRecordEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48,
});

// 0x2141A0
constexpr auto GetStateCountEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0xE5, 0xC8, 0x0E, 0x00, 0x48,
    0x8B, 0x98, 0x98, 0x02, 0x00, 0x00, 0x48, 0x63, 0xCB, 0x48, 0x3B, 0xCB,
});

// 0x3354C0
constexpr auto ToggleStateEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xE8, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1,
});

// 0x2F5940
constexpr auto GetStateStatListEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA,
    0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13,
});

// 0x2F7920
constexpr auto UnlinkStatListEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0xB2, 0x27,
    0x05, 0x00, 0x0F, 0xB6, 0xC8, 0x48, 0x8B, 0xD3, 0x48, 0x83, 0xC4, 0x20,
    0x5B, 0xE9,
});

// 0x2F4180
constexpr auto FreeStatListEntry = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xD2, 0x74, 0x0A, 0x83, 0x7A, 0x1C, 0x00, 0x0F, 0x8D, 0x21,
    0x5D, 0x00, 0x00, 0xC3,
});

// 0x34B9D0
constexpr auto GetUnitTypeEntry = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C,
});

// 0x34A540, left skill getter (as celestialrayone.whirlwind-follow-cursor checks it).
constexpr auto GetLeftSkillEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x13,
});

// FF 25: jmp qword [rip+disp32], the loader's thunk into D2RCore. The
// displacement depends on the loader build, so only the opcode is checked.
constexpr auto LoaderThunkJump = std::to_array<std::uint8_t>({0xFF, 0x25});

// 0x43B8C2..0x43B914, AuraStart's final arm (0x43B8FD -> 0x43B2B0, native 1).
constexpr auto AuraStartArmWindow = std::to_array<std::uint8_t>({
    0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00, 0x45, 0x8B, 0xCD, 0x41, 0xB8,
    0xCA, 0x00, 0x00, 0x00, 0x90, 0x89, 0x74, 0x24, 0x20, 0x49, 0x8B, 0xD7,
    0xE8, 0x21, 0xC3, 0xEB, 0xFF, 0x4C, 0x8B, 0x7C, 0x24, 0x40, 0xC7, 0x44,
    0x24, 0x28, 0x01, 0x00, 0x00, 0x00, 0x45, 0x8B, 0xCC, 0x45, 0x8B, 0xC6,
    0x44, 0x89, 0x6C, 0x24, 0x20, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCD, 0xE8,
    0xAE, 0xF9, 0xFF, 0xFF, 0x4C, 0x8B, 0xAC, 0x24, 0x80, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x5C, 0x24, 0x70, 0x4C, 0x8B, 0x64, 0x24, 0x78,
});

// 0x102490..0x1024C0, world click handler entry (mov rdi,rcx).
constexpr auto ClickHandlerEntry = std::to_array<std::uint8_t>({
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9, 0xE8, 0x82, 0xEB,
    0xFF, 0xFF, 0x85, 0xC0, 0x0F, 0x84, 0x4D, 0x06, 0x00, 0x00, 0x48, 0x89,
    0x5C, 0x24, 0x60, 0xB9, 0x18, 0x00, 0x00, 0x00, 0x48, 0x89, 0x74, 0x24,
    0x40, 0x4C, 0x89, 0x64, 0x24, 0x38, 0x4C, 0x89, 0x6C, 0x24, 0x30, 0x4C,
});

// 0x102700..0x102770, the skill node gate call (0x102753 -> 0x33F770).
constexpr auto ClickGateWindow = std::to_array<std::uint8_t>({
    0x49, 0x8B, 0xCF, 0xE8, 0xC8, 0x92, 0x24, 0x00, 0x41, 0xB8, 0x2E, 0x0A,
    0x00, 0x00, 0x48, 0x8D, 0x15, 0x3B, 0x4B, 0xBC, 0x01, 0x49, 0x8B, 0xCF,
    0x8B, 0xE8, 0xE8, 0x11, 0x7C, 0x24, 0x00, 0x83, 0x7C, 0x24, 0x68, 0x00,
    0x8B, 0xF0, 0x75, 0x20, 0x8B, 0xC5, 0x83, 0xE8, 0x02, 0x74, 0x0A, 0x83,
    0xE8, 0x02, 0x74, 0x05, 0x83, 0xF8, 0x01, 0x75, 0x0F, 0xF6, 0xC3, 0x02,
    0x74, 0x0A, 0x41, 0x83, 0xFC, 0x01, 0x0F, 0x85, 0x88, 0x03, 0x00, 0x00,
    0x4C, 0x8B, 0x64, 0x24, 0x70, 0x49, 0x8B, 0x16, 0x49, 0x8B, 0xCC, 0xE8,
    0x18, 0xD0, 0x23, 0x00, 0x85, 0xC0, 0x0F, 0x84, 0x97, 0x01, 0x00, 0x00,
    0x49, 0x8B, 0xCC, 0xE8, 0x78, 0x79, 0x24, 0x00, 0x49, 0x8B, 0x0E, 0x88,
    0x44, 0x24, 0x68, 0x48,
});

// Witnesses shared with celestialrayone.aura-reactivation (same image).

// 0x48B890, EVENT_DeleteByTypeId entry
constexpr auto DeleteEventsByTypeEntry = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x45, 0x8B, 0xF1, 0x45, 0x8B, 0xE0, 0x4C, 0x8B, 0xFA, 0x48, 0x8B,
    0xF1, 0x41, 0x83, 0xF8, 0x0E,
});

// 0x48B8F0, its node walk: next at +38h, unit at +08h
constexpr auto DeleteEventsWalkWitness = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0x6B, 0x38, 0x4C, 0x39, 0x7B, 0x08, 0x74, 0x0F, 0x48, 0x8D,
    0x4C, 0x24, 0x60, 0xE8, 0x2C, 0xF3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x0F, 0xB6, 0x03, 0x41, 0x3B, 0xC4, 0x0F, 0x85, 0x44, 0x01, 0x00,
    0x00, 0x45, 0x85, 0xF6, 0x74, 0x0A, 0x44, 0x39, 0x73, 0x18, 0x0F, 0x85,
    0x35, 0x01, 0x00, 0x00,
});

// 0x48FE50, returns [unit+148h]
constexpr auto GetUnitEventListBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xB9, 0xDD, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x48,
    0x8B, 0x81, 0x48, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x12BB40..0x12BC3C, the client's skill confirmation handler, whole
// function. rbx = packet from 0x12BB52 to the end; 0x12BBD7 xor edx,edx is
// the hard-coded right side; 0x12BBDF calls 0x14A930; its call bytes also
// prove 0x077E10 and 0x09A480.
constexpr auto ControllerSyncBody = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x20, 0x44, 0x0F, 0xB7, 0x71, 0x07, 0x48, 0x8B, 0xD9, 0x8B, 0x69, 0x09,
    0x33, 0xD2, 0x8B, 0x49, 0x02, 0xE8, 0x6E, 0xEA, 0xF6, 0xFF, 0x48, 0x8B,
    0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0xC8, 0x48, 0x89, 0x74, 0x24, 0x40, 0xE8, 0xC5, 0xF6, 0x21, 0x00, 0x48,
    0x85, 0xC0, 0x74, 0x21, 0x80, 0x7B, 0x06, 0x00, 0x74, 0x08, 0x8B, 0xB0,
    0x10, 0x02, 0x00, 0x00, 0xEB, 0x06, 0x8B, 0xB0, 0x14, 0x02, 0x00, 0x00,
    0xE8, 0xC7, 0x36, 0xF6, 0xFF, 0x3B, 0xF0, 0x0F, 0x87, 0x88, 0x00, 0x00,
    0x00, 0xE8, 0x6A, 0xC2, 0xF4, 0xFF, 0x84, 0xC0, 0x74, 0x63, 0xE8, 0x21,
    0xF7, 0xF5, 0xFF, 0x8B, 0xC8, 0xE8, 0xCA, 0xE8, 0xF6, 0xFF, 0x48, 0x3B,
    0xF8, 0x75, 0x52, 0x4C, 0x89, 0x7C, 0x24, 0x48, 0xE8, 0x7B, 0x8A, 0x01,
    0x00, 0x41, 0x8B, 0xD6, 0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xF0, 0xE8, 0x9D,
    0xED, 0x01, 0x00, 0x84, 0xC0, 0x74, 0x31, 0x45, 0x8B, 0xC6, 0x33, 0xD2,
    0x48, 0x8B, 0xCE, 0xE8, 0x4C, 0xED, 0x01, 0x00, 0x84, 0xC0, 0x75, 0x20,
    0x0F, 0xB6, 0x43, 0x06, 0x45, 0x33, 0xF6, 0xF6, 0xD8, 0xBD, 0xFF, 0xFF,
    0xFF, 0xFF, 0x44, 0x8B, 0xC5, 0xB1, 0x3C, 0x1B, 0xD2, 0x81, 0xE2, 0x00,
    0x00, 0x00, 0x80, 0xE8, 0x28, 0x0B, 0xFC, 0xFF, 0x4C, 0x8B, 0x7C, 0x24,
    0x48, 0x80, 0x7B, 0x06, 0x00, 0x44, 0x8B, 0xC5, 0x41, 0x0F, 0xB7, 0xD6,
    0x48, 0x8B, 0xCF, 0x74, 0x07, 0xE8, 0x4E, 0x30, 0x21, 0x00, 0xEB, 0x05,
    0xE8, 0xE7, 0x32, 0x21, 0x00, 0x48, 0x8B, 0x74, 0x24, 0x40, 0x48, 0x8B,
    0x5C, 0x24, 0x50, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5D, 0xC3,
});

// 0x144AEB..0x144B0D, the controller press processor: rcx manager, edx player
// index, r8d button at the call 0x144B00 -> 0x14A5A0.
constexpr auto ControllerPressWindow = std::to_array<std::uint8_t>({
    0x8B, 0xFA, 0x41, 0x0F, 0xB6, 0xF1, 0x44, 0x89, 0x44, 0x24, 0x38, 0x48,
    0x8B, 0xD9, 0x45, 0x84, 0xC9, 0x75, 0x0F, 0x8B, 0xD7, 0xE8, 0x9B, 0x5A,
    0x00, 0x00, 0x84, 0xC0, 0x0F, 0x84, 0x73, 0x07, 0x00, 0x00,
});

// 0x14A642..0x14A6B3, the press start reads the player (0x09A480), the
// button's side (0x117DE0), that side's skill (0x34A540 / 0x34B400) and the
// button's skill and guid (0x117D20 / 0x117CC0).
constexpr auto PressSideReadWindow = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x6C, 0x24, 0x58, 0x41, 0x8B, 0xCD, 0x48, 0x89, 0x74, 0x24,
    0x50, 0x48, 0x89, 0x7C, 0x24, 0x48, 0x4C, 0x89, 0x64, 0x24, 0x40, 0xE8,
    0x22, 0xFE, 0xF4, 0xFF, 0x41, 0x8B, 0xCF, 0x48, 0x8B, 0xF8, 0xE8, 0x77,
    0xD7, 0xFC, 0xFF, 0x88, 0x44, 0x24, 0x20, 0x48, 0x8B, 0xCF, 0x84, 0xC0,
    0x74, 0x07, 0xE8, 0xC7, 0xFE, 0x1F, 0x00, 0xEB, 0x05, 0xE8, 0x80, 0x0D,
    0x20, 0x00, 0x41, 0xB8, 0x5D, 0x05, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xE3,
    0x05, 0xB8, 0x01, 0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xE8, 0xE8, 0xE8, 0x39,
    0x1F, 0x00, 0x41, 0x8B, 0xCF, 0x44, 0x8B, 0xE0, 0xE8, 0x7D, 0xD6, 0xFC,
    0xFF, 0x41, 0x8B, 0xCF, 0x8B, 0xF0, 0xE8, 0x13, 0xD6, 0xFC, 0xFF, 0x89,
    0x44, 0x24, 0x28, 0x8B, 0xF8,
});

// 0x117DE0..0x117E29, the binding side getter: button <= 15, controller mode
// 0x077E10, bank 0x1195C0, byte [2A48A98h + 1Ch * (button + bank)].
constexpr auto BindingSideGetterBody = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xD9, 0x83, 0xFB, 0x0F,
    0x76, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0x33, 0xFA, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xE8, 0x09,
    0x00, 0xF6, 0xFF, 0x84, 0xC0, 0x74, 0x1E, 0xE8, 0xB0, 0x17, 0x00, 0x00,
    0x03, 0xC3, 0x48, 0x98, 0x48, 0x6B, 0xC8, 0x1C, 0x48, 0x8D, 0x05, 0x79,
    0x0C, 0x93, 0x02, 0x0F, 0xB6, 0x04, 0x01, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xC3,
});

// Aura level refresh.

// 0x561707..0x56177D, the aura callback builds the state request and calls the
// applier: r12 = args, request at [rbp-19h], call 0x433D20 at 0x561778. Its
// end is the return address the applier bridge recognises.
constexpr auto AuraCallbackRequestWindow = std::to_array<std::uint8_t>({
    0x4C, 0x8B, 0x61, 0x18, 0x45, 0x33, 0xFF, 0x4C, 0x89, 0x7D, 0x0F, 0x48,
    0x8B, 0xF1, 0x48, 0x8B, 0x49, 0x08, 0x4C, 0x8B, 0xF2, 0x48, 0x89, 0x4D,
    0xE7, 0x48, 0x89, 0x55, 0xEF, 0x41, 0x8B, 0x44, 0x24, 0x04, 0x89, 0x45,
    0xF7, 0x41, 0x8B, 0x44, 0x24, 0x08, 0x89, 0x45, 0xFB, 0x41, 0x8B, 0x44,
    0x24, 0x10, 0x89, 0x45, 0xFF, 0xC7, 0x45, 0x03, 0xFF, 0xFF, 0xFF, 0xFF,
    0x44, 0x89, 0x7D, 0x07, 0x41, 0x8B, 0x04, 0x24, 0x89, 0x45, 0x0B, 0x49,
    0x8B, 0x84, 0x24, 0x98, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x65, 0x7F, 0x48,
    0x85, 0xC0, 0x75, 0x11, 0x49, 0x3B, 0xCE, 0x48, 0x8D, 0x15, 0x07, 0x4A,
    0x00, 0x00, 0x41, 0x8B, 0xC7, 0x48, 0x0F, 0x44, 0xC2, 0x48, 0x8D, 0x4D,
    0xE7, 0x48, 0x89, 0x45, 0x0F, 0xE8, 0xA3, 0x25, 0xED, 0xFF,
});

// 0x433D20, jmp [rip+39F671Ah]: the thunk whose pointer level refresh replaces.
constexpr auto StateApplierThunkBytes = std::to_array<std::uint8_t>({
    0xFF, 0x25, 0x1A, 0x67, 0x9F, 0x03,
});

// 0x2F7A50, stores the remove callback at list +80h (read directly).
constexpr auto SetRemoveCallbackBody = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xC9, 0x74, 0x07, 0x48, 0x89, 0x91, 0x80, 0x00, 0x00, 0x00,
    0xC3,
});

// 0x563FFC, srvdofunc 65 passive stat loop: calcs at record +0D8h, stat ids at
// record +0BAh, 14 slots.
constexpr auto PassiveStatSlotsWitness = std::to_array<std::uint8_t>({
    0x4D, 0x8D, 0xBE, 0xD8, 0x00, 0x00, 0x00, 0x4D, 0x8D, 0xA6, 0xBA, 0x00,
    0x00, 0x00, 0x41, 0xBE, 0x0E, 0x00, 0x00, 0x00,
});

// 0x2F6E20, stat list owner id, [list+0Ch]
constexpr auto StatListOwnerIdBody = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x8B, 0x41, 0x0C, 0xC3,
});

// 0x2F6E30, stat list owner type, [list+08h]
constexpr auto StatListOwnerTypeBody = std::to_array<std::uint8_t>({
    0xB8, 0x06, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x03, 0x8B, 0x41,
    0x08, 0xC3,
});

// 0x2F6E40, stat list state, [list+20h]
constexpr auto StatListStateBody = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x8B, 0x41, 0x20, 0xC3,
});

// 0x2F6CA0, sets [list+0Ch] = id, [list+08h] = type
constexpr auto SetStatListOwnerBody = std::to_array<std::uint8_t>({
    0x44, 0x89, 0x41, 0x0C, 0x89, 0x51, 0x08, 0xC3,
});

// 0x2F6CB0, clears the other-caster record, [list+48h..+5Fh] and [list+60h]
constexpr auto ClearOtherCasterRecordBody = std::to_array<std::uint8_t>({
    0xC7, 0x41, 0x60, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x33, 0xC0,
    0x0F, 0x11, 0x41, 0x48, 0x48, 0x89, 0x41, 0x58, 0xC3,
});

// 0x2F7A00, sets [list+2Ch] = level
constexpr auto SetStatListLevelBody = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x21, 0x88, 0x4C, 0x24, 0x38,
    0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x8E, 0xA1, 0xFF, 0xFF, 0x84, 0xC0,
    0x74, 0x01, 0xCC, 0x89, 0x7B, 0x2C, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48,
    0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x89, 0x79,
    0x2C, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3,
});

// 0x2F56A0, stat list skill, [list+28h]
constexpr auto StatListSkillBody = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x1C, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x44, 0xBF, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0x43, 0x28,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x8B, 0x41, 0x28, 0x48, 0x83, 0xC4,
    0x20, 0x5B, 0xC3,
});

// 0x2F56E0, stat list level, [list+2Ch]
constexpr auto StatListLevelBody = std::to_array<std::uint8_t>({
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x75, 0x1C, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8,
    0x44, 0xC5, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0x43, 0x2C,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x8B, 0x41, 0x2C, 0x48, 0x83, 0xC4,
});

// 0x2F5810, the unit's stat list with a flag (the applier's curse lookup)
constexpr auto StatListByFlagEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x48,
});

// 0x335A40, states.txt curse test
constexpr auto IsCurseStateEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
    0x83, 0xEC, 0x20, 0x48, 0x63, 0xDA, 0x0F, 0xB6, 0xF1, 0x85, 0xD2, 0x78,
    0x30,
});

// 0x34A0E0, byte [unit+1BDh]
constexpr auto UnitDataContextBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x0F,
    0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x34A330, [unit+08h]
constexpr auto GetUnitIdBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0x8B, 0x41, 0x08, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x3351B0, STATES_CheckState
constexpr auto CheckStateEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF1,
});

// 0x3355A0, state change flag (what the aura callback sets before it changes a stat)
constexpr auto MarkStateChangedEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x30, 0x41, 0x8B, 0xE8, 0x48, 0x63, 0xDA, 0x48, 0x8B, 0xF1,
});

// 0x300A90, data tables of a game version
constexpr auto DataTablesEntry = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38,
    0x48, 0x83, 0xF8, 0x04,
});

// 0x436240, the applier's default remove callback
constexpr auto DefaultRemoveCallbackEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xCA, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xDA,
});

// The call sites, stock bytes.
constexpr auto LeftSelectCall        = std::to_array<std::uint8_t>({0xE8, 0x7C, 0x61, 0xF0, 0xFF});
constexpr auto RightAuraStatListCall = std::to_array<std::uint8_t>({0xE8, 0x39, 0xCD, 0xEB, 0xFF});
constexpr auto RightAuraStateOffCall = std::to_array<std::uint8_t>({0xE8, 0x88, 0xC8, 0xEF, 0xFF});
constexpr auto AuraStartArmCall      = std::to_array<std::uint8_t>({0xE8, 0xAE, 0xF9, 0xFF, 0xFF});
constexpr auto ClickGateCall         = std::to_array<std::uint8_t>({0xE8, 0x18, 0xD0, 0x23, 0x00});
constexpr auto ControllerSyncCall    = std::to_array<std::uint8_t>({0xE8, 0x4C, 0xED, 0x01, 0x00});
constexpr auto ControllerPressCall   = std::to_array<std::uint8_t>({0xE8, 0x9B, 0x5A, 0x00, 0x00});
constexpr auto RightSelectCall          = std::to_array<std::uint8_t>({0xE8, 0x02, 0x64, 0xF0, 0xFF});  // -> 0x33EF10
constexpr auto RightAuraTimerDeleteCall = std::to_array<std::uint8_t>({0xE8, 0x39, 0x2C, 0x05, 0x00});  // -> 0x48B890
constexpr auto RightAuraStartCall       = std::to_array<std::uint8_t>({0xE8, 0xFB, 0x29, 0x00, 0x00});  // -> 0x43B680
constexpr auto ItemRefreshFinishCall    = std::to_array<std::uint8_t>({0xE8, 0xD8, 0xF3, 0x08, 0x00});  // -> 0x5005A0

// 0x34FB00, the unit flag test (unit, flag): null unit 0, else
// test [rcx+128h],edx / setne al.
constexpr auto UnitFlagTestBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x49, 0x4F, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x85,
    0x91, 0x28, 0x01, 0x00, 0x00, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x95,
    0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// 0x33EF10, SetRightActiveSkill entry (rcx unit, edx skill, r8d owner guid).
constexpr auto SetRightActiveSkillEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF0, 0x8B,
    0xDA, 0x48, 0x8B, 0xE9,
});

// 0x5005A0, the pet limit count entry (rcx game, rdx unit): test rdx,rdx.
constexpr auto UpdatePetLimitsEntry = std::to_array<std::uint8_t>({
    0x48, 0x85, 0xD2, 0x0F, 0x84, 0x89, 0x04, 0x00, 0x00, 0x55, 0x57, 0x41,
    0x57,
});

// 0x4702AE..0x4702EE, the oskill case of the server stat callback (rdi unit,
// r15 skill node): a button holding the node gets skill 0 through AssignSkill,
// left 0x4702C9, right 0x4702E9.
constexpr auto OskillButtonResetWindow = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0xCF, 0xE8, 0x8A, 0xA2, 0xED, 0xFF, 0x49, 0x3B, 0xC7, 0x75,
    0x13, 0x41, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0x45, 0x33, 0xC0, 0x8B, 0xD3,
    0x48, 0x8B, 0xCF, 0xE8, 0xA2, 0x87, 0xFC, 0xFF, 0x48, 0x8B, 0xCF, 0xE8,
    0x2A, 0xB1, 0xED, 0xFF, 0x49, 0x3B, 0xC7, 0x75, 0x13, 0x41, 0xB9, 0xFF,
    0xFF, 0xFF, 0xFF, 0x45, 0x33, 0xC0, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8,
    0x82, 0x87, 0xFC, 0xFF,
});

// 0x470E77..0x470EA8 and 0x470F79..0x470FAA, the equipment re-check: unit flag
// 80000000h on (0x34E140), detach (0x475020) or attach (0x475090), flag off.
constexpr auto ItemRefreshDetachWindow = std::to_array<std::uint8_t>({
    0xBA, 0x00, 0x00, 0x00, 0x80, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xCF, 0xE8, 0xB6, 0xD2, 0xED, 0xFF, 0x4C, 0x8B, 0xC3, 0x48, 0x8B,
    0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0x88, 0x41, 0x00, 0x00, 0x45, 0x33, 0xC0,
    0xBA, 0x00, 0x00, 0x00, 0x80, 0x48, 0x8B, 0xCF, 0xE8, 0x98, 0xD2, 0xED,
    0xFF,
});
constexpr auto ItemRefreshAttachWindow = std::to_array<std::uint8_t>({
    0xBA, 0x00, 0x00, 0x00, 0x80, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48,
    0x8B, 0xCF, 0xE8, 0xB4, 0xD1, 0xED, 0xFF, 0x4C, 0x8B, 0xC3, 0x48, 0x8B,
    0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0xF6, 0x40, 0x00, 0x00, 0x45, 0x33, 0xC0,
    0xBA, 0x00, 0x00, 0x00, 0x80, 0x48, 0x8B, 0xCF, 0xE8, 0x96, 0xD1, 0xED,
    0xFF,
});

// 0x471096..0x4710A3, the saved selection goes back: lea rdx,[rsp+30h] /
// mov rcx,rdi / call 0x474AA0.
constexpr auto ItemRefreshRestoreWindow = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0x54, 0x24, 0x30, 0x48, 0x8B, 0xCF, 0xE8, 0xFD, 0x39, 0x00,
    0x00,
});

// 0x4711BD..0x4711C8, the pet limit count: mov rdx,rdi / mov rcx,r15 /
// call 0x5005A0 (the relay site).
constexpr auto ItemRefreshFinishWindow = std::to_array<std::uint8_t>({
    0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0xD8, 0xF3, 0x08, 0x00,
});

// Inline hook entry: whole instructions, none of them relative.
constexpr std::uint32_t PeriodicSkillEventHookBytes = 22;  // two home saves, five pushes, sub rsp,50h

// ---------------------------------------------------------------------------
//  Relay page. One page the plugin allocates within rel32 reach of the image;
//  nothing inside D2R's own code is used as a cave.
//
//      +0x000  UNWIND_INFO, no frame (every stub leaves rsp untouched)
//      +0x010  RUNTIME_FUNCTION table, 11 entries
//      +0x100  mov rax,<bridge> / jmp rax      left select
//      +0x120  mov rax,<bridge> / jmp rax      right aura stat list
//      +0x140  mov rax,<bridge> / jmp rax      right aura state off
//      +0x160  mov rax,<bridge> / jmp rax      AuraStart arm
//      +0x180  lea r8,[r14-28h] / mov rax,<bridge> / jmp rax   click gate
//      +0x1A0  cmp byte [rbx+6],0 / setne dl / movzx edx,dl /
//              mov rax,<bridge> / jmp rax      controller confirmation side
//      +0x1C0  mov rax,<bridge> / jmp rax      controller press
//      +0x1E0  mov rax,<bridge> / jmp rax      right select
//      +0x200  mov rax,<bridge> / jmp rax      right timer delete
//      +0x220  mov rax,<bridge> / jmp rax      right AuraStart
//      +0x240  mov rax,<bridge> / jmp rax      equipment re-check pet limits
//
//  A stub is reached by the retargeted call, so the return address is on the
//  stack and the stock arguments are in place: the bridge is entered as if
//  the game had called it. rax and r8 are volatile and not arguments of the
//  replaced callees (0x33F770 takes two). At 0x12BBDF rbx is the packet and
//  edx is about to be the constant 0, so the stub only replaces edx.
// ---------------------------------------------------------------------------
constexpr std::size_t RelayPageBytes           = 0x1000;
constexpr std::size_t RelayUnwindOffset        = 0x000;
constexpr std::size_t RelayFunctionTableOffset = 0x010;
constexpr std::size_t RelayStubCount           = 11;
enum RelayStub : std::size_t {
    StubLeftSelect        = 0,
    StubRightAuraStatList = 1,
    StubRightAuraStateOff = 2,
    StubAuraStartArm      = 3,
    StubClickGate         = 4,
    StubControllerSync    = 5,
    StubControllerPress   = 6,
    StubRightSelect          = 7,
    StubRightAuraTimerDelete = 8,
    StubRightAuraStart       = 9,
    StubItemRefreshFinish    = 10,
};
constexpr std::array<std::size_t, RelayStubCount> RelayStubOffsets{0x100, 0x120, 0x140, 0x160, 0x180, 0x1A0,
                                                                   0x1C0, 0x1E0, 0x200, 0x220, 0x240};
constexpr std::array<std::size_t, RelayStubCount> RelayStubSizes{12, 12, 12, 12, 16, 22, 12, 12, 12, 12, 12};
static_assert(RelayFunctionTableOffset + RelayStubCount * 12 <= 0x100, "the function table must end before the stubs");

// ---------------------------------------------------------------------------
//  Native signatures
// ---------------------------------------------------------------------------
using SetLeftActiveSkillFn  = std::int64_t(__fastcall*)(void* unit, std::int32_t skillId, std::int32_t ownerGuid) noexcept;
using PeriodicSkillEventFn  = void(__fastcall*)(void* game, void* unit, std::int32_t customId, std::int32_t customParam, std::int32_t castId) noexcept;
using AuraStartFn           = std::int64_t(__fastcall*)(void* game, void* unit, std::int32_t skillId) noexcept;
using ArmPeriodicSkillFn    = std::int64_t(__fastcall*)(void* game, void* unit, std::int32_t skillId, std::int32_t level, std::int32_t castId, std::int32_t rightTimer) noexcept;
using ServerDoSkillFn       = std::int32_t(__fastcall*)(void* game, void* unit, std::int32_t skillId, std::int32_t level, std::int32_t consume, std::int32_t itemCast, std::int32_t itemEffect) noexcept;
using SkillNodeGateFn       = std::uint64_t(__fastcall*)(void* player, void* node) noexcept;
using GetSkillNodeFn        = void*(__fastcall*)(void* unit) noexcept;
using GetGameFn             = void*(__fastcall*)(void* unit) noexcept;
using CheckStateFn          = std::int32_t(__fastcall*)(void* unit, std::int32_t state) noexcept;
using DeleteEventsByTypeFn  = std::uint64_t(__fastcall*)(void* game, void* unit, std::int32_t type, std::uint32_t customId) noexcept;
using GetSkillsTxtRecordFn  = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext, std::int32_t skillId) noexcept;
using GetStateCountFn       = std::int32_t(__fastcall*)(std::uint32_t dataContext) noexcept;
using ToggleStateFn         = void(__fastcall*)(void* unit, std::int32_t state, std::int32_t enable) noexcept;
using GetStateStatListFn    = void*(__fastcall*)(void* unit, std::int32_t state) noexcept;
using UnlinkStatListFn      = void(__fastcall*)(void* unit, void* statList) noexcept;
using FreeStatListFn        = void(__fastcall*)(std::uint32_t dataContext, void* statList) noexcept;
using GetUnitTypeFn         = std::int32_t(__fastcall*)(void* unit) noexcept;
using ReadSkillLevelFn      = std::int32_t(__fastcall*)(void* unit, void* skillNode, std::int32_t withBonuses, std::int32_t reserved) noexcept;
using GetUnitCastIdFn       = std::uint32_t(__fastcall*)(void* unit) noexcept;
using SetUnitCastIdFn       = void(__fastcall*)(void* unit, std::uint32_t castId) noexcept;
using SetUnitFlagFn         = void(__fastcall*)(void* unit, std::uint32_t flag, std::int32_t enable) noexcept;
using IsUnitDeadFn          = std::int32_t(__fastcall*)(void* unit) noexcept;
using GetUnitDataContextFn  = std::uint8_t(__fastcall*)(void* unit) noexcept;
using ToggleBindingCheckFn  = bool(__fastcall*)(void* manager, std::uint8_t side, std::uint32_t skillId) noexcept;
using PressStartFn          = bool(__fastcall*)(void* manager, std::uint32_t player, std::uint32_t button) noexcept;
using IsControllerModeFn    = bool(__fastcall*)() noexcept;
using BindingBankFn         = std::int32_t(__fastcall*)() noexcept;
using GetClientPlayerFn     = void*(__fastcall*)(std::uint32_t index) noexcept;
using ApplyStateEffectFn    = void*(__fastcall*)(StateEffectRequest* request) noexcept;
using IsCurseStateFn        = std::int32_t(__fastcall*)(std::uint32_t dataContext, std::int32_t state) noexcept;
using StatListByFlagFn      = void*(__fastcall*)(void* unit, std::uint32_t flag) noexcept;
using StatListValueFn       = std::int32_t(__fastcall*)(void* statList) noexcept;
using SetStatListLevelFn    = void(__fastcall*)(void* statList, std::int32_t level) noexcept;
using SetStatListOwnerFn    = void(__fastcall*)(void* statList, std::int32_t unitType, std::uint32_t unitId) noexcept;
using StatListActionFn      = void(__fastcall*)(void* statList) noexcept;
using SetRemoveCallbackFn   = void(__fastcall*)(void* statList, void* callback) noexcept;
using GetUnitIdFn           = std::uint32_t(__fastcall*)(void* unit) noexcept;
using MarkStateChangedFn    = void(__fastcall*)(void* unit, std::int32_t state, std::int32_t enable) noexcept;
using DataTablesFn          = const std::uint8_t*(__fastcall*)(std::uint32_t dataContext) noexcept;
using ReadListStatFn        = std::int32_t(__fastcall*)(std::uint32_t dataContext, void* statList, std::int32_t stat, std::int32_t layer) noexcept;
using SetListStatFn         = void(__fastcall*)(std::uint32_t dataContext, void* statList, std::int32_t stat, std::int32_t value, std::int32_t layer) noexcept;
using UnitFlagTestFn        = std::int32_t(__fastcall*)(void* unit, std::uint32_t flag) noexcept;
using UpdatePetLimitsFn     = std::int64_t(__fastcall*)(void* game, void* unit) noexcept;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
struct Settings {
    bool enabled{true};
    bool leftSlotAuras{true};
    bool singleEmanation{true};
    bool moveOnlyClicks{true};
    bool liveLevel{true};
    bool itemRefreshKeepsAuras{true};
    bool levelUpInPlace{true};
    bool takeoverInPlace{true};
    bool controllerAuraActivation{true};
    bool controllerTwoAuras{true};
    bool diagnostics{false};
};

struct InstallState {
    bool selectRelay{false};
    bool timerHook{false};
    bool startRelay{false};
    bool rightRemovalRelays{false};
    bool clickRelay{false};
    bool syncRelay{false};
    bool pressRelay{false};
    bool applierPointer{false};
    bool itemRefreshRelays{false};
};

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Settings                   Config{};
InstallState               Installed{};
char                       ConfigSource[48]{"built-in defaults"};
std::atomic<bool>          ServerActive{false};
std::atomic<bool>          SingleEmanationActive{false};
std::atomic<bool>          LiveLevelActive{false};
std::atomic<bool>          ClientActive{false};
std::atomic<bool>          ControllerActive{false};
std::atomic<bool>          ControllerTwoAurasActive{false};
std::atomic<bool>          LevelRefreshActive{false};
std::atomic<bool>          LevelUpInPlaceActive{false};
std::atomic<bool>          TakeoverInPlaceActive{false};
std::atomic<bool>          ItemRefreshActive{false};
std::uintptr_t             AuraCallbackReturn{};
std::uint64_t              ApplierPointerOriginal{};
std::uint64_t              ApplierPointerBridge{};

SetLeftActiveSkillFn  SetLeftActiveSkill{};
SetLeftActiveSkillFn  SetRightActiveSkill{};   // same signature, 0x33EF10
UnitFlagTestFn        UnitFlagTest{};
UpdatePetLimitsFn     UpdatePetLimits{};
PeriodicSkillEventFn  OriginalPeriodicSkillEvent{};
AuraStartFn           AuraStart{};
ArmPeriodicSkillFn    ArmPeriodicSkill{};
ServerDoSkillFn       ServerDoSkill{};
SkillNodeGateFn       SkillNodeGate{};
GetSkillNodeFn        GetLeftSkill{};
GetSkillNodeFn        GetRightSkill{};
GetGameFn             GetGame{};
CheckStateFn          CheckState{};
DeleteEventsByTypeFn  DeleteEventsByType{};
GetSkillsTxtRecordFn  GetSkillsTxtRecord{};
GetStateCountFn       GetStateCount{};
ToggleStateFn         ToggleState{};
GetStateStatListFn    GetStateStatList{};
UnlinkStatListFn      UnlinkStatList{};
FreeStatListFn        FreeStatList{};
GetUnitTypeFn         GetUnitType{};
ReadSkillLevelFn      ReadSkillLevel{};
GetUnitCastIdFn       GetUnitCastId{};
SetUnitCastIdFn       SetUnitCastId{};
SetUnitFlagFn         SetUnitFlag{};
IsUnitDeadFn          IsUnitDead{};
GetUnitDataContextFn  GetUnitDataContext{};
ToggleBindingCheckFn  ToggleBindingCheck{};
PressStartFn          PressStart{};
IsControllerModeFn    IsControllerMode{};
BindingBankFn         BindingBank{};
GetClientPlayerFn     GetClientPlayer{};
ApplyStateEffectFn    OriginalStateApplier{};
IsCurseStateFn        IsCurseState{};
StatListByFlagFn      StatListByFlag{};
StatListValueFn       StatListState{};
StatListValueFn       StatListSkill{};
StatListValueFn       StatListLevel{};
SetStatListLevelFn    SetStatListLevel{};
StatListValueFn       StatListOwnerType{};
StatListValueFn       StatListOwnerId{};
SetStatListOwnerFn    SetStatListOwner{};
StatListActionFn      ClearOtherCasterRecord{};
SetRemoveCallbackFn   SetRemoveCallback{};
GetUnitIdFn           GetUnitId{};
MarkStateChangedFn    MarkStateChanged{};
DataTablesFn          DataTables{};
ReadListStatFn        ReadListStat{};
SetListStatFn         SetListStat{};
void*                 DefaultRemoveCallback{};

std::uint8_t* RelayPage{};
bool          RelayUnwindRegistered{};

struct CallSitePatch {
    std::uint64_t               rva;
    const std::uint8_t*         stock;
    std::size_t                 stub;
    std::array<std::uint8_t, 5> written;
    bool                        applied;
};
std::array<CallSitePatch, RelayStubCount> CallSites{{
    {LeftSelectCallRva, LeftSelectCall.data(), StubLeftSelect, {}, false},
    {RightAuraStatListCallRva, RightAuraStatListCall.data(), StubRightAuraStatList, {}, false},
    {RightAuraStateOffCallRva, RightAuraStateOffCall.data(), StubRightAuraStateOff, {}, false},
    {AuraStartArmCallRva, AuraStartArmCall.data(), StubAuraStartArm, {}, false},
    {ClickGateCallRva, ClickGateCall.data(), StubClickGate, {}, false},
    {ControllerSyncCallRva, ControllerSyncCall.data(), StubControllerSync, {}, false},
    {ControllerPressCallRva, ControllerPressCall.data(), StubControllerPress, {}, false},
    {RightSelectCallRva, RightSelectCall.data(), StubRightSelect, {}, false},
    {RightAuraTimerDeleteCallRva, RightAuraTimerDeleteCall.data(), StubRightAuraTimerDelete, {}, false},
    {RightAuraStartCallRva, RightAuraStartCall.data(), StubRightAuraStart, {}, false},
    {ItemRefreshFinishCallRva, ItemRefreshFinishCall.data(), StubItemRefreshFinish, {}, false},
}};

std::atomic<std::uint64_t> LeftStarts{};
std::atomic<std::uint64_t> LeftStartsRefused{};
std::atomic<std::uint64_t> LeftKeptRunning{};
std::atomic<std::uint64_t> LeftStopsReselect{};
std::atomic<std::uint64_t> LeftStopsOffButton{};
std::atomic<std::uint64_t> LeftStopsInvalid{};
std::atomic<std::uint64_t> LeftTicks{};
std::atomic<std::uint64_t> SharedStatesKept{};
std::atomic<std::uint64_t> EmanationsSkipped{};
std::atomic<std::uint64_t> MoveOnlyClicks{};
std::atomic<std::uint64_t> LiveLevelTicks{};
std::atomic<std::uint64_t> LiveLevelChanges{};
std::atomic<std::uint64_t> ControllerLeftConfirmsKept{};
std::atomic<std::uint64_t> ControllerLeftConfirmsReset{};
std::atomic<std::uint64_t> ControllerAuraPresses{};
std::atomic<std::uint64_t> ControllerSidesMoved{};
std::atomic<std::uint64_t> ControllerAurasReplaced{};
std::atomic<std::uint64_t> AuraApplications{};
std::atomic<std::uint64_t> LevelUpsKept{};
std::atomic<std::uint64_t> TakeoversKept{};
std::atomic<std::uint64_t> RemoveCallbacksAdded{};
std::atomic<std::uint64_t> PassiveStatsRemoved{};
std::atomic<std::uint64_t> StatesTurnedOn{};
std::atomic<std::uint64_t> GameRefusalsRestored{};
std::atomic<std::uint64_t> RebuiltAnyway{};
std::atomic<std::uint64_t> LowerLevelsRefused{};
std::atomic<std::uint64_t> OtherSkillsReplaced{};
std::atomic<std::uint64_t> AurasKeptByItemOperations{};
std::atomic<std::uint64_t> LeftKeptThroughItems{};
std::atomic<std::uint64_t> RightKeptThroughItems{};
std::atomic<std::uint64_t> LeftStopsItemsGone{};
std::atomic<std::uint64_t> RightStopsItemsGone{};
std::atomic<std::uint32_t> RebuildWarnings{};
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

auto ReadU16(const void* base, std::size_t offset) noexcept -> std::uint16_t {
    std::uint16_t value = 0;
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


void ParseConfig(std::string_view text) noexcept {
    std::size_t lineNumber = 0;
    for (std::size_t start = 0; start <= text.size();) {
        ++lineNumber;
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;

        const auto comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            LogWarn("LeftClickAuras: config line %zu ignored, expected key = value.", lineNumber);
            continue;
        }
        const auto key   = Trim(line.substr(0, equals));
        const auto value = Trim(line.substr(equals + 1));

        bool* target = nullptr;
        if (key == "enabled") {
            target = &Config.enabled;
        } else if (key == "left_slot_auras") {
            target = &Config.leftSlotAuras;
        } else if (key == "single_emanation") {
            target = &Config.singleEmanation;
        } else if (key == "move_only_clicks") {
            target = &Config.moveOnlyClicks;
        } else if (key == "live_level") {
            target = &Config.liveLevel;
        } else if (key == "item_refresh_keeps_auras") {
            target = &Config.itemRefreshKeepsAuras;
        } else if (key == "level_up_in_place") {
            target = &Config.levelUpInPlace;
        } else if (key == "takeover_in_place") {
            target = &Config.takeoverInPlace;
        } else if (key == "controller_aura_activation") {
            target = &Config.controllerAuraActivation;
        } else if (key == "controller_two_auras") {
            target = &Config.controllerTwoAuras;
        } else if (key == "diagnostics") {
            target = &Config.diagnostics;
        } else {
            LogWarn("LeftClickAuras: config line %zu has an unknown key.", lineNumber);
            continue;
        }

        if (value == "true") {
            *target = true;
        } else if (value == "false") {
            *target = false;
        } else {
            LogWarn("LeftClickAuras: config line %zu ignored, the value must be true or false.", lineNumber);
        }
    }
}

void LoadConfig() noexcept {
    Config = Settings{};
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "built-in defaults");
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        LogWarn("LeftClickAuras: could not create the default config, using built-in defaults.");
    }

    std::string buffer(16384, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required <= buffer.size()) {
            LogWarn("LeftClickAuras: could not read the config, using built-in defaults.");
            return;
        }
        buffer.assign(required, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            LogWarn("LeftClickAuras: could not read the config, using built-in defaults.");
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
    LogError("LeftClickAuras: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
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
    if (Context->QueryService(&diagnostics)
            == D2RL::ServiceQueryResult::Success
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
            LogInfo("LeftClickAuras: %s at RVA 0x%llX is hooked by another plugin, calling through it.", name,
                    static_cast<unsigned long long>(rva));
            return true;
        }
    }

    LogError("LeftClickAuras: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
             static_cast<unsigned long long>(rva));
    return false;
}


auto ValidateServer() noexcept -> bool {
    return CheckExact(AssignSkillRva, AssignSkillBody.data(), AssignSkillBody.size(), "skill assignment")
        && CheckExact(PeriodicSkillEventRva, PeriodicSkillEventWindow.data(), PeriodicSkillEventWindow.size(),
                      "aura timer")
        && CheckExact(AuraStartArmWindowRva, AuraStartArmWindow.data(), AuraStartArmWindow.size(),
                      "aura start arm")
        && CheckCallable(DeleteEventsByTypeRva, DeleteEventsByTypeEntry.data(), DeleteEventsByTypeEntry.size(),
                         "EVENT_DeleteByTypeId entry")
        && CheckExact(DeleteEventsWalkRva, DeleteEventsWalkWitness.data(), DeleteEventsWalkWitness.size(),
                      "EVENT_DeleteByTypeId node walk")
        && CheckCallable(GetUnitEventListRva, GetUnitEventListBody.data(), GetUnitEventListBody.size(),
                         "unit event list getter")
        && CheckExact(SetEventStoreRva, SetEventStoreWitness.data(), SetEventStoreWitness.size(),
                      "EVENT_SetEvent node stores")
        && CheckCallable(GetSkillsTxtRecordRva, GetSkillsTxtRecordEntry.data(), GetSkillsTxtRecordEntry.size(),
                         "skills.txt record getter")
        && CheckCallable(GetStateCountRva, GetStateCountEntry.data(), GetStateCountEntry.size(), "state count")
        && CheckCallable(ToggleStateRva, ToggleStateEntry.data(), ToggleStateEntry.size(), "STATES_ToggleState")
        && CheckCallable(GetStateStatListRva, GetStateStatListEntry.data(), GetStateStatListEntry.size(),
                         "state stat list getter")
        && CheckCallable(UnlinkStatListRva, UnlinkStatListEntry.data(), UnlinkStatListEntry.size(),
                         "STATLIST_UnlinkStatList")
        && CheckCallable(FreeStatListRva, FreeStatListEntry.data(), FreeStatListEntry.size(),
                         "STATLIST_FreeStatList")
        && CheckCallable(GetUnitTypeRva, GetUnitTypeEntry.data(), GetUnitTypeEntry.size(), "UNITS_GetUnitType")
        && CheckCallable(GetLeftSkillRva, GetLeftSkillEntry.data(), GetLeftSkillEntry.size(), "left skill getter")
        && CheckCallable(ReadSkillLevelRva, LoaderThunkJump.data(), LoaderThunkJump.size(),
                         "ReadWideSkillLevel (loader thunk)")
        && CheckCallable(SetUnitCastIdRva, LoaderThunkJump.data(), LoaderThunkJump.size(),
                         "SetUnitCastId (loader thunk)");
}

// The equipment re-check's item operation flag, the oskill button reset it
// causes, the selection restore and the pet limit count the relay sits on.
// AssignSkill's three new call sites are inside the AssignSkill witness.
auto ValidateItemRefresh() noexcept -> bool {
    return CheckExact(OskillButtonResetRva, OskillButtonResetWindow.data(), OskillButtonResetWindow.size(),
                      "oskill button reset")
        && CheckExact(ItemRefreshDetachRva, ItemRefreshDetachWindow.data(), ItemRefreshDetachWindow.size(),
                      "equipment re-check detach")
        && CheckExact(ItemRefreshAttachRva, ItemRefreshAttachWindow.data(), ItemRefreshAttachWindow.size(),
                      "equipment re-check attach")
        && CheckExact(ItemRefreshRestoreRva, ItemRefreshRestoreWindow.data(), ItemRefreshRestoreWindow.size(),
                      "equipment re-check selection restore")
        && CheckExact(ItemRefreshFinishWindowRva, ItemRefreshFinishWindow.data(), ItemRefreshFinishWindow.size(),
                      "equipment re-check pet limit count")
        && CheckCallable(UnitFlagTestRva, UnitFlagTestBody.data(), UnitFlagTestBody.size(), "unit flag test")
        && CheckCallable(SetRightActiveSkillRva, SetRightActiveSkillEntry.data(), SetRightActiveSkillEntry.size(),
                         "right skill setter")
        && CheckCallable(UpdatePetLimitsRva, UpdatePetLimitsEntry.data(), UpdatePetLimitsEntry.size(),
                         "pet limit count");
}

auto ValidateClient() noexcept -> bool {
    return CheckExact(ClickHandlerRva, ClickHandlerEntry.data(), ClickHandlerEntry.size(), "world click handler")
        && CheckExact(ClickGateWindowRva, ClickGateWindow.data(), ClickGateWindow.size(), "world click skill gate")
        && CheckCallable(GetLeftSkillRva, GetLeftSkillEntry.data(), GetLeftSkillEntry.size(), "left skill getter");
}

// Runs before the server relays are written: the AssignSkill witness also
// proves 0x34A0E0, and it contains call sites this plugin changes.
auto ValidateController() noexcept -> bool {
    return CheckExact(ControllerSyncRva, ControllerSyncBody.data(), ControllerSyncBody.size(),
                      "controller skill confirmation")
        && CheckExact(ControllerPressWindowRva, ControllerPressWindow.data(), ControllerPressWindow.size(),
                      "controller press")
        && CheckExact(PressSideReadWindowRva, PressSideReadWindow.data(), PressSideReadWindow.size(),
                      "controller press side read")
        && CheckExact(BindingSideGetterRva, BindingSideGetterBody.data(), BindingSideGetterBody.size(),
                      "controller binding side getter")
        && CheckExact(AssignSkillRva, AssignSkillBody.data(), AssignSkillBody.size(), "skill assignment")
        && CheckCallable(GetSkillsTxtRecordRva, GetSkillsTxtRecordEntry.data(), GetSkillsTxtRecordEntry.size(),
                         "skills.txt record getter")
        && CheckCallable(GetLeftSkillRva, GetLeftSkillEntry.data(), GetLeftSkillEntry.size(), "left skill getter");
}

auto ValidateLevelRefresh() noexcept -> bool {
    return CheckExact(AuraCallbackRequestRva, AuraCallbackRequestWindow.data(), AuraCallbackRequestWindow.size(),
                      "aura callback state request")
        && CheckExact(StateApplierThunkRva, StateApplierThunkBytes.data(), StateApplierThunkBytes.size(),
                      "state applier thunk")
        && CheckExact(SetRemoveCallbackRva, SetRemoveCallbackBody.data(), SetRemoveCallbackBody.size(),
                      "stat list remove callback setter")
        && CheckExact(PassiveStatSlotsRva, PassiveStatSlotsWitness.data(), PassiveStatSlotsWitness.size(),
                      "srvdofunc 65 passive stat loop")
        && CheckCallable(StatListOwnerIdRva, StatListOwnerIdBody.data(), StatListOwnerIdBody.size(),
                         "stat list owner id")
        && CheckCallable(StatListOwnerTypeRva, StatListOwnerTypeBody.data(), StatListOwnerTypeBody.size(),
                         "stat list owner type")
        && CheckCallable(StatListStateRva, StatListStateBody.data(), StatListStateBody.size(), "stat list state")
        && CheckCallable(SetStatListOwnerRva, SetStatListOwnerBody.data(), SetStatListOwnerBody.size(),
                         "stat list owner setter")
        && CheckCallable(ClearOtherCasterRecordRva, ClearOtherCasterRecordBody.data(),
                         ClearOtherCasterRecordBody.size(), "stat list other caster record clear")
        && CheckCallable(SetStatListLevelRva, SetStatListLevelBody.data(), SetStatListLevelBody.size(),
                         "stat list level setter")
        && CheckCallable(StatListSkillRva, StatListSkillBody.data(), StatListSkillBody.size(), "stat list skill")
        && CheckCallable(StatListLevelRva, StatListLevelBody.data(), StatListLevelBody.size(), "stat list level")
        && CheckCallable(StatListByFlagRva, StatListByFlagEntry.data(), StatListByFlagEntry.size(),
                         "stat list by flag")
        && CheckCallable(IsCurseStateRva, IsCurseStateEntry.data(), IsCurseStateEntry.size(), "curse state test")
        && CheckCallable(GetUnitDataContextRva, UnitDataContextBody.data(), UnitDataContextBody.size(),
                         "unit data context")
        && CheckCallable(GetUnitIdRva, GetUnitIdBody.data(), GetUnitIdBody.size(), "unit id")
        && CheckCallable(CheckStateRva, CheckStateEntry.data(), CheckStateEntry.size(), "STATES_CheckState")
        && CheckCallable(MarkStateChangedRva, MarkStateChangedEntry.data(), MarkStateChangedEntry.size(),
                         "state change flag")
        && CheckCallable(DataTablesRva, DataTablesEntry.data(), DataTablesEntry.size(), "data tables")
        && CheckCallable(DefaultRemoveCallbackRva, DefaultRemoveCallbackEntry.data(),
                         DefaultRemoveCallbackEntry.size(), "default state remove callback")
        && CheckCallable(GetSkillsTxtRecordRva, GetSkillsTxtRecordEntry.data(), GetSkillsTxtRecordEntry.size(),
                         "skills.txt record getter")
        && CheckCallable(GetStateCountRva, GetStateCountEntry.data(), GetStateCountEntry.size(), "state count")
        && CheckCallable(ToggleStateRva, ToggleStateEntry.data(), ToggleStateEntry.size(), "STATES_ToggleState")
        && CheckCallable(GetStateStatListRva, GetStateStatListEntry.data(), GetStateStatListEntry.size(),
                         "state stat list getter")
        && CheckCallable(GetUnitTypeRva, GetUnitTypeEntry.data(), GetUnitTypeEntry.size(), "UNITS_GetUnitType")
        && CheckCallable(ReadListStatRva, LoaderThunkJump.data(), LoaderThunkJump.size(),
                         "ReadWideListStat (loader thunk)")
        && CheckCallable(SetListStatRva, LoaderThunkJump.data(), LoaderThunkJump.size(),
                         "SetWideListStat (loader thunk)");
}

// ---------------------------------------------------------------------------
//  Left timers the plugin armed: (game, unit, skill, cast id). The type 8 event
//  carries the cast id in arg7 and every re-arm keeps it, so a timer the game
//  made on its own (a monster, a used skill) never matches.
// ---------------------------------------------------------------------------
struct ArmedLeftTimer {
    void*        game;
    void*        unit;
    std::int32_t skillId;
    std::int32_t castId;
};

constexpr std::size_t ArmedLeftTimerCapacity = 1024;
std::array<ArmedLeftTimer, ArmedLeftTimerCapacity> ArmedLeftTimers{};
std::size_t ArmedLeftTimerCursor{};
SRWLOCK     ArmedLeftTimerLock = SRWLOCK_INIT;

// Same (unit, skill) entry first, then a free entry; only a full table falls
// back to the oldest written entry.
void RememberLeftTimer(void* game, void* unit, std::int32_t skillId, std::int32_t castId) noexcept {
    AcquireSRWLockExclusive(&ArmedLeftTimerLock);
    ArmedLeftTimer* freeEntry = nullptr;
    for (auto& entry : ArmedLeftTimers) {
        if (entry.unit == unit && entry.skillId == skillId) {
            entry.game   = game;
            entry.castId = castId;
            ReleaseSRWLockExclusive(&ArmedLeftTimerLock);
            return;
        }
        if (freeEntry == nullptr && entry.unit == nullptr) {
            freeEntry = &entry;
        }
    }
    if (freeEntry == nullptr) {
        freeEntry            = &ArmedLeftTimers[ArmedLeftTimerCursor];
        ArmedLeftTimerCursor = (ArmedLeftTimerCursor + 1) % ArmedLeftTimerCapacity;
        LogWarn("LeftClickAuras: the left timer table is full, the oldest entry was reused.");
    }
    *freeEntry = ArmedLeftTimer{game, unit, skillId, castId};
    ReleaseSRWLockExclusive(&ArmedLeftTimerLock);
}

auto IsLeftTimer(void* game, void* unit, std::int32_t skillId, std::int32_t castId) noexcept -> bool {
    AcquireSRWLockShared(&ArmedLeftTimerLock);
    bool found = false;
    for (const auto& entry : ArmedLeftTimers) {
        if (entry.unit == unit && entry.game == game && entry.skillId == skillId && entry.castId == castId) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&ArmedLeftTimerLock);
    return found;
}

void ForgetLeftTimer(void* game, void* unit, std::int32_t skillId) noexcept {
    AcquireSRWLockExclusive(&ArmedLeftTimerLock);
    for (auto& entry : ArmedLeftTimers) {
        if (entry.unit == unit && entry.game == game && entry.skillId == skillId) {
            entry = ArmedLeftTimer{};
        }
    }
    ReleaseSRWLockExclusive(&ArmedLeftTimerLock);
}

// The AuraStart call the assignment hook is making on this thread, if any.
struct PendingLeftStart {
    void*        unit;
    std::int32_t skillId;
    bool         armed;
};
thread_local PendingLeftStart* PendingStart = nullptr;

// ---------------------------------------------------------------------------
//  Game data helpers
// ---------------------------------------------------------------------------
auto AuraFlagMask() noexcept -> std::uint8_t {
    return ReadU8(reinterpret_cast<const void*>(Base), static_cast<std::size_t>(AuraFlagMaskRva));
}

auto LeftSkillFlagMask() noexcept -> std::uint8_t {
    return ReadU8(reinterpret_cast<const void*>(Base), static_cast<std::size_t>(LeftSkillFlagMaskRva));
}

auto DataContextOf(void* game) noexcept -> std::uint32_t {
    return ReadU8(game, GameDataContextOffset);
}

auto UnitIdOf(void* unit) noexcept -> std::uint32_t {
    return unit != nullptr ? ReadU32(unit, UnitIdOffset) : 0xFFFFFFFFU;
}

// Skill id of a skill node, -1 for none (word [[node]], as 0x3402D0 reads it).
auto NodeSkillId(void* node) noexcept -> std::int32_t {
    if (node == nullptr) {
        return -1;
    }
    const auto* record = static_cast<const std::uint8_t*>(ReadPointer(node, SkillNodeRecordOffset));
    if (record == nullptr) {
        return -1;
    }
    return ReadI16(record, SkillRecordIdOffset);
}


auto NodeOwnerGuid(void* node) noexcept -> std::int32_t {
    return node != nullptr ? static_cast<std::int32_t>(ReadU32(node, SkillNodeOwnerGuidOffset)) : -1;
}

// The node's skill is an aura (aura flag on its skills.txt record).
auto NodeIsAura(void* node) noexcept -> bool {
    if (node == nullptr) {
        return false;
    }
    const auto* record = static_cast<const std::uint8_t*>(ReadPointer(node, SkillNodeRecordOffset));
    return record != nullptr && (record[SkillRecordFlagsByteOffset] & AuraFlagMask()) != 0;
}

// The skills.txt record when the skill is an aura AuraStart accepts: aura flag
// and an aurastate inside the state table. Null otherwise.
auto AuraRecordFor(std::uint32_t dataContext, std::int32_t skillId) noexcept -> const std::uint8_t* {
    if (skillId <= 0) {
        return nullptr;
    }
    const auto* record = GetSkillsTxtRecord(dataContext, skillId);
    if (record == nullptr || (record[SkillRecordFlagsByteOffset] & AuraFlagMask()) == 0) {
        return nullptr;
    }
    const auto state = static_cast<std::int32_t>(ReadI16(record, SkillRecordAuraStateOffset));
    if (state < 0 || state >= GetStateCount(dataContext)) {
        return nullptr;
    }
    return record;
}

auto AuraStateOf(const std::uint8_t* record) noexcept -> std::int32_t {
    return ReadI16(record, SkillRecordAuraStateOffset);
}


// A type 8 event with this custom id that is not already being deleted.
auto HasLiveTimer(void* unit, std::uint32_t customId) noexcept -> bool {
    if (unit == nullptr) {
        return false;
    }
    void* node = ReadPointer(unit, UnitEventListOffset);
    for (std::size_t walked = 0; node != nullptr && walked < MaximumUnitEvents; ++walked) {
        if (ReadU8(node, EventTypeOffset) == PeriodicSkillEventType
            && ReadU32(node, EventCustomIdOffset) == customId
            && (ReadU16(node, EventFlagsOffset) & EventDeleteDeferredFlag) == 0) {
            return true;
        }
        node = ReadPointer(node, EventUnitNextOffset);
    }
    return false;
}

// A live (8, skill) event that is this plugin's left timer (cast id in arg7).
auto HasLiveLeftTimer(void* game, void* unit, std::int32_t skillId) noexcept -> bool {
    if (unit == nullptr) {
        return false;
    }
    void* node = ReadPointer(unit, UnitEventListOffset);
    for (std::size_t walked = 0; node != nullptr && walked < MaximumUnitEvents; ++walked) {
        if (ReadU8(node, EventTypeOffset) == PeriodicSkillEventType
            && ReadU32(node, EventCustomIdOffset) == static_cast<std::uint32_t>(skillId)
            && (ReadU16(node, EventFlagsOffset) & EventDeleteDeferredFlag) == 0
            && IsLeftTimer(game, unit, skillId, static_cast<std::int32_t>(ReadU32(node, EventArg7Offset)))) {
            return true;
        }
        node = ReadPointer(node, EventUnitNextOffset);
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Auras kept through an item operation (item_refresh_keeps_auras)
//
//  While unit flag 80000000h is set the game is attaching or detaching an item
//  inside the equipment re-check 0x470CA0 (0x470E77, 0x470F79). A skill an
//  item grants has no level for that instant, and the oskill stat case puts
//  Attack on its button through AssignSkill (0x4702C9 left, 0x4702E9 right);
//  the re-check puts the saved selection back afterwards (0x47109E). An aura
//  running on a button when that happens keeps running and is remembered
//  here, per button and unit. It is settled when its button gets it back, at
//  the next selection change outside an item operation, or when the re-check
//  ends (0x4711C3), the way the game settles item auras (0x4711C8).
// ---------------------------------------------------------------------------
enum class Button : std::size_t {
    Left  = 0,
    Right = 1,
};

struct KeptAura {
    void*         game;
    void*         unit;
    std::uint32_t unitId;
    std::int32_t  skillId;
    std::int32_t  state;
};

constexpr std::size_t KeptAuraCapacity = 128;
constexpr std::size_t KeptAuraBatch    = 16;  // kept auras of one unit and button handled in one pass
std::array<std::array<KeptAura, KeptAuraCapacity>, 2> KeptAuras{};
SRWLOCK           KeptAuraLock = SRWLOCK_INIT;
std::atomic<bool> KeptAuraTableFullLogged{false};
using KeptAuraList = std::array<KeptAura, KeptAuraBatch>;

auto KeptTable(Button button) noexcept -> std::array<KeptAura, KeptAuraCapacity>& {
    return KeptAuras[static_cast<std::size_t>(button)];
}

auto ButtonName(Button button) noexcept -> const char* {
    return button == Button::Left ? "left" : "right";
}

auto KeptFor(const KeptAura& entry, void* game, void* unit, std::uint32_t unitId) noexcept -> bool {
    return entry.unit == unit && entry.game == game && entry.unitId == unitId;
}

// Remembers an aura kept running on this button. False when the table is
// full; the caller then lets the game switch the aura off as it always did.
auto KeepAura(Button button, void* game, void* unit, std::int32_t skillId, std::int32_t state) noexcept -> bool {
    const auto unitId = UnitIdOf(unit);
    AcquireSRWLockExclusive(&KeptAuraLock);
    KeptAura* freeEntry = nullptr;
    bool      known     = false;
    for (auto& entry : KeptTable(button)) {
        if (entry.unit != nullptr && KeptFor(entry, game, unit, unitId) && entry.skillId == skillId) {
            known = true;
            break;
        }
        if (freeEntry == nullptr && entry.unit == nullptr) {
            freeEntry = &entry;
        }
    }
    if (!known && freeEntry != nullptr) {
        *freeEntry = KeptAura{game, unit, unitId, skillId, state};
    }
    ReleaseSRWLockExclusive(&KeptAuraLock);
    if (!known && freeEntry == nullptr) {
        if (!KeptAuraTableFullLogged.exchange(true, std::memory_order_relaxed)) {
            LogWarn("LeftClickAuras: the kept aura table is full; an item operation switches an aura off as in the "
                    "unmodded game.");
        }
        return false;
    }
    return true;
}

// A kept aura of this unit and button with this skill.
auto IsKeptAura(Button button, void* game, void* unit, std::int32_t skillId) noexcept -> bool {
    const auto unitId = UnitIdOf(unit);
    AcquireSRWLockShared(&KeptAuraLock);
    bool found = false;
    for (const auto& entry : KeptTable(button)) {
        if (entry.unit != nullptr && KeptFor(entry, game, unit, unitId) && entry.skillId == skillId) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&KeptAuraLock);
    return found;
}

// A kept aura of this unit and button whose aurastate is this state, copied out.
auto FindKeptState(Button button, void* game, void* unit, std::int32_t state, KeptAura& found) noexcept -> bool {
    const auto unitId = UnitIdOf(unit);
    AcquireSRWLockShared(&KeptAuraLock);
    bool any = false;
    for (const auto& entry : KeptTable(button)) {
        if (entry.unit != nullptr && KeptFor(entry, game, unit, unitId) && entry.state == state) {
            found = entry;
            any   = true;
            break;
        }
    }
    ReleaseSRWLockShared(&KeptAuraLock);
    return any;
}

// Takes the kept auras of this unit and button out of the table.
auto TakeKeptAuras(Button button, void* game, void* unit, KeptAuraList& taken) noexcept -> std::size_t {
    const auto unitId = UnitIdOf(unit);
    std::size_t count = 0;
    AcquireSRWLockExclusive(&KeptAuraLock);
    for (auto& entry : KeptTable(button)) {
        if (count < taken.size() && entry.unit != nullptr && KeptFor(entry, game, unit, unitId)) {
            taken[count++] = entry;
            entry          = KeptAura{};
        }
    }
    ReleaseSRWLockExclusive(&KeptAuraLock);
    return count;
}

void ForgetKeptAura(Button button, void* game, void* unit, std::int32_t skillId) noexcept {
    const auto unitId = UnitIdOf(unit);
    AcquireSRWLockExclusive(&KeptAuraLock);
    for (auto& entry : KeptTable(button)) {
        if (entry.unit != nullptr && KeptFor(entry, game, unit, unitId) && entry.skillId == skillId) {
            entry = KeptAura{};
        }
    }
    ReleaseSRWLockExclusive(&KeptAuraLock);
}

// The game is attaching or detaching an item of this unit inside an item
// operation (unit flag 80000000h, tested the way the game tests it, 0x34FB00).
auto InItemOperation(void* unit) noexcept -> bool {
    return ItemRefreshActive.load(std::memory_order_relaxed) && unit != nullptr
        && UnitFlagTest(unit, UnitItemOperationFlag) != 0;
}

// This plugin's left timer for the skill runs and the aura's state is on.
auto LeftAuraRunning(void* game, void* unit, std::int32_t skillId, const std::uint8_t* record) noexcept -> bool {
    return HasLiveLeftTimer(game, unit, skillId) && CheckState(unit, AuraStateOf(record)) != 0;
}

// The right button's timer runs and the aura's state is on.
auto RightAuraRunning(void* unit, const std::uint8_t* record) noexcept -> bool {
    return HasLiveTimer(unit, RightTimerCustomId) && CheckState(unit, AuraStateOf(record)) != 0;
}

// The left button holds a running aura that uses this state, or a left aura
// kept through an item operation does.
auto LeftAuraUsesState(void* game, void* unit, std::int32_t state) noexcept -> bool {
    const auto skillId = NodeSkillId(GetLeftSkill(unit));
    const auto* record = AuraRecordFor(DataContextOf(game), skillId);
    if (record != nullptr && AuraStateOf(record) == state && HasLiveTimer(unit, static_cast<std::uint32_t>(skillId))) {
        return true;
    }
    KeptAura kept{};
    return FindKeptState(Button::Left, game, unit, state, kept) && HasLiveLeftTimer(game, unit, kept.skillId);
}

// The right button holds a running aura that uses this state, or a right aura
// kept through an item operation does.
auto RightAuraUsesState(void* game, void* unit, std::int32_t state) noexcept -> bool {
    if (!HasLiveTimer(unit, RightTimerCustomId)) {
        return false;
    }
    const auto skillId = NodeSkillId(GetRightSkill(unit));
    const auto* record = AuraRecordFor(DataContextOf(game), skillId);
    if (record != nullptr && AuraStateOf(record) == state) {
        return true;
    }
    KeptAura kept{};
    return FindKeptState(Button::Right, game, unit, state, kept);
}

// The right button's removal order in 0x438A70: stat list, then state.
void RemoveAuraState(void* game, void* unit, const std::uint8_t* record) noexcept {
    const auto state = AuraStateOf(record);
    if (auto* statList = GetStateStatList(unit, state); statList != nullptr) {
        UnlinkStatList(unit, statList);
        FreeStatList(DataContextOf(game), statList);
    }
    ToggleState(unit, state, 0);
}

// ---------------------------------------------------------------------------
//  Starting and stopping the left aura
// ---------------------------------------------------------------------------
void StartLeftAura(void* game, void* unit, std::int32_t skillId) noexcept {
    PendingLeftStart pending{unit, skillId, false};
    PendingStart = &pending;
    AuraStart(game, unit, skillId);
    PendingStart = nullptr;

    if (pending.armed) {
        LeftStarts.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: started skill %d on unit %u.", skillId, UnitIdOf(unit));
        }
    } else {
        LeftStartsRefused.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: the game did not start skill %d on unit %u (no skill level).", skillId,
                    UnitIdOf(unit));
        }
    }
}

enum class LeftStop {
    Reselect,   // another skill went on the left button through AssignSkill
    OffButton,  // the timer found another skill on the left button
    Invalid,    // the timer found a dead unit or a skill that is not an aura
    ItemsGone,  // kept through an item operation, not given back to the button
};

void StopLeftTimer(void* game, void* unit, std::int32_t skillId, LeftStop reason) noexcept {
    ForgetLeftTimer(game, unit, skillId);
    ForgetKeptAura(Button::Left, game, unit, skillId);
    if (const auto* record = AuraRecordFor(DataContextOf(game), skillId); record != nullptr) {
        if (RightAuraUsesState(game, unit, AuraStateOf(record))) {
            SharedStatesKept.fetch_add(1, std::memory_order_relaxed);
        } else {
            RemoveAuraState(game, unit, record);
        }
    }
    DeleteEventsByType(game, unit, PeriodicSkillEventType, static_cast<std::uint32_t>(skillId));

    const char* why = "the left button changed";
    switch (reason) {
    case LeftStop::Reselect:
        LeftStopsReselect.fetch_add(1, std::memory_order_relaxed);
        break;
    case LeftStop::OffButton:
        LeftStopsOffButton.fetch_add(1, std::memory_order_relaxed);
        why = "it is no longer on the left button";
        break;
    case LeftStop::Invalid:
        LeftStopsInvalid.fetch_add(1, std::memory_order_relaxed);
        why = "the unit is dead or the skill is not an aura";
        break;
    case LeftStop::ItemsGone:
        LeftStopsItemsGone.fetch_add(1, std::memory_order_relaxed);
        why = "an item operation took it off the left button and did not give it back";
        break;
    }
    if (TakeDiagnosticsLine()) {
        LogInfo("LeftClickAuras: stopped skill %d on unit %u, %s.", skillId, UnitIdOf(unit), why);
    }
}

void LogKept(Button button, std::int32_t skillId, void* unit, std::int32_t otherSkillId) noexcept {
    if (TakeDiagnosticsLine()) {
        LogInfo("LeftClickAuras: skill %d on unit %u keeps running while an item operation puts skill %d on the %s "
                "button.",
                skillId, UnitIdOf(unit), otherSkillId, ButtonName(button));
    }
}

void LogKeptBack(Button button, std::int32_t skillId, void* unit) noexcept {
    if (TakeDiagnosticsLine()) {
        LogInfo("LeftClickAuras: skill %d on unit %u is back on the %s button after an item operation and never "
                "stopped.",
                skillId, UnitIdOf(unit), ButtonName(button));
    }
}

void UpdateLeftAura(void* unit, std::int32_t beforeSkillId, std::int32_t afterSkillId) noexcept {
    auto* game = GetGame(unit);
    if (game == nullptr) {
        return;
    }
    const auto dataContext = DataContextOf(game);
    const auto* beforeRecord = AuraRecordFor(dataContext, beforeSkillId);
    const auto* afterRecord  = AuraRecordFor(dataContext, afterSkillId);

    if (InItemOperation(unit)) {
        // What an item operation puts on the button can be undone before it
        // ends: a running left aura is kept, not stopped.
        if (beforeRecord != nullptr && afterSkillId != beforeSkillId) {
            if (LeftAuraRunning(game, unit, beforeSkillId, beforeRecord)
                && KeepAura(Button::Left, game, unit, beforeSkillId, AuraStateOf(beforeRecord))) {
                AurasKeptByItemOperations.fetch_add(1, std::memory_order_relaxed);
                LogKept(Button::Left, beforeSkillId, unit, afterSkillId);
            } else {
                StopLeftTimer(game, unit, beforeSkillId, LeftStop::Reselect);
            }
        }
        if (afterRecord != nullptr && !LeftAuraRunning(game, unit, afterSkillId, afterRecord)) {
            StartLeftAura(game, unit, afterSkillId);
        }
        return;
    }

    // Left auras kept through an item operation: the one the button gets back
    // carries on, any other stops now.
    bool        keptBack = false;
    KeptAuraList kept{};
    const auto  keptCount = TakeKeptAuras(Button::Left, game, unit, kept);
    for (std::size_t index = 0; index < keptCount; ++index) {
        if (kept[index].skillId == afterSkillId) {
            keptBack = true;
        } else if (HasLiveLeftTimer(game, unit, kept[index].skillId)) {
            StopLeftTimer(game, unit, kept[index].skillId, LeftStop::ItemsGone);
        }
    }

    if (beforeRecord != nullptr && afterSkillId != beforeSkillId) {
        StopLeftTimer(game, unit, beforeSkillId, LeftStop::Reselect);
    }
    if (afterRecord == nullptr) {
        return;
    }
    // Its timer only survives a change of the left button when that change never
    // went through AssignSkill (a direct select by the game) or was an item
    // operation: either way the aura never stopped, so it carries on.
    const bool backFromOtherSkill =
        keptBack
        || (ItemRefreshActive.load(std::memory_order_relaxed) && beforeRecord == nullptr && afterSkillId != beforeSkillId);
    if ((afterSkillId == beforeSkillId || backFromOtherSkill) && LeftAuraRunning(game, unit, afterSkillId, afterRecord)) {
        if (backFromOtherSkill) {
            LeftKeptThroughItems.fetch_add(1, std::memory_order_relaxed);
            LogKeptBack(Button::Left, afterSkillId, unit);
        } else {
            LeftKeptRunning.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    StartLeftAura(game, unit, afterSkillId);
}

// ---------------------------------------------------------------------------
//  The right button through an item operation
// ---------------------------------------------------------------------------
// The right button's AssignSkill call in progress on this thread: what the
// relays after its select (0x438C02, 0x438C33, 0x438C52, 0x438C80) do.
struct RightAssignment {
    void* unit;
    bool  keepOldAura;  // the old right aura stays: no stat list, state or timer removal
    bool  keepNewAura;  // the new right aura already runs: no AuraStart
};
thread_local RightAssignment CurrentRight{};

auto KeepingOldRightAura(void* unit) noexcept -> bool {
    return unit != nullptr && CurrentRight.unit == unit && CurrentRight.keepOldAura;
}

// The stock right removal (0x438BFC..0x438C52) for a kept right aura that did
// not come back: stat list and state (kept when the left aura uses the state),
// then the right timer when nothing on the right button needs it.
void StopKeptRightAura(void* game, void* unit, const KeptAura& kept, bool deleteTimer) noexcept {
    if (LeftAuraUsesState(game, unit, kept.state)) {
        SharedStatesKept.fetch_add(1, std::memory_order_relaxed);
    } else {
        if (auto* statList = GetStateStatList(unit, kept.state); statList != nullptr) {
            UnlinkStatList(unit, statList);
            FreeStatList(DataContextOf(game), statList);
        }
        ToggleState(unit, kept.state, 0);
    }
    if (deleteTimer) {
        DeleteEventsByType(game, unit, PeriodicSkillEventType, RightTimerCustomId);
    }
    RightStopsItemsGone.fetch_add(1, std::memory_order_relaxed);
    if (TakeDiagnosticsLine()) {
        LogInfo("LeftClickAuras: stopped skill %d on unit %u, an item operation took it off the right button and did "
                "not give it back.",
                kept.skillId, UnitIdOf(unit));
    }
}

void UpdateRightAura(void* unit, std::int32_t beforeSkillId, std::int32_t afterSkillId) noexcept {
    auto* game = GetGame(unit);
    if (game == nullptr) {
        return;
    }
    const auto dataContext = DataContextOf(game);
    const auto* beforeRecord = AuraRecordFor(dataContext, beforeSkillId);
    const auto* afterRecord  = AuraRecordFor(dataContext, afterSkillId);
    CurrentRight.unit = unit;

    if (InItemOperation(unit)) {
        if (beforeRecord != nullptr && afterSkillId != beforeSkillId && RightAuraRunning(unit, beforeRecord)
            && KeepAura(Button::Right, game, unit, beforeSkillId, AuraStateOf(beforeRecord))) {
            CurrentRight.keepOldAura = true;
            AurasKeptByItemOperations.fetch_add(1, std::memory_order_relaxed);
            LogKept(Button::Right, beforeSkillId, unit, afterSkillId);
        }
        if (afterRecord != nullptr && afterSkillId != beforeSkillId
            && IsKeptAura(Button::Right, game, unit, afterSkillId) && RightAuraRunning(unit, afterRecord)) {
            CurrentRight.keepNewAura = true;
        }
        return;
    }

    // Right auras kept through an item operation: the one the button gets back
    // carries on without AuraStart, any other is removed as the stock removal
    // would have removed it.
    KeptAuraList kept{};
    const auto   keptCount = TakeKeptAuras(Button::Right, game, unit, kept);
    if (keptCount == 0) {
        // The right timer only runs on while the right button holds no aura when
        // the game changed the button without AssignSkill (a direct select) and
        // no tick has passed since: the aura it gets back never stopped.
        if (beforeRecord == nullptr && afterRecord != nullptr && afterSkillId != beforeSkillId
            && RightAuraRunning(unit, afterRecord)) {
            CurrentRight.keepNewAura = true;
            RightKeptThroughItems.fetch_add(1, std::memory_order_relaxed);
            LogKeptBack(Button::Right, afterSkillId, unit);
        }
        return;
    }
    for (std::size_t index = 0; index < keptCount; ++index) {
        if (kept[index].skillId == afterSkillId && afterRecord != nullptr && RightAuraRunning(unit, afterRecord)) {
            CurrentRight.keepNewAura = true;
        }
    }
    if (CurrentRight.keepNewAura) {
        RightKeptThroughItems.fetch_add(1, std::memory_order_relaxed);
        LogKeptBack(Button::Right, afterSkillId, unit);
    }
    for (std::size_t index = 0; index < keptCount; ++index) {
        if (CurrentRight.keepNewAura && kept[index].skillId == afterSkillId) {
            continue;
        }
        StopKeptRightAura(game, unit, kept[index], !CurrentRight.keepNewAura);
    }
}

// The equipment re-check is about to count the pet limits: every aura it kept
// on a button and did not give back stops now, as the game's own item auras do
// right after (0x4711C8).
void FinishItemRefresh(void* game, void* unit) noexcept {
    const auto dataContext = DataContextOf(game);

    KeptAuraList kept{};
    const auto   leftSkillId = NodeSkillId(GetLeftSkill(unit));
    auto         keptCount   = TakeKeptAuras(Button::Left, game, unit, kept);
    for (std::size_t index = 0; index < keptCount; ++index) {
        const auto* record = AuraRecordFor(dataContext, kept[index].skillId);
        if (record != nullptr && kept[index].skillId == leftSkillId) {
            if (LeftAuraRunning(game, unit, kept[index].skillId, record)) {
                LeftKeptThroughItems.fetch_add(1, std::memory_order_relaxed);
                LogKeptBack(Button::Left, kept[index].skillId, unit);
            } else {
                StartLeftAura(game, unit, kept[index].skillId);
            }
        } else if (HasLiveLeftTimer(game, unit, kept[index].skillId)) {
            StopLeftTimer(game, unit, kept[index].skillId, LeftStop::ItemsGone);
        }
    }

    const auto  rightSkillId = NodeSkillId(GetRightSkill(unit));
    const auto* rightRecord  = AuraRecordFor(dataContext, rightSkillId);
    keptCount                = TakeKeptAuras(Button::Right, game, unit, kept);
    for (std::size_t index = 0; index < keptCount; ++index) {
        if (rightRecord != nullptr && kept[index].skillId == rightSkillId) {
            RightKeptThroughItems.fetch_add(1, std::memory_order_relaxed);
            LogKeptBack(Button::Right, kept[index].skillId, unit);
            continue;
        }
        // An aura on the right button now runs on the right timer: keep it.
        StopKeptRightAura(game, unit, kept[index], rightRecord == nullptr);
    }
}

// One tick of a left timer: the id -1 branch of 0x437460 (0x437499..0x4375A0
// and its tail 0x4377D6..0x4377F1), run against the LEFT button.
void RunLeftTimer(void* game, void* unit, std::int32_t skillId, std::int32_t armedLevel, std::int32_t castId) noexcept {
    void* node = GetLeftSkill(unit);
    if (node == nullptr || NodeSkillId(node) != skillId) {
        StopLeftTimer(game, unit, skillId, LeftStop::OffButton);
        return;
    }
    const auto* record = GetSkillsTxtRecord(DataContextOf(game), skillId);
    if (record == nullptr || (record[SkillRecordFlagsByteOffset] & AuraFlagMask()) == 0 || IsUnitDead(unit) != 0) {
        StopLeftTimer(game, unit, skillId, LeftStop::Invalid);
        return;
    }
    LeftTicks.fetch_add(1, std::memory_order_relaxed);

    std::int32_t level = armedLevel;
    if (LiveLevelActive.load(std::memory_order_relaxed)) {
        const auto live = ReadSkillLevel(unit, node, 1, 0);
        if (live > 0) {
            LiveLevelTicks.fetch_add(1, std::memory_order_relaxed);
            if (live != armedLevel) {
                LiveLevelChanges.fetch_add(1, std::memory_order_relaxed);
                if (TakeDiagnosticsLine()) {
                    LogInfo("LeftClickAuras: skill %d on unit %u runs at level %d, the previous tick ran at %d.",
                            skillId, UnitIdOf(unit), live, armedLevel);
                }
            }
            level = live;
        }
    }

    const auto savedCastId = GetUnitCastId(unit);
    SetUnitCastId(unit, static_cast<std::uint32_t>(castId));
    SetUnitFlag(unit, UnitPeriodicPerformFlag, 1);
    if (SingleEmanationActive.load(std::memory_order_relaxed) && NodeSkillId(GetRightSkill(unit)) == skillId
        && HasLiveTimer(unit, RightTimerCustomId)) {
        EmanationsSkipped.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: skill %d on unit %u emanates from the right button this time.", skillId,
                    UnitIdOf(unit));
        }
    } else {
        ServerDoSkill(game, unit, skillId, level, 1, 0, 0);
    }
    SetUnitFlag(unit, UnitPeriodicPerformFlag, 0);
    ArmPeriodicSkill(game, unit, skillId, level, castId, 0);
    SetUnitCastId(unit, savedCastId);
}

// ---------------------------------------------------------------------------
//  Controller: which side an aura button uses
// ---------------------------------------------------------------------------
// Side that received the most recent new aura, per controller player slot.
std::array<std::uint8_t, ControllerPlayers> LastAuraSide{};

auto BindingEntry(std::uint32_t button) noexcept -> std::uint8_t* {
    if (button > ControllerHighestButton) {
        return nullptr;
    }
    const auto bank = BindingBank();
    if (bank < 0) {
        return nullptr;
    }
    const auto index = static_cast<std::uintptr_t>(button) + static_cast<std::uintptr_t>(bank);
    return reinterpret_cast<std::uint8_t*>(Base + static_cast<std::uintptr_t>(ControllerBindingTableRva))
         + index * BindingEntryBytes;
}

auto NodeHolds(void* node, std::int32_t skillId, std::int32_t ownerGuid) noexcept -> bool {
    return node != nullptr && NodeSkillId(node) == skillId && NodeOwnerGuid(node) == ownerGuid;
}

void ChooseAuraSide(std::uint32_t playerIndex, std::uint32_t button) noexcept {
    auto* entry = BindingEntry(button);
    if (entry == nullptr) {
        return;
    }
    const auto skillId   = static_cast<std::int32_t>(ReadU32(entry, BindingSkillOffset));
    const auto ownerGuid = static_cast<std::int32_t>(ReadU32(entry, BindingOwnerGuidOffset));
    if (skillId <= 0) {
        return;
    }
    void* player = GetClientPlayer(playerIndex);
    if (player == nullptr) {
        return;
    }
    const auto* record = GetSkillsTxtRecord(GetUnitDataContext(player), skillId);
    if (record == nullptr || (record[SkillRecordFlagsByteOffset] & AuraFlagMask()) == 0) {
        return;
    }
    ControllerAuraPresses.fetch_add(1, std::memory_order_relaxed);

    void* left  = GetLeftSkill(player);
    void* right = GetRightSkill(player);
    const bool leftAllowed = (record[SkillRecordLeftSkillByteOffset] & LeftSkillFlagMask()) != 0;

    std::uint8_t side = BindingSideRight;
    bool replaced = false;
    if (NodeHolds(right, skillId, ownerGuid)) {
        side = BindingSideRight;
    } else if (leftAllowed && NodeHolds(left, skillId, ownerGuid)) {
        side = BindingSideLeft;
    } else {
        if (!NodeIsAura(right)) {
            side = BindingSideRight;
        } else if (leftAllowed && !NodeIsAura(left)) {
            side = BindingSideLeft;
        } else {
            side     = (leftAllowed && LastAuraSide[playerIndex] == BindingSideRight) ? BindingSideLeft : BindingSideRight;
            replaced = true;
        }
        LastAuraSide[playerIndex] = side;
    }
    if (replaced) {
        ControllerAurasReplaced.fetch_add(1, std::memory_order_relaxed);
    }

    const auto previous = entry[BindingSideOffset];
    if (previous != side) {
        entry[BindingSideOffset] = side;
        ControllerSidesMoved.fetch_add(1, std::memory_order_relaxed);
    }
    if (TakeDiagnosticsLine()) {
        LogInfo("LeftClickAuras: controller button %u, aura %d on the %s side (binding was %s)%s.", button, skillId,
                side == BindingSideLeft ? "left" : "right", previous != 0 ? "left" : "right",
                replaced ? ", replacing the older aura" : "");
    }
}

// ---------------------------------------------------------------------------
//  Hooks and relay bridges
// ---------------------------------------------------------------------------
// 0x438AEF, AssignSkill's left branch selects the skill it found.
std::int64_t __fastcall BridgeLeftSelect(void* unit, std::int32_t skillId, std::int32_t ownerGuid) noexcept {
    if (!ServerActive.load(std::memory_order_relaxed) || unit == nullptr) {
        return SetLeftActiveSkill(unit, skillId, ownerGuid);
    }
    const auto beforeSkillId = NodeSkillId(GetLeftSkill(unit));
    const auto result        = SetLeftActiveSkill(unit, skillId, ownerGuid);
    UpdateLeftAura(unit, beforeSkillId, NodeSkillId(GetLeftSkill(unit)));
    return result;
}

// 0x43B8FD, AuraStart's arm.
std::int64_t __fastcall BridgeAuraStartArm(void* game, void* unit, std::int32_t skillId, std::int32_t level,
                                           std::int32_t castId, std::int32_t rightTimer) noexcept {
    auto* pending = PendingStart;
    if (pending != nullptr && pending->unit == unit && pending->skillId == skillId) {
        const auto result = ArmPeriodicSkill(game, unit, skillId, level, castId, 0);
        if (result != 0) {
            RememberLeftTimer(game, unit, skillId, castId);
            pending->armed = true;
        }
        return result;
    }
    return ArmPeriodicSkill(game, unit, skillId, level, castId, rightTimer);
}

// 0x438B09, AssignSkill's right branch selects the skill it found. The old
// right node is already in r12 for the removal that follows; rax is unused.
std::int64_t __fastcall BridgeRightSelect(void* unit, std::int32_t skillId, std::int32_t ownerGuid) noexcept {
    CurrentRight = RightAssignment{};
    if (!ServerActive.load(std::memory_order_relaxed) || !ItemRefreshActive.load(std::memory_order_relaxed)
        || unit == nullptr) {
        return SetRightActiveSkill(unit, skillId, ownerGuid);
    }
    const auto beforeSkillId = NodeSkillId(GetRightSkill(unit));
    const auto result        = SetRightActiveSkill(unit, skillId, ownerGuid);
    UpdateRightAura(unit, beforeSkillId, NodeSkillId(GetRightSkill(unit)));
    return result;
}

// 0x438C02, the right button's removal asks for the old aura's state stat list.
void* __fastcall BridgeRightAuraStatList(void* unit, std::int32_t state) noexcept {
    if (KeepingOldRightAura(unit)) {
        return nullptr;
    }
    if (ServerActive.load(std::memory_order_relaxed) && unit != nullptr) {
        auto* game = GetGame(unit);
        if (game != nullptr && LeftAuraUsesState(game, unit, state)) {
            SharedStatesKept.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
    }
    return GetStateStatList(unit, state);
}

// 0x438C33, the right button's removal turns the old aura's state off.
void __fastcall BridgeRightAuraStateOff(void* unit, std::int32_t state, std::int32_t enable) noexcept {
    if (enable == 0 && KeepingOldRightAura(unit)) {
        return;
    }
    if (ServerActive.load(std::memory_order_relaxed) && unit != nullptr && enable == 0) {
        auto* game = GetGame(unit);
        if (game != nullptr && LeftAuraUsesState(game, unit, state)) {
            return;
        }
    }
    ToggleState(unit, state, enable);
}

// 0x438C52, the right button's removal deletes the right timer (8, -1); rax
// is unused afterwards.
std::uint64_t __fastcall BridgeRightAuraTimerDelete(void* game, void* unit, std::int32_t type,
                                                    std::uint32_t customId) noexcept {
    if (KeepingOldRightAura(unit)) {
        return 0;
    }
    return DeleteEventsByType(game, unit, type, customId);
}

// 0x438C80, the right button starts its new aura. Its eax is AssignSkill's
// return value, which no caller reads.
std::int64_t __fastcall BridgeRightAuraStart(void* game, void* unit, std::int32_t skillId) noexcept {
    if (unit != nullptr && CurrentRight.unit == unit && CurrentRight.keepNewAura) {
        CurrentRight.keepNewAura = false;
        return 1;
    }
    return AuraStart(game, unit, skillId);
}

// 0x4711C3, the equipment re-check counts the pet limits (0x5005A0).
std::int64_t __fastcall BridgeItemRefreshFinish(void* game, void* unit) noexcept {
    if (ServerActive.load(std::memory_order_relaxed) && ItemRefreshActive.load(std::memory_order_relaxed)
        && unit != nullptr) {
        auto* unitGame = GetGame(unit);
        FinishItemRefresh(unitGame != nullptr ? unitGame : game, unit);
    }
    return UpdatePetLimits(game, unit);
}

// 0x437460
void __fastcall HookPeriodicSkillEvent(void* game, void* unit, std::int32_t customId, std::int32_t customParam,
                                       std::int32_t castId) noexcept {
    if (ServerActive.load(std::memory_order_relaxed) && customId > 0 && game != nullptr && unit != nullptr
        && GetUnitType(unit) == UnitTypePlayer && IsLeftTimer(game, unit, customId, castId)) {
        RunLeftTimer(game, unit, customId, customParam, castId);
        return;
    }
    OriginalPeriodicSkillEvent(game, unit, customId, customParam, castId);
}

// 0x102753, client world click: may this click enter the skill branch.
std::uint64_t __fastcall BridgeClickGate(void* player, void* node, const std::uint8_t* command) noexcept {
    if (ClientActive.load(std::memory_order_relaxed) && player != nullptr && node != nullptr && command != nullptr
        && (ReadU32(command, ClickCommandFlagsOffset) & ClickCommandLeftButton) != 0
        && node == GetLeftSkill(player)) {
        const auto* record = static_cast<const std::uint8_t*>(ReadPointer(node, SkillNodeRecordOffset));
        if (record != nullptr && (record[SkillRecordFlagsByteOffset] & AuraFlagMask()) != 0) {
            MoveOnlyClicks.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }
    return SkillNodeGate(player, node);
}

// 0x12BBDF, the client checks a confirmed toggle skill against the bindings.
// The stub has already put the confirmed side (0 right, 1 left) into edx.
bool __fastcall BridgeControllerSync(void* manager, std::uint32_t side, std::uint32_t skillId) noexcept {
    const bool bound = ToggleBindingCheck(manager, static_cast<std::uint8_t>(side), skillId);
    if (side != BindingSideRight) {
        (bound ? ControllerLeftConfirmsKept : ControllerLeftConfirmsReset).fetch_add(1, std::memory_order_relaxed);
        if (!bound && TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: controller, toggle skill %u is not bound on the left side, the game resets it.",
                    skillId);
        }
    }
    return bound;
}

// 0x144B00, the controller starts a button press.
bool __fastcall BridgeControllerPress(void* manager, std::uint32_t player, std::uint32_t button) noexcept {
    if (ControllerTwoAurasActive.load(std::memory_order_relaxed) && player < ControllerPlayers && IsControllerMode()) {
        ChooseAuraSide(player, button);
    }
    return PressStart(manager, player, button);
}

// ---------------------------------------------------------------------------
//  Aura level refresh: the same aura coming back higher keeps its state
// ---------------------------------------------------------------------------
auto ReadU64(const void* base, std::size_t offset) noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

// What PrepareRaise changed before the applier ran, to finish or undo after it.
struct RaisedList {
    void*         list;
    std::int32_t  oldLevel;
    std::int32_t  ownerType;
    std::uint32_t ownerId;
    bool          sameCaster;
    std::uint32_t dataContext;
};

// The remove callback a rebuild would have given the list (0x4341A3).
auto RemoveCallbackFor(const StateEffectRequest* request) noexcept -> void* {
    return request->removeCallback != nullptr ? request->removeCallback : DefaultRemoveCallback;
}

// Removes the passive stats the target's own application of this skill wrote
// on the list (srvdofunc 65 writes them on the caster's own list only).
auto RemovePassiveStats(std::uint32_t dataContext, void* list, void* target, std::int32_t state,
                        std::int32_t skillId) noexcept -> std::uint32_t {
    const auto* record = GetSkillsTxtRecord(dataContext, skillId);
    const auto* tables = DataTables(dataContext);
    if (record == nullptr || tables == nullptr) {
        return 0;
    }
    const auto statCount = ReadU64(tables, ItemStatCostCountOffset);
    std::uint32_t removed = 0;
    for (std::size_t slot = 0; slot < SkillRecordPassiveStatSlots; ++slot) {
        const std::int32_t stat = ReadI16(record, SkillRecordPassiveStatsOffset + slot * sizeof(std::int16_t));
        if (stat <= 0 || static_cast<std::uint64_t>(stat) >= statCount) {
            continue;
        }
        if (ReadListStat(dataContext, list, stat, 0) != 0) {
            MarkStateChanged(target, state, 1);  // what the callback's zeroing block does, 0x5619B4
            SetListStat(dataContext, list, stat, 0, 0);
            ++removed;
        }
    }
    return removed;
}

// Before the applier: find the list the applier will use and, when it holds
// the same skill at a lower level, raise its level to the request's.
void PrepareRaise(StateEffectRequest* request, RaisedList& raised) noexcept {
    void* caster = request->caster;
    void* target = request->target;
    const std::int32_t state = request->state;
    const std::int32_t level = request->skillLevel;
    if (caster == nullptr || target == nullptr || state <= 0 || level <= 0) {
        return;
    }

    // The applier's own lookup (0x433D60, 0x433F28, 0x433FE9 / 0x433FF6).
    const std::uint32_t dataContext = GetUnitDataContext(caster);
    if (state >= GetStateCount(dataContext)) {
        return;
    }
    void* list = IsCurseState(dataContext, state) != 0 ? StatListByFlag(target, CurseStatListFlag)
                                                       : GetStateStatList(target, state);
    if (list == nullptr || StatListState(list) != state) {
        return;
    }

    const std::int32_t listSkill = StatListSkill(list);
    const std::int32_t listLevel = StatListLevel(list);
    if (listSkill != request->skillId) {
        OtherSkillsReplaced.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: skill %d replaced skill %d on state %d of unit %u (two auras share this state; "
                    "the game rebuilds it, unchanged).",
                    request->skillId, listSkill, state, UnitIdOf(target));
        }
        return;
    }
    if (level == listLevel) {
        return;
    }
    if (level < listLevel) {
        LowerLevelsRefused.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: skill %d at level %d from unit %u was refused on unit %u, the state holds level "
                    "%d (unchanged).",
                    request->skillId, level, UnitIdOf(caster), UnitIdOf(target), listLevel);
        }
        return;
    }

    const std::int32_t  ownerType  = StatListOwnerType(list);
    const std::uint32_t ownerId    = static_cast<std::uint32_t>(StatListOwnerId(list));
    const bool          sameCaster = ownerType == GetUnitType(caster) && ownerId == GetUnitId(caster);
    const bool          allowed    = sameCaster ? LevelUpInPlaceActive.load(std::memory_order_relaxed)
                                                : TakeoverInPlaceActive.load(std::memory_order_relaxed);
    if (!allowed) {
        return;
    }

    SetStatListLevel(list, level);
    raised = RaisedList{list, listLevel, ownerType, ownerId, sameCaster, dataContext};
}

// After the applier.
void FinishRaise(StateEffectRequest* request, const RaisedList& raised, void* result) noexcept {
    void* list = raised.list;
    if (result == nullptr) {
        // Every path of the applier that returns 0 does so before it touches
        // the list (0x433D74, 0x433DBE..0x433F16, 0x433F48, 0x433FD0).
        SetStatListLevel(list, raised.oldLevel);
        GameRefusalsRestored.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (result != list) {
        // The applier rebuilt the list after all; the old one is freed and is
        // never touched again.
        RebuiltAnyway.fetch_add(1, std::memory_order_relaxed);
        if (RebuildWarnings.fetch_add(1, std::memory_order_relaxed) < 4) {
            LogWarn("LeftClickAuras: the state applier rebuilt state %d for skill %d although its level was raised "
                    "first. Please report this; the aura still blinks.",
                    request->state, request->skillId);
        }
        return;
    }

    void* caster = request->caster;
    void* target = request->target;
    const std::int32_t state = request->state;
    if (raised.sameCaster) {
        if (ReadPointer(list, StatListRemoveCallbackOffset) == nullptr) {
            SetRemoveCallback(list, RemoveCallbackFor(request));
            RemoveCallbacksAdded.fetch_add(1, std::memory_order_relaxed);
        }
        LevelUpsKept.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: skill %d on unit %u went from level %d to %d, state %d kept.", request->skillId,
                    UnitIdOf(target), raised.oldLevel, request->skillLevel, state);
        }
    } else {
        const bool targetOwnedIt = raised.ownerType == GetUnitType(target) && raised.ownerId == GetUnitId(target);
        SetStatListOwner(list, GetUnitType(caster), GetUnitId(caster));
        ClearOtherCasterRecord(list);
        SetRemoveCallback(list, RemoveCallbackFor(request));
        std::uint32_t removed = 0;
        if (targetOwnedIt && caster != target) {
            removed = RemovePassiveStats(raised.dataContext, list, target, state, request->skillId);
            PassiveStatsRemoved.fetch_add(removed, std::memory_order_relaxed);
        }
        TakeoversKept.fetch_add(1, std::memory_order_relaxed);
        if (TakeDiagnosticsLine()) {
            LogInfo("LeftClickAuras: skill %d from unit %u took state %d of unit %u over from unit %u, level %d to "
                    "%d, state kept, %u passive stats removed.",
                    request->skillId, UnitIdOf(caster), state, UnitIdOf(target), raised.ownerId, raised.oldLevel,
                    request->skillLevel, removed);
        }
    }

    // The rebuild turns the state on (0x4340A7).
    if (CheckState(target, state) == 0) {
        ToggleState(target, state, 1);
        StatesTurnedOn.fetch_add(1, std::memory_order_relaxed);
    }
}

// The pointer behind the applier thunk 0x433D20 leads here.
void* __fastcall BridgeApplyStateEffect(StateEffectRequest* request) noexcept {
    const auto caller = reinterpret_cast<std::uintptr_t>(LEFT_CLICK_AURAS_RETURN_ADDRESS());
    if (caller != AuraCallbackReturn || request == nullptr || !LevelRefreshActive.load(std::memory_order_relaxed)) {
        return OriginalStateApplier(request);
    }
    AuraApplications.fetch_add(1, std::memory_order_relaxed);
    RaisedList raised{};
    PrepareRaise(request, raised);
    void* result = OriginalStateApplier(request);
    if (raised.list != nullptr) {
        FinishRaise(request, raised, result);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Installation
// ---------------------------------------------------------------------------
void ResolveNativeFunctions() noexcept {
    AuraStart          = At<AuraStartFn>(AuraStartRva);
    ArmPeriodicSkill   = At<ArmPeriodicSkillFn>(ArmPeriodicSkillRva);
    ServerDoSkill      = At<ServerDoSkillFn>(ServerDoSkillRva);
    SetLeftActiveSkill = At<SetLeftActiveSkillFn>(SetLeftActiveSkillRva);
    SetRightActiveSkill = At<SetLeftActiveSkillFn>(SetRightActiveSkillRva);
    UnitFlagTest        = At<UnitFlagTestFn>(UnitFlagTestRva);
    UpdatePetLimits     = At<UpdatePetLimitsFn>(UpdatePetLimitsRva);
    SkillNodeGate      = At<SkillNodeGateFn>(SkillNodeGateRva);
    GetLeftSkill       = At<GetSkillNodeFn>(GetLeftSkillRva);
    GetRightSkill      = At<GetSkillNodeFn>(GetRightSkillRva);
    GetGame            = At<GetGameFn>(GetGameRva);
    CheckState         = At<CheckStateFn>(CheckStateRva);
    DeleteEventsByType = At<DeleteEventsByTypeFn>(DeleteEventsByTypeRva);
    GetSkillsTxtRecord = At<GetSkillsTxtRecordFn>(GetSkillsTxtRecordRva);
    GetStateCount      = At<GetStateCountFn>(GetStateCountRva);
    ToggleState        = At<ToggleStateFn>(ToggleStateRva);
    GetStateStatList   = At<GetStateStatListFn>(GetStateStatListRva);
    UnlinkStatList     = At<UnlinkStatListFn>(UnlinkStatListRva);
    FreeStatList       = At<FreeStatListFn>(FreeStatListRva);
    GetUnitType        = At<GetUnitTypeFn>(GetUnitTypeRva);
    ReadSkillLevel     = At<ReadSkillLevelFn>(ReadSkillLevelRva);
    GetUnitCastId      = At<GetUnitCastIdFn>(GetUnitCastIdRva);
    SetUnitCastId      = At<SetUnitCastIdFn>(SetUnitCastIdRva);
    SetUnitFlag        = At<SetUnitFlagFn>(SetUnitFlagRva);
    IsUnitDead         = At<IsUnitDeadFn>(IsUnitDeadRva);
    GetUnitDataContext = At<GetUnitDataContextFn>(GetUnitDataContextRva);
    ToggleBindingCheck = At<ToggleBindingCheckFn>(ToggleBindingCheckRva);
    PressStart         = At<PressStartFn>(PressStartRva);
    IsControllerMode   = At<IsControllerModeFn>(IsControllerModeRva);
    BindingBank        = At<BindingBankFn>(BindingBankRva);
    GetClientPlayer    = At<GetClientPlayerFn>(GetClientPlayerRva);
    IsCurseState           = At<IsCurseStateFn>(IsCurseStateRva);
    StatListByFlag         = At<StatListByFlagFn>(StatListByFlagRva);
    StatListState          = At<StatListValueFn>(StatListStateRva);
    StatListSkill          = At<StatListValueFn>(StatListSkillRva);
    StatListLevel          = At<StatListValueFn>(StatListLevelRva);
    SetStatListLevel       = At<SetStatListLevelFn>(SetStatListLevelRva);
    StatListOwnerType      = At<StatListValueFn>(StatListOwnerTypeRva);
    StatListOwnerId        = At<StatListValueFn>(StatListOwnerIdRva);
    SetStatListOwner       = At<SetStatListOwnerFn>(SetStatListOwnerRva);
    ClearOtherCasterRecord = At<StatListActionFn>(ClearOtherCasterRecordRva);
    SetRemoveCallback      = At<SetRemoveCallbackFn>(SetRemoveCallbackRva);
    GetUnitId              = At<GetUnitIdFn>(GetUnitIdRva);
    MarkStateChanged       = At<MarkStateChangedFn>(MarkStateChangedRva);
    DataTables             = At<DataTablesFn>(DataTablesRva);
    ReadListStat           = At<ReadListStatFn>(ReadListStatRva);
    SetListStat            = At<SetListStatFn>(SetListStatRva);
    DefaultRemoveCallback  = At<void*>(DefaultRemoveCallbackRva);
    AuraCallbackReturn     = Base + static_cast<std::uintptr_t>(AuraCallbackReturnRva);
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

    // The page sits above the image, so the lowest call site is the farthest.
    const std::uintptr_t limit = Base + ClickGateCallRva + 0x7FF00000ULL;
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

    const std::array<std::uint64_t, RelayStubCount> bridges{
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeLeftSelect)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeRightAuraStatList)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeRightAuraStateOff)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeAuraStartArm)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeClickGate)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeControllerSync)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeControllerPress)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeRightSelect)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeRightAuraTimerDelete)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeRightAuraStart)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeItemRefreshFinish)),
    };

    for (std::size_t index = 0; index < RelayStubCount; ++index) {
        auto* stub = page + RelayStubOffsets[index];
        std::size_t at = 0;
        if (index == StubClickGate) {
            const std::uint8_t commandLoad[]{0x4D, 0x8D, 0x46, 0xD8};  // lea r8,[r14-28h]
            std::memcpy(stub, commandLoad, sizeof(commandLoad));
            at = sizeof(commandLoad);
        } else if (index == StubControllerSync) {
            const std::uint8_t confirmedSide[]{
                0x80, 0x7B, 0x06, 0x00,  // cmp byte [rbx+6],0
                0x0F, 0x95, 0xC2,        // setne dl
                0x0F, 0xB6, 0xD2,        // movzx edx,dl
            };
            std::memcpy(stub, confirmedSide, sizeof(confirmedSide));
            at = sizeof(confirmedSide);
        }
        stub[at]     = 0x48;  // mov rax,imm64
        stub[at + 1] = 0xB8;
        PutU64(stub + at + 2, bridges[index]);
        stub[at + 10] = 0xFF;  // jmp rax
        stub[at + 11] = 0xE0;

        auto* function = page + RelayFunctionTableOffset + index * 12;
        PutU32(function, static_cast<std::uint32_t>(RelayStubOffsets[index]));
        PutU32(function + 4, static_cast<std::uint32_t>(RelayStubOffsets[index] + RelayStubSizes[index]));
        PutU32(function + 8, static_cast<std::uint32_t>(RelayUnwindOffset));
    }
}

auto EnsureRelay() noexcept -> bool {
    if (RelayPage != nullptr) {
        return true;
    }
    auto* page = AllocateRelayPage();
    if (page == nullptr) {
        LogError("LeftClickAuras: no free page within call range of the game image.");
        return false;
    }
    std::memset(page, 0xCC, RelayPageBytes);
    std::memset(page, 0, RelayStubOffsets[0]);
    EmitRelay(page);

    DWORD previous = 0;
    if (!VirtualProtect(page, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
        LogError("LeftClickAuras: could not make the relay page executable.");
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, RelayPageBytes);

    RelayUnwindRegistered = RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(page + RelayFunctionTableOffset),
                                                static_cast<DWORD>(RelayStubCount),
                                                static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(page)))
                         != FALSE;
    if (!RelayUnwindRegistered) {
        LogWarn("LeftClickAuras: unwind data for the relay page was not registered; stack walks stop there.");
    }
    RelayPage = page;
    return true;
}

auto RestoreCallSite(CallSitePatch& site) noexcept -> bool {
    if (!site.applied) {
        return true;
    }
    if (!Context->PatchBytes(site.rva, site.written.data(), 5, site.stock, 5)) {
        LogError("LeftClickAuras: could not restore the call at RVA 0x%llX.", static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.applied = false;
    return true;
}

auto PatchCallSite(CallSitePatch& site) noexcept -> bool {
    const auto from = Base + static_cast<std::uintptr_t>(site.rva) + 5;
    const auto to   = reinterpret_cast<std::uintptr_t>(RelayPage) + RelayStubOffsets[site.stub];
    const auto rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    if (rel < INT32_MIN || rel > INT32_MAX) {
        LogError("LeftClickAuras: the relay page is out of reach of RVA 0x%llX.",
                 static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.written[0] = 0xE8;
    const auto rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(site.written.data() + 1, &rel32, sizeof(rel32));

    if (!Context->PatchBytes(site.rva, site.stock, 5, site.written.data(), 5)) {
        LogError("LeftClickAuras: the loader refused the call at RVA 0x%llX.", static_cast<unsigned long long>(site.rva));
        return false;
    }
    site.applied = true;
    if (std::memcmp(reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(site.rva)),
                    site.written.data(), 5) != 0) {
        LogError("LeftClickAuras: RVA 0x%llX does not hold the bytes that were written.",
                 static_cast<unsigned long long>(site.rva));
        RestoreCallSite(site);
        return false;
    }
    return true;
}


// Runs inside InstallServer once the left relays are in. The pet limit,
// timer delete and AuraStart relays only pass through until the right select
// relay keeps an aura, so it goes in last.
void InstallItemRefresh() noexcept {
    if (!ValidateItemRefresh()) {
        LogError("LeftClickAuras: item_refresh_keeps_auras is off, the game image does not match.");
        return;
    }
    auto& finishSite = CallSites[StubItemRefreshFinish];
    auto& timerSite  = CallSites[StubRightAuraTimerDelete];
    auto& startSite  = CallSites[StubRightAuraStart];
    auto& selectSite = CallSites[StubRightSelect];
    const bool placed = PatchCallSite(finishSite) && PatchCallSite(timerSite) && PatchCallSite(startSite)
                     && PatchCallSite(selectSite);
    if (!placed) {
        RestoreCallSite(selectSite);
        RestoreCallSite(startSite);
        RestoreCallSite(timerSite);
        RestoreCallSite(finishSite);
        LogError("LeftClickAuras: item_refresh_keeps_auras is off, a call site could not be changed.");
        return;
    }
    Installed.itemRefreshRelays = true;
    ItemRefreshActive.store(true, std::memory_order_relaxed);
}

void InstallServer() noexcept {
    if (!ValidateServer()) {
        LogError("LeftClickAuras: left_slot_auras is off, the game image does not match.");
        return;
    }
    if (!EnsureRelay()) {
        LogError("LeftClickAuras: left_slot_auras is off.");
        return;
    }

    auto& selectSite   = CallSites[StubLeftSelect];
    auto& startSite    = CallSites[StubAuraStartArm];
    auto& statListSite = CallSites[StubRightAuraStatList];
    auto& stateOffSite = CallSites[StubRightAuraStateOff];
    const auto restoreServerSites = [&]() noexcept {
        RestoreCallSite(selectSite);
        RestoreCallSite(stateOffSite);
        RestoreCallSite(statListSite);
        RestoreCallSite(startSite);
        Installed.selectRelay = Installed.startRelay = Installed.rightRemovalRelays = false;
    };

    // The timer hook goes in first, the left select last.
    Installed.timerHook = Context->InstallInlineHook(PeriodicSkillEventRva, PeriodicSkillEventWindow.data(),
                                                     PeriodicSkillEventHookBytes, &HookPeriodicSkillEvent,
                                                     &OriginalPeriodicSkillEvent)
                       && OriginalPeriodicSkillEvent != nullptr;
    if (!Installed.timerHook) {
        // A hook already in place only passes through while ServerActive is false.
        LogError("LeftClickAuras: left_slot_auras is off, the aura timer is already hooked by another plugin.");
        return;
    }

    Installed.startRelay         = PatchCallSite(startSite);
    Installed.rightRemovalRelays = Installed.startRelay && PatchCallSite(statListSite) && PatchCallSite(stateOffSite);
    Installed.selectRelay        = Installed.rightRemovalRelays && PatchCallSite(selectSite);
    if (!Installed.selectRelay) {
        restoreServerSites();
        LogError("LeftClickAuras: left_slot_auras is off, a call site could not be changed.");
        return;
    }
    SingleEmanationActive.store(Config.singleEmanation, std::memory_order_relaxed);
    LiveLevelActive.store(Config.liveLevel, std::memory_order_relaxed);
    if (Config.itemRefreshKeepsAuras) {
        InstallItemRefresh();
    }
    ServerActive.store(true, std::memory_order_relaxed);
}

void InstallClient() noexcept {
    if (!ValidateClient()) {
        LogError("LeftClickAuras: move_only_clicks is off, the game image does not match.");
        return;
    }
    if (!EnsureRelay()) {
        LogError("LeftClickAuras: move_only_clicks is off.");
        return;
    }
    Installed.clickRelay = PatchCallSite(CallSites[StubClickGate]);
    if (Installed.clickRelay) {
        ClientActive.store(true, std::memory_order_relaxed);
    } else {
        LogError("LeftClickAuras: move_only_clicks is off, its call site could not be changed.");
    }
}

void InstallController(bool imageMatches) noexcept {
    if (!imageMatches) {
        LogError("LeftClickAuras: controller_aura_activation is off, the game image does not match.");
        return;
    }
    if (!EnsureRelay()) {
        LogError("LeftClickAuras: controller_aura_activation is off.");
        return;
    }
    Installed.syncRelay = PatchCallSite(CallSites[StubControllerSync]);
    if (!Installed.syncRelay) {
        LogError("LeftClickAuras: controller_aura_activation is off, its call site could not be changed.");
        return;
    }
    ControllerActive.store(true, std::memory_order_relaxed);

    if (Config.controllerTwoAuras) {
        Installed.pressRelay = PatchCallSite(CallSites[StubControllerPress]);
        if (Installed.pressRelay) {
            ControllerTwoAurasActive.store(true, std::memory_order_relaxed);
        } else {
            LogError("LeftClickAuras: controller_two_auras is off, its call site could not be changed.");
        }
    }
}

auto IsExecutableAddress(std::uint64_t address) noexcept -> bool {
    if (address == 0 || address == ~0ULL) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
        return false;
    }
    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & executable) != 0 && (info.Protect & PAGE_GUARD) == 0;
}

auto CurrentApplierPointer() noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(StateApplierPointerRva)),
                sizeof(value));
    return value;
}

// Points the applier thunk's pointer at BridgeApplyStateEffect. Only data is
// written, never game code.
void InstallLevelRefresh() noexcept {
    if (!ValidateLevelRefresh()) {
        LogError("LeftClickAuras: level_up_in_place and takeover_in_place are off, the game image does not match.");
        return;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement,
                reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(StateApplierThunkRva) + 2),
                sizeof(displacement));
    const auto pointerRva = StateApplierThunkRva + 6 + static_cast<std::int64_t>(displacement);
    if (pointerRva != StateApplierPointerRva) {
        LogError("LeftClickAuras: the state applier thunk points at RVA 0x%llX, expected 0x%llX; level refresh is off.",
                 static_cast<unsigned long long>(pointerRva), static_cast<unsigned long long>(StateApplierPointerRva));
        return;
    }
    const auto current = CurrentApplierPointer();
    if (!IsExecutableAddress(current)) {
        LogError("LeftClickAuras: the state applier pointer at RVA 0x%llX is 0x%llX, not code. D2RLoader has not "
                 "resolved it yet; level refresh is off.",
                 static_cast<unsigned long long>(StateApplierPointerRva), static_cast<unsigned long long>(current));
        return;
    }

    ApplierPointerOriginal = current;
    OriginalStateApplier   = reinterpret_cast<ApplyStateEffectFn>(static_cast<std::uintptr_t>(current));
    ApplierPointerBridge   = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&BridgeApplyStateEffect));
    LevelUpInPlaceActive.store(Config.levelUpInPlace, std::memory_order_relaxed);
    TakeoverInPlaceActive.store(Config.takeoverInPlace, std::memory_order_relaxed);
    LevelRefreshActive.store(true, std::memory_order_relaxed);
    if (!Context->PatchBytes(StateApplierPointerRva, &ApplierPointerOriginal, sizeof(ApplierPointerOriginal),
                             &ApplierPointerBridge, sizeof(ApplierPointerBridge))) {
        LevelRefreshActive.store(false, std::memory_order_relaxed);
        LogError("LeftClickAuras: the loader refused to change the state applier pointer at RVA 0x%llX; level "
                 "refresh is off.",
                 static_cast<unsigned long long>(StateApplierPointerRva));
        return;
    }
    if (CurrentApplierPointer() != ApplierPointerBridge) {
        LevelRefreshActive.store(false, std::memory_order_relaxed);
        LogError("LeftClickAuras: the state applier pointer did not read back as written; level refresh is off.");
        return;
    }
    Installed.applierPointer = true;
}

void RemoveLevelRefresh() noexcept {
    if (!Installed.applierPointer) {
        return;
    }
    if (CurrentApplierPointer() != ApplierPointerBridge) {
        LogWarn("LeftClickAuras: the state applier pointer was changed by someone else, left as it is.");
        Installed.applierPointer = false;
        return;
    }
    if (Context->PatchBytes(StateApplierPointerRva, &ApplierPointerBridge, sizeof(ApplierPointerBridge),
                            &ApplierPointerOriginal, sizeof(ApplierPointerOriginal))) {
        Installed.applierPointer = false;
    } else {
        LogError("LeftClickAuras: could not restore the state applier pointer.");
    }
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
    std::snprintf(line, sizeof(line),
                  "Left Click Auras %s (%s): left slot auras %s [left select %s, start %s, right removal %s, "
                  "timer %s]; single emanation %s; live level %s; item refresh keeps auras %s; move only clicks %s; "
                  "controller aura activation %s; controller two auras %s; level up in place %s; takeover in place "
                  "%s; diagnostics %s.",
                  PluginVersion, ConfigSource, OnOff(ServerActive.load(std::memory_order_relaxed)),
                  OnOff(Installed.selectRelay), OnOff(Installed.startRelay), OnOff(Installed.rightRemovalRelays),
                  OnOff(Installed.timerHook), OnOff(SingleEmanationActive.load(std::memory_order_relaxed)),
                  OnOff(LiveLevelActive.load(std::memory_order_relaxed)),
                  OnOff(ItemRefreshActive.load(std::memory_order_relaxed)),
                  OnOff(ClientActive.load(std::memory_order_relaxed)),
                  OnOff(ControllerActive.load(std::memory_order_relaxed)),
                  OnOff(ControllerTwoAurasActive.load(std::memory_order_relaxed)),
                  OnOff(LevelRefreshActive.load(std::memory_order_relaxed)
                        && LevelUpInPlaceActive.load(std::memory_order_relaxed)),
                  OnOff(LevelRefreshActive.load(std::memory_order_relaxed)
                        && TakeoverInPlaceActive.load(std::memory_order_relaxed)),
                  OnOff(Config.diagnostics));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Left auras: started %llu, not started by the game %llu, kept running %llu, ticks %llu. Stopped: "
                  "left button changed %llu, off the left button %llu, dead or not an aura %llu. Shared states kept "
                  "%llu.",
                  static_cast<unsigned long long>(LeftStarts.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftStartsRefused.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftKeptRunning.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftTicks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftStopsReselect.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftStopsOffButton.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftStopsInvalid.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(SharedStatesKept.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Item operations: auras kept on their button %llu; given back and kept running: left %llu, right "
                  "%llu; not given back, stopped: left %llu, right %llu.",
                  static_cast<unsigned long long>(AurasKeptByItemOperations.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftKeptThroughItems.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(RightKeptThroughItems.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LeftStopsItemsGone.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(RightStopsItemsGone.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Emanations left to the right button: %llu. Move only clicks: %llu. Live level ticks %llu, level "
                  "changes %llu.",
                  static_cast<unsigned long long>(EmanationsSkipped.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(MoveOnlyClicks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LiveLevelTicks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LiveLevelChanges.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Controller: left side toggle confirmations kept %llu, reset by the game %llu. Aura button presses "
                  "%llu, binding sides changed %llu, older aura replaced %llu.",
                  static_cast<unsigned long long>(ControllerLeftConfirmsKept.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ControllerLeftConfirmsReset.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ControllerAuraPresses.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ControllerSidesMoved.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(ControllerAurasReplaced.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Aura level refresh (applier pointer %s): aura applications %llu. Kept in place: level ups %llu, "
                  "takeovers %llu (remove callbacks added %llu, passive stats removed %llu, states turned on %llu). "
                  "Refused by the game, level put back %llu. Rebuilt anyway %llu (should stay 0).",
                  Installed.applierPointer
                      ? (CurrentApplierPointer() == ApplierPointerBridge ? "redirected" : "changed by someone else")
                      : "untouched",
                  static_cast<unsigned long long>(AuraApplications.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(LevelUpsKept.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(TakeoversKept.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(RemoveCallbacksAdded.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(PassiveStatsRemoved.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(StatesTurnedOn.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(GameRefusalsRestored.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(RebuiltAnyway.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line),
                  "Unchanged game behaviour seen: lower levels refused %llu, another aura replaced a state %llu.",
                  static_cast<unsigned long long>(LowerLevelsRefused.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(OtherSkillsReplaced.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Config    = Settings{};
    Installed = InstallState{};
    for (auto* flag : {&ServerActive, &SingleEmanationActive, &LiveLevelActive, &ClientActive, &ControllerActive,
                       &ControllerTwoAurasActive, &LevelRefreshActive, &LevelUpInPlaceActive, &TakeoverInPlaceActive,
                       &ItemRefreshActive}) {
        flag->store(false, std::memory_order_relaxed);
    }
    OriginalPeriodicSkillEvent = nullptr;
    OriginalStateApplier       = nullptr;
    ApplierPointerOriginal     = 0;
    ApplierPointerBridge       = 0;
    for (auto* counter : {&LeftStarts, &LeftStartsRefused, &LeftKeptRunning, &LeftStopsReselect, &LeftStopsOffButton,
                          &LeftStopsInvalid, &LeftTicks, &SharedStatesKept, &EmanationsSkipped, &MoveOnlyClicks,
                          &LiveLevelTicks, &LiveLevelChanges, &ControllerLeftConfirmsKept,
                          &ControllerLeftConfirmsReset, &ControllerAuraPresses, &ControllerSidesMoved,
                          &ControllerAurasReplaced, &AuraApplications, &LevelUpsKept, &TakeoversKept,
                          &RemoveCallbacksAdded, &PassiveStatsRemoved, &StatesTurnedOn, &GameRefusalsRestored,
                          &RebuiltAnyway, &LowerLevelsRefused, &OtherSkillsReplaced, &AurasKeptByItemOperations,
                          &LeftKeptThroughItems, &RightKeptThroughItems, &LeftStopsItemsGone, &RightStopsItemsGone}) {
        counter->store(0, std::memory_order_relaxed);
    }
    DiagnosticsBudget.store(64, std::memory_order_relaxed);
    RebuildWarnings.store(0, std::memory_order_relaxed);
    LastAuraSide.fill(BindingSideLeft);
    AcquireSRWLockExclusive(&ArmedLeftTimerLock);
    ArmedLeftTimers.fill(ArmedLeftTimer{});
    ArmedLeftTimerCursor = 0;
    ReleaseSRWLockExclusive(&ArmedLeftTimerLock);
    AcquireSRWLockExclusive(&KeptAuraLock);
    for (auto& table : KeptAuras) {
        table.fill(KeptAura{});
    }
    ReleaseSRWLockExclusive(&KeptAuraLock);
    KeptAuraTableFullLogged.store(false, std::memory_order_relaxed);
}

}  // namespace
}  // namespace CelestialRayOne::LeftClickAuras

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.left-click-auras",
    .name        = "Left Click Auras",
    .version     = "1.4.0",
    .author      = "CelestialRayOne",
    .description = "Auras on the left mouse button run alongside the right button's aura; controller auras work, two at once; auras keep their state when their level rises and while the game re-checks the equipment.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::LeftClickAuras;

    if (!D2RL::HasContext(context) || context->exeBase == 0) {
        return false;
    }
    Context = context;
    Base    = context->exeBase;
    ResetState();
    LoadConfig();

    if (!Config.enabled) {
        LogInfo("Left Click Auras %s loaded disabled by config; the game runs stock.", PluginVersion);
        return true;
    }

    ResolveNativeFunctions();
    // Checked before the server relays change call sites inside its witnesses.
    const bool controllerImage = Config.controllerAuraActivation && ValidateController();
    if (Config.leftSlotAuras) {
        InstallServer();
    }
    if (Config.moveOnlyClicks) {
        InstallClient();
    }
    if (Config.controllerAuraActivation) {
        InstallController(controllerImage);
    }
    if (Config.levelUpInPlace || Config.takeoverInPlace) {
        InstallLevelRefresh();
    }

    if (!context->RegisterConsoleCommand("leftclickauras", StatusCommand, "Show Left Click Auras status and counters.")) {
        LogWarn("LeftClickAuras: the leftclickauras console command could not be registered.");
    }

    LogInfo("Left Click Auras %s by CelestialRayOne: left slot auras %s, single emanation %s, live level %s, item "
            "refresh keeps auras %s, move only clicks %s, controller aura activation %s, controller two auras %s, "
            "level up in place %s, takeover in place %s, build %s.",
            PluginVersion, OnOff(ServerActive.load(std::memory_order_relaxed)),
            OnOff(SingleEmanationActive.load(std::memory_order_relaxed)),
            OnOff(LiveLevelActive.load(std::memory_order_relaxed)),
            OnOff(ItemRefreshActive.load(std::memory_order_relaxed)), OnOff(ClientActive.load(std::memory_order_relaxed)),
            OnOff(ControllerActive.load(std::memory_order_relaxed)),
            OnOff(ControllerTwoAurasActive.load(std::memory_order_relaxed)),
            OnOff(LevelRefreshActive.load(std::memory_order_relaxed)
                  && LevelUpInPlaceActive.load(std::memory_order_relaxed)),
            OnOff(LevelRefreshActive.load(std::memory_order_relaxed)
                  && TakeoverInPlaceActive.load(std::memory_order_relaxed)),
            context->buildName != nullptr ? context->buildName : "unknown");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::LeftClickAuras;
    if (Context == nullptr) {
        return;
    }
    for (auto* flag : {&ServerActive, &SingleEmanationActive, &LiveLevelActive, &ClientActive, &ControllerActive,
                       &ControllerTwoAurasActive, &LevelRefreshActive, &LevelUpInPlaceActive, &TakeoverInPlaceActive,
                       &ItemRefreshActive}) {
        flag->store(false, std::memory_order_relaxed);
    }
    RemoveLevelRefresh();

    bool allRestored = true;
    for (auto& site : CallSites) {
        allRestored = RestoreCallSite(site) && allRestored;
    }
    if (allRestored && RelayUnwindRegistered && RelayPage != nullptr) {
        RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(RelayPage + RelayFunctionTableOffset));
        RelayUnwindRegistered = false;
    }
}
