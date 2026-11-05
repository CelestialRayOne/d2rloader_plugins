// Transfer to Stash
//
// A "Transfer to Stash" button on the inventory panel, shown only while the
// stash is open, that sends every Advanced Stash item in the inventory and in
// the Horadric Cube to the stash. Shift + left click (or holding A on a
// controller) also sends everything else to the shared stash afterwards.
// Where the gold display sits is up to the mod's own inventory layout; the
// plugin does not move it.
//
// Everything below was read out of the D2RLoader 1.3.1 process image, and
// every entry used here is checked byte for byte at load. Nothing is installed
// when a check fails.
//
// ---------------------------------------------------------------------------
// 1. The Ctrl+click this copies, 2AAAA0 (the whole function)
// ---------------------------------------------------------------------------
//   Both inventory click handlers (228AB0 legacy, 2C7540 modern) run it on a
//   Ctrl+click while the stash (UI state 18h) is open, once 15A0B0 (item) says
//   the item may go to the Advanced Stash. 15A0B0 checks UI state 18h, then
//   the item class in the Advanced Stash registry (15F320), which only holds
//   classes that have a slot in the stash layout. 2AAAA0 then does:
//     item = widget->vtable[C8h](widget)
//     if (1C7360(item) == 0)                         the item is not busy
//         dest = 46DA50(9A480(8B2D0()))              the Advanced Stash owner
//         if (dest != 0)
//             placement = { 0, 0, engaged 0 }
//             15F8B0(item, dest, 4, 36CFE0(item), 1, &placement)
//     1A0780(3, 0, 0, 0, 0)                          sound 3
//   The plugin runs the same sequence for every item, with the widget lookup
//   replaced by a walk over the player's items (34A360 -> 388C10 / 38ABA0),
//   keeping those on page 0 (inventory) and page 3 (cube). Sound 3 plays once
//   per press instead of once per item.
//
// ---------------------------------------------------------------------------
// 2. Why cube items go straight to the stash
// ---------------------------------------------------------------------------
//   15F8B0 with mode 1 and an owner carrying state 209 (the Advanced Stash)
//   never looks at the source page: it checks the class registration again
//   (15F320) and calls 159A30, which sends packet 63h, operation 0, item GUID,
//   owner GUID. The server handler 4C4AF0 -> 4F92F0 then needs:
//     - 46DC00: the item's items.txt record +1BDh, which is misc.txt
//       AdvancedStashStackable, not 0
//     - the owner's slot for the class, when it has one, below its cap
//     - 472590 lifting the item from where it sits. 474220 has its own page 3
//       (cube) branch, and 474700 accepts pages 0 and 3 anywhere (page 4 only
//       near the bank).
//   So a cube item needs no stop in the inventory and no free inventory space.
//
// ---------------------------------------------------------------------------
// 3. Busy items
// ---------------------------------------------------------------------------
//   When 15F8B0 sends a transfer it calls 1C6AC0(item), which gives the item a
//   mode 2 stat list (state 54). 1C7360 reports such an item as busy until the
//   server answers, so pressing the button again before that skips it.
//   1C7360 is called but not checked: the whirlwind plugin hooks it.
//
// ---------------------------------------------------------------------------
// 4. The inventory panel, 22BA70 (hooked)
// ---------------------------------------------------------------------------
//   PlayerInventoryPanel's update, slot 2 of the table at 1CE36D0 (its only
//   reference). It refreshes gold_amount and gold_button, switches the gold
//   button to deposit while UI state 18h is open, and ends in the base widget
//   update. The hook finds the button by name and shows it only while the
//   stash is open, then runs the original.
//     856220 (widget, name)       direct child by name: children at +58h,
//                                 count at +60h, name at child +8h
//     8562A0 (widget, rectOut)    rect at +70h {x, y, width, height} in parent
//                                 space (read for the console)
//     widget vtable +48h / +50h   set enabled / visible: for ButtonWidget
//                                 837D0 writes +50h, F02A0 writes +51h
//     widget vtable +58h          the widget's type; a type's parent at +10h
//     86F0B0                      ButtonWidget type
//   The widget parent is at +30h.
//
// ---------------------------------------------------------------------------
// 5. The buttons
// ---------------------------------------------------------------------------
//   The buttons are part of the mod's own inventory layouts, so their place,
//   size, font and d-pad links are set there:
//     transfer_to_stash_button             keyboard/mouse inventory
//     transfer_to_stash_controller_button  controller inventory
//   The plugin hands D2RCore their art as plugin resources (frame 0 normal,
//   1 pressed, 2 disabled, 3 hovered) and the keyboard/mouse tooltip string,
//   and its UI message listener consumes their click messages and queues the
//   transfer on the UI thread.
//     keyboard/mouse  368x58 per frame (184x29 px at 1080p)
//     controller      404x82 per frame (202x41 px at 1080p)
//   Up to 1.5.2 the plugin added both buttons itself as SDK child layouts;
//   D2RCore never built the controller one (1.5.2's console listed the
//   controller inventory's children: no Transfer to Stash button among them).
//
// ---------------------------------------------------------------------------
// 6. The Controls key
// ---------------------------------------------------------------------------
//   "Transfer to Stash" in Options > Controls, in its own "Stash" section, with
//   no default key, through the SDK's input service. A press while the stash
//   is open queues the same transfer as the button; a key held down counts as
//   one press. The input service runs the handler during input processing, so
//   the handler only reads the stash UI state (0CE500, a byte read) and queues
//   the work on the UI thread. The key always sends materials only.
//
// ---------------------------------------------------------------------------
// 7. Shift + left click: everything else goes to the shared stash
// ---------------------------------------------------------------------------
//   The press first does section 1 (materials to their Advanced Stash tabs),
//   then moves every other inventory and cube item into the shared stash, the
//   way Ctrl+click moves an item into a shared page, one item at a time.
//   Shift is read with GetAsyncKeyState when the click message arrives, the
//   same test the game's own item widgets make (120A100 with VK_SHIFT).
//
//   Shared stash pages. Each page is a hidden player unit (a proxy, state 186)
//   listed in the local player's inventory at +68h, the list the Advanced Stash
//   owner is in too. Per entry: 2EF880 the proxy id (+0), 38CD60 remove-only
//   (+8), 38CD50 active (+9), 38CD80 type (+0Ah, 1 = the Advanced Stash),
//   38CD70 next (+10h). A page can take items when it is not the Advanced
//   Stash, not remove-only and active; the server refuses the other two kinds
//   (4C5570 / 4C7690 log "remove-only" and "inactive"). Pages are numbered
//   in that list order, skipping the Advanced Stash and remove-only entries,
//   and only the first 388C70() of them are shared tabs (388C70 is the shared
//   tab count the bank and the gold limit use). That is exactly how D2RCore
//   numbers the normal shared tabs (D2RCore 41C250), so page 1 here is tab 1
//   in game. Pages are filled in that order. 09A5D0(id, 0) gives the proxy
//   unit on this client.
//
//   The request. 15F8B0 sends opcode 55h (CCMD_GRIDSHAREDSTASHTRANSACTION)
//   through 0EC8F0(55h, item id, proxy id, source page, source position,
//   destination page 4, destination position, 0), but always with the proxy
//   of the page on screen (UI model key 8D4D18A675312BD4), so the plugin calls
//   0EC8F0 itself with each page's own id. Source position is 1601E0(item),
//   the destination cell is INVENTORY_FindFreePosition 3865B0 on the proxy's
//   page 4 grid (34A410(proxy, 4, !08D3B0())). Then 1C6AC0 marks the item busy,
//   as 15F8B0 does. The server handler 4C7690 runs the same 3865B0 search on its
//   own copy of the page and refuses the request unless the cell matches, so
//   only one shared request is in flight at a time: the next item is placed
//   once the server has answered (the item left the inventory or cube, or its
//   busy mark was cleared). That keeps every request identical to Ctrl+click.
//
//   Items that stay where they are: Tome of Town Portal (tbk), Tome of
//   Identify (ibk), the Horadric Cube itself (box), items 424550(player, item)
//   refuses for the shared stash (the game's own rule, items.txt record +13Ah)
//   and anything still on its way somewhere (busy). The cursor must be empty;
//   the server refuses 55h while the player holds an item (4C7690, 388A70).
//
//   Multiplayer. Both requests are the client's own Ctrl+click requests (63h
//   and 55h), so the host checks them as usual: the plugin is needed only by
//   the player who presses the button, on the host or on a TCP/IP client.
//
// ---------------------------------------------------------------------------
// 8. Controller: tap and hold
// ---------------------------------------------------------------------------
//   ButtonWidget input 8F0F00 (hooked) activates a focused button on the
//   InputMessage command 1E439768F7DD4594 with payload action +10h = 0. The
//   payload byte +19h is the hold flag: the item widgets (2A98B0) use it for
//   their "hold" variants of the same action. The hook only notes, for the
//   plugin's controller button, whether the activation was a tap or a hold,
//   then runs the original. The controller button sends its own click text,
//   so the UI message listener knows the note belongs to it: tap sends the
//   materials, hold sends everything. This hook is optional: when its byte
//   check or install fails, the rest of the plugin still works and a hold
//   counts as a tap.

#include <D2RLPlugin/api.h>

#include "resource_ids.h"

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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.transfer-to-stash";
constexpr char PluginVersion[] = "1.6.0";
constexpr char SourceFile[]    = "transfer-to-stash";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 image)
// ---------------------------------------------------------------------------

// Hooked
constexpr std::uint64_t InventoryUpdateRva     = 0x22BA70;
constexpr std::uint64_t ButtonInputRva         = 0x8F0F00;   // ButtonWidget input message handler

// Called
constexpr std::uint64_t LocalDataContextRva    = 0x08B2D0;
constexpr std::uint64_t LocalPlayerRva         = 0x09A480;
constexpr std::uint64_t UiStateOpenRva         = 0x0CE500;
constexpr std::uint64_t CanQuickMoveRva        = 0x15A0B0;
constexpr std::uint64_t TransferRva            = 0x15F8B0;
constexpr std::uint64_t PlaySoundRva           = 0x1A0780;
constexpr std::uint64_t InteractionBlockedRva  = 0x1C7360;   // not checked, see section 3
constexpr std::uint64_t UnitInventoryRva       = 0x34A360;
constexpr std::uint64_t UnitTypeRva            = 0x34B9D0;
constexpr std::uint64_t ItemPageRva            = 0x36CFE0;
constexpr std::uint64_t FirstItemRva           = 0x388C10;
constexpr std::uint64_t NextItemRva            = 0x38ABA0;
constexpr std::uint64_t StashDestinationRva    = 0x46DA50;
constexpr std::uint64_t FindChildRva           = 0x856220;
constexpr std::uint64_t WidgetRectRva          = 0x8562A0;
constexpr std::uint64_t ButtonTypeRva          = 0x86F0B0;

// Called by the shared stash transfer (section 7)
constexpr std::uint64_t OnlineStateRva         = 0x08D3B0;
constexpr std::uint64_t ClientUnitRva          = 0x09A5D0;
constexpr std::uint64_t SharedRequestRva       = 0x0EC8F0;
constexpr std::uint64_t ItemPositionRva        = 0x1601E0;
constexpr std::uint64_t MarkBusyRva            = 0x1C6AC0;
constexpr std::uint64_t RecordIdRva            = 0x2EF880;
constexpr std::uint64_t InventoryGridRva       = 0x34A410;
constexpr std::uint64_t ItemCodeRva            = 0x36EF50;
constexpr std::uint64_t FindFreePositionRva    = 0x3865B0;
constexpr std::uint64_t CursorItemRva          = 0x388A70;
constexpr std::uint64_t SharedTabCountRva      = 0x388C70;
constexpr std::uint64_t FirstRecordRva         = 0x388E00;
constexpr std::uint64_t RecordActiveRva        = 0x38CD50;
constexpr std::uint64_t RecordRemoveOnlyRva    = 0x38CD60;
constexpr std::uint64_t NextRecordRva          = 0x38CD70;
constexpr std::uint64_t RecordTypeRva          = 0x38CD80;
constexpr std::uint64_t SharedAllowedRva       = 0x424550;

// Checked only: the Ctrl+click copied here and the ButtonWidget flag setters
constexpr std::uint64_t QuickMoveActionRva     = 0x2AAAA0;
constexpr std::uint64_t ButtonSetEnabledRva    = 0x0837D0;
constexpr std::uint64_t ButtonSetVisibleRva    = 0x0F02A0;

// Widget layout
constexpr std::size_t WidgetParentOffset       = 0x30;
constexpr std::size_t WidgetEnabledOffset      = 0x50;
constexpr std::size_t WidgetVisibleOffset      = 0x51;
constexpr std::size_t WidgetChildrenOffset     = 0x58;
constexpr std::size_t WidgetChildCountOffset   = 0x60;
constexpr std::size_t WidgetNameOffset         = 0x08;     // char*, read by 856220 for the name compare
constexpr std::size_t VtableSetEnabled         = 0x48 / 8;
constexpr std::size_t VtableSetVisible         = 0x50 / 8;
constexpr std::size_t VtableType               = 0x58 / 8;
constexpr std::size_t TypeParentOffset         = 0x10;
constexpr std::uint64_t MaxChildren            = 4096;
constexpr std::size_t MaxTypeDepth             = 64;

// Game values
constexpr std::int32_t StashUiState            = 0x18;
constexpr std::int32_t ItemUnitType            = 4;
constexpr std::uint8_t InventoryPage           = 0;
constexpr std::uint8_t CubePage                = 3;
constexpr std::uint8_t AdvancedStashPage       = 4;
constexpr std::uint8_t QuickMoveMode           = 1;
constexpr std::int32_t QuickMoveSound          = 3;
constexpr std::size_t  MaxItemsWalked          = 4096;
constexpr std::size_t  MaxBatch                = 1024;

// Section 7
constexpr std::uint8_t  SharedRequestCommand   = 0x55;     // CCMD_GRIDSHAREDSTASHTRANSACTION
constexpr std::uint8_t  StashPage              = 4;
constexpr std::uint32_t PlayerUnitType         = 0;
constexpr std::uint8_t  AdvancedStashRecord    = 1;        // registry entry type of the Advanced Stash owner
constexpr std::size_t   UnitIdOffset           = 0x08;     // unit id: UNITS_GetUnitId 34A330 reads [unit+8]
constexpr std::size_t   MaxRecords             = 4096;
constexpr std::size_t   MaxSharedPages         = 1024;
constexpr std::uint64_t SharedAnswerTimeoutMs  = 5000;     // per request; TCP/IP latency included
constexpr std::uint32_t TownPortalTomeCode     = 0x206B6274;   // "tbk "
constexpr std::uint32_t IdentifyTomeCode       = 0x206B6269;   // "ibk "
constexpr std::uint32_t HoradricCubeCode       = 0x20786F62;   // "box "

// Section 8
constexpr std::uint64_t ControllerActionCommand = 0x1E439768F7DD4594ULL;
constexpr std::size_t   MessageCommandOffset    = 0x88;
constexpr std::size_t   MessagePayloadOffset    = 0x110;
constexpr std::size_t   PayloadActionOffset     = 0x10;
constexpr std::size_t   PayloadHoldOffset       = 0x19;

// Widget names

// ---------------------------------------------------------------------------
// The button
// ---------------------------------------------------------------------------

constexpr std::string_view ClickTarget  = "PanelManager";
constexpr std::string_view ClickCommand = "OpenPanel";
constexpr std::string_view ClickText           = "CelestialRayOneTransferToStash";
constexpr std::string_view ControllerClickText = "CelestialRayOneTransferToStashController";

// The keyboard/mouse tooltip, as a string of the plugin's own namespace. A
// layout field shows its text as written, so a color code there is drawn as
// plain characters; strings loaded from a string table have their color codes
// turned into the renderer's marker (U+E07E) by D2RCore's string loader, like
// the game's own strings. Two lines, each starting with the tooltip gray (color
// code 5). Every game language gets the same English text, since a missing
// translation shows nothing. A mod can replace it from its own string files
// with the full key celestialrayone.transfer-to-stash:TransferButtonTooltip.
constexpr char TooltipStringsPath[] = "data/local/lng/strings/d2rloader/celestialrayone.transfer-to-stash/strings.json";
constexpr char TooltipGray[]        = "\xC3\xBF" "c5";   // the color code for gray, UTF-8
constexpr const char* TooltipLanguages[]{
    "enUS", "zhTW", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "esMX", "jaJP", "ptBR", "ruRU", "zhCN" };

auto TooltipStrings() -> std::string {
    const std::string text = std::string(TooltipGray) + "Shift + Left Click to transfer\\n" + TooltipGray
                           + "everything to shared stash";
    std::string json = "[\n  {\n    \"Key\": \"TransferButtonTooltip\"";
    for (const char* language : TooltipLanguages) {
        json += ",\n    \"";
        json += language;
        json += "\": \"";
        json += text;
        json += "\"";
    }
    json += "\n  }\n]\n";
    return json;
}

struct ButtonSpec {
    const char* label;            // for the log and the console
    const char* widgetName;       // the button's name in the mod's inventory layout
    int         spriteId;         // RCDATA ids, resource_ids.h
    int         lowendSpriteId;
    const char* spritePath;
    const char* lowendSpritePath;
};

const std::array<ButtonSpec, 2> Buttons{ {
    { "keyboard/mouse", "transfer_to_stash_button",
      TRANSFER_BUTTON_SPRITE_ID, TRANSFER_BUTTON_LOWEND_SPRITE_ID,
      "data/hd/global/ui/d2rloader/celestialrayone.transfer-to-stash/transferbutton.sprite",
      "data/hd/global/ui/d2rloader/celestialrayone.transfer-to-stash/transferbutton.lowend.sprite" },
    { "controller", "transfer_to_stash_controller_button",
      TRANSFER_BUTTON_CONTROLLER_SPRITE_ID, TRANSFER_BUTTON_CONTROLLER_LOWEND_SPRITE_ID,
      "data/hd/global/ui/d2rloader/celestialrayone.transfer-to-stash/transferbuttoncontroller.sprite",
      "data/hd/global/ui/d2rloader/celestialrayone.transfer-to-stash/transferbuttoncontroller.lowend.sprite" },
} };

// ---------------------------------------------------------------------------
// Byte witnesses
// ---------------------------------------------------------------------------

// 22BA70: mov r11,rsp / mov [r11+20h],rbx / push rbp, rsi, rdi, r13, r14 /
// lea rbp,[rsp-70h] / sub rsp,170h
constexpr std::uint8_t InventoryUpdateBytes[]{
    0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x8D,
    0x6C,0x24,0x90,0x48,0x81,0xEC,0x70,0x01,0x00,0x00 };
constexpr std::uint8_t QuickMoveActionBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x40,0x48,0x8B,0x01,0xFF,0x90,0xC8,0x00,0x00,0x00,0x48,
    0x8B,0xD8,0x48,0x85,0xC0,0x74,0x60,0x48,0x8B,0xC8,0xE8,0xA1,0xC8,0xF1,0xFF,0x85,
    0xC0,0x75,0x54,0x48,0x89,0x7C,0x24,0x50,0xE8,0x03,0x08,0xDE,0xFF,0x8B,0xC8,0xE8,
    0xAC,0xF9,0xDE,0xFF,0x48,0x8B,0xC8,0xE8,0x74,0x2F,0x1C,0x00,0x48,0x8B,0xF8,0x48,
    0x85,0xC0,0x74,0x2E,0x48,0x8B,0xCB,0xC6,0x44,0x24,0x38,0x00,0xE8,0xEF,0x24,0x0C,
    0x00,0x48,0x8D,0x4C,0x24,0x30,0x44,0x0F,0xB6,0xC8,0x48,0x89,0x4C,0x24,0x28,0x41,
    0xB0,0x04,0x48,0x8B,0xCB,0xC6,0x44,0x24,0x20,0x01,0x48,0x8B,0xD7,0xE8,0x9E,0x4D,
    0xEB,0xFF,0x48,0x8B,0x7C,0x24,0x50,0x33,0xD2,0xC6,0x44,0x24,0x20,0x00,0x45,0x33,
    0xC9,0x45,0x33,0xC0,0x8D,0x4A,0x03,0xE8,0x54,0x5C,0xEF,0xFF,0x48,0x83,0xC4,0x40,
    0x5B,0xC3 };
constexpr std::uint8_t LocalDataContextBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
constexpr std::uint8_t LocalPlayerBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08,0x0F,0x83,0x85,
    0x00,0x00,0x00,0x8B,0xD9,0x48,0x89,0x5C,0x24,0x38,0x48,0x83,0xFB,0x08,0x72,0x19 };
constexpr std::uint8_t UiStateOpenBytes[]{
    0x48,0x63,0xC1,0x48,0x8D,0x0D,0x96,0xC8,0x95,0x02,0x0F,0xB6,0x04,0x08,0xC3 };
constexpr std::uint8_t CanQuickMoveBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xB9,0x18,0x00,0x00,0x00,0xE8,0x3D,
    0x44,0xF7,0xFF,0x84,0xC0,0x74,0x3E,0xE8,0xB4,0x12,0xF3,0xFF,0x0F,0xB6,0xC8,0xBA };
constexpr std::uint8_t TransferBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,
    0x24,0x08,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xF8,0x01,0x00,0x00,0x48,0x8B,0x05,0xF5 };
constexpr std::uint8_t PlaySoundBytes[]{
    0x40,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD1,0x48,0x81,0xEC,
    0x00,0x01,0x00,0x00,0x48,0x8B,0x05,0x2D,0xAB,0x82,0x02,0x48,0x33,0xC4,0x48,0x89 };
constexpr std::uint8_t UnitInventoryBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9,
    0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x70,0xCC,0xFF,0xFF };
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00 };
constexpr std::uint8_t ItemPageBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x0A,0xE8,0xDD,
    0xE9,0xFD,0xFF,0x83,0xF8,0x04,0x74,0x19,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24 };
constexpr std::uint8_t FirstItemBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x2E,0x81,0x39,
    0x04,0x03,0x02,0x01,0x74,0x1C,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24,0x30,0x00 };
constexpr std::uint8_t NextItemBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x10,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x84,0x98,0xFF,0xFF,0xEB,0x67,0xE8,0x0D };
constexpr std::uint8_t StashDestinationBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30,0x48,
    0x8D,0x4C,0x24,0x30,0xE8,0xC7,0xF3,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x33,0xC0 };
constexpr std::uint8_t FindChildBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0x59,0x58,0x48,0x8B,0xF2,0x48,0x8B,0x41,0x60,0x48,0x8D,0x3C,0xC3,0x48,0x3B };
constexpr std::uint8_t WidgetRectBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0x79,0x52,0x00,0x48,0x8B,0xDA,0x74,0x2A,0x48,
    0x8B,0x49,0x30,0x48,0x8D,0x54,0x24,0x20,0xE8,0xE3,0xFF,0xFF,0xFF,0x33,0xC0,0x48 };
constexpr std::uint8_t ButtonTypeBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D,0x0D,0x63,0x24,0x48,0x01,0xE8,0xCE,0x3F,
    0xFE,0xFF,0x48,0x85,0xC0,0x75,0x13,0x48,0x8D,0x4C,0x24,0x30,0x88,0x44,0x24,0x30 };
constexpr std::uint8_t ButtonSetEnabledBytes[]{ 0x88,0x51,0x50,0xC3 };   // mov [rcx+50h],dl / ret
constexpr std::uint8_t ButtonSetVisibleBytes[]{ 0x88,0x51,0x51,0xC3 };   // mov [rcx+51h],dl / ret

// 8F0F00: mov r11,rsp / mov [r11+18h],rbx / mov [r11+20h],rsi / push rdi / sub rsp,0E0h
constexpr std::uint8_t ButtonInputBytes[]{
    0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x49,0x89,0x73,0x20,0x57,0x48,0x81,0xEC,0xE0,
    0x00,0x00,0x00 };
// 08D3B0: the whole function (client state byte +5Ch == 1)
constexpr std::uint8_t OnlineStateBytes[]{
    0x48,0x8B,0x05,0x29,0x63,0x99,0x02,0x48,0x85,0xC0,0x74,0x08,0x80,0x78,0x5C,0x01,
    0x0F,0x94,0xC0,0xC3,0xC3 };
constexpr std::uint8_t ClientUnitBytes[]{
    0x4C,0x63,0xCA,0x48,0x8D,0x05,0x36,0x93,0x98,0x02,0x8B,0xD1,0x44,0x8B,0xC1,0x49,
    0x8B,0xC9,0x83,0xE2,0x7F,0x48,0xC1,0xE1,0x0A,0x48,0x03,0xC8,0xE9,0x7F,0x4C,0x00 };
// 0EC8F0: arguments 5..8 to packet +0Dh/+11h/+15h/+19h, length 1Dh
constexpr std::uint8_t SharedRequestBytes[]{
    0x48,0x83,0xEC,0x58,0x48,0x8B,0x05,0xCD,0xE9,0x8D,0x02,0x48,0x33,0xC4,0x48,0x89,
    0x44,0x24,0x40,0x8B,0x84,0x24,0x80,0x00,0x00,0x00,0x89,0x44,0x24,0x2D,0x8B,0x84 };
constexpr std::uint8_t ItemPositionBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30 };
// 1C6AC0: mov edx,36h (state 54)
constexpr std::uint8_t MarkBusyBytes[]{
    0x40,0x57,0x48,0x83,0xEC,0x20,0xBA,0x36,0x00,0x00,0x00,0x48,0x8B,0xF9,0xE8,0x6D,
    0xEE,0x12,0x00,0x48,0x85,0xC0,0x75,0x71,0x41,0xB8,0xC1,0x0C,0x00,0x00,0x48,0x89 };
constexpr std::uint8_t RecordIdBytes[]{ 0x8B,0x01,0xC3 };                    // mov eax,[rcx]
constexpr std::uint8_t InventoryGridBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xEC,0x20,0x0F,0xB6,0xEA,0x41,0x8B,0xF0,0x48,0x8B,0xF9,0xBB,0xFF,0xFF };
constexpr std::uint8_t ItemCodeBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x85,0xC9,
    0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x80,0x83,0xFF,0xFF };
constexpr std::uint8_t FindFreePositionBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8B,0xEC,0x48,0x83,
    0xEC,0x60,0x48,0x8B,0x05,0xFF,0x4C,0x64,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0xF0 };
// 388A70: the cursor item, inventory +40h
constexpr std::uint8_t CursorItemBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0xD4,0xB4,0xFF,0xFF,0x84,0xC0,0x74,0x2D };
// 388C70: shared tab count (setting +0C96h, plus 3, plus the mode extras)
constexpr std::uint8_t SharedTabCountBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0xE8,0x91,0xD3,0xCE,0xFF,0x8B,
    0x98,0x96,0x0C,0x00,0x00,0xE8,0x76,0x7D,0xFF,0x00,0x8B,0xF8,0xE8,0x7F,0xD3,0xCE };
constexpr std::uint8_t FirstRecordBytes[]{ 0x48,0x8B,0x41,0x68,0xC3 };        // mov rax,[rcx+68h]
constexpr std::uint8_t RecordActiveBytes[]{ 0x0F,0xB6,0x41,0x09,0xC3 };       // movzx eax,byte [rcx+9]
constexpr std::uint8_t RecordRemoveOnlyBytes[]{ 0x0F,0xB6,0x41,0x08,0xC3 };   // movzx eax,byte [rcx+8]
constexpr std::uint8_t NextRecordBytes[]{ 0x48,0x8B,0x41,0x10,0xC3 };         // mov rax,[rcx+10h]
constexpr std::uint8_t RecordTypeBytes[]{ 0x0F,0xB6,0x41,0x0A,0xC3 };         // movzx eax,byte [rcx+0Ah]
constexpr std::uint8_t SharedAllowedBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xC2,0x48,0x8B,0xF9,
    0x48,0x8B,0xC8,0x48,0x8D,0x15,0x06,0x45,0x8F,0x01,0x41,0xB8,0xCC,0x04,0x00,0x00 };

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

template <std::size_t Size>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[Size], const char* name) noexcept -> Witness {
    return Witness{ rva, bytes, static_cast<std::uint32_t>(Size), name };
}

const Witness Witnesses[]{
    W(InventoryUpdateRva, InventoryUpdateBytes, "inventory panel update"),
    W(QuickMoveActionRva, QuickMoveActionBytes, "Ctrl+click Advanced Stash action"),
    W(LocalDataContextRva, LocalDataContextBytes, "local data context"),
    W(LocalPlayerRva, LocalPlayerBytes, "local player"),
    W(UiStateOpenRva, UiStateOpenBytes, "UI state open"),
    W(CanQuickMoveRva, CanQuickMoveBytes, "Advanced Stash eligibility"),
    W(TransferRva, TransferBytes, "item transfer request"),
    W(PlaySoundRva, PlaySoundBytes, "play sound"),
    W(UnitInventoryRva, UnitInventoryBytes, "unit inventory"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(ItemPageRva, ItemPageBytes, "item page"),
    W(FirstItemRva, FirstItemBytes, "first inventory item"),
    W(NextItemRva, NextItemBytes, "next inventory item"),
    W(StashDestinationRva, StashDestinationBytes, "Advanced Stash owner"),
    W(FindChildRva, FindChildBytes, "child widget by name"),
    W(WidgetRectRva, WidgetRectBytes, "widget rect"),
    W(ButtonTypeRva, ButtonTypeBytes, "ButtonWidget type"),
    W(ButtonSetEnabledRva, ButtonSetEnabledBytes, "ButtonWidget set enabled"),
    W(ButtonSetVisibleRva, ButtonSetVisibleBytes, "ButtonWidget set visible"),
    W(OnlineStateRva, OnlineStateBytes, "online state"),
    W(ClientUnitRva, ClientUnitBytes, "client unit by id"),
    W(SharedRequestRva, SharedRequestBytes, "shared stash request"),
    W(ItemPositionRva, ItemPositionBytes, "item position"),
    W(MarkBusyRva, MarkBusyBytes, "mark item busy"),
    W(RecordIdRva, RecordIdBytes, "stash entry id"),
    W(InventoryGridRva, InventoryGridBytes, "inventory grid"),
    W(ItemCodeRva, ItemCodeBytes, "item code"),
    W(FindFreePositionRva, FindFreePositionBytes, "find free position"),
    W(CursorItemRva, CursorItemBytes, "cursor item"),
    W(SharedTabCountRva, SharedTabCountBytes, "shared tab count"),
    W(FirstRecordRva, FirstRecordBytes, "first stash entry"),
    W(RecordActiveRva, RecordActiveBytes, "stash entry active"),
    W(RecordRemoveOnlyRva, RecordRemoveOnlyBytes, "stash entry remove-only"),
    W(NextRecordRva, NextRecordBytes, "next stash entry"),
    W(RecordTypeRva, RecordTypeBytes, "stash entry type"),
    W(SharedAllowedRva, SharedAllowedBytes, "shared stash item rule"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

struct WidgetRect {
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};
static_assert(sizeof(WidgetRect) == 16);

// The placement 15F8B0 fills in: x and y, and whether they were found.
struct TransferPlacement {
    std::int32_t x;
    std::int32_t y;
    std::uint8_t engaged;
    std::uint8_t padding[7];
};
static_assert(sizeof(TransferPlacement) == 16 && offsetof(TransferPlacement, engaged) == 8);

using LocalDataContextFn   = std::uint32_t(__fastcall*)();
using LocalPlayerFn        = void*(__fastcall*)(std::uint32_t index);
using UiStateOpenFn        = std::uint8_t(__fastcall*)(std::int32_t state);
using CanQuickMoveFn       = bool(__fastcall*)(void* item);
using TransferFn           = std::uint8_t(__fastcall*)(void* item, void* owner, std::uint8_t destinationPage,
                                                       std::uint8_t sourcePage, std::uint8_t mode,
                                                       TransferPlacement* placement);
using PlaySoundFn          = std::uint64_t(__fastcall*)(std::int32_t sound, void* unit, std::int32_t delay,
                                                        std::int32_t option, std::uint8_t flag);
using InteractionBlockedFn = std::int32_t(__fastcall*)(void* item);
using TypeFn               = void*(__fastcall*)();
using UnitInventoryFn      = void*(__fastcall*)(void* unit, const char* file, std::int32_t line);
using UnitTypeFn           = std::int32_t(__fastcall*)(void* unit);
using ItemPageFn           = std::uint8_t(__fastcall*)(void* item);
using FirstItemFn          = void*(__fastcall*)(void* inventory);
using NextItemFn           = void*(__fastcall*)(void* item);
using StashDestinationFn   = void*(__fastcall*)(void* player);
using FindWidgetFn         = void*(__fastcall*)(void* parent, const char* name);
using WidgetRectFn         = WidgetRect*(__fastcall*)(void* widget, WidgetRect* rect);
using InventoryUpdateFn    = std::uint64_t(__fastcall*)(void* panel);
using WidgetBoolFn         = void(__fastcall*)(void* widget, bool value);
using WidgetTypeFn         = void*(__fastcall*)(void* widget);
using ButtonInputFn        = std::uint64_t(__fastcall*)(void* widget, void* message);
using OnlineStateFn        = bool(__fastcall*)();
using ClientUnitFn         = void*(__fastcall*)(std::uint32_t id, std::uint32_t type);
using SharedRequestFn      = void(__fastcall*)(std::uint8_t command, std::uint32_t item, std::uint32_t proxy,
                                               std::uint32_t sourcePage, std::uint32_t sourcePosition,
                                               std::uint32_t destinationPage, std::uint32_t destinationPosition,
                                               std::uint32_t fromShared);
using ItemPositionFn       = std::uint32_t(__fastcall*)(void* unit);
using MarkBusyFn           = void(__fastcall*)(void* item);
using RecordIdFn           = std::uint32_t(__fastcall*)(void* record);
using InventoryGridFn      = std::int32_t(__fastcall*)(void* unit, std::uint32_t page, std::uint32_t expansion);
using ItemCodeFn           = std::uint32_t(__fastcall*)(void* item);
using FindFreePositionFn   = std::int32_t(__fastcall*)(void* inventory, void* item, std::int32_t grid,
                                                       std::int32_t* x, std::int32_t* y, std::uint8_t page);
using CursorItemFn         = void*(__fastcall*)(void* inventory);
using SharedTabCountFn     = std::int32_t(__fastcall*)();
using FirstRecordFn        = void*(__fastcall*)(void* inventory);
using NextRecordFn         = void*(__fastcall*)(void* record);
using RecordByteFn         = std::uint8_t(__fastcall*)(void* record);
using SharedAllowedFn      = bool(__fastcall*)(void* player, void* item);

LocalDataContextFn   LocalDataContext{};
LocalPlayerFn        LocalPlayer{};
UiStateOpenFn        UiStateOpen{};
CanQuickMoveFn       CanQuickMove{};
TransferFn           Transfer{};
PlaySoundFn          PlaySound{};
InteractionBlockedFn InteractionBlocked{};
TypeFn               ButtonType{};
UnitInventoryFn      UnitInventory{};
UnitTypeFn           UnitType{};
ItemPageFn           ItemPage{};
FirstItemFn          FirstItem{};
NextItemFn           NextItem{};
StashDestinationFn   StashDestination{};
FindWidgetFn         FindChild{};
WidgetRectFn         GetWidgetRect{};
InventoryUpdateFn    OriginalInventoryUpdate{};
ButtonInputFn        OriginalButtonInput{};
OnlineStateFn        OnlineState{};
ClientUnitFn         ClientUnit{};
SharedRequestFn      SharedRequest{};
ItemPositionFn       ItemPosition{};
MarkBusyFn           MarkBusy{};
RecordIdFn           RecordId{};
InventoryGridFn      InventoryGrid{};
ItemCodeFn           ItemCode{};
FindFreePositionFn   FindFreePosition{};
CursorItemFn         CursorItem{};
SharedTabCountFn     SharedTabCount{};
FirstRecordFn        FirstRecord{};
NextRecordFn         NextRecord{};
RecordByteFn         RecordActive{};
RecordByteFn         RecordRemoveOnly{};
RecordByteFn         RecordType{};
SharedAllowedFn      SharedAllowed{};

template <typename Function>
auto At(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

void BindNatives() {
    LocalDataContext   = At<LocalDataContextFn>(LocalDataContextRva);
    LocalPlayer        = At<LocalPlayerFn>(LocalPlayerRva);
    UiStateOpen        = At<UiStateOpenFn>(UiStateOpenRva);
    CanQuickMove       = At<CanQuickMoveFn>(CanQuickMoveRva);
    Transfer           = At<TransferFn>(TransferRva);
    PlaySound          = At<PlaySoundFn>(PlaySoundRva);
    InteractionBlocked = At<InteractionBlockedFn>(InteractionBlockedRva);
    ButtonType         = At<TypeFn>(ButtonTypeRva);
    UnitInventory      = At<UnitInventoryFn>(UnitInventoryRva);
    UnitType           = At<UnitTypeFn>(UnitTypeRva);
    ItemPage           = At<ItemPageFn>(ItemPageRva);
    FirstItem          = At<FirstItemFn>(FirstItemRva);
    NextItem           = At<NextItemFn>(NextItemRva);
    StashDestination   = At<StashDestinationFn>(StashDestinationRva);
    FindChild          = At<FindWidgetFn>(FindChildRva);
    GetWidgetRect      = At<WidgetRectFn>(WidgetRectRva);
    OnlineState        = At<OnlineStateFn>(OnlineStateRva);
    ClientUnit         = At<ClientUnitFn>(ClientUnitRva);
    SharedRequest      = At<SharedRequestFn>(SharedRequestRva);
    ItemPosition       = At<ItemPositionFn>(ItemPositionRva);
    MarkBusy           = At<MarkBusyFn>(MarkBusyRva);
    RecordId           = At<RecordIdFn>(RecordIdRva);
    InventoryGrid      = At<InventoryGridFn>(InventoryGridRva);
    ItemCode           = At<ItemCodeFn>(ItemCodeRva);
    FindFreePosition   = At<FindFreePositionFn>(FindFreePositionRva);
    CursorItem         = At<CursorItemFn>(CursorItemRva);
    SharedTabCount     = At<SharedTabCountFn>(SharedTabCountRva);
    FirstRecord        = At<FirstRecordFn>(FirstRecordRva);
    NextRecord         = At<NextRecordFn>(NextRecordRva);
    RecordActive       = At<RecordByteFn>(RecordActiveRva);
    RecordRemoveOnly   = At<RecordByteFn>(RecordRemoveOnlyRva);
    RecordType         = At<RecordByteFn>(RecordTypeRva);
    SharedAllowed      = At<SharedAllowedFn>(SharedAllowedRva);
}

// ---------------------------------------------------------------------------
// State and counters
// ---------------------------------------------------------------------------

enum class PluginState {
    Loading,
    DisabledByConfig,
    UnsupportedBuild,
    ServicesMissing,
    InstallFailed,
    ButtonFailed,
    Active,
};

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::Loading:          return "loading";
    case PluginState::DisabledByConfig: return "disabled in the config";
    case PluginState::UnsupportedBuild: return "not installed, this D2R build does not match";
    case PluginState::ServicesMissing:  return "not installed, D2RLoader lacks a needed service";
    case PluginState::InstallFailed:    return "not installed, the inventory panel hook failed";
    case PluginState::ButtonFailed:     return "the button art, tooltip or click listener could not be registered";
    case PluginState::Active:           return "active";
    }
    return "unknown";
}

PluginState       State = PluginState::Loading;
std::string       VerifyFailures;
std::atomic<bool> Active{};

std::atomic<std::uint64_t> PanelUpdates{};     // inventory updates with the button in the panel
std::atomic<std::uint64_t> Presses{};          // button clicks
std::atomic<std::uint64_t> HotkeyPresses{};    // Controls key presses with the stash open
std::atomic<std::uint64_t> QueueFailures{};
std::atomic<std::uint64_t> ItemsSent{};
std::atomic<bool>          TransferQueued{};
std::atomic<std::uint64_t> EverythingPresses{};  // Shift+clicks and controller holds
std::atomic<std::uint64_t> SharedItemsSent{};

// What a press asks for. A press while a transfer is queued joins it, keeping
// the larger request.
enum class TransferMode : std::uint8_t {
    None       = 0,
    Materials  = 1,   // section 1 only
    Everything = 2,   // section 1, then section 7
};
std::atomic<std::uint8_t> QueuedMode{};

// Section 8: the controller button and how it was last activated.
enum class ControllerPress : std::uint8_t { None = 0, Tap = 1, Hold = 2 };
std::atomic<void*>        ControllerButton{};
std::atomic<std::uint8_t> LastControllerPress{};
bool                      ControllerHoldReady{};

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load(std::memory_order_relaxed));
}

// What the last inventory update found, for the console command.
struct PlacementView {
    bool        seen{};
    const char* layout{};
    const char* name{};
    bool        stashOpen{};
    bool        shown{};
    WidgetRect  button{};
};

// What the shared stash transfer did, for the console command.
struct SharedView {
    bool          ran{};
    bool          running{};
    const char*   stopped{};      // why it ended early, or null
    std::uint32_t pages{};        // shared pages that take items
    std::uint32_t sent{};
    std::uint32_t landed{};
    std::uint32_t refused{};      // the server kept them where they were
    std::uint32_t noRoom{};
    std::uint32_t tomes{};
    std::uint32_t cube{};
    std::uint32_t notAllowed{};
};

// What the last press did, for the console command.
struct TransferView {
    bool          ran{};
    const char*   stopped{};
    std::uint32_t carried{};
    std::uint32_t busy{};
    std::uint32_t notStashable{};
    std::uint32_t sent{};
    std::uint32_t refused{};
};

std::mutex    ViewMutex;
PlacementView LastPlacement;
TransferView  LastTransfer;
SharedView    LastShared;

// One warning per problem per session.
std::atomic<bool> WarnedNotButton{};

void WarnOnce(std::atomic<bool>& flag, const char* message) {
    if (!flag.exchange(true)) Context->LogWarn(message);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Transfer to Stash\n"
    "#\n"
    "# Adds a \"Transfer to Stash\" button to the inventory panel.\n"
    "#\n"
    "# What it does\n"
    "#   - A \"Transfer to Stash\" button in the inventory, shown only while the stash\n"
    "#     is open. The button itself is part of the mod's inventory layouts (see\n"
    "#     The buttons, below); this plugin gives it its art, tooltip and click.\n"
    "#   - Left click: does what Ctrl+click does with the stash open, for every\n"
    "#     Advanced Stash item in the inventory and in the Horadric Cube at once.\n"
    "#     The game picks the stash tab for each item.\n"
    "#   - Shift + left click: the same, then every other item in the inventory and\n"
    "#     in the cube goes to the shared stash (see below). Hovering the button\n"
    "#     shows this as a tooltip.\n"
    "#   - Controller: tap A on the button for a left click, hold A for a\n"
    "#     Shift + left click.\n"
    "#   - Keyboard key: Options > Controls > Stash > Transfer to Stash, no default\n"
    "#     key. It does the same as a left click, and only while the stash is open.\n"
    "#     Holding it down counts as one press.\n"
    "#\n"
    "# The buttons\n"
    "#   The mod's inventory layouts hold the buttons, so their place, size, font\n"
    "#   and d-pad links are set there. The plugin finds them by name:\n"
    "#     transfer_to_stash_button             keyboard/mouse inventory\n"
    "#     transfer_to_stash_controller_button  controller inventory\n"
    "#   An expansion layout lists the same names, like its other children. Each\n"
    "#   button needs the plugin's click message and art:\n"
    "#   keyboard/mouse button\n"
    "#     \"onClickMessage\": \"PanelManager:OpenPanel:CelestialRayOneTransferToStash\"\n"
    "#     \"filename\": \"d2rloader/celestialrayone.transfer-to-stash/transferbutton\"\n"
    "#     \"tooltipString\": \"@celestialrayone.transfer-to-stash:TransferButtonTooltip\"\n"
    "#   controller button\n"
    "#     \"onClickMessage\": \"PanelManager:OpenPanel:CelestialRayOneTransferToStashController\"\n"
    "#     \"filename\": \"d2rloader/celestialrayone.transfer-to-stash/transferbuttoncontroller\"\n"
    "#   The art: 4 frames each (normal, pressed, disabled, hovered), 368x58 for\n"
    "#   keyboard/mouse and 404x82 for controller, half that for lowend. A mod can\n"
    "#   replace it with its own files under\n"
    "#     data/hd/global/ui/d2rloader/celestialrayone.transfer-to-stash/\n"
    "#\n"
    "# Which items move on a left click\n"
    "#   - Items whose misc.txt row has AdvancedStashStackable = 1 and that have a\n"
    "#     slot in the stash layout (bankexpansionlayouthd.json). That is the same\n"
    "#     rule Ctrl+click uses.\n"
    "#   - Items already on their way to the stash (a transfer the server has not\n"
    "#     answered yet) are skipped.\n"
    "#   - The server still checks every item. A stash slot that is full keeps its\n"
    "#     items where they are.\n"
    "#\n"
    "# Shift + left click (and a controller hold)\n"
    "#   - First everything a left click does, then the rest of the inventory and\n"
    "#     the cube goes to the shared stash, the way Ctrl+click moves an item into\n"
    "#     a shared page.\n"
    "#   - One item at a time: the server picks the free spot itself and refuses a\n"
    "#     request for any other spot, so each item waits for the server's answer\n"
    "#     to the one before it. A full inventory takes a couple of seconds, a bit\n"
    "#     more in a TCP/IP game.\n"
    "#   - Shared tabs are filled in order, from the first one. An item that does not\n"
    "#     fit on a tab tries the next one. Remove-only and inactive tabs are\n"
    "#     skipped.\n"
    "#   - Materials whose Advanced Stash slot is full go to the shared stash too.\n"
    "#   - These stay where they are:\n"
    "#       equipped items and the belt (only the inventory and the cube are read)\n"
    "#       Tome of Town Portal and Tome of Identify\n"
    "#       the Horadric Cube itself (what is inside it does move)\n"
    "#       items the game does not allow in the shared stash, such as quest items\n"
    "#       items that fit on no shared tab\n"
    "#   - The cursor must be empty: while an item is on it the server refuses every\n"
    "#     shared stash request, so the transfer stops.\n"
    "#   - Closing the stash stops the transfer. What was already sent stays sent.\n"
    "#\n"
    "# Limits\n"
    "#   - Works in single player and in TCP/IP games, on the host and on the\n"
    "#     clients. Every request is one of the game's own Ctrl+click requests, so\n"
    "#     the host checks them as usual, and only the player who presses the\n"
    "#     button needs the plugin.\n"
    "#\n"
    "# Console command\n"
    "#   transfertostash   status, where the button was placed, the Controls key,\n"
    "#                     the controller hold, and what the last press sent to the\n"
    "#                     Advanced Stash and to the shared stash\n"
    "#\n"
    "[transfer_to_stash]\n"
    "\n"
    "# Master switch. false installs nothing.\n"
    "enabled = true\n";

constexpr std::uint32_t MaximumConfigBytes = 64 * 1024;

struct Settings {
    bool enabled = true;
};
Settings Config;
std::string ConfigProblems;

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != "transfer_to_stash") continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key == "enabled") {
            if (value == "true") Config.enabled = true;
            else if (value == "false") Config.enabled = false;
            else ConfigProblems += "enabled: expected true or false. ";
        }
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("TransferToStash: the config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("TransferToStash: the config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "TransferToStash: %s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

auto VerifyAll() -> bool {
    bool ok = true;
    const auto check = [&](const Witness& witness) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) return;
        ok = false;
        char line[160];
        std::snprintf(line, sizeof(line), "%s (%llX); ", witness.name, static_cast<unsigned long long>(witness.rva));
        VerifyFailures += line;
    };
    for (const Witness& witness : Witnesses) check(witness);
    if (!ok) D2RL::LogErrorF(Context, "TransferToStash: byte check failed: %s", VerifyFailures.c_str());
    return ok;
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

auto VirtualAt(void* widget, std::size_t slot) noexcept -> void* {
    void** vtable = Read<void**>(widget, 0);
    return vtable != nullptr ? vtable[slot] : nullptr;
}

// The game's own type test: the widget's type, then its parent types.
auto IsA(void* widget, void* type) noexcept -> bool {
    if (widget == nullptr || type == nullptr) return false;
    auto getType = reinterpret_cast<WidgetTypeFn>(VirtualAt(widget, VtableType));
    if (getType == nullptr) return false;
    void* current = getType(widget);
    for (std::size_t depth = 0; current != nullptr && depth < MaxTypeDepth; ++depth) {
        if (current == type) return true;
        current = Read<void*>(current, TypeParentOffset);
    }
    return false;
}

auto RectOf(void* widget) noexcept -> WidgetRect {
    WidgetRect rect{};
    GetWidgetRect(widget, &rect);
    return rect;
}

void SetShown(void* widget, bool shown) noexcept {
    const std::uint8_t wanted = shown ? 1 : 0;
    if (Read<std::uint8_t>(widget, WidgetVisibleOffset) != wanted) {
        reinterpret_cast<WidgetBoolFn>(VirtualAt(widget, VtableSetVisible))(widget, shown);
    }
    if (Read<std::uint8_t>(widget, WidgetEnabledOffset) != wanted) {
        reinterpret_cast<WidgetBoolFn>(VirtualAt(widget, VtableSetEnabled))(widget, shown);
    }
}

// ---------------------------------------------------------------------------
// Hook: the inventory panel update
// ---------------------------------------------------------------------------

// Console diagnostics: the direct children of the last inventory panel whose
// update found neither button, as 856220 sees them (name at +8h).
std::atomic<std::uint64_t> UpdatesWithoutButton{};
std::string                PanelChildren;     // under ViewMutex
const void*                PanelChildrenOf{};  // under ViewMutex

void NoteMissingButton(void* panel) {
    UpdatesWithoutButton.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(ViewMutex);
    if (panel == PanelChildrenOf) return;
    PanelChildrenOf = panel;
    PanelChildren.clear();
    void** const children = Read<void**>(panel, WidgetChildrenOffset);
    const auto count = Read<std::uint64_t>(panel, WidgetChildCountOffset);
    if (children == nullptr || count > MaxChildren) {
        PanelChildren = "(unreadable)";
        return;
    }
    for (std::uint64_t i = 0; i < count && PanelChildren.size() < 900; ++i) {
        if (!PanelChildren.empty()) PanelChildren += ", ";
        const char* name = children[i] != nullptr ? Read<const char*>(children[i], WidgetNameOffset) : nullptr;
        if (name == nullptr) PanelChildren += "?";
        else PanelChildren.append(name, strnlen(name, 96));
    }
}

void Arrange(void* panel) {
    const ButtonSpec* spec = nullptr;
    void* button = nullptr;
    for (const ButtonSpec& candidate : Buttons) {
        button = FindChild(panel, candidate.widgetName);
        if (button != nullptr) {
            spec = &candidate;
            break;
        }
    }
    if (button == nullptr) {         // neither button is loaded in this panel
        ControllerButton.store(nullptr, std::memory_order_relaxed);
        NoteMissingButton(panel);
        return;
    }
    ControllerButton.store(spec == &Buttons[1] ? button : nullptr, std::memory_order_relaxed);
    if (!IsA(button, ButtonType()) || Read<void*>(button, WidgetParentOffset) != panel) {
        WarnOnce(WarnedNotButton, "TransferToStash: the widget named after the button is not the plugin's button; "
                                  "nothing is placed.");
        return;
    }
    PanelUpdates.fetch_add(1, std::memory_order_relaxed);

    // The layout places the button; the plugin only shows it with the stash.
    PlacementView view{};
    view.seen      = true;
    view.layout    = spec->label;
    view.name      = spec->widgetName;
    view.stashOpen = UiStateOpen(StashUiState) != 0;
    view.button    = RectOf(button);
    view.shown     = view.stashOpen;
    SetShown(button, view.shown);

    std::lock_guard lock(ViewMutex);
    LastPlacement = view;
}

void PumpSharedTransfer();

std::uint64_t __fastcall HookedInventoryUpdate(void* panel) {
    if (Active.load(std::memory_order_relaxed) && panel != nullptr) {
        Arrange(panel);
        PumpSharedTransfer();
    }
    return OriginalInventoryUpdate(panel);
}

// Section 8. Only notes how the plugin's controller button was activated;
// the original always runs and does the activation itself.
std::uint64_t __fastcall HookedButtonInput(void* widget, void* message) {
    if (widget != nullptr && message != nullptr
            && widget == ControllerButton.load(std::memory_order_relaxed)
            && Read<std::uint64_t>(message, MessageCommandOffset) == ControllerActionCommand) {
        const void* payload = Read<const void*>(message, MessagePayloadOffset);
        if (payload != nullptr && Read<std::uint32_t>(payload, PayloadActionOffset) == 0) {
            const bool hold = Read<std::uint8_t>(payload, PayloadHoldOffset) != 0;
            LastControllerPress.store(static_cast<std::uint8_t>(hold ? ControllerPress::Hold : ControllerPress::Tap));
        }
    }
    return OriginalButtonInput(widget, message);
}

// ---------------------------------------------------------------------------
// The transfer
// ---------------------------------------------------------------------------

void FinishTransfer(TransferView& view, const char* stopped) {
    view.ran     = true;
    view.stopped = stopped;
    ItemsSent.fetch_add(view.sent, std::memory_order_relaxed);
    {
        std::lock_guard lock(ViewMutex);
        LastTransfer = view;
    }
    if (stopped != nullptr) {
        D2RL::LogInfoF(Context, "TransferToStash: nothing sent, %s.", stopped);
        return;
    }
    D2RL::LogInfoF(Context, "TransferToStash: %u sent, %u refused by the client check; %u items in the inventory and "
        "cube, %u busy, %u not for the Advanced Stash.", view.sent, view.refused, view.carried, view.busy,
        view.notStashable);
}

// ---------------------------------------------------------------------------
// Section 7: the shared stash transfer (UI thread only)
// ---------------------------------------------------------------------------

struct SharedPageRef {
    void*         proxy;
    std::uint32_t id;
};

struct SharedJob {
    bool                       active{};
    bool                       soundPlayed{};
    std::uint32_t              inFlight{};        // item id waiting for the server, 0 = none
    std::uint64_t              inFlightSince{};
    std::uint64_t              lastProgress{};
    std::vector<std::uint32_t> done;              // item ids handled in this run
    SharedView                 view{};
};

SharedJob                                  Job;
std::array<SharedPageRef, MaxSharedPages> PageScratch{};

auto UnitIdOf(void* unit) noexcept -> std::uint32_t {
    return Read<std::uint32_t>(unit, UnitIdOffset);
}

auto Handled(std::uint32_t id) noexcept -> bool {
    return std::find(Job.done.begin(), Job.done.end(), id) != Job.done.end();
}

void PublishShared() {
    std::lock_guard lock(ViewMutex);
    LastShared = Job.view;
}

void FinishShared(const char* stopped) {
    Job.active       = false;
    Job.view.running = false;
    Job.view.stopped = stopped;
    PublishShared();
    const SharedView& v = Job.view;
    if (stopped != nullptr) {
        D2RL::LogInfoF(Context, "TransferToStash: shared stash transfer stopped, %s (%u sent, %u arrived).", stopped,
            v.sent, v.landed);
        return;
    }
    D2RL::LogInfoF(Context, "TransferToStash: shared stash transfer done: %u sent, %u arrived, %u refused by the "
        "server, %u without room; kept %u tomes, %u cube, %u not allowed in the shared stash; %u shared pages take "
        "items.", v.sent, v.landed, v.refused, v.noRoom, v.tomes, v.cube, v.notAllowed, v.pages);
}

// An inventory or cube item of the player, by id.
auto FindCarried(void* inventory, std::uint32_t id) -> void* {
    std::size_t walked = 0;
    for (void* item = FirstItem(inventory); item != nullptr && ++walked <= MaxItemsWalked; item = NextItem(item)) {
        if (UnitType(item) != ItemUnitType) continue;
        const std::uint8_t page = ItemPage(item);
        if (page != InventoryPage && page != CubePage) continue;
        if (UnitIdOf(item) == id) return item;
    }
    return nullptr;
}

// The shared pages that take items, in tab order: the stash list entries that
// are neither the Advanced Stash nor remove-only are tabs 1, 2, 3 and so on, up
// to the shared tab count; of those, the active ones take items.
auto CollectSharedPages(void* inventory) -> std::size_t {
    const std::int32_t tabs   = SharedTabCount();
    std::int32_t       tab    = 0;
    std::size_t        count  = 0;
    std::size_t        walked = 0;
    for (void* record = FirstRecord(inventory); record != nullptr && ++walked <= MaxRecords;
            record = NextRecord(record)) {
        if (RecordType(record) == AdvancedStashRecord || RecordRemoveOnly(record) != 0) continue;
        if (++tab > tabs) break;
        if (RecordActive(record) == 0) continue;
        const std::uint32_t id    = RecordId(record);
        void* const         proxy = ClientUnit(id, PlayerUnitType);
        if (proxy == nullptr || count >= PageScratch.size()) continue;
        PageScratch[count++] = SharedPageRef{ proxy, id };
    }
    return count;
}

// One step: check the request in flight, then send the next item. Runs from
// the inventory panel update (every frame while it is open) and right after a
// press.
void PumpSharedTransfer() {
    if (!Job.active) return;
    const std::uint64_t now = GetTickCount64();
    if (UiStateOpen(StashUiState) == 0) return FinishShared("the stash was closed");
    void* const player = LocalPlayer(LocalDataContext());
    if (player == nullptr) return FinishShared("there is no local player");
    void* const inventory = UnitInventory(player, SourceFile, __LINE__);
    if (inventory == nullptr) return FinishShared("the player has no inventory");

    if (Job.inFlight != 0) {
        void* const item = FindCarried(inventory, Job.inFlight);
        if (item == nullptr) {
            ++Job.view.landed;
        } else if (InteractionBlocked(item) == 0) {
            ++Job.view.refused;
        } else if (now - Job.inFlightSince > SharedAnswerTimeoutMs) {
            return FinishShared("the server did not answer in time");
        } else {
            return;   // still on its way
        }
        Job.inFlight     = 0;
        Job.lastProgress = now;
        PublishShared();
    }

    if (CursorItem(inventory) != nullptr) return FinishShared("an item is on the cursor");
    const std::size_t pageCount = CollectSharedPages(inventory);
    Job.view.pages = static_cast<std::uint32_t>(pageCount);
    if (pageCount == 0) return FinishShared("no shared stash page takes items");
    const std::uint32_t expansion = OnlineState() ? 0u : 1u;

    bool        waiting = false;
    std::size_t walked  = 0;
    for (void* item = FirstItem(inventory); item != nullptr; item = NextItem(item)) {
        if (++walked > MaxItemsWalked) return FinishShared("the item list did not end");
        if (UnitType(item) != ItemUnitType) continue;
        const std::uint8_t page = ItemPage(item);
        if (page != InventoryPage && page != CubePage) continue;
        const std::uint32_t id = UnitIdOf(item);
        if (Handled(id)) continue;
        if (InteractionBlocked(item) != 0) {   // on its way somewhere, for example to an Advanced Stash tab
            waiting = true;
            continue;
        }
        const std::uint32_t code = ItemCode(item);
        if (code == TownPortalTomeCode || code == IdentifyTomeCode) {
            Job.done.push_back(id);
            ++Job.view.tomes;
            continue;
        }
        if (code == HoradricCubeCode) {
            Job.done.push_back(id);
            ++Job.view.cube;
            continue;
        }
        if (!SharedAllowed(player, item)) {
            Job.done.push_back(id);
            ++Job.view.notAllowed;
            continue;
        }
        for (std::size_t i = 0; i < pageCount; ++i) {
            void* const proxyInventory = UnitInventory(PageScratch[i].proxy, SourceFile, __LINE__);
            if (proxyInventory == nullptr) continue;
            const std::int32_t grid = InventoryGrid(PageScratch[i].proxy, StashPage, expansion);
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (FindFreePosition(proxyInventory, item, grid, &x, &y, StashPage) == 0) continue;
            const std::uint32_t cell = (static_cast<std::uint32_t>(x) & 0xFFFFu)
                                     | ((static_cast<std::uint32_t>(y) & 0xFFFFu) << 16);
            SharedRequest(SharedRequestCommand, id, PageScratch[i].id, page, ItemPosition(item), StashPage, cell, 0);
            MarkBusy(item);
            if (!Job.soundPlayed) {
                PlaySound(QuickMoveSound, nullptr, 0, 0, 0);
                Job.soundPlayed = true;
            }
            Job.done.push_back(id);
            Job.inFlight      = id;
            Job.inFlightSince = now;
            Job.lastProgress  = now;
            ++Job.view.sent;
            SharedItemsSent.fetch_add(1, std::memory_order_relaxed);
            PublishShared();
            return;
        }
        Job.done.push_back(id);
        ++Job.view.noRoom;
    }
    if (waiting && now - Job.lastProgress <= SharedAnswerTimeoutMs) {
        PublishShared();
        return;   // wait for items still on their way, they may come back
    }
    FinishShared(waiting ? "some items stayed busy" : nullptr);
}

void StartSharedTransfer(bool soundPlayed) {
    if (Job.active) {
        Context->LogInfo("TransferToStash: a shared stash transfer is already running; the press joins it.");
        return;
    }
    Job              = SharedJob{};
    Job.active       = true;
    Job.soundPlayed  = soundPlayed;
    Job.lastProgress = GetTickCount64();
    Job.view.ran     = true;
    Job.view.running = true;
    Job.done.reserve(256);
    PublishShared();
    PumpSharedTransfer();
}

// Section 1, for every item on page 0 or 3, then section 7 when asked for.
// Runs on the UI thread.
void __cdecl RunTransfer(const D2RL::PluginContext*, void*) noexcept {
    TransferQueued.store(false);
    const auto mode = static_cast<TransferMode>(QueuedMode.exchange(0));
    if (!Active.load(std::memory_order_relaxed)) return;
    const bool everything = mode == TransferMode::Everything;

    TransferView view{};
    if (UiStateOpen(StashUiState) == 0) return FinishTransfer(view, "the stash is closed");
    void* const player = LocalPlayer(LocalDataContext());
    if (player == nullptr) return FinishTransfer(view, "there is no local player");
    void* const inventory = UnitInventory(player, SourceFile, __LINE__);
    if (inventory == nullptr) return FinishTransfer(view, "the player has no inventory");
    void* const destination = StashDestination(player);
    if (destination == nullptr) {
        FinishTransfer(view, "the Advanced Stash is not loaded");
        if (everything) StartSharedTransfer(false);
        return;
    }

    std::array<void*, MaxBatch> batch{};
    std::size_t batchSize = 0;
    std::size_t walked    = 0;
    for (void* item = FirstItem(inventory); item != nullptr; item = NextItem(item)) {
        if (++walked > MaxItemsWalked) return FinishTransfer(view, "the item list did not end");
        if (UnitType(item) != ItemUnitType) continue;
        const std::uint8_t page = ItemPage(item);
        if (page != InventoryPage && page != CubePage) continue;
        ++view.carried;
        if (InteractionBlocked(item) != 0) {
            ++view.busy;
            continue;
        }
        if (!CanQuickMove(item)) {
            ++view.notStashable;
            continue;
        }
        if (batchSize < batch.size()) batch[batchSize++] = item;
    }

    for (std::size_t i = 0; i < batchSize; ++i) {
        void* const item = batch[i];
        TransferPlacement placement{};
        if (Transfer(item, destination, AdvancedStashPage, ItemPage(item), QuickMoveMode, &placement) != 0) {
            ++view.sent;
        } else {
            ++view.refused;
        }
    }
    if (batchSize != 0) PlaySound(QuickMoveSound, nullptr, 0, 0, 0);
    FinishTransfer(view, nullptr);
    if (everything) StartSharedTransfer(batchSize != 0);
}

// ---------------------------------------------------------------------------
// Services: the button layout, its click and the UI thread
// ---------------------------------------------------------------------------

const D2RL::ResourceService*    ResourceSvc{};
const D2RL::SharedEventService* EventSvc{};
const D2RL::ThreadService*      ThreadSvc{};
const D2RL::InputService*       InputSvc{};

std::atomic<D2RL::Input::ActionHandle> HotkeyAction{ D2RL::Input::InvalidHandle };
std::atomic<bool>                      HotkeyHeld{};

struct ButtonHandles {
    D2RL::Resources::RegistrationHandle sprite = D2RL::Resources::InvalidHandle;
    D2RL::Resources::RegistrationHandle lowend = D2RL::Resources::InvalidHandle;
};
std::array<ButtonHandles, 2>       Handles{};
D2RL::Resources::RegistrationHandle StringsHandle = D2RL::Resources::InvalidHandle;
D2RL::SharedEvents::ListenerHandle ClickListener = D2RL::SharedEvents::InvalidHandle;

auto QueryServices() -> bool {
    bool ok = true;
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize)) {
        Context->LogError("TransferToStash: D2RLoader has no resource service.");
        ok = false;
    }
    if (Context->QueryService(&EventSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasSharedEventServiceField(EventSvc, D2RL::SharedEventServiceRequiredSize)) {
        Context->LogError("TransferToStash: D2RLoader has no shared event service.");
        ok = false;
    }
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        Context->LogError("TransferToStash: D2RLoader has no thread service.");
        ok = false;
    }
    return ok;
}

// Both buttons and the Controls key end here. A press while a transfer is
// already queued joins that one, keeping the larger request.
auto QueueTransfer(TransferMode mode) noexcept -> bool {
    if (!Active.load(std::memory_order_relaxed)) return false;
    const auto   wanted  = static_cast<std::uint8_t>(mode);
    std::uint8_t current = QueuedMode.load();
    while (current < wanted && !QueuedMode.compare_exchange_weak(current, wanted)) {}
    if (TransferQueued.exchange(true)) return true;
    if (ThreadSvc->runOnUiThread(Context, &RunTransfer, nullptr) != D2RL::Threads::Result::Success) {
        TransferQueued.store(false);
        QueueFailures.fetch_add(1, std::memory_order_relaxed);
        Context->LogWarn("TransferToStash: the transfer could not be queued on the UI thread.");
        return false;
    }
    return true;
}

auto __cdecl OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
        -> D2RL::SharedEvents::UiMessageAction {
    if (event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
            || event->target == nullptr || event->command == nullptr || event->text == nullptr
            || ClickTarget != event->target || ClickCommand != event->command) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    const bool controller = ControllerClickText == event->text;
    if (!controller && ClickText != event->text) return D2RL::SharedEvents::UiMessageAction::Continue;

    // The message is private to this plugin, so it never goes on to PanelManager.
    // Keyboard/mouse: Shift held asks for everything. Controller: a hold does
    // (section 8).
    Presses.fetch_add(1, std::memory_order_relaxed);
    TransferMode mode = TransferMode::Materials;
    if (controller) {
        if (LastControllerPress.exchange(0) == static_cast<std::uint8_t>(ControllerPress::Hold)) {
            mode = TransferMode::Everything;
        }
    } else if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
        mode = TransferMode::Everything;
    }
    if (mode == TransferMode::Everything) EverythingPresses.fetch_add(1, std::memory_order_relaxed);
    (void)QueueTransfer(mode);
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

// The Controls key: the same transfer as the button, only while the stash is
// open. Key repeats while it is held are not presses.
auto __cdecl OnHotkey(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept
        -> D2RL::Input::ActionResult {
    if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind == D2RL::Input::ActionEventKind::Released) {
        HotkeyHeld.store(false);
        return D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind != D2RL::Input::ActionEventKind::Pressed || HotkeyHeld.exchange(true)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (!Active.load(std::memory_order_relaxed) || UiStateOpen(StashUiState) == 0) {
        return D2RL::Input::ActionResult::Ignored;
    }
    HotkeyPresses.fetch_add(1, std::memory_order_relaxed);
    return QueueTransfer(TransferMode::Materials) ? D2RL::Input::ActionResult::Handled
                                                  : D2RL::Input::ActionResult::Ignored;
}

auto RegisterHotkey() -> bool {
    if (Context->QueryService(&InputSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasInputServiceField(InputSvc, D2RL::InputServiceRequiredSize)) {
        InputSvc = nullptr;
        Context->LogWarn("TransferToStash: D2RLoader has no input service; there is no Controls key, the button "
                         "still works.");
        return false;
    }
    const D2RL::Input::ActionRegistration registration{
        .structSize       = D2RL::Input::ActionRegistrationSize,
        .flags            = 0,
        .logicalId        = "transfer-to-stash",
        .displayName      = "Transfer to Stash",
        .category         = "Stash",
        .defaultPrimary   = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .defaultSecondary = { .key = D2RL::Input::Key::None, .modifier = D2RL::Input::Modifier::None },
        .callback         = &OnHotkey,
        .userData         = nullptr,
    };
    D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
    const D2RL::Input::Result result = InputSvc->registerAction(Context, &registration, &handle);
    if (result != D2RL::Input::Result::Success || handle == D2RL::Input::InvalidHandle) {
        D2RL::LogErrorF(Context, "TransferToStash: the Controls entry was refused (result %u); the button still "
            "works.", static_cast<unsigned>(result));
        return false;
    }
    HotkeyAction.store(handle);
    return true;
}

void RemoveButton() {
    if (ClickListener != D2RL::SharedEvents::InvalidHandle && EventSvc != nullptr) {
        (void)EventSvc->unregisterUiMessageListener(Context, ClickListener);
    }
    ClickListener = D2RL::SharedEvents::InvalidHandle;
    if (StringsHandle != D2RL::Resources::InvalidHandle && ResourceSvc != nullptr) {
        (void)ResourceSvc->unregisterResource(Context, StringsHandle);
    }
    StringsHandle = D2RL::Resources::InvalidHandle;
    for (ButtonHandles& handles : Handles) {
        for (auto* handle : { &handles.sprite, &handles.lowend }) {
            if (*handle != D2RL::Resources::InvalidHandle && ResourceSvc != nullptr) {
                (void)ResourceSvc->unregisterResource(Context, *handle);
            }
            *handle = D2RL::Resources::InvalidHandle;
        }
    }
}

// An RCDATA resource of this DLL (plugin.rc), checked to be a sprite.
auto EmbeddedSprite(int id, const void*& bytes, std::uint64_t& size) -> bool {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&EmbeddedSprite), &module)) {
        return false;
    }
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    size  = found != nullptr ? SizeofResource(module, found) : 0;
    return bytes != nullptr && size > 40 && std::memcmp(bytes, "SpA1", 4) == 0;
}

auto RegisterSprite(int id, const char* path, D2RL::Resources::RegistrationHandle& handle) -> bool {
    const void*   bytes{};
    std::uint64_t size{};
    if (!EmbeddedSprite(id, bytes, size)) {
        D2RL::LogErrorF(Context, "TransferToStash: the embedded sprite %d is missing from the DLL.", id);
        return false;
    }
    const D2RL::Resources::ResourceRegistration sprite{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = path,
        .bytes      = bytes,
        .byteCount  = size,
    };
    const auto result = ResourceSvc->registerResource(Context, &sprite, &handle);
    if (result != D2RL::Resources::Result::Success) {
        D2RL::LogErrorF(Context, "TransferToStash: %s could not be registered (result %u).", path,
            static_cast<unsigned>(result));
        return false;
    }
    return true;
}

auto AddButton() -> bool {
    // The tooltip string, the art and the click listener for the layout's buttons.
    const std::string strings = TooltipStrings();
    const D2RL::Resources::ResourceRegistration stringFile{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = TooltipStringsPath,
        .bytes      = strings.data(),
        .byteCount  = strings.size(),
    };
    const auto stringsResult = ResourceSvc->registerResource(Context, &stringFile, &StringsHandle);
    if (stringsResult != D2RL::Resources::Result::Success) {
        D2RL::LogErrorF(Context, "TransferToStash: the tooltip strings could not be registered (result %u).",
            static_cast<unsigned>(stringsResult));
        RemoveButton();
        return false;
    }

    for (std::size_t i = 0; i < Buttons.size(); ++i) {
        const ButtonSpec& spec    = Buttons[i];
        ButtonHandles&    handles = Handles[i];
        if (!RegisterSprite(spec.spriteId, spec.spritePath, handles.sprite)
                || !RegisterSprite(spec.lowendSpriteId, spec.lowendSpritePath, handles.lowend)) {
            RemoveButton();
            return false;
        }
    }

    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags      = 0,
        .priority   = 10000,
        .reserved   = 0,
        .callback   = &OnUiMessage,
        .userData   = nullptr,
    };
    const auto listenerResult = EventSvc->registerUiMessageListener(Context, &listener, &ClickListener);
    if (listenerResult != D2RL::SharedEvents::Result::Success) {
        D2RL::LogErrorF(Context, "TransferToStash: the button click listener could not be registered (result %u).",
            static_cast<unsigned>(listenerResult));
        RemoveButton();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char line[400];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };

    std::snprintf(line, sizeof(line), "Transfer to Stash %s: %s.", PluginVersion, StateName(State));
    say(line);
    if (!VerifyFailures.empty()) {
        std::snprintf(line, sizeof(line), "  byte check failed: %s", VerifyFailures.c_str());
        say(line);
    }

    PlacementView placement;
    TransferView  transfer;
    SharedView    shared;
    {
        std::lock_guard lock(ViewMutex);
        placement = LastPlacement;
        transfer  = LastTransfer;
        shared    = LastShared;
    }

    if (!placement.seen) {
        say("  inventory panel: no button seen yet (open the inventory).");
    } else {
        std::snprintf(line, sizeof(line), "  %s button %s: at %d,%d, %dx%d, %s (stash %s); seen in %llu updates.",
            placement.layout != nullptr ? placement.layout : "?", placement.name != nullptr ? placement.name : "?",
            placement.button.x, placement.button.y, placement.button.width, placement.button.height,
            placement.shown ? "shown" : "hidden", placement.stashOpen ? "open" : "closed", Count(PanelUpdates));
        say(line);
    }

    if (Count(UpdatesWithoutButton) != 0) {
        std::string children;
        {
            std::lock_guard lock(ViewMutex);
            children = PanelChildren;
        }
        std::snprintf(line, sizeof(line), "  inventory panel without transfer_to_stash_button or transfer_to_stash_controller_button: %llu updates. "
            "Its children:",
            Count(UpdatesWithoutButton));
        say(line);
        say(("    " + children).c_str());
    }

    if (!transfer.ran) {
        say("  last press: none yet.");
    } else if (transfer.stopped != nullptr) {
        std::snprintf(line, sizeof(line), "  last press: nothing sent, %s.", transfer.stopped);
        say(line);
    } else {
        std::snprintf(line, sizeof(line), "  last press: %u sent, %u refused by the client check; %u items in the "
            "inventory and cube, %u busy, %u not for the Advanced Stash.", transfer.sent, transfer.refused,
            transfer.carried, transfer.busy, transfer.notStashable);
        say(line);
    }
    if (!shared.ran) {
        say("  shared stash: not used yet (Shift + left click, or hold A on a controller).");
    } else {
        std::snprintf(line, sizeof(line), "  shared stash%s: %u pages take items; %u sent, %u arrived, %u refused by "
            "the server, %u without room; kept %u tomes, %u cube, %u not allowed%s%s.",
            shared.running ? " (running)" : "", shared.pages, shared.sent, shared.landed, shared.refused,
            shared.noRoom, shared.tomes, shared.cube, shared.notAllowed,
            shared.stopped != nullptr ? "; stopped: " : "", shared.stopped != nullptr ? shared.stopped : "");
        say(line);
    }
    std::snprintf(line, sizeof(line), "  Controls key (Stash > Transfer to Stash): %s, %llu presses with the stash "
        "open.", HotkeyAction.load() != D2RL::Input::InvalidHandle ? "registered" : "not registered",
        Count(HotkeyPresses));
    say(line);
    std::snprintf(line, sizeof(line), "  controller hold: %s.", ControllerHoldReady
        ? "ready (tap sends materials, hold sends everything)" : "not available, a hold counts as a tap");
    say(line);
    std::snprintf(line, sizeof(line), "  totals: %llu button presses (%llu for everything), %llu items sent to the "
        "Advanced Stash, %llu to the shared stash, %llu presses not queued.", Count(Presses),
        Count(EverythingPresses), Count(ItemsSent), Count(SharedItemsSent), Count(QueueFailures));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Transfer to Stash",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "A Transfer to Stash button on the inventory panel, shown while the stash is open: a click sends "
                   "every Advanced Stash item in the inventory and the cube to the stash, Shift + click (or holding A "
                   "on a controller) then also moves everything else to the shared stash.",
    .flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("transfertostash", &StatusCommand,
            "Transfer to Stash status: where the button was placed, and what the last press sent.")) {
        Context->LogWarn("TransferToStash: the console command could not be registered.");
    }

    ReadConfiguration();
    if (!Config.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("TransferToStash: disabled by config.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("TransferToStash: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }

    // The hook goes in first: it shows the layout's button only while the stash
    // is open.
    if (!Context->InstallInlineHook(InventoryUpdateRva, InventoryUpdateBytes, sizeof(InventoryUpdateBytes),
            &HookedInventoryUpdate, &OriginalInventoryUpdate)) {
        State = PluginState::InstallFailed;
        Context->LogError("TransferToStash: the inventory panel hook could not be installed; nothing was installed.");
        return true;
    }
    Active.store(true);

    // Section 8, optional: without it a controller hold works like a tap.
    if (!Context->CheckExpectedBytes(ButtonInputRva, ButtonInputBytes, sizeof(ButtonInputBytes))) {
        Context->LogWarn("TransferToStash: ButtonWidget input (8F0F00) does not match; on a controller a hold works "
                         "like a tap.");
    } else if (!Context->InstallInlineHook(ButtonInputRva, ButtonInputBytes, sizeof(ButtonInputBytes),
            &HookedButtonInput, &OriginalButtonInput)) {
        Context->LogWarn("TransferToStash: the ButtonWidget input hook could not be installed; on a controller a "
                         "hold works like a tap.");
    } else {
        ControllerHoldReady = true;
    }

    if (!AddButton()) {
        State = PluginState::ButtonFailed;
        return true;
    }
    const bool hotkey = RegisterHotkey();

    State = PluginState::Active;
    D2RL::LogInfoF(Context, "TransferToStash %s: active (buttons %s and %s from the mod's layouts, Controls key %s, "
        "controller hold %s).", PluginVersion, Buttons[0].widgetName, Buttons[1].widgetName,
        hotkey ? "Stash > Transfer to Stash" : "none", ControllerHoldReady ? "ready" : "not available");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    if (Context == nullptr) return;
    const D2RL::Input::ActionHandle action = HotkeyAction.exchange(D2RL::Input::InvalidHandle);
    if (action != D2RL::Input::InvalidHandle && InputSvc != nullptr) {
        (void)InputSvc->unregisterAction(Context, action);
    }
    RemoveButton();
}
