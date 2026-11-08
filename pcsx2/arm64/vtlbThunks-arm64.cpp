// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Call targets for the vtlb dispatchers, as the arm64 recompilers emit them.
//
// The recompilers keep guest values in x9-x15 across a vtlb slow-path call and
// rely on the callee preserving them: the dispatchers are declared
// __attribute__((preserve_most)) (vtlb.h). Compilers whose AArch64 backend cannot
// build that convention (LLVM < 18, e.g. the HarmonyOS NDK's clang 15) get plain
// AAPCS dispatchers instead, and the recompilers call them through the thunks
// below, which save and restore x9-x15 themselves. Arguments (x0/x1/q0) and
// results (x0/q0) pass through untouched, so the contract seen by the emitted
// code is the same either way.

#include "Memory.h"
#include "vtlb.h"

#include "common/Assertions.h"

#if defined(VTLB_PRESERVE_MOST_VIA_THUNKS)

// The real dispatchers, read by the thunks through x16 (IP0, free to clobber at a
// call). Variables rather than direct branches: the targets are C++ template
// instances, whose mangled names should not be spelled out in assembly.
#define VTLB_THUNK_TARGET(name, fn) \
	extern "C" __attribute__((visibility("hidden"), used)) const void* name = reinterpret_cast<const void*>(fn)

VTLB_THUNK_TARGET(armsx2_vtlb_target_read8, &vtlb_memRead<mem8_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_read16, &vtlb_memRead<mem16_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_read32, &vtlb_memRead<mem32_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_read64, &vtlb_memRead<mem64_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_read128, &vtlb_memRead128);
VTLB_THUNK_TARGET(armsx2_vtlb_target_write8, &vtlb_memWrite<mem8_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_write16, &vtlb_memWrite<mem16_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_write32, &vtlb_memWrite<mem32_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_write64, &vtlb_memWrite<mem64_t>);
VTLB_THUNK_TARGET(armsx2_vtlb_target_write128, &vtlb_memWrite128);

#undef VTLB_THUNK_TARGET

// x29/x30 plus x9-x15, in an 80-byte (16-aligned) frame. "hint #34" is BTI C, a
// no-op on cores without BTI, so the thunks are valid indirect-branch targets
// either way.
#define VTLB_THUNK(name, target) \
	".globl " #name "\n" \
	".hidden " #name "\n" \
	".type " #name ", %function\n" \
	".balign 16\n" \
	#name ":\n" \
	"	hint #34\n" \
	"	stp x29, x30, [sp, #-80]!\n" \
	"	mov x29, sp\n" \
	"	stp x9, x10, [sp, #16]\n" \
	"	stp x11, x12, [sp, #32]\n" \
	"	stp x13, x14, [sp, #48]\n" \
	"	str x15, [sp, #64]\n" \
	"	adrp x16, " #target "\n" \
	"	ldr x16, [x16, :lo12:" #target "]\n" \
	"	blr x16\n" \
	"	ldp x9, x10, [sp, #16]\n" \
	"	ldp x11, x12, [sp, #32]\n" \
	"	ldp x13, x14, [sp, #48]\n" \
	"	ldr x15, [sp, #64]\n" \
	"	ldp x29, x30, [sp], #80\n" \
	"	ret\n" \
	".size " #name ", . - " #name "\n"

__asm__(
	".text\n"
	VTLB_THUNK(armsx2_vtlb_thunk_read8, armsx2_vtlb_target_read8)
	VTLB_THUNK(armsx2_vtlb_thunk_read16, armsx2_vtlb_target_read16)
	VTLB_THUNK(armsx2_vtlb_thunk_read32, armsx2_vtlb_target_read32)
	VTLB_THUNK(armsx2_vtlb_thunk_read64, armsx2_vtlb_target_read64)
	VTLB_THUNK(armsx2_vtlb_thunk_read128, armsx2_vtlb_target_read128)
	VTLB_THUNK(armsx2_vtlb_thunk_write8, armsx2_vtlb_target_write8)
	VTLB_THUNK(armsx2_vtlb_thunk_write16, armsx2_vtlb_target_write16)
	VTLB_THUNK(armsx2_vtlb_thunk_write32, armsx2_vtlb_target_write32)
	VTLB_THUNK(armsx2_vtlb_thunk_write64, armsx2_vtlb_target_write64)
	VTLB_THUNK(armsx2_vtlb_thunk_write128, armsx2_vtlb_target_write128)
);

#undef VTLB_THUNK

extern "C" {
void armsx2_vtlb_thunk_read8();
void armsx2_vtlb_thunk_read16();
void armsx2_vtlb_thunk_read32();
void armsx2_vtlb_thunk_read64();
void armsx2_vtlb_thunk_read128();
void armsx2_vtlb_thunk_write8();
void armsx2_vtlb_thunk_write16();
void armsx2_vtlb_thunk_write32();
void armsx2_vtlb_thunk_write64();
void armsx2_vtlb_thunk_write128();
}

const void* vtlb_JitMemRead(u32 bits)
{
	switch (bits)
	{
		case 8: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_read8);
		case 16: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_read16);
		case 32: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_read32);
		case 64: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_read64);
		case 128: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_read128);
		default: pxFailRel("Bad vtlb read size"); return nullptr;
	}
}

const void* vtlb_JitMemWrite(u32 bits)
{
	switch (bits)
	{
		case 8: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_write8);
		case 16: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_write16);
		case 32: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_write32);
		case 64: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_write64);
		case 128: return reinterpret_cast<const void*>(&armsx2_vtlb_thunk_write128);
		default: pxFailRel("Bad vtlb write size"); return nullptr;
	}
}

#else // The dispatchers are preserve_most themselves: call them directly.

const void* vtlb_JitMemRead(u32 bits)
{
	switch (bits)
	{
		case 8: return reinterpret_cast<const void*>(&vtlb_memRead<mem8_t>);
		case 16: return reinterpret_cast<const void*>(&vtlb_memRead<mem16_t>);
		case 32: return reinterpret_cast<const void*>(&vtlb_memRead<mem32_t>);
		case 64: return reinterpret_cast<const void*>(&vtlb_memRead<mem64_t>);
		case 128: return reinterpret_cast<const void*>(&vtlb_memRead128);
		default: pxFailRel("Bad vtlb read size"); return nullptr;
	}
}

const void* vtlb_JitMemWrite(u32 bits)
{
	switch (bits)
	{
		case 8: return reinterpret_cast<const void*>(&vtlb_memWrite<mem8_t>);
		case 16: return reinterpret_cast<const void*>(&vtlb_memWrite<mem16_t>);
		case 32: return reinterpret_cast<const void*>(&vtlb_memWrite<mem32_t>);
		case 64: return reinterpret_cast<const void*>(&vtlb_memWrite<mem64_t>);
		case 128: return reinterpret_cast<const void*>(&vtlb_memWrite128);
		default: pxFailRel("Bad vtlb write size"); return nullptr;
	}
}

#endif
