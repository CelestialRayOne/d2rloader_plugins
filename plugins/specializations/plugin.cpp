// Specializations
//
// A specialization system in one plugin:
//   - a "Specialization" entry on the character sheet: a + while the character
//     has points to spend, a - once it has learned something and has no point
//     left. Both open the specialization window.
//   - the specialization window: a plugin panel whose layout and art belong to
//     the mod.
//   - learning a perk: the host writes the perk's stat on the character (its
//     own value, saved with the character) and takes one point.
//   - Magic and Physical resistance rows on the character sheet.
// The perks do nothing by themselves. The mod gives their stats a meaning
// (softcode, other plugins). README.md is the setup guide for modders.
//
// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------
//   Points are the character's own value of points_stat (what a pSpell, a quest
//   or another plugin adds; points granted by items are not counted). A perk is
//   learned when the character's own value of its stat is above 0. A perk can
//   be learned when
//     - the character has at least 1 point,
//     - no perk of another specialization is learned (the first perk picks the
//       specialization for good),
//     - every row above it has a learned perk of this specialization,
//     - no perk of its own row is learned.
//   With Shift a learned perk is unlearned when respec_stat is set, the
//   character's own value of it is at least unlearn_cost and no perk of a row
//   below it is learned (only the deepest learned row, so the rows stay taken
//   in order). Its stat goes back to 0, the point comes back and unlearn_cost
//   is taken from respec_stat.
//   The client only asks. The host checks the same rules against its own copy
//   of the character before it writes anything.
//
// ---------------------------------------------------------------------------
// The character sheet (client)
// ---------------------------------------------------------------------------
//   14E9F80 is the keyboard/mouse CharacterStatsPanel update. It calls
//   14E6E70 (panel, player) for the resistance rows: that loop walks magic,
//   fire, lightning, cold and poison, but has no text field for magic, and
//   physical is not in it. 14E6E70 is hooked: after the game fills its rows the
//   plugin fills MagicText and PhysicalText the way the stock rows are filled.
//     value    the stat total (2F5020)
//     floor    the sheet's own floor, read live from 14E7299 (mov eax, imm32),
//              so a patch that moves it moves these two rows with the others
//     magic    cap = min(maxmagicresist + 75, 95). The 75 (14E728E) and the 95
//              (14E7295) are read live from the stock loop. No difficulty
//              penalty: the damage code 4523E0 adds it to every resistance
//              except stats 36 and 37.
//     physical cap = the damage code's own physical cap, read live from 4524D6
//              (4523E0 uses it when the resistance has no max stat)
//     colour   gold at the cap, red below 0, otherwise the panel's default
//              colour (panel +168h), the stock rule of 14E72BC..14E72FC. The
//              text colour of a TextBoxWidget is at +0BCh; 908690 (out, index,
//              5) gives a colour by index (1 red, 4 gold).
//     text     with 846210() set, the d2r:CharacterSheetResistanceStat format,
//              otherwise the bare number, as 14E7303 does.
//   The same hook drives SpecializationButton and SpecializationTitle: 856220
//   (widget, name) finds a direct child, vtable +50h sets visible, vtable +58h
//   gives the widget's type descriptor. SpecializationButton is one button with
//   six frames (+ normal, hovered, pressed, then - normal, hovered, pressed).
//   Two buttons on one spot do not work: a hidden widget still takes the mouse,
//   so the hidden one on top got every press and the visible one never showed
//   its pressed frame. The plugin writes the button's frame numbers instead
//   (normalFrame +7E8h, pressedFrame +7ECh, hoveredFrame +7F0h, disabledFrame
//   +7F4h, the shown frame +710h; 870200 presses with them), and while hidden
//   the button is disabled (+67Ah, cleared by 86EFB0), so its spot takes no
//   press, no sound and no click.
//
// ---------------------------------------------------------------------------
// The window (client)
// ---------------------------------------------------------------------------
//   An SDK plugin panel, localId SpecializationPanel. D2RCore loads a plugin
//   panel from data/global/ui/layouts/<plugin-id>/<localId>hd.json, whose root
//   must be named <plugin-id>/<localId>. The layout is compiled into the
//   plugin (SpecializationPanelLayout.inc, generated from
//   SpecializationPanelhd.json) and registered at that path, so a mod needs no
//   layout file; a mod replaces it with its own
//     data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json
//   Text in a plugin panel must be a full reference (@d2r:Key), or D2RCore
//   refuses the layout; mod strings loaded the normal way are d2r: strings.
//   Widget names, all optional (a missing widget is skipped); s = specialization,
//   r = row, p = perk, all from 1:
//     Tab<s>, TabActive<s> (viewed), TabChosen<s> (chosen), TabLocked<s>
//     Page<s>                container of specialization s, shown while viewed;
//                            images only: Glow<r>_<p> (can be learned now),
//                            Learned<r>_<p>, Locked<r>_<p> (shade on a perk not
//                            learned and not learnable), plus the icons
//                            (Icon<r>_<p>, which the plugin leaves alone)
//     Hover<r>_<p>           one hover area per perk position, shared by all
//                            pages, after the pages so nothing hidden sits on it
//     Perk<r>_<p>            optional, at window level: shown only where a click
//                            does something; hovering it counts as the hover area
//     Tip<r>_<p>             the tooltip under that position: TipBack1..TipBack14
//                            (background for that many text lines), TipTitle,
//                            TipText, TipHint1..TipHint13 (the gray unlearn line,
//                            the box of the text line it starts on). The plugin
//                            breaks the text into lines for a TipText 672 units
//                            wide at $XMediumFontSize (TipLineWidth, GlyphWidth)
//                            and shows the background of exactly that height.
//     PointsAvailable        text box: @d2r:SpecializationsPointsAvailable, %d the
//                            unspent points
//   Click messages, all "PanelManager:OpenPanel:<text>", consumed by the plugin:
//     SpecializationsOpen               open / close the window
//     SpecializationsClose              close it
//     SpecializationsTab<s>             view specialization s
//     SpecializationsPerk.<r>.<p>       learn (or, with Shift, unlearn) that perk
//                                       of the specialization on screen (optional:
//                                       a click on a perk's hover area does the same)
//   Every page's widgets sit at the same place. Hidden pages still take mouse
//   input for their children, so the hover areas exist once, at window level.
//   A left click (right click with swapped buttons) released over the same
//   hover area it was pressed on learns that perk, or unlearns it with Shift.
//   Space closes the window like the game's own panels (not while the chat
//   line has the keyboard). With no points and no learned perk the window does
//   not open: a hidden button still takes clicks on its spot.
//   The UI keeps the hovered widget at +178h of the object 846020 returns (the
//   widget tooltip drawer 875980 tests it). The window is found by name, as an
//   ancestor of the hovered widget (parent at +30h) or among the children of
//   the UI root (children at +58h, count at +60h, name at +8h, 85622F).
//   While the window is open a UI task refreshes it on every root widget update.
//
// ---------------------------------------------------------------------------
// The host
// ---------------------------------------------------------------------------
//   A click sends (player unit id, specialization, row, perk) over the plugin's
//   network channel. The host runs the request on its game thread: the player
//   is found through the server's client registry (485290 / 485320 / 484CDB,
//   48FE80), the rules are checked on the server unit, then 2F7D10 writes the
//   perk's own value and the points. Rows with Saved = 1 reach the client by
//   themselves.
//
// ---------------------------------------------------------------------------
// Native pieces, D2RLoader 1.3.1 process image, all checked byte for byte at
// load. Nothing is hooked when a check fails.
// ---------------------------------------------------------------------------
//   14E6E70  sheet resistance rows (panel, player), hooked
//   14E728C  lea ecx, [rdi+disp8] / cmp ecx, imm8 / jl / mov ecx, imm32
//   14E7299  mov eax, imm32 (the sheet floor) and the clamp after it
//   4524D5   mov eax, imm32 (the physical cap in the damage code)
//   856220   child widget by name     86DD50  text box set text
//   846020   UI state object          875980  widget tooltip drawer (+178h, +190h)
//   8562A0   widget rectangle (parent at +30h)
//   908690   colour by index          846210  resistance format switch
//   2F5020   stat total (unit, stat, layer), loader thunk into D2RCore
//   2F48C0   own value (unit, stat, layer), loader thunk into D2RCore
//   2F7D10   own value write (unit, stat, value, layer), loader thunk
//   34B9D0   unit type    34A330  unit id    8B2D0 / 9A480  local player
//   48FE80   server unit lookup (game, type, id)
//   485290 / 485320 / 484CDB  the server's client registry (2AAECA0, ready
//            flag 2AAF4A0)

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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.specializations";
constexpr char PluginVersion[] = "1.14.2";
constexpr char PanelLocalId[]  = "SpecializationPanel";
constexpr char PanelRootName[] = "celestialrayone.specializations/SpecializationPanel";

// The path D2RCore reads this plugin's panel layout from. The plugin registers
// its built-in layout there.
constexpr char PanelResourcePath[] = "data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json";
// The window layout is compiled into the plugin (SpecializationPanelLayout.inc,
// generated from the layout JSON). D2RLoader builds plugin panels from the
// registered bytes. A mod can still replace it through the normal resource order
// with its own data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json.
#include "SpecializationPanelLayout.inc"

auto PanelLayoutBytes(std::string_view& out) noexcept -> bool {
    static std::string layout;
    if (layout.empty()) {
        for (const char* const chunk : PanelLayoutChunks) layout += chunk;
    }
    out = layout;
    return !layout.empty();
}

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t ResistRowsRva       = 0x14E6E70;
constexpr std::uint64_t SheetCapLeaRva      = 0x14E728C;  // 8D 4F disp8
constexpr std::uint64_t SheetCapCmpRva      = 0x14E728F;  // 83 F9 imm8
constexpr std::uint64_t SheetCapMovRva      = 0x14E7292;  // 7C 05 B9 imm32
constexpr std::uint64_t SheetFloorRva       = 0x14E7299;  // B8 imm32
constexpr std::uint64_t SheetClampRva       = 0x14E729E;
constexpr std::uint64_t PhysicalCapRva      = 0x4524D5;   // B8 imm32
constexpr std::uint64_t FindChildRva        = 0x856220;
constexpr std::uint64_t SetTextRva          = 0x86DD50;
constexpr std::uint64_t ColorByIndexRva     = 0x908690;
constexpr std::uint64_t ResistFormatRva     = 0x846210;
constexpr std::uint64_t UiStateRva          = 0x846020;
constexpr std::uint64_t WidgetTooltipRva    = 0x875980;
constexpr std::uint64_t WidgetRectRva       = 0x8562A0;
constexpr std::uint64_t ReadStatRva         = 0x2F5020;
constexpr std::uint64_t ReadOwnStatRva      = 0x2F48C0;
constexpr std::uint64_t WriteOwnStatRva     = 0x2F7D10;
constexpr std::uint64_t UnitTypeRva         = 0x34B9D0;
constexpr std::uint64_t UnitIdRva           = 0x34A330;
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;
constexpr std::uint64_t RegistryResetRva    = 0x485290;
constexpr std::uint64_t ClientLookupRva     = 0x485320;
constexpr std::uint64_t ClientPlayerRva     = 0x484CDB;

// Live operands inside the checked instructions above.
constexpr std::uint64_t SheetCapBaseOperand    = SheetCapLeaRva + 2;  // int8, 75
constexpr std::uint64_t SheetCeilingOperand    = SheetCapMovRva + 3;  // int32, 95
constexpr std::uint64_t SheetFloorOperand      = SheetFloorRva + 1;   // int32, -100
constexpr std::uint64_t PhysicalCapOperand     = PhysicalCapRva + 1;  // int32, 50

// Data. Proven by the instructions checked below: the lea and the mov inside
// RegistryResetBytes, the lea inside ClientLookupBytes.
constexpr std::uint64_t RegistryRva      = 0x2AAECA0;
constexpr std::uint64_t RegistryReadyRva = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets  = 256;

constexpr std::size_t ClientNextOffset     = 0x630;
constexpr std::size_t ClientUnitTypeOffset = 0x26C;
constexpr std::size_t ClientUnitIdOffset   = 0x270;
constexpr std::size_t ClientGameOffset     = 0x2B0;

constexpr std::uint32_t UnitTypePlayer = 0;

// Widgets.
constexpr std::size_t PanelDefaultColorOffset = 0x168;  // CharacterStatsPanel defaultTextColor
constexpr std::size_t TextColorOffset         = 0xBC;   // TextBoxWidget text colour, 16 bytes
constexpr std::size_t SetVisibleSlot          = 0x50 / 8;
constexpr std::size_t TypeDescSlot            = 0x58 / 8;
constexpr std::size_t TypeDescNameOffset      = 0x08;
constexpr std::size_t TypeDescParentOffset    = 0x10;
constexpr std::size_t WidgetParentOffset      = 0x30;   // 8562A0: mov rcx, [rcx+30h]
constexpr std::size_t WidgetNameOffset        = 0x08;   // 85625D: mov rcx, [rbx] / mov rdx, rsi / mov rcx, [rcx+8]
constexpr std::size_t WidgetChildrenOffset    = 0x58;   // 85622F: mov rbx, [rcx+58h]
constexpr std::size_t WidgetChildCountOffset  = 0x60;   // 856236: mov rax, [rcx+60h] / lea rdi, [rbx+rax*8]
constexpr std::size_t UiHoveredOffset         = 0x178;  // 875980: cmp [rax+178h], rdi
constexpr std::size_t UiFocusedOffset         = 0x190;  // 875980: cmp [rax+190h], rdi
constexpr std::int32_t ColorIndexRed          = 1;
constexpr std::int32_t ColorIndexGold         = 4;
constexpr std::int32_t ColorMode              = 5;

// itemstatcost.txt.
constexpr std::int32_t  StatPhysicalResist  = 36;  // damageresist
constexpr std::int32_t  StatMagicResist     = 37;  // magicresist
constexpr std::int32_t  StatMaxMagicResist  = 38;  // maxmagicresist
constexpr std::size_t   IscFlagsOffset      = 0x04;
constexpr std::uint32_t IscFlagSaved        = 0x800;
constexpr std::size_t   IscCsvBitsOffset    = 0x0A;
constexpr std::size_t   IscCsvParamOffset   = 0x0B;

// ---------------------------------------------------------------------------
// Expected bytes
// ---------------------------------------------------------------------------

// 14E6E70 prologue, the bytes the hook displaces: push rbp / push rbx /
// push rdi / push r12 / push r13 / push r14 / push r15 / lea rbp, [rsp-170h] /
// sub rsp, 270h. Nothing relative; the next instruction is the rip-relative
// cookie load.
constexpr std::uint8_t ResistRowsPrologue[]{
    0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,
    0x90,0xFE,0xFF,0xFF,0x48,0x81,0xEC,0x70,0x02,0x00,0x00 };
constexpr std::uint8_t SheetCapLeaBytes[]{ 0x8D,0x4F };
constexpr std::uint8_t SheetCapCmpBytes[]{ 0x83,0xF9 };
constexpr std::uint8_t SheetCapMovBytes[]{ 0x7C,0x05,0xB9 };
constexpr std::uint8_t MovEaxImmBytes[]{ 0xB8 };
// 14E729E: mov edi, ecx / cmp r15d, eax / cmovg eax, r15d / cmp eax, ecx /
// cmovl edi, eax.
constexpr std::uint8_t SheetClampBytes[]{
    0x8B,0xF9,0x44,0x3B,0xF8,0x41,0x0F,0x4F,0xC7,0x3B,0xC1,0x0F,0x4C,0xF8 };
// 85622F, inside the child lookup: the children array at +58h, its count at
// +60h, and each child's name pointer at +8, which the plugin also walks itself.
constexpr std::uint64_t WidgetChildrenRva = 0x85622F;
constexpr std::uint8_t WidgetChildrenBytes[]{
    0x48,0x8B,0x59,0x58,0x48,0x8B,0xF2,0x48,0x8B,0x41,0x60,0x48,0x8D,0x3C,0xC3,0x48,
    0x3B,0xDF,0x74,0x36,0x48,0x83,0x3B,0x00,0x75,0x14,0x48,0x8D,0x4C,0x24,0x30,0xC6,
    0x44,0x24,0x30,0x00,0xE8,0xF8,0xFA,0x98,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,
    0x0B,0x48,0x8B,0xD6,0x48,0x8B,0x49,0x08 };
constexpr std::uint8_t FindChildBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0x59,0x58 };
// 86DD50: push rbx / sub rsp, 20h / mov r8, [rdx+8] / mov rbx, rcx /
// mov rdx, [rdx] / add rcx, 88h.
constexpr std::uint8_t SetTextBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x4C,0x8B,0x42,0x08,0x48,0x8B,0xD9,0x48,0x8B,0x12,
    0x48,0x81,0xC1,0x88,0x00,0x00,0x00 };
// 908690 (out, index, mode): index below 0Dh takes the table path.
constexpr std::uint8_t ColorByIndexBytes[]{
    0x40,0x53,0x57,0x48,0x83,0xEC,0x38,0xC6,0x44,0x24,0x58,0x00,0x41,0x8B,0xD8,0xC6,
    0x44,0x24,0x60,0x00,0x8B,0xC2,0xC6,0x44,0x24,0x50,0x00,0x48,0x8B,0xF9,0x83,0xFA,
    0x0D,0x7D,0x18 };
constexpr std::uint8_t ResistFormatBytes[]{
    0x48,0x8B,0x05,0x59,0x9F,0xBF,0x02,0x48,0x85,0xC0,0x74,0x08,0x0F,0xB6,0x80,0xB9,
    0x00,0x00,0x00,0xC3,0xE9,0xE7,0x9F,0xE0,0xFF };
// 846020: sub rsp, 28h / mov rax, [2A40170] / test rax, rax / jnz / ... /
// mov rax, [2A40170]; then returns [rax+0D0h].
constexpr std::uint8_t UiStateBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x45,0xA1,0xBF,0x02,0x48,0x85,0xC0,0x75,0x1A,
    0x48,0x8D,0x4C,0x24,0x30,0x88,0x44,0x24,0x30,0xE8,0x82,0xD8,0xFF,0xFF,0x84,0xC0,
    0x74,0x01,0xCC,0x48,0x8B,0x05,0x26,0xA1,0xBF,0x02 };
// 875980: push rdi / sub rsp, 40h / mov rdi, rcx / call 846020 /
// cmp [rax+178h], rdi / je / call 846020 / cmp [rax+190h], rdi / jne.
// 87598E (875980 + 0Eh): cmp [rax+178h], rdi / jz / call / cmp [rax+190h], rdi /
// jnz. Only these two offsets are used, so the witness starts after the
// prologue: the respec plugin hooks 875980 itself, which rewrites the prologue.
// 870200, the button press: add rcx, 680h (its image) / mov edx, [rbx+7ECh]
// (pressedFrame) / call 859700 (image frame = edx, stored at image+90h) /
// mov byte [rbx+679h], 1 (pressed).
constexpr std::uint64_t ButtonPressRva = 0x870200;
constexpr std::uint8_t ButtonPressBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x81,0xC1,0x80,0x06,0x00,0x00,
    0x8B,0x93,0xEC,0x07,0x00,0x00,0xE8,0xE5,0x94,0xFE,0xFF,0x48,0x8B,0xCB,0xE8,0x3D,
    0x02,0x00,0x00,0x48,0x8B,0xCB,0xC6,0x83,0x79,0x06,0x00,0x00,0x01 };
// 86EFB0, the button enable: mov eax, [rcx+7F4h] (disabledFrame) / mov byte
// [rcx+67Ah], 0 (disabled) / cmp [rcx+710h], eax (the shown frame).
// 1A0C00 plays a sound by its sounds.txt name: rcx name, rdx/r8d/r9d 0
// (1414E9F1C, the character sheet's stat point, plays cursor_stat_point_one_hd
// this way). It looks the name up (1D9760) and plays that row (1A0780); a name
// sounds.txt does not have plays nothing. Row 0 is ESOUND_NONE: silence.
constexpr std::uint64_t PlaySoundByNameRva = 0x1A0C00;
constexpr std::uint8_t PlaySoundByNameBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x41,
    0x8B,0xD9,0x41,0x8B,0xF8,0x48,0x8B,0xF2 };
constexpr std::uint64_t ButtonEnableRva = 0x86EFB0;
constexpr std::uint8_t ButtonEnableBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x81,0xF4,0x07,0x00,0x00,0x48,0x8B,0xD9,0xC6,
    0x81,0x7A,0x06,0x00,0x00,0x00,0x39,0x81,0x10,0x07,0x00,0x00 };
constexpr std::size_t ButtonNormalFrameOffset   = 0x7E8;
constexpr std::size_t ButtonPressedFrameOffset  = 0x7EC;
constexpr std::size_t ButtonHoveredFrameOffset  = 0x7F0;
constexpr std::size_t ButtonDisabledFrameOffset = 0x7F4;
constexpr std::size_t ButtonShownFrameOffset    = 0x710;
constexpr std::size_t ButtonPressedOffset       = 0x679;
constexpr std::size_t ButtonDisabledOffset      = 0x67A;
constexpr std::uint64_t WidgetFocusFieldsRva = 0x87598E;
constexpr std::uint8_t WidgetFocusFieldsBytes[]{
    0x48,0x39,0xB8,0x78,0x01,0x00,0x00,0x74,0x12,0xE8,0x84,0x06,0xFD,0xFF,0x48,0x39,
    0xB8,0x90,0x01,0x00,0x00,0x0F,0x85,0xB7,0x00,0x00,0x00 };
// 8562A0: push rbx / sub rsp, 30h / cmp byte [rcx+52h], 0 / mov rbx, rdx /
// je / mov rcx, [rcx+30h] (the parent).
constexpr std::uint8_t WidgetRectBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0x79,0x52,0x00,0x48,0x8B,0xDA,0x74,0x2A,0x48,
    0x8B,0x49,0x30 };
// Loader thunks: jmp [rip+slot], then four nops. The slot offset changes with
// the loader version, so only the fixed parts are checked.
constexpr std::uint8_t ThunkHead[]{ 0xFF,0x25 };
constexpr std::uint8_t ThunkTail[]{ 0x90,0x90,0x90,0x90 };
// 34B9D0, the whole function: null -> 6, else [rcx].
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
    0x00,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x01,0x48,0x83,0xC4,0x28,0xC3 };
// 34A330, the whole function: null -> -1, else [rcx+8].
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3 };
constexpr std::uint8_t LocalPlayerIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
constexpr std::uint8_t PlayerFromIndexBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08 };
constexpr std::uint8_t ServerUnitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9 };
constexpr std::uint8_t RegistryResetBytes[]{
    0x33,0xD2,0x48,0x8D,0x05,0x07,0x9A,0x62,0x02,0x8D,0x4A,0x20,0x0F,0x1F,0x40,0x00,
    0x48,0x89,0x10,0x48,0x89,0x50,0x08,0x48,0x89,0x50,0x10,0x48,0x8D,0x40,0x40,0x48,
    0x89,0x50,0xD8,0x48,0x89,0x50,0xE0,0x48,0x89,0x50,0xE8,0x48,0x89,0x50,0xF0,0x48,
    0x89,0x50,0xF8,0x48,0x83,0xE9,0x01,0x75,0xD7,0xC7,0x05,0xCD,0xA1,0x62,0x02,0x01,
    0x00,0x00,0x00,0xC3 };
constexpr std::uint8_t ClientLookupBytes[]{
    0x0F,0xB6,0xC1,0x48,0x8D,0x15,0x76,0x99,0x62,0x02,0x48,0x8B,0x04,0xC2,0x48,0x85,
    0xC0,0x74,0x10,0x39,0x08,0x74,0x0E,0x48,0x8B,0x80,0x30,0x06,0x00,0x00,0x48,0x85,
    0xC0,0x75,0xF0,0x33,0xC0,0xC3 };
constexpr std::uint8_t ClientPlayerBytes[]{
    0x44,0x8B,0x83,0x70,0x02,0x00,0x00,0x8B,0x93,0x6C,0x02,0x00,0x00,0x48,0x8B,0x8B,
    0xB0,0x02,0x00,0x00,0xE8,0x8C,0xB1,0x00,0x00 };

// callable: a native function the plugin only calls. Another plugin may hook it
// (its first bytes become a jump), which is fine: the call goes through that hook.
struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
    bool                callable;
};

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name, false };
}

template <std::size_t N>
constexpr auto WC(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name, true };
}

const std::array<Witness, 32> Witnesses{
    W(ResistRowsRva, ResistRowsPrologue, "sheet resistance rows"),
    W(SheetCapLeaRva, SheetCapLeaBytes, "sheet resistance cap base"),
    W(SheetCapCmpRva, SheetCapCmpBytes, "sheet resistance ceiling compare"),
    W(SheetCapMovRva, SheetCapMovBytes, "sheet resistance ceiling"),
    W(SheetFloorRva, MovEaxImmBytes, "sheet resistance floor"),
    W(SheetClampRva, SheetClampBytes, "sheet resistance clamp"),
    W(PhysicalCapRva, MovEaxImmBytes, "physical resistance cap"),
    WC(FindChildRva, FindChildBytes, "child widget lookup"),
    W(ButtonPressRva, ButtonPressBytes, "button press and frame fields"),
    WC(ButtonEnableRva, ButtonEnableBytes, "button enable"),
    WC(PlaySoundByNameRva, PlaySoundByNameBytes, "play sound by name"),
    WC(SetTextRva, SetTextBytes, "text box text"),
    WC(ColorByIndexRva, ColorByIndexBytes, "colour by index"),
    WC(ResistFormatRva, ResistFormatBytes, "resistance format switch"),
    WC(UiStateRva, UiStateBytes, "UI state"),
    W(WidgetFocusFieldsRva, WidgetFocusFieldsBytes, "UI hover and focus fields"),
    W(WidgetChildrenRva, WidgetChildrenBytes, "widget children and names"),
    WC(WidgetRectRva, WidgetRectBytes, "widget rectangle"),
    WC(ReadStatRva, ThunkHead, "stat read"),
    W(ReadStatRva + 6, ThunkTail, "stat read"),
    WC(ReadOwnStatRva, ThunkHead, "own stat read"),
    W(ReadOwnStatRva + 6, ThunkTail, "own stat read"),
    WC(WriteOwnStatRva, ThunkHead, "own stat write"),
    W(WriteOwnStatRva + 6, ThunkTail, "own stat write"),
    WC(UnitTypeRva, UnitTypeBytes, "unit type"),
    WC(UnitIdRva, UnitIdBytes, "unit id"),
    WC(LocalPlayerIndexRva, LocalPlayerIndexBytes, "local player index"),
    WC(PlayerFromIndexRva, PlayerFromIndexBytes, "local player"),
    WC(ServerUnitRva, ServerUnitBytes, "server unit lookup"),
    W(RegistryResetRva, RegistryResetBytes, "client registry reset"),
    W(ClientLookupRva, ClientLookupBytes, "client lookup"),
    W(ClientPlayerRva, ClientPlayerBytes, "client player fields"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

struct StringView {
    const char* data;
    std::size_t size;
};

using ResistRowsFn   = std::int64_t(__fastcall*)(void* panel, void* player) noexcept;
using FindChildFn    = void*(__fastcall*)(void* widget, const char* name);
using ButtonEnableFn = void(__fastcall*)(void* button);
using PlaySoundFn    = std::int64_t(__fastcall*)(const char* name, void*, std::int32_t, std::int32_t);
using SetTextFn      = void*(__fastcall*)(void* textBox, const StringView* text);
using ColorByIndexFn = void*(__fastcall*)(void* out, std::int32_t index, std::int32_t mode);
using UiFlagFn       = std::uint8_t(__fastcall*)();
using ReadStatFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using WriteStatFn    = void(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);
using UnitTypeFn     = std::uint32_t(__fastcall*)(void* unit);
using UnitIdFn       = std::uint32_t(__fastcall*)(void* unit);
using PlayerIndexFn  = std::int32_t(__fastcall*)();
using PlayerFromFn   = void*(__fastcall*)(std::int32_t index);
using ServerUnitFn   = void*(__fastcall*)(void* game, std::uint32_t type, std::uint32_t id);
using TypeDescFn     = void*(__fastcall*)(void* widget);
using SetVisibleFn   = void(__fastcall*)(void* widget, std::uint64_t visible);
using UiStateFn      = void*(__fastcall*)();

ResistRowsFn   OriginalResistRows{};
FindChildFn    FindChild{};
ButtonEnableFn ButtonEnable{};
PlaySoundFn    PlaySound{};
SetTextFn      NativeSetText{};
ColorByIndexFn ColorByIndex{};
UiFlagFn       ResistFormatOn{};
ReadStatFn     ReadStat{};
ReadStatFn     ReadOwnStat{};
WriteStatFn    WriteOwnStat{};
UnitTypeFn     UnitType{};
UnitIdFn       UnitId{};
PlayerIndexFn  LocalPlayerIndex{};
PlayerFromFn   PlayerFromIndex{};
ServerUnitFn   ServerUnit{};
UiStateFn      UiState{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

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

template <typename T>
auto ReadRva(std::uint64_t rva) noexcept -> T {
    return Read<T>(reinterpret_cast<const void*>(Base), static_cast<std::size_t>(rva));
}

// ---------------------------------------------------------------------------
// Services and state
// ---------------------------------------------------------------------------

const D2RL::PanelService*        PanelSvc{};
const D2RL::WidgetService*       WidgetSvc{};
const D2RL::SharedEventService*  EventSvc{};
const D2RL::NetworkService*      NetworkSvc{};
const D2RL::ThreadService*       ThreadSvc{};
const D2RL::LifecycleService*    LifecycleSvc{};
const D2RL::DataTableService*    TableSvc{};
const D2RL::LocalizationService* TextSvc{};
const D2RL::ResourceService*     ResourceSvc{};

enum class PluginState { NotLoaded, DisabledByConfig, BadConfig, UnsupportedBuild, ServicesMissing, InstallFailed, Active };
PluginState       State{ PluginState::NotLoaded };
std::atomic<bool> Active{};
std::atomic<bool> SheetHooked{};
std::atomic<bool> SheetReady{};
std::atomic<std::uint64_t> LastSheetUpdateMs{};

D2RL::Panels::RegistrationHandle      Panel{ D2RL::Panels::InvalidHandle };
D2RL::Network::ChannelHandle          Channel{ D2RL::Network::InvalidChannelHandle };
D2RL::SharedEvents::ListenerHandle    ClickListener{ D2RL::SharedEvents::InvalidHandle };

constexpr std::uint16_t ChannelLocalId     = 1;
constexpr std::uint64_t CompatibilityToken = 0x5350454349414C32ULL;  // "SPECIAL2"
constexpr std::uint16_t LearnMessageId     = 1;

struct LearnMessage {
    std::uint32_t playerId;
    std::uint8_t  specialization;  // 0-based
    std::uint8_t  row;             // 0-based
    std::uint8_t  perk;            // 0-based
    std::uint8_t  op;              // 0 learn, 1 unlearn, 2 claim the points of the character's level
};
static_assert(sizeof(LearnMessage) == 8);

// Counters, shown by the console command.
std::atomic<std::uint64_t> SheetUpdates{};
std::atomic<std::uint64_t> ClicksLearn{};
std::atomic<std::uint64_t> ClicksIgnored{};
std::atomic<std::uint64_t> ClicksRefusedLocally{};
std::atomic<std::uint64_t> RequestsSent{};
std::atomic<std::uint64_t> RequestsDropped{};
std::atomic<std::uint64_t> RequestsReceived{};
std::atomic<std::uint64_t> RequestsRefused{};
std::atomic<std::uint64_t> RequestsUnknown{};
std::atomic<std::uint64_t> PerksLearned{};
std::atomic<std::uint64_t> PerksUnlearned{};
std::atomic<std::uint64_t> PointsAwarded{};
std::atomic<std::uint64_t> PointsRemoved{};
std::atomic<std::uint32_t> ConnectionNow{ static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected) };

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr std::size_t MaxSpecializations = 8;
constexpr std::size_t MaxRows            = 10;
constexpr std::size_t MaxPerksPerRow     = 5;
constexpr std::size_t MaxPips            = 10;
constexpr std::size_t MaximumConfigBytes = 65'536;

// The default config, identical to celestialrayone.specializations.toml.
constexpr char DefaultConfigToml[] =
    "# Specializations\n"
    "#\n"
    "# A specialization system: a \"Specialization\" entry on the character sheet\n"
    "# opens a specialization window where the character spends specialization\n"
    "# points on perks. Learning a perk writes the perk's stat on the character,\n"
    "# saved with it. The perks do nothing by themselves: give their stats a\n"
    "# meaning in softcode or in other plugins. The plugin also adds Magic and\n"
    "# Physical resistance rows to the character sheet.\n"
    "#\n"
    "# Needs D2RLoader 1.3.1 (on any other build the plugin installs nothing), on\n"
    "# the host and every client, the same version. Mouse and keyboard only.\n"
    "# README.md, shipped with the plugin, is the full setup guide for modders.\n"
    "#\n"
    "# Rules\n"
    "#   - Points are the character's own value of points_stat: what a misc.txt\n"
    "#     pSpell, a quest or another plugin adds. Points granted by items do not\n"
    "#     count. Learning a perk costs 1 point.\n"
    "#   - A perk is learned when the character's own value of its stat is above\n"
    "#     0. Learning writes perk_value into it.\n"
    "#   - The first perk picks the specialization: the others are locked.\n"
    "#   - Rows are taken in order, one perk per row.\n"
    "#   - Shift + click unlearns a perk of the deepest learned row only, so the\n"
    "#     rows stay taken in order (see respec_stat below).\n"
    "#   - The host checks every rule again before it writes anything, so the\n"
    "#     host and every client need this plugin.\n"
    "#\n"
    "# itemstatcost.txt rows (points_stat, every perk stat, respec_stat and\n"
    "# unlock_stat)\n"
    "#   - CSvBits above 0. Required: D2RLoader's extended character save writes a\n"
    "#     character's own stat only then. 8 holds 0 to 255.\n"
    "#   - Saved = 1. Required: the game sends a change of the character's own\n"
    "#     value to its client only for rows with Saved = 1. Without it the host\n"
    "#     learns the perk but the window and the sheet never see it.\n"
    "#   - CSvParam blank: the stats use layer 0.\n"
    "#   - Add new rows at the end of the file. Changing CSvBits later stops\n"
    "#     existing characters from loading.\n"
    "#   The plugin log reports this check every time the tables load. A\n"
    "#   specialization with a missing or unsaved perk row cannot be learned, and\n"
    "#   a respec_stat without a saved row turns unlearning off.\n"
    "#\n"
    "# Character sheet (characterstatspanelhd.json, keyboard/mouse)\n"
    "#   All four widgets are direct children of the panel's root.\n"
    "#   MagicText, PhysicalText    TextBoxWidgets the plugin fills every frame:\n"
    "#     Magic = magicresist, capped at 75 + maxmagicresist and at most 95.\n"
    "#     Physical = damageresist, capped at the game's physical cap (50).\n"
    "#     Same floor, format and colours as the four stock resistance rows (gold\n"
    "#     at the cap, red below 0). No difficulty penalty, as in the damage code.\n"
    "#   SpecializationButton       a ButtonWidget, six frames: + (normal, hovered,\n"
    "#                              pressed) while the character has points, then\n"
    "#                              - (normal, hovered, pressed) once it learned a\n"
    "#                              perk and has no point; hidden otherwise. Its\n"
    "#                              click sends PanelManager:OpenPanel:SpecializationsOpen.\n"
    "#   SpecializationTitle        shown with the button\n"
    "#\n"
    "# HUD (hudpanelhd.json)\n"
    "#   SpecializationAlert        a button the plugin shows while the character\n"
    "#                              has points, the window is closed and the\n"
    "#                              character sheet is closed. Like the stock New\n"
    "#                              Stats and New Skill alerts, it stays hidden\n"
    "#                              once the window has been opened, until the\n"
    "#                              character gets a new point or a new game\n"
    "#                              starts. Its click sends\n"
    "#                              PanelManager:OpenPanel:SpecializationsOpen.\n"
    "#\n"
    "# Specialization window\n"
    "#   The layout is built into the plugin: Eastern Sun Resurrected's window,\n"
    "#   4 specializations of 5 rows of 3 perks (README.md lists the art and the\n"
    "#   strings it needs). SpecializationPanelhd.json, shipped with the plugin,\n"
    "#   is that layout. A mod can replace it with its own\n"
    "#   data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json\n"
    "#   (root type Panel, root name celestialrayone.specializations/SpecializationPanel).\n"
    "#   Every text in it must be a full reference such as @d2r:Key (strings from\n"
    "#   the mod's own string files are d2r: strings), or D2RLoader refuses it.\n"
    "#   Widgets (s = specialization, r = row, p = perk, all from 1):\n"
    "#     Tab<s>, TabActive<s>, TabChosen<s>, TabLocked<s>, Page<s>\n"
    "#     inside Page<s> (images only): Glow<r>_<p>, Learned<r>_<p>,\n"
    "#     Locked<r>_<p>, and the perk icons (Icon<r>_<p>, left as the layout has\n"
    "#     them)\n"
    "#     in the window, shared by every page: Hover<r>_<p> (hover area; a click\n"
    "#     on it learns, Shift + click unlearns), Perk<r>_<p> (optional, shown\n"
    "#     only where a click does something), Tip<r>_<p> (tooltip: TipBack1 to\n"
    "#     TipBack14 for 1 to 14 text lines, TipTitle, TipText, TipHint1 to\n"
    "#     TipHint13 for the gray unlearn line), PointsAvailable (shows\n"
    "#     @d2r:SpecializationsPointsAvailable, %d the unspent points)\n"
    "#   Click messages, PanelManager:OpenPanel:<text>:\n"
    "#     SpecializationsOpen / SpecializationsClose / SpecializationsTab<s> /\n"
    "#     SpecializationsPerk.<r>.<p> (the specialization on screen)\n"
    "#   The plugin breaks the tooltip text into lines for a TipText 672 units\n"
    "#   wide at $XMediumFontSize, 42 units a line, and shows the TipBack of that\n"
    "#   many lines.\n"
    "#   Perk icons: one sprite sheet per specialization, one frame per perk in\n"
    "#   row order (row 1 perks 1-3 are frames 0-2, row 2 is 3-5, ...), each frame\n"
    "#   the size of a skill icon. Each Icon<r>_<p> image points at its frame.\n"
    "#   Limits: 8 specializations, 10 rows, 5 perks per row.\n"
    "#\n"
    "# Console command\n"
    "#   specializations   status, the itemstatcost check, your points and perks,\n"
    "#                     the live resistance numbers and counters.\n"
    "\n"
    "[specializations]\n"
    "\n"
    "# The stat rows in this file are examples: set every row to your own.\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# itemstatcost.txt row of the specialization points.\n"
    "points_stat = 1000\n"
    "\n"
    "# Value written into a perk's stat when it is learned.\n"
    "perk_value = 1\n"
    "\n"
    "# Perks per row, 1 to 5. The rows of a specialization are its stats split\n"
    "# in groups of this size.\n"
    "perks_per_row = 3\n"
    "\n"
    "# Unlearning: Shift + click on a learned perk while the character has at\n"
    "# least unlearn_cost of respec_stat (its own value). The perk's stat goes\n"
    "# back to 0, the point is refunded and unlearn_cost is taken from\n"
    "# respec_stat. Only a perk of the deepest learned row can be unlearned, so\n"
    "# the rows stay taken in order; the freed row can then be learned again.\n"
    "# respec_stat needs a saved itemstatcost row. -1 turns unlearning off.\n"
    "respec_stat = -1\n"
    "unlearn_cost = 5\n"
    "\n"
    "# Gray line under every perk's description, so the cost of unlearning is\n"
    "# always known. %d is replaced by unlearn_cost.\n"
    "unlearn_hint = \"@d2r:SpecializationsUnlearnHint\"\n"
    "\n"
    "# Sounds (sounds.txt names) played when a learn or unlearn click goes out.\n"
    "# cursor_stat_point_one_hd is the game's sound for spending a stat point.\n"
    "# An empty text plays nothing.\n"
    "learn_sound = \"cursor_stat_point_one_hd\"\n"
    "unlearn_sound = \"cursor_stat_point_one_hd\"\n"
    "\n"
    "# Points by character level. A character is unlocked once it owns any\n"
    "# point, unspent or spent on a perk (the specialization token's pSpell gives\n"
    "# the first ones). From then on the plugin keeps the character's points at\n"
    "# unlock_points plus one for every level in point_levels it has reached: it\n"
    "# adds the missing points when the character reaches such a level, and when\n"
    "# it enters a game. The token should give unlock_points plus one for every\n"
    "# point level already passed (3 at level 63 with these values), so the total\n"
    "# never goes past unlock_points + the number of point levels (5 here).\n"
    "# Points from other sources count too and are never taken away.\n"
    "point_levels = [45, 60, 75, 90]\n"
    "unlock_points = 1\n"
    "\n"
    "# The stat the specialization token raises when it is used (its cap stat).\n"
    "# With it set, a character is unlocked once that stat is 1 or more, even if\n"
    "# its points were lost; -1 counts any owned point as unlocked instead.\n"
    "unlock_stat = -1\n"
    "\n"
    "# Hard cap on owned points (unspent plus learned perks), whatever gives them:\n"
    "# the host takes unspent points above it away when the character enters a\n"
    "# game, levels up, gains points or sends a learn or unlearn click.\n"
    "max_points = 5\n"
    "\n"
    "# One block per specialization, in tab order (the first block is Tab1), up\n"
    "# to 8.\n"
    "# name: for the log and the console only; the tab text is in the layout.\n"
    "# stats: itemstatcost.txt rows of the perks, row by row, 1 to 10 full rows.\n"
    "# titles, descriptions: one text per perk, in the same order as stats.\n"
    "#   \"@namespace:Key\" is a string: @d2r:Key for strings in the mod's own\n"
    "#   string files. Anything else is shown as written. \\n starts a new line.\n"
    "#   Both lists are optional; a list of another length than stats is\n"
    "#   ignored, and a missing key shows the key itself.\n"
    "\n"
    "[[specialization]]\n"
    "name = \"Alchemist\"\n"
    "stats = [\n"
    "    1001, 1002, 1003,\n"
    "    1004, 1005, 1006,\n"
    "    1007, 1008, 1009,\n"
    "    1010, 1011, 1012,\n"
    "    1013, 1014, 1015,\n"
    "]\n"
    "titles = [\n"
    "    \"@d2r:SpecializationsAlchemist1_1Title\", \"@d2r:SpecializationsAlchemist1_2Title\", \"@d2r:SpecializationsAlchemist1_3Title\",\n"
    "    \"@d2r:SpecializationsAlchemist2_1Title\", \"@d2r:SpecializationsAlchemist2_2Title\", \"@d2r:SpecializationsAlchemist2_3Title\",\n"
    "    \"@d2r:SpecializationsAlchemist3_1Title\", \"@d2r:SpecializationsAlchemist3_2Title\", \"@d2r:SpecializationsAlchemist3_3Title\",\n"
    "    \"@d2r:SpecializationsAlchemist4_1Title\", \"@d2r:SpecializationsAlchemist4_2Title\", \"@d2r:SpecializationsAlchemist4_3Title\",\n"
    "    \"@d2r:SpecializationsAlchemist5_1Title\", \"@d2r:SpecializationsAlchemist5_2Title\", \"@d2r:SpecializationsAlchemist5_3Title\",\n"
    "]\n"
    "descriptions = [\n"
    "    \"@d2r:SpecializationsAlchemist1_1Desc\", \"@d2r:SpecializationsAlchemist1_2Desc\", \"@d2r:SpecializationsAlchemist1_3Desc\",\n"
    "    \"@d2r:SpecializationsAlchemist2_1Desc\", \"@d2r:SpecializationsAlchemist2_2Desc\", \"@d2r:SpecializationsAlchemist2_3Desc\",\n"
    "    \"@d2r:SpecializationsAlchemist3_1Desc\", \"@d2r:SpecializationsAlchemist3_2Desc\", \"@d2r:SpecializationsAlchemist3_3Desc\",\n"
    "    \"@d2r:SpecializationsAlchemist4_1Desc\", \"@d2r:SpecializationsAlchemist4_2Desc\", \"@d2r:SpecializationsAlchemist4_3Desc\",\n"
    "    \"@d2r:SpecializationsAlchemist5_1Desc\", \"@d2r:SpecializationsAlchemist5_2Desc\", \"@d2r:SpecializationsAlchemist5_3Desc\",\n"
    "]\n"
    "\n"
    "[[specialization]]\n"
    "name = \"Blacksmith\"\n"
    "stats = [\n"
    "    1016, 1017, 1018,\n"
    "    1019, 1020, 1021,\n"
    "    1022, 1023, 1024,\n"
    "    1025, 1026, 1027,\n"
    "    1028, 1029, 1030,\n"
    "]\n"
    "titles = [\n"
    "    \"@d2r:SpecializationsBlacksmith1_1Title\", \"@d2r:SpecializationsBlacksmith1_2Title\", \"@d2r:SpecializationsBlacksmith1_3Title\",\n"
    "    \"@d2r:SpecializationsBlacksmith2_1Title\", \"@d2r:SpecializationsBlacksmith2_2Title\", \"@d2r:SpecializationsBlacksmith2_3Title\",\n"
    "    \"@d2r:SpecializationsBlacksmith3_1Title\", \"@d2r:SpecializationsBlacksmith3_2Title\", \"@d2r:SpecializationsBlacksmith3_3Title\",\n"
    "    \"@d2r:SpecializationsBlacksmith4_1Title\", \"@d2r:SpecializationsBlacksmith4_2Title\", \"@d2r:SpecializationsBlacksmith4_3Title\",\n"
    "    \"@d2r:SpecializationsBlacksmith5_1Title\", \"@d2r:SpecializationsBlacksmith5_2Title\", \"@d2r:SpecializationsBlacksmith5_3Title\",\n"
    "]\n"
    "descriptions = [\n"
    "    \"@d2r:SpecializationsBlacksmith1_1Desc\", \"@d2r:SpecializationsBlacksmith1_2Desc\", \"@d2r:SpecializationsBlacksmith1_3Desc\",\n"
    "    \"@d2r:SpecializationsBlacksmith2_1Desc\", \"@d2r:SpecializationsBlacksmith2_2Desc\", \"@d2r:SpecializationsBlacksmith2_3Desc\",\n"
    "    \"@d2r:SpecializationsBlacksmith3_1Desc\", \"@d2r:SpecializationsBlacksmith3_2Desc\", \"@d2r:SpecializationsBlacksmith3_3Desc\",\n"
    "    \"@d2r:SpecializationsBlacksmith4_1Desc\", \"@d2r:SpecializationsBlacksmith4_2Desc\", \"@d2r:SpecializationsBlacksmith4_3Desc\",\n"
    "    \"@d2r:SpecializationsBlacksmith5_1Desc\", \"@d2r:SpecializationsBlacksmith5_2Desc\", \"@d2r:SpecializationsBlacksmith5_3Desc\",\n"
    "]\n"
    "\n"
    "[[specialization]]\n"
    "name = \"Jeweler\"\n"
    "stats = [\n"
    "    1031, 1032, 1033,\n"
    "    1034, 1035, 1036,\n"
    "    1037, 1038, 1039,\n"
    "    1040, 1041, 1042,\n"
    "    1043, 1044, 1045,\n"
    "]\n"
    "titles = [\n"
    "    \"@d2r:SpecializationsJeweler1_1Title\", \"@d2r:SpecializationsJeweler1_2Title\", \"@d2r:SpecializationsJeweler1_3Title\",\n"
    "    \"@d2r:SpecializationsJeweler2_1Title\", \"@d2r:SpecializationsJeweler2_2Title\", \"@d2r:SpecializationsJeweler2_3Title\",\n"
    "    \"@d2r:SpecializationsJeweler3_1Title\", \"@d2r:SpecializationsJeweler3_2Title\", \"@d2r:SpecializationsJeweler3_3Title\",\n"
    "    \"@d2r:SpecializationsJeweler4_1Title\", \"@d2r:SpecializationsJeweler4_2Title\", \"@d2r:SpecializationsJeweler4_3Title\",\n"
    "    \"@d2r:SpecializationsJeweler5_1Title\", \"@d2r:SpecializationsJeweler5_2Title\", \"@d2r:SpecializationsJeweler5_3Title\",\n"
    "]\n"
    "descriptions = [\n"
    "    \"@d2r:SpecializationsJeweler1_1Desc\", \"@d2r:SpecializationsJeweler1_2Desc\", \"@d2r:SpecializationsJeweler1_3Desc\",\n"
    "    \"@d2r:SpecializationsJeweler2_1Desc\", \"@d2r:SpecializationsJeweler2_2Desc\", \"@d2r:SpecializationsJeweler2_3Desc\",\n"
    "    \"@d2r:SpecializationsJeweler3_1Desc\", \"@d2r:SpecializationsJeweler3_2Desc\", \"@d2r:SpecializationsJeweler3_3Desc\",\n"
    "    \"@d2r:SpecializationsJeweler4_1Desc\", \"@d2r:SpecializationsJeweler4_2Desc\", \"@d2r:SpecializationsJeweler4_3Desc\",\n"
    "    \"@d2r:SpecializationsJeweler5_1Desc\", \"@d2r:SpecializationsJeweler5_2Desc\", \"@d2r:SpecializationsJeweler5_3Desc\",\n"
    "]\n"
    "\n"
    "[[specialization]]\n"
    "name = \"Archaeologist\"\n"
    "stats = [\n"
    "    1046, 1047, 1048,\n"
    "    1049, 1050, 1051,\n"
    "    1052, 1053, 1054,\n"
    "    1055, 1056, 1057,\n"
    "    1058, 1059, 1060,\n"
    "]\n"
    "titles = [\n"
    "    \"@d2r:SpecializationsArchaeologist1_1Title\", \"@d2r:SpecializationsArchaeologist1_2Title\", \"@d2r:SpecializationsArchaeologist1_3Title\",\n"
    "    \"@d2r:SpecializationsArchaeologist2_1Title\", \"@d2r:SpecializationsArchaeologist2_2Title\", \"@d2r:SpecializationsArchaeologist2_3Title\",\n"
    "    \"@d2r:SpecializationsArchaeologist3_1Title\", \"@d2r:SpecializationsArchaeologist3_2Title\", \"@d2r:SpecializationsArchaeologist3_3Title\",\n"
    "    \"@d2r:SpecializationsArchaeologist4_1Title\", \"@d2r:SpecializationsArchaeologist4_2Title\", \"@d2r:SpecializationsArchaeologist4_3Title\",\n"
    "    \"@d2r:SpecializationsArchaeologist5_1Title\", \"@d2r:SpecializationsArchaeologist5_2Title\", \"@d2r:SpecializationsArchaeologist5_3Title\",\n"
    "]\n"
    "descriptions = [\n"
    "    \"@d2r:SpecializationsArchaeologist1_1Desc\", \"@d2r:SpecializationsArchaeologist1_2Desc\", \"@d2r:SpecializationsArchaeologist1_3Desc\",\n"
    "    \"@d2r:SpecializationsArchaeologist2_1Desc\", \"@d2r:SpecializationsArchaeologist2_2Desc\", \"@d2r:SpecializationsArchaeologist2_3Desc\",\n"
    "    \"@d2r:SpecializationsArchaeologist3_1Desc\", \"@d2r:SpecializationsArchaeologist3_2Desc\", \"@d2r:SpecializationsArchaeologist3_3Desc\",\n"
    "    \"@d2r:SpecializationsArchaeologist4_1Desc\", \"@d2r:SpecializationsArchaeologist4_2Desc\", \"@d2r:SpecializationsArchaeologist4_3Desc\",\n"
    "    \"@d2r:SpecializationsArchaeologist5_1Desc\", \"@d2r:SpecializationsArchaeologist5_2Desc\", \"@d2r:SpecializationsArchaeologist5_3Desc\",\n"
    "]\n";

struct SpecConfig {
    std::string               name;
    std::vector<std::int32_t> stats;
    std::vector<std::string>  titles;        // one per perk, row order; may be empty
    std::vector<std::string>  descriptions;  // one per perk, row order; may be empty
    std::size_t               rows{};
};

struct Config {
    bool                    enabled{ true };
    std::int32_t            pointsStat{ -1 };
    std::int32_t            perkValue{ 1 };
    std::int32_t            perksPerRow{ 3 };
    std::int32_t            respecStat{ -1 };
    std::int32_t            unlearnCost{ 5 };
    std::string             unlearnHint{ "@d2r:SpecializationsUnlearnHint" };
    std::string             learnSound{ "cursor_stat_point_one_hd" };
    std::vector<std::int32_t> pointLevels{ 45, 60, 75, 90 };
    std::int32_t            unlockPoints{ 1 };
    std::int32_t            unlockStat{ -1 };
    std::int32_t            maxPoints{ 5 };
    std::string             unlearnSound{ "cursor_stat_point_one_hd" };
    std::vector<SpecConfig> specs;
};

Config      Settings;
std::string ConfigProblems;

void Problem(std::string_view text) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(text);
}

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r'
            || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r'
            || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

// Drops a trailing # comment that is not inside a quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (quoted && line[i] == '\\') { ++i; continue; }
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

// True when the text holds a ']' outside quoted strings.
auto ClosesArray(std::string_view text) noexcept -> bool {
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (quoted && text[i] == '\\') { ++i; continue; }
        if (text[i] == '"') quoted = !quoted;
        if (text[i] == ']' && !quoted) return true;
    }
    return false;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int64_t minimum, std::int64_t maximum, std::int32_t& out) noexcept -> bool {
    value = Trim(value);
    if (value.empty()) return false;
    std::int64_t result   = 0;
    std::size_t  i        = 0;
    bool         negative = false;
    if (value[0] == '-' || value[0] == '+') {
        negative = value[0] == '-';
        i = 1;
    }
    if (i >= value.size()) return false;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') continue;
        if (value[i] < '0' || value[i] > '9') return false;
        result = result * 10 + (value[i] - '0');
        if (result > 4'000'000'000LL) return false;
    }
    if (negative) result = -result;
    if (result < minimum || result > maximum) return false;
    out = static_cast<std::int32_t>(result);
    return true;
}

auto ParseString(std::string_view value, std::string& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.substr(1, value.size() - 2));
    return true;
}

auto ParseIntArray(std::string_view value, std::vector<std::int32_t>& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    value = value.substr(1, value.size() - 2);
    out.clear();
    while (!value.empty()) {
        const std::size_t comma = value.find(',');
        const std::string_view item = Trim(value.substr(0, comma));
        value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
        if (item.empty()) continue;
        std::int32_t number = 0;
        if (!ParseInt(item, 0, 32767, number)) return false;
        out.push_back(number);
    }
    return true;
}

// A list of quoted strings: ["a", "b\\n c"]. Escapes: \\" \\\\ \\n \\t.
auto ParseStringArray(std::string_view value, std::vector<std::string>& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    out.clear();
    std::size_t i = 1;
    const std::size_t end = value.size() - 1;
    while (i < end) {
        const char c = value[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++i; continue; }
        if (c != '"') return false;
        std::string item;
        ++i;
        bool closed = false;
        while (i < end) {
            const char d = value[i++];
            if (d == '"') { closed = true; break; }
            if (d == '\\' && i < end) {
                const char e = value[i++];
                item += e == 'n' ? '\n' : (e == 't' ? '\t' : e);
                continue;
            }
            item += d;
        }
        if (!closed) return false;
        out.push_back(std::move(item));
    }
    return true;
}

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) {
    char note[200];
    const auto bad = [&](const char* what) {
        std::snprintf(note, sizeof(note), "[%.*s] %.*s: %s", static_cast<int>(section.size()), section.data(),
            static_cast<int>(key.size()), key.data(), what);
        Problem(note);
    };
    if (section == "specializations") {
        if (key == "enabled") {
            if (!ParseBool(value, Settings.enabled)) bad("expected true or false, default kept");
        } else if (key == "points_stat") {
            if (!ParseInt(value, 0, 32767, Settings.pointsStat)) bad("expected 0 to 32767");
        } else if (key == "perk_value") {
            if (!ParseInt(value, 1, 2'147'483'647LL, Settings.perkValue)) bad("expected 1 or more, default kept");
        } else if (key == "perks_per_row") {
            if (!ParseInt(value, 1, static_cast<std::int64_t>(MaxPerksPerRow), Settings.perksPerRow)) {
                bad("expected 1 to 5, default kept");
            }
        } else if (key == "respec_stat") {
            if (!ParseInt(value, -1, 32767, Settings.respecStat)) bad("expected -1 to 32767, default kept");
        } else if (key == "unlearn_cost") {
            if (!ParseInt(value, 1, 1'000'000, Settings.unlearnCost)) bad("expected 1 or more, default kept");
        } else if (key == "unlearn_hint") {
            if (!ParseString(value, Settings.unlearnHint)) bad("expected a quoted text");
        } else if (key == "point_levels") {
            if (!ParseIntArray(value, Settings.pointLevels)) bad("expected a list of character levels");
        } else if (key == "unlock_points") {
            if (!ParseInt(value, 0, 1000, Settings.unlockPoints)) bad("expected 0 to 1000, default kept");
        } else if (key == "unlock_stat") {
            if (!ParseInt(value, -1, 32767, Settings.unlockStat)) bad("expected -1 to 32767, default kept");
        } else if (key == "max_points") {
            if (!ParseInt(value, 0, 1000, Settings.maxPoints)) bad("expected 0 to 1000, default kept");
        } else if (key == "learn_sound") {
            if (!ParseString(value, Settings.learnSound)) bad("expected a quoted sounds.txt name");
        } else if (key == "unlearn_sound") {
            if (!ParseString(value, Settings.unlearnSound)) bad("expected a quoted sounds.txt name");
        }
        return;
    }
    if (section == "specialization" && !Settings.specs.empty()) {
        SpecConfig& spec = Settings.specs.back();
        if (key == "name") {
            if (!ParseString(value, spec.name)) bad("expected a quoted name");
        } else if (key == "stats") {
            if (!ParseIntArray(value, spec.stats)) bad("expected a list of itemstatcost rows (0 to 32767)");
        } else if (key == "titles") {
            if (!ParseStringArray(value, spec.titles)) bad("expected a list of quoted texts");
        } else if (key == "descriptions") {
            if (!ParseStringArray(value, spec.descriptions)) bad("expected a list of quoted texts");
        }
    }
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    std::string      pendingKey;
    std::string      pendingValue;
    bool             collecting = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (collecting) {
            pendingValue += ' ';
            pendingValue.append(line);
            if (ClosesArray(pendingValue)) {
                collecting = false;
                ApplyConfigLine(section, pendingKey, pendingValue);
            }
            continue;
        }
        if (line.empty()) continue;
        if (line.size() >= 4 && line.substr(0, 2) == "[[" && line.substr(line.size() - 2) == "]]") {
            section = Trim(line.substr(2, line.size() - 4));
            if (section == "specialization") {
                if (Settings.specs.size() < MaxSpecializations) {
                    Settings.specs.emplace_back();
                } else {
                    Problem("more than 8 [[specialization]] blocks; the extra ones are ignored");
                    section = "ignored";
                }
            }
            continue;
        }
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (!value.empty() && value.front() == '[' && !ClosesArray(value)) {
            collecting = true;
            pendingKey.assign(key);
            pendingValue.assign(value);
            continue;
        }
        ApplyConfigLine(section, key, value);
    }
}

// Checks the parsed config. Returns false when the plugin cannot run with it.
auto ValidateConfig() -> bool {
    bool ok = true;
    char note[200];
    if (Settings.maxPoints < Settings.unlockPoints + static_cast<std::int32_t>(Settings.pointLevels.size())) {
        Problem("max_points is lower than unlock_points plus the point levels; the cap wins");
    }
    if (Settings.pointsStat < 0) {
        Problem("points_stat is missing");
        ok = false;
    }
    if (Settings.specs.empty()) {
        Problem("no [[specialization]] block");
        ok = false;
    }
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        SpecConfig& spec = Settings.specs[s];
        if (spec.name.empty()) spec.name = "specialization " + std::to_string(s + 1);
        const std::size_t perRow = static_cast<std::size_t>(Settings.perksPerRow);
        if (spec.stats.empty() || spec.stats.size() % perRow != 0 || spec.stats.size() / perRow > MaxRows) {
            std::snprintf(note, sizeof(note), "%s: stats must hold 1 to %zu full rows of %zu perks (it has %zu stats)",
                spec.name.c_str(), MaxRows, perRow, spec.stats.size());
            Problem(note);
            ok = false;
            continue;
        }
        spec.rows = spec.stats.size() / perRow;
        for (auto* list : { &spec.titles, &spec.descriptions }) {
            if (!list->empty() && list->size() != spec.stats.size()) {
                std::snprintf(note, sizeof(note), "%s: %s has %zu entries but stats has %zu; it is ignored",
                    spec.name.c_str(), list == &spec.titles ? "titles" : "descriptions", list->size(), spec.stats.size());
                Problem(note);
                list->clear();
            }
        }
    }
    return ok;
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Specializations: the config file could not be created; using the defaults.");
        ParseConfig(DefaultConfigToml);
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            ParseConfig(buffer);
        } else {
            Context->LogWarn("Specializations: the config file could not be read; using the defaults.");
            ParseConfig(DefaultConfigToml);
        }
    }
}

auto PerkStat(std::size_t s, std::size_t r, std::size_t p) noexcept -> std::int32_t {
    return Settings.specs[s].stats[r * static_cast<std::size_t>(Settings.perksPerRow) + p];
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

constexpr int NoChoice      = -1;
constexpr int SeveralChosen = -2;

// Whether a specialization's perk stats (and the respec stat) exist and are
// saved in itemstatcost.txt; known after the check at game start.
constexpr std::uint8_t StateUnknown = 0;
constexpr std::uint8_t StateUsable  = 1;
constexpr std::uint8_t StateMissing = 2;
std::array<std::atomic<std::uint8_t>, MaxSpecializations> SpecState{};
std::atomic<std::uint8_t>                                 RespecState{};

auto SpecUsable(std::size_t s) noexcept -> bool {
    return s < MaxSpecializations && SpecState[s].load(std::memory_order_relaxed) != StateMissing;
}

struct Snapshot {
    std::int32_t points{};
    std::int32_t respec{};
    int          chosen{ NoChoice };
    bool         anyLearned{};
    std::array<std::array<std::array<std::uint8_t, MaxPerksPerRow>, MaxRows>, MaxSpecializations> learned{};
    std::array<std::uint8_t, MaxSpecializations> hasLearned{};
};

auto ReadSnapshot(void* unit) noexcept -> Snapshot {
    Snapshot snap{};
    snap.points = ReadOwnStat(unit, Settings.pointsStat, 0);
    if (Settings.respecStat >= 0) snap.respec = ReadOwnStat(unit, Settings.respecStat, 0);
    int specsWithPerks = 0;
    const std::size_t perRow = static_cast<std::size_t>(Settings.perksPerRow);
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        if (!SpecUsable(s)) continue;  // its stats are not in itemstatcost.txt
        for (std::size_t r = 0; r < Settings.specs[s].rows; ++r) {
            for (std::size_t p = 0; p < perRow; ++p) {
                if (ReadOwnStat(unit, PerkStat(s, r, p), 0) > 0) {
                    snap.learned[s][r][p] = 1;
                    snap.hasLearned[s]    = 1;
                }
            }
        }
        if (snap.hasLearned[s] != 0) {
            ++specsWithPerks;
            snap.chosen = static_cast<int>(s);
        }
    }
    if (specsWithPerks > 1) snap.chosen = SeveralChosen;
    snap.anyLearned = specsWithPerks > 0;
    return snap;
}

auto RowTaken(const Snapshot& snap, std::size_t s, std::size_t r) noexcept -> bool {
    for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
        if (snap.learned[s][r][p] != 0) return true;
    }
    return false;
}

auto CanLearn(const Snapshot& snap, std::size_t s, std::size_t r, std::size_t p) noexcept -> bool {
    if (s >= Settings.specs.size() || r >= Settings.specs[s].rows
            || p >= static_cast<std::size_t>(Settings.perksPerRow)) {
        return false;
    }
    if (!SpecUsable(s) || snap.points <= 0 || snap.learned[s][r][p] != 0) return false;
    if (snap.chosen == SeveralChosen || (snap.chosen != NoChoice && snap.chosen != static_cast<int>(s))) return false;
    for (std::size_t above = 0; above < r; ++above) {
        if (!RowTaken(snap, s, above)) return false;
    }
    return !RowTaken(snap, s, r);
}

auto CanUnlearn(const Snapshot& snap, std::size_t s, std::size_t r, std::size_t p) noexcept -> bool {
    if (s >= Settings.specs.size() || r >= Settings.specs[s].rows
            || p >= static_cast<std::size_t>(Settings.perksPerRow)) {
        return false;
    }
    if (Settings.respecStat < 0 || RespecState.load(std::memory_order_relaxed) == StateMissing) return false;
    if (snap.learned[s][r][p] == 0 || snap.respec < Settings.unlearnCost) return false;
    // Only the deepest learned row can be unlearned, so rows always stay taken in order.
    for (std::size_t below = r + 1; below < Settings.specs[s].rows; ++below) {
        if (RowTaken(snap, s, below)) return false;
    }
    return true;
}

auto ShiftDown() noexcept -> bool {
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

auto LocalPlayer() noexcept -> void* {
    return PlayerFromIndex(LocalPlayerIndex());
}

// ---------------------------------------------------------------------------
// Native widgets (UI thread)
// ---------------------------------------------------------------------------

auto IsType(void* widget, const char* typeName) noexcept -> bool {
    auto** table = Read<void**>(widget, 0);
    void*  desc  = reinterpret_cast<TypeDescFn>(table[TypeDescSlot])(widget);
    const std::size_t length = std::strlen(typeName) + 1;
    for (int depth = 0; desc != nullptr && depth < 32; ++depth) {
        const auto* name = Read<const char*>(desc, TypeDescNameOffset);
        if (name != nullptr && std::strncmp(name, typeName, length) == 0) return true;
        desc = Read<void*>(desc, TypeDescParentOffset);
    }
    return false;
}

void SetNativeVisible(void* widget, bool visible) noexcept {
    auto** table = Read<void**>(widget, 0);
    reinterpret_cast<SetVisibleFn>(table[SetVisibleSlot])(widget, visible ? 1U : 0U);
}

void SetNativeText(void* box, const std::string& text) noexcept {
    const StringView view{ text.c_str(), text.size() };
    NativeSetText(box, &view);
}

void SetColorIndex(void* box, std::int32_t index) noexcept {
    alignas(16) std::uint8_t color[16]{};
    const void* source = ColorByIndex(color, index, ColorMode);
    std::memcpy(static_cast<std::uint8_t*>(box) + TextColorOffset, source != nullptr ? source : color, 16);
}

void SetDefaultColor(void* panel, void* box) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(box) + TextColorOffset,
        static_cast<const std::uint8_t*>(panel) + PanelDefaultColorOffset, 16);
}

// d2r:CharacterSheetResistanceStat, refreshed every 2 seconds.
std::string   ResistFormatText;
std::uint64_t ResistFormatReadMs{};

auto ResistFormat() -> const std::string& {
    const std::uint64_t now = GetTickCount64();
    if (ResistFormatReadMs != 0 && now - ResistFormatReadMs < 2000) return ResistFormatText;
    ResistFormatReadMs = now;
    ResistFormatText.clear();
    if (TextSvc == nullptr) return ResistFormatText;
    char           buffer[128]{};
    std::uint32_t  required = 0;
    if (TextSvc->getStringByKey(Context, "d2r:CharacterSheetResistanceStat", buffer, sizeof(buffer), &required)
            == D2RL::Localization::Result::Success) {
        ResistFormatText.assign(buffer);
    }
    return ResistFormatText;
}

auto FormatResist(std::int32_t value) -> std::string {
    const std::string number = std::to_string(value);
    if (ResistFormatOn() == 0) return number;
    const std::string& format = ResistFormat();
    if (format.empty()) return number + "%";
    std::string out;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%' && i + 1 < format.size()) {
            const char next = format[i + 1];
            if (next == '%') { out += '%'; ++i; continue; }
            if (next == 'd' || next == 'i') { out += number; ++i; continue; }
        }
        out += format[i];
    }
    return out;
}

struct LiveResistNumbers {
    std::int32_t floor;
    std::int32_t capBase;
    std::int32_t ceiling;
    std::int32_t physicalCap;
};

auto LiveNumbers() noexcept -> LiveResistNumbers {
    return { ReadRva<std::int32_t>(SheetFloorOperand), ReadRva<std::int8_t>(SheetCapBaseOperand),
        ReadRva<std::int32_t>(SheetCeilingOperand), ReadRva<std::int32_t>(PhysicalCapOperand) };
}

void FillResist(void* panel, void* player, const char* name, bool physical) {
    void* box = FindChild(panel, name);
    if (box == nullptr || !IsType(box, "TextBoxWidget")) return;
    const LiveResistNumbers live = LiveNumbers();
    const std::int32_t value = ReadStat(player, physical ? StatPhysicalResist : StatMagicResist, 0);
    std::int32_t cap = live.physicalCap;
    if (!physical) {
        const std::int64_t raw = static_cast<std::int64_t>(ReadStat(player, StatMaxMagicResist, 0)) + live.capBase;
        cap = static_cast<std::int32_t>(std::min<std::int64_t>(raw, live.ceiling));
    }
    const std::int32_t shown = std::min(std::max(value, live.floor), cap);
    SetNativeText(box, FormatResist(shown));
    if (shown >= cap) {
        SetColorIndex(box, ColorIndexGold);
    } else if (shown < 0) {
        SetColorIndex(box, ColorIndexRed);
    } else {
        SetDefaultColor(panel, box);
    }
}

// The sheet's own widgets: visibility is set only when it changes. Setting it on
// every update put a button back to its normal look each frame, so a pressed
// button never showed its pressed frame. The record is dropped whenever the
// sheet is a different panel or was closed in between.
std::unordered_map<void*, std::uint8_t> SheetVisible;
void*                                   SheetPanel{};

std::unordered_map<void*, int> SheetButtonLook;

void ShowChild(void* panel, const char* name, bool visible) noexcept;

// look: first frame of the look to show (0 the +, 3 the -), or -1 for none.
void UpdateSheetButton(void* panel, int look) noexcept {
    void* const button = FindChild(panel, "SpecializationButton");
    if (button == nullptr || !IsType(button, "ButtonWidget")) return;
    const auto it = SheetButtonLook.find(button);
    if (it != SheetButtonLook.end() && it->second == look) return;
    SheetButtonLook[button] = look;
    if (look < 0) {
        ShowChild(panel, "SpecializationButton", false);
        Write<std::uint8_t>(button, ButtonDisabledOffset, 1);  // the hidden spot takes no press
        return;
    }
    Write<std::uint32_t>(button, ButtonNormalFrameOffset, static_cast<std::uint32_t>(look));
    Write<std::uint32_t>(button, ButtonHoveredFrameOffset, static_cast<std::uint32_t>(look + 1));
    Write<std::uint32_t>(button, ButtonPressedFrameOffset, static_cast<std::uint32_t>(look + 2));
    Write<std::uint32_t>(button, ButtonDisabledFrameOffset, static_cast<std::uint32_t>(look));
    if (Read<std::uint8_t>(button, ButtonPressedOffset) == 0) {
        Write<std::uint32_t>(button, ButtonShownFrameOffset, static_cast<std::uint32_t>(look));
    }
    if (Read<std::uint8_t>(button, ButtonDisabledOffset) != 0 && ButtonEnable != nullptr) ButtonEnable(button);
    ShowChild(panel, "SpecializationButton", true);
}

void ShowChild(void* panel, const char* name, bool visible) noexcept {
    void* const widget = FindChild(panel, name);
    if (widget == nullptr) return;
    const std::uint8_t want = visible ? 1 : 0;
    const auto it = SheetVisible.find(widget);
    if (it != SheetVisible.end() && it->second == want) return;
    SetNativeVisible(widget, visible);
    SheetVisible[widget] = want;
}

// ---------------------------------------------------------------------------
// The window (SDK widgets, UI thread)
// ---------------------------------------------------------------------------

using WidgetHandle = D2RL::Widgets::WidgetHandle;

// The window's widgets are used natively: the window is found by name (as an
// ancestor of the hovered widget, or under the UI root), each widget the
// plugin changes is looked up once per window, and visibility and text are set
// only when they change.

struct CellWidgets {
    void* glow{};
    void* learned{};
    void* shade{};
};

struct PageWidgets {
    void* page{};
    std::array<std::array<CellWidgets, MaxPerksPerRow>, MaxRows> cells{};
};

constexpr std::size_t MaxTipLines = 14;

struct TipWidgets {
    void*                              group{};
    void*                              title{};
    void*                              text{};
    std::array<void*, MaxTipLines>     backs{};  // TipBack1..TipBack14: background for that many lines
    std::array<void*, MaxTipLines>     hints{};  // TipHint1..TipHint13: the gray line starting on that text line
};

struct WindowWidgets {
    void*                                                        window{};
    std::array<void*, MaxSpecializations>                        tabActive{}, tabLocked{}, tabChosen{};
    std::array<PageWidgets, MaxSpecializations>                  pages{};
    std::array<std::array<TipWidgets, MaxPerksPerRow>, MaxRows>  tips{};
    std::array<std::array<void*, MaxPerksPerRow>, MaxRows>       hover{};
    std::array<std::array<void*, MaxPerksPerRow>, MaxRows>       perk{};
    void*                                                        pointsAvailable{};
    std::size_t                                                  found{};
};

constexpr char PointsAvailableText[] = "@d2r:SpecializationsPointsAvailable";

WindowWidgets                           Native;
std::unordered_map<void*, std::uint8_t> NativeVisible;
std::unordered_map<void*, std::string>  NativeText;
void*                                   UiRoot{};
std::size_t                             ViewedSpec{};  // 0-based
std::atomic<std::uint64_t>              LastWindowTickMs{};

struct NodeRef {
    std::size_t r{}, p{};
    bool        valid{};
};

NodeRef ShownNode;

auto WidgetName(void* widget) noexcept -> const char* {
    return Read<const char*>(widget, WidgetNameOffset);
}

// Remembers the top of the widget tree from any live widget.
void NoteWidget(void* widget) noexcept {
    void* top = nullptr;
    for (int depth = 0; widget != nullptr && depth < 64; ++depth) {
        top    = widget;
        widget = Read<void*>(widget, WidgetParentOffset);
    }
    if (top != nullptr) UiRoot = top;
}

// The named widget among the children, then (depth > 0) among theirs.
auto FindNamed(void* widget, const char* name, int depth) noexcept -> void* {
    if (widget == nullptr) return nullptr;
    auto** const       children = Read<void**>(widget, WidgetChildrenOffset);
    const std::uint64_t count   = Read<std::uint64_t>(widget, WidgetChildCountOffset);
    if (children == nullptr || count == 0 || count > 4096) return nullptr;
    for (std::uint64_t i = 0; i < count; ++i) {
        const char* const childName = children[i] != nullptr ? WidgetName(children[i]) : nullptr;
        if (childName != nullptr && std::strcmp(childName, name) == 0) return children[i];
    }
    if (depth <= 0) return nullptr;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (void* const found = FindNamed(children[i], name, depth - 1); found != nullptr) return found;
    }
    return nullptr;
}

auto Child(void* parent, const std::string& name) noexcept -> void* {
    return parent != nullptr ? FindChild(parent, name.c_str()) : nullptr;
}

auto HoveredWidget() noexcept -> void* {
    if (UiState == nullptr) return nullptr;
    void* const ui = UiState();
    return ui != nullptr ? Read<void*>(ui, UiHoveredOffset) : nullptr;
}

// The open window: an ancestor of the hovered widget, else a child of the UI root.
auto LocateWindow() noexcept -> void* {
    void* w = HoveredWidget();
    if (w != nullptr) NoteWidget(w);
    for (int depth = 0; w != nullptr && depth < 64; ++depth) {
        const char* const name = WidgetName(w);
        if (name != nullptr && std::strcmp(name, PanelRootName) == 0) return w;
        w = Read<void*>(w, WidgetParentOffset);
    }
    return FindNamed(UiRoot, PanelRootName, 1);
}

auto TextBox(void* widget) noexcept -> void* {
    return widget != nullptr && IsType(widget, "TextBoxWidget") ? widget : nullptr;
}

void ResolveNative(void* window) {
    Native = {};
    NativeVisible.clear();
    NativeText.clear();
    ShownNode     = {};
    Native.window = window;
    std::size_t found = 0;
    const auto get = [&found](void* parent, const std::string& name) -> void* {
        void* const widget = Child(parent, name);
        found += widget != nullptr ? 1U : 0U;
        return widget;
    };
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        const std::string n = std::to_string(s + 1);
        Native.tabActive[s] = get(window, "TabActive" + n);
        Native.tabLocked[s] = get(window, "TabLocked" + n);
        Native.tabChosen[s] = get(window, "TabChosen" + n);
        PageWidgets& page = Native.pages[s];
        page.page = get(window, "Page" + n);
        for (std::size_t r = 0; r < Settings.specs[s].rows; ++r) {
            for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
                const std::string tag = std::to_string(r + 1) + "_" + std::to_string(p + 1);
                CellWidgets& cell = page.cells[r][p];
                cell.glow    = get(page.page, "Glow" + tag);
                cell.learned = get(page.page, "Learned" + tag);
                cell.shade   = get(page.page, "Locked" + tag);
            }
        }
    }
    for (std::size_t r = 0; r < MaxRows; ++r) {
        for (std::size_t p = 0; p < MaxPerksPerRow; ++p) {
            const std::string tag = std::to_string(r + 1) + "_" + std::to_string(p + 1);
            Native.hover[r][p] = get(window, "Hover" + tag);
            Native.perk[r][p]  = get(window, "Perk" + tag);
            TipWidgets& tip = Native.tips[r][p];
            tip.group = get(window, "Tip" + tag);
            if (tip.group == nullptr) continue;
            tip.title    = TextBox(get(tip.group, "TipTitle"));
            tip.text     = TextBox(get(tip.group, "TipText"));
            for (std::size_t k = 0; k < MaxTipLines; ++k) tip.backs[k] = get(tip.group, "TipBack" + std::to_string(k + 1));
            for (std::size_t k = 1; k < MaxTipLines; ++k) tip.hints[k] = TextBox(get(tip.group, "TipHint" + std::to_string(k)));
        }
    }
    Native.pointsAvailable = TextBox(get(window, "PointsAvailable"));
    Native.found           = found;
}

void Show(void* widget, bool visible) noexcept {
    if (widget == nullptr) return;
    const std::uint8_t want = visible ? 1 : 0;
    const auto it = NativeVisible.find(widget);
    if (it != NativeVisible.end() && it->second == want) return;
    SetNativeVisible(widget, visible);
    NativeVisible[widget] = want;
}

void SetBoxText(void* box, const std::string& text) {
    if (box == nullptr) return;
    const auto it = NativeText.find(box);
    if (it != NativeText.end() && it->second == text) return;
    SetNativeText(box, text);
    NativeText[box] = text;
}

void ResetWindowCaches() {
    Native = {};
    NativeVisible.clear();
    NativeText.clear();
    ShownNode = {};
}

auto WindowOpen() noexcept -> bool {
    if (Panel == D2RL::Panels::InvalidHandle) return false;
    D2RL::Panels::PanelInfo info{};
    info.structSize = D2RL::Panels::PanelInfoSize;
    return PanelSvc->getPanelInfo(Context, Panel, &info) == D2RL::Panels::Result::Success
        && info.presentationState == D2RL::Panels::PresentationState::Open;
}

// "@namespace:Key" is looked up; anything else is shown as written. A key that
// is not found shows itself, so a typo is visible in game.
auto ResolveText(const std::string& value) -> std::string {
    if (value.size() < 2 || value[0] != '@' || TextSvc == nullptr) return value;
    const std::string key = value.substr(1);
    std::uint32_t required = 0;
    char          small[512]{};
    const D2RL::Localization::Result result = TextSvc->getStringByKey(Context, key.c_str(), small, sizeof(small),
        &required);
    if (result == D2RL::Localization::Result::Success) return small;
    if (result == D2RL::Localization::Result::BufferTooSmall && required > sizeof(small) && required < 65'536) {
        std::string big(required, '\0');
        if (TextSvc->getStringByKey(Context, key.c_str(), big.data(), required, &required)
                == D2RL::Localization::Result::Success) {
            big.resize(std::strlen(big.c_str()));
            return big;
        }
    }
    return value;
}

// Line breaking for the description box ------------------------------------
auto DecodeUtf8(const std::string& text, std::size_t i, std::uint32_t& cp) noexcept -> std::size_t {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t n = 1;
    if ((lead & 0xE0U) == 0xC0U) n = 2;
    else if ((lead & 0xF0U) == 0xE0U) n = 3;
    else if ((lead & 0xF8U) == 0xF0U) n = 4;
    if (n == 1 || i + n > text.size()) {
        cp = lead;
        return 1;
    }
    cp = n == 2 ? (lead & 0x1FU) : n == 3 ? (lead & 0x0FU) : (lead & 0x07U);
    for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
    return n;
}

auto IsWide(std::uint32_t c) noexcept -> bool {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6);
}

// Width of one character of the tooltip text (TipText, $XMediumFontSize), in
// layout units, measured from the game's own rendering of this font: narrow
// letters 18, wide ones 32, spaces 22, the rest 21; Chinese, Japanese and
// Korean characters 30. Colour codes take no room.
auto GlyphWidth(std::uint32_t c) noexcept -> int {
    if (c == ' ') return 22;
    if (IsWide(c)) return 30;
    switch (c) {
    case 'i': case 'l': case 'j': case 't': case 'f': case 'r': case 'I': case '1':
    case '.': case ',': case '\'': case ':': case ';': case '!': case '|': case '(': case ')':
        return 18;
    case 'm': case 'w': case 'M': case 'W': case '%':
        return 32;
    default:
        return 21;
    }
}

auto IsClosingMark(std::uint32_t c) noexcept -> bool {
    switch (c) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF01: case 0xFF1F:
    case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x30FC: case 0xFF5E:
        return true;
    default:
        return false;
    }
}

struct WrappedText {
    std::string text;
    std::size_t lines{};
};

// Breaks text into lines no wider than limit: at spaces, and between Chinese,
// Japanese and Korean characters (never before a closing mark). Line breaks in
// the text stay. prefix (a colour code) starts every line, so a colour holds
// across the breaks.
auto WrapTip(const std::string& text, int limit, const std::string& prefix = {}) -> WrappedText {
    enum class Kind { Word, Wide, Space, Newline };
    struct Token { std::string bytes; int width{}; Kind kind{ Kind::Word }; };
    std::vector<Token> tokens;
    const auto open = [&tokens]() -> bool {  // the last token can take more characters of a word
        return !tokens.empty() && (tokens.back().kind == Kind::Word || tokens.back().kind == Kind::Wide);
    };
    for (std::size_t i = 0; i < text.size();) {
        std::uint32_t c = 0;
        const std::size_t n = DecodeUtf8(text, i, c);
        if (c == 0xFF && i + n + 1 < text.size() && text[i + n] == 'c') {  // a colour code rides on what follows
            if (!open()) tokens.push_back({ {}, 0, Kind::Word });
            tokens.back().bytes.append(text, i, n + 2);
            i += n + 2;
            continue;
        }
        const std::string bytes = text.substr(i, n);
        i += n;
        if (c == '\n') {
            tokens.push_back({ {}, 0, Kind::Newline });
        } else if (c == ' ') {
            tokens.push_back({ bytes, GlyphWidth(c), Kind::Space });
        } else if (IsWide(c) && !IsClosingMark(c)) {
            // a wide character is a word of its own, unless only a colour code waits for it
            if (!tokens.empty() && tokens.back().kind == Kind::Word && tokens.back().width == 0) {
                tokens.back().bytes += bytes;
                tokens.back().width += GlyphWidth(c);
                tokens.back().kind   = Kind::Wide;
            } else {
                tokens.push_back({ bytes, GlyphWidth(c), Kind::Wide });
            }
        } else if (IsWide(c) || (open() && tokens.back().kind == Kind::Word)) {
            if (open()) {  // closing marks and the rest of a word stay with what came before
                tokens.back().bytes += bytes;
                tokens.back().width += GlyphWidth(c);
            } else {
                tokens.push_back({ bytes, GlyphWidth(c), Kind::Word });
            }
        } else {
            tokens.push_back({ bytes, GlyphWidth(c), Kind::Word });
        }
    }
    WrappedText out;
    std::string line = prefix;
    int  width = 0;
    bool empty = true;
    std::string pendingSpace;
    int  pendingWidth = 0;
    const auto flush = [&]() {
        out.text += line;
        out.text += '\n';
        ++out.lines;
        line  = prefix;
        width = 0;
        empty = true;
        pendingSpace.clear();
        pendingWidth = 0;
    };
    for (const Token& t : tokens) {
        if (t.kind == Kind::Newline) { flush(); continue; }
        if (t.kind == Kind::Space) {
            if (!empty) { pendingSpace += t.bytes; pendingWidth += t.width; }
            continue;
        }
        if (!empty && width + pendingWidth + t.width > limit) flush();
        if (!empty) { line += pendingSpace; width += pendingWidth; }
        pendingSpace.clear();
        pendingWidth = 0;
        line  += t.bytes;
        width += t.width;
        empty  = false;
    }
    out.text += line;
    ++out.lines;
    return out;
}

// Width the tooltip lines are broken at: the TipText box is 672 units wide and
// the measured widths can be about 4% short, so lines stay within 640.
constexpr int TipLineWidth = 640;

auto WithNumber(std::string text, std::int64_t value) -> std::string {
    if (const std::size_t at = text.find("%d"); at != std::string::npos) text.replace(at, 2, std::to_string(value));
    return text;
}

// The hovered perk of the page on screen: its hover area or its click button.
auto HoveredNode() noexcept -> NodeRef {
    void* const hovered = HoveredWidget();
    if (hovered == nullptr || ViewedSpec >= Settings.specs.size()) return {};
    for (std::size_t r = 0; r < Settings.specs[ViewedSpec].rows; ++r) {
        for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
            if ((Native.hover[r][p] != nullptr && hovered == Native.hover[r][p])
                    || (Native.perk[r][p] != nullptr && hovered == Native.perk[r][p])) {
                return { r, p, true };
            }
        }
    }
    return {};
}

// The hovered perk's tooltip: title, description, and the gray unlearn line
// while it can be unlearned. Every other tooltip is hidden.
void UpdateTooltip(const Snapshot& snap) {
    const NodeRef node = HoveredNode();
    for (std::size_t r = 0; r < MaxRows; ++r) {
        for (std::size_t p = 0; p < MaxPerksPerRow; ++p) {
            Show(Native.tips[r][p].group, node.valid && node.r == r && node.p == p);
        }
    }
    ShownNode = node;
    if (!node.valid) return;
    const std::size_t s     = ViewedSpec;
    const SpecConfig& spec  = Settings.specs[s];
    const std::size_t index = node.r * static_cast<std::size_t>(Settings.perksPerRow) + node.p;
    (void)snap;
    const WrappedText body = WrapTip(index < spec.descriptions.size() ? ResolveText(spec.descriptions[index]) : std::string{},
        TipLineWidth);
    const std::string text = body.text;
    // The unlearn line, always shown so the cost is known: its own gray text box one line below the
    // description (a text box shows colour codes as letters), on one line when it fits, else broken
    // after its first sentence.
    WrappedText hint;
    if (Settings.respecStat >= 0) {
        const std::string line = WithNumber(ResolveText(Settings.unlearnHint), Settings.unlearnCost);
        hint = WrapTip(line, TipLineWidth);
        if (hint.lines > 1) {
            const std::size_t cut = line.find(". ");
            if (cut != std::string::npos) {
                const WrappedText first = WrapTip(line.substr(0, cut + 1), TipLineWidth);
                const WrappedText rest  = WrapTip(line.substr(cut + 2), TipLineWidth);
                if (first.lines == 1 && rest.lines == 1) hint = { first.text + "\n" + rest.text, 2 };
            }
        }
    }
    const std::size_t hintAt = hint.lines > 0 ? body.lines + 1 : 0;   // the text line the gray line starts on
    const std::size_t lines  = std::clamp<std::size_t>(body.lines + (hint.lines > 0 ? 1 + hint.lines : 0), 1, MaxTipLines);
    const TipWidgets& tip = Native.tips[node.r][node.p];
    for (std::size_t k = 0; k < tip.backs.size(); ++k) Show(tip.backs[k], k + 1 == lines);
    for (std::size_t k = 1; k < tip.hints.size(); ++k) Show(tip.hints[k], k == hintAt);
    if (hintAt > 0 && hintAt < tip.hints.size()) SetBoxText(tip.hints[hintAt], hint.text);
    SetBoxText(tip.title, index < spec.titles.size() ? ResolveText(spec.titles[index]) : std::string{});
    SetBoxText(tip.text, text);
}

// A perk is clicked when the primary mouse button goes down and comes back up
// over the same perk's hover area, with the game in front. Learning needs a
// learnable perk; Shift + click unlearns. Anything else does nothing.
bool    MouseWasDown{};
NodeRef PressedNode;

auto GameInFront() noexcept -> bool {
    DWORD process = 0;
    const HWND front = GetForegroundWindow();
    return front != nullptr && GetWindowThreadProcessId(front, &process) != 0 && process == GetCurrentProcessId();
}

void SendLearn(const D2RL::PluginContext* context, std::size_t s, std::size_t r, std::size_t p, bool shift);

// A text box (the chat line) has the keyboard: Space types, it does not close.
auto TypingText() noexcept -> bool {
    if (UiState == nullptr) return false;
    void* const ui = UiState();
    void* const focused = ui != nullptr ? Read<void*>(ui, UiFocusedOffset) : nullptr;
    return focused != nullptr && IsType(focused, "InputTextBoxWidget");
}

bool SpaceWasDown{ true };

void PollPerkClick() {
    const int  button = GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_RBUTTON : VK_LBUTTON;
    const bool down   = (GetAsyncKeyState(button) & 0x8000) != 0 && GameInFront();
    const NodeRef node = HoveredNode();
    if (down && !MouseWasDown) PressedNode = node;
    if (!down && MouseWasDown && node.valid && PressedNode.valid && node.r == PressedNode.r && node.p == PressedNode.p) {
        ClicksLearn.fetch_add(1);
        SendLearn(Context, ViewedSpec, node.r, node.p, ShiftDown());
    }
    if (!down) PressedNode = {};
    MouseWasDown = down;
}

// Brings the open window in line with the character: the page on screen, the
// tabs, each perk (learned ring; glow on a perk that can be learned now; shade
// on every other perk not learned; the click button only where a click does
// something), the points box and the hovered perk's tooltip.
void RefreshWindow() {
    if (!Active.load() || !WindowOpen()) return;
    void* const window = LocateWindow();
    if (window == nullptr) return;
    if (window != Native.window) ResolveNative(window);
    void* const player = LocalPlayer();
    if (player == nullptr) return;
    const Snapshot snap  = ReadSnapshot(player);
    const bool     shift = ShiftDown();
    if (ViewedSpec >= Settings.specs.size()) ViewedSpec = 0;
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        const bool viewed = s == ViewedSpec;
        const bool locked = !SpecUsable(s) || snap.chosen == SeveralChosen
            || (snap.chosen != NoChoice && snap.chosen != static_cast<int>(s));
        const PageWidgets& page = Native.pages[s];
        Show(page.page, viewed);
        Show(Native.tabActive[s], viewed);
        Show(Native.tabChosen[s], snap.hasLearned[s] != 0);
        Show(Native.tabLocked[s], locked);
        for (std::size_t r = 0; r < Settings.specs[s].rows; ++r) {
            for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
                const CellWidgets& cell = page.cells[r][p];
                const bool learned   = snap.learned[s][r][p] != 0;
                const bool learnable = CanLearn(snap, s, r, p);
                Show(cell.learned, learned);
                Show(cell.glow, learnable);
                Show(cell.shade, !learned && !learnable);
            }
        }
    }
    const std::size_t viewedRows = Settings.specs[ViewedSpec].rows;
    for (std::size_t r = 0; r < MaxRows; ++r) {
        for (std::size_t p = 0; p < MaxPerksPerRow; ++p) {
            const bool used = r < viewedRows && p < static_cast<std::size_t>(Settings.perksPerRow);
            Show(Native.hover[r][p], used);
            Show(Native.perk[r][p], used && (CanLearn(snap, ViewedSpec, r, p)
                || (shift && CanUnlearn(snap, ViewedSpec, r, p))));
        }
    }
    SetBoxText(Native.pointsAvailable, WithNumber(ResolveText(PointsAvailableText), std::max<std::int64_t>(snap.points, 0)));
    UpdateTooltip(snap);
    PollPerkClick();
}

// While the window is open, a UI task refreshes it on every root widget update
// and queues itself for the next one. A guard stops the chain if the UI queue
// ever ran it again inside the same update.
std::atomic<std::uint64_t> WindowChain{};
std::atomic<std::uint64_t> WindowChainCounter{};
std::atomic<bool>          WindowChainBroken{};
std::uint64_t              GuardWindow{};
std::uint32_t              GuardCount{};

void __cdecl WindowTick(const D2RL::PluginContext* context, void* userData) noexcept;

void StopWindowChain(std::uint64_t id) noexcept {
    std::uint64_t expected = id;
    (void)WindowChain.compare_exchange_strong(expected, 0);
}

void StartWindowChain(const D2RL::PluginContext* context) noexcept {
    if (WindowChainBroken.load()) return;
    const std::uint64_t id = WindowChainCounter.fetch_add(1) + 1;
    WindowChain.store(id);
    if (ThreadSvc->runOnUiThread(context, &WindowTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
            != D2RL::Threads::Result::Success) {
        StopWindowChain(id);
    }
}

void __cdecl WindowTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || WindowChain.load() != id || !Active.load()) return;
    const std::uint64_t now = GetTickCount64();
    if (now / 25 != GuardWindow) {
        GuardWindow = now / 25;
        GuardCount  = 0;
    }
    if (++GuardCount > 50) {
        StopWindowChain(id);
        WindowChainBroken.store(true);
        context->LogError("Specializations: the window refresh ran 50 times inside 25 ms; the per-frame refresh is "
            "off. The window still refreshes while the character sheet is open and on every click.");
        return;
    }
    if (!WindowOpen()) {
        StopWindowChain(id);
        return;
    }
    const bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 && GameInFront() && !TypingText();
    if (space && !SpaceWasDown) {
        SpaceWasDown = true;
        (void)PanelSvc->closePanel(context, Panel);
        StopWindowChain(id);
        return;
    }
    SpaceWasDown = space;
    RefreshWindow();
    LastWindowTickMs.store(now);
    if (ThreadSvc->runOnUiThread(context, &WindowTick, userData) != D2RL::Threads::Result::Success) StopWindowChain(id);
}

// The HUD alert (SpecializationAlert, a button in hudpanelhd.json): shown while
// the character has points, the window is closed and the window has not been
// opened since the last new point. Like the stock New Stats and New Skill
// alerts: opening the window hides it until a point arrives, and a new game
// shows it again while points are left. A UI task re-queued on every root
// widget update keeps it in step while a game is running.
std::atomic<std::uint64_t> HudChain{};
std::atomic<std::uint64_t> HudChainCounter{};
std::uint64_t              HudChainSeen{};
WidgetHandle               HudPanelHandle{ D2RL::Widgets::InvalidHandle };
WidgetHandle               HudAlertHandle{ D2RL::Widgets::InvalidHandle };
int                        HudShown{ -1 };
std::uint64_t              HudLastApplyMs{};
std::uint64_t              HudLastLookupMs{};
std::uint32_t              HudLookups{};
std::uint64_t              GuardHud{};
std::uint32_t              GuardHudCount{};
bool                       HudVisited{};        // the window was opened since the last new point
std::int32_t               HudVisitedPoints{};  // the points the character had then

void __cdecl HudTick(const D2RL::PluginContext* context, void* userData) noexcept;

void StartHudChain(const D2RL::PluginContext* context) noexcept {
    if (ThreadSvc == nullptr || WidgetSvc == nullptr) return;
    const std::uint64_t id = HudChainCounter.fetch_add(1) + 1;
    HudChain.store(id);
    if (ThreadSvc->runOnUiThread(context, &HudTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
            != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)HudChain.compare_exchange_strong(expected, 0);
    }
}

void SendClaim(const D2RL::PluginContext* context);
auto OwnedPoints(const Snapshot& snap) noexcept -> std::int32_t;

// Watches the character's owned points and the unlock stat: any change (the
// token, a quest, another plugin) asks the host to check level points and the cap.
std::int32_t  WatchOwned{ -1 };
std::int32_t  WatchUnlock{ -1 };
std::uint64_t WatchLastMs{};
std::uint64_t WatchLastClaimMs{};

void WatchPoints(const D2RL::PluginContext* context, std::uint64_t now) {
    if (now - WatchLastMs < 250) return;
    WatchLastMs = now;
    void* const player = LocalPlayer();
    if (player == nullptr) return;
    const std::int32_t owned  = OwnedPoints(ReadSnapshot(player));
    const std::int32_t unlock = Settings.unlockStat >= 0 ? ReadOwnStat(player, Settings.unlockStat, 0) : 0;
    if ((owned == WatchOwned && unlock == WatchUnlock) || now - WatchLastClaimMs < 1000) return;
    const bool first = WatchOwned < 0;
    WatchOwned       = owned;
    WatchUnlock      = unlock;
    WatchLastClaimMs = now;
    if (!first) SendClaim(context);  // entering the game already claims once
}

void __cdecl HudTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || HudChain.load() != id || !Active.load()) return;
    WatchPoints(context, GetTickCount64());
    if (HudChainSeen != id) {
        HudChainSeen   = id;
        HudPanelHandle = D2RL::Widgets::InvalidHandle;
        HudAlertHandle = D2RL::Widgets::InvalidHandle;
        HudShown       = -1;
        HudLookups     = 0;
        HudVisited     = false;  // a new game: shown again while points are left
    }
    const std::uint64_t now = GetTickCount64();
    if (now / 25 != GuardHud) {
        GuardHud      = now / 25;
        GuardHudCount = 0;
    }
    if (++GuardHudCount > 50) {
        HudChain.store(0);
        context->LogError("Specializations: the HUD alert refresh ran 50 times inside 25 ms; the HUD alert is off.");
        return;
    }
    if (HudAlertHandle == D2RL::Widgets::InvalidHandle && now - HudLastLookupMs >= 500) {
        HudLastLookupMs = now;
        if (HudPanelHandle == D2RL::Widgets::InvalidHandle
                && WidgetSvc->findPanel(context, "HUDPanel", &HudPanelHandle) != D2RL::Widgets::Result::Success) {
            HudPanelHandle = D2RL::Widgets::InvalidHandle;
        }
        if (HudPanelHandle != D2RL::Widgets::InvalidHandle
                && WidgetSvc->findWidget(context, HudPanelHandle, "SpecializationAlert", &HudAlertHandle)
                    != D2RL::Widgets::Result::Success) {
            HudAlertHandle = D2RL::Widgets::InvalidHandle;
        }
        if (HudAlertHandle == D2RL::Widgets::InvalidHandle && ++HudLookups == 20) {
            context->LogWarn("Specializations: hudpanelhd.json has no SpecializationAlert widget, so there is no HUD alert.");
        }
    }
    // Opening the window counts even while the alert widget is not found yet.
    void* const player     = LocalPlayer();
    const std::int32_t points = player != nullptr ? ReadSnapshot(player).points : 0;
    const bool windowOpen  = WindowOpen();
    if (windowOpen) {
        HudVisited       = true;
        HudVisitedPoints = points;
    } else if (HudVisited && points != HudVisitedPoints) {
        if (points > HudVisitedPoints) HudVisited = false;  // a new point arrived
        else HudVisitedPoints = points;                      // spent elsewhere: the next point is new
    }
    if (HudAlertHandle != D2RL::Widgets::InvalidHandle) {
        const bool sheetOpen = now - LastSheetUpdateMs.load(std::memory_order_relaxed) < 250;
        const bool show = player != nullptr && points > 0 && !windowOpen && !sheetOpen && !HudVisited;
        if ((show ? 1 : 0) != HudShown || now - HudLastApplyMs >= 250) {
            const D2RL::Widgets::Result r = WidgetSvc->setWidgetVisible(context, HudAlertHandle, show);
            if (r == D2RL::Widgets::Result::Success) {
                HudShown       = show ? 1 : 0;
                HudLastApplyMs = now;
            } else if (r == D2RL::Widgets::Result::StaleHandle || r == D2RL::Widgets::Result::NotFound) {
                HudAlertHandle = D2RL::Widgets::InvalidHandle;
                HudPanelHandle = D2RL::Widgets::InvalidHandle;
            }
        }
    }
    if (ThreadSvc->runOnUiThread(context, &HudTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)HudChain.compare_exchange_strong(expected, 0);
    }
}


// ---------------------------------------------------------------------------
// The character sheet hook (UI thread)
// ---------------------------------------------------------------------------

auto __fastcall HookResistRows(void* panel, void* player) noexcept -> std::int64_t {
    const std::int64_t result = OriginalResistRows(panel, player);
    if (!SheetHooked.load(std::memory_order_relaxed) || panel == nullptr || player == nullptr) return result;
    if (UiRoot == nullptr) NoteWidget(panel);
    SheetUpdates.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t sheetNow = GetTickCount64();
    if (panel != SheetPanel || sheetNow - LastSheetUpdateMs.load(std::memory_order_relaxed) > 250) {
        SheetVisible.clear();
        SheetButtonLook.clear();
        SheetPanel = panel;
    }
    LastSheetUpdateMs.store(sheetNow, std::memory_order_relaxed);
    FillResist(panel, player, "MagicText", false);
    FillResist(panel, player, "PhysicalText", true);
    if (SheetReady.load(std::memory_order_relaxed)) {
        const Snapshot snap = ReadSnapshot(player);
        const bool points = snap.points > 0;
        UpdateSheetButton(panel, points ? 0 : (snap.anyLearned ? 3 : -1));
        ShowChild(panel, "SpecializationTitle", points || snap.anyLearned);
        if (Active.load(std::memory_order_relaxed)
                && GetTickCount64() - LastWindowTickMs.load(std::memory_order_relaxed) > 300 && WindowOpen()) {
            RefreshWindow();
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Network: client side
// ---------------------------------------------------------------------------

constexpr std::size_t MaxPending = 8;
std::mutex                             PendingMutex;
std::array<LearnMessage, MaxPending>   Pending{};
std::size_t                            PendingCount{};
std::uint64_t                          PendingSinceMs{};

auto Send(const D2RL::PluginContext* context, const LearnMessage& message) noexcept -> bool {
    return NetworkSvc->sendToHost(context, Channel, LearnMessageId, &message, sizeof(message))
        == D2RL::Network::Result::Success;
}

void Connect(const D2RL::PluginContext* context) noexcept {
    (void)NetworkSvc->connectToHost(context, Channel);
}

void FlushPending(const D2RL::PluginContext* context) noexcept {
    std::array<LearnMessage, MaxPending> taken{};
    std::size_t                          count = 0;
    std::uint64_t                        since = 0;
    {
        std::lock_guard lock(PendingMutex);
        taken        = Pending;
        count        = PendingCount;
        since        = PendingSinceMs;
        PendingCount = 0;
    }
    if (count == 0) return;
    if (GetTickCount64() - since > 5000) {
        RequestsDropped.fetch_add(count);
        return;
    }
    for (std::size_t i = 0; i < count; ++i) (Send(context, taken[i]) ? RequestsSent : RequestsDropped).fetch_add(1);
}

void SendLearn(const D2RL::PluginContext* context, std::size_t s, std::size_t r, std::size_t p, bool shift) {
    void* const player = LocalPlayer();
    if (player == nullptr) return;
    const Snapshot snap    = ReadSnapshot(player);
    const bool     unlearn = shift && CanUnlearn(snap, s, r, p);
    if (!unlearn && (shift || !CanLearn(snap, s, r, p))) {
        ClicksRefusedLocally.fetch_add(1);
        return;
    }
    const LearnMessage message{ UnitId(player), static_cast<std::uint8_t>(s), static_cast<std::uint8_t>(r),
        static_cast<std::uint8_t>(p), static_cast<std::uint8_t>(unlearn ? 1 : 0) };
    const std::string& sound = unlearn ? Settings.unlearnSound : Settings.learnSound;
    if (PlaySound != nullptr && !sound.empty()) PlaySound(sound.c_str(), nullptr, 0, 0);
    if (Send(context, message)) {
        RequestsSent.fetch_add(1);
        return;
    }
    {
        std::lock_guard lock(PendingMutex);
        if (PendingCount < MaxPending) {
            if (PendingCount == 0) PendingSinceMs = GetTickCount64();
            Pending[PendingCount++] = message;
        } else {
            RequestsDropped.fetch_add(1);
        }
    }
    Connect(context);
}

void __cdecl OnConnectionState(const D2RL::PluginContext* context, const D2RL::Network::ConnectionEvent* event,
        void*) noexcept {
    if (context == nullptr || !D2RL::Network::HasConnectionEventField(event, D2RL::Network::ConnectionEventRequiredSize)) {
        return;
    }
    ConnectionNow.store(static_cast<std::uint32_t>(event->state));
    if (event->state == D2RL::Network::ConnectionState::Rejected) {
        D2RL::LogWarnF(context, "Specializations: the host refused the plugin channel (reason %u); perks cannot be "
            "learned in this game. The host needs this plugin, the same version.", static_cast<unsigned>(event->reason));
        return;
    }
    if (event->state == D2RL::Network::ConnectionState::Connected) FlushPending(context);
}

// The host never answers: the stats themselves reach the client.
void __cdecl OnClientMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, std::uint16_t, const void*,
        std::uint32_t, void*) noexcept {}

// ---------------------------------------------------------------------------
// Network: host side (game thread)
// ---------------------------------------------------------------------------

struct PlayerRef {
    std::uint32_t id;
    void*         unit;
};

constexpr std::size_t MaxPlayers        = 16;
constexpr std::size_t MaxClientsVisited = 256;

auto CollectPlayers(std::array<PlayerRef, MaxPlayers>& players) noexcept -> std::size_t {
    if (Read<std::int32_t>(reinterpret_cast<const void*>(Base + RegistryReadyRva), 0) == 0) return 0;
    const auto* const buckets = reinterpret_cast<const std::uint8_t* const*>(Base + RegistryRva);
    std::size_t count   = 0;
    std::size_t visited = 0;
    for (std::size_t bucket = 0; bucket < RegistryBuckets; ++bucket) {
        for (const std::uint8_t* client = buckets[bucket]; client != nullptr;
                client = Read<const std::uint8_t*>(client, ClientNextOffset)) {
            if (++visited > MaxClientsVisited) return count;
            void* const game = Read<void*>(client, ClientGameOffset);
            if (game == nullptr || Read<std::uint32_t>(client, ClientUnitTypeOffset) != UnitTypePlayer) continue;
            const std::uint32_t id   = Read<std::uint32_t>(client, ClientUnitIdOffset);
            void* const         unit = ServerUnit(game, UnitTypePlayer, id);
            if (unit == nullptr || UnitType(unit) != UnitTypePlayer || count >= players.size()) continue;
            players[count++] = { id, unit };
        }
    }
    return count;
}

struct Request {
    D2RL::Network::PeerHandle peer;
    LearnMessage              message;
};

constexpr std::size_t            MaxRequests = 64;
std::mutex                       RequestMutex;
std::array<Request, MaxRequests> RequestQueue{};
std::size_t                      RequestCount{};

auto StatStorable(const D2RL::PluginContext* context, std::int32_t stat) noexcept -> bool;

void Unlearn(const D2RL::PluginContext* context, void* unit, const LearnMessage& message) noexcept {
    const std::size_t s = message.specialization;
    const std::size_t r = message.row;
    const std::size_t p = message.perk;
    const Snapshot    before = ReadSnapshot(unit);
    if (!CanUnlearn(before, s, r, p)) {
        RequestsRefused.fetch_add(1);
        D2RL::LogInfoF(context, "Specializations: player %u asked to unlearn %s row %zu perk %zu, which it cannot "
            "(%d respec points, %d needed); nothing changed.", message.playerId,
            s < Settings.specs.size() ? Settings.specs[s].name.c_str() : "?", r + 1, p + 1, before.respec,
            Settings.unlearnCost);
        return;
    }
    const std::int32_t stat = PerkStat(s, r, p);
    if (!StatStorable(context, stat) || !StatStorable(context, Settings.respecStat)) {
        RequestsRefused.fetch_add(1);
        return;
    }
    WriteOwnStat(unit, stat, 0, 0);
    WriteOwnStat(unit, Settings.pointsStat, before.points + 1, 0);
    WriteOwnStat(unit, Settings.respecStat, before.respec - Settings.unlearnCost, 0);
    PerksUnlearned.fetch_add(1);
    D2RL::LogInfoF(context, "Specializations: player %u unlearned %s row %zu perk %zu, points %d -> %d, respec %d -> %d.",
        message.playerId, Settings.specs[s].name.c_str(), r + 1, p + 1, before.points,
        ReadOwnStat(unit, Settings.pointsStat, 0), before.respec, ReadOwnStat(unit, Settings.respecStat, 0));
}

constexpr std::int32_t LevelStat = 12;  // itemstatcost 'level'

// Points the character owns: unspent, plus one for every learned perk.
auto OwnedPoints(const Snapshot& snap) noexcept -> std::int32_t {
    std::int32_t owned = std::max<std::int32_t>(snap.points, 0);
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        for (std::size_t r = 0; r < Settings.specs[s].rows; ++r) {
            for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
                owned += snap.learned[s][r][p] != 0 ? 1 : 0;
            }
        }
    }
    return owned;
}

// Host: never more than max_points owned. Only unspent points are taken; a perk
// costs one point and a specialization has one perk per row, so with
// max_points at least the row count the unspent points always cover the excess.
void ClampPoints(const D2RL::PluginContext* context, void* unit, std::uint32_t playerId) noexcept {
    const Snapshot     snap   = ReadSnapshot(unit);
    const std::int32_t owned  = OwnedPoints(snap);
    if (owned <= Settings.maxPoints) return;
    const std::int32_t excess = std::min(owned - Settings.maxPoints, std::max<std::int32_t>(snap.points, 0));
    if (excess > 0) {
        WriteOwnStat(unit, Settings.pointsStat, snap.points - excess, 0);
        PointsRemoved.fetch_add(static_cast<std::uint64_t>(excess));
    }
    D2RL::LogWarnF(context, "Specializations: player %u owned %d points, over max_points %d: %d unspent removed.",
        playerId, owned, Settings.maxPoints, excess);
}

auto Unlocked(void* unit, const Snapshot& snap) noexcept -> bool {
    if (Settings.unlockStat >= 0) return ReadOwnStat(unit, Settings.unlockStat, 0) >= 1;
    return OwnedPoints(snap) > 0;
}

// Host: once unlocked, the character owns unlock_points + one point per point
// level reached, never more than max_points.
void ClaimLevelPoints(const D2RL::PluginContext* context, void* unit, const LearnMessage& message) noexcept {
    if (!StatStorable(context, Settings.pointsStat)) return;
    ClampPoints(context, unit, message.playerId);
    const Snapshot     snap  = ReadSnapshot(unit);
    const std::int32_t owned = OwnedPoints(snap);
    if (!Unlocked(unit, snap)) return;  // the token gives the first points
    const std::int32_t level = ReadOwnStat(unit, LevelStat, 0);
    std::int32_t entitled = Settings.unlockPoints;
    for (const std::int32_t at : Settings.pointLevels) entitled += level >= at ? 1 : 0;
    entitled = std::min(entitled, Settings.maxPoints);
    if (owned >= entitled) return;
    WriteOwnStat(unit, Settings.pointsStat, snap.points + (entitled - owned), 0);
    PointsAwarded.fetch_add(static_cast<std::uint64_t>(entitled - owned));
    D2RL::LogInfoF(context, "Specializations: player %u at level %d owned %d points, %d by its level: %d added.",
        message.playerId, level, owned, entitled, entitled - owned);
}

void Learn(const D2RL::PluginContext* context, void* unit, const LearnMessage& message) noexcept {
    if (StatStorable(context, Settings.pointsStat)) ClampPoints(context, unit, message.playerId);
    if (message.op == 2) {
        ClaimLevelPoints(context, unit, message);
        return;
    }
    if (message.op == 1) {
        Unlearn(context, unit, message);
        return;
    }
    const std::size_t s = message.specialization;
    const std::size_t r = message.row;
    const std::size_t p = message.perk;
    const Snapshot    before = ReadSnapshot(unit);
    if (!CanLearn(before, s, r, p)) {
        RequestsRefused.fetch_add(1);
        D2RL::LogInfoF(context, "Specializations: player %u asked for %s row %zu perk %zu, which it cannot learn now "
            "(%d points); nothing changed.", message.playerId,
            s < Settings.specs.size() ? Settings.specs[s].name.c_str() : "?", r + 1, p + 1, before.points);
        return;
    }
    const std::int32_t stat = PerkStat(s, r, p);
    if (!StatStorable(context, stat) || !StatStorable(context, Settings.pointsStat)) {
        RequestsRefused.fetch_add(1);
        D2RL::LogWarnF(context, "Specializations: %s row %zu perk %zu uses stat %d, which has no saved itemstatcost "
            "row; nothing changed.", Settings.specs[s].name.c_str(), r + 1, p + 1, stat);
        return;
    }
    WriteOwnStat(unit, stat, Settings.perkValue, 0);
    WriteOwnStat(unit, Settings.pointsStat, before.points - 1, 0);
    PerksLearned.fetch_add(1);
    D2RL::LogInfoF(context, "Specializations: player %u learned %s row %zu perk %zu (stat %d = %d), points %d -> %d.",
        message.playerId, Settings.specs[s].name.c_str(), r + 1, p + 1, stat, ReadOwnStat(unit, stat, 0),
        before.points, ReadOwnStat(unit, Settings.pointsStat, 0));
}

void __cdecl HostProcess(const D2RL::PluginContext* context, void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    std::array<Request, MaxRequests> requests{};
    std::size_t                      count = 0;
    {
        std::lock_guard lock(RequestMutex);
        count = RequestCount;
        std::copy_n(RequestQueue.begin(), count, requests.begin());
        RequestCount = 0;
    }
    if (count == 0) return;
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t playerCount = CollectPlayers(players);
    for (std::size_t i = 0; i < count; ++i) {
        D2RL::PlayerHandle sender = D2RL::InvalidPlayerHandle;
        if (NetworkSvc->getPeerPlayer(context, Channel, requests[i].peer, &sender) != D2RL::Network::Result::Success
                || sender == D2RL::InvalidPlayerHandle) {
            RequestsRefused.fetch_add(1);
            continue;
        }
        void* unit = nullptr;
        for (std::size_t k = 0; k < playerCount; ++k) {
            if (players[k].id == requests[i].message.playerId) unit = players[k].unit;
        }
        if (unit == nullptr) {
            RequestsUnknown.fetch_add(1);
            continue;
        }
        Learn(context, unit, requests[i].message);
    }
}

void __cdecl OnHostMessage(const D2RL::PluginContext* context, D2RL::Network::ChannelHandle,
        D2RL::Network::PeerHandle peer, std::uint16_t messageId, const void* data, std::uint32_t size, void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    if (messageId != LearnMessageId || data == nullptr || size != sizeof(LearnMessage)) {
        RequestsRefused.fetch_add(1);
        return;
    }
    Request request{ peer, {} };
    std::memcpy(&request.message, data, sizeof(LearnMessage));
    {
        std::lock_guard lock(RequestMutex);
        if (RequestCount >= RequestQueue.size()) {
            RequestsRefused.fetch_add(1);
            return;
        }
        RequestQueue[RequestCount++] = request;
    }
    RequestsReceived.fetch_add(1);
    if (ThreadSvc->runOnGameThread(context, &HostProcess, nullptr) != D2RL::Threads::Result::Success) {
        context->LogWarn("Specializations: a learn request could not be queued on the host's game thread.");
    }
}

// ---------------------------------------------------------------------------
// Clicks (UI messages)
// ---------------------------------------------------------------------------

constexpr std::string_view ClickTarget  = "PanelManager";
constexpr std::string_view ClickCommand = "OpenPanel";
constexpr std::string_view ClickPrefix  = "Specializations";

enum ClickKind : std::uintptr_t { ClickOpen = 1, ClickClose = 2, ClickTab = 3, ClickPerk = 4 };

auto PackClick(std::uintptr_t kind, std::size_t s, std::size_t r, std::size_t p) noexcept -> void* {
    return reinterpret_cast<void*>(kind | (static_cast<std::uintptr_t>(s) << 8) | (static_cast<std::uintptr_t>(r) << 16)
        | (static_cast<std::uintptr_t>(p) << 24));
}

void __cdecl HandleClick(const D2RL::PluginContext* context, void* userData) noexcept {
    if (context == nullptr || !Active.load()) return;
    const auto     packed = reinterpret_cast<std::uintptr_t>(userData);
    const auto     kind   = packed & 0xFF;
    const std::size_t s   = (packed >> 8) & 0xFF;
    const std::size_t r   = (packed >> 16) & 0xFF;
    const std::size_t p   = (packed >> 24) & 0xFF;
    NoteWidget(HoveredWidget());  // the clicked button: learns the UI root the window opens under
    switch (kind) {
    case ClickOpen: {
        const bool wasOpen = WindowOpen();
        if (wasOpen) {
            (void)PanelSvc->closePanel(context, Panel);
            return;
        }
        if (void* const player = LocalPlayer(); player != nullptr) {
            // Hidden buttons still take clicks: the sheet's spot of the hidden + must not open the window.
            const Snapshot snap = ReadSnapshot(player);
            if (snap.points <= 0 && !snap.anyLearned) {
                ClicksIgnored.fetch_add(1);
                return;
            }
        }
        if (PanelSvc->openPanel(context, Panel) != D2RL::Panels::Result::Success) {
            context->LogWarn("Specializations: the window did not open. If the mod replaces the layout, check "
                "that every text in it is a full reference such as @d2r:Key (D2RLoader's log names the problem).");
            return;
        }
        ResetWindowCaches();
        SpaceWasDown = true;
        if (void* const player = LocalPlayer(); player != nullptr) {
            const Snapshot snap = ReadSnapshot(player);
            if (snap.chosen >= 0) ViewedSpec = static_cast<std::size_t>(snap.chosen);
        }
        RefreshWindow();
        StartWindowChain(context);
        return;
    }
    case ClickClose:
        (void)PanelSvc->closePanel(context, Panel);
        return;
    case ClickTab:
        if (s < Settings.specs.size()) ViewedSpec = s;
        RefreshWindow();
        return;
    case ClickPerk:  // the perk buttons are shared by every page: the one on screen
        (void)s;
        ClicksLearn.fetch_add(1);
        SendLearn(context, ViewedSpec, r, p, ShiftDown());
        RefreshWindow();
        return;
    default:
        return;
    }
}

// Parses "<n>" or "<n>.<n>.<n>" (1-based) into 0-based numbers.
auto ParseNumbers(std::string_view text, std::array<std::size_t, 3>& out, std::size_t want) noexcept -> bool {
    std::size_t index = 0;
    while (!text.empty() && index < want) {
        const std::size_t dot  = text.find('.');
        const std::string_view part = text.substr(0, dot);
        text = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
        std::int32_t value = 0;
        if (!ParseInt(part, 1, 255, value)) return false;
        out[index++] = static_cast<std::size_t>(value - 1);
    }
    return index == want && text.empty();
}

auto __cdecl OnUiMessage(const D2RL::PluginContext* context, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
        -> D2RL::SharedEvents::UiMessageAction {
    if (context == nullptr || event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
            || event->target == nullptr || event->command == nullptr || event->text == nullptr
            || ClickTarget != event->target || ClickCommand != event->command) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    const std::string_view text(event->text);
    if (text.substr(0, ClickPrefix.size()) != ClickPrefix) return D2RL::SharedEvents::UiMessageAction::Continue;
    const std::string_view rest = text.substr(ClickPrefix.size());
    void* click = nullptr;
    std::array<std::size_t, 3> numbers{};
    if (rest == "Open") {
        click = PackClick(ClickOpen, 0, 0, 0);
    } else if (rest == "Close") {
        click = PackClick(ClickClose, 0, 0, 0);
    } else if (rest.substr(0, 3) == "Tab" && ParseNumbers(rest.substr(3), numbers, 1)) {
        click = PackClick(ClickTab, numbers[0], 0, 0);
    } else if (rest.substr(0, 5) == "Perk." && ParseNumbers(rest.substr(5), numbers, 2)) {
        click = PackClick(ClickPerk, 0, numbers[0], numbers[1]);
    }
    if (click != nullptr && Active.load()) (void)ThreadSvc->runOnUiThread(context, &HandleClick, click);
    // The message is private to this plugin, so it never goes on to PanelManager.
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

// ---------------------------------------------------------------------------
// Game join and leave
// ---------------------------------------------------------------------------

void ClearSession() noexcept {
    WindowChain.store(0);
    WatchOwned  = -1;
    WatchUnlock = -1;
    ResetWindowCaches();
    {
        std::lock_guard lock(PendingMutex);
        PendingCount = 0;
    }
    std::lock_guard lock(RequestMutex);
    RequestCount = 0;
}

// Table rows can only be read on the game thread, so the itemstatcost check is
// queued there when a game starts after the tables (re)loaded.
std::atomic<bool> RowCheckPending{ true };
void __cdecl RunRowCheck(const D2RL::PluginContext* context, void*) noexcept;

void __cdecl OnGameJoined(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

// Asks the host to bring the character's points in line with its level.
void SendClaim(const D2RL::PluginContext* context) {
    if (Settings.pointLevels.empty()) return;
    void* const player = LocalPlayer();
    if (player == nullptr) return;
    const LearnMessage message{ UnitId(player), 0, 0, 0, 2 };
    if (Send(context, message)) {
        RequestsSent.fetch_add(1);
        return;
    }
    {
        std::lock_guard lock(PendingMutex);
        if (PendingCount < MaxPending) {
            if (PendingCount == 0) PendingSinceMs = GetTickCount64();
            Pending[PendingCount++] = message;
        } else {
            RequestsDropped.fetch_add(1);
        }
    }
    Connect(context);
}

void __cdecl OnPlayerLevelChanged(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event,
        void*) noexcept {
    if (context == nullptr || !Active.load() || event == nullptr) return;
    if (event->currentValue > event->previousValue) SendClaim(context);
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*,
        void*) noexcept {
    if (context != nullptr && Active.load()) {
        Connect(context);
        SendClaim(context);
        StartHudChain(context);
        if (TableSvc != nullptr && RowCheckPending.exchange(false)
                && ThreadSvc->runOnGameThread(context, &RunRowCheck, nullptr) != D2RL::Threads::Result::Success) {
            RowCheckPending.store(true);
        }
    }
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    HudChain.store(0);
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

// ---------------------------------------------------------------------------
// itemstatcost check, every time the tables load
// ---------------------------------------------------------------------------

struct RowReport {
    bool          busy{};
    std::uint32_t checked{}, missing{}, noCsvBits{}, notSaved{}, withParam{};
    std::int32_t  firstMissing{ -1 }, firstNoCsvBits{ -1 }, firstNotSaved{ -1 };
};

std::mutex RowReportMutex;
RowReport  LastRowReport;
bool       RowReportReady{};

// The game's active tables are in the Rotw bank (D2R 3.x); Lod is tried after it.
auto IscRow(const D2RL::PluginContext* context, std::int32_t stat, D2RL::DataTables::RowView& row) noexcept
        -> D2RL::DataTables::Result {
    D2RL::DataTables::Result result = D2RL::DataTables::Result::NotFound;
    if (stat < 0) return result;
    for (const D2RL::DataTables::Bank bank : { D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod }) {
        row            = {};
        row.structSize = D2RL::DataTables::RowViewSize;
        result = TableSvc->getRow(context, bank, D2RL::DataTables::TableId::ItemStatCost,
            static_cast<std::uint32_t>(stat), &row);
        if (result == D2RL::DataTables::Result::Busy) return result;
        if (result == D2RL::DataTables::Result::Success && D2RL::DataTables::HasRowViewField(&row,
                D2RL::DataTables::RowViewRequiredSize) && row.row != nullptr && row.rowSize > IscCsvParamOffset) {
            return result;
        }
    }
    return D2RL::DataTables::Result::NotFound;
}

// Game thread: the stat has an itemstatcost row that is saved with the character.
auto StatStorable(const D2RL::PluginContext* context, std::int32_t stat) noexcept -> bool {
    D2RL::DataTables::RowView row{};
    if (TableSvc == nullptr || IscRow(context, stat, row) != D2RL::DataTables::Result::Success) return false;
    return Read<std::uint8_t>(row.row, IscCsvBitsOffset) != 0
        && (Read<std::uint32_t>(row.row, IscFlagsOffset) & IscFlagSaved) != 0;
}

void CheckStatRow(const D2RL::PluginContext* context, std::int32_t stat, RowReport& report) noexcept {
    ++report.checked;
    D2RL::DataTables::RowView row{};
    const D2RL::DataTables::Result result = IscRow(context, stat, row);
    if (result == D2RL::DataTables::Result::Busy) {
        report.busy = true;
        return;
    }
    if (result != D2RL::DataTables::Result::Success
            || !D2RL::DataTables::HasRowViewField(&row, D2RL::DataTables::RowViewRequiredSize)
            || row.row == nullptr || row.rowSize <= IscCsvParamOffset) {
        ++report.missing;
        if (report.firstMissing < 0) report.firstMissing = stat;
        return;
    }
    const std::uint32_t flags = Read<std::uint32_t>(row.row, IscFlagsOffset);
    if (Read<std::uint8_t>(row.row, IscCsvBitsOffset) == 0) {
        ++report.noCsvBits;
        if (report.firstNoCsvBits < 0) report.firstNoCsvBits = stat;
    }
    if ((flags & IscFlagSaved) == 0) {
        ++report.notSaved;
        if (report.firstNotSaved < 0) report.firstNotSaved = stat;
    }
    if (Read<std::uint8_t>(row.row, IscCsvParamOffset) != 0) ++report.withParam;
}

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*,
        void*) noexcept {
    RowCheckPending.store(true);
}

void __cdecl RunRowCheck(const D2RL::PluginContext* context, void*) noexcept {
    if (context == nullptr || TableSvc == nullptr) return;
    RowReport report{};
    CheckStatRow(context, Settings.pointsStat, report);
    for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
        bool usable = true;
        for (const std::int32_t stat : Settings.specs[s].stats) {
            CheckStatRow(context, stat, report);
            usable = usable && StatStorable(context, stat);
        }
        SpecState[s].store(usable ? StateUsable : StateMissing);
        if (!usable) {
            D2RL::LogWarnF(context, "Specializations: %s has perk stats without a saved itemstatcost row; its perks "
                "cannot be learned until the rows are added.", Settings.specs[s].name.c_str());
        }
    }
    if (Settings.respecStat >= 0) {
        const bool respecOk = StatStorable(context, Settings.respecStat);
        RespecState.store(respecOk ? StateUsable : StateMissing);
        if (!respecOk) {
            D2RL::LogWarnF(context, "Specializations: respec_stat %d has no saved itemstatcost row; unlearning is off.",
                Settings.respecStat);
        }
    }
    if (report.busy) {
        RowCheckPending.store(true);
        context->LogWarn("Specializations: itemstatcost.txt could not be read yet; the check runs again next game.");
        return;
    }
    {
        std::lock_guard lock(RowReportMutex);
        LastRowReport  = report;
        RowReportReady = true;
    }
    if (report.missing != 0) {
        D2RL::LogErrorF(context, "Specializations: %u of %u stats have no itemstatcost.txt row (first: %d). Those "
            "perks cannot work.", report.missing, report.checked, report.firstMissing);
    }
    if (report.noCsvBits != 0) {
        D2RL::LogErrorF(context, "Specializations: %u stats have CSvBits 0 (first: %d). They are not saved: every "
            "character comes back without them.", report.noCsvBits, report.firstNoCsvBits);
    }
    if (report.notSaved != 0) {
        D2RL::LogWarnF(context, "Specializations: %u stats have Saved = 0 (first: %d). The host changes them, but "
            "the client is not told, so the window and the sheet do not see it.", report.notSaved, report.firstNotSaved);
    }
    if (report.withParam != 0) {
        D2RL::LogWarnF(context, "Specializations: %u stats have CSvParam set. The plugin uses layer 0, so CSvParam "
            "is not needed.", report.withParam);
    }
    if (report.missing == 0 && report.noCsvBits == 0 && report.notSaved == 0) {
        D2RL::LogInfoF(context, "Specializations: itemstatcost.txt checked, all %u stats are saved and sent to the "
            "client.", report.checked);
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::BadConfig:        return "the config is unusable, see the log";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing hooked";
    case PluginState::ServicesMissing:  return "a D2RLoader service is missing";
    case PluginState::InstallFailed:    return "an install step failed, see the log";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load());
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char       line[512];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };
    std::snprintf(line, sizeof(line), "Specializations %s: %s. Points stat %d, perk value %d, %zu specializations, "
        "%d perks per row.", PluginVersion, StateName(State), Settings.pointsStat, Settings.perkValue,
        Settings.specs.size(), Settings.perksPerRow);
    say(line);
    if (!ConfigProblems.empty()) {
        std::snprintf(line, sizeof(line), "  config: %s", ConfigProblems.c_str());
        say(line);
    }
    if (State != PluginState::Active) return D2RL::ConsoleCommandResult::Handled;
    {
        std::lock_guard lock(RowReportMutex);
        if (RowReportReady) {
            std::snprintf(line, sizeof(line), "  itemstatcost: %u stats, %u missing, %u with CSvBits 0, %u with "
                "Saved 0.", LastRowReport.checked, LastRowReport.missing, LastRowReport.noCsvBits, LastRowReport.notSaved);
        } else {
            std::snprintf(line, sizeof(line), "  itemstatcost: not checked yet.");
        }
        say(line);
    }
    const LiveResistNumbers live = LiveNumbers();
    std::snprintf(line, sizeof(line), "  character sheet: %s, %llu updates. Floor %d, magic cap %d + max magic "
        "resistance up to %d, physical cap %d.", SheetHooked.load() ? "hooked" : "not hooked", Count(SheetUpdates),
        live.floor, live.capBase, live.ceiling, live.physicalCap);
    say(line);
    if (void* const player = LocalPlayer(); player != nullptr) {
        const Snapshot snap = ReadSnapshot(player);
        std::string learned;
        for (std::size_t s = 0; s < Settings.specs.size(); ++s) {
            for (std::size_t r = 0; r < Settings.specs[s].rows; ++r) {
                for (std::size_t p = 0; p < static_cast<std::size_t>(Settings.perksPerRow); ++p) {
                    if (snap.learned[s][r][p] == 0) continue;
                    char item[96];
                    std::snprintf(item, sizeof(item), "%s%s %zu.%zu", learned.empty() ? "" : ", ",
                        Settings.specs[s].name.c_str(), r + 1, p + 1);
                    learned += item;
                }
            }
        }
        const char* chosen = snap.chosen >= 0 ? Settings.specs[static_cast<std::size_t>(snap.chosen)].name.c_str()
            : (snap.chosen == SeveralChosen ? "several (all locked)" : "none");
        std::snprintf(line, sizeof(line), "  you: %d points, %d respec points, specialization %s, learned: %s.",
            snap.points, snap.respec, chosen, learned.empty() ? "nothing" : learned.c_str());
        say(line);
    }
    std::snprintf(line, sizeof(line), "  window: %s, per-frame refresh %s.", WindowOpen() ? "open" : "closed",
        WindowChainBroken.load() ? "off (see the log)" : (WindowChain.load() != 0 ? "running" : "idle"));
    say(line);
    if (WindowOpen()) {
        std::snprintf(line, sizeof(line), "  window widgets: %s, %zu found%s.",
            Native.window != nullptr ? "window found" : "window not found yet (move the mouse over it)", Native.found,
            UiRoot != nullptr ? "" : ", UI root not known yet");
        say(line);
    }
    if (ShownNode.valid && ViewedSpec < Settings.specs.size()) {
        std::snprintf(line, sizeof(line), "  tooltip: showing %s row %zu perk %zu.",
            Settings.specs[ViewedSpec].name.c_str(), ShownNode.r + 1, ShownNode.p + 1);
        say(line);
    }
    std::snprintf(line, sizeof(line), "  client: %llu perk clicks, %llu not learnable, %llu sent, %llu dropped. "
        "Host: %llu received, %llu learned, %llu refused, %llu for an unknown player.", Count(ClicksLearn),
        Count(ClicksRefusedLocally), Count(RequestsSent), Count(RequestsDropped), Count(RequestsReceived),
        Count(PerksLearned), Count(RequestsRefused), Count(RequestsUnknown));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

// A hook placed by another plugin starts with a jump: E9 rel32, FF 25 [rip], or
// mov rax, imm64 / jmp rax. Hooks rewrite at most the first 14 bytes.
auto HookedByOtherPlugin(const Witness& witness) noexcept -> bool {
    const auto* live = reinterpret_cast<const std::uint8_t*>(Base + witness.rva);
    const bool jump = live[0] == 0xE9 || (live[0] == 0xFF && live[1] == 0x25) || (live[0] == 0x48 && live[1] == 0xB8);
    if (!jump) return false;
    constexpr std::uint32_t Rewritten = 14;
    return witness.size <= Rewritten || std::memcmp(live + Rewritten, witness.bytes + Rewritten, witness.size - Rewritten) == 0;
}

auto VerifyAll() noexcept -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        if (witness.callable && HookedByOtherPlugin(witness)) {
            D2RL::LogInfoF(Context, "Specializations: %s at RVA 0x%llX is hooked by another plugin; calls go through that "
                "hook.", witness.name, static_cast<unsigned long long>(witness.rva));
            continue;
        }
        {
            D2RL::LogErrorF(Context, "Specializations: %s at RVA 0x%llX does not match this build.", witness.name,
                static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

void BindNatives() noexcept {
    FindChild        = At<FindChildFn>(FindChildRva);
    ButtonEnable     = At<ButtonEnableFn>(ButtonEnableRva);
    PlaySound        = At<PlaySoundFn>(PlaySoundByNameRva);
    NativeSetText    = At<SetTextFn>(SetTextRva);
    ColorByIndex     = At<ColorByIndexFn>(ColorByIndexRva);
    ResistFormatOn   = At<UiFlagFn>(ResistFormatRva);
    ReadStat         = At<ReadStatFn>(ReadStatRva);
    ReadOwnStat      = At<ReadStatFn>(ReadOwnStatRva);
    WriteOwnStat     = At<WriteStatFn>(WriteOwnStatRva);
    UnitType         = At<UnitTypeFn>(UnitTypeRva);
    UnitId           = At<UnitIdFn>(UnitIdRva);
    LocalPlayerIndex = At<PlayerIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromFn>(PlayerFromIndexRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    UiState          = At<UiStateFn>(UiStateRva);
}

template <typename Service>
auto Need(const Service*& service, bool usable, const char* name) noexcept -> bool {
    if (service != nullptr && usable) return true;
    service = nullptr;
    D2RL::LogErrorF(Context, "Specializations: the %s service is unavailable.", name);
    return false;
}

auto QueryServices() noexcept -> bool {
    bool ok = true;
    if (Context->QueryService(&PanelSvc) != D2RL::ServiceQueryResult::Success) PanelSvc = nullptr;
    ok &= Need(PanelSvc, D2RL::HasPanelServiceField(PanelSvc, D2RL::PanelServiceRequiredSize), "panel");
    if (Context->QueryService(&WidgetSvc) != D2RL::ServiceQueryResult::Success) WidgetSvc = nullptr;
    ok &= Need(WidgetSvc, D2RL::HasWidgetServiceField(WidgetSvc, D2RL::WidgetServiceRequiredSize), "widget");
    if (Context->QueryService(&EventSvc) != D2RL::ServiceQueryResult::Success) EventSvc = nullptr;
    ok &= Need(EventSvc, D2RL::HasSharedEventServiceField(EventSvc, D2RL::SharedEventServiceRequiredSize), "UI message");
    if (Context->QueryService(&NetworkSvc) != D2RL::ServiceQueryResult::Success) NetworkSvc = nullptr;
    ok &= Need(NetworkSvc, D2RL::HasNetworkServiceField(NetworkSvc, D2RL::NetworkServiceRequiredSize), "network");
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success) ThreadSvc = nullptr;
    ok &= Need(ThreadSvc, D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize), "thread");
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success) ResourceSvc = nullptr;
    ok &= Need(ResourceSvc, D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize), "resource");
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success) LifecycleSvc = nullptr;
    ok &= Need(LifecycleSvc, D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize),
        "lifecycle");
    if (Context->QueryService(&TableSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(TableSvc, D2RL::DataTableServiceRequiredSize)) {
        TableSvc = nullptr;
        Context->LogWarn("Specializations: the data table service is unavailable; the itemstatcost check is off.");
    }
    if (Context->QueryService(&TextSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceField(TextSvc, D2RL::LocalizationServiceRequiredSize)) {
        TextSvc = nullptr;
        Context->LogWarn("Specializations: the localization service is unavailable; the new resistance rows print "
            "the number with a % sign.");
    }
    return ok;
}

auto RegisterEverything() noexcept -> bool {
    if (TableSvc != nullptr) {
        const D2RL::Lifecycle::DataTablesLoadedListener tables{
            .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
            .callback   = &OnDataTablesLoaded,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerDataTablesLoadedListener(Context, &tables, &handle) != D2RL::Lifecycle::Result::Success) {
            Context->LogWarn("Specializations: the table listener was refused; the itemstatcost check is off.");
        }
    }
    struct Game {
        D2RL::Lifecycle::GameplayEventKind     kind;
        D2RL::Lifecycle::GameplayEventCallback callback;
    };
    const std::array<Game, 4> games{ {
        { D2RL::Lifecycle::GameplayEventKind::GameJoined, &OnGameJoined },
        { D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, &OnLocalPlayerReady },
        { D2RL::Lifecycle::GameplayEventKind::PlayerLevelChanged, &OnPlayerLevelChanged },
        { D2RL::Lifecycle::GameplayEventKind::GameLeft, &OnGameLeft },
    } };
    for (const Game& game : games) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = game.kind,
            .callback   = game.callback,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
            Context->LogError("Specializations: a game join/leave listener was refused.");
            return false;
        }
    }
    const D2RL::Network::ChannelRegistration channel{
        .structSize         = D2RL::Network::ChannelRegistrationSize,
        .localChannelId     = ChannelLocalId,
        .compatibilityToken = CompatibilityToken,
        .hostMessage        = &OnHostMessage,
        .clientMessage      = &OnClientMessage,
        .connectionState    = &OnConnectionState,
    };
    const D2RL::Network::Result channelResult = NetworkSvc->registerChannel(Context, &channel, &Channel);
    if (channelResult != D2RL::Network::Result::Success || Channel == D2RL::Network::InvalidChannelHandle) {
        D2RL::LogErrorF(Context, "Specializations: the network channel was refused (result %u).",
            static_cast<unsigned>(channelResult));
        return false;
    }
    std::string_view layoutBytes;
    if (!PanelLayoutBytes(layoutBytes)) {
        Context->LogError("Specializations: the window layout is missing from the plugin (SpecializationPanelLayout.inc).");
        return false;
    }
    const D2RL::Resources::ResourceRegistration layout{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = PanelResourcePath,
        .bytes      = reinterpret_cast<decltype(D2RL::Resources::ResourceRegistration::bytes)>(layoutBytes.data()),
        .byteCount  = static_cast<decltype(D2RL::Resources::ResourceRegistration::byteCount)>(layoutBytes.size()),
    };
    D2RL::Resources::RegistrationHandle layoutHandle = D2RL::Resources::InvalidHandle;
    const D2RL::Resources::Result layoutResult = ResourceSvc->registerResource(Context, &layout, &layoutHandle);
    if (layoutResult != D2RL::Resources::Result::Success) {
        D2RL::LogErrorF(Context, "Specializations: the window layout could not be handed to D2RLoader (result %u).",
            static_cast<unsigned>(layoutResult));
        return false;
    }
    const D2RL::Panels::PanelRegistration panel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags      = D2RL::Panels::PanelFlags::CloseOnEscape,
        .localId    = PanelLocalId,
    };
    const D2RL::Panels::Result panelResult = PanelSvc->registerPanel(Context, &panel, &Panel);
    if (panelResult != D2RL::Panels::Result::Success || Panel == D2RL::Panels::InvalidHandle) {
        D2RL::LogErrorF(Context, "Specializations: the window panel was refused (result %u).",
            static_cast<unsigned>(panelResult));
        return false;
    }
    const D2RL::SharedEvents::UiMessageListener clicks{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags      = 0,
        .priority   = 0,
        .reserved   = 0,
        .callback   = &OnUiMessage,
        .userData   = nullptr,
    };
    if (EventSvc->registerUiMessageListener(Context, &clicks, &ClickListener) != D2RL::SharedEvents::Result::Success) {
        Context->LogError("Specializations: the click listener was refused.");
        return false;
    }
    return true;
}

auto InstallSheetHook() noexcept -> bool {
    if (!Context->InstallInlineHook(ResistRowsRva, ResistRowsPrologue,
            static_cast<std::uint32_t>(sizeof(ResistRowsPrologue)), &HookResistRows, &OriginalResistRows)) {
        Context->LogError("Specializations: the character sheet hook at 0x14E6E70 failed to install; the sheet shows "
            "no Magic, Physical or Specialization rows.");
        return false;
    }
    return true;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Specializations",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Specialization window and points, perks saved as character stats, Magic and Physical "
                   "resistance rows on the character sheet.",
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

    if (!Context->RegisterConsoleCommand("specializations", &StatusCommand,
            "Specializations status: itemstatcost check, your points and perks, resistance numbers, counters.")) {
        Context->LogWarn("Specializations: the console command could not be registered.");
    }

    ReadConfiguration();
    const bool configOk = ValidateConfig();
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Specializations: %s", ConfigProblems.c_str());
    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("Specializations: disabled by config.");
        return true;
    }
    if (!configOk) {
        State = PluginState::BadConfig;
        Context->LogError("Specializations: the config cannot be used; nothing was installed.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("Specializations: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    SheetHooked.store(InstallSheetHook());
    SheetReady.store(true);
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    if (!RegisterEverything()) {
        State = PluginState::InstallFailed;
        return true;
    }
    Active.store(true);
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "Specializations %s: active, %zu specializations, points stat %d.", PluginVersion,
        Settings.specs.size(), Settings.pointsStat);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    SheetHooked.store(false);
    SheetReady.store(false);
    WindowChain.store(0);
    HudChain.store(0);
}
