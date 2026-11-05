// ---------------------------------------------------------------------------
// celestialrayone.whirlwind
//
// Whirlwind rework for Diablo II: Resurrected 3.3.93847.
//
//   ctc_while_uninterruptible  item procs fire while the caster is
//                              Uninterruptible (global), optional allow list
//                              and chance (the WhirlwindCTCChance behaviour)
//   proc_start_fix             a proc keeps the caster's used skill and mode
//   dual_wield_same_target     both weapons may strike the same target
//   cyclone                    Whirlwind follows the cursor while held
//                              (the movement stick on a controller)
//   death_while_whirlwinding   a lethal hit kills during Whirlwind
//   potions_while_whirlwinding potions can be used during Whirlwind
//   whirlwind_with_bows        sequence skills work with bows and crossbows
//   whirlwind_speed_sync       the server starts Whirlwind at the same movement
//                              speed the client does
//   pets_follow_whirlwind      summons, pets and hirelings follow a
//                              Whirlwinding player
//   gold_while_whirlwinding    Auto Gold Pickup keeps picking up gold during
//                              Whirlwind
//   whirlwind_hit_interval     a new formula for the frames between a
//                              player's Whirlwind hits (off by default)
//
// Every address is an RVA against the executable base. Nothing is written
// unless the bytes at the site, and at every game function the part calls,
// match the verified 3.3.93847 image as hosted by D2RLoader 1.3.0 byte for
// byte. A part whose bytes do not match installs nothing; the other parts are
// unaffected.
//
// D2RLoader 1.3.0 took over two game functions the cyclone server part calls:
// the skill resource payment 00436830 (now D2RCore ConsumeWideSkillResource)
// and the skill level reader 0033D1E0 (now D2RCore ReadWideSkillLevel). Their
// entries are loader jumps; the plugin still calls them there and checks the
// jump shape plus the untouched rest of each function, never the loader-owned
// pointer. The native code that calls them is unchanged.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const D2RL::PluginContext* g_context = nullptr;

struct Witness {
	std::uint64_t       rva;
	const std::uint8_t* bytes;
	std::uint32_t       size;
	const char*         what;
};

// ---------------------------------------------------------------------------
// Verified bytes (generated)
// ---------------------------------------------------------------------------

// Generated from bytes read out of the 3.3.93847 image as hosted by
// D2RLoader 1.3.0. Do not edit by hand.
constexpr std::uint8_t W_CastOnTarget_5896E0[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70,0x49,0x8B,0xF1,0x41,0x8B,0xE8,0x44,0x8B,0xF2,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x0F,0x84,0xF0,0x00,0x00,0x00,0x4D,0x85,0xC9,0x0F,0x84,0xE7,0x00,0x00,0x00,0xE8,0xE5,0x67,0xF0,0xFF,0x48,0x8B,0xF8,0x48,0x85,0xC0,0x0F,0x84,0xD6,0x00,0x00,0x00,0xBA,0x36,0x00,0x00,0x00,0x48,0x8B,0xCB,0xE8,0x7C,0xBA,0xDA,0xFF,0x85,0xC0};
constexpr std::uint8_t W_CastFailAtPos_5897FD[] = {0x33,0xC0,0x4C,0x8D,0x5C,0x24,0x70,0x49,0x8B,0x5B,0x28,0x49,0x8B,0x6B,0x30,0x49,0x8B,0x73,0x38,0x49,0x8B,0xE3,0x41,0x5F,0x41,0x5E,0x5F,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70,0x41,0x8B,0xE9,0x45,0x8B,0xF0,0x44,0x8B,0xFA,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x0F,0x84,0xCA,0x00,0x00,0x00,0xE8,0xB0,0x66,0xF0,0xFF,0x48,0x8B,0xF0,0x48,0x85,0xC0,0x0F,0x84,0xB9,0x00,0x00,0x00,0xBA,0x36,0x00,0x00,0x00,0x48,0x8B,0xCB,0xE8,0x47,0xB9,0xDA,0xFF,0x85,0xC0};
constexpr Witness CtcWitnesses[] = {
	{0x5896E0ULL, W_CastOnTarget_5896E0, 86, "CastOnTarget"},
	{0x5897FDULL, W_CastFailAtPos_5897FD, 110, "CastFailAtPos"},
};
constexpr std::uint8_t W_ProcCall_231219[] = {0xE8,0xA2,0x42,0x10,0x00,0x44,0x8B,0x84,0x24,0xC0,0x00,0x00,0x00,0x41,0xB9,0xFF,0xFF,0xFF,0xFF,0x41,0x8B,0xD6,0xC7,0x44,0x24,0x20,0x01,0x00,0x00,0x00,0x48,0x8B,0xCB};
constexpr std::uint8_t W_ProcCall_23123F[] = {0x85,0xC0,0x0F,0x84,0xBA,0x00,0x00,0x00,0x45,0x85,0xFF,0x74,0x11,0xBA,0x76,0x00,0x00,0x00,0x48,0x8B,0xCD,0x44,0x8D,0x42,0x8B,0xE8};
constexpr std::uint8_t W_ProcTail_231315[] = {0x49,0x8B,0xD4,0xE8,0xA3,0xCD,0x11,0x00,0xEB,0x0E,0x44,0x8B,0x44,0x24,0x44,0x8B,0x54,0x24,0x48,0xE8,0xA3,0xDF,0x11,0x00,0x8B,0x54,0x24,0x4C,0x48,0x8B,0xCB};
constexpr std::uint8_t W_ProcTail_231339[] = {0x4C,0x8B,0xA4,0x24,0xB0,0x00,0x00,0x00,0x8B,0xC6,0x48,0x83};
constexpr std::uint8_t W_SkillStartA_218490[] = {0x48,0x8B,0xCF,0xE8,0x38,0x35,0x13,0x00,0x48,0x8B,0xCF,0x84,0xC0,0x75,0x0C,0x45,0x85,0xFF,0x75,0x07,0xE8,0xF7,0xF8,0xFF,0xFF,0xEB,0x08,0x48,0x8B,0xD3,0xE8,0xBD,0x72,0x12,0x00,0x45,0x33,0xE4,0x8B,0xD8,0x48,0x8B,0xCF,0x85,0xC0,0x74,0x04,0x8B,0xD0,0xEB,0x3B,0xBA,0x0C,0x00,0x00,0x00,0xE8,0xE3,0xCC,0x11,0x00,0x85,0xC0,0x74,0x0C,0x0F,0xB6,0x46,0x25,0x84,0x05,0x01,0x12,0xB8,0x01,0x75,0x39,0x48,0x8B,0xCF,0xE8,0xEB,0x34,0x13,0x00,0x48,0x8B,0xCF,0x85,0xC0,0x74,0x0D,0xE8,0xDF,0x34,0x13,0x00};
constexpr std::uint8_t W_SkillStartB_2184F1[] = {0x83,0xF8,0x01,0x75,0x20,0x48,0x8B,0xCF,0xBA,0x01,0x00,0x00,0x00};
constexpr std::uint8_t W_SkillStartB_218503[] = {0x44,0x0F,0xB6,0xE0,0x45,0x85,0xE4,0x74,0x0A,0x8B,0xD3,0x48,0x8B,0xCF,0xE8,0x3A,0x12,0xEE,0xFF,0x48,0x0F,0xBF,0x8E,0x48,0x01,0x00,0x00,0x41,0xBE,0x01,0x00,0x00,0x00,0x66,0x85,0xC9,0x78,0x62,0x3B,0x0D,0x91,0x72,0x14,0x02,0x7D,0x5A,0x4C,0x8D,0x0D,0x58,0x70,0x14,0x02,0x4D,0x8B,0x0C,0xC9,0x4D,0x85,0xC9,0x74,0x4A,0x44,0x8B,0x45,0xB7,0x41,0x8B,0xD7,0x48,0x8B,0xCF,0x41,0xFF,0xD1,0x44,0x8B,0xF0,0x85,0xC0,0x75,0x36,0x33,0xD2,0x41,0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x8B,0xCF,0xE8,0xCB,0x6E,0x13,0x00,0x48,0x8B,0xCF,0xE8,0x63,0x34,0x13,0x00,0x48,0x8B,0xCF,0x85,0xC0,0x74,0x0D,0xE8,0x57,0x34,0x13,0x00,0x83,0xF8,0x01,0x75,0x0D,0x48,0x8B,0xCF,0xBA,0x01,0x00,0x00,0x00};
constexpr std::uint8_t W_SkillStartB_21858B[] = {0xE8,0x40,0x2D,0xE7,0xFF};
constexpr std::uint8_t W_GetUsedSkill_34BA40[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13};
constexpr Witness ProcFixWitnesses[] = {
	{0x231219ULL, W_ProcCall_231219, 33, "ProcCall"},
	{0x23123FULL, W_ProcCall_23123F, 26, "ProcCall"},
	{0x231315ULL, W_ProcTail_231315, 31, "ProcTail"},
	{0x231339ULL, W_ProcTail_231339, 12, "ProcTail"},
	{0x218490ULL, W_SkillStartA_218490, 97, "SkillStartA"},
	{0x2184F1ULL, W_SkillStartB_2184F1, 13, "SkillStartB"},
	{0x218503ULL, W_SkillStartB_218503, 131, "SkillStartB"},
	{0x21858BULL, W_SkillStartB_21858B, 5, "SkillStartB"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
};
constexpr std::uint8_t W_TickHead_5691C4[] = {0x49,0x8D,0x7F,0x30,0x45,0x33,0xFF,0x48,0x89,0x7C,0x24,0x60,0x44,0x89,0x7C,0x24,0x50,0xEB,0x4C,0xE8,0xD4,0xB0,0xEB,0xFF,0x48,0x8B,0xD0,0x48,0x8B,0xCB,0xE8,0x19,0x1A,0x00,0x00,0x41,0x03,0x85,0x70,0x01,0x00,0x00,0x48,0x8B,0xCB,0x41,0x89,0x47,0x2C,0xE8,0xC6,0xF5,0xDD,0xFF,0x33,0xC9,0x84,0xC0,0x0F,0x95,0xC1,0xFF,0xC1,0x49,0x8D,0x7F,0x30,0x89,0x4C,0x24,0x40,0x45,0x33,0xFF,0x48,0x89,0x7C,0x24,0x60,0x44,0x89,0x7C,0x24,0x50,0xEB,0x09,0x66,0x0F,0x1F,0x44,0x00,0x00,0x45,0x33,0xFF,0x8B,0x07,0x45,0x33,0xC9,0x4C,0x89,0x7C,0x24,0x38,0x45,0x33,0xC0,0x89,0x44,0x24,0x30,0x48,0x8B,0xD3,0xC7,0x44,0x24,0x28,0x03,0x00,0x00,0x00,0x49,0x8B,0xCD,0xC7,0x44,0x24,0x20,0x05,0x00,0x00,0x00,0xE8,0x81,0x9F,0xEC,0xFF,0x48,0x8B,0xF0,0x48,0x85,0xC0,0x75,0x13,0xC7,0x07,0xFF,0xFF,0xFF,0xFF,0x41,0x8B,0xFF};
constexpr std::uint8_t W_TickTail_569465[] = {0x8B,0x7C,0x24,0x40,0x45,0x33,0xC0,0x41,0x8B,0xD4,0x48,0x8B,0xCB,0xE8,0xC9,0x48,0xDD,0xFF,0x4C,0x8B,0xF8,0x48,0x85,0xC0,0x74,0x6F,0x48,0x8B,0xC8,0xE8,0x79,0x37,0xDD,0xFF,0x8B,0xC8,0x8B,0xD0,0x0F,0xBA,0xE9,0x0D,0x0F,0xBA,0xF0,0x0D,0x81,0xE2,0x00,0x20,0x00,0x00,0x0F,0x44,0xC1,0x49,0x8B,0xCF,0x8B,0xD0,0xE8,0x7A,0x57,0xDD,0xFF,0x8B,0x44,0x24,0x50,0xFF,0xC0,0x3B,0xC7,0x89,0x44,0x24,0x50,0x48,0x8B,0x7C,0x24,0x60};
constexpr std::uint8_t W_TickTail_5694BD[] = {0x8B,0x7C,0x24,0x44,0x8B,0x74,0x24,0x48,0x83,0xFF,0x03,0x75,0x1D,0x8B,0x44,0x24,0x4C,0x45,0x8B,0xCC,0x89,0x74,0x24,0x28};
constexpr Witness DualWieldWitnesses[] = {
	{0x5691C4ULL, W_TickHead_5691C4, 156, "TickHead"},
	{0x569465ULL, W_TickTail_569465, 82, "TickTail"},
	{0x5694BDULL, W_TickTail_5694BD, 24, "TickTail"},
};
constexpr std::uint8_t W_SqFrame_42A940[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x41,0x8B,0xF1,0x48,0x8B,0xCA,0x45,0x8B,0xF0,0x48,0x8B,0xDA,0xE8,0xD2,0x10,0xF2,0xFF,0x48,0x8B,0xE8,0x48,0x85,0xC0,0x74,0x72,0x8B,0xD6,0x48,0x8B,0xCB,0xE8,0x10,0x30,0xF2,0xFF,0x48,0x8B,0xCD,0xE8,0x78,0x22,0xF1,0xFF,0x8B,0xF0,0xA8,0x01,0x74,0x29,0x48,0x8B,0xD3,0x48,0x8B,0xCF,0xE8,0xB7,0x71,0x06,0x00,0x48,0x8B,0xD3,0x48,0x8B,0xCF};
constexpr std::uint8_t W_SqFrame_42A9A4[] = {0x83,0xF8,0x02,0x75,0x0E,0x0B,0xF0,0x48,0x8B,0xCD,0x8B,0xD6,0xE8,0x6B,0x42,0xF1,0xFF,0xEB,0x15,0x48,0x8B,0xCB,0xE8,0x91,0x53,0xF2,0xFF,0x85,0xC0,0x75,0x14,0x41,0x8D,0x46,0xFF,0x83,0xF8,0x01,0x77,0x0B,0x48,0x8B,0xD3,0x48,0x8B,0xCF,0xE8,0x39,0xC1,0x00,0x00,0x48,0x8B,0xCB,0xE8,0xE1,0x18,0xF2,0xFF,0x85,0xC0,0xB8,0x01,0x00,0x00,0x00,0x74,0x05,0xB8,0x02,0x00,0x00,0x00,0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x6C,0x24,0x38,0x48,0x8B,0x74,0x24,0x40,0x48,0x8B,0x7C,0x24,0x48,0x48,0x83,0xC4,0x20,0x41,0x5E,0xC3};
constexpr std::uint8_t W_ServerStart_56A4A0[] = {0x49,0x8B,0xCE,0xE8,0x08,0xAD,0xDC,0xFF,0x85,0xC0,0x0F,0x85,0xF3,0x03,0x00,0x00,0x49,0x8B,0xD6,0x49,0x8B,0xCF,0xE8,0x65,0x59,0xF2,0xFF,0x48,0x8B,0xD0,0x49,0x8B,0xCE,0x48,0x8B,0xF8,0xE8,0x07,0x03,0xDE,0xFF,0x48,0x85,0xFF,0x74,0x33,0x45,0x33,0xC9,0x44,0x8B,0xC0,0x48,0x8B,0xD7,0x49,0x8B,0xCE,0xE8,0x71,0xE1,0xDD,0xFF,0x85,0xC0};
constexpr std::uint8_t W_ServerStart_56A4E3[] = {0x41,0x8B,0xD4,0x49,0x8B,0xCE,0xE8,0xC2,0xE0,0xFC,0xFF,0x4C,0x8B,0xC7,0x49,0x8B,0xD6,0x49,0x8B,0xCF,0xE8,0xB4,0xE7,0xEC,0xFF,0xE9,0xA4,0x03,0x00,0x00,0x49,0x8B,0xCE,0xE8,0x77,0x09,0xDE,0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x0F,0x84,0x83,0x03,0x00,0x00,0x49,0x8B,0xCE,0xE8,0xB3,0x14,0xDE,0xFF,0x33,0xF6,0x85,0xC0,0x40,0x0F,0x94,0xC6,0x89,0x75,0xDF,0x48,0x85,0xFF,0x74,0x2C,0x85,0xF6,0x74,0x28,0x44,0x8B,0x45,0xDB,0x48,0x8B,0xCB,0x8B,0x55,0xD7,0xE8,0x10,0x85,0xDD,0xFF,0xBA,0x01,0x0C,0x00,0x00,0x48,0x8B,0xCB,0xE8,0xF3,0x81,0xDD,0xFF,0xBF,0x09,0x1C,0x00,0x00,0xE8,0x89,0x6D,0xDD,0xFF,0xEB,0x2E,0xB8,0x01,0x3C,0x00,0x00,0xBF,0x09,0x1C,0x00,0x00,0x85,0xF6,0xBA,0x01,0x0C,0x00,0x00,0x48,0x8B,0xCB,0x0F,0x45,0xC7,0x8B,0xF8,0xE8,0xC9,0x81,0xDD,0xFF,0x85,0xF6,0x74,0x07,0xE8,0x60,0x6D,0xDD,0xFF,0xEB,0x05,0xB8,0x07,0x00,0x00,0x00,0x8B,0xD0,0x48,0x8B,0xCB,0xE8,0xDF,0x84,0xDD,0xFF,0x41,0xB9,0x01,0x00,0x00,0x00,0x45,0x33,0xC0,0x49,0x8B,0xD6,0x48,0x8B,0xCB,0xE8,0xCB,0x68,0xDD,0xFF,0x85,0xC0,0x0F,0x84,0xD4,0x02,0x00,0x00,0x49,0x8B,0xCE,0xE8,0xCB,0xDB,0xEC,0xFF,0x8B,0xD0,0x4C,0x8D,0x05,0x52,0x3A,0x7D,0x01,0x41,0xB9,0x50,0x04,0x00,0x00,0x48,0x8B,0xCB,0xE8,0x14,0x85,0xDD,0xFF,0xBA,0x01,0x04,0x00,0x00,0x48,0x8B,0xCB,0xE8,0x67,0x81,0xDD,0xFF,0x48,0x8D,0x55,0xE7,0x48,0x8B,0xCB,0xE8,0xFB,0x74,0xDD,0xFF,0x85,0xC0,0x0F,0x8E,0x94,0x02,0x00,0x00,0xFF,0xC8,0x48,0x63,0xC8,0x48,0x8B,0x45,0xE7,0x48,0x8D,0x14,0x88,0x49,0x8B,0xCE,0x0F,0xB7,0x02,0x89,0x45,0xD7,0x0F,0xB7,0x42,0x02,0x89,0x45,0xDB,0xE8,0x61,0x72,0xDD,0xFF,0x8B,0x7D,0xDB,0x49,0x8B,0xCE,0x8B,0x75,0xD7,0x8B,0xD8,0xE8,0x21,0x0E,0xDE,0xFF,0x8B,0x4D,0xDF,0x44,0x8B,0xCB,0x83,0xF1,0x01,0x44,0x8B,0xC7,0xFF,0xC1,0x8B,0xD6,0xC1,0xE1,0x07,0x89,0x4C,0x24,0x20,0x48,0x8B,0xC8,0xE8,0xB2,0x96,0xDF,0xFF,0xBA,0x01,0x00,0x00,0x00,0x49,0x8B,0xCE,0xE8,0xD5,0x41,0xF2,0xFF,0xBA,0x12,0x00,0x00,0x00,0x49,0x8B,0xCE,0x44,0x8D,0x42,0xEF,0xE8,0x64,0xAE,0xDC,0xFF,0x48,0x8B,0x5D,0x67,0xBA,0x01,0x00,0x00,0x00,0x48,0x8B,0xCB,0xE8,0xB3,0x45,0xDD,0xFF,0x8B,0x45,0xD7,0x33,0xF6,0x89,0x43,0x24,0x8B,0x45,0xDB,0x89,0x43,0x28,0xC7,0x43,0x30,0xFF,0xFF,0xFF,0xFF,0x89,0x73,0x2C,0x66,0x41,0x83,0xBD,0xA0,0x00,0x00,0x00,0xFF,0x7C,0x19};
constexpr std::uint8_t W_SkillRecFlags_33CBC0[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x06,0x48,0x83,0x39,0x00,0x75,0x21,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24,0x30,0x00,0xE8,0xDD,0xA3,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x85,0xDB,0x75,0x08,0x33,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3,0x48,0x8B,0x03,0x48,0x83,0xC4,0x20,0x5B,0xC3,0xCC,0xCC,0x48,0x85,0xC9,0x74,0x04,0x8B,0x41,0x14,0xC3,0x33,0xC0,0xC3};
constexpr std::uint8_t W_SetFlags_33EC20[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0x48,0x8B,0xD9};
constexpr Witness ServerFrameWitnesses[] = {
	{0x42A940ULL, W_SqFrame_42A940, 95, "SqFrame"},
	{0x42A9A4ULL, W_SqFrame_42A9A4, 100, "SqFrame"},
	{0x56A4A0ULL, W_ServerStart_56A4A0, 65, "ServerStart"},
	{0x56A4E3ULL, W_ServerStart_56A4E3, 429, "ServerStart"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
	{0x33EC20ULL, W_SetFlags_33EC20, 15, "SetFlags"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
};
constexpr std::uint8_t W_Dispatcher_4F2FA0[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20,0x41,0x0F,0xB6,0x30,0x45,0x8B,0xE1,0x4D,0x8B,0xF8,0x48,0x8B,0xFA,0x4C,0x8B,0xE9,0xBB,0x01,0x00,0x00,0x00,0x40,0x80,0xFE,0x67,0x72,0x14,0x48,0x8D,0x4C,0x24,0x60,0xC6,0x44,0x24,0x60,0x00,0xE8,0x8A,0xE2,0xFE,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8D,0x15,0x0E,0xD0,0xB0,0xFF,0x4C,0x8D,0xB2,0x90,0xA7,0xD2,0x01,0x49,0x83,0x3C,0xF6,0x00,0x4D,0x8D,0x34,0xF6,0x75,0x1B,0x48,0x8D,0x4C,0x24,0x60,0xC6,0x44,0x24,0x60,0x00,0xE8,0x2D,0xE2,0xFE,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8D,0x15,0xE1,0xCF,0xB0,0xFF,0x8D,0x46,0xFF,0x83,0xF8,0x41,0x77,0x35,0x48,0x98,0x0F,0xB6,0x84,0x02,0xF0,0x30,0x4F,0x00,0x8B,0x8C,0x82,0xE0,0x30,0x4F,0x00,0x48,0x03,0xCA,0xFF,0xE1,0x48,0x8B,0xCF,0xE8,0x1B,0x7B,0xE5,0xFF,0x83,0xF8,0x11,0x75,0x45,0x33,0xDB,0xEB,0x41,0x48,0x8B,0xCF,0xE8,0x6A,0x92,0xE5,0xFF,0x8B,0xD8,0x85,0xC0,0xEB,0x23,0x48,0x8B,0xCF,0xE8,0x5C,0x92,0xE5,0xFF,0x85,0xC0,0x75,0x13,0x8D,0x50,0x36,0x48,0x8B,0xCF};
constexpr std::uint8_t W_Dispatcher_4F3073[] = {0x85,0xC0,0x75,0x04,0x33,0xDB,0xEB,0x14,0x40,0x80,0xFE,0x26,0x74,0x0E,0x8B,0xD6,0x48,0x8D,0x0D,0x0E,0x64};
constexpr std::uint8_t W_UseItem_4F40C0[] = {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8D,0xAC,0x24,0x58,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xA8,0x01,0x00,0x00,0x48,0x8B,0x05,0xE9,0x71,0x4D,0x02,0x48,0x33,0xC4,0x48,0x89,0x85,0x90,0x00,0x00,0x00,0x45,0x33,0xE4,0x48,0x89,0x4C,0x24,0x70,0xBB,0x05,0x00,0x00,0x00,0x4C,0x89,0x65,0x88,0x8B,0xF3,0x48,0x89,0x54,0x24,0x68,0x45,0x0F,0xB6,0xF1,0x4C,0x89,0x44,0x24,0x78,0x48,0x8D,0x7D,0xF0,0x66,0x90,0x48,0x8B,0xCF,0xE8,0x18,0xED,0xE8,0xFF,0x48,0x83,0xC7,0x10,0x48,0x83,0xEE,0x01,0x75,0xEE,0x48,0x8D,0x7D,0x40,0x48,0x8B,0xCF,0xE8,0x02,0xED,0xE8,0xFF,0x48,0x83,0xC7,0x10,0x48,0x83,0xEB,0x01,0x75,0xEE,0x48,0x8B,0x4C,0x24,0x68,0x8D,0x53,0x36};
constexpr std::uint8_t W_UseItem_4F4145[] = {0x85,0xC0,0x0F,0x85,0x36,0x1E,0x00,0x00,0x48,0x8B,0x4C,0x24,0x68,0xE8,0x69,0x81,0xE5,0xFF,0x85,0xC0,0x0F,0x85,0x24,0x1E,0x00,0x00,0x48,0x8B,0x44,0x24,0x78,0x8D,0x53,0x04,0x48,0x8B,0x4C,0x24,0x70,0x44,0x8B,0x40,0x0A,0xE8,0x0B,0xBD,0xF9,0xFF,0x48,0x89,0x45};
constexpr std::uint8_t W_CheckItemType_373890[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20};
constexpr Witness PotionWitnesses[] = {
	{0x4F2FA0ULL, W_Dispatcher_4F2FA0, 206, "Dispatcher"},
	{0x4F3073ULL, W_Dispatcher_4F3073, 21, "Dispatcher"},
	{0x4F40C0ULL, W_UseItem_4F40C0, 128, "UseItem"},
	{0x4F4145ULL, W_UseItem_4F4145, 51, "UseItem"},
	{0x373890ULL, W_CheckItemType_373890, 24, "CheckItemType"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};
constexpr std::uint8_t W_SeqResolver_3CB890[] = {0x40,0x55,0x56,0x48,0x83,0xEC,0x38,0x48,0x8B,0xF1,0xE8,0xA1,0x01,0xF8,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x57,0x01,0x00,0x00,0x48,0x8B,0xD0,0x48,0x8B,0xCE,0xE8,0x0D,0x23,0xF7,0xFF,0x48,0x63,0xE8,0x85,0xC0,0x0F,0x8E,0x41,0x01,0x00,0x00,0x48,0x89,0x5C,0x24,0x50,0x48,0x8B,0xCE,0x48,0x89,0x7C,0x24,0x60,0xE8,0x00,0x01,0xF8,0xFF,0x33,0xFF,0x85,0xC0,0x0F,0x85,0xC9,0x00,0x00,0x00,0x4C,0x89,0x74,0x24,0x68,0x48,0x8D,0x15,0xFA,0x12,0x94,0x01,0x4C,0x89,0x7C,0x24,0x30,0x41,0xB8,0x5C,0x06,0x00,0x00,0x4C,0x8D,0x3D,0x08,0x47,0xC3,0xFF,0x48,0x8B,0xCE,0x4D,0x8B,0xB4,0xEF,0x50,0x66,0x38,0x02,0x49,0x8B,0x06,0x0F,0xB6,0x58,0x02,0xE8,0x51,0xEA,0xF7,0xFF,0x48,0x8B,0xD0,0xC7,0x44,0x24,0x20,0x01,0x00,0x00,0x00,0x44,0x8B,0xCB,0x4C,0x8D,0x44,0x24,0x58,0x48,0x8B,0xCE,0xE8,0xD6,0xB4,0xFE,0xFF,0x8B,0x4C,0x24,0x58,0x48,0x8D,0x05,0xFF,0xAD,0xFB,0x01,0x48,0x8D,0x15,0x68,0xAE,0xFB,0x01,0x0F,0x1F,0x40,0x00,0x3B,0x08,0x74,0x12,0xFF,0xC7,0x48,0x83,0xC0,0x08,0x48,0x3B,0xC2,0x7C,0xF1,0xBB,0xFF,0xFF,0xFF,0xFF,0xEB,0x10,0x48,0x63,0xC7,0x41,0x8B,0x9C,0xC7,0x30,0x67,0x38,0x02,0x83,0xFB,0xFF,0x75,0x14,0x48,0x8D,0x4C,0x24,0x58,0xC6,0x44,0x24,0x58,0x00,0xE8,0x9B,0xFC,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x4C,0x8B,0x7C,0x24,0x30,0x48,0x63,0xC3,0x48,0x8B,0x5C,0x24,0x50};
constexpr std::uint8_t W_SeqResolver_3CB98F[] = {0x4C,0x8B,0x74,0x24,0x68,0x48,0x8B,0xC7,0x48,0x8B,0x7C,0x24,0x60,0x48,0x83,0xC4,0x38,0x5E,0x5D,0xC3,0x83,0xF8,0x01,0x75,0x43,0x41,0xB8,0x83,0x06,0x00,0x00,0x48,0x8D,0x15,0x2B,0x12,0x94,0x01,0x48,0x8B,0xCE,0xE8,0xA3,0xDE,0xF7,0xFF,0x48,0x8B,0xCE,0x8B,0xD8,0xE8,0x19,0xE7,0xF7,0xFF,0x8B,0xD3,0x0F,0xB6,0xC8,0xE8,0x0F,0xBD,0xCC,0xFF,0x48,0x85,0xC0,0x74,0x15,0x48,0x8B,0xCE,0xE8,0x02,0xE7,0xF7,0xFF,0x0F,0xB6,0xC8,0x8B,0xD5,0xE8,0xF8,0xB5,0xFC,0xFF,0x48,0x8B,0xF8,0x48,0x8B,0x5C,0x24,0x50,0x48,0x8B,0xC7,0x48,0x8B,0x7C,0x24,0x60,0x48,0x83,0xC4,0x38,0x5E,0x5D,0xC3,0x33,0xC0,0x48,0x83,0xC4,0x38,0x5E,0x5D,0xC3};
constexpr Witness SequenceWitnesses[] = {
	{0x3CB890ULL, W_SeqResolver_3CB890, 247, "SeqResolver"},
	{0x3CB98FULL, W_SeqResolver_3CB98F, 121, "SeqResolver"},
};
constexpr std::uint8_t W_ItemUseLock_1C7360[] = {0x48,0x83,0xEC,0x28,0xBA,0x02,0x00,0x00,0x00,0xE8,0xA2,0xE4,0x12,0x00,0x48,0x85,0xC0,0x75,0x29,0xE8,0x58,0x3F,0xEC,0xFF,0x8B,0xC8,0xE8,0x01,0x31,0xED,0xFF,0x48,0x85,0xC0,0x74,0x11,0xBA,0x36,0x00,0x00,0x00,0x48,0x8B,0xC8};
constexpr std::uint8_t W_ItemUseLock_1C7391[] = {0x85,0xC0,0x75,0x07,0x33,0xC0,0x48,0x83,0xC4,0x28,0xC3,0xB8,0x01,0x00,0x00,0x00,0x48,0x83,0xC4};
constexpr std::uint8_t W_LocalPlayer_230A90[] = {0xFF,0x0F,0x84,0xF5,0x02,0x00,0x00,0x48,0x85,0xC0,0x0F,0x84,0xEC,0x02,0x00,0x00,0xE8,0x2B,0xA8,0xE5,0xFF,0x8B,0xC8,0xE8,0xD4,0x99,0xE6,0xFF,0x4C,0x3B,0xF0,0x75,0x29,0x41,0xB8,0x1A,0x05,0x00,0x00,0x48,0x8D,0x15,0xF2,0x30,0xAB,0x01,0x49,0x8B,0xCF,0xE8,0xBA,0xD5,0x10,0x00,0x3B,0xC7,0x75,0x10,0x49,0x8B,0xCF,0xE8,0x2E,0xC1,0x10,0x00,0xA8,0x01};
constexpr Witness PotionClientWitnesses[] = {
	{0x1C7360ULL, W_ItemUseLock_1C7360, 44, "ItemUseLock"},
	{0x1C7391ULL, W_ItemUseLock_1C7391, 19, "ItemUseLock"},
	{0x230A90ULL, W_LocalPlayer_230A90, 68, "LocalPlayer"},
	{0x373890ULL, W_CheckItemType_373890, 24, "CheckItemType"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};
// 00436830: jmp qword [rip+disp32] into D2RCore ConsumeWideSkillResource, the
// loader's four NOPs, then the untouched body. disp32 is loader-owned.
constexpr std::uint8_t W_PayThunk_436830[] = {0xFF,0x25};
constexpr std::uint8_t W_PayBody_436836[] = {0x90,0x90,0x90,0x90,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x48,0x8B,0xF1,0x45,0x8B,0xF1};
constexpr std::uint8_t W_ManaCost_33AA00[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x41,0x8B,0xF8,0x33,0xDB,0xE8,0x7C,0xCD,0xD5,0xFF};
// 0033D1E0: jmp qword [rip+disp32] into D2RCore ReadWideSkillLevel, then the
// untouched body. disp32 is loader-owned.
constexpr std::uint8_t W_SkillLevelThunk_33D1E0[] = {0xFF,0x25};
constexpr std::uint8_t W_SkillLevelBody_33D1E6[] = {0x48,0x8B,0xF9,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30};
// 0043B427, native skill start 0043B3E0: skills.txt record from the game's data
// context (game+106h), then, unless it is an item cast, the cast gate
// 00436750(unit), refusing the cast when it returns 0.
constexpr std::uint8_t W_CastGateCall_43B427[] = {0x0F,0xB6,0x8D,0x06,0x01,0x00,0x00,0x41,0x8B,0xD6,0xE8,0x5A,0xC3,0xC5,0xFF,0x48,0x8B,0xF8,0x48,0x85,0xC0,0x74,0x7E,0x83,0xBC,0x24,0x90,0x00,0x00,0x00,0x00,0x75,0x0C,0x48,0x8B,0xCB,0xE8,0x00,0xB3,0xFF,0xFF,0x85,0xC0,0x74,0x68};
// 0043B59E, same function, after the start function succeeded: unless it is an
// item cast, pay through 00436830(game, unit, skill id, level) with 32-bit id
// and level, and ignore the result.
constexpr std::uint8_t W_PayCall_43B59E[] = {0x83,0xBC,0x24,0x90,0x00,0x00,0x00,0x00,0x75,0x11,0x45,0x8B,0xCC,0x45,0x8B,0xC6,0x48,0x8B,0xD3,0x48,0x8B,0xCD,0xE8,0x77,0xB2,0xFF,0xFF,0x0F,0xB6,0x47,0x24};
constexpr std::uint8_t W_CollisionReset_3636D0[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30};
constexpr std::uint8_t W_PathSetTarget_342A50[] = {0x48,0x85,0xC9,0x74,0x11,0x66,0x89,0x51,0x10,0x66,0x44,0x89,0x41,0x12,0x48,0xC7,0x41,0x70,0x00,0x00,0x00,0x00,0xC3};
constexpr std::uint8_t W_PathGetX_341A20[] = {0x0F,0xB7,0x41,0x02,0xC3};
constexpr std::uint8_t W_PathGetY_341A30[] = {0x0F,0xB7,0x41,0x06,0xC3};
// 00342AE0 PATH_SetVelocity: refuses while the owner's used skill has flag
// 0x1000, else velocity at path+A0h and max velocity at path+A8h.
constexpr std::uint8_t W_PathSetVelocity_342AE0[] = {0x48,0x85,0xC9,0x74,0x55,0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x8B,0xFA,0x48,0x8B,0x49,0x40,0x48,0x85,0xC9,0x74,0x18,0xE8,0x3E,0x8F,0x00,0x00,0x48,0x85,0xC0,0x74,0x0E,0x48,0x8B,0xC8,0xE8,0xF1,0xA0,0xFF,0xFF,0x0F,0xBA,0xE0,0x0C,0x72,0x1B,0x3B,0xBB,0xA0,0x00,0x00,0x00,0x74,0x07,0xC7,0x43,0x4C,0x0F,0x00,0x00,0x00,0x89,0xBB,0xA0,0x00,0x00,0x00,0x89,0xBB,0xA8,0x00,0x00,0x00};
// 00350B40 UNITS_UpdateAnimRateAndVelocity, the game's movement speed function.
constexpr std::uint8_t W_SpeedFnEntry_350B40[] = {0x48,0x85,0xC9,0x0F,0x84,0x6A,0x01,0x00,0x00,0x55,0x56,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x68,0x8B,0x01,0x48,0x8B,0xF1,0x89,0x45,0x30,0x83,0xF8,0x03,0x0F,0x8F,0x4A,0x01,0x00,0x00,0x8B,0x41,0x04,0x4C,0x8D,0x4D,0xC8,0x89,0x45,0xCC};
// 00350FDC..003510B9, same function: the velocity branch (stat 96 through
// 003516C0 index 4, plus stat 67, floor 25) and the gate that sends a unit in a
// passive-skill mode there when its used skill has (flags & 0x1001) == 1.
constexpr std::uint8_t W_SpeedFnVelocity_350FDC[] = {0x83,0x3E,0x05,0x75,0x12,0x48,0x8D,0x4D,0x18,0x44,0x88,0x75,0x18,0xE8,0xB2,0x43,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x4C,0x8B,0x7E,0x38,0x4D,0x85,0xFF,0x0F,0x84,0x8C,0xFC,0xFF,0xFF,0xBA,0x04,0x00,0x00,0x00,0x48,0x8B,0xCE,0xE8,0xB3,0x06,0x00,0x00,0x45,0x33,0xC0,0x48,0x8B,0xCE,0x8B,0xF8,0x41,0x8D,0x50,0x43,0xE8,0x02,0x40,0xFA,0xFF,0x49,0x8B,0xCF,0x03,0xF8,0xE8,0xB8,0x0C,0xFF,0xFF,0x83,0xF8,0x11,0x75,0x17,0x49,0x8B,0xCF,0xE8,0xAB,0x08,0xFF,0xFF,0x66,0x0F,0x6E,0xCF,0x0F,0x5B,0xC9,0xF3,0x0F,0x59,0xC1,0xF3,0x0F,0x2C,0xF8,0x8B,0x4D,0x30,0xB8,0x19,0x00,0x00,0x00,0x8B,0x5D,0xC8,0x3B,0xF8,0x0F,0x4C,0xF8,0x85,0xC9,0x0F,0x84,0x84,0x05,0x00,0x00,0x83,0xF9,0x01,0x0F,0x84,0x5A,0x05,0x00,0x00,0x41,0x8B,0xCE,0xE9,0x83,0x05,0x00,0x00,0x48,0x63,0xC3,0x48,0x8D,0x3D,0x09,0xF6,0x9A,0x01,0x48,0x8D,0x0C,0x80,0x49,0x8D,0x04,0x8F,0x44,0x39,0x70,0x04,0x0F,0x85,0x53,0xFF,0xFF,0xFF,0x44,0x39,0x30,0x0F,0x84,0x9B,0xFE,0xFF,0xFF,0x48,0x8B,0x8E,0x00,0x01,0x00,0x00,0xE8,0xF2,0xCF,0xFE,0xFF,0x48,0x85,0xC0,0x74,0x29,0x48,0x8B,0xC8,0xE8,0x55,0xBB,0xFE,0xFF,0x25,0x01,0x10,0x00,0x00,0x83,0xF8,0x01,0x0F,0x84,0x23,0xFF,0xFF,0xFF};
// 01D00518: player mode 18 (SQ) row of the speed rules table 01D003B0, five
// dwords: by passive skill 1, velocity 0, skill attack rate 1, attack rate 0,
// anim speed 0.
constexpr std::uint8_t W_PlayerModeSpeedRules_1D00518[] = {0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
// 01D00B90: move entry of the rate stat table 01D00B60: diminished, 150, stat 96.
constexpr std::uint8_t W_MoveRateStat_1D00B90[] = {0x01,0x00,0x00,0x00,0x96,0x00,0x00,0x00,0x60,0x00,0x00,0x00};
constexpr Witness CycloneServerWitnesses[] = {
	{0x56A4A0ULL, W_ServerStart_56A4A0, 65, "ServerStart"},
	{0x56A4E3ULL, W_ServerStart_56A4E3, 429, "ServerStart"},
	{0x42A940ULL, W_SqFrame_42A940, 95, "SqFrame"},
	{0x42A9A4ULL, W_SqFrame_42A9A4, 100, "SqFrame"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
	{0x33EC20ULL, W_SetFlags_33EC20, 15, "SetFlags"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x436830ULL, W_PayThunk_436830, 2, "PayThunk"},
	{0x436836ULL, W_PayBody_436836, 18, "PayThunk"},
	{0x33AA00ULL, W_ManaCost_33AA00, 20, "ManaCost"},
	{0x33D1E0ULL, W_SkillLevelThunk_33D1E0, 2, "SkillLevelThunk"},
	{0x33D1E6ULL, W_SkillLevelBody_33D1E6, 12, "SkillLevelThunk"},
	{0x43B427ULL, W_CastGateCall_43B427, 45, "CastGateCall"},
	{0x43B59EULL, W_PayCall_43B59E, 31, "PayCall"},
	{0x3636D0ULL, W_CollisionReset_3636D0, 20, "CollisionReset"},
	{0x342A50ULL, W_PathSetTarget_342A50, 23, "PathSetTarget"},
	{0x341A20ULL, W_PathGetX_341A20, 5, "PathGetX"},
	{0x341A30ULL, W_PathGetY_341A30, 5, "PathGetY"},
	{0x342AE0ULL, W_PathSetVelocity_342AE0, 80, "PathSetVelocity"},
	{0x350B40ULL, W_SpeedFnEntry_350B40, 45, "SpeedFunction"},
	{0x350FDCULL, W_SpeedFnVelocity_350FDC, 221, "SpeedFunction"},
	{0x1D00518ULL, W_PlayerModeSpeedRules_1D00518, 20, "SpeedRules"},
	{0x1D00B90ULL, W_MoveRateStat_1D00B90, 12, "MoveRateStat"},
};
constexpr std::uint8_t W_ClientStart_230AE0[] = {0xCE,0x48,0x8D,0x55,0xC0,0xE8,0x96,0x41,0xFE,0xFF,0x85,0xC0,0x0F,0x84,0x9A,0x02,0x00,0x00,0x49,0x8B,0xCE,0xE8,0x76,0x9A,0xE6,0xFF,0x48,0x8B,0xD0,0x49,0x8B,0xCE,0x48,0x8B,0xF8,0xE8,0xC8,0x9C,0x11,0x00,0x48,0x85,0xFF,0x74,0x25,0x45,0x33,0xC9,0x44,0x8B,0xC0,0x48,0x8B,0xD7,0x49,0x8B,0xCE,0xE8,0x32,0x7B,0x11,0x00,0x85,0xC0};
constexpr std::uint8_t W_ClientStart_230B22[] = {0x48,0x8B,0xD7,0x49,0x8B,0xCE,0xE8,0xC3,0xE6,0xFF,0xFF,0xE9,0xC6,0x01,0x00,0x00,0x48,0x89,0xB4,0x24,0x98,0x00,0x00,0x00,0x49,0x8B,0xCE,0x4C,0x89,0xA4,0x24,0xA0,0x00,0x00,0x00,0xE8,0x86,0xAE,0x11,0x00,0x33,0xF6,0x85,0xC0,0x40,0x0F,0x94,0xC6,0x85,0xF6,0x74,0x1D,0x48,0x85,0xFF,0x74,0x0F,0x44,0x8B,0x45,0xC4,0x48,0x8B,0xCB,0x8B,0x55,0xC0,0xE8,0xE6,0x1E,0x11,0x00,0xE8,0x71,0x07,0x11,0x00,0x8B,0xF8,0xEB,0x05,0xBF,0x07,0x00,0x00,0x00,0xB8,0x01,0x3C,0x00,0x00,0xB9,0x09,0x1C,0x00,0x00,0x85,0xF6,0x44,0x8B,0xE6,0xBA,0x01,0x0C,0x00,0x00,0x0F,0x45,0xC1,0x41,0x83,0xF4,0x01,0x41,0xFF,0xC4,0x89,0x45,0xC8,0x48,0x8B,0xCB,0x41,0xC1,0xE4,0x07,0xE8,0x9B,0x1B,0x11,0x00,0x8B,0xD7,0x48,0x8B,0xCB,0xE8,0xC1,0x1E,0x11,0x00,0x48,0x8B,0xD3,0x49,0x8B,0xCE,0xE8,0x36,0x53,0xFE,0xFF,0x84,0xC0,0x0F,0x84,0x19,0x01,0x00,0x00,0x49,0x8B,0xCE,0x4C,0x89,0x6C,0x24,0x68,0xE8,0x11,0x95,0x11,0x00,0x48,0x8D,0x15,0xDA,0x2F,0xAB,0x01,0x44,0x0F,0xB6,0xE8,0x49,0x8B,0xCE,0x85,0xF6,0x0F,0x84,0x91,0x00,0x00,0x00,0x41,0xB8,0x4D,0x05,0x00,0x00,0xE8,0x70,0x8C,0x11,0x00,0x41,0x0F,0xB6,0xCD,0x48,0x63,0xF8,0xE8,0x94,0xFE,0x0C,0x00,0x48,0x8B,0xF0,0x85,0xFF,0x78,0x09,0x48,0x3B,0xB8,0x48,0x12,0x00,0x00,0x72,0x1A,0x48,0x8D,0x4D,0x38,0xC6,0x45,0x38,0x00,0xE8,0xF7,0x48,0xE5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x85,0xFF,0x0F,0x88,0x82,0x00,0x00,0x00,0x48,0x8B,0xC7,0x48,0x3B,0xBE,0x48,0x12,0x00,0x00,0x73,0x76,0x48,0x8D,0xBE,0x40,0x12,0x00,0x00,0x48,0x89,0x45,0x38,0x48,0x3B,0x47,0x08,0x72,0x1A,0x48,0x8D,0x45,0x38,0x48,0x89,0x7D,0xE0,0x48,0x8D,0x4D,0xD8,0x48,0x89,0x45,0xD8,0xE8,0xB8,0x54,0xE5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x69,0x45,0x38,0xD0,0x00,0x00,0x00,0x48,0x03,0x07,0x74,0x3E,0x0F,0xB6,0x50,0x3C,0x41,0xB9,0x50,0x05,0x00,0x00,0xEB,0x20,0x41,0xB8,0x55,0x05,0x00,0x00,0xE8,0xDF,0x8B,0x11,0x00,0x8B,0xD0,0x41,0x0F,0xB6,0xCD,0xE8,0x54,0x6A,0xE6,0xFF,0x41,0xB9,0x56,0x05,0x00,0x00,0x0F,0xBF,0x50,0x68,0x4C,0x8D,0x05,0x13,0x2F,0xAB,0x01,0xC1,0xE2,0x08,0x48,0x8B,0xCB,0xE8,0x38,0x1E,0x11,0x00,0xBA,0x01,0x04,0x00,0x00,0x48,0x8B,0xCB,0xE8,0x8B,0x1A,0x11,0x00,0x48,0x8D,0x55,0xD0,0x48,0x8B,0xCB,0xE8,0x1F,0x0E,0x11,0x00,0x4C,0x8B,0x6C,0x24,0x68,0x85,0xC0,0x75,0x44,0x48,0x8D,0x4D,0x38,0x88,0x45,0x38,0xE8,0x3A,0xDA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x8B,0x55,0xC8,0x48,0x8B,0xCB,0xE8,0x5A,0x1A,0x11,0x00,0x33,0xC0,0x48,0x8B,0xB4,0x24,0x98,0x00,0x00,0x00,0x4C,0x8B,0xA4,0x24,0xA0,0x00,0x00,0x00,0x4C,0x8B,0x7C,0x24,0x60,0x48,0x8B,0x9C,0x24,0x90,0x00,0x00,0x00,0x48,0x83,0xC4,0x70,0x41,0x5E,0x5F,0x5D,0xC3,0xFF,0xC8,0x48,0x63,0xC8,0x48,0x8B,0x45,0xD0,0x48,0x8D,0x14,0x88,0x49,0x8B,0xCE,0x0F,0xB7,0x02,0x89,0x45,0xC0,0x0F,0xB7,0x42,0x02,0x89,0x45,0xC4,0xE8,0x40,0x0B,0x11,0x00,0x8B,0x7D,0xC4,0x49,0x8B,0xCE,0x8B,0x75,0xC0,0x8B,0xD8,0xE8,0x00,0xA7,0x11,0x00,0x44,0x8B,0xCB,0x44,0x89,0x64,0x24,0x20,0x44,0x8B,0xC7,0x8B,0xD6,0x48,0x8B,0xC8,0xE8,0x9B,0x2F,0x13,0x00,0xBA,0x01,0x00,0x00,0x00,0x49,0x8B,0xCF,0xE8,0xBE,0xDE,0x10,0x00,0x8B,0x4D,0xC0,0x33,0xD2,0x41,0x89,0x4F,0x24,0x8B,0x4D,0xC4,0x41,0x89,0x4F,0x28,0x49,0x8B,0xCE,0xE8,0xB6,0x40,0xFE,0xFF,0x49,0x8B,0xCE,0xE8,0x4E,0x68,0xFE,0xFF,0xB8,0x01,0x00,0x00,0x00,0xE9};
constexpr std::uint8_t W_ClientUpdate_FFCE1[] = {0x0F,0x84,0x8B,0x00,0x00,0x00,0x48,0x8B,0xC8,0xE8,0x11,0xCF,0x23,0x00,0x8B,0xD8,0x41,0x84,0xC6,0x74,0x41,0x41,0x0F,0xB6,0xD6,0x48,0x8B,0xCE};
constexpr std::uint8_t W_ClientUpdate_FFD02[] = {0x85,0xC0,0x74,0x18,0x83,0xCB,0x02,0x48,0x8B,0xCF,0x8B,0xD3,0xE8,0x0D,0xEF,0x23,0x00,0x48,0x8B,0xCE,0xE8,0x45,0x6F};
constexpr std::uint8_t W_IssuerTail_FAFFE[] = {0x01,0x00,0x00,0x48,0x8B,0xCB,0xE8,0xB7,0x98,0x1F,0x00,0x44,0x0F,0xB7,0xCE,0xBA,0x48,0x01,0x00,0x00,0x48,0x8B,0xCB,0x44,0x8D,0x40,0x01,0xE8,0xF2,0xCC,0x1F,0x00,0x45,0x8B,0xCE,0x45,0x8B,0xC7,0x48,0x8B,0xD3,0x41,0x8B,0xCD,0xE8,0xD1,0x0F,0x00,0x00,0x45,0x84,0xE4};
constexpr std::uint8_t W_GetLeftSkill_34A540[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13};
constexpr std::uint8_t W_GetRightSkill_34B400[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13};
constexpr std::uint8_t W_SendCommand_FC000[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30};
// Starting heading (client). Read out of the 3.3.93847 image as hosted by
// D2RLoader 1.3.1.
// 003411EB..00341225, in the path compute 00340E70, on its success path only:
// the target the compute was asked for (+10h/+12h) is saved at +14h/+16h, then
// +10h/+12h become the last route point. So +14h/+16h keep the subtile a
// Whirlwind was cast at until the next successful compute.
constexpr std::uint8_t W_PathSaveTarget_3411EB[] = {0x0F,0xB7,0x43,0x10,0x66,0x89,0x43,0x14,0x0F,0xB7,0x43,0x12,0x66,0x89,0x43,0x16,0x44,0x89,0x6B,0x4C,0x41,0xF6,0xC0,0x10,0x75,0x21,0x4C,0x39,0x6B,0x70,0x75,0x1B,0x48,0x63,0xCA,0x0F,0xB7,0x84,0x8B,0xC4,0x00,0x00,0x00,0x66,0x89,0x43,0x10,0x0F,0xB7,0x84,0x8B,0xC6,0x00,0x00,0x00,0x66,0x89,0x43,0x12};
// 003416D3..00341702, in the path's face-a-point 003416C0 (rbx = rcx = path):
// the path's first qword is the exact position, x at +0 and y at +4 in 16.16
// subtiles, and a subtile's centre is (subtile << 16) + 8000h.
constexpr std::uint8_t W_PathFaceToPoint_3416D3[] = {0xC1,0xE2,0x10,0x48,0x8B,0xD9,0x48,0x8B,0x09,0x81,0xC2,0x00,0x80,0x00,0x00,0x41,0xC1,0xE0,0x10,0x41,0x8B,0xF9,0x41,0x81,0xC0,0x00,0x80,0x00,0x00,0x89,0x54,0x24,0x30,0x44,0x89,0x44,0x24,0x34,0x48,0x8B,0x54,0x24,0x30,0xE8,0xAD,0xF4,0x03,0x00};
// 00349B41..00349B6D, in UNITS_GetDirection 00349B20 (rbx = unit): for a
// player, the facing of the path at unit+38h, through 00341950.
constexpr std::uint8_t W_UnitsGetDirection_349B41[] = {0x8B,0x0B,0x83,0xE9,0x02,0x74,0x18,0x83,0xE9,0x02,0x74,0x13,0x83,0xF9,0x01,0x74,0x0E,0x48,0x8B,0x4B,0x38,0x48,0x83,0xC4,0x20,0x5B,0xE9,0xF0,0x7D,0xFF,0xFF,0x48,0x8B,0x43,0x38,0x0F,0xB6,0x40,0x20,0x48,0x83,0xC4,0x20,0x5B,0xC3};
// 00341955..00341965, in PATH_GetDirection 00341950: the facing, 0 to 63, a
// float at path+80h, truncated.
constexpr std::uint8_t W_PathGetDirection_341955[] = {0x0F,0x57,0xC0,0xF3,0x0F,0x2C,0xC0,0xC3,0xF3,0x0F,0x2C,0x81,0x80,0x00,0x00,0x00,0xC3};
// 00214D41..00214E07, in the skill target getter 00214C80: with no target the
// game aims 2 subtiles ahead of the unit. Its facing (00349B20) shifted right
// by 3 picks one of 8 offsets, x from 01CE2220/01CE2200, y from
// 01CE2210/01CE21F0.
constexpr std::uint8_t W_AheadPoint_214D41[] = {0xE8,0xDA,0x4D,0x13,0x00,0x66,0x0F,0x6F,0x05,0xD2,0xD4,0xAC,0x01,0x66,0x0F,0x6F,0x0D,0xAA,0xD4,0xAC,0x01,0x0F,0xB6,0xD8,0xC1,0xEB,0x03,0xF3,0x0F,0x7F,0x44,0x24,0x28,0x66,0x0F,0x6F,0x05,0xA6,0xD4,0xAC,0x01,0xF3,0x0F,0x7F,0x4C,0x24,0x38,0x66,0x0F,0x6F,0x0D,0x78,0xD4,0xAC,0x01,0xF3,0x0F,0x7F,0x44,0x24,0x48,0xF3,0x0F,0x7F,0x4C,0x24,0x58,0x48,0x85,0xFF,0x75,0x0F,0x48,0x8D,0x4C,0x24,0x20,0xE8,0x9D,0x0A,0xE7,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x8B,0x0F,0x83,0xE9,0x02,0x74,0x20,0x83,0xE9,0x02,0x74,0x1B,0x83,0xF9,0x01,0x74,0x16,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x09,0xE8,0x69,0xCC,0x12,0x00,0x8B,0xC8,0xEB,0x0B,0x8B,0xCE,0xEB,0x07,0x48,0x8B,0x47,0x38,0x8B,0x48,0x10,0x48,0x8D,0x1C,0x9D,0x00,0x00,0x00,0x00,0x03,0x4C,0x1C,0x28,0x41,0x89,0x0E,0x8B,0x0F,0x83,0xE9,0x02,0x74,0x1C,0x83,0xE9,0x02,0x74,0x17,0x83,0xF9,0x01,0x74,0x12,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x10,0xE8,0x3C,0xCC,0x12,0x00,0x8B,0xF0,0xEB,0x07,0x48,0x8B,0x47,0x38,0x8B,0x70,0x14,0x8B,0x4C,0x1C,0x48,0x03,0xCE,0x41,0x89,0x0F};
// 01CE21F0..01CE222F: those offset tables.
constexpr std::uint8_t W_AheadOffsets_1CE21F0[] = {0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr Witness CycloneClientWitnesses[] = {
	{0x230AE0ULL, W_ClientStart_230AE0, 64, "ClientStart"},
	{0x230B22ULL, W_ClientStart_230B22, 614, "ClientStart"},
	{0x0FFCE1ULL, W_ClientUpdate_FFCE1, 28, "ClientUpdate"},
	{0x0FFD02ULL, W_ClientUpdate_FFD02, 23, "ClientUpdate"},
	{0x230A90ULL, W_LocalPlayer_230A90, 68, "LocalPlayer"},
	{0x0FAFFEULL, W_IssuerTail_FAFFE, 52, "IssuerTail"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x34A540ULL, W_GetLeftSkill_34A540, 14, "GetLeftSkill"},
	{0x34B400ULL, W_GetRightSkill_34B400, 14, "GetRightSkill"},
	{0x0FC000ULL, W_SendCommand_FC000, 24, "SendCommand"},
	{0x3411EBULL, W_PathSaveTarget_3411EB, 59, "PathSaveTarget"},
	{0x3416D3ULL, W_PathFaceToPoint_3416D3, 48, "PathFaceToPoint"},
	{0x349B41ULL, W_UnitsGetDirection_349B41, 45, "UnitsGetDirection"},
	{0x341955ULL, W_PathGetDirection_341955, 17, "PathGetDirection"},
	{0x214D41ULL, W_AheadPoint_214D41, 199, "AheadPoint"},
	{0x1CE21F0ULL, W_AheadOffsets_1CE21F0, 64, "AheadOffsets"},
	{0x3636D0ULL, W_CollisionReset_3636D0, 20, "CollisionReset"},
	{0x342A50ULL, W_PathSetTarget_342A50, 23, "PathSetTarget"},
	{0x341A20ULL, W_PathGetX_341A20, 5, "PathGetX"},
	{0x341A30ULL, W_PathGetY_341A30, 5, "PathGetY"},
	{0x342AE0ULL, W_PathSetVelocity_342AE0, 80, "PathSetVelocity"},
	{0x350B40ULL, W_SpeedFnEntry_350B40, 45, "SpeedFunction"},
	{0x350FDCULL, W_SpeedFnVelocity_350FDC, 221, "SpeedFunction"},
	{0x1D00518ULL, W_PlayerModeSpeedRules_1D00518, 20, "SpeedRules"},
	{0x1D00B90ULL, W_MoveRateStat_1D00B90, 12, "MoveRateStat"},
};
// 0056A420, server Whirlwind start (srvstfunc 38): skills.txt record 00097790,
// token 0033DD40, target 00432B70. The native path block follows at 0056A4A0.
constexpr std::uint8_t W_ServerStartHead_56A420[] = {0x48,0x89,0x5C,0x24,0x10,0x44,0x89,0x4C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x4C,0x8B,0xF2,0x4C,0x8B,0xF9,0x0F,0xB6,0x89,0x06,0x01,0x00,0x00,0x41,0x8B,0xD0,0x45,0x8B,0xE0,0xE8,0x37,0xD3,0xB2,0xFF,0x4C,0x8B,0xE8,0x48,0x85,0xC0,0x0F,0x84,0x3E,0x04,0x00,0x00,0x45,0x33,0xC0,0x41,0x8B,0xD4,0x49,0x8B,0xCE,0xE8,0xCD,0x38,0xDD,0xFF,0x48,0x89,0x45,0x67,0x48,0x85,0xC0,0x0F,0x84,0x18,0x04,0x00,0x00,0x4C,0x8D,0x4D,0xDB,0x49,0x8B,0xD6,0x4C,0x8D,0x45,0xD7,0x49,0x8B,0xCF,0xE8,0xDD,0x86,0xEC,0xFF,0x85,0xC0,0x0F,0x84,0xFD,0x03,0x00,0x00,0xBA,0x0C,0x00,0x00,0x00};
constexpr Witness SpeedSyncWitnesses[] = {
	{0x56A420ULL, W_ServerStartHead_56A420, 128, "ServerStart"},
	{0x56A4A0ULL, W_ServerStart_56A4A0, 65, "ServerStart"},
	{0x56A4E3ULL, W_ServerStart_56A4E3, 429, "ServerStart"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x342AE0ULL, W_PathSetVelocity_342AE0, 80, "PathSetVelocity"},
	{0x350B40ULL, W_SpeedFnEntry_350B40, 45, "SpeedFunction"},
	{0x350FDCULL, W_SpeedFnVelocity_350FDC, 221, "SpeedFunction"},
	{0x1D00518ULL, W_PlayerModeSpeedRules_1D00518, 20, "SpeedRules"},
	{0x1D00B90ULL, W_MoveRateStat_1D00B90, 12, "MoveRateStat"},
};
// 00429B8E..00429CBB, inside the player walk/run/knockback frame handler
// 00429A40 (player mode table 0238E340, rows 2, 3, 6 and 19). Every 25 ms,
// once the player stands more than sqrt(45) subtiles from the newest point,
// it writes the position into the 20-slot owner trail in the player data
// (0034B240): write index byte at +110h, GetTickCount64 of the last write at
// +118h, points at +120h as x, y dwords. The pet follow code reads that trail
// (005BC940 mode 1, 005C1460 mode 1). Witnessed for those offsets and for the
// calls it makes: 0034B240, 00341A20 / 00341A30 (path x / y) and 003412C0.
constexpr std::uint8_t W_RunFrameTrail_429B8E[] = {0xE8,0x8D,0x3B,0xDE,0x00,0x48,0x8B,0xCF,0x48,0x89,0x44,0x24,0x38,0x4C,0x8B,0xF0,0xE8,0x9D,0x16,0xF2,0xFF,0x45,0x33,0xFF,0x48,0x8B,0xD8,0x48,0x8B,0x88,0x18,0x01,0x00,0x00,0x48,0x83,0xC1,0x19,0x4C,0x3B,0xF1,0x0F,0x8E,0x0C,0x01,0x00,0x00,0x4C,0x89,0x64,0x24,0x78,0x4C,0x89,0xAC,0x24,0x80,0x00,0x00,0x00,0x44,0x0F,0xB6,0xA8,0x10,0x01,0x00,0x00,0x45,0x84,0xED,0x75,0x05,0x41,0xB4,0x13,0xEB,0x04,0x45,0x8D,0x65,0xFF,0x48,0x85,0xFF,0x75,0x0F,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x41,0xBC,0xC5,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x8B,0x17,0x8B,0xCA,0x83,0xE9,0x02,0x74,0x1F,0x83,0xE9,0x02,0x74,0x1A,0x83,0xF9,0x01,0x74,0x15,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x14,0xE8,0x0B,0x7E,0xF1,0xFF,0x8B,0x17,0x44,0x8B,0xF8,0xEB,0x08,0x48,0x8B,0x47,0x38,0x44,0x8B,0x78,0x10,0x83,0xEA,0x02,0x74,0x22,0x83,0xEA,0x02,0x74,0x1D,0x83,0xFA,0x01,0x74,0x18,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x0A,0xE8,0xEF,0x7D,0xF1,0xFF,0x44,0x8B,0xF0,0xEB,0x0D,0x45,0x33,0xF6,0xEB,0x08,0x48,0x8B,0x47,0x38,0x44,0x8B,0x70,0x14,0x41,0x0F,0xB6,0xC4,0x41,0x8B,0xD6,0x41,0x8B,0xCF,0x44,0x8B,0x8C,0xC3,0x24,0x01,0x00,0x00,0x44,0x8B,0x84,0xC3,0x20,0x01,0x00,0x00,0xE8,0x4E,0x76,0xF1,0xFF,0x4C,0x8B,0x64,0x24,0x78,0x83,0xF8,0x2D,0x7E,0x42,0x48,0x8B,0x44,0x24,0x38,0x46,0x89,0xBC,0xEB,0x20,0x01,0x00,0x00,0x41,0xBF,0x00,0x00,0x00,0x00,0x46,0x89,0xB4,0xEB,0x24,0x01,0x00,0x00,0xFE,0x83,0x10,0x01,0x00,0x00,0x80,0xBB,0x10,0x01,0x00,0x00,0x14,0x48,0x89,0x83,0x18,0x01,0x00,0x00,0x0F,0xB6,0x83,0x10,0x01,0x00,0x00,0x41,0x0F,0x43,0xC7,0x88,0x83,0x10,0x01,0x00,0x00};
// 003412C0: (x2 - x1)^2 + (y2 - y1)^2, 32-bit.
constexpr std::uint8_t W_SquaredDistance_3412C0[] = {0x44,0x2B,0xC1,0x44,0x2B,0xCA,0x45,0x0F,0xAF,0xC9,0x45,0x0F,0xAF,0xC0,0x43,0x8D,0x04,0x08,0xC3};
constexpr Witness PetTrailWitnesses[] = {
	{0x429B8EULL, W_RunFrameTrail_429B8E, 302, "RunFrameTrail"},
	{0x3412C0ULL, W_SquaredDistance_3412C0, 19, "SquaredDistance"},
	{0x341A20ULL, W_PathGetX_341A20, 5, "PathGetX"},
	{0x341A30ULL, W_PathGetY_341A30, 5, "PathGetY"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};
// Gold while Whirlwinding (client). 000FE656..000FE6AE, inside the click
// dispatcher 000FE3B0, on the path of actions 7 and 0: the press (001023D0), the
// left skill (0034A540), then the "can act now" gate 0009A8C0 called at 000FE689
// with rcx = player and rdx = left skill. A refusal (000FE690 je 000FE6A9) skips
// the click. The call itself is the redirected site, checked by the patch.
constexpr std::uint8_t W_ClickGate_FE656[] = {0x8B,0x54,0x24,0x58,0x83,0xCA,0x05,0x48,0x8D,0x4C,0x24,0x58,0x89,0x54,0x24,0x58,0xE8,0x65,0x3D,0x00,0x00,0x85,0xC0,0x0F,0x84,0xD3,0x01,0x00,0x00,0x48,0x8B,0x4C,0x24,0x60,0xE8,0xC3,0xBE,0x24,0x00,0x48,0x8B,0x4C,0x24,0x60,0x48,0x8B,0xD0,0x48,0x89,0x45,0x80};
constexpr std::uint8_t W_ClickGate_FE68E[] = {0x85,0xC0,0x74,0x17,0x81,0x4C,0x24,0x58,0x80,0x00,0x00,0x00,0x48,0x8D,0x4C,0x24,0x58,0xE8,0xEC,0x3D,0x00,0x00,0xE9,0x9D,0x01,0x00,0x00,0xE8,0x62,0x0B,0x05,0x00};
// 0009A8C0, the gate: refuses while the player is in a skill mode, Whirlwind's
// mode 18 included. It reads only rcx.
constexpr std::uint8_t W_CanAct_9A8C0[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,0xB8,0xE8,0x0A,0x00,0x00,0x48,0x8D,0x15,0x24,0x2B,0xC2,0x01,0x48,0x8B,0xD9,0xE8,0x7C,0xFA,0x2A,0x00,0x48,0x8B,0xC8,0xE8,0x84,0xE1,0x2E,0x00,0x48,0x85,0xC0};
// 001932A2..001932DC, Auto Gold tick 00193010: the gold being picked up becomes
// the Auto Gold target (+18h) at 001932BE, the click (action 7) is issued at
// 001932CC, and the target is cleared at 001932D1. The 5-byte call at 001932CC
// is left out: yin's StatGoldPickup and AutoGoldPickup redirect it. Their
// handler still calls 000FE3B0 for every mode but neutral and cast, so a
// Whirlwinding player's click reaches 000FE689 either way.
constexpr std::uint8_t W_AutoGoldClick_1932A2[] = {0x66,0x89,0x44,0x24,0x30,0xBA,0x07,0x00,0x00,0x00,0xF3,0x44,0x0F,0x2C,0xCE,0xC6,0x44,0x24,0x30,0x00,0x49,0x8B,0xCE,0x0F,0xB7,0x44,0x24,0x30,0x48,0x89,0x7E,0x18,0x66,0x89,0x44,0x24,0x20,0xF3,0x44,0x0F,0x2C,0xC7};
constexpr std::uint8_t W_AutoGoldClick_1932D1[] = {0x48,0xC7,0x46,0x18,0x00,0x00,0x00,0x00,0x40,0xB5,0x01};
// 00144640, the client context getter. Auto Gold sits at context+1918h.
constexpr std::uint8_t W_ClientContext_144640[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00,0x8B,0x0D,0x73,0xFE,0x46,0x03,0xBA,0x6C,0x12,0x00,0x00,0x48,0x8B,0x0C,0xC8,0x8B,0x04,0x0A,0x39,0x05,0x61,0x40,0x94,0x02,0x7F,0x0D,0x48,0x8D,0x05,0x40,0xA4,0x90,0x02};
// 00192B20: mov rax,[rcx+18h] / ret, the getter the game uses for the Auto
// Gold target.
constexpr std::uint8_t W_AutoGoldTarget_192B20[] = {0x48,0x8B,0x41,0x18,0xC3};
// 00101A38..00101AE8, in the click on a unit 00101910: with Auto Gold Pickup on
// and the item being the Auto Gold target, the native click sends the pickup
// (000FABE0 with 16h, player, unit type, unit id at 00101ADF) and returns.
constexpr std::uint8_t W_AutoGoldNative_101A38[] = {0xE8,0x73,0xE8,0xFD,0xFF,0x48,0x8B,0xC8,0xE8,0x4B,0xCA,0xC2,0x00,0x85,0xC0,0x74,0x26,0xE8,0xF2,0x2B,0x04,0x00,0x48,0x8D,0x88,0x18,0x19,0x00,0x00,0xE8,0xC6,0x10,0x09,0x00,0x8B,0xD5,0x8B,0xCE,0x48,0x8B,0xD8,0xE8,0x6A,0x8B,0xF9,0xFF,0x48,0x3B,0xD8,0x75,0x04,0xB3,0x01,0xEB,0x02,0x32,0xDB,0x45,0x84,0xF6,0x74,0x5A,0x84,0xDB,0x75,0x56,0x8B,0xD6,0x8B,0xCD,0xE8,0xCD,0x07,0x00,0x00,0x85,0xC0,0x75,0x39,0x48,0x8B,0x57,0x08,0x44,0x8B,0xCE,0x8B,0x4F,0x24,0x44,0x8B,0xC5,0xE8,0x47,0x91,0xFF,0xFF,0xB1,0x01,0xE8,0x30,0xBF,0xF8,0xFF,0x48,0x8B,0x57,0x08,0x45,0x33,0xF6,0x44,0x89,0x74,0x24,0x20,0x44,0x8B,0xCE,0x44,0x8B,0xC5,0x41,0x8D,0x4E,0x16,0xE8,0xC5,0x7B,0xFF,0xFF,0xE8,0xE0,0xFA,0xF8,0xFF,0x4C,0x8B,0x7C,0x24,0x70,0x48,0x83,0xC4,0x30,0x41,0x5E,0x5F,0x5E,0x5D,0x5B,0xC3,0x48,0x8B,0x57,0x08,0x44,0x8B,0xCE,0x44,0x8B,0xC5,0xB9,0x16,0x00,0x00,0x00,0xE8,0xFC,0x90,0xFF,0xFF,0x84,0xDB,0x75,0xD8};
// 000FA180..000FA2BB, the item interaction sender 000FA180 (cx = unit type,
// edx = unit id). For the Auto Gold target it skips the stop 000FF010 and
// 000FE080 (000FA24B / 000FA258 jump over them) and goes on to the send.
constexpr std::uint8_t W_InteractUnit_FA180[] = {0x40,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x8B,0x05,0x32,0x11,0x8D,0x02,0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x88,0x00,0x00,0x00,0x44,0x8B,0xF2,0x44,0x0F,0xB7,0xF9,0xE8,0x23,0x11,0xF9,0xFF,0x8B,0xC8,0xE8,0xCC,0x02,0xFA,0xFF,0x41,0x8B,0xD7,0x41,0x8B,0xCE,0x48,0x8B,0xF0,0x41,0x8B,0xDF,0xE8,0x0B,0x04,0xFA,0xFF,0x48,0x8B,0xF8,0x48,0x85,0xC0,0x0F,0x84,0x34,0x07,0x00,0x00,0x48,0x89,0xAC,0x24,0xD0,0x00,0x00,0x00,0x48,0x8B,0xCE,0x4C,0x89,0xA4,0x24,0xD8,0x00,0x00,0x00,0xE8,0xF7,0xFE,0x24,0x00,0x0F,0xB6,0xE8,0x85,0xDB,0x0F,0x84,0x21,0x06,0x00,0x00,0x83,0xEB,0x01,0x0F,0x84,0xDE,0x05,0x00,0x00,0x83,0xEB,0x01,0x0F,0x84,0x86,0x02,0x00,0x00,0x83,0xEB,0x02,0x74,0x18,0x83,0xFB,0x01,0x0F,0x85,0x85,0x06,0x00,0x00,0x41,0x8B,0xD6,0xB1,0x40,0xE8,0xC2,0x24,0xFF,0xFF,0xE9,0xD2,0x06,0x00,0x00,0xE8,0x18,0xA4,0x04,0x00,0x48,0x8B,0xD8,0xE8,0x80,0x60,0xFE,0xFF,0x48,0x8B,0xC8,0xE8,0x58,0x42,0xC3,0x00,0x85,0xC0,0x74,0x34,0x48,0x8D,0x8B,0x18,0x19,0x00,0x00,0xE8,0xD8,0x88,0x09,0x00,0x48,0x3B,0xC7,0x75,0x23,0x48,0x8B,0xCF,0xE8,0x3B,0x8A,0x27,0x00,0x83,0xF8,0x04,0x74,0x5A,0x48,0x8D,0x4C,0x24,0x40,0xC6,0x44,0x24,0x40,0x00,0xE8,0x67,0xF2,0xFF,0xFF,0x84,0xC0,0x74,0x47,0xCC,0xEB,0x44,0x48,0x8B,0xCE,0xE8,0xE8,0x08,0x25,0x00,0x83,0xF8,0x06,0x74,0x1A,0x48,0x8B,0xCE,0xE8,0xDB,0x08,0x25,0x00,0x83,0xF8,0x02,0x74,0x0D,0x48,0x8B,0xCE,0xE8,0xCE,0x08,0x25,0x00,0x83,0xF8,0x03,0x75,0x15,0x41,0xB9,0x01,0x00,0x00,0x00,0x45,0x33,0xC0,0x48,0x8B,0xD6,0x41,0x8D,0x49,0x06,0xE8,0x64,0x4D,0x00,0x00,0x48,0x8B,0xCE,0xE8,0xCC,0x3D,0x00,0x00,0xE8,0x57,0xDB,0xF7,0xFF,0x84,0xC0};
// 000FA405..000FA48C: the send of message 16h (unit id, x, y, flag) through
// 000EC7D0.
constexpr std::uint8_t W_InteractSend_FA405[] = {0xB9,0x01,0x00,0x00,0x00,0xE8,0xF1,0x40,0xFD,0xFF,0x8B,0x17,0x33,0xED,0x84,0xC0,0x8B,0xF5,0x8B,0xCA,0x40,0x0F,0x94,0xC6,0x83,0xE9,0x02,0x74,0x22,0x83,0xE9,0x02,0x74,0x1D,0x83,0xF9,0x01,0x74,0x18,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x0B,0xE8,0xF6,0x75,0x24,0x00,0x8B,0x17,0x8B,0xD8,0xEB,0x0B,0x8B,0xDD,0xEB,0x07,0x48,0x8B,0x47,0x38,0x8B,0x58,0x14,0x83,0xEA,0x02,0x74,0x1C,0x83,0xEA,0x02,0x74,0x17,0x83,0xFA,0x01,0x74,0x12,0x48,0x8B,0x4F,0x38,0x48,0x85,0xC9,0x74,0x10,0xE8,0xB8,0x75,0x24,0x00,0x8B,0xE8,0xEB,0x07,0x48,0x8B,0x47,0x38,0x8B,0x68,0x10,0x44,0x8B,0xCB,0x89,0x74,0x24,0x20,0x44,0x8B,0xC5,0x41,0x8B,0xD6,0xB1,0x16,0xE8,0x49,0x23,0xFF,0xFF,0xE9,0x69,0x04,0x00,0x00};
// 0034A330 UNITS_GetUnitId: [rcx+8], or -1 for a null unit.
constexpr std::uint8_t W_GetUnitId_34A330[] = {0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3};
// 00372C90 ITEMS_GetItemType: the item type of an item unit, 0 for any other
// unit.
constexpr std::uint8_t W_GetItemType_372C90[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x24,0x4E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,0xCF,0xE8,0x17,0x8D,0xFD,0xFF,0x83,0xF8,0x04,0x74,0x08,0x33,0xC0,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x41,0xB8,0x9D,0x07,0x00,0x00};
constexpr Witness GoldClientWitnesses[] = {
	{0x0FE656ULL, W_ClickGate_FE656, 51, "ClickGate"},
	{0x0FE68EULL, W_ClickGate_FE68E, 32, "ClickGate"},
	{0x09A8C0ULL, W_CanAct_9A8C0, 47, "CanAct"},
	{0x1932A2ULL, W_AutoGoldClick_1932A2, 42, "AutoGoldClick"},
	{0x1932D1ULL, W_AutoGoldClick_1932D1, 11, "AutoGoldClick"},
	{0x144640ULL, W_ClientContext_144640, 48, "ClientContext"},
	{0x192B20ULL, W_AutoGoldTarget_192B20, 5, "AutoGoldTarget"},
	{0x101A38ULL, W_AutoGoldNative_101A38, 176, "AutoGoldNative"},
	{0x0FA180ULL, W_InteractUnit_FA180, 315, "InteractUnit"},
	{0x0FA405ULL, W_InteractSend_FA405, 135, "InteractSend"},
	{0x34A330ULL, W_GetUnitId_34A330, 46, "GetUnitId"},
	{0x372C90ULL, W_GetItemType_372C90, 60, "GetItemType"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};
// Gold while Whirlwinding (server). 004B9DF0, handler of message 16h (pick up
// a ground item): exactly 17 bytes (004B9E2A), packet copied to rbp-1
// (004B9E4B), item GUID read from +1 (004B9E76).
constexpr std::uint8_t W_PickupGround_4B9DF0[] = {0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00,0x48,0x8B,0x05,0xB5,0x14,0x51,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0x17,0x49,0x63,0xD9,0x49,0x8B,0xF8,0x48,0x89,0x55,0xBF,0x4C,0x8B,0xF2,0x4C,0x8B,0xF9,0x83,0xFB,0x11,0x74,0x12,0x48,0x8D,0x4D,0xB7,0xC6,0x45,0xB7,0x00,0xE8,0x34,0x31,0x01,0x00,0x84,0xC0,0x74,0x01,0xCC,0x4C,0x8B,0xC3,0x48,0x8D,0x4D,0xFF,0x48,0x8B,0xD7,0xE8,0x10,0x98,0xE1,0x00,0x45,0x33,0xE4,0x49,0x8B,0xCE,0x4C,0x89,0x65,0xCF,0x44,0x89,0x65,0xBB,0xE8,0xDD,0x15,0xE9,0xFF,0x48,0x8B,0xC8,0x4C,0x8D,0x45,0xBB,0x48,0x8D,0x55,0xCF,0xE8,0x6D,0x5F,0xE3,0xFF,0x41,0x8B,0xFC,0x44,0x8B,0x6D,0x00,0x41,0x8B,0xF4};
// 004BB600, handler of message 5Fh (pick up into an inventory position, sent in
// controller play): exactly 25 bytes (004BB631), item GUID at +1 (004BB6E9).
constexpr std::uint8_t W_PickupToInventory_4BB600[] = {0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xD0,0x48,0x81,0xEC,0x30,0x01,0x00,0x00,0x48,0x8B,0x05,0xAE,0xFC,0x50,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0x00,0x49,0x63,0xD9,0x49,0x8B,0xF8,0x48,0x89,0x4C,0x24,0x78,0x48,0x89,0x54,0x24,0x68,0x83,0xFB,0x19,0x74,0x14,0x48,0x8D,0x4C,0x24,0x60,0xC6,0x44,0x24,0x60,0x00,0xE8,0xDB,0x79,0x01,0x00,0x84,0xC0,0x74,0x01,0xCC,0x4C,0x89,0xA4,0x24,0x28,0x01,0x00,0x00,0x48,0x8D,0x4D,0xE0,0x4C,0x89,0xAC,0x24,0x20,0x01,0x00,0x00,0x4C,0x8B,0xC3,0x48,0x8B,0xD7,0x4C,0x89,0xBC,0x24,0x18,0x01,0x00,0x00,0xE8,0xEF,0x7F,0xE1,0x00,0x48,0x8B,0x4C,0x24,0x68,0x45,0x33,0xF6,0x4C,0x89,0x75,0x90,0x44,0x89,0x75,0x80,0xE8,0xBA,0xFD,0xE8,0xFF,0x48,0x8B,0xC8,0x4C,0x8D,0x45,0x80,0x48,0x8D,0x55,0x90,0xE8,0x4A,0x47,0xE3,0xFF,0x41,0x8B,0xFE,0x41,0x8B,0xF6,0x44,0x39,0x75,0x80,0x0F,0x86,0x43,0x06,0x00,0x00,0x66,0x66,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x48,0x8B,0x4D,0x90,0x8B,0xC6,0x48,0x8B,0x0C,0xC1,0xE8,0xD1,0x46,0xE3,0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x74,0x39,0x48,0x8B,0xCB,0xE8,0x01,0x03,0xE9,0xFF,0x83,0xF8,0x04,0x75,0x1C,0x41,0xB8,0x51,0x0B,0x00,0x00,0x48,0x8D,0x15,0xDF,0x83,0x87,0x01,0x48,0x8B,0xCB,0xE8,0x47,0xEC,0xE8,0xFF,0x3B,0x45,0xE1,0x48,0x0F,0x44,0xFB};
// The dispatcher hook and its Uninterruptible-check redirect, shared by the
// potion and the gold parts.
constexpr Witness ServerMessageGateWitnesses[] = {
	{0x4F2FA0ULL, W_Dispatcher_4F2FA0, 206, "Dispatcher"},
	{0x4F3073ULL, W_Dispatcher_4F3073, 21, "Dispatcher"},
};
// The dispatcher's own bytes are verified by InstallServerMessageGate, before
// it hooks them, so they are not repeated here: the potion part may already
// have hooked the dispatcher when this list is checked. W_UseItem_4F4145
// proves the item lookup 0048FE80(game, 4, guid) as the use-item handler makes
// it (004F4170).
constexpr Witness GoldServerWitnesses[] = {
	{0x4B9DF0ULL, W_PickupGround_4B9DF0, 141, "PickupGround"},
	{0x4BB600ULL, W_PickupToInventory_4BB600, 240, "PickupToInventory"},
	{0x4F4145ULL, W_UseItem_4F4145, 51, "UseItem"},
	{0x372C90ULL, W_GetItemType_372C90, 60, "GetItemType"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};

// Cyclone (controller). 0014B889..0014B8B0, ControllerManager::Update 0014B720:
// every frame, for each of the 8 player slots that has a player (0009A480),
// the per-player controller update 0014C540 runs with rcx = manager,
// edx = slot, r8d = elapsed ms. The call itself (0014B89D) is the redirected
// site, checked by the patch.
constexpr std::uint8_t W_ControllerLoop_14B889[] = {0x8B,0xCF,0xE8,0xF0,0xEB,0xF4,0xFF,0x48,0x85,0xC0,0x74,0x0D,0x44,0x8B,0xC5,0x8B,0xD7,0x48,0x8B,0xCB};
constexpr std::uint8_t W_ControllerLoop_14B8A2[] = {0xFF,0xC7,0x48,0xFF,0xC6,0x83,0xFF,0x08,0x0F,0x8C,0x50,0xFF,0xFF,0xFF};
// 0014C615..0014C804, in 0014C540: in controller mode (00077E10) the movement
// stick (0013CF10) is stored per player slot (stride B8h) at +1954h/+1958h,
// clamped to length 1; its world direction, rotated by the camera, at
// +195Ch/+1960h; the stick counts as pushed when its length is above the dead
// zone at manager+1F94h (0014C7F8).
constexpr std::uint8_t W_Stick_14C615[] = {0xE8,0xF6,0xB7,0xF2,0xFF,0x45,0x0F,0x57,0xE4,0x84,0xC0,0x74,0x45,0xE8,0x69,0x08,0xFF,0xFF,0x48,0x8B,0xC8,0xE8,0x31,0x18,0xFF,0xFF,0x84,0xC0,0x74,0x1F,0xE8,0x58,0x08,0xFF,0xFF,0x44,0x8B,0xC3,0x48,0x8D,0x55,0x98,0x48,0x8B,0xC8,0xE8,0xC9,0x08,0xFF,0xFF,0xF3,0x0F,0x10,0x00,0xF3,0x0F,0x10,0x48,0x04,0xEB,0x2B,0x48,0x8D,0x44,0x24,0x40,0x4C,0x89,0x64,0x24,0x40,0xF3,0x0F,0x10,0x00,0xF3,0x0F,0x10,0x48,0x04,0xEB,0x16,0xE8,0xF4,0xE2,0xF3,0xFF,0x48,0x89,0x44,0x24,0x48,0xF3,0x0F,0x10,0x44,0x24,0x48,0xF3,0x0F,0x10,0x4C,0x24,0x4C,0xF3,0x41,0x0F,0x11,0x84,0x37,0x54,0x19,0x00,0x00,0x0F,0x28,0xD9,0xF3,0x41,0x0F,0x11,0x8C,0x37,0x58,0x19,0x00,0x00,0x0F,0x28,0xC3,0xF3,0x45,0x0F,0x10,0x8C,0x37,0x54,0x19,0x00,0x00,0xF3,0x44,0x0F,0x10,0x15,0x5A,0xE2,0xB6,0x01,0x41,0x0F,0x28,0xC9,0xF3,0x41,0x0F,0x59,0xC9,0xF3,0x0F,0x59,0xC3,0xF3,0x0F,0x58,0xC8,0xF3,0x0F,0x51,0xD1,0x41,0x0F,0x2F,0xD2,0x76,0x25,0x41,0x0F,0x28,0xC2,0xF3,0x0F,0x5E,0xC2,0xF3,0x44,0x0F,0x59,0xC8,0xF3,0x0F,0x59,0xD8,0xF3,0x45,0x0F,0x11,0x8C,0x37,0x54,0x19,0x00,0x00,0xF3,0x41,0x0F,0x11,0x9C,0x37,0x58,0x19,0x00,0x00,0xF3,0x42,0x0F,0x10,0x84,0x3E,0x58,0x19,0x00,0x00,0x49,0x8D,0x8F,0x5C,0x19,0x00,0x00,0x0F,0x57,0x05,0xEE,0x7E,0xB7,0x01,0x48,0x03,0xCE,0xF3,0x0F,0x10,0x35,0x73,0xFF,0xB7,0x01,0xF3,0x42,0x0F,0x11,0x84,0x3E,0x60,0x19,0x00,0x00,0x0F,0x28,0xC6,0x48,0x89,0x4C,0x24,0x40,0xE8,0x2C,0x2D,0x19,0x01,0x0F,0x28,0xF8,0x44,0x0F,0x28,0xC0,0x0F,0x28,0xC6,0xF3,0x41,0x0F,0x59,0xFC,0xE8,0x58,0x8B,0x19,0x01,0x48,0x8B,0x44,0x24,0x40,0x0F,0x28,0xD0,0x0F,0x28,0xE0,0xF3,0x41,0x0F,0x59,0xD4,0x44,0x88,0x64,0x24,0x30,0x0F,0x28,0xDF,0x0F,0x28,0xCF,0xF3,0x0F,0x10,0x68,0x04,0x0F,0x28,0xF7,0xF3,0x41,0x0F,0x59,0xC9,0x0F,0x28,0xC5,0xF3,0x41,0x0F,0x59,0xF4,0xF3,0x0F,0x59,0xC4,0xF3,0x0F,0x59,0xDD,0xF3,0x0F,0x5C,0xD9,0x41,0x0F,0x28,0xC8,0xF3,0x41,0x0F,0x59,0xC9,0xF3,0x0F,0x5C,0xCE,0xF3,0x0F,0x58,0xDA,0xF3,0x0F,0x58,0xC8,0x41,0x0F,0x28,0xC0,0xF3,0x0F,0x59,0xC5,0xF3,0x0F,0x59,0xDF,0xF3,0x0F,0x5C,0xF0,0xF3,0x41,0x0F,0x59,0xC8,0x0F,0x28,0xD3,0xF3,0x0F,0x5C,0xD1,0x41,0x0F,0x28,0xC9,0xF3,0x0F,0x59,0xCC,0xF3,0x0F,0x58,0xF1,0xF3,0x0F,0x58,0xD2,0xF3,0x41,0x0F,0x59,0xF0,0xF3,0x41,0x0F,0x58,0xD1,0xF3,0x0F,0x5C,0xF3,0xF3,0x0F,0x11,0x10,0xF3,0x0F,0x58,0xF6,0xF3,0x0F,0x58,0xF5,0xF3,0x0F,0x11,0x70,0x04,0xF3,0x41,0x0F,0x10,0x84,0x37,0x58,0x19,0x00,0x00,0xF3,0x41,0x0F,0x10,0x8C,0x37,0x54,0x19,0x00,0x00,0xF3,0x0F,0x59,0xC0,0xF3,0x0F,0x59,0xC9,0xF3,0x0F,0x58,0xC8,0xF3,0x0F,0x51,0xC1,0x0F,0x2F,0x86,0x94,0x1F,0x00,0x00,0x0F,0x97,0x44,0x24,0x35};
// 001495AB..001495F8, the press/release matcher 00149550: the held skill
// button of a player slot is at +1968h, -1 when none is held.
constexpr std::uint8_t W_Matcher_1495AB[] = {0x4C,0x69,0x64,0x24,0x58,0xB8,0x00,0x00,0x00,0x48,0x89,0x9C,0x24,0x88,0x03,0x00,0x00,0x4C,0x89,0xAC,0x24,0x30,0x03,0x00,0x00,0x41,0x83,0xBC,0x3C,0x68,0x19,0x00,0x00,0xFF,0x74,0x18,0x40,0x84,0xF6,0x0F,0x85,0xBF,0x06,0x00,0x00,0x41,0x8B,0xDF,0x41,0x38,0xB4,0x3C,0x64,0x19,0x00,0x00,0x74,0x24,0xEB,0x1E,0x40,0x84,0xF6,0x0F,0x84,0xA7,0x06,0x00,0x00,0x41,0x8B,0xDF,0xE8,0x48,0x39,0x00,0x00};
// 0011A9E0..0011AA4E, the release handler of skill button 1: 00117D20 returns
// the skill id on a skill button (ecx = button).
constexpr std::uint8_t W_ReleaseSlot1_11A9E0[] = {0x48,0x83,0xEC,0x28,0xE8,0xE7,0x29,0xF7,0xFF,0x85,0xC0,0x75,0x6B,0xE8,0xDE,0x08,0xF7,0xFF,0x8B,0xC8,0xE8,0x67,0xF1,0xF7,0xFF,0x84,0xC0,0x75,0x5B,0xB9,0x03,0x00,0x00,0x00,0xE8,0xF9,0x3A,0xFB,0xFF,0x84,0xC0,0x75,0x4D,0xB9,0x01,0x00,0x00,0x00,0x48,0x89,0x5C,0x24,0x20,0xE8,0x06,0xD3,0xFF,0xFF,0x8B,0xD8,0xE8,0x3F,0x72,0xFC,0xFF,0x48,0x8B,0xC8,0xE8,0x67,0x3A,0xC1,0x00,0x85,0xC0,0x74,0x26,0xE8,0x0E,0x9C,0x02,0x00,0x8B,0xD3,0x48,0x8B,0xC8,0xE8,0x34,0xFF,0x02,0x00,0x84,0xC0,0x75,0x13,0x33,0xD2,0x8D,0x4A,0x01,0x48,0x8B,0x5C,0x24,0x20,0x48,0x83,0xC4,0x28};
constexpr Witness CycloneControllerWitnesses[] = {
	{0x14B889ULL, W_ControllerLoop_14B889, 20, "ControllerLoop"},
	{0x14B8A2ULL, W_ControllerLoop_14B8A2, 14, "ControllerLoop"},
	{0x14C615ULL, W_Stick_14C615, 495, "Stick"},
	{0x1495ABULL, W_Matcher_1495AB, 77, "Matcher"},
	{0x11A9E0ULL, W_ReleaseSlot1_11A9E0, 110, "ReleaseSlot1"},
	{0x34BA40ULL, W_GetUsedSkill_34BA40, 14, "GetUsedSkill"},
	{0x33CBC0ULL, W_SkillRecFlags_33CBC0, 76, "SkillRecFlags"},
};

constexpr std::uint8_t P_Executor[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x50};
constexpr std::uint8_t P_HeldRight[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0xDA};
constexpr std::uint8_t P_HeldLeft[] = {0x40,0x55,0x57,0x48,0x83,0xEC,0x38,0x48,0x8B,0xF9,0x33,0xED,0x48,0x8B,0x49,0x08};
constexpr std::uint8_t P_CastOnTarget[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20};
constexpr std::uint8_t P_CastAtPos[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x70};
constexpr std::uint8_t P_Dispatcher[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t P_UseItem[] = {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8D,0xAC,0x24,0x58,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xA8,0x01,0x00,0x00};
constexpr std::uint8_t P_ItemUseLock[] = {0x48,0x83,0xEC,0x28,0xBA,0x02,0x00,0x00,0x00};
constexpr std::uint8_t P_WhirlwindStart[] = {0x48,0x89,0x5C,0x24,0x10,0x44,0x89,0x4C,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};

// Whirlwind hit interval (server). Read out of the 3.3.93847 image as hosted by
// D2RLoader 1.3.1: the whole interval helper 0056AC00. It pins the frame layout
// the relay relies on (the unit in rbp from 0056AC28 on, the animation speed at
// rsp+60h and the attack frame at rsp+64h written by the animation lookup
// 003E3720), the five replaced bytes at 0056AD23 and the epilogue at 0056AD6B
// the relay returns to. Do not edit by hand.
constexpr std::uint8_t W_IntervalHelper_56AC00[] = {0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8B,0x05,0xAE,0x06,0x46,0x02,0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x78,0x45,0x33,0xFF,0x4C,0x8B,0xF2,0x48,0x8B,0xE9,0x48,0x85,0xD2,0x74,0x0A,0x48,0x8B,0xCA,0xE8,0x18,0x85,0xE0,0xFF,0xEB,0x03,0x41,0x8B,0xC7,0x48,0x8B,0xCD,0x89,0x44,0x24,0x68,0xE8,0x97,0xF4,0xDD,0xFF,0x41,0xB8,0xAE,0x04,0x00,0x00,0x48,0x8D,0x15,0xBA,0x33,0x7D,0x01,0x48,0x8B,0xCD,0x0F,0xB6,0xD8,0xE8,0xFF,0xF6,0xDD,0xFF,0x48,0x8B,0xCD,0x48,0x8B,0xF0,0xE8,0x64,0x0D,0xDE,0xFF,0x41,0xB8,0xAB,0x04,0x00,0x00,0x48,0x8D,0x15,0x97,0x33,0x7D,0x01,0x48,0x8B,0xCD,0x8B,0xF8,0xE8,0xDD,0xEB,0xDD,0xFF,0x88,0x5C,0x24,0x50,0x8B,0xD0,0x41,0x8B,0xC7,0x4D,0x85,0xF6,0x44,0x8B,0xCF,0x41,0xB8,0x07,0x00,0x00,0x00,0x0F,0x94,0xC0,0x48,0x8B,0xCD,0x89,0x44,0x24,0x48,0x48,0x8D,0x44,0x24,0x68,0x44,0x89,0x7C,0x24,0x40,0x48,0x89,0x44,0x24,0x38,0x48,0x8D,0x44,0x24,0x70,0x48,0xC7,0x44,0x24,0x30,0x08,0x00,0x00,0x00,0x48,0x89,0x44,0x24,0x28,0x48,0x89,0x74,0x24,0x20,0xE8,0x32,0xBA,0xE4,0xFF,0x48,0x8B,0xCD,0x44,0x89,0x7C,0x24,0x6C,0x44,0x89,0x7C,0x24,0x60,0x44,0x89,0x7C,0x24,0x64,0xE8,0xFB,0xF3,0xDD,0xFF,0x0F,0xB6,0xC8,0x4C,0x8D,0x4C,0x24,0x60,0x48,0x8D,0x44,0x24,0x64,0x4C,0x8D,0x44,0x24,0x6C,0x48,0x89,0x44,0x24,0x20,0x48,0x8D,0x54,0x24,0x70,0xE8,0x1A,0x8A,0xE7,0xFF,0x85,0xC0,0x74,0x5C,0x45,0x33,0xC0,0x48,0x8B,0xCD,0x41,0x8D,0x50,0x44,0xE8,0x07,0xA3,0xD8,0xFF,0x48,0x8B,0xCD,0x8B,0xF8,0xE8,0x3D,0xE9,0xDD,0xFF,0x48,0x8B,0xCD,0x03,0xF8,0xE8,0x23,0xFE,0xDD,0xFF,0x48,0x8B,0xCD,0x8B,0xD8,0xE8,0x89,0xF8,0xDD,0xFF,0x3B,0xFB,0x7C,0x07,0x3B,0xF8,0x8B,0xDF,0x0F,0x4F,0xD8,0x0F,0xAF,0x5C,0x24,0x60,0xB8,0x1F,0x85,0xEB,0x51,0xF7,0xEB,0x8B,0xCA,0xC1,0xF9,0x05,0x8B,0xC1,0xC1,0xE8,0x1F,0x03,0xC8,0x8B,0x44,0x24,0x64,0xC1,0xE0,0x08,0x99,0xF7,0xF9,0xEB,0x05,0xB8,0x0A,0x00,0x00,0x00,0x48,0x8B,0x4C,0x24,0x78,0x48,0x33,0xCC,0xE8,0xD8,0x63,0xD6,0x00,0x48,0x8B,0x9C,0x24,0xC0,0x00,0x00,0x00,0x48,0x81,0xC4,0x80,0x00,0x00,0x00,0x41,0x5F,0x41,0x5E,0x5F,0x5E,0x5D,0xC3};
constexpr Witness HitIntervalWitnesses[] = {
	{0x56AC00ULL, W_IntervalHelper_56AC00, 399, "IntervalHelper"},
};

// ---------------------------------------------------------------------------
// Part bookkeeping
// ---------------------------------------------------------------------------

enum class PartState : std::uint8_t { NotAttempted, Installed, DisabledByConfig, UnsupportedBuild, InstallFailed };

enum class Part : std::size_t {
	CtcGates,
	CtcPolicy,
	ProcStartFix,
	DualWield,
	CycloneServer,
	CycloneClient,
	MeleeSpinServer,
	MeleeSpinClient,
	DeathWhileWhirlwinding,
	PotionsWhileWhirlwinding,
	PotionsWhileWhirlwindingClient,
	WhirlwindWithBows,
	WhirlwindSpeedSync,
	PetsFollowWhirlwind,
	GoldWhileWhirlwindingServer,
	GoldWhileWhirlwindingClient,
	CycloneController,
	WhirlwindHitInterval,
	Count,
};

constexpr std::array<const char*, static_cast<std::size_t>(Part::Count)> PartNames {
	"CTC while Uninterruptible (gates)",
	"CTC while Uninterruptible (allow list / chance)",
	"Proc start fix (client)",
	"Dual wield same target",
	"Cyclone (server)",
	"Cyclone (client)",
	"Spin at melee targets (server)",
	"Spin at melee targets (client)",
	"Death while Whirlwinding",
	"Potions while Whirlwinding (server)",
	"Potions while Whirlwinding (client)",
	"Whirlwind with bows and crossbows",
	"Whirlwind speed sync (server start)",
	"Pets follow during Whirlwind",
	"Gold while Whirlwinding (server)",
	"Gold while Whirlwinding (client)",
	"Cyclone (controller)",
	"Whirlwind hit interval (server)",
};

std::array<PartState, static_cast<std::size_t>(Part::Count)> g_parts {};

void SetPart(Part part, PartState state) noexcept {
	g_parts[static_cast<std::size_t>(part)] = state;
}

auto PartIs(Part part, PartState state) noexcept -> bool {
	return g_parts[static_cast<std::size_t>(part)] == state;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr const char* DefaultConfigToml = R"toml(# Whirlwind - Whirlwind rework for Diablo II: Resurrected 3.3
#
# Eleven parts, each behind its own switch:
#   [ctc_while_uninterruptible]  item procs (chance to cast) fire while the
#                                caster is Uninterruptible, optionally filtered
#   [proc_start_fix]             a proc no longer takes over the caster's skill
#                                and animation on the client (the stuck
#                                Whirlwind after a Corpse Explosion proc)
#   [dual_wield_same_target]     both dual-wielded weapons may strike the same
#                                target in one Whirlwind hit
#   [cyclone]                    Whirlwind follows the cursor, or the movement
#                                stick on a controller, for as long as the
#                                button is held, and only that long
#   [death_while_whirlwinding]   a lethal hit kills the character during
#                                Whirlwind instead of waiting for it to end
#   [potions_while_whirlwinding] potions can be used during Whirlwind
#   [whirlwind_with_bows]        Whirlwind works with bows and crossbows
#   [whirlwind_speed_sync]       the server starts Whirlwind at the same
#                                movement speed the client does
#   [pets_follow_whirlwind]      summons, pets and hirelings follow the
#                                character during Whirlwind
#   [gold_while_whirlwinding]    Auto Gold Pickup keeps picking up gold during
#                                Whirlwind
#   [whirlwind_hit_interval]     a new formula for the frames between a
#                                player's Whirlwind hits (off by default)
#
# Safety: the plugin verifies the original bytes of every site it touches, and
# of every game function it calls, before it installs anything. On a game build
# it does not recognise, the affected part installs nothing, the reason is
# logged, and the plugin stays loaded so the `whirlwind` console command can
# tell you what happened.
#
# This file lives at <scope>\d2rloader\config\celestialrayone.whirlwind.toml
# and is created with these defaults on first run. Delete it to get them back.
# Changes are read at plugin load, so restart the game after editing.


[whirlwind]

# Master switch for the whole plugin.
#   true  - the parts below are installed as configured.
#   false - the DLL stays loaded and the console command still answers, but
#           nothing is installed.
# Default: true
enabled = true


[ctc_while_uninterruptible]

# Vanilla refuses every item proc while the caster is in the Uninterruptible
# state (Whirlwind, and any other skill that sets it). With this on, procs
# fire. This is global: it applies to every Uninterruptible caster, not only
# Whirlwind.
# Sites: RVA 00589736 (unit-target procs) and RVA 0058986B (position procs).
# Default: true
enabled = true

# Chance, in percent, that a proc which already passed its own roll is let
# through while the caster is Uninterruptible. 100 lets every such proc
# through. 0 blocks them all. Values above 100 count as 100.
# Default: 100
chance_percent = 100

# Skill ids allowed to proc while the caster is Uninterruptible. Any skill not
# in the list is blocked while Uninterruptible. An empty list means no filter:
# every skill is allowed (the chance above still applies).
# Example: allowed_skills = [ 56, 59, 64 ]
# Default: []
allowed_skills = []


[proc_start_fix]

# Some procs run the full client skill start. That start writes the proc skill
# into the caster's used-skill slot and switches the caster's animation to the
# proc's mode, which the server never does for a proc. A Whirlwind in progress
# then loses its client-side owner, never gets cleaned up, and every later
# Whirlwind is refused. With this on, a proc keeps the caster's used skill and
# animation mode exactly as they were.
# Sites: RVA 0023123A, 002184FE, 00218586, 00231334 (client).
# Default: true
enabled = true


[dual_wield_same_target]

# When dual wielding, Whirlwind swings both weapons each hit. Vanilla makes the
# second weapon look for the next target after the one the first weapon just
# struck, so with two or more enemies in range the weapons never strike the
# same enemy. With this on, the second weapon may strike the same enemy (or the
# next one if the first swing killed it). Rotation across enemies from one hit
# to the next is unchanged.
# Site: RVA 005694B7 (server).
# Default: true
enabled = true


[cyclone]

# Whirlwind follows the cursor while the button is held and ends when it is
# released. A single click gives a Whirlwind that lasts only the grace window
# below. Holding the button never ends the Whirlwind by itself: when the
# character reaches the point it was heading for it keeps spinning until the
# cursor gives it somewhere new to go.
# Steering never sets a movement speed of its own. The character moves at the
# speed the game gives a Whirlwinding unit: walk speed scaled by faster
# run/walk and by slows and haste, updated by the game whenever those change.
# The one time steering asks for a speed is when a new route starts from a
# standstill, because arriving at a point stops the path; it then runs the
# game's own movement speed function (RVA 00350B40), the same one the game
# runs itself.
# Default: true
enabled = true

# How long, in milliseconds, Whirlwind keeps going after the last held-button
# update. This is what "released" means: once the button is up, updates stop,
# and the Whirlwind ends this long after the last one. Movement stops at that
# moment; the spin animation finishes its current turn, like a vanilla
# Whirlwind ending. Keep it short. Too short can end a held Whirlwind during a
# frame hitch.
# Default: 120
hold_grace_ms = 120

# What to do when the cursor is on or very close to the character, or, with
# a controller, while the movement stick is not pushed.
#   "keep_heading"  - keep moving in the last direction the cursor (or the
#                     stick) gave. Until it has given one, keep moving in the
#                     direction the Whirlwind was cast in.
#   "spin_in_place" - stop moving and spin until the cursor moves away (or
#                     the stick is pushed again).
# Default: "keep_heading"
near_cursor = "keep_heading"

# How close, in subtiles, counts as "very close" for near_cursor.
# Default: 3
near_cursor_radius = 3

# With a controller, Whirlwind follows the stick you move with (the left
# stick unless you rebound it) for as long as the skill button is held,
# from the moment it is pressed. The game's own controller aim (a point 2
# subtiles ahead, or the enemy it docks to) only picks where the first spin
# starts. A stick that is not pushed counts as "very close" for near_cursor
# above, so keep_heading and spin_in_place work the same way as with a mouse.
# Site: RVA 0014B89D (client).

# While the button is held, Whirlwind pays its normal cost again every this
# many game frames (25 frames is one second). Mana skills pay mana, skills cast
# from charges use a charge. If the cost cannot be paid, Whirlwind ends. 0
# turns the recurring cost off, so only the first cast pays.
# Default: 25
mana_interval_frames = 25

# Vanilla Whirlwind turns into a normal attack when it is aimed at a target
# that is already in melee range. With this on, Whirlwind always spins.
# Sites: RVA 0056A4E1 (server), RVA 00230B20 (client).
# Default: true
spin_at_melee_targets = true


[death_while_whirlwinding]

# Whirlwind makes the character Uninterruptible, and the game never kills an
# Uninterruptible character on the spot: a lethal hit only marks the death as
# pending (state 92), and the death happens when the Uninterruptible action
# ends. With a cyclone Whirlwind that end might never come. With this on, the
# frame a death becomes pending the Whirlwind ends, and the game's own
# pending-death code kills the character right there, with the normal
# Whirlwind teardown.
# Site: shares the redirect at RVA 0042A99F (server).
# Default: true
enabled = true


[potions_while_whirlwinding]

# Vanilla blocks item use while the character is Uninterruptible, in three
# places. The client's item-use lock refuses the belt keys, inventory use and
# every other use path before anything is sent; the server then ignores the
# use message (0x26, belt and inventory alike) in its message dispatcher, and
# refuses it again in the use-item handler. With this on, using a potion is let
# through all three while the character is Whirlwinding. Every other item stays
# blocked exactly as before.
# "Potion" is item type poti (itemtypes.txt row 9) and every type that
# inherits from it.
# Sites: RVA 001C7360, 001C738C (client);
#        RVA 004F2FA0, 004F306E, 004F40C0, 004F4140 (server).
# Default: true
enabled = true


[whirlwind_with_bows]

# Player skill sequences are compiled into the game, one entry per weapon
# class, and Whirlwind's sequence has no entry for bows or crossbows. With a
# missile weapon the skill starts with no sequence to run: it spins for one
# tick, stops, and leaves the character stuck Uninterruptible. With this on,
# when the equipped weapon class has no entry, the sequence of the bare-handed
# (hth) entry is used instead. Whirlwind's hand-to-hand entry is the same
# 8-step sequence every melee weapon uses, so the hit timing is identical.
# This covers every sequence skill that lacks an entry for the equipped weapon
# class, not only Whirlwind, exactly like the 2.4 patch did. Sequences that do
# have an entry for the weapon are untouched. Client and server share the code.
# Site: RVA 003CB987 (sequence resolver 003CB890).
# Default: true
enabled = true


[whirlwind_speed_sync]

# The game moves a Whirlwinding character at walk speed scaled by faster
# run/walk and by slows and haste (velocitypercent). The client applies that
# the moment Whirlwind starts. The server start sets plain walk speed and only
# switches to the scaled speed when a buff or debuff makes the game recompute
# it. With any faster run/walk the two sides move at different speeds, drift
# apart, and the server keeps pulling the character back, which shows as
# stutter. The longer the Whirlwind, the worse it gets, so cyclone makes it
# very visible. With this on, the server runs the game's movement speed
# function right after a player's Whirlwind starts, exactly as the client
# start does, so both sides move at the same speed from the first frame.
# Players only. Monster Whirlwind is unchanged.
# Site: RVA 0056A420 (server Whirlwind start, srvstfunc 38).
# Default: true
enabled = true


[pets_follow_whirlwind]

# Summons, pets and hirelings that fall behind follow their owner's trail:
# the last 20 spots the character passed. Only walking, running and
# knockback write that trail, never a skill, so during Whirlwind it stays
# frozen where the Whirlwind started and every pet keeps heading back there.
# A cyclone Whirlwind never ends, so they never catch up. With this on,
# Whirlwind writes the trail exactly the way walking does: the character's
# spot every 25 ms, once it is more than about 7 subtiles (squared distance
# 45) from the last one. Pets then follow you as they do when you run.
# Players only. The whirlwind console command shows how many trail points
# Whirlwind has written.
# Sites: shares the redirect at RVA 0042A99F (server); the trail is written
# the same way as RVA 00429B8E (walk/run frame handler).
# Default: true
enabled = true


[gold_while_whirlwinding]

# The game's Auto Gold Pickup option does nothing during Whirlwind. The game
# still finds the gold, but it picks gold up with a click, and every click is
# refused while Whirlwind runs. The server also ignores pickup messages from an
# Uninterruptible character. With this on, Auto Gold Pickup works during
# Whirlwind the same way it works while walking: same option, same radius,
# same delays, same server distance check. Like walking, it picks up gold while
# the character is moving, not while it spins in place.
# Only gold is affected. Clicks, every other item and the Whirlwind itself are
# untouched. Nothing happens when Auto Gold Pickup is off in the game options.
# Players only.
# Sites: RVA 000FE689 (client); RVA 004F2FA0, 004F306E (server, shared with
# potions_while_whirlwinding).
# Default: true
enabled = true

[whirlwind_hit_interval]

# Replaces the formula for how many frames pass between a player's Whirlwind
# hits. Off by default: with it off the game's own formula runs untouched.
#
# Vanilla:
#   rate     = attack rate + diminished IAS, kept between 15 and 175
#              (250 while shapeshifted)
#   interval = 256 * attack frame / (animation speed * rate / 100)
# With this on:
#   rate     = 100 + (attack rate - 100) * attackrate_weight / 100
#              + diminished IAS, kept between min_rate and max_rate
#   interval = 256 * attack frame * interval_stretch / 100
#              / (animation speed * rate / 100), never below min_frames
#
# attack rate      the character's attack rate: 100, minus the weapon's speed,
#                  plus attack rate from skills and auras, minus slows.
# diminished IAS   the game's own 120 * IAS / (120 + IAS), IAS from all gear.
# attack frame and animation speed come from animdata: the class's normal
#                  attack (A1) row for the weapon's wclass. The attack frame
#                  is the first frame with an event, the frame the normal
#                  attack lands its hit.
# Divisions round toward zero, as in the game. With a weapon in each hand each
# hand is worked out on its own weapon and the game averages the two, rounding
# up, as before. Players only: monster Whirlwind never uses this formula.
# Setting the five values below to 100, 100, 0, 175 and 15 gives the vanilla
# numbers back (shapeshifted forms aside).
# The whirlwind console command shows how many intervals were worked out and
# the numbers that went into the last one.
# Site: RVA 0056AD23, inside the interval helper at RVA 0056AC00 (server).
# Default: false
enabled = false

# How much the attack rate counts, in percent of its distance from 100. Weapon
# speed, skill and aura attack rate and slows all go through it. 100 counts it
# in full like vanilla, 50 counts it half. IAS is not affected.
# Default: 50
attackrate_weight = 50

# Stretches every interval, in percent. 100 is vanilla. 170 makes every
# interval 70% longer, so every breakpoint needs more speed.
# Default: 170
interval_stretch = 170

# The fewest frames between hits. Nothing goes faster, whatever the speed.
# 0 means no floor, like vanilla.
# Default: 6
min_frames = 6

# Highest rate Whirlwind can use. Vanilla stops at 175, or 250 while
# shapeshifted; this one value replaces both. It only blocks, it never adds
# speed: with the defaults two-handed weapons need a rate of 185 to reach 6
# frames, so anything below 185 stops them at 7.
# Default: 300
max_rate = 300

# Lowest rate Whirlwind can use, for heavy slows. Vanilla is 15. A value above
# max_rate is lowered to max_rate.
# Default: 15
min_rate = 15
)toml";

struct Config {
	bool enabled            = true;
	bool ctcEnabled         = true;
	int  ctcChancePercent   = 100;
	bool procStartFix       = true;
	bool dualWieldSameTarget = true;
	bool cycloneEnabled     = true;
	int  holdGraceMs        = 120;
	bool keepHeading        = true;
	int  nearCursorRadius   = 3;
	int  manaIntervalFrames = 25;
	bool spinAtMeleeTargets = true;
	bool deathWhileWhirlwinding = true;
	bool potionsWhileWhirlwinding = true;
	bool whirlwindWithBows = true;
	bool whirlwindSpeedSync = true;
	bool petsFollowWhirlwind = true;
	bool goldWhileWhirlwinding = true;
	bool hitIntervalEnabled = false;
	int  attackRateWeight   = 50;
	int  intervalStretch    = 170;
	int  minFrames          = 6;
	int  maxRate            = 300;
	int  minRate            = 15;
};

Config                      g_config {};
std::array<bool, 4096>      g_ctcAllowed {};
bool                        g_ctcAllowListActive = false;
std::uint32_t               g_serverGraceFrames  = 5;

// Finds `key` inside `[section]` and returns a pointer to the first
// non-blank character after '=', or nullptr. Comments (#) are skipped.
auto FindValue(const char* toml, const char* section, const char* key) noexcept -> const char* {
	const std::size_t sectionLength = std::strlen(section);
	const std::size_t keyLength     = std::strlen(key);
	bool              inSection     = false;
	for (const char* line = toml; line != nullptr && *line != '\0';) {
		const char* next = std::strchr(line, '\n');
		const char* end  = next != nullptr ? next : line + std::strlen(line);
		const char* p    = line;
		while (p < end && (*p == ' ' || *p == '\t')) {
			++p;
		}
		if (p < end && *p == '[') {
			const char* close = static_cast<const char*>(std::memchr(p, ']', static_cast<std::size_t>(end - p)));
			inSection = close != nullptr && static_cast<std::size_t>(close - p - 1) == sectionLength && std::strncmp(p + 1, section, sectionLength) == 0;
		} else if (inSection && p < end && *p != '#' && static_cast<std::size_t>(end - p) > keyLength && std::strncmp(p, key, keyLength) == 0) {
			const char* q = p + keyLength;
			while (q < end && (*q == ' ' || *q == '\t')) {
				++q;
			}
			if (q < end && *q == '=') {
				++q;
				while (q < end && (*q == ' ' || *q == '\t')) {
					++q;
				}
				return q < end ? q : nullptr;
			}
		}
		line = next != nullptr ? next + 1 : nullptr;
	}
	return nullptr;
}

void ReadBool(const char* toml, const char* section, const char* key, bool& value) noexcept {
	if (const char* v = FindValue(toml, section, key); v != nullptr) {
		if (std::strncmp(v, "true", 4) == 0) {
			value = true;
		} else if (std::strncmp(v, "false", 5) == 0) {
			value = false;
		}
	}
}

void ReadInt(const char* toml, const char* section, const char* key, int& value, int minimum, int maximum) noexcept {
	if (const char* v = FindValue(toml, section, key); v != nullptr) {
		char*      end    = nullptr;
		const long parsed = std::strtol(v, &end, 10);
		if (end != v) {
			value = static_cast<int>(parsed < minimum ? minimum : (parsed > maximum ? maximum : parsed));
		}
	}
}

void ReadNearCursor(const char* toml, bool& keepHeading) noexcept {
	if (const char* v = FindValue(toml, "cyclone", "near_cursor"); v != nullptr) {
		if (std::strncmp(v, "\"spin_in_place\"", 15) == 0) {
			keepHeading = false;
		} else if (std::strncmp(v, "\"keep_heading\"", 14) == 0) {
			keepHeading = true;
		}
	}
}

void ReadAllowList(const char* toml) noexcept {
	g_ctcAllowed.fill(false);
	g_ctcAllowListActive = false;
	const char* v        = FindValue(toml, "ctc_while_uninterruptible", "allowed_skills");
	if (v == nullptr || *v != '[') {
		return;
	}
	for (const char* p = v + 1; *p != '\0' && *p != ']' && *p != '\n';) {
		if (*p >= '0' && *p <= '9') {
			char*      end = nullptr;
			const long id  = std::strtol(p, &end, 10);
			if (id >= 0 && id < static_cast<long>(g_ctcAllowed.size())) {
				g_ctcAllowed[static_cast<std::size_t>(id)] = true;
				g_ctcAllowListActive                       = true;
			}
			p = end;
		} else {
			++p;
		}
	}
}

void LoadConfiguration(const D2RL::PluginContext* context) noexcept {
	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn("Could not create or open celestialrayone.whirlwind.toml. Built-in defaults are used.");
	}
	static std::array<char, 16384> buffer {};
	std::uint32_t                  required = 0;
	if (!context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &required)) {
		context->LogWarn("Could not read celestialrayone.whirlwind.toml. Built-in defaults are used.");
		ReadAllowList("");
	} else {
		buffer.back() = '\0';
		const char* t = buffer.data();
		ReadBool(t, "whirlwind", "enabled", g_config.enabled);
		ReadBool(t, "ctc_while_uninterruptible", "enabled", g_config.ctcEnabled);
		ReadInt(t, "ctc_while_uninterruptible", "chance_percent", g_config.ctcChancePercent, 0, 100);
		ReadAllowList(t);
		ReadBool(t, "proc_start_fix", "enabled", g_config.procStartFix);
		ReadBool(t, "dual_wield_same_target", "enabled", g_config.dualWieldSameTarget);
		ReadBool(t, "cyclone", "enabled", g_config.cycloneEnabled);
		ReadInt(t, "cyclone", "hold_grace_ms", g_config.holdGraceMs, 40, 2000);
		ReadNearCursor(t, g_config.keepHeading);
		ReadInt(t, "cyclone", "near_cursor_radius", g_config.nearCursorRadius, 1, 20);
		ReadInt(t, "cyclone", "mana_interval_frames", g_config.manaIntervalFrames, 0, 1000);
		ReadBool(t, "cyclone", "spin_at_melee_targets", g_config.spinAtMeleeTargets);
		ReadBool(t, "death_while_whirlwinding", "enabled", g_config.deathWhileWhirlwinding);
		ReadBool(t, "potions_while_whirlwinding", "enabled", g_config.potionsWhileWhirlwinding);
		ReadBool(t, "whirlwind_with_bows", "enabled", g_config.whirlwindWithBows);
		ReadBool(t, "whirlwind_speed_sync", "enabled", g_config.whirlwindSpeedSync);
		ReadBool(t, "pets_follow_whirlwind", "enabled", g_config.petsFollowWhirlwind);
		ReadBool(t, "gold_while_whirlwinding", "enabled", g_config.goldWhileWhirlwinding);
		ReadBool(t, "whirlwind_hit_interval", "enabled", g_config.hitIntervalEnabled);
		ReadInt(t, "whirlwind_hit_interval", "attackrate_weight", g_config.attackRateWeight, 0, 1000);
		ReadInt(t, "whirlwind_hit_interval", "interval_stretch", g_config.intervalStretch, 1, 1000);
		ReadInt(t, "whirlwind_hit_interval", "min_frames", g_config.minFrames, 0, 250);
		ReadInt(t, "whirlwind_hit_interval", "max_rate", g_config.maxRate, 1, 1000);
		ReadInt(t, "whirlwind_hit_interval", "min_rate", g_config.minRate, 1, 1000);
	}
	if (g_config.minRate > g_config.maxRate) {
		g_config.minRate = g_config.maxRate;
	}
	// The server keeps a Whirlwind alive a little longer than the client does,
	// so a client aim can never reach a server whose Whirlwind already ended.
	g_serverGraceFrames = static_cast<std::uint32_t>((g_config.holdGraceMs + 39) / 40 + 2);
	D2RL::LogInfoF(
		context,
		"config: enabled=%d ctc=%d chance=%d allow_list=%d proc_fix=%d dual_wield=%d cyclone=%d grace_ms=%d keep_heading=%d radius=%d mana_frames=%d spin_melee=%d death=%d potions=%d bows=%d speed_sync=%d pets=%d gold=%d",
		g_config.enabled, g_config.ctcEnabled, g_config.ctcChancePercent, g_ctcAllowListActive, g_config.procStartFix,
		g_config.dualWieldSameTarget, g_config.cycloneEnabled, g_config.holdGraceMs, g_config.keepHeading,
		g_config.nearCursorRadius, g_config.manaIntervalFrames, g_config.spinAtMeleeTargets, g_config.deathWhileWhirlwinding, g_config.potionsWhileWhirlwinding, g_config.whirlwindWithBows, g_config.whirlwindSpeedSync, g_config.petsFollowWhirlwind, g_config.goldWhileWhirlwinding);
	D2RL::LogInfoF(context, "config: hit_interval=%d attackrate_weight=%d interval_stretch=%d min_frames=%d max_rate=%d min_rate=%d",
		g_config.hitIntervalEnabled, g_config.attackRateWeight, g_config.intervalStretch, g_config.minFrames, g_config.maxRate,
		g_config.minRate);
}

// ---------------------------------------------------------------------------
// Verification and relay page
// ---------------------------------------------------------------------------

template <std::size_t N>
auto VerifyAll(const D2RL::PluginContext* context, const Witness (&list)[N], const char* part) noexcept -> bool {
	for (const Witness& w : list) {
		if (!context->CheckExpectedBytes(w.rva, w.bytes, w.size)) {
			D2RL::LogErrorF(context, "%s NOT installed: bytes at RVA %08llX (%s) do not match 3.3.93847 under D2RLoader 1.3.0.", part,
				static_cast<unsigned long long>(w.rva), w.what);
			return false;
		}
	}
	return true;
}

constexpr std::size_t RelayPageBytes = 4096;
constexpr std::size_t RelayServerPathStatus = 0x000;
constexpr std::size_t RelayClientPathStep   = 0x020;
constexpr std::size_t RelayProcStart        = 0x040;
constexpr std::size_t RelayModeCheck        = 0x060;
constexpr std::size_t RelaySetNeutral       = 0x080;
constexpr std::size_t RelayProcTail         = 0x0A0;
constexpr std::size_t RelayDualWield        = 0x0C0;
constexpr std::size_t RelayDispatchState    = 0x0E0;
constexpr std::size_t RelayUseItemState     = 0x100;
constexpr std::size_t RelayItemLockState    = 0x120;
constexpr std::size_t RelaySequenceFallback = 0x140;
constexpr std::size_t RelayClickGate        = 0x160;
constexpr std::size_t RelayControllerUpdate = 0x180;
constexpr std::size_t RelayHitInterval      = 0x1A0;   // 0x40 bytes

std::uint8_t* g_relayPage = nullptr;

auto WithinRel32(std::uintptr_t next, std::uintptr_t target) noexcept -> bool {
	const std::int64_t delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next);
	return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> std::uint8_t* {
	SYSTEM_INFO info {};
	GetSystemInfo(&info);
	const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
	const auto aligned     = hint & ~(granularity - 1U);
	for (std::uintptr_t delta = granularity; delta < 0x70000000ULL; delta += granularity) {
		const auto candidate = aligned + delta;
		if (!WithinRel32(hint, candidate + size)) {
			break;
		}
		if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
			return static_cast<std::uint8_t*>(memory);
		}
	}
	return nullptr;
}

void WriteAbsoluteJump(std::uint8_t* at, std::uint64_t target) noexcept {
	static constexpr std::uint8_t JumpQwordRip[] { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	std::memcpy(at, JumpQwordRip, sizeof(JumpQwordRip));
	std::memcpy(at + sizeof(JumpQwordRip), &target, sizeof(target));
}

// ---------------------------------------------------------------------------
// Game layout used by the handlers. Each offset is confirmed by the verified
// bytes above or by the decompiled function named next to it.
// ---------------------------------------------------------------------------

struct PathPoint {
	std::uint16_t x;
	std::uint16_t y;
};

// Command handed to the held-button handlers 00101FA0 / 001013B0 (layout from
// 00100BB0 and 00102490).
struct SkillCommand {
	std::uint32_t flags;          // 1 left, 2 right, 4 press, 8 held, 0x10 release
	std::uint32_t pad04;
	void*         player;         // +0x08
	void*         hovered;        // +0x10
	std::int32_t  x;              // +0x18 target subtile x
	std::int32_t  y;              // +0x1C target subtile y
	std::int32_t  locationOpcode; // +0x20
	std::int32_t  unitOpcode;     // +0x24
	void*         skill;          // +0x28
};
static_assert(offsetof(SkillCommand, player) == 0x08);
static_assert(offsetof(SkillCommand, x) == 0x18);
static_assert(offsetof(SkillCommand, skill) == 0x28);

constexpr std::uint32_t CommandHeld          = 0x08;
constexpr std::int16_t  WhirlwindSrvDoFunc   = 76;
constexpr int           StateUninterruptable = 54;
constexpr int           StateDeathDelay      = 92;   // 0048E820 / 0044AA57
constexpr std::uint8_t  PacketUseItem        = 0x26; // 004C13B0 -> 004F40C0
constexpr std::int32_t  UnitTypeItem         = 4;    // 004F4164: lea edx,[rbx+4]
constexpr std::int32_t  ItemTypePotion       = 9;    // itemtypes.txt row 9 (poti)
constexpr std::uint32_t TokenActive          = 0x01;
constexpr std::uint32_t TokenFinishing       = 0x02;
constexpr int           MaskPathfind         = 0x0C01; // 0056A540
constexpr int           MaskMoveTest         = 0x0401; // 0056A5CC
constexpr int           CollisionPlayer      = 0x80;   // 0056A62F: (1 xor 1)+1 << 7
constexpr std::uint64_t ClientAimIntervalMs  = 40;     // one game frame
constexpr std::uint32_t ControllerPlayers       = 8;      // 0014B8A7: cmp edi,8
constexpr std::size_t   ControllerStride        = 0xB8;   // 0014B831 / 001495AB: imul ...,0B8h
constexpr std::size_t   ControllerHeldButton    = 0x1968; // 001495C4: -1 when no skill button is held
constexpr std::size_t   ControllerStickX        = 0x1954; // 0014C67D: movement stick, length <= 1
constexpr std::size_t   ControllerStickY        = 0x1958; // 0014C68A
constexpr std::size_t   ControllerStickWorldX   = 0x195C; // 0014C7C3: camera-rotated, world subtiles
constexpr std::size_t   ControllerStickWorldY   = 0x1960; // 0014C7CF
constexpr std::size_t   ControllerStickDeadzone = 0x1F94; // 0014C7F8: on the manager, not per slot
constexpr int           StickRunway             = 10;     // subtiles ahead along the stick
constexpr int           MaxAimDistance       = 40;     // server accepts up to 50 (004FE1F0)
constexpr std::int32_t  ItemTypeGold         = 4;      // itemtypes.txt row 4 (gold): 000FA255, 00471B04
constexpr std::size_t   AutoGoldManager      = 0x1918; // client context+1918h: 00101A4E, 000FA23C
constexpr std::uint8_t  PacketPickupGround      = 0x16; // 004B9DF0
constexpr std::uint32_t PickupGroundSize        = 17;   // 004B9E2A: cmp ebx,11h
constexpr std::uint8_t  PacketPickupToInventory = 0x5F; // 004BB600
constexpr std::uint32_t PickupToInventorySize   = 25;   // 004BB631: cmp ebx,19h
constexpr std::size_t   PickupItemGuid          = 1;    // 004B9E76, 004BB6E9: GUID at +1

inline auto U32(void* object, std::size_t offset) noexcept -> std::uint32_t& {
	return *static_cast<std::uint32_t*>(static_cast<void*>(static_cast<std::uint8_t*>(object) + offset));
}
inline auto U16(void* object, std::size_t offset) noexcept -> std::uint16_t& {
	return *static_cast<std::uint16_t*>(static_cast<void*>(static_cast<std::uint8_t*>(object) + offset));
}
inline auto UnitType(void* unit) noexcept -> std::uint32_t { return U32(unit, 0x00); }         // 002310B0 et al: *unit == 2
inline auto GameFrame(void* game) noexcept -> std::uint32_t { return U32(game, 0x170); }       // 0056A84C: game+368
inline auto GameDataContext(void* game) noexcept -> std::uint8_t { return static_cast<std::uint8_t*>(game)[0x106]; } // 0043B427
inline auto SkillRecord(void* skill) noexcept -> void* { return *static_cast<void**>(skill); } // 0033CBC0: mov rax,[rbx]
inline auto RecordSkillId(void* record) noexcept -> std::int16_t { return *static_cast<std::int16_t*>(record); }
inline auto RecordSrvDoFunc(void* record) noexcept -> std::int16_t { return static_cast<std::int16_t*>(record)[39]; } // +78, 0043AED3
inline auto TokenDestX(void* skill) noexcept -> std::uint32_t& { return U32(skill, 0x24); }    // 0056A672
inline auto TokenDestY(void* skill) noexcept -> std::uint32_t& { return U32(skill, 0x28); }    // 0056A678
inline auto UnitCastId(void* unit) noexcept -> std::uint32_t& { return U32(unit, 0x12C); }     // 0034F430: unit+300
inline auto PathVelocity(void* path) noexcept -> std::uint32_t { return U32(path, 0xA0); }     // 00342B24: mov [rbx+0A0h],edi

// Owner trail in the player data, as the walk/run frame handler writes it.
constexpr std::size_t  PlayerTrailIndex  = 0x110; // 00429BCA: byte, next slot to write
constexpr std::size_t  PlayerTrailTick   = 0x118; // 00429BA9 / 00429CA4: GetTickCount64 of the last write
constexpr std::size_t  PlayerTrailPoints = 0x120; // 00429C81 / 00429C8F: x, y dwords per slot
constexpr std::uint8_t PlayerTrailSlots  = 20;    // 00429BD7 / 00429C9D
constexpr std::int64_t TrailIntervalMs   = 25;    // 00429BB0: add rcx,19h
constexpr std::int32_t TrailMinSquared   = 45;    // 00429C77: cmp eax,2Dh

// Native functions (RVA noted; each is proven by a witness).
struct Native {
	void* (*getUsedSkill)(void*)                                        = nullptr; // 0034BA40
	std::uint32_t (*getSkillFlags)(void*)                               = nullptr; // 0033CC00
	void (*setSkillFlags)(void*, std::uint32_t)                         = nullptr; // 0033EC20
	void* (*getLeftSkill)(void*)                                        = nullptr; // 0034A540
	void* (*getRightSkill)(void*)                                       = nullptr; // 0034B400
	void (*setUsedSkill)(void*, void*, std::uint32_t)                   = nullptr; // 0034F430
	std::int32_t (*checkState)(void*, std::int32_t)                     = nullptr; // 003351B0
	void* (*getDynamicPath)(void*)                                      = nullptr; // 0034AE80
	void* (*getRoom)(void*)                                             = nullptr; // 0034B440
	std::int32_t (*collisionPattern)(void*)                             = nullptr; // 00341870
	void (*collisionReset)(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr; // 003636D0
	void (*collisionSet)(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t)   = nullptr; // 00363CF0
	void (*pathSetTarget)(void*, std::int32_t, std::int32_t)            = nullptr; // 00342A50
	void (*pathSetMoveMask)(void*, std::int32_t)                        = nullptr; // 00342740
	std::int32_t (*playerPathType)()                                    = nullptr; // 003412E0
	void (*pathSetType)(void*, std::int32_t)                            = nullptr; // 00342A70
	std::int32_t (*pathGetPoints)(void*, PathPoint**)                   = nullptr; // 00341AE0
	std::int32_t (*pathGetX)(void*)                                     = nullptr; // 00341A20
	std::int32_t (*pathGetY)(void*)                                     = nullptr; // 00341A30
	std::int32_t (*serverPathCompute)(void*, void*, std::int32_t, std::int32_t) = nullptr; // 00340E70
	// 00350B40, the game's movement speed function (UNITS_UpdateAnimRateAndVelocity).
	void (*updateAnimRateAndVelocity)(void*)                            = nullptr; // 00350B40
	// 00436830, a D2RLoader jump to D2RCore ConsumeWideSkillResource. The only
	// native caller (0043B5B4) ignores the result, so the plugin does too. Id
	// and level travel as full-width registers so D2RCore sees clean values
	// whichever width it reads.
	void (*manaConsume)(void*, void*, std::uint64_t, std::uint64_t)     = nullptr; // 00436830
	std::int32_t (*manaCost)(std::uint8_t, std::int32_t, std::int32_t) = nullptr; // 0033AA00
	// 0033D1E0, a D2RLoader jump to D2RCore ReadWideSkillLevel; same call shape
	// as the native cast gate (unit, skill, 1, 0).
	std::int32_t (*skillLevel)(void*, void*, std::uint64_t, std::uint64_t) = nullptr; // 0033D1E0
	// 00436750, the native cast gate: can the unit pay for its used skill.
	std::int32_t (*canPayUsedSkill)(void*)                              = nullptr; // 00436750
	std::uint8_t (*clientPathCompute)(void*, void*)                     = nullptr; // 00215EF0
	std::int64_t (*sendCommand)(std::int32_t, void*, std::int32_t, std::int32_t) = nullptr; // 000FC000
	std::int32_t (*localPlayerIndex)()                                  = nullptr; // 0008B2D0
	void* (*playerFromIndex)(std::int32_t)                              = nullptr; // 0009A480
	void* (*getServerUnit)(void*, std::int32_t, std::uint32_t)          = nullptr; // 0048FE80
	std::int32_t (*checkItemTypeId)(void*, std::int32_t)                = nullptr; // 00373890
	void* (*getPlayerData)(void*)                                       = nullptr; // 0034B240
	std::int32_t (*squaredDistance)(std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr; // 003412C0
	// Gold while Whirlwinding.
	std::int64_t (*canActNow)(void*, void*)                             = nullptr; // 0009A8C0
	void* (*clientContext)()                                            = nullptr; // 00144640
	void* (*autoGoldTarget)(void*)                                      = nullptr; // 00192B20
	std::int64_t (*interactWithUnit)(std::uint32_t, std::uint32_t)      = nullptr; // 000FA180
	std::uint32_t (*getUnitId)(void*)                                   = nullptr; // 0034A330
	std::int32_t (*getItemType)(void*)                                  = nullptr; // 00372C90
	// Cyclone (controller).
	bool (*isControllerMode)()                                          = nullptr; // 00077E10
	std::int32_t (*buttonSkill)(std::uint32_t)                          = nullptr; // 00117D20
	std::int64_t (*controllerPlayerUpdate)(void*, std::uint32_t, std::uint32_t) = nullptr; // 0014C540
	// Originals of redirected calls.
	std::int32_t (*serverPathStatus)(void*, void*)                      = nullptr; // 00491AC0
	std::int32_t (*clientPathStep)(void*, std::int32_t)                 = nullptr; // 000F9A00
	std::int32_t (*procStart)(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr; // 00217640
	std::uint8_t (*modeCheck)(void*, std::int32_t)                      = nullptr; // 000F99B0
	std::int64_t (*setNeutral)(void*, std::int32_t)                     = nullptr; // 000F9860
	std::int64_t (*procTail)(void*, std::int32_t)                       = nullptr; // 0034E790
};

Native g_native {};

template <typename Fn>
void Bind(Fn& fn, std::uintptr_t base, std::uint64_t rva) noexcept {
	fn = reinterpret_cast<Fn>(base + rva);
}

void BindNatives(std::uintptr_t base) noexcept {
	Bind(g_native.getUsedSkill, base, 0x34BA40);
	Bind(g_native.getSkillFlags, base, 0x33CC00);
	Bind(g_native.setSkillFlags, base, 0x33EC20);
	Bind(g_native.getLeftSkill, base, 0x34A540);
	Bind(g_native.getRightSkill, base, 0x34B400);
	Bind(g_native.setUsedSkill, base, 0x34F430);
	Bind(g_native.checkState, base, 0x3351B0);
	Bind(g_native.getDynamicPath, base, 0x34AE80);
	Bind(g_native.getRoom, base, 0x34B440);
	Bind(g_native.collisionPattern, base, 0x341870);
	Bind(g_native.collisionReset, base, 0x3636D0);
	Bind(g_native.collisionSet, base, 0x363CF0);
	Bind(g_native.pathSetTarget, base, 0x342A50);
	Bind(g_native.pathSetMoveMask, base, 0x342740);
	Bind(g_native.playerPathType, base, 0x3412E0);
	Bind(g_native.pathSetType, base, 0x342A70);
	Bind(g_native.pathGetPoints, base, 0x341AE0);
	Bind(g_native.pathGetX, base, 0x341A20);
	Bind(g_native.pathGetY, base, 0x341A30);
	Bind(g_native.serverPathCompute, base, 0x340E70);
	Bind(g_native.updateAnimRateAndVelocity, base, 0x350B40);
	Bind(g_native.manaConsume, base, 0x436830);
	Bind(g_native.manaCost, base, 0x33AA00);
	Bind(g_native.skillLevel, base, 0x33D1E0);
	Bind(g_native.canPayUsedSkill, base, 0x436750);
	Bind(g_native.clientPathCompute, base, 0x215EF0);
	Bind(g_native.sendCommand, base, 0x0FC000);
	Bind(g_native.localPlayerIndex, base, 0x08B2D0);
	Bind(g_native.playerFromIndex, base, 0x09A480);
	Bind(g_native.getServerUnit, base, 0x48FE80);
	Bind(g_native.checkItemTypeId, base, 0x373890);
	Bind(g_native.getPlayerData, base, 0x34B240);
	Bind(g_native.squaredDistance, base, 0x3412C0);
	Bind(g_native.canActNow, base, 0x09A8C0);
	Bind(g_native.clientContext, base, 0x144640);
	Bind(g_native.autoGoldTarget, base, 0x192B20);
	Bind(g_native.interactWithUnit, base, 0x0FA180);
	Bind(g_native.getUnitId, base, 0x34A330);
	Bind(g_native.getItemType, base, 0x372C90);
	Bind(g_native.isControllerMode, base, 0x077E10);
	Bind(g_native.buttonSkill, base, 0x117D20);
	Bind(g_native.controllerPlayerUpdate, base, 0x14C540);
	Bind(g_native.serverPathStatus, base, 0x491AC0);
	Bind(g_native.clientPathStep, base, 0x0F9A00);
	Bind(g_native.procStart, base, 0x217640);
	Bind(g_native.modeCheck, base, 0x0F99B0);
	Bind(g_native.setNeutral, base, 0x0F9860);
	Bind(g_native.procTail, base, 0x34E790);
}

auto IsWhirlwindSkill(void* skill) noexcept -> bool {
	if (skill == nullptr) {
		return false;
	}
	void* record = SkillRecord(skill);
	return record != nullptr && RecordSrvDoFunc(record) == WhirlwindSrvDoFunc;
}

// The unit's used skill is a Whirlwind and its path is active, the same test
// the potion parts make.
auto IsWhirlwinding(void* unit) noexcept -> bool {
	void* const used = g_native.getUsedSkill(unit);
	return IsWhirlwindSkill(used) && (g_native.getSkillFlags(used) & TokenActive) != 0;
}

// The same endpoint bookkeeping the native starts do (0056A5E0..0056A678 on the
// server, 00230CBC..00230D6E on the client): release the old destination's
// collision reservation, reserve the new one, store it in the token.
void FinishRetarget(void* unit, void* skill, void* path, void* room, std::int32_t pattern) noexcept {
	g_native.pathSetMoveMask(path, MaskMoveTest);
	std::int32_t endX   = 0;
	std::int32_t endY   = 0;
	PathPoint*   points = nullptr;
	const std::int32_t count = g_native.pathGetPoints(path, &points);
	if (count > 0 && points != nullptr) {
		endX = points[count - 1].x;
		endY = points[count - 1].y;
	}
	if (endX == 0 || endY == 0) {
		// No route (aim inside a wall, or on the character): the character
		// stays where it is and keeps spinning. The token must never hold a
		// zero destination, the tick treats that as "remove Whirlwind".
		endX = g_native.pathGetX(path);
		endY = g_native.pathGetY(path);
	}
	if (room != nullptr && endX != 0 && endY != 0) {
		g_native.collisionSet(room, endX, endY, pattern, CollisionPlayer);
	}
	if (endX != 0 && endY != 0) {
		TokenDestX(skill) = static_cast<std::uint32_t>(endX);
		TokenDestY(skill) = static_cast<std::uint32_t>(endY);
	}
	(void)unit;
}

auto BeginRetarget(void* unit, void* skill, std::int32_t x, std::int32_t y, void*& path, void*& room, std::int32_t& pattern) noexcept -> bool {
	path = g_native.getDynamicPath(unit);
	if (path == nullptr) {
		return false;
	}
	room    = g_native.getRoom(unit);
	pattern = g_native.collisionPattern(unit);
	const std::uint32_t oldX = TokenDestX(skill);
	const std::uint32_t oldY = TokenDestY(skill);
	if (room != nullptr && oldX != 0 && oldY != 0) {
		g_native.collisionReset(room, static_cast<std::int32_t>(oldX), static_cast<std::int32_t>(oldY), pattern, CollisionPlayer);
	}
	g_native.pathSetTarget(path, x, y);
	g_native.pathSetMoveMask(path, MaskPathfind);
	g_native.pathSetType(path, g_native.playerPathType());
	return true;
}

// Movement speed after a retarget. The path compute keeps the path's velocity
// as it is, but arriving at the last point of a route zeroes it (00380FD0
// writes 0 to path+A0h when the unit sits on the centre of its final point),
// which is why the native starts write a velocity after their compute. A
// retarget never invents one: the velocity the game set stays, including
// every change the game made for faster run/walk, slows and haste. Only a
// route that starts from a standstill asks the game's movement speed function
// 00350B40 for it. For a player in mode 18 whose used skill has
// (flags & 0x1001) == 1, which a running Whirlwind always has, that function
// sets walk speed scaled by faster run/walk and velocitypercent, the same
// value the native client start gets from it (00230D75 -> 00214E30). So
// client and server always agree.
void RestoreMovementSpeed(void* unit, void* path) noexcept {
	if (PathVelocity(path) == 0) {
		g_native.updateAnimRateAndVelocity(unit);
	}
}

void ServerRetarget(void* unit, void* skill, std::int32_t x, std::int32_t y) noexcept {
	void*        path    = nullptr;
	void*        room    = nullptr;
	std::int32_t pattern = 0;
	if (!BeginRetarget(unit, skill, x, y, path, room, pattern)) {
		return;
	}
	if (g_native.serverPathCompute(path, unit, 0, 1) != 0) {
		RestoreMovementSpeed(unit, path);
	}
	FinishRetarget(unit, skill, path, room, pattern);
}

void ClientRetarget(void* unit, void* skill, std::int32_t x, std::int32_t y) noexcept {
	void*        path    = nullptr;
	void*        room    = nullptr;
	std::int32_t pattern = 0;
	if (!BeginRetarget(unit, skill, x, y, path, room, pattern)) {
		return;
	}
	if (g_native.clientPathCompute(unit, path) != 0) {
		RestoreMovementSpeed(unit, path);
	}
	FinishRetarget(unit, skill, path, room, pattern);
}

// ---------------------------------------------------------------------------
// Cyclone: server
// ---------------------------------------------------------------------------

struct ServerSession {
	void*         unit            = nullptr;
	std::uint32_t lastSeenFrame   = 0;
	std::uint32_t lastAimFrame    = 0;
	std::uint32_t nextChargeFrame = 0;
};

std::array<ServerSession, 32> g_serverSessions {};

auto ServerSessionFor(void* unit, std::uint32_t frame) noexcept -> ServerSession& {
	ServerSession* freeSlot = nullptr;
	for (ServerSession& s : g_serverSessions) {
		if (s.unit == unit) {
			if (frame - s.lastSeenFrame <= 1) {
				return s;
			}
			freeSlot = &s;
			break;
		}
		if (freeSlot == nullptr && (s.unit == nullptr || frame - s.lastSeenFrame > 250)) {
			freeSlot = &s;
		}
	}
	if (freeSlot == nullptr) {
		freeSlot = &g_serverSessions[0];
	}
	// A new Whirlwind: its first frame counts as the first held update.
	freeSlot->unit            = unit;
	freeSlot->lastSeenFrame   = frame;
	freeSlot->lastAimFrame    = frame;
	freeSlot->nextChargeFrame = frame + static_cast<std::uint32_t>(g_config.manaIntervalFrames);
	return *freeSlot;
}

void EndServerSession(ServerSession& s) noexcept {
	s.unit = nullptr;
}

// Pays one interval the way the native skill start pays a cast (0043B3E0):
// the cast gate 00436750 decides whether the used skill can be paid for, then
// the resource payment 00436830 pays it and its result is not used. The gate
// is the check every cast of this skill faces (mana, or charges for a skill
// cast from charges), so a held Whirlwind ends exactly when a fresh cast would
// be refused. The caller has already made sure the used skill is Whirlwind.
auto ChargeWhirlwind(void* game, void* unit, void* skill) noexcept -> bool {
	void* record = SkillRecord(skill);
	if (record == nullptr) {
		return true;
	}
	const std::int32_t skillId = RecordSkillId(record);
	const std::int32_t level   = g_native.skillLevel(unit, skill, 1, 0);
	if (skillId < 0 || level <= 0) {
		return true;
	}
	if (g_native.manaCost(GameDataContext(game), skillId, level) <= 0) {
		return true; // a skill with no cost is never drained
	}
	if (g_native.canPayUsedSkill(unit) == 0) {
		return false;
	}
	g_native.manaConsume(game, unit, static_cast<std::uint64_t>(skillId), static_cast<std::uint64_t>(level));
	return true;
}

// Replaces the call to the path-status query at 0042A99F inside the sequence
// frame handler 0042A940. Status 2 means "path finished": the handler then
// requests the finish (token bit1) and runs the skill's do-function, which
// removes Whirlwind. While the button is held, a finished path is reported as
// still running, so the character spins where it stands until the next aim.
// When the hold lapses, or the recurring cost cannot be paid, status 2 is
// reported so the native finish ends Whirlwind at once. Every frame a
// player's Whirlwind runs through here it also writes the owner trail the
// pets follow (RecordPetTrail), since the skill mode never does.
bool g_cycloneServerActive = false;
bool g_deathFixActive      = false;
bool g_petTrailActive      = false;

std::atomic<std::uint64_t> g_petTrailWrites {0};

// The owner trail write of the walk/run frame handler (00429B8E..00429CBB),
// done for a player whose Whirlwind moved them this frame. Same clock, same
// 25 ms spacing, same squared-distance threshold of 45 against the newest
// point, same 20-slot ring. Called right after the path status, which is
// where the walk/run handler does it too.
void RecordPetTrail(void* unit) noexcept {
	const auto now = static_cast<std::int64_t>(GetTickCount64());
	auto* const data = static_cast<std::uint8_t*>(g_native.getPlayerData(unit));
	if (data == nullptr) {
		return;
	}
	std::int64_t last = 0;
	std::memcpy(&last, data + PlayerTrailTick, sizeof(last));
	if (now <= last + TrailIntervalMs) {
		return;
	}
	const std::uint8_t index = data[PlayerTrailIndex];
	if (index >= PlayerTrailSlots) {
		return; // never written by the game; refuse rather than write past the ring
	}
	const std::uint8_t newest = index != 0 ? static_cast<std::uint8_t>(index - 1) : static_cast<std::uint8_t>(PlayerTrailSlots - 1);
	void* const path = g_native.getDynamicPath(unit);
	if (path == nullptr) {
		return;
	}
	const std::int32_t x = g_native.pathGetX(path);
	const std::int32_t y = g_native.pathGetY(path);
	auto* const points = data + PlayerTrailPoints;
	std::int32_t newestX = 0;
	std::int32_t newestY = 0;
	std::memcpy(&newestX, points + 8 * newest, sizeof(newestX));
	std::memcpy(&newestY, points + 8 * newest + 4, sizeof(newestY));
	if (g_native.squaredDistance(x, y, newestX, newestY) <= TrailMinSquared) {
		return;
	}
	std::memcpy(points + 8 * index, &x, sizeof(x));
	std::memcpy(points + 8 * index + 4, &y, sizeof(y));
	std::memcpy(data + PlayerTrailTick, &now, sizeof(now));
	data[PlayerTrailIndex] = index + 1 >= PlayerTrailSlots ? 0 : static_cast<std::uint8_t>(index + 1);
	g_petTrailWrites.fetch_add(1, std::memory_order_relaxed);
}

void ForgetServerSession(void* unit) noexcept {
	for (ServerSession& s : g_serverSessions) {
		if (s.unit == unit) {
			EndServerSession(s);
		}
	}
}

std::int32_t __fastcall OnServerPathStatus(void* game, void* unit) noexcept {
	const std::int32_t status = g_native.serverPathStatus(game, unit);
	if (game == nullptr || unit == nullptr || UnitType(unit) != 0) {
		return status;
	}
	void* const skill = g_native.getUsedSkill(unit);
	if (!IsWhirlwindSkill(skill)) {
		return status;
	}
	if (g_petTrailActive) {
		RecordPetTrail(unit);
	}
	const std::uint32_t flags = g_native.getSkillFlags(skill);
	if ((flags & TokenActive) == 0 || (flags & TokenFinishing) != 0) {
		return status;
	}
	// A lethal hit on an Uninterruptible unit only marks the death as pending
	// (0044AA57 sets state 92); the death itself happens when Uninterruptible is
	// cleared (0048E820). Report the path finished: the native finish runs the
	// tick, RemoveWhirlwind 0056BB60 clears Uninterruptible, and the game's own
	// pending-death code kills the character in that same call.
	if (g_deathFixActive && g_native.checkState(unit, StateDeathDelay) != 0) {
		ForgetServerSession(unit);
		return 2;
	}
	if (!g_cycloneServerActive) {
		return status;
	}
	const std::uint32_t frame = GameFrame(game);
	ServerSession&      s     = ServerSessionFor(unit, frame);
	s.lastSeenFrame           = frame;
	if (frame - s.lastAimFrame > g_serverGraceFrames) {
		EndServerSession(s);
		return 2;
	}
	if (g_config.manaIntervalFrames > 0 && frame >= s.nextChargeFrame) {
		s.nextChargeFrame = frame + static_cast<std::uint32_t>(g_config.manaIntervalFrames);
		if (!ChargeWhirlwind(game, unit, skill)) {
			EndServerSession(s);
			return 2;
		}
	}
	return status == 2 ? 1 : status;
}

using PositionExecutorFn = std::int32_t(__fastcall*)(void*, void*, std::uint16_t, std::uint16_t, std::int32_t, void*);
PositionExecutorFn g_originalPositionExecutor = nullptr;

// Entry hook on the server position executor 004FDB40, reached from the
// skill-on-location packets. A command for the Whirlwind that is currently
// running is the client's aim update: retarget the live path, renew the hold,
// and never pass it on, so the native code cannot queue it as a new cast.
// Returns 1, the executor's own "handled, nothing further" result.
std::int32_t __fastcall OnPositionExecutor(void* game, void* player, std::uint16_t x, std::uint16_t y, std::int32_t argument, void* activeSkill) noexcept {
	if (g_cycloneServerActive && game != nullptr && player != nullptr && activeSkill != nullptr && UnitType(player) == 0) {
		void* const used = g_native.getUsedSkill(player);
		if (used == activeSkill && IsWhirlwindSkill(used)) {
			const std::uint32_t flags = g_native.getSkillFlags(used);
			if ((flags & TokenActive) != 0) {
				if ((flags & TokenFinishing) == 0) {
					const std::uint32_t frame = GameFrame(game);
					ServerSession&      s     = ServerSessionFor(player, frame);
					s.lastAimFrame            = frame;
					ServerRetarget(player, used, x, y);
				}
				return 1;
			}
		}
	}
	return g_originalPositionExecutor(game, player, x, y, argument, activeSkill);
}

// ---------------------------------------------------------------------------
// Whirlwind speed sync (server start)
// ---------------------------------------------------------------------------

using WhirlwindStartFn = std::int64_t(__fastcall*)(void*, void*, std::uint32_t, std::uint32_t);
WhirlwindStartFn g_originalWhirlwindStart = nullptr;

// Entry hook on the server Whirlwind start 0056A420 (srvstfunc 38). Its path
// block sets plain walk speed (00438180 at 0056A5B0, 00342AE0 at 0056A5C7)
// before it sets the token active (0033EC20 at 0056A668), and nothing after that runs
// the movement speed function 00350B40 unless the skill has an aura state
// (00574480). The server's mode 18 start 0042A8B0 runs it too early, through
// 004919C0, while the token is still inactive, so its velocity branch is
// skipped. The client start 00230A10 sets the token active first and then
// runs the function through 00214E30, so the client moves at walk speed scaled
// by faster run/walk and velocitypercent while the server moves at plain walk
// speed. After a start that left a player's Whirlwind active, run the same
// function the client ran, at the same point in the start.
std::int64_t __fastcall OnServerWhirlwindStart(void* game, void* unit, std::uint32_t skillId, std::uint32_t skillLevel) noexcept {
	const std::int64_t result = g_originalWhirlwindStart(game, unit, skillId, skillLevel);
	if (static_cast<std::uint32_t>(result) != 0 && unit != nullptr && UnitType(unit) == 0) {
		void* const used = g_native.getUsedSkill(unit);
		if (IsWhirlwindSkill(used) && (g_native.getSkillFlags(used) & TokenActive) != 0) {
			g_native.updateAnimRateAndVelocity(unit);
		}
	}
	return result;
}

// ---------------------------------------------------------------------------
// Cyclone: client (local player only)
// ---------------------------------------------------------------------------

struct ClientSession {
	void*         unit        = nullptr;
	std::uint64_t lastSeenMs  = 0;
	std::uint64_t lastAimMs   = 0;
	std::uint64_t lastSendMs  = 0;
	float         headingX    = 0.0F;
	float         headingY    = 0.0F;
	bool          haveHeading = false;
};

ClientSession g_client {};

// Path fields the starting heading reads.
constexpr std::size_t PathPositionX  = 0x00; // 003416D9: x in 16.16 subtiles
constexpr std::size_t PathPositionY  = 0x04; // 003416D9: y in 16.16 subtiles
constexpr std::size_t PathCastTarget = 0x14; // 003411EF / 003411F7: x, y words
constexpr std::size_t PathFacing     = 0x80; // 0034195D: float, 0 to 63

// The offsets the game aims at when a skill has no target, 2 subtiles ahead,
// one per eighth of the facing (00214D41, tables at 01CE21F0).
constexpr std::array<std::int32_t, 8> AheadX {0, -1, -2, -1, 0, 1, 2, 1};
constexpr std::array<std::int32_t, 8> AheadY {2, 1, 0, -1, -2, -1, 0, 1};

std::atomic<std::uint64_t> g_headingsFromCast {0};
std::atomic<std::uint64_t> g_headingsFromFacing {0};

// The heading a session starts with, so near_cursor = "keep_heading" always has
// one, from the first frame on. Without it, a Whirlwind cast at a point inside
// the near radius (or, on a controller, cast without pushing the stick, which
// aims 2 subtiles ahead) had no heading to keep and spun in place.
// It is the direction the Whirlwind was cast in: from the character's exact
// position to the centre of the subtile the cast aimed at, which the path keeps
// at +14h/+16h. A session starts on the first frame of a Whirlwind, before any
// aim update recomputed the path, so that subtile is the cast's own. The game
// never accepts a cast at the character's own subtile (00340E70 refuses that
// target, 00230A10 refuses a route with no points), so the direction is real.
// A session that starts again mid-Whirlwind, after a frame hitch, reads the
// last aim the path was computed to instead, so the heading carries on.
// Only if the character already stands on that centre does the facing decide,
// the same way the game picks a point ahead when a skill has no target.
void SeedHeading(ClientSession& s, void* unit) noexcept {
	void* const path = g_native.getDynamicPath(unit);
	if (path == nullptr) {
		return;
	}
	const std::uint32_t castX = U16(path, PathCastTarget);
	const std::uint32_t castY = U16(path, PathCastTarget + 2);
	if (castX != 0 && castY != 0) {
		const double dx = (static_cast<double>((castX << 16) + 0x8000U) - static_cast<double>(U32(path, PathPositionX))) / 65536.0;
		const double dy = (static_cast<double>((castY << 16) + 0x8000U) - static_cast<double>(U32(path, PathPositionY))) / 65536.0;
		const double length = std::sqrt(dx * dx + dy * dy);
		if (length > 1.0 / 256.0) {
			s.headingX    = static_cast<float>(dx / length);
			s.headingY    = static_cast<float>(dy / length);
			s.haveHeading = true;
			g_headingsFromCast.fetch_add(1, std::memory_order_relaxed);
			return;
		}
	}
	float facing = 0.0F;
	std::memcpy(&facing, static_cast<std::uint8_t*>(path) + PathFacing, sizeof(facing));
	if (!(facing >= 0.0F && facing < 64.0F)) {
		facing = 0.0F;
	}
	const std::size_t eighth = (static_cast<std::uint32_t>(facing) & 0x3FU) >> 3;
	const float       ox     = static_cast<float>(AheadX[eighth]);
	const float       oy     = static_cast<float>(AheadY[eighth]);
	const float       length = std::sqrt(ox * ox + oy * oy);
	s.headingX               = ox / length;
	s.headingY               = oy / length;
	s.haveHeading            = true;
	g_headingsFromFacing.fetch_add(1, std::memory_order_relaxed);
}

auto ClientSessionFor(void* unit, std::uint64_t now) noexcept -> ClientSession& {
	if (g_client.unit != unit || now - g_client.lastSeenMs > 90) {
		g_client             = {};
		g_client.unit        = unit;
		g_client.lastSeenMs  = now;
		g_client.lastAimMs   = now;
		SeedHeading(g_client, unit);
	}
	return g_client;
}

auto LocalPlayer() noexcept -> void* {
	return g_native.playerFromIndex(g_native.localPlayerIndex());
}

// Replaces the path step at 000FFCFD inside the client unit update 000FFBB0.
// A nonzero result makes the update request the finish and run the skill's
// client do-function, the same thing a vanilla Whirlwind reaching its end
// does. Mirrors the server rule: finished paths keep spinning while held,
// and the lapse of the hold reports "finished".
std::int32_t __fastcall OnClientPathStep(void* unit, std::int32_t flag) noexcept {
	const std::int32_t finished = g_native.clientPathStep(unit, flag);
	if (unit == nullptr || UnitType(unit) != 0 || unit != LocalPlayer()) {
		return finished;
	}
	void* const skill = g_native.getUsedSkill(unit);
	if (!IsWhirlwindSkill(skill)) {
		return finished;
	}
	const std::uint32_t flags = g_native.getSkillFlags(skill);
	if ((flags & TokenActive) == 0 || (flags & TokenFinishing) != 0) {
		return finished;
	}
	const std::uint64_t now = GetTickCount64();
	ClientSession&      s   = ClientSessionFor(unit, now);
	s.lastSeenMs            = now;
	if (now - s.lastAimMs > static_cast<std::uint64_t>(g_config.holdGraceMs)) {
		s.unit = nullptr;
		return 1;
	}
	return 0;
}

// Set once the controller steering is installed (OnControllerPlayerUpdate).
bool g_cycloneControllerActive = false;

auto HandleCycloneAim(SkillCommand* command, void* skill) noexcept -> bool {
	if (command == nullptr || skill == nullptr || (command->flags & CommandHeld) == 0) {
		return false;
	}
	void* const player = command->player;
	if (player == nullptr || UnitType(player) != 0) {
		return false;
	}
	void* const used = g_native.getUsedSkill(player);
	if (used != skill || !IsWhirlwindSkill(used)) {
		return false;
	}
	const std::uint32_t flags = g_native.getSkillFlags(used);
	if ((flags & TokenActive) == 0) {
		return false;
	}
	const std::uint64_t now = GetTickCount64();
	ClientSession&      s   = ClientSessionFor(player, now);
	// A Whirlwind that is finishing, or whose hold already lapsed, is never
	// revived from here. The next held update after it ends starts a fresh
	// cast through the vanilla path, on both sides at once.
	if ((flags & TokenFinishing) != 0 || now - s.lastAimMs > static_cast<std::uint64_t>(g_config.holdGraceMs)) {
		return true;
	}
	s.lastAimMs = now;
	// With a controller the held command carries the game's controller aim
	// (a point 2 subtiles ahead, or a docked enemy), which is inside the near
	// radius and would only ever spin in place. The held command still proves
	// the button is down; the steering comes from the stick, in
	// OnControllerPlayerUpdate.
	if (g_cycloneControllerActive && g_native.isControllerMode()) {
		return true;
	}
	if (s.lastSendMs != 0 && now - s.lastSendMs < ClientAimIntervalMs) {
		return true;
	}

	void* const        path = g_native.getDynamicPath(player);
	if (path == nullptr) {
		return true;
	}
	const std::int32_t px = g_native.pathGetX(path);
	const std::int32_t py = g_native.pathGetY(path);
	std::int32_t       dx = command->x - px;
	std::int32_t       dy = command->y - py;
	const std::int32_t r  = g_config.nearCursorRadius;
	std::int32_t       ax = px;
	std::int32_t       ay = py;
	if (dx * dx + dy * dy > r * r) {
		const float length = std::sqrt(static_cast<float>(dx * dx + dy * dy));
		s.headingX         = static_cast<float>(dx) / length;
		s.headingY         = static_cast<float>(dy) / length;
		s.haveHeading      = true;
		if (length > static_cast<float>(MaxAimDistance)) {
			dx = static_cast<std::int32_t>(std::lround(s.headingX * static_cast<float>(MaxAimDistance)));
			dy = static_cast<std::int32_t>(std::lround(s.headingY * static_cast<float>(MaxAimDistance)));
		}
		ax = px + dx;
		ay = py + dy;
	} else if (g_config.keepHeading && s.haveHeading) {
		const float runway = static_cast<float>(r * 2 + 2 > 6 ? r * 2 + 2 : 6);
		ax                 = px + static_cast<std::int32_t>(std::lround(s.headingX * runway));
		ay                 = py + static_cast<std::int32_t>(std::lround(s.headingY * runway));
	}
	if (ax <= 0 || ay <= 0 || ax > 0xFFFF || ay > 0xFFFF) {
		return true;
	}

	ClientRetarget(player, used, ax, ay);
	// Plain skill-on-location for the slot that holds the skill. No pierce
	// counter bump (stat 328): both sides only bump it when a cast starts
	// (000FB004 client, 0042D39E server), and an aim update is not a cast.
	const std::int32_t opcode = used == g_native.getLeftSkill(player) ? 0x05 : 0x0C;
	g_native.sendCommand(opcode, player, ax, ay);
	s.lastSendMs = now;
	return true;
}

using HeldSkillFn = std::int64_t(__fastcall*)(SkillCommand*, void*);
using HeldLeftFn  = std::int64_t(__fastcall*)(SkillCommand*);
HeldSkillFn g_originalHeldSkill = nullptr;
HeldLeftFn  g_originalHeldLeft  = nullptr;

// Held right button (and left+0x400) handler 00101FA0. Vanilla asks the busy
// gate 0009A8C0 next, which refuses every command while Whirlwind runs.
std::int64_t __fastcall OnHeldSkillCommand(SkillCommand* command, void* skill) noexcept {
	if (HandleCycloneAim(command, skill)) {
		return 1;
	}
	return g_originalHeldSkill(command, skill);
}

// Held left button handler 001013B0; it resolves the left skill itself.
std::int64_t __fastcall OnHeldLeftCommand(SkillCommand* command) noexcept {
	if (command != nullptr && command->player != nullptr && HandleCycloneAim(command, g_native.getLeftSkill(command->player))) {
		return 1;
	}
	return g_originalHeldLeft(command);
}

// ---------------------------------------------------------------------------
// Cyclone: controller (local player only)
// ---------------------------------------------------------------------------

std::atomic<std::uint64_t> g_controllerAims {0};

// Where the stick sends Whirlwind: StickRunway subtiles ahead of the character
// along the stick's world direction, the vector the game's own stick movement
// walks along (0014CC17), while the stick is pushed past the game's dead zone.
// A released stick is the controller's "cursor on the character": it keeps the
// last stick heading or spins in place, as near_cursor says. Until the stick is
// pushed, the last heading is the direction the Whirlwind was cast in
// (SeedHeading).
void StickAim(const std::uint8_t* manager, const std::uint8_t* slot, std::int32_t px, std::int32_t py, ClientSession& s, std::int32_t& ax, std::int32_t& ay) noexcept {
	float rawX     = 0.0F;
	float rawY     = 0.0F;
	float worldX   = 0.0F;
	float worldY   = 0.0F;
	float deadzone = 0.0F;
	std::memcpy(&rawX, slot + ControllerStickX, sizeof(rawX));
	std::memcpy(&rawY, slot + ControllerStickY, sizeof(rawY));
	std::memcpy(&worldX, slot + ControllerStickWorldX, sizeof(worldX));
	std::memcpy(&worldY, slot + ControllerStickWorldY, sizeof(worldY));
	std::memcpy(&deadzone, manager + ControllerStickDeadzone, sizeof(deadzone));
	ax = px;
	ay = py;
	const float worldLength = std::sqrt(worldX * worldX + worldY * worldY);
	if (std::sqrt(rawX * rawX + rawY * rawY) > deadzone && worldLength > 0.0001F) {
		s.headingX    = worldX / worldLength;
		s.headingY    = worldY / worldLength;
		s.haveHeading = true;
	} else if (!g_config.keepHeading || !s.haveHeading) {
		return;
	}
	ax = px + static_cast<std::int32_t>(std::lround(s.headingX * static_cast<float>(StickRunway)));
	ay = py + static_cast<std::int32_t>(std::lround(s.headingY * static_cast<float>(StickRunway)));
}

// Steering for a Whirlwind held on a controller skill button. The hold is the
// controller's own: the button held on this player slot (+1968h, set by the
// press itself) carries the skill that is Whirlwinding. That is true from the
// press on, so steering and the hold renewal start at once instead of after
// the controller's tapWindowMS, which is longer than the hold grace. Same
// session, same retarget, same aim message and the same send spacing as the
// cursor path; only the target comes from the stick.
void SteerByStick(std::uint8_t* manager, std::uint32_t index, void* player) noexcept {
	void* const used = g_native.getUsedSkill(player);
	if (!IsWhirlwindSkill(used)) {
		return;
	}
	const std::uint32_t flags = g_native.getSkillFlags(used);
	if ((flags & TokenActive) == 0 || (flags & TokenFinishing) != 0) {
		return;
	}
	std::uint8_t* const slot = manager + ControllerStride * index;
	std::int32_t        held = -1;
	std::memcpy(&held, slot + ControllerHeldButton, sizeof(held));
	if (held < 0) {
		return;
	}
	void* const record = SkillRecord(used);
	if (record == nullptr || g_native.buttonSkill(static_cast<std::uint32_t>(held)) != RecordSkillId(record)) {
		return;
	}
	const std::uint64_t now = GetTickCount64();
	ClientSession&      s   = ClientSessionFor(player, now);
	if (now - s.lastAimMs > static_cast<std::uint64_t>(g_config.holdGraceMs)) {
		return; // a lapsed hold is never revived, exactly as on the cursor path
	}
	s.lastAimMs = now;
	if (s.lastSendMs != 0 && now - s.lastSendMs < ClientAimIntervalMs) {
		return;
	}
	void* const path = g_native.getDynamicPath(player);
	if (path == nullptr) {
		return;
	}
	std::int32_t ax = 0;
	std::int32_t ay = 0;
	StickAim(manager, slot, g_native.pathGetX(path), g_native.pathGetY(path), s, ax, ay);
	if (ax <= 0 || ay <= 0 || ax > 0xFFFF || ay > 0xFFFF) {
		return;
	}
	ClientRetarget(player, used, ax, ay);
	const std::int32_t opcode = used == g_native.getLeftSkill(player) ? 0x05 : 0x0C;
	g_native.sendCommand(opcode, player, ax, ay);
	s.lastSendMs = now;
	g_controllerAims.fetch_add(1, std::memory_order_relaxed);
}

// Replaces the call to the per-player controller update 0014C540 at 0014B89D
// inside ControllerManager::Update 0014B720. The update runs first, unchanged:
// it refreshes the stick and runs the controller's own skill hold, which
// reaches HandleCycloneAim through the held-button handlers. Then, in
// controller mode, the local player's Whirlwind is steered by the stick.
std::int64_t __fastcall OnControllerPlayerUpdate(void* manager, std::uint32_t index, std::uint32_t elapsed) noexcept {
	const std::int64_t result = g_native.controllerPlayerUpdate(manager, index, elapsed);
	if (g_cycloneControllerActive && manager != nullptr && index < ControllerPlayers && g_native.isControllerMode()) {
		void* const player = g_native.playerFromIndex(index);
		if (player != nullptr && UnitType(player) == 0 && player == LocalPlayer()) {
			SteerByStick(static_cast<std::uint8_t*>(manager), index, player);
		}
	}
	return result;
}

// ---------------------------------------------------------------------------
// Proc start fix (client)
// ---------------------------------------------------------------------------

struct ProcScope {
	void*         unit      = nullptr;
	void*         usedSkill = nullptr;
	std::uint32_t castId    = 0;
	bool          open      = false;
};

ProcScope g_proc {};

// 0023123A: the client item-effect cast 002310B0 calls the full skill start.
std::int32_t __fastcall OnProcStart(void* unit, std::int32_t skillId, std::int32_t level, std::int32_t owner, std::int32_t itemCast) noexcept {
	if (unit != nullptr && !g_proc.open) {
		g_proc.unit      = unit;
		g_proc.usedSkill = g_native.getUsedSkill(unit);
		g_proc.castId    = UnitCastId(unit);
		g_proc.open      = true;
	}
	return g_native.procStart(unit, skillId, level, owner, itemCast);
}

// 002184FE: the only mode-change check in the client skill start 00218030.
std::uint8_t __fastcall OnProcModeCheck(void* unit, std::int32_t mode) noexcept {
	if (g_proc.open && unit == g_proc.unit) {
		return 0; // a proc does not change the caster's mode (the server never does)
	}
	return g_native.modeCheck(unit, mode);
}

// 00218586: a failed start forces the caster to neutral.
std::int64_t __fastcall OnProcSetNeutral(void* unit, std::int32_t mode) noexcept {
	if (g_proc.open && unit == g_proc.unit) {
		return 0;
	}
	return g_native.setNeutral(unit, mode);
}

// 00231334: the last call of 002310B0 on every path after the proc start.
std::int64_t __fastcall OnProcTail(void* unit, std::int32_t flag) noexcept {
	const std::int64_t result = g_native.procTail(unit, flag);
	if (g_proc.open && unit == g_proc.unit) {
		if (g_native.getUsedSkill(unit) != g_proc.usedSkill || UnitCastId(unit) != g_proc.castId) {
			g_native.setUsedSkill(unit, g_proc.usedSkill, g_proc.castId);
		}
		g_proc = {};
	}
	return result;
}

// ---------------------------------------------------------------------------
// Gold while Whirlwinding (server)
// ---------------------------------------------------------------------------

// The player whose gold pickup is being let past the Uninterruptible check.
// Server game thread only.
void*                      g_goldPlayer       = nullptr;
bool                       g_goldServerActive = false;
std::atomic<std::uint64_t> g_goldServerPasses {0};

// A pickup message (16h, or 5Fh in controller play) from a Whirlwinding player
// for an item that is gold. Everything else about the pickup (distance,
// position, the gold itself) stays with the native handler.
auto IsGoldPickupWhileWhirlwinding(void* game, void* player, const std::uint8_t* packet, std::uint32_t size) noexcept -> bool {
	if (packet == nullptr || !((packet[0] == PacketPickupGround && size == PickupGroundSize) || (packet[0] == PacketPickupToInventory && size == PickupToInventorySize))) {
		return false;
	}
	if (game == nullptr || player == nullptr || UnitType(player) != 0 || !IsWhirlwinding(player)) {
		return false;
	}
	std::uint32_t guid = 0;
	std::memcpy(&guid, packet + PickupItemGuid, sizeof(guid));
	void* const item = g_native.getServerUnit(game, UnitTypeItem, guid);
	return item != nullptr && g_native.getItemType(item) == ItemTypeGold;
}

// ---------------------------------------------------------------------------
// Potions while Whirlwinding (server)
// ---------------------------------------------------------------------------

// The player whose potion use is being let past the Uninterruptible checks.
// Server game thread only.
void* g_potionPlayer       = nullptr;
bool  g_potionServerActive = false;

auto IsPotionUseWhileWhirlwinding(void* game, void* player, const std::uint8_t* packet) noexcept -> bool {
	if (game == nullptr || player == nullptr || packet == nullptr || packet[0] != PacketUseItem || UnitType(player) != 0) {
		return false;
	}
	void* const used = g_native.getUsedSkill(player);
	if (!IsWhirlwindSkill(used) || (g_native.getSkillFlags(used) & TokenActive) == 0) {
		return false;
	}
	std::uint32_t guid = 0;
	std::memcpy(&guid, packet + 10, sizeof(guid)); // 004F416C: mov r8d,[rax+0Ah]
	void* const item = g_native.getServerUnit(game, UnitTypeItem, guid);
	return item != nullptr && g_native.checkItemTypeId(item, ItemTypePotion) != 0;
}

using PacketDispatchFn = std::int64_t(__fastcall*)(void*, void*, std::uint8_t*, std::uint32_t);
using UseItemFn        = std::int64_t(__fastcall*)(void*, void*, std::uint8_t*, std::uint8_t);
PacketDispatchFn g_originalPacketDispatch = nullptr;
UseItemFn        g_originalUseItem        = nullptr;

// Entry hook on the server message dispatcher 004F2FA0. Its default branch
// ignores every message while the player is Uninterruptible (004F306E). The
// hook serves the potion part (message 26h) and the gold part (messages 16h
// and 5Fh); each opens its scope only once that part is installed.
std::int64_t __fastcall OnPacketDispatch(void* game, void* player, std::uint8_t* packet, std::uint32_t size) noexcept {
	const bool potion = g_potionServerActive && g_potionPlayer == nullptr && size == 36 && IsPotionUseWhileWhirlwinding(game, player, packet);
	const bool gold   = !potion && g_goldServerActive && g_goldPlayer == nullptr && IsGoldPickupWhileWhirlwinding(game, player, packet, size);
	if (potion) {
		g_potionPlayer = player;
	}
	if (gold) {
		g_goldPlayer = player;
	}
	const std::int64_t result = g_originalPacketDispatch(game, player, packet, size);
	if (potion) {
		g_potionPlayer = nullptr;
	}
	if (gold) {
		g_goldPlayer = nullptr;
		g_goldServerPasses.fetch_add(1, std::memory_order_relaxed);
	}
	return result;
}

// Entry hook on the use-item handler 004F40C0, reached for message 0x26 both
// directly and when a queued use is processed later. It refuses the use while
// the player is Uninterruptible (004F4140).
std::int64_t __fastcall OnUseItem(void* game, void* player, std::uint8_t* packet, std::uint8_t queued) noexcept {
	const bool open = g_potionServerActive && g_potionPlayer == nullptr && IsPotionUseWhileWhirlwinding(game, player, packet);
	if (open) {
		g_potionPlayer = player;
	}
	const std::int64_t result = g_originalUseItem(game, player, packet, queued);
	if (open) {
		g_potionPlayer = nullptr;
	}
	return result;
}

// Replaces the two STATES_CheckState(player, 54) calls above. Identical to the
// native call except for the potion use or the gold pickup currently being
// processed.
std::int32_t __fastcall OnUseItemStateCheck(void* unit, std::int32_t state) noexcept {
	if (state == StateUninterruptable && unit != nullptr && (unit == g_potionPlayer || unit == g_goldPlayer)) {
		return 0;
	}
	return g_native.checkState(unit, state);
}

// ---------------------------------------------------------------------------
// Potions while Whirlwinding (client)
// ---------------------------------------------------------------------------

// The item whose client-side use lock is being evaluated. Client thread only.
void* g_clientPotionItem = nullptr;

auto IsClientPotionWhileWhirlwinding(void* item) noexcept -> bool {
	if (item == nullptr || UnitType(item) != static_cast<std::uint32_t>(UnitTypeItem)) {
		return false;
	}
	void* const player = LocalPlayer();
	if (player == nullptr) {
		return false;
	}
	void* const used = g_native.getUsedSkill(player);
	if (!IsWhirlwindSkill(used) || (g_native.getSkillFlags(used) & TokenActive) == 0) {
		return false;
	}
	return g_native.checkItemTypeId(item, ItemTypePotion) != 0;
}

using ItemUseLockFn = std::int64_t(__fastcall*)(void*);
ItemUseLockFn g_originalItemUseLock = nullptr;

// Entry hook on the client item-use lock 001C7360. The belt keys (00229594),
// inventory use (00161F20) and every other client use path ask it first and
// send nothing when it answers "locked". Vanilla locks every item while the
// local player is Uninterruptible (001C738C).
std::int64_t __fastcall OnItemUseLock(void* item) noexcept {
	const bool open = g_clientPotionItem == nullptr && IsClientPotionWhileWhirlwinding(item);
	if (open) {
		g_clientPotionItem = item;
	}
	const std::int64_t result = g_originalItemUseLock(item);
	if (open) {
		g_clientPotionItem = nullptr;
	}
	return result;
}

// Replaces STATES_CheckState(localPlayer, 54) at 001C738C. The item pointer is
// gone by then (rcx is not preserved past 001C7369), hence the entry scope.
std::int32_t __fastcall OnItemUseLockStateCheck(void* unit, std::int32_t state) noexcept {
	if (g_clientPotionItem != nullptr && state == StateUninterruptable) {
		return 0;
	}
	return g_native.checkState(unit, state);
}

// ---------------------------------------------------------------------------
// Gold while Whirlwinding (client)
// ---------------------------------------------------------------------------

bool                       g_goldClientActive = false;
std::atomic<std::uint64_t> g_goldClientSends {0};

// Replaces the call to the "can act now" gate 0009A8C0 at 000FE689 inside the
// click dispatcher 000FE3B0. Auto Gold Pickup (tick 00193010) picks gold up by
// issuing a click (action 7) on the gold it holds as its target. While
// Whirlwind runs the gate refuses every click, so vanilla sends nothing. When
// the gate refuses during a Whirlwind and the click is Auto Gold's own, the
// item interaction 000FA180 runs for that gold. That is the routine the native
// click ends in (00101ADF -> 000FABE0 -> 000FA180), without the command start
// 000FF010 in between: that start ends the caster's current skill, so it would
// end the Whirlwind (and 000FABE0 itself refuses a caster in a skill mode at
// 000FFAC0). For the Auto Gold target, 000FA180 never stops the character; it
// only sends the pickup message. The gate's answer is returned unchanged, so
// the dispatcher carries on exactly as vanilla.
std::int64_t __fastcall OnClickGate(void* player, void* skill) noexcept {
	const std::int64_t canAct = g_native.canActNow(player, skill);
	if (static_cast<std::uint32_t>(canAct) != 0 || !g_goldClientActive || player == nullptr || UnitType(player) != 0 || !IsWhirlwinding(player)) {
		return canAct;
	}
	auto* const context = static_cast<std::uint8_t*>(g_native.clientContext());
	if (context == nullptr) {
		return canAct;
	}
	// The target is set only while the Auto Gold tick's own click runs
	// (001932BE..001932D1), so a target here means this click is Auto Gold's.
	void* const gold = g_native.autoGoldTarget(context + AutoGoldManager);
	if (gold == nullptr || UnitType(gold) != static_cast<std::uint32_t>(UnitTypeItem) || g_native.getItemType(gold) != ItemTypeGold) {
		return canAct;
	}
	g_native.interactWithUnit(static_cast<std::uint32_t>(UnitTypeItem), g_native.getUnitId(gold));
	g_goldClientSends.fetch_add(1, std::memory_order_relaxed);
	return canAct;
}

// ---------------------------------------------------------------------------
// CTC while Uninterruptible
// ---------------------------------------------------------------------------

std::uint32_t g_rng = 0x9E3779B9U;

auto CtcRoll() noexcept -> std::uint32_t {
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng % 100U;
}

auto CtcAllows(std::int32_t skillId) noexcept -> bool {
	if (g_ctcAllowListActive && (skillId < 0 || skillId >= static_cast<std::int32_t>(g_ctcAllowed.size()) || !g_ctcAllowed[static_cast<std::size_t>(skillId)])) {
		return false;
	}
	if (g_config.ctcChancePercent >= 100) {
		return true;
	}
	if (g_config.ctcChancePercent <= 0) {
		return false;
	}
	return CtcRoll() < static_cast<std::uint32_t>(g_config.ctcChancePercent);
}

using CastOnTargetFn   = std::int32_t(__fastcall*)(void*, std::int32_t, std::int32_t, void*, std::int32_t);
using CastAtPositionFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t);
CastOnTargetFn   g_originalCastOnTarget   = nullptr;
CastAtPositionFn g_originalCastAtPosition = nullptr;

// 005896E0 SKILLITEM_CastOnTarget. With its Uninterruptible gate NOPed, the
// policy decides here; a refusal returns 0, the native gate's own result.
std::int32_t __fastcall OnCastOnTarget(void* caster, std::int32_t skillId, std::int32_t level, void* target, std::int32_t flag) noexcept {
	if (caster != nullptr && g_native.checkState(caster, StateUninterruptable) != 0 && !CtcAllows(skillId)) {
		return 0;
	}
	return g_originalCastOnTarget(caster, skillId, level, target, flag);
}

// 00589820 SKILLITEM_CastAtPosition, same rule.
std::int32_t __fastcall OnCastAtPosition(void* caster, std::int32_t skillId, std::int32_t level, std::int32_t x, std::int32_t y, std::int32_t flag) noexcept {
	if (caster != nullptr && g_native.checkState(caster, StateUninterruptable) != 0 && !CtcAllows(skillId)) {
		return 0;
	}
	return g_originalCastAtPosition(caster, skillId, level, x, y, flag);
}

// ---------------------------------------------------------------------------
// Whirlwind hit interval (server)
// ---------------------------------------------------------------------------

std::atomic<std::uint64_t> g_hitIntervals {0};
std::atomic<std::int32_t>  g_lastAttackRate {0};
std::atomic<std::int32_t>  g_lastIasBonus {0};
std::atomic<std::int32_t>  g_lastRate {0};
std::atomic<std::int32_t>  g_lastFrames {0};

// The frames between a player's Whirlwind hits. The Whirlwind do-function
// 00568F40 asks the interval helper 0056AC00 for them, once per hand when dual
// wielding, and it is the helper's only caller, so monster Whirlwind (which
// never calls it) is untouched. The helper reads the class's A1 animation for
// the weapon's wclass (003E3720), the attack rate stat (68) and the diminished
// IAS (00349660, used by this helper only). At 0056AD23 it holds all of them,
// and from there the relay hands them to this function instead of the game's
// clamp and division; the result goes back through the helper's own epilogue.
//   vanilla: rate     = attack rate + diminished IAS, clamped 15..175 (250
//                       shapeshifted)
//            interval = 256 * attack frame / (animation speed * rate / 100)
//   here:    rate     = 100 + (attack rate - 100) * attackrate_weight / 100
//                       + diminished IAS, clamped min_rate..max_rate
//            interval = 256 * attack frame * interval_stretch / 100
//                       / (animation speed * rate / 100), at least min_frames
// Every division rounds toward zero, like the game's.
std::int32_t __fastcall WhirlwindHitInterval(void* unit, std::int32_t attackRate, std::int32_t iasBonus,
	std::int32_t animationSpeed, std::int32_t attackFrame) noexcept {
	(void)unit;
	std::int64_t rate = 100 + (static_cast<std::int64_t>(attackRate) - 100) * g_config.attackRateWeight / 100 + iasBonus;
	if (rate < g_config.minRate) {
		rate = g_config.minRate;
	} else if (rate > g_config.maxRate) {
		rate = g_config.maxRate;
	}
	std::int64_t step = static_cast<std::int64_t>(animationSpeed) * rate / 100;
	if (step < 1) {
		step = 1; // an animation speed of 0 would divide by zero, as it does in the game
	}
	std::int64_t frames = 256LL * attackFrame * g_config.intervalStretch / (100LL * step);
	if (frames < g_config.minFrames) {
		frames = g_config.minFrames;
	}
	if (frames > INT32_MAX) {
		frames = INT32_MAX;
	}
	g_hitIntervals.fetch_add(1, std::memory_order_relaxed);
	g_lastAttackRate.store(attackRate, std::memory_order_relaxed);
	g_lastIasBonus.store(iasBonus, std::memory_order_relaxed);
	g_lastRate.store(static_cast<std::int32_t>(rate), std::memory_order_relaxed);
	g_lastFrames.store(static_cast<std::int32_t>(frames), std::memory_order_relaxed);
	return static_cast<std::int32_t>(frames);
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

constexpr std::uint8_t CtcGateTarget[] = {0x0F, 0x85, 0xC1, 0x00, 0x00, 0x00};
constexpr std::uint8_t CtcGatePosition[] = {0x0F, 0x85, 0xA4, 0x00, 0x00, 0x00};
constexpr std::uint8_t MeleeBranchServer[] = {0x74, 0x1E};
constexpr std::uint8_t MeleeBranchClient[] = {0x74, 0x10};
constexpr std::uint8_t MeleeSkipServer[] = {0xEB, 0x1E};
constexpr std::uint8_t MeleeSkipClient[] = {0xEB, 0x10};
constexpr std::uint8_t DualWieldBackEdge[] = {0x0F, 0x8C, 0x63, 0xFD, 0xFF, 0xFF};

struct CallSite {
	std::uint64_t      rva;
	std::array<std::uint8_t, 5> original;
	std::size_t        relay;
};

constexpr CallSite SiteServerPathStatus {0x42A99F, {0xE8, 0x1C, 0x71, 0x06, 0x00}, RelayServerPathStatus};
constexpr CallSite SiteClientPathStep   {0x0FFCFD, {0xE8, 0xFE, 0x9C, 0xFF, 0xFF}, RelayClientPathStep};
constexpr CallSite SiteProcStart        {0x23123A, {0xE8, 0x01, 0x64, 0xFE, 0xFF}, RelayProcStart};
constexpr CallSite SiteModeCheck        {0x2184FE, {0xE8, 0xAD, 0x14, 0xEE, 0xFF}, RelayModeCheck};
constexpr CallSite SiteSetNeutral       {0x218586, {0xE8, 0xD5, 0x12, 0xEE, 0xFF}, RelaySetNeutral};
constexpr CallSite SiteProcTail         {0x231334, {0xE8, 0x57, 0xD4, 0x11, 0x00}, RelayProcTail};
constexpr CallSite SiteDispatchState    {0x4F306E, {0xE8, 0x3D, 0x21, 0xE4, 0xFF}, RelayDispatchState};
constexpr CallSite SiteUseItemState     {0x4F4140, {0xE8, 0x6B, 0x10, 0xE4, 0xFF}, RelayUseItemState};
constexpr CallSite SiteItemLockState    {0x1C738C, {0xE8, 0x1F, 0xDE, 0x16, 0x00}, RelayItemLockState};
constexpr CallSite SiteClickGate        {0x0FE689, {0xE8, 0x32, 0xC2, 0xF9, 0xFF}, RelayClickGate};
constexpr CallSite SiteControllerUpdate {0x14B89D, {0xE8, 0x9E, 0x0C, 0x00, 0x00}, RelayControllerUpdate};

auto PrepareRelayPage(const D2RL::PluginContext* context) noexcept -> bool {
	const auto base = static_cast<std::uintptr_t>(context->exeBase);
	g_relayPage     = AllocateNear(base + 0x300000, RelayPageBytes);
	if (g_relayPage == nullptr) {
		context->LogError("No relay page could be allocated within rel32 reach of the game image.");
		return false;
	}
	std::memset(g_relayPage, 0xCC, RelayPageBytes);
	WriteAbsoluteJump(g_relayPage + RelayServerPathStatus, reinterpret_cast<std::uint64_t>(&OnServerPathStatus));
	WriteAbsoluteJump(g_relayPage + RelayClientPathStep, reinterpret_cast<std::uint64_t>(&OnClientPathStep));
	WriteAbsoluteJump(g_relayPage + RelayProcStart, reinterpret_cast<std::uint64_t>(&OnProcStart));
	WriteAbsoluteJump(g_relayPage + RelayModeCheck, reinterpret_cast<std::uint64_t>(&OnProcModeCheck));
	WriteAbsoluteJump(g_relayPage + RelaySetNeutral, reinterpret_cast<std::uint64_t>(&OnProcSetNeutral));
	WriteAbsoluteJump(g_relayPage + RelayProcTail, reinterpret_cast<std::uint64_t>(&OnProcTail));
	WriteAbsoluteJump(g_relayPage + RelayDispatchState, reinterpret_cast<std::uint64_t>(&OnUseItemStateCheck));
	WriteAbsoluteJump(g_relayPage + RelayUseItemState, reinterpret_cast<std::uint64_t>(&OnUseItemStateCheck));
	WriteAbsoluteJump(g_relayPage + RelayItemLockState, reinterpret_cast<std::uint64_t>(&OnItemUseLockStateCheck));
	WriteAbsoluteJump(g_relayPage + RelayClickGate, reinterpret_cast<std::uint64_t>(&OnClickGate));
	WriteAbsoluteJump(g_relayPage + RelayControllerUpdate, reinterpret_cast<std::uint64_t>(&OnControllerPlayerUpdate));
	// Dual wield: entered from the loop back-edge 005694B7 with rdi = &token+30h
	// (reloaded at 005694B2). dec dword [rdi] turns "search after the GUID just
	// struck" into "search from that GUID", then resume the loop at 00569220.
	// Sequence fallback: entered from the player path of the sequence resolver
	// 003CB890 at 003CB987, where rax = weapon-class slot and r14 = the
	// sequence's slot array (24-byte slots, rows pointer first). Replays the two
	// displaced LEAs, then swaps an empty slot (NULL rows) for slot 0 before
	// resuming at 003CB98F. Slot 0 is never empty: the resolver has already
	// read its first row at 003CB903.
	static constexpr std::uint8_t SequenceFallbackStub[] {
		0x48, 0x8D, 0x0C, 0x40, // lea  rcx, [rax+rax*2]
		0x49, 0x8D, 0x3C, 0xCE, // lea  rdi, [r14+rcx*8]
		0x48, 0x83, 0x3F, 0x00, // cmp  qword ptr [rdi], 0
		0x49, 0x0F, 0x44, 0xFE, // cmove rdi, r14
	};
	std::memcpy(g_relayPage + RelaySequenceFallback, SequenceFallbackStub, sizeof(SequenceFallbackStub));
	WriteAbsoluteJump(g_relayPage + RelaySequenceFallback + sizeof(SequenceFallbackStub), base + 0x3CB98F);
	g_relayPage[RelayDualWield]     = 0xFF;
	g_relayPage[RelayDualWield + 1] = 0x0F;
	WriteAbsoluteJump(g_relayPage + RelayDualWield + 2, base + 0x569220);
	// Whirlwind hit interval: entered from 0056AD23 with rbp = unit, edi = the
	// attack rate stat, eax = the diminished IAS, [rsp+60h] = animation speed and
	// [rsp+64h] = attack frame, rsp 16-byte aligned. Calls WhirlwindHitInterval
	// (unit, attack rate, diminished IAS, animation speed, attack frame) and
	// leaves through the helper's epilogue at 0056AD6B with the frames in eax.
	// Assembled with keystone and checked by emulating hook, relay and return.
	static constexpr std::uint8_t HitIntervalStub[] {
		0x44, 0x8B, 0x4C, 0x24, 0x60,                         // mov  r9d, [rsp+60h]   animation speed
		0x44, 0x8B, 0x54, 0x24, 0x64,                         // mov  r10d, [rsp+64h]  attack frame
		0x41, 0x89, 0xC0,                                     // mov  r8d, eax         diminished IAS
		0x89, 0xFA,                                           // mov  edx, edi         attack rate
		0x48, 0x89, 0xE9,                                     // mov  rcx, rbp         unit
		0x48, 0x83, 0xEC, 0x30,                               // sub  rsp, 30h
		0x44, 0x89, 0x54, 0x24, 0x20,                         // mov  [rsp+20h], r10d  fifth argument
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, WhirlwindHitInterval
		0xFF, 0xD0,                                           // call rax
		0x48, 0x83, 0xC4, 0x30,                               // add  rsp, 30h
	};
	constexpr std::size_t HitIntervalStubTarget = 29;   // the mov rax immediate
	static_assert(sizeof(HitIntervalStub) + 14 <= 0x40, "the hit interval relay must fit its slot");
	std::memcpy(g_relayPage + RelayHitInterval, HitIntervalStub, sizeof(HitIntervalStub));
	const auto hitInterval = reinterpret_cast<std::uint64_t>(&WhirlwindHitInterval);
	std::memcpy(g_relayPage + RelayHitInterval + HitIntervalStubTarget, &hitInterval, sizeof(hitInterval));
	WriteAbsoluteJump(g_relayPage + RelayHitInterval + sizeof(HitIntervalStub), base + 0x56AD6B);
	DWORD previous = 0;
	if (!VirtualProtect(g_relayPage, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) {
		VirtualFree(g_relayPage, 0, MEM_RELEASE);
		g_relayPage = nullptr;
		context->LogError("The relay page could not be made executable.");
		return false;
	}
	FlushInstructionCache(GetCurrentProcess(), g_relayPage, RelayPageBytes);
	return true;
}

auto RedirectCall(const D2RL::PluginContext* context, const CallSite& site) noexcept -> bool {
	const auto base   = static_cast<std::uintptr_t>(context->exeBase);
	const auto next   = base + site.rva + 5;
	const auto target = reinterpret_cast<std::uintptr_t>(g_relayPage) + site.relay;
	if (!WithinRel32(next, target)) {
		return false;
	}
	const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next));
	std::array<std::uint8_t, 5> bytes {0xE8};
	std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
	return context->PatchBytes(site.rva, site.original.data(), 5, bytes.data(), 5);
}

void InstallCtc(const D2RL::PluginContext* context) noexcept {
	if (!g_config.ctcEnabled) {
		SetPart(Part::CtcGates, PartState::DisabledByConfig);
		SetPart(Part::CtcPolicy, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, CtcWitnesses, PartNames[static_cast<std::size_t>(Part::CtcGates)])) {
		SetPart(Part::CtcGates, PartState::UnsupportedBuild);
		SetPart(Part::CtcPolicy, PartState::UnsupportedBuild);
		return;
	}
	const bool policyNeeded = g_ctcAllowListActive || g_config.ctcChancePercent < 100;
	if (policyNeeded) {
		// Hooks first: if they cannot go in, the gates stay closed rather than
		// open more widely than the config asks for.
		if (!context->InstallInlineHook(0x5896E0, P_CastOnTarget, sizeof(P_CastOnTarget), reinterpret_cast<void*>(&OnCastOnTarget), reinterpret_cast<void**>(&g_originalCastOnTarget))
			|| !context->InstallInlineHook(0x589820, P_CastAtPos, sizeof(P_CastAtPos), reinterpret_cast<void*>(&OnCastAtPosition), reinterpret_cast<void**>(&g_originalCastAtPosition))) {
			SetPart(Part::CtcPolicy, PartState::InstallFailed);
			SetPart(Part::CtcGates, PartState::InstallFailed);
			context->LogError("CTC while Uninterruptible NOT installed: the policy hooks failed, so the gates were left closed.");
			return;
		}
		SetPart(Part::CtcPolicy, PartState::Installed);
	} else {
		SetPart(Part::CtcPolicy, PartState::DisabledByConfig);
	}
	const bool gates = context->PatchNop(0x589736, CtcGateTarget, sizeof(CtcGateTarget), sizeof(CtcGateTarget))
		&& context->PatchNop(0x58986B, CtcGatePosition, sizeof(CtcGatePosition), sizeof(CtcGatePosition));
	SetPart(Part::CtcGates, gates ? PartState::Installed : PartState::InstallFailed);
}

void InstallProcFix(const D2RL::PluginContext* context) noexcept {
	if (!g_config.procStartFix) {
		SetPart(Part::ProcStartFix, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, ProcFixWitnesses, PartNames[static_cast<std::size_t>(Part::ProcStartFix)])) {
		SetPart(Part::ProcStartFix, PartState::UnsupportedBuild);
		return;
	}
	// Order: the scope closer and the two in-scope refusals first, the scope
	// opener last, so a partial install can never open a scope nobody closes.
	const bool ok = RedirectCall(context, SiteProcTail) && RedirectCall(context, SiteModeCheck)
		&& RedirectCall(context, SiteSetNeutral) && RedirectCall(context, SiteProcStart);
	SetPart(Part::ProcStartFix, ok ? PartState::Installed : PartState::InstallFailed);
}

void InstallDualWield(const D2RL::PluginContext* context) noexcept {
	if (!g_config.dualWieldSameTarget) {
		SetPart(Part::DualWield, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, DualWieldWitnesses, PartNames[static_cast<std::size_t>(Part::DualWield)])) {
		SetPart(Part::DualWield, PartState::UnsupportedBuild);
		return;
	}
	const auto base   = static_cast<std::uintptr_t>(context->exeBase);
	const auto next   = base + 0x5694B7 + 6;
	const auto target = reinterpret_cast<std::uintptr_t>(g_relayPage) + RelayDualWield;
	if (!WithinRel32(next, target)) {
		SetPart(Part::DualWield, PartState::InstallFailed);
		return;
	}
	const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next));
	std::array<std::uint8_t, 6> bytes {0x0F, 0x8C};
	std::memcpy(bytes.data() + 2, &displacement, sizeof(displacement));
	const bool ok = context->PatchBytes(0x5694B7, DualWieldBackEdge, sizeof(DualWieldBackEdge), bytes.data(), 6);
	SetPart(Part::DualWield, ok ? PartState::Installed : PartState::InstallFailed);
}

// The redirect at 0042A99F serves the cyclone, the death fix and the pet
// trail, and is installed once for whichever of them asks first.
auto InstallServerFrameRedirect(const D2RL::PluginContext* context) noexcept -> PartState {
	static PartState state = PartState::NotAttempted;
	if (state != PartState::NotAttempted) {
		return state;
	}
	if (!VerifyAll(context, ServerFrameWitnesses, "Whirlwind frame redirect")) {
		state = PartState::UnsupportedBuild;
	} else {
		state = RedirectCall(context, SiteServerPathStatus) ? PartState::Installed : PartState::InstallFailed;
	}
	return state;
}

void InstallDeathFix(const D2RL::PluginContext* context) noexcept {
	if (!g_config.deathWhileWhirlwinding) {
		SetPart(Part::DeathWhileWhirlwinding, PartState::DisabledByConfig);
		return;
	}
	const PartState frame = InstallServerFrameRedirect(context);
	g_deathFixActive      = frame == PartState::Installed;
	SetPart(Part::DeathWhileWhirlwinding, frame);
}

void InstallPetTrail(const D2RL::PluginContext* context) noexcept {
	if (!g_config.petsFollowWhirlwind) {
		SetPart(Part::PetsFollowWhirlwind, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, PetTrailWitnesses, PartNames[static_cast<std::size_t>(Part::PetsFollowWhirlwind)])) {
		SetPart(Part::PetsFollowWhirlwind, PartState::UnsupportedBuild);
		return;
	}
	const PartState frame = InstallServerFrameRedirect(context);
	g_petTrailActive      = frame == PartState::Installed;
	SetPart(Part::PetsFollowWhirlwind, frame);
}

// The dispatcher hook at 004F2FA0 and the Uninterruptible-check redirect at
// 004F306E serve the potion and the gold parts, and are installed once for
// whichever of them asks first. The redirect behaves exactly like the native
// call until a hook opens a scope, so it goes in first.
auto InstallServerMessageGate(const D2RL::PluginContext* context) noexcept -> PartState {
	static PartState state = PartState::NotAttempted;
	if (state != PartState::NotAttempted) {
		return state;
	}
	if (!VerifyAll(context, ServerMessageGateWitnesses, "Server message gate")) {
		state = PartState::UnsupportedBuild;
	} else {
		const bool ok = RedirectCall(context, SiteDispatchState)
			&& context->InstallInlineHook(0x4F2FA0, P_Dispatcher, sizeof(P_Dispatcher), reinterpret_cast<void*>(&OnPacketDispatch), reinterpret_cast<void**>(&g_originalPacketDispatch));
		state = ok ? PartState::Installed : PartState::InstallFailed;
	}
	return state;
}

// "potion-scope" service, for Alchemy (celestialrayone.alchemy): Alchemy runs
// Bottomless Vials and Savvy Elixirs before this plugin's use-item hook and
// copies the handler's Uninterruptible refusal; this tells it whether the
// potion part has opened its scope for that player, so a kept potion is
// allowed while whirlwinding exactly like a used one.
struct PotionScopeService {
	std::uint32_t serviceSize;
	std::uint32_t serviceVersion;
	bool(__cdecl* potionUseOpen)(void* player) noexcept;
};

bool __cdecl PotionUseOpen(void* player) noexcept {
	return player != nullptr && g_potionServerActive && g_potionPlayer == player;
}

constexpr PotionScopeService PotionScopeTable {
	.serviceSize    = sizeof(PotionScopeService),
	.serviceVersion = 1,
	.potionUseOpen  = &PotionUseOpen,
};

void PublishPotionScope(const D2RL::PluginContext* context) noexcept {
	const D2RL::PluginCommunicationService* communication = nullptr;
	if (context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
	    || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
		return;
	}
	const D2RL::PluginCommunication::PublishServiceRequest request {
		.structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
		.flags          = 0,
		.name           = "potion-scope",
		.serviceVersion = 1,
		.tableSize      = static_cast<std::uint32_t>(sizeof(PotionScopeTable)),
		.table          = &PotionScopeTable,
	};
	if (communication->publishService(context, &request) != D2RL::PluginCommunication::Result::Success) {
		context->LogWarn("The potion-scope service was refused; Alchemy's kept potions are not allowed while whirlwinding.");
	}
}

void InstallPotions(const D2RL::PluginContext* context) noexcept {
	if (!g_config.potionsWhileWhirlwinding) {
		SetPart(Part::PotionsWhileWhirlwinding, PartState::DisabledByConfig);
		SetPart(Part::PotionsWhileWhirlwindingClient, PartState::DisabledByConfig);
		return;
	}
	// In every half the redirected checks behave exactly like the native call
	// until a hook opens a potion scope, so they go in first.
	if (!VerifyAll(context, PotionWitnesses, PartNames[static_cast<std::size_t>(Part::PotionsWhileWhirlwinding)])) {
		SetPart(Part::PotionsWhileWhirlwinding, PartState::UnsupportedBuild);
	} else {
		const bool ok = RedirectCall(context, SiteUseItemState)
			&& context->InstallInlineHook(0x4F40C0, P_UseItem, sizeof(P_UseItem), reinterpret_cast<void*>(&OnUseItem), reinterpret_cast<void**>(&g_originalUseItem))
			&& InstallServerMessageGate(context) == PartState::Installed;
		g_potionServerActive = ok;
		SetPart(Part::PotionsWhileWhirlwinding, ok ? PartState::Installed : PartState::InstallFailed);
	}
	if (!VerifyAll(context, PotionClientWitnesses, PartNames[static_cast<std::size_t>(Part::PotionsWhileWhirlwindingClient)])) {
		SetPart(Part::PotionsWhileWhirlwindingClient, PartState::UnsupportedBuild);
	} else {
		const bool ok = RedirectCall(context, SiteItemLockState)
			&& context->InstallInlineHook(0x1C7360, P_ItemUseLock, sizeof(P_ItemUseLock), reinterpret_cast<void*>(&OnItemUseLock), reinterpret_cast<void**>(&g_originalItemUseLock));
		SetPart(Part::PotionsWhileWhirlwindingClient, ok ? PartState::Installed : PartState::InstallFailed);
	}
}

void InstallSequenceFallback(const D2RL::PluginContext* context) noexcept {
	if (!g_config.whirlwindWithBows) {
		SetPart(Part::WhirlwindWithBows, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, SequenceWitnesses, PartNames[static_cast<std::size_t>(Part::WhirlwindWithBows)])) {
		SetPart(Part::WhirlwindWithBows, PartState::UnsupportedBuild);
		return;
	}
	static constexpr std::uint8_t SlotSelect[] {0x48, 0x8D, 0x0C, 0x40, 0x49, 0x8D, 0x3C, 0xCE};
	const auto base   = static_cast<std::uintptr_t>(context->exeBase);
	const auto next   = base + 0x3CB987 + 5;
	const auto target = reinterpret_cast<std::uintptr_t>(g_relayPage) + RelaySequenceFallback;
	if (!WithinRel32(next, target)) {
		SetPart(Part::WhirlwindWithBows, PartState::InstallFailed);
		return;
	}
	const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next));
	std::array<std::uint8_t, 8> bytes {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
	std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
	const bool ok = context->PatchBytes(0x3CB987, SlotSelect, sizeof(SlotSelect), bytes.data(), static_cast<std::uint32_t>(bytes.size()));
	SetPart(Part::WhirlwindWithBows, ok ? PartState::Installed : PartState::InstallFailed);
}

// Server: the shared message gate. Client: the redirect of the click gate at
// 000FE689, which behaves exactly like the native call unless Auto Gold's own
// click is refused during a Whirlwind.
void InstallGold(const D2RL::PluginContext* context) noexcept {
	if (!g_config.goldWhileWhirlwinding) {
		SetPart(Part::GoldWhileWhirlwindingServer, PartState::DisabledByConfig);
		SetPart(Part::GoldWhileWhirlwindingClient, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, GoldServerWitnesses, PartNames[static_cast<std::size_t>(Part::GoldWhileWhirlwindingServer)])) {
		SetPart(Part::GoldWhileWhirlwindingServer, PartState::UnsupportedBuild);
	} else {
		const PartState gate = InstallServerMessageGate(context);
		g_goldServerActive   = gate == PartState::Installed;
		SetPart(Part::GoldWhileWhirlwindingServer, gate);
	}
	if (!VerifyAll(context, GoldClientWitnesses, PartNames[static_cast<std::size_t>(Part::GoldWhileWhirlwindingClient)])) {
		SetPart(Part::GoldWhileWhirlwindingClient, PartState::UnsupportedBuild);
	} else {
		const bool ok      = RedirectCall(context, SiteClickGate);
		g_goldClientActive = ok;
		SetPart(Part::GoldWhileWhirlwindingClient, ok ? PartState::Installed : PartState::InstallFailed);
	}
}

// No relay needed: an entry hook through the loader.
void InstallSpeedSync(const D2RL::PluginContext* context) noexcept {
	if (!g_config.whirlwindSpeedSync) {
		SetPart(Part::WhirlwindSpeedSync, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, SpeedSyncWitnesses, PartNames[static_cast<std::size_t>(Part::WhirlwindSpeedSync)])) {
		SetPart(Part::WhirlwindSpeedSync, PartState::UnsupportedBuild);
		return;
	}
	const bool ok = context->InstallInlineHook(0x56A420, P_WhirlwindStart, sizeof(P_WhirlwindStart), reinterpret_cast<void*>(&OnServerWhirlwindStart), reinterpret_cast<void**>(&g_originalWhirlwindStart));
	SetPart(Part::WhirlwindSpeedSync, ok ? PartState::Installed : PartState::InstallFailed);
}

// The five bytes at 0056AD23 (mov rcx,rbp / add edi,eax) become a jump to the
// relay. Everything from there to the epilogue (the vanilla clamp and division)
// is skipped while the part is on.
constexpr std::uint8_t HitIntervalSite[] = {0x48, 0x8B, 0xCD, 0x03, 0xF8};

void InstallHitInterval(const D2RL::PluginContext* context) noexcept {
	if (!g_config.hitIntervalEnabled) {
		SetPart(Part::WhirlwindHitInterval, PartState::DisabledByConfig);
		return;
	}
	if (!VerifyAll(context, HitIntervalWitnesses, PartNames[static_cast<std::size_t>(Part::WhirlwindHitInterval)])) {
		SetPart(Part::WhirlwindHitInterval, PartState::UnsupportedBuild);
		return;
	}
	const auto base   = static_cast<std::uintptr_t>(context->exeBase);
	const auto next   = base + 0x56AD23 + 5;
	const auto target = reinterpret_cast<std::uintptr_t>(g_relayPage) + RelayHitInterval;
	if (!WithinRel32(next, target)) {
		SetPart(Part::WhirlwindHitInterval, PartState::InstallFailed);
		return;
	}
	const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next));
	std::array<std::uint8_t, 5> bytes {0xE9};
	std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
	const bool ok = context->PatchBytes(0x56AD23, HitIntervalSite, sizeof(HitIntervalSite), bytes.data(), 5);
	SetPart(Part::WhirlwindHitInterval, ok ? PartState::Installed : PartState::InstallFailed);
}

void InstallCyclone(const D2RL::PluginContext* context) noexcept {

	if (!g_config.cycloneEnabled) {
		SetPart(Part::CycloneServer, PartState::DisabledByConfig);
		SetPart(Part::CycloneClient, PartState::DisabledByConfig);
		SetPart(Part::CycloneController, PartState::DisabledByConfig);
		SetPart(Part::MeleeSpinServer, PartState::DisabledByConfig);
		SetPart(Part::MeleeSpinClient, PartState::DisabledByConfig);
		return;
	}
	if (VerifyAll(context, CycloneServerWitnesses, PartNames[static_cast<std::size_t>(Part::CycloneServer)])) {
		const bool ok = InstallServerFrameRedirect(context) == PartState::Installed
			&& context->InstallInlineHook(0x4FDB40, P_Executor, sizeof(P_Executor), reinterpret_cast<void*>(&OnPositionExecutor), reinterpret_cast<void**>(&g_originalPositionExecutor));
		g_cycloneServerActive = ok;
		SetPart(Part::CycloneServer, ok ? PartState::Installed : PartState::InstallFailed);
		if (!g_config.spinAtMeleeTargets) {
			SetPart(Part::MeleeSpinServer, PartState::DisabledByConfig);
		} else {
			const bool spin = context->PatchBytes(0x56A4E1, MeleeBranchServer, 2, MeleeSkipServer, 2);
			SetPart(Part::MeleeSpinServer, spin ? PartState::Installed : PartState::InstallFailed);
		}
	} else {
		SetPart(Part::CycloneServer, PartState::UnsupportedBuild);
		SetPart(Part::MeleeSpinServer, PartState::UnsupportedBuild);
	}
	if (VerifyAll(context, CycloneClientWitnesses, PartNames[static_cast<std::size_t>(Part::CycloneClient)])) {
		const bool ok = RedirectCall(context, SiteClientPathStep)
			&& context->InstallInlineHook(0x101FA0, P_HeldRight, sizeof(P_HeldRight), reinterpret_cast<void*>(&OnHeldSkillCommand), reinterpret_cast<void**>(&g_originalHeldSkill))
			&& context->InstallInlineHook(0x1013B0, P_HeldLeft, sizeof(P_HeldLeft), reinterpret_cast<void*>(&OnHeldLeftCommand), reinterpret_cast<void**>(&g_originalHeldLeft));
		SetPart(Part::CycloneClient, ok ? PartState::Installed : PartState::InstallFailed);
		if (!g_config.spinAtMeleeTargets) {
			SetPart(Part::MeleeSpinClient, PartState::DisabledByConfig);
		} else {
			const bool spin = context->PatchBytes(0x230B20, MeleeBranchClient, 2, MeleeSkipClient, 2);
			SetPart(Part::MeleeSpinClient, spin ? PartState::Installed : PartState::InstallFailed);
		}
	} else {
		SetPart(Part::CycloneClient, PartState::UnsupportedBuild);
		SetPart(Part::MeleeSpinClient, PartState::UnsupportedBuild);
	}

	// Controller steering rides on the client part: it shares its session,
	// retarget and aim message.
	if (!g_config.cycloneEnabled) {
		return;
	}
	if (!PartIs(Part::CycloneClient, PartState::Installed)) {
		SetPart(Part::CycloneController, PartState::InstallFailed);
	} else if (!VerifyAll(context, CycloneControllerWitnesses, PartNames[static_cast<std::size_t>(Part::CycloneController)])) {
		SetPart(Part::CycloneController, PartState::UnsupportedBuild);
	} else {
		const bool ok             = RedirectCall(context, SiteControllerUpdate);
		g_cycloneControllerActive = ok;
		SetPart(Part::CycloneController, ok ? PartState::Installed : PartState::InstallFailed);
	}
}

auto StateText(PartState state) noexcept -> const char* {
	switch (state) {
	case PartState::Installed: return "active";
	case PartState::DisabledByConfig: return "off in config";
	case PartState::UnsupportedBuild: return "NOT installed: game bytes do not match 3.3.93847 under D2RLoader 1.3.0";
	case PartState::InstallFailed: return "NOT installed: install failed (see log)";
	default: return "not attempted";
	}
}

auto WhirlwindCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	const D2RL::PluginContext* context = command != nullptr && command->plugin != nullptr ? command->plugin : g_context;
	if (context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[256];
	for (std::size_t i = 0; i < g_parts.size(); ++i) {
		std::snprintf(line, sizeof(line), "%s: %s", PartNames[i], StateText(g_parts[i]));
		context->WriteConsoleMessage(line);
	}
	std::snprintf(line, sizeof(line), "Pet trail points written during Whirlwind: %llu",
		static_cast<unsigned long long>(g_petTrailWrites.load(std::memory_order_relaxed)));
	context->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "Controller Whirlwind aims sent: %llu",
		static_cast<unsigned long long>(g_controllerAims.load(std::memory_order_relaxed)));
	context->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "Cyclone starting headings: %llu from the cast direction, %llu from the facing",
		static_cast<unsigned long long>(g_headingsFromCast.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(g_headingsFromFacing.load(std::memory_order_relaxed)));
	context->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "Gold pickups during Whirlwind: %llu sent (client), %llu let through (server)",
		static_cast<unsigned long long>(g_goldClientSends.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(g_goldServerPasses.load(std::memory_order_relaxed)));
	context->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "Whirlwind hit intervals worked out: %llu (last: attack rate %d, diminished IAS %d, rate %d, %d frames)",
		static_cast<unsigned long long>(g_hitIntervals.load(std::memory_order_relaxed)),
		g_lastAttackRate.load(std::memory_order_relaxed), g_lastIasBonus.load(std::memory_order_relaxed),
		g_lastRate.load(std::memory_order_relaxed), g_lastFrames.load(std::memory_order_relaxed));
	context->WriteConsoleMessage(line);
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo WhirlwindInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.whirlwind",
	.name        = "Whirlwind",
	.version     = "0.9.1",
	.author      = "CelestialRayOne",
	.description = "Whirlwind rework: procs while Uninterruptible, proc start fix, dual wield same target, cyclone steering (cursor and controller stick), death and potions while Whirlwinding, Whirlwind with bows, movement speed sync, pets follow during Whirlwind, Auto Gold Pickup during Whirlwind, optional Whirlwind hit interval formula.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(WhirlwindInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasFlag(WhirlwindInfo.flags, D2RL::PluginFlags::NativeHooks), "Inline hooks require the NativeHooks flag.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &WhirlwindInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr || context->exeBase == 0) {
		return false;
	}
	g_context = context;
	if (!context->RegisterConsoleCommand("whirlwind", WhirlwindCommand, "Report which Whirlwind parts are active.")) {
		context->LogWarn("The whirlwind console command was not registered.");
	}
	LoadConfiguration(context);
	g_rng ^= static_cast<std::uint32_t>(GetTickCount64());
	if (g_rng == 0) {
		g_rng = 0x9E3779B9U;
	}
	if (!g_config.enabled) {
		g_parts.fill(PartState::DisabledByConfig);
		context->LogWarn("whirlwind is disabled in its config file. Nothing was installed.");
		return true;
	}
	BindNatives(static_cast<std::uintptr_t>(context->exeBase));
	const bool relays = PrepareRelayPage(context);
	InstallCtc(context);
	InstallSpeedSync(context);
	if (relays) {
		InstallProcFix(context);
		InstallDualWield(context);
		InstallCyclone(context);
		InstallDeathFix(context);
		InstallPotions(context);
		if (g_potionServerActive) {
			PublishPotionScope(context);
		}
		InstallSequenceFallback(context);
		InstallPetTrail(context);
		InstallGold(context);
		InstallHitInterval(context);
	} else {
		SetPart(Part::ProcStartFix, PartState::InstallFailed);
		SetPart(Part::DualWield, PartState::InstallFailed);
		SetPart(Part::CycloneServer, PartState::InstallFailed);
		SetPart(Part::CycloneClient, PartState::InstallFailed);
		SetPart(Part::CycloneController, PartState::InstallFailed);
		SetPart(Part::MeleeSpinServer, PartState::InstallFailed);
		SetPart(Part::MeleeSpinClient, PartState::InstallFailed);
		SetPart(Part::DeathWhileWhirlwinding, PartState::InstallFailed);
		SetPart(Part::PotionsWhileWhirlwinding, PartState::InstallFailed);
		SetPart(Part::PotionsWhileWhirlwindingClient, PartState::InstallFailed);
		SetPart(Part::WhirlwindWithBows, PartState::InstallFailed);
		SetPart(Part::PetsFollowWhirlwind, PartState::InstallFailed);
		SetPart(Part::GoldWhileWhirlwindingServer, PartState::InstallFailed);
		SetPart(Part::GoldWhileWhirlwindingClient, PartState::InstallFailed);
		SetPart(Part::WhirlwindHitInterval, g_config.hitIntervalEnabled ? PartState::InstallFailed : PartState::DisabledByConfig);
	}
	for (std::size_t i = 0; i < g_parts.size(); ++i) {
		D2RL::LogInfoF(context, "%s: %s", PartNames[i], StateText(g_parts[i]));
	}
	return true;
}

// Inline hooks cannot be withdrawn and their trampolines live in this module,
// so the DLL stays resident for the life of the process.
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
