#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


#include "mips_dasm.h"

#define BITMASK(n) ((1 << n) - 1)


int mips_disassemble(mips_instruction_t* instruction, uint32_t number) {
    /*
     * In the MIPS architecture, all machine instructions are 
     * represented as 32-bit numbers
     */
    uint8_t op, op_lower, op_upper, 
            rs, rd, 
            rt, rt_upper, rt_lower, 
            sa, 
            funct, funct_upper, funct_lower;
    int16_t imm;
    int32_t target;

    op = number >> 26;
    op_upper = (op >> 3) & BITMASK(3);
    op_lower = op & BITMASK(3);

    rs = (number >> 21) & BITMASK(5);

    rt = (number >> 16) & BITMASK(5);
    rt_upper = rt >> 3;
    rt_lower = rt & BITMASK(3);

    rd = (number >> 11) & BITMASK(5);
    sa = (number >> 6) & BITMASK(5);

    funct = number & BITMASK(6);
    funct_upper = (funct >> 3) & BITMASK(3);
    funct_lower = funct & BITMASK(3);

    imm = number & BITMASK(16);

    // Tricky business: two's complement 26-bit
    target = number & BITMASK(26);
    if(target > BITMASK(25)) {
        target |= 0xfc000000;
    }


    if(op == 0) {
        instruction->name = 
                MIPS_REGISTER_INSTRUCTION_NAMES[funct_upper][funct_lower];
        instruction->type = MIPS_TYPE_R;
    } else if(op == 0x1c) {
        instruction->name = 
                MIPS_REGISTER_C_INSTRUCTION_NAMES[funct_upper][funct_lower];
        instruction->type = MIPS_TYPE_R;
    } else if(op == 1) {
        instruction->name = 
                MIPS_REGISTER_RT_INSTRUCTION_NAMES[rt_upper][rt_lower];
        instruction->type = MIPS_TYPE_I;
    } else {
        instruction->name = 
                MIPS_ROOT_INSTRUCTION_NAMES[op_upper][op_lower];
        instruction->type = MIPS_TYPE_I;
    }


    if(instruction->name == NULL) {
        printf("Did not find name for OP 0x%x:%d[%d,%d] ; FUNCT 0x%x:%d[%d,%d]\n", op, op, op_upper, op_lower, funct, funct, funct_upper, funct_lower);
        return 0;
    }

    if(number == 0) {
        instruction->name = "nop";
        instruction->arguments[0] = 0;

    } else if(op == 0) {
        switch(funct_upper) {
        case MIPS_REG_TYPE_SHIFT_OR_SHIFTV:
            if(funct_lower < 4) { //Shift
                sprintf(instruction->arguments, "%s, %s, %d", 
                    MIPS_REGISTER_NAMES[rd],
                    MIPS_REGISTER_NAMES[rt],
                    sa);
            } else { //ShiftV
                sprintf(instruction->arguments, "%s, %s, %s", 
                    MIPS_REGISTER_NAMES[rd],
                    MIPS_REGISTER_NAMES[rt],
                    MIPS_REGISTER_NAMES[rs]);
            }
            break;

        case MIPS_REG_TYPE_JUMPR:
            if(funct_lower < 1) {
                sprintf(instruction->arguments, "%s", MIPS_REGISTER_NAMES[rs]);
            } else {
                sprintf(instruction->arguments, "%s, %s", 
                    MIPS_REGISTER_NAMES[rd],
                    MIPS_REGISTER_NAMES[rs]);
            }
            break;

        case MIPS_REG_TYPE_MOVE:
            if(funct_lower % 2 == 0) {
                sprintf(instruction->arguments, "%s", MIPS_REGISTER_NAMES[rd]);
            } else {
                sprintf(instruction->arguments, "%s", MIPS_REGISTER_NAMES[rs]);
            }
            break;

        case MIPS_REG_TYPE_DIVMULT:
            sprintf(instruction->arguments, "%s, %s", 
                MIPS_REGISTER_NAMES[rs],
                MIPS_REGISTER_NAMES[rt]);
            break;

        case MIPS_REG_TYPE_ARITHLOG_GTE:
        case MIPS_REG_TYPE_ARITHLOG_GTE + 1:
            sprintf(instruction->arguments, "%s, %s, %s", 
                MIPS_REGISTER_NAMES[rd],
                MIPS_REGISTER_NAMES[rs],
                MIPS_REGISTER_NAMES[rt]);
            break;

        default:
            return 0;
        }

    } else if(op == 0x1c) {
        switch(funct_upper) {
        case MIPS_REG_C_TYPE_MULTIPLY:
            if(funct_lower == 2) {
                sprintf(instruction->arguments, "%s, %s, %s", 
                    MIPS_REGISTER_NAMES[rd],
                    MIPS_REGISTER_NAMES[rs],
                    MIPS_REGISTER_NAMES[rt]);
            } else {
                sprintf(instruction->arguments, "%s, %s", 
                    MIPS_REGISTER_NAMES[rs],
                    MIPS_REGISTER_NAMES[rt]);
            }
            break;

        case MIPS_REG_C_TYPE_COUNT:
            sprintf(instruction->arguments, "%s, %s", 
                MIPS_REGISTER_NAMES[rd],
                MIPS_REGISTER_NAMES[rs]);
            break;
        }

    } else if(op == 1) {
        sprintf(instruction->arguments, "%s, %d", 
                MIPS_REGISTER_NAMES[rs],
                imm);


    } else {
        switch(op_upper) {
        case MIPS_ROOT_TYPE_JUMP_OR_BRANCH:
            if(op_lower < 4) { // Jump
                sprintf(instruction->arguments, "%d", target);
                instruction->type = MIPS_TYPE_J;
            } else {
                if(op_lower < 6) { //Branch
                    sprintf(instruction->arguments, "%s, %s, %d", 
                        MIPS_REGISTER_NAMES[rs],
                        MIPS_REGISTER_NAMES[rt],
                        imm);
                } else { //BranchZ
                    //dbg("imm", imm);
                    sprintf(instruction->arguments, "%s, %d", 
                        MIPS_REGISTER_NAMES[rs],
                        imm);
                }
            }
            break;

        case MIPS_ROOT_TYPE_ARITHLOGI:
            if(op_lower < 7) {
                sprintf(instruction->arguments, "%s, %s, %d", 
                        MIPS_REGISTER_NAMES[rt],
                        MIPS_REGISTER_NAMES[rs],
                        imm);
            } else {
                sprintf(instruction->arguments, "%s, %d", 
                        MIPS_REGISTER_NAMES[rt],
                        imm);
            }
            break;

        case MIPS_ROOT_TYPE_LOADSTORE_GTE:
        case MIPS_ROOT_TYPE_LOADSTORE_GTE+1:
        case MIPS_ROOT_TYPE_LOADSTORE_GTE+2:
        case MIPS_ROOT_TYPE_LOADSTORE_GTE+3:
            sprintf(instruction->arguments, "%s, %d(%s)", 
                MIPS_REGISTER_NAMES[rt],
                imm,
                MIPS_REGISTER_NAMES[rs]);
            break;
        

        default:
            return 0;
        }
    }

    return 1;
}
