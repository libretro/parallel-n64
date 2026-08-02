#!/usr/bin/env python3
"""Annotated MIPS disassembler for N64/Aleck64 PIF boot roms (IPL1/IPL2).

Usage: dis_pifrom.py <pifdata.bin> [--all]
Prints a linear disassembly of the 0x7C0-byte boot rom (big-endian words)
with N64-specific annotations (PIF RAM, SP/DP/RI/MI registers).
"""
import sys, struct

REG = ['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
       's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
C0 = {0:'Index',1:'Random',2:'EntryLo0',3:'EntryLo1',4:'Context',5:'PageMask',6:'Wired',
      8:'BadVAddr',9:'Count',10:'EntryHi',11:'Compare',12:'Status',13:'Cause',14:'EPC',
      16:'Config',17:'LLAddr',28:'TagLo',29:'TagHi'}

IO = {0xa4040010:'SP_STATUS', 0xa4080000:'SP_PC', 0xa4300004:'MI_VERSION',
      0xa4600010:'PI_STATUS', 0xa4700000:'RI_MODE', 0xa4700004:'RI_CONFIG',
      0xa4700008:'RI_CURRENT_LOAD', 0xa470000c:'RI_SELECT', 0xa4700010:'RI_REFRESH',
      0xa4300000:'MI_INIT_MODE', 0xa4040000:'SP_MEM_ADDR', 0xa4040004:'SP_DRAM_ADDR',
      0xa4040008:'SP_RD_LEN', 0xa404000c:'SP_WR_LEN', 0xa4600000:'PI_DRAM_ADDR',
      0xa4600004:'PI_CART_ADDR'}

def note_addr(v):
    if v in IO: return IO[v]
    if 0xbfc007c0 <= v < 0xbfc00800: return f'PIF_RAM+{v-0xbfc007c0:#x}'
    if 0xa4000000 <= v < 0xa4001000: return f'DMEM+{v-0xa4000000:#x}'
    if 0xa4001000 <= v < 0xa4002000: return f'IMEM+{v-0xa4001000:#x}'
    if 0xb0000000 <= v < 0xb4000000: return f'CART+{v-0xb0000000:#x}'
    return None

def dis(base, words):
    hi_val = {}   # naive lui tracking per register
    out = []
    for i, w in enumerate(words):
        pc = base + i*4
        op = w >> 26
        rs, rt, rd = (w>>21)&31, (w>>16)&31, (w>>11)&31
        sa, fn = (w>>6)&31, w&63
        imm = w & 0xffff
        simm = imm - 0x10000 if imm & 0x8000 else imm
        tgt = ((pc+4)&0xf0000000) | ((w&0x3ffffff)<<2)
        br = pc + 4 + simm*4
        s = None; note = None
        if w == 0: s = 'nop'
        elif op == 0:
            f = {32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',
                 42:'slt',43:'sltu',8:'jr',9:'jalr',0:'sll',2:'srl',3:'sra',16:'mfhi',
                 18:'mflo',24:'mult',25:'multu',26:'div',27:'divu',4:'sllv',6:'srlv'}.get(fn, f'fn{fn}')
            if fn in (8,9): s = f'{f} ${REG[rs]}'
            elif fn in (0,2,3): s = f'{f} ${REG[rd]}, ${REG[rt]}, {sa}'
            elif fn in (16,18): s = f'{f} ${REG[rd]}'
            elif fn in (24,25,26,27): s = f'{f} ${REG[rs]}, ${REG[rt]}'
            else: s = f'{f} ${REG[rd]}, ${REG[rs]}, ${REG[rt]}'
        elif op == 1:
            f = {0:'bltz',1:'bgez',16:'bltzal',17:'bgezal'}.get(rt, f'regimm{rt}')
            s = f'{f} ${REG[rs]}, {br:#x}'
            if br == pc: note = 'INFINITE LOOP'
        elif op in (2,3): s = f'{"j" if op==2 else "jal"} {tgt:#x}'
        elif op in (4,5):
            f = 'beq' if op==4 else 'bne'
            s = f'{f} ${REG[rs]}, ${REG[rt]}, {br:#x}'
            if br == pc and rs == rt and op == 4: note = 'INFINITE LOOP'
        elif op in (6,7): s = f'{"blez" if op==6 else "bgtz"} ${REG[rs]}, {br:#x}'
        elif op in (20,21,22,23):
            f = {20:'beql',21:'bnel',22:'blezl',23:'bgtzl'}[op]
            s = f'{f} ${REG[rs]}, ${REG[rt]}, {br:#x}' if op in (20,21) else f'{f} ${REG[rs]}, {br:#x}'
        elif op in (8,9,10,11,12,13,14):
            f = {8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori'}[op]
            s = f'{f} ${REG[rt]}, ${REG[rs]}, {simm if op<12 else imm:#x}'
            if op in (9,13) and rs in hi_val:
                v = (hi_val[rs] + simm) & 0xffffffff if op==9 else hi_val[rs]|imm
                note = note_addr(v) or (f'= {v:#010x}')
                hi_val[rt] = v
        elif op == 15:
            s = f'lui ${REG[rt]}, {imm:#x}'
            hi_val[rt] = imm << 16
        elif op == 16:
            if rs == 4: s = f'mtc0 ${REG[rt]}, {C0.get(rd, rd)}'
            elif rs == 0: s = f'mfc0 ${REG[rt]}, {C0.get(rd, rd)}'
            else: s = f'cop0 {w:#010x}'
        elif op in (32,33,35,36,37,40,41,43,55,63,42,46,50,58,26,27,44,45,38,39):
            f = {32:'lb',33:'lh',35:'lw',36:'lbu',37:'lhu',40:'sb',41:'sh',43:'sw',
                 55:'ld',63:'sd',42:'swl',46:'swr',34:'lwl',38:'lwr',50:'lwc2',58:'swc2',
                 26:'ldl',27:'ldr',44:'sdl',45:'sdr',39:'lwu'}.get(op, f'op{op}')
            s = f'{f} ${REG[rt]}, {simm:#x}(${REG[rs]})'
            if rs in hi_val:
                v = (hi_val[rs] + simm) & 0xffffffff
                note = note_addr(v) or f'@ {v:#010x}'
        elif op == 47: s = f'cache {rt:#x}, {simm:#x}(${REG[rs]})'
        else: s = f'op{op} {w:#010x}'
        out.append((pc, w, s, note))
        if op in (2,3,4,5,6,7,1) or (op==0 and fn in (8,9)):
            hi_val = dict(hi_val)  # branches: keep naive state
    return out

def main():
    data = open(sys.argv[1], 'rb').read()
    words = struct.unpack(f'>{len(data)//4}I', data)
    n = len(words) if '--all' in sys.argv else 0x7c0//4
    for pc, w, s, note in dis(0xbfc00000, words[:n]):
        line = f'{pc:08x}: {w:08x}  {s}'
        if note: line += f'\t; {note}'
        print(line)

if __name__ == '__main__':
    main()
