/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - linkage_arm64.S                                         *
 *   Copyright (C) 2009-2011 Ari64                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define GLOBAL_FUNCTION(name)  \
    .align 3;                  \
    .globl name;               \
    .type name, %function;     \
    name

#define LOCAL_FUNCTION(name)  \
    .align 3;                 \
    .type name, %function;    \
    name

#define GLOBAL_VARIABLE(name, size_) \
    .globl name;                     \
    .type   name, %object;           \
    .size   name, size_

#define TEXT_SECTION .text
#define END_SECTION

TEXT_SECTION

GLOBAL_FUNCTION(_dyna_linker):
    brk 0
    
GLOBAL_FUNCTION(_dyna_linker_ds):
    brk 0

GLOBAL_FUNCTION(_jump_vaddr):
    str    w20, [x29, #260 ] /* cycle_count */
    ldr    w18, [x29, #264 ] /* last_count */
    add    w20, w18, w20 /* Count */
    str    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    bl     _get_addr_ht
    ldr    w20, [x29, #260 ] /* cycle_count */
    br     x0
    /*adrp   x1, hash_table
    add    x1, x1, :lo12:hash_table
    movn   w3, #15
    and    w2, w3, w2, lsr #12
    add    x1, x1, x2
    ldr    w2, [x1]
    cmp    w2, w0
    b.eq   .A1
    add    x1, x1, #16
    ldr    w2, [x1]
    cmp    w2, w0
    b.eq   .A1
    str    w20, [x29, #260 ]
    bl     get_addr
    ldr    w20, [x29, #260 ]
    br     x0
.A1:
    ldr    x1, [x1, #8]
    br     x1*/
    
GLOBAL_FUNCTION(_verify_code_ds):
    str    w19, [x29, #1256 ] /* branch_target */
    
GLOBAL_FUNCTION(_verify_code_vm):
    /* w0 = instruction pointer (virtual address) */
    /* w1 = source (virtual address) */
    /* x2 = copy */
    /* w3 = length */
    mov    w4, #0xC0000000
    cmp    w1, w4
    b.lt   _verify_code
    add    x8, x29, #2528 /* memory_map */
    lsr    w4, w1, #12
    add    w5, w1, w3
    sub    w5, w5, #1
    ldr    x6, [x8, x4, lsl #3]
    lsr    w5, w5, #12
    tst    x6, x6
    b.mi   .D5
    mov    x7, x6
    add    x1, x1, x6, lsl #2
    lsl    x6, x6, #2
.C1:
    add    w4, w4, #1
    cmp    x6, x7, lsl #2
    b.ne   .D5
    ldr    x7, [x8, x4, lsl #3]
    cmp    w4, w5
    b.ls   .C1
    
/*TOBEDONE: Optimize for 64bit*/
GLOBAL_FUNCTION(_verify_code):
    /* x1 = source */
    /* x2 = copy */
    /* w3 = length */
    tst    w3, #4
    add    x3, x1, x3
    mov    w4, #0
    mov    w5, #0
    mov    w12, #0
    b.eq   .D1
    ldr    w4, [x1], #4
    ldr    w5, [x2], #4
.D1:
    cmp    x1, x3
    b.eq   .D3
.D2:
    ldr    w7, [x1], #4
    eor    w9, w4, w5
    ldr    w8, [x2], #4
    orr    w9, w9, w12
    tst    w9, w9
    b.ne   .D4
    ldr    w4, [x1], #4
    eor    w12, w7, w8
    ldr    w5, [x2], #4
    cmp    x1, x3
    b.cc   .D2
    cmp    w7, w8
    b.ne   .D4
.D3:
    cmp    w4, w5
.D4:
    ldr    w19, [x29, #1256 ] /* branch_target */
    b.eq   .D6
.D5:
    bl     _get_addr
    br     x0
.D6:
    ret
    
GLOBAL_FUNCTION(_cc_interrupt):
    ldr    w0, [x29, #264 ] /* last_count */
    add    w20, w0, w20 /* Count */
    str    wzr, [x29, #268 ] /* pending_exception */
    mov    w1, #0x1fc
    lsr    w0, w20, #19
    and    w1, w1, w0 
    add    x0, x29, 2000 /* restore_candidate */
    str    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    ldr    w22, [x1, x0]
    mov    x20, x30 /* Save link register */
    tst    w22, w22
    b.ne   .E4
.E1:
    bl     _gen_interrupt
    mov    x30, x20 /* Restore link register */
    ldr    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    ldr    w0, [x29, #256 ] /* next_interrupt */
    ldr    w1, [x29, #268 ] /* pending_exception */
    ldr    w2, [x29, #276 ] /* stop */
    str    w0, [x29, #264 ] /* last_count */
    sub    w20, w20, w0
    tst    w2, w2
    b.ne   .E3
    tst    w1, w1
    b.ne   .E2
    ret
.E2:
    ldr    w0, [x29, #272 ] /* pcaddr */
    bl     _get_addr_ht
    br     x0
.E3:
    add    x16, x29, #152
    ldp    x19,x20,[x16,#0]
    ldp    x21,x22,[x16,#16]
    ldp    x23,x24,[x16,#32]
    ldp    x25,x26,[x16,#48]
    ldp    x27,x28,[x16,#64]
    ldp    x29,x30,[x16,#80]
    ret
.E4:
    /* Move 'dirty' blocks to the 'clean' list */
    str    wzr, [x1, x0]
    lsl    w21, w1, #3
    mov    w23, #0
.E5:
    tst    w22, #1
    b.eq   .E6
    add    w0, w21, w23
    bl     _clean_blocks
.E6:
    lsr    w22, w22, #1
    add    w23, w23, #1
    tst    w23, #31
    b.ne   .E5
    b      .E1
    
GLOBAL_FUNCTION(_do_interrupt):
    ldr    w0, [x29, #272 ] /* pcaddr */
    bl     _get_addr_ht
    ldr    w1, [x29, #256 ] /* next_interrupt */
    ldr    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    str    w1, [x29, #264 ] /* last_count */
    sub    w20, w20, w1
    add    w20, w20, #2
    br     x0
    
GLOBAL_FUNCTION(_fp_exception):
    mov    w2, #0x10000000
.F1:
    ldr    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    mov    w3, #0x80000000
    str    w0, [x29, #656 ] /* g_cp0_regs+56 */ /* EPC */
    orr    w1, w1, #2
    add    w2, w2, #0x2c
    str    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    str    w2, [x29, #652 ] /* g_cp0_regs+52 */ /* Cause */
    add    w0, w3, #0x180
    bl     _get_addr_ht
    br     x0
    
GLOBAL_FUNCTION(_fp_exception_ds):
    /* brk    0 */
    mov    w2, #0x90000000 /* Set high bit if delay slot */
    b      .F1
    
GLOBAL_FUNCTION(_jump_syscall):
    ldr    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    mov    w3, #0x80000000
    str    w0, [x29, #656 ] /* g_cp0_regs+56 */ /* EPC */
    orr    w1, w1, #2
    mov    w2, #0x20
    str    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    str    w2, [x29, #652 ] /* g_cp0_regs+52 */ /* Cause */
    add    w0, w3, #0x180
    bl     _get_addr_ht
    br     x0

GLOBAL_FUNCTION(_jump_eret):
    ldr    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    ldr    w0, [x29, #264 ] /* last_count */
    bic    w1, w1, #2
    add    w20, w0, w20
    str    w1, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    str    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    bl     _check_interrupt
    ldr    w1, [x29, #256 ] /* next_interrupt */
    ldr    w0, [x29, #656 ] /* g_cp0_regs+56 */ /* EPC */
    str    w1, [x29, #264 ] /* last_count */
    subs   w20, w20, w1
    b.pl   .E11
.E8:
    add    x6, x29, #584 /* reg+256 */
    mov    w5, #248
    mov    w1, #0
.E9:
    /*TOBEDONE: Optimize for 64bit*/
    sub    x6, x6, #8
    ldr    w2, [x6]
    ldr    w3, [x6, #4]
    eor    w3, w3, w2, asr #31
    subs   w3, w3, #1
    adc    w1, w1, w1
    subs   w5, w5, #8
    b.ne    .E9
    ldr    w2, [x29, #584 ] /* hi */
    ldr    w3, [x29, #588 ] /* hi+4 */
    eor    w3, w3, w2, asr #31
    tst    w3, w3
    b.ne   .E10
    ldr    w2, [x29, #592 ] /* lo */
    ldr    w3, [x29, #596 ] /* lo+4 */
    eor    w3, w3, w2, asr #31
.E10:
    subs   w3, w3, #1
    adc    w1, w1, w1
    bl     _get_addr_32
    br     x0
.E11:
    str    w0, [x29, #272 ] /* pcaddr */
    bl     _cc_interrupt
    ldr    w0, [x29, #272 ] /* pcaddr */
    b      .E8

GLOBAL_FUNCTION(_new_dyna_start):
    adrp   x16, _memory_layout@PAGE
    add    x16, x16, _memory_layout@PAGEOFF
    add    x16, x16, #152
    mov    w0, #0xa4000000
    stp    x19,x20,[x16,#0]
    stp    x21,x22,[x16,#16]
    stp    x23,x24,[x16,#32]
    stp    x25,x26,[x16,#48]
    stp    x27,x28,[x16,#64]
    stp    x29,x30,[x16,#80]
    sub    x29, x16, #152
    adrp   x1, _base_addr@PAGE
    add    x1, x1, _base_addr@PAGEOFF
    ldr    x19, [x1]
    add    w0, w0, #0x40
    bl     _new_recompile_block
    ldr    w0, [x29, #256 ] /* next_interrupt */
    ldr    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    str    w0, [x29, #264 ] /* last_count */
    sub    w20, w20, w0
    br     x19
    
GLOBAL_FUNCTION(_invalidate_addr_x0):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x1):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x2):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x3):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x4):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x5):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x6):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x7):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x8):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x9):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x10):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x11):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x12):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x13):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x14):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x15):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x16):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x17):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x18):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x19):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x20):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x21):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x22):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x23):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x24):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x25):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x26):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x27):
    brk 0
    
GLOBAL_FUNCTION(_invalidate_addr_x28):
    brk 0
    
GLOBAL_FUNCTION(_write_rdram_new):
    ldr    x3, [x29, #1480 ] /* ram_offset */
    ldr    w2, [x29, #288 ] /* address */
    ldr    w0, [x29, #312 ] /* cpu_word */
    str    w0, [x2, x3, lsl #2]
    b      .E12
    
GLOBAL_FUNCTION(_write_rdramb_new):
    ldr    x3, [x29, #1480 ] /* ram_offset */
    ldr    w2, [x29, #288 ] /* address */
    ldrb   w0, [x29, #318 ] /* cpu_byte */
    eor    w2, w2, #3
    lsl    x3, x3, #2
    strb   w0, [x2, x3]
    b      .E12
    
GLOBAL_FUNCTION(_write_rdramh_new):
    ldr    x3, [x29, #1480 ] /* ram_offset */
    ldr    w2, [x29, #288 ] /* address */
    ldrh   w0, [x29, #316 ] /* cpu_hword */
    eor    w2, w2, #2
    lsl    x3, x3, #2
    strh   w0, [x2, x3]
    b      .E12
    
GLOBAL_FUNCTION(_write_rdramd_new):
    ldr    x3, [x29, #1480 ] /* ram_offset */
    ldr    w2, [x29, #288 ] /* address */
    ldr    w0, [x29, #304 ] /* cpu_dword */
    ldr    w1, [x29, #308 ] /* cpu_dword+4 */
    add    x3, x2, x3, lsl #2
    str    w0, [x3, #4]
    str    w1, [x3]
    b      .E12

LOCAL_FUNCTION(_do_invalidate):
    ldr    w2, [x29, #288 ] /* address */
.E12:
    ldr    x1, [x29, #280 ] /* invc_ptr */
    lsr    w0, w2, #12
    ldrb   w2, [x1, x0]
    tst    w2, w2
    b.eq   .invalidate_block_apple
    ret

.invalidate_block_apple:
    b _invalidate_block

GLOBAL_FUNCTION(_read_nomem_new):
    /* w3 = instr addr/flags */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #8
    tst    x4, x4
    b.mi   _tlb_exception
    ldr    w0, [x2, x4, lsl #2]
    str    w0, [x29, #296 ] /* readmem_dword */
    ret
    
GLOBAL_FUNCTION(_read_nomemb_new):
    /* w3 = instr addr/flags */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #8
    tst    x4, x4
    b.mi   _tlb_exception
    eor    w2, w2, #3
    lsl    x4, x4, #2
    ldrb   w0, [x2, x4]
    str    w0, [x29, #296 ] /* readmem_dword */
    ret
    
GLOBAL_FUNCTION(_read_nomemh_new):
    /* w3 = instr addr/flags */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #8
    tst    x4, x4
    b.mi   _tlb_exception
    lsl    x4, x4, #2
    eor    w2, w2, #2
    ldrh   w0, [x2, x4]
    str    w0, [x29, #296 ] /* readmem_dword */
    ret
    
GLOBAL_FUNCTION(_read_nomemd_new):
    /* w3 = instr addr/flags */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #8
    tst    x4, x4
    b.mi   _tlb_exception
    add    x4, x2, x4, lsl #2
    ldr    w0, [x4]
    ldr    w1, [x4, #4]
    str    w0, [x29, #300 ] /* readmem_dword+4 */
    str    w1, [x29, #296 ] /* readmem_dword */
    ret
    
GLOBAL_FUNCTION(_write_nomem_new):
    /* w3 = instr addr/flags */
    str    w3, [x29, #2512 ] /* instr_addr */
    str    x30, [x29, #2520 ] /* link_register */
    bl     _do_invalidate
    ldr    w3, [x29, #2512 ] /* instr_addr */
    ldr    x30, [x29, #2520 ] /* link_register */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #0xc
    tst    x4, #0x4000000000000000
    b.ne   _tlb_exception
    ldr    w0, [x29, #312 ] /* cpu_word */
    str    w0, [x2, x4, lsl #2]
    ret
    
GLOBAL_FUNCTION(_write_nomemb_new):
    /* w3 = instr addr/flags */
    str    w3, [x29, #2512 ] /* instr_addr */
    str    x30, [x29, #2520 ] /* link_register */
    bl     _do_invalidate
    ldr    w3, [x29, #2512 ] /* instr_addr */
    ldr    x30, [x29, #2520 ] /* link_register */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #0xc
    tst    x4, #0x4000000000000000
    b.ne   _tlb_exception
    lsl    x4, x4, #2
    eor    w2, w2, #3
    ldrb   w0, [x29, #318 ] /* cpu_byte */
    strb   w0, [x2, x4]
    ret
    
GLOBAL_FUNCTION(_write_nomemh_new):
    /* w3 = instr addr/flags */
    str    w3, [x29, #2512 ] /* instr_addr */
    str    x30, [x29, #2520 ] /* link_register */
    bl     _do_invalidate
    ldr    w3, [x29, #2512 ] /* instr_addr */
    ldr    x30, [x29, #2520 ] /* link_register */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #0xc
    tst    x4, #0x4000000000000000
    b.ne   _tlb_exception
    lsl    x4, x4, #2
    eor    w2, w2, #2
    ldrh   w0, [x29, #316 ] /* cpu_hword */
    strh   w0, [x2, x4]
    ret
    
GLOBAL_FUNCTION(_write_nomemd_new):
    /* w3 = instr addr/flags */
    str    w3, [x29, #2512 ] /* instr_addr */
    str    x30, [x29, #2520 ] /* link_register */
    bl     _do_invalidate
    ldr    w3, [x29, #2512 ] /* instr_addr */
    ldr    x30, [x29, #2520 ] /* link_register */
    ldr    w2, [x29, #288 ] /* address */
    add    x4, x29, #2528 /* memory_map */
    lsr    w0, w2, #12
    ldr    x4, [x4, x0, lsl #3]
    mov    w1, #0xc
    tst    x4, #0x4000000000000000
    b.ne   _tlb_exception
    lsl    x4, x4, #2
    ldr    w0, [x29, #308 ] /* cpu_dword+4 */
    ldr    w1, [x29, #304 ] /* cpu_dword */
    str    w0, [x4]
    str    w1, [x4, #4]
    ret

LOCAL_FUNCTION(_tlb_exception):
    /* w1 = cause */
    /* w2 = address */
    /* w3 = instr addr/flags */
    ldr    w4, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    add    x5, x29, #2528 /* memory_map */
    lsr    w6, w3, #12
    orr    w1, w1, w3, lsl #31
    orr    w4, w4, #2
    ldr    x7, [x5, x6, lsl #3]
    bic    w8, w3, #3
    str    w4, [x29, #648 ] /* g_cp0_regs+48 */ /* Status */
    mov    w6, #0x6000000
    movk   w6, #0x22
    str    w1, [x29, #652 ] /* g_cp0_regs+52 */ /* Cause */
    ldr    w0, [x8, x7, lsl #2]
    add    w4, w8, w1, asr #29
    add    x5, x29, #328 /* reg */
    str    w4, [x29, #656 ] /* g_cp0_regs+56 */ /* EPC */
    mov    w7, #0xf8
    ldr    w8, [x29, #616 ] /* g_cp0_regs+16 */ /* Context */
    lsl    w1, w0, #16
    lsr    w4, w0, #26
    and    w4, w4, #31
    and    w7, w7, w0, lsr #18
    mov    w9, #0x0FFFFFF0
    sub    w2, w2, w1, asr #16
    bic    w9, w9, #0x0F800000
    ror    w6, w6, w4
    mov    w0, #0x80000000
    tst    w6, w0
    b.eq   .G1
    ldr    w2, [x5, x7]
.G1:
    bic    w8, w8, w9
    tst    w3, #2
    str    w2, [x5, x7]
    add    w4, w2, w1, asr #16
    add    x6, x29, #332 /* reg+4 */
    asr    w3, w2, #31
    str    w4, [x29, #632 ] /* g_cp0_regs+32 */ /* BadVAddr */
    add    w0, w0, #0x180
    and    w4, w9, w4, lsr #9
    b.eq   .G2
    str    w3, [x6, x7]
.G2:
    orr    w8, w8, w4
    str    w8, [x29, #616 ] /* g_cp0_regs+16 */ /* Context */
    bl     _get_addr_ht
    ldr    w1, [x29, #256 ] /* next_interrupt */
    ldr    w20, [x29, #636 ] /* g_cp0_regs+36 */ /* Count */
    str    w1, [x29, #264 ] /* last_count */
    sub    w20, w20, w1
    br     x0
    
GLOBAL_FUNCTION(_breakpoint):
    brk 0

END_SECTION
