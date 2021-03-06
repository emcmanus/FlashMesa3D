/*
 * Copyright (C) 2008 Nicolai Haehnle.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RADEON_PROGRAM_H_
#define __RADEON_PROGRAM_H_

#include <stdint.h>
#include <string.h>

#include "radeon_opcodes.h"
#include "radeon_code.h"
#include "radeon_program_constants.h"
#include "radeon_program_pair.h"

struct radeon_compiler;

struct rc_src_register {
	rc_register_file File:3;

	/** Negative values may be used for relative addressing. */
	signed int Index:(RC_REGISTER_INDEX_BITS+1);
	unsigned int RelAddr:1;

	unsigned int Swizzle:12;

	/** Take the component-wise absolute value */
	unsigned int Abs:1;

	/** Post-Abs negation. */
	unsigned int Negate:4;
};

struct rc_dst_register {
	rc_register_file File:3;

	/** Negative values may be used for relative addressing. */
	signed int Index:(RC_REGISTER_INDEX_BITS+1);
	unsigned int RelAddr:1;

	unsigned int WriteMask:4;
};

/**
 * Instructions are maintained by the compiler in a doubly linked list
 * of these structures.
 *
 * This instruction format is intended to be expanded for hardware-specific
 * trickery. At different stages of compilation, a different set of
 * instruction types may be valid.
 */
struct rc_sub_instruction {
	struct rc_src_register SrcReg[3];
	struct rc_dst_register DstReg;

	/**
	 * Opcode of this instruction, according to \ref rc_opcode enums.
	 */
	rc_opcode Opcode:8;

	/**
	 * Saturate each value of the result to the range [0,1] or [-1,1],
	 * according to \ref rc_saturate_mode enums.
	 */
	rc_saturate_mode SaturateMode:2;

	/**
	 * Writing to the special register RC_SPECIAL_ALU_RESULT
	 */
	/*@{*/
	rc_write_aluresult WriteALUResult:2;
	rc_compare_func ALUResultCompare:3;
	/*@}*/

	/**
	 * \name Extra fields for TEX, TXB, TXD, TXL, TXP instructions.
	 */
	/*@{*/
	/** Source texture unit. */
	unsigned int TexSrcUnit:5;

	/** Source texture target, one of the \ref rc_texture_target enums */
	rc_texture_target TexSrcTarget:3;

	/** True if tex instruction should do shadow comparison */
	unsigned int TexShadow:1;
	/*@}*/
};

typedef enum {
	RC_INSTRUCTION_NORMAL = 0,
	RC_INSTRUCTION_PAIR
} rc_instruction_type;

struct rc_instruction {
	struct rc_instruction * Prev;
	struct rc_instruction * Next;

	rc_instruction_type Type;
	union {
		struct rc_sub_instruction I;
		struct rc_pair_instruction P;
	} U;

	/**
	 * Warning: IPs are not stable. If you want to use them,
	 * you need to recompute them at the beginning of each pass
	 * using \ref rc_recompute_ips
	 */
	unsigned int IP;
};

struct rc_program {
	/**
	 * Instructions.Next points to the first instruction,
	 * Instructions.Prev points to the last instruction.
	 */
	struct rc_instruction Instructions;

	/* Long term, we should probably remove InputsRead & OutputsWritten,
	 * since updating dependent state can be fragile, and they aren't
	 * actually used very often. */
	uint32_t InputsRead;
	uint32_t OutputsWritten;
	uint32_t ShadowSamplers; /**< Texture units used for shadow sampling. */

	struct rc_constant_list Constants;
};

enum {
	OPCODE_REPL_ALPHA = MAX_RC_OPCODE /**< used in paired instructions */
};


static inline rc_swizzle get_swz(unsigned int swz, rc_swizzle idx)
{
	if (idx & 0x4)
		return idx;
	return GET_SWZ(swz, idx);
}

static inline unsigned int combine_swizzles4(unsigned int src,
		rc_swizzle swz_x, rc_swizzle swz_y, rc_swizzle swz_z, rc_swizzle swz_w)
{
	unsigned int ret = 0;

	ret |= get_swz(src, swz_x);
	ret |= get_swz(src, swz_y) << 3;
	ret |= get_swz(src, swz_z) << 6;
	ret |= get_swz(src, swz_w) << 9;

	return ret;
}

static inline unsigned int combine_swizzles(unsigned int src, unsigned int swz)
{
	unsigned int ret = 0;

	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_X));
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_Y)) << 3;
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_Z)) << 6;
	ret |= get_swz(src, GET_SWZ(swz, RC_SWIZZLE_W)) << 9;

	return ret;
}

struct rc_src_register lmul_swizzle(unsigned int swizzle, struct rc_src_register srcreg);

static inline void reset_srcreg(struct rc_src_register* reg)
{
	memset(reg, 0, sizeof(struct rc_src_register));
	reg->Swizzle = RC_SWIZZLE_XYZW;
}


/**
 * A transformation that can be passed to \ref radeonLocalTransform.
 *
 * The function will be called once for each instruction.
 * It has to either emit the appropriate transformed code for the instruction
 * and return true, or return false if it doesn't understand the
 * instruction.
 *
 * The function gets passed the userData as last parameter.
 */
struct radeon_program_transformation {
	int (*function)(
		struct radeon_compiler*,
		struct rc_instruction*,
		void*);
	void *userData;
};

void radeonLocalTransform(
	struct radeon_compiler *c,
	int num_transformations,
	struct radeon_program_transformation* transformations);

unsigned int rc_find_free_temporary(struct radeon_compiler * c);

struct rc_instruction *rc_alloc_instruction(struct radeon_compiler * c);
struct rc_instruction *rc_insert_new_instruction(struct radeon_compiler * c, struct rc_instruction * after);
void rc_insert_instruction(struct rc_instruction * after, struct rc_instruction * inst);
void rc_remove_instruction(struct rc_instruction * inst);

unsigned int rc_recompute_ips(struct radeon_compiler * c);

void rc_print_program(const struct rc_program *prog);

#endif
