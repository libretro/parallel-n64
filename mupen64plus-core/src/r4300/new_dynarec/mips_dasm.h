#ifndef __MIPS_DASM_H__
#define __MIPS_DASM_H__

typedef struct {
    char* name;
    char type;
    char arguments[32];
} mips_instruction_t;



int mips_disassemble(mips_instruction_t *instruction_buffer, uint32_t number);

static const char * const MIPS_REGISTER_NAMES[32] = {
    "$zero",    // Hardware constant 0
    "$at",      // Reserved for assembler
    "$v0",      // Return values
    "$v1",
    "$a0",      // Arguments
    "$a1",
    "$a2",
    "$a3",
    "$t0",      // Temporaries
    "$t1",
    "$t2",
    "$t3",
    "$t4",
    "$t5",
    "$t6",
    "$t7",
    "$s0",      // Saved values
    "$s1",
    "$s2",
    "$s3",
    "$s4",
    "$s5",
    "$s6",
    "$s7",
    "$t8",      // Cont. Saved values
    "$t9",
    "$k0",      // Reserved for OS
    "$k1",
    "$gp",      // Global pointer
    "$sp",      // Stack Pointer
    "$fp",      // Frame Pointer
    "$ra"       // Return Adress
};

static char * const MIPS_REGISTER_RT_INSTRUCTION_NAMES[4][8] = {
    {"bltz", "bgez"},
    {"tgei", "tgeiu", "tlti", "tltiu", "teqi", NULL, "tnei"},
    {"bltzal", "bgezal"}
};

static char * const MIPS_REGISTER_C_INSTRUCTION_NAMES[8][8] = {
    {"madd", "maddu", "mul", NULL, "msub", "msubu"},
    {},
    {},
    {},
    {"clz", "clo"}
};

static char * const MIPS_REGISTER_INSTRUCTION_NAMES[8][8] = {
    {"sll", NULL, "srl", "sra", "sllv", NULL, "srlv", "srav"},
    {"jr", "jalr"},
    {"mfhi", "mthi", "mflo", "mtlo"},
    {"mult", "multu", "div", "divu"},
    {"add", "addu", "sub", "subu", "and", "or", "xor", "nor"},
    {NULL, NULL, "slt", "sltu"}
};

static char * const MIPS_ROOT_INSTRUCTION_NAMES[8][8] = {
	{NULL, NULL, "j", "jal", "beq", "bne", "blez", "bgtz"},
	{"addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui"},
	{},
	{"llo", "lhi", "trap"},
	{"lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr"},
	{"sb", "sh", "swl", "sw", NULL, NULL, "swr"},
    {"ll"},
    {"sc"}
};

#define MIPS_TYPE_R 'R'
#define MIPS_TYPE_J 'J'
#define MIPS_TYPE_I 'I'

#define MIPS_REG_C_TYPE_MULTIPLY 0
#define MIPS_REG_C_TYPE_COUNT 4

#define MIPS_REG_TYPE_SHIFT_OR_SHIFTV 0
#define MIPS_REG_TYPE_JUMPR 1
#define MIPS_REG_TYPE_MOVE 2
#define MIPS_REG_TYPE_DIVMULT 3
#define MIPS_REG_TYPE_ARITHLOG_GTE 4

#define MIPS_ROOT_TYPE_JUMP_OR_BRANCH 0
#define MIPS_ROOT_TYPE_ARITHLOGI 1
#define MIPS_ROOT_TYPE_LOADI_OR_TRAP 3
#define MIPS_ROOT_TYPE_LOADSTORE_GTE 4


#endif
