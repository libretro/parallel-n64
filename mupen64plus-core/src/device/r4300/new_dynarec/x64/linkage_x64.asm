;Mupen64plus - linkage_x64.asm
;Copyright (C) 2009-2011 Ari64
;
;This program is free software; you can redistribute it and/or modify
;it under the terms of the GNU General Public License as published by
;the Free Software Foundation; either version 2 of the License, or
;(at your option) any later version.
;
;This program is distributed in the hope that it will be useful,
;but WITHOUT ANY WARRANTY; without even the implied warranty of
;MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;GNU General Public License for more details.
;
;You should have received a copy of the GNU General Public License
;along with this program; if not, write to the
;Free Software Foundation, Inc.,
;51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

;x86_64 linkage for the parallel-n64 new_dynarec generation:
;Count = CCREG + last_count, asm memory handlers backed by the 64-bit
;memory_map (bit 63 = unmapped, bit 62 = write protect, host address =
;vaddr + (entry << 2); ram_offset uses the same >>2 convention).
;All data references are RIP-relative so the
;object links into a shared library without text relocations.
;
;Stack contract with the generated code: rsp is 16-byte aligned while
;JIT code runs.  Memory slow-path stubs push a fixed 64-byte frame
;(emit_pusha: seven GPRs + pad) and store the faulting PC + flags at
;[rsp+64] before calling a handler, so inside a handler the PC word is
;at [rsp+72].  Handlers clobber only rax, rcx, rdx, r9, r10, r11
;(rsi/rdi are callee-save on WIN64 and may hold live JIT registers).
;r12/r13 are scratch for this file: the JIT never allocates them and
;the C ABI preserves them.

; region 14 / Phase 2d: struct offsets for addressing migrated hot-state fields
; through g_dev (generated from the C struct; see tools/gen_asm_defines.awk).
%include "asm_defines_nasm.h"

%ifdef LEADING_UNDERSCORE
    %macro  cglobal 1
      global  _%1
      %define %1 _%1
    %endmacro

    %macro  cextern 1
      extern  _%1
      %define %1 _%1
    %endmacro
%else
    %macro  cglobal 1
      global  %1
    %endmacro

    %macro  cextern 1
      extern  %1
    %endmacro
%endif

%ifdef WIN64
%define ARG1_REG ecx
%define ARG2_REG edx
%define ARG3_REG r8d
%define ARG4_REG r9d
%define ARG1_REG64 rcx
%define ARG2_REG64 rdx
%define ARG3_REG64 r8
%define ARG4_REG64 r9
%define CCREG esi
%define CCREG64 rsi
%else
%define ARG1_REG edi
%define ARG2_REG esi
%define ARG3_REG edx
%define ARG4_REG ecx
%define ARG1_REG64 rdi
%define ARG2_REG64 rsi
%define ARG3_REG64 rdx
%define ARG4_REG64 rcx
%define CCREG ebx
%define CCREG64 rbx
%endif

;Call a C function with rsp 16-byte aligned at the macro entry.
%ifdef WIN64
%macro CALL_C 1
    sub     rsp,    32
    call    %1
    add     rsp,    32
%endmacro
%else
%macro CALL_C 1
    call    %1
%endmacro
%endif

cglobal jump_vaddr_eax
cglobal jump_vaddr_ecx
cglobal jump_vaddr_edx
cglobal jump_vaddr_ebx
cglobal jump_vaddr_ebp
cglobal jump_vaddr_esi
cglobal jump_vaddr_edi
cglobal verify_code
cglobal verify_code_vm
cglobal verify_code_ds
cglobal cc_interrupt
cglobal do_interrupt
cglobal fp_exception
cglobal fp_exception_ds
cglobal jump_syscall
cglobal jump_eret
cglobal new_dyna_start
cglobal invalidate_block_eax
cglobal invalidate_block_ecx
cglobal invalidate_block_edx
cglobal invalidate_block_ebx
cglobal invalidate_block_ebp
cglobal invalidate_block_esi
cglobal invalidate_block_edi
cglobal read_nomem_new
cglobal read_nomemb_new
cglobal read_nomemh_new
cglobal read_nomemd_new
cglobal write_nomem_new
cglobal write_nomemb_new
cglobal write_nomemh_new
cglobal write_nomemd_new
cglobal write_rdram_new
cglobal write_rdramb_new
cglobal write_rdramh_new
cglobal write_rdramd_new
cglobal breakpoint

cextern base_addr
cextern new_recompile_block
cextern get_addr_ht
cextern get_addr
cextern get_addr_32
cextern gen_interrupt
cextern check_interrupt
cextern clean_blocks
cextern invalidate_block
cextern new_dynarec_tlb_refill
cextern next_interrupt
cextern frame_break
cextern dyna_entry_rsp
cextern invalid_code
cextern hash_table

; region 14 / Phase 2d (increment 2): hot-state fields migrated into the embedded
; struct are addressed through g_dev plus the generated structural + field offsets,
; instead of as flat symbols. Compose the full displacement once per field.
cextern g_dev
%define g_dev_r4300_new_dynarec_hot_state_pcaddr            (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_pcaddr)
%define g_dev_r4300_new_dynarec_hot_state_pending_exception  (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_pending_exception)
%define g_dev_r4300_new_dynarec_hot_state_cycle_count       (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_cycle_count)
%define g_dev_r4300_new_dynarec_hot_state_stop              (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_stop)
%define g_dev_r4300_new_dynarec_hot_state_hi                 (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_hi)
%define g_dev_r4300_new_dynarec_hot_state_lo                 (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_lo)
%define g_dev_r4300_new_dynarec_hot_state_regs               (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_regs)
%define g_dev_r4300_new_dynarec_hot_state_mem_address        (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_mem_address)
%define g_dev_r4300_new_dynarec_hot_state_cp0_regs           (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_cp0_regs)
%define g_dev_r4300_new_dynarec_hot_state_memory_map         (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_memory_map)
%define g_dev_r4300_new_dynarec_hot_state_restore_candidate  (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_restore_candidate)
%define g_dev_r4300_new_dynarec_hot_state_hs_cpu_byte        (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_hs_cpu_byte)
%define g_dev_r4300_new_dynarec_hot_state_hs_cpu_hword       (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_hs_cpu_hword)
%define g_dev_r4300_new_dynarec_hot_state_wword              (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_wword)
%define g_dev_r4300_new_dynarec_hot_state_wdword             (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_wdword)
%define g_dev_r4300_new_dynarec_hot_state_rdword             (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_rdword)
%define g_dev_r4300_new_dynarec_hot_state_last_count        (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_last_count)
%define g_dev_r4300_new_dynarec_hot_state_ram_offset        (g_dev + offsetof_struct_device_r4300 + offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_ram_offset)

section .text

jump_vaddr_eax:
    mov     ARG1_REG,    eax
    jmp     jump_vaddr

jump_vaddr_edx:
    mov     ARG1_REG,    edx
    jmp     jump_vaddr

jump_vaddr_ebx:
%ifdef WIN64
    mov     ARG1_REG,    ebx
    jmp     jump_vaddr
%else
    int     3    ;CCREG, never a jump target
%endif

jump_vaddr_edi:
    mov     ARG1_REG,    edi
    jmp     jump_vaddr

jump_vaddr_ebp:
    mov     ARG1_REG,    ebp
    jmp     jump_vaddr

jump_vaddr_esi:
%ifdef WIN64
    int     3    ;CCREG, never a jump target
%else
    mov     ARG1_REG,    esi
    jmp     jump_vaddr
%endif

jump_vaddr_ecx:
    mov     ARG1_REG,    ecx

jump_vaddr:
    ;ARG1_REG = target virtual address
    ;Check hash table
    mov     eax,    ARG1_REG
    mov     r9d,    ARG1_REG
    shr     eax,    16
    xor     eax,    ARG1_REG
    movzx   eax,    ax
    shl     eax,    5    ;32-byte buckets
    lea     r10,    [rel hash_table]
    cmp     r9,     [r10+rax]
    jne     _C2
_C1:
    mov     rax,    [r10+rax+8]
    jmp     rax
_C2:
    cmp     r9,     [r10+rax+16]
    lea     rax,    [rax+16]
    je      _C1
    ;No hit on hash table, call compiler
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cycle_count],    CCREG
    CALL_C  get_addr
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cycle_count]
    jmp     rax

verify_code_ds:
verify_code_vm:
    ;ARG1_REG = source (virtual address)
    ;ARG2_REG64 = copy
    ;ARG3_REG = length
    ;ARG4_REG = pcaddr
    cmp     ARG1_REG,    0C0000000h
    jl      verify_code
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cycle_count],    CCREG
    mov     CCREG,    ARG1_REG
    lea     r10d,    [-1+ARG1_REG64+ARG3_REG64*1]
    shr     CCREG,    12
    shr     r10d,    12
    lea     r11,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     rax,    [r11+CCREG64*8]
    test    rax,    rax
    js      _D4
    lea     ARG1_REG64,    [ARG1_REG64+rax*4]
_D1:
    xor     rax,    [r11+CCREG64*8]
    shl     rax,    2
    jne     _D4
    mov     rax,    [r11+CCREG64*8]
    inc     CCREG
    cmp     CCREG,    r10d
    jbe     _D1
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cycle_count]

verify_code:
    ;ARG1_REG64 = source
    ;ARG2_REG64 = copy
    ;ARG3_REG = length
    ;ARG4_REG = pcaddr
    mov     eax,    [-4+ARG1_REG64+ARG3_REG64*1]
    xor     eax,    [-4+ARG2_REG64+ARG3_REG64*1]
    jne     _D5
    mov     eax,    ARG3_REG
    add     ARG3_REG,    -4
    je      _D3
    test    eax,    4
    cmove   ARG3_REG,    eax
_D2:
    mov     eax,    [-4+ARG1_REG64+ARG3_REG64*1]
    xor     eax,    [-4+ARG2_REG64+ARG3_REG64*1]
    jne     _D5
    mov     eax,    [-8+ARG1_REG64+ARG3_REG64*1]
    xor     eax,    [-8+ARG2_REG64+ARG3_REG64*1]
    jne     _D5
    add     ARG3_REG64,    -8
    jne     _D2
_D3:
    ret
_D4:
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cycle_count]
_D5:
    ;Code has changed, recompile.  Discard our own return address and
    ;jump straight to the fresh block.
    mov     ARG1_REG,    ARG4_REG
%ifdef WIN64
    sub     rsp,    40
    call    get_addr
    add     rsp,    48
%else
    sub     rsp,    8
    call    get_addr
    add     rsp,    16
%endif
    jmp     rax

cc_interrupt:
    ;The register allocator assumes the constant-caching host registers
    ;(ROREG/MMREG/INVCP) survive this call: load_needed_regs and
    ;load_regs_entry never reload them.  On arm64 the allocatable set is
    ;callee-saved so the C calls below preserve them for free; here the
    ;volatile allocatables must be saved explicitly (same invariant the
    ;invalidate_block thunk already honors).
    add     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_last_count]
    push    rax
    push    rcx
    push    rdx
%ifdef WIN64
    ;CCREG is esi here and is recomputed below, so it must NOT be in the
    ;save/restore set; rsi/rdi are callee-saved on WIN64 anyway.
    sub     rsp,    64    ;align stack (entry: rsp == 8 mod 16, +24 saved)
%else
    push    rsi
    push    rdi
    sub     rsp,    48    ;align stack (entry: rsp == 8 mod 16, +40 saved)
%endif
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36],    CCREG    ;Count
    shr     CCREG,    19
    mov     dword [rel g_dev_r4300_new_dynarec_hot_state_pending_exception],    0
    and     CCREG,    01fch
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_restore_candidate]
    cmp     dword [r10+CCREG64],    0
    jne     _E4
_E1:
    call    gen_interrupt
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36]
    mov     eax,    [rel next_interrupt]
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_pending_exception]
    mov     edx,    [rel g_dev_r4300_new_dynarec_hot_state_stop]
    or      edx,    [rel frame_break]    ;frame boundary: unwind like stop, without teardown
%ifdef WIN64
    add     rsp,    64
%else
    add     rsp,    48
%endif
    mov     [rel g_dev_r4300_new_dynarec_hot_state_last_count],    eax
    sub     CCREG,    eax
    test    edx,    edx
    jne     _E3
    test    ecx,    ecx
    jne     _E2
%ifndef WIN64
    pop     rdi
    pop     rsi
%endif
    pop     rdx
    pop     rcx
    pop     rax
    ret
_E2:
    ;Pending exception: jump to the handler instead of returning
    mov     ARG1_REG,    [rel g_dev_r4300_new_dynarec_hot_state_pcaddr]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cycle_count],    CCREG
    CALL_C  get_addr_ht    ;rsp == 0 mod 16 here
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cycle_count]
    mov     r10,    rax
%ifndef WIN64
    pop     rdi
    pop     rsi
%endif
    pop     rdx
    pop     rcx
    pop     rax
    add     rsp,    8    ;discard our return address, we jump to the handler
    jmp     r10
_E3:
    ;Unwind straight to new_dyna_start's frame.  A fixed constant is not
    ;enough: cc_interrupt is also reached through jump_eret (_E11), one
    ;call level deeper.
    mov     rsp,    [rel dyna_entry_rsp]
    ;restore callee-save registers
    pop     rbp
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
%ifdef WIN64
    pop     rsi
    pop     rdi
%endif
    ret             ;exit dynarec
_E4:
    ;Move 'dirty' blocks to the 'clean' list
    mov     r12d,    [r10+CCREG64]
    mov     dword [r10+CCREG64],    0
    shl     CCREG,    3
    xor     r13d,    r13d
_E5:
    shr     r12d,    1
    jnc     _E6
    mov     ARG1_REG,    CCREG
    add     ARG1_REG,    r13d
    call    clean_blocks    ;rsp == 0 mod 16 here, frame provides shadow
_E6:
    inc     r13d
    test    r13d,    31
    jne     _E5
    jmp     _E1

do_interrupt:
    mov     ARG1_REG,    [rel g_dev_r4300_new_dynarec_hot_state_pcaddr]
    CALL_C  get_addr_ht
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36]
    mov     edx,    [rel next_interrupt]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_last_count],    edx
    sub     CCREG,    edx
    add     CCREG,    2
    jmp     rax

fp_exception:
    ;eax = pcaddr
    mov     edx,    01000002ch
_E7:
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+48]
    or      ecx,    2
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+48],    ecx    ;Status
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+52],    edx    ;Cause
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+56],    eax    ;EPC
    mov     ARG1_REG,    080000180h
    CALL_C  get_addr_ht
    jmp     rax

fp_exception_ds:
    mov     edx,    09000002ch    ;Set high bit if delay slot
    jmp     _E7

jump_syscall:
    ;eax = pcaddr
    mov     edx,    020h
    jmp     _E7

jump_eret:
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+48]    ;Status
    add     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_last_count]
    and     ecx,    0FFFFFFFDh
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36],    CCREG    ;Count
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+48],    ecx      ;Status
    CALL_C  check_interrupt
    mov     eax,    [rel next_interrupt]
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_last_count],    eax
    sub     CCREG,    eax
    mov     eax,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+56]    ;EPC
    jns     _E11
_E8:
    ;Scan the 64-bit register file to build the is32 mask for get_addr_32
    mov     r9d,    248
    xor     r10d,    r10d
    lea     r11,    [rel g_dev_r4300_new_dynarec_hot_state_regs]
_E9:
    mov     ecx,    [r11+r9]
    mov     edx,    [r11+r9+4]
    sar     ecx,    31
    xor     edx,    ecx
    neg     edx
    adc     r10d,    r10d
    sub     r9,     8
    jne     _E9
    lea     r11,    [rel g_dev_r4300_new_dynarec_hot_state_hi]
    mov     ecx,    [r11]
    mov     edx,    [r11+4]
    sar     ecx,    31
    xor     edx,    ecx
    jne     _E10
    lea     r11,    [rel g_dev_r4300_new_dynarec_hot_state_lo]
    mov     ecx,    [r11]
    mov     edx,    [r11+4]
    sar     ecx,    31
    xor     edx,    ecx
_E10:
    neg     edx
    adc     r10d,    r10d
    mov     ARG1_REG,    eax
    mov     ARG2_REG,    r10d
    mov     [rel g_dev_r4300_new_dynarec_hot_state_cycle_count],    CCREG
    CALL_C  get_addr_32
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cycle_count]
    jmp     rax
_E11:
    mov     [rel g_dev_r4300_new_dynarec_hot_state_pcaddr],    eax
    call    cc_interrupt
    mov     eax,    [rel g_dev_r4300_new_dynarec_hot_state_pcaddr]
    jmp     _E8

new_dyna_start:
    ;we must keep the stack 16-byte aligned for the C calls below and
    ;for all generated code (see stack contract at the top of the file)
%ifdef WIN64
    push    rdi
    push    rsi
%endif
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbp
    mov     [rel dyna_entry_rsp],    rsp
    add     rsp,    -56
    ;Resume at pcaddr (seeded to the boot vector by new_dynarec_init):
    mov     ARG1_REG,    [rel g_dev_r4300_new_dynarec_hot_state_pcaddr]
    CALL_C  get_addr_ht
    mov     ecx,    [rel next_interrupt]
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_last_count],    ecx
    sub     CCREG,    ecx
    jmp     rax

invalidate_block_eax:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    eax
    jmp     invalidate_block_call

invalidate_block_ecx:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    ecx
    jmp     invalidate_block_call

invalidate_block_edx:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    edx
    jmp     invalidate_block_call

invalidate_block_ebx:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    ebx
    jmp     invalidate_block_call

invalidate_block_ebp:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    ebp
    jmp     invalidate_block_call

invalidate_block_esi:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    esi
    jmp     invalidate_block_call

invalidate_block_edi:
    push    rax
    push    rcx
    push    rdx
    push    rsi
    push    rdi
    mov     ARG1_REG,    edi

invalidate_block_call:
    ;entry rsp == 8 mod 16, five pushes realign to 0 mod 16
    CALL_C  invalidate_block
    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rax
    ret

;Memory slow-path handlers.  Called from the stubs emitted by
;do_readstub/do_writestub with the JIT registers saved in the fixed
;64-byte frame and the faulting PC + flags at [rsp+72].  Clobber only
;rax, rcx, rdx, r9, r10, r11.  On a TLB miss they tail-jump to
;tlb_exception with eax = read(0)/write(1) and r9d = the address.

read_nomem_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    0
    test    r10,    r10
    js      tlb_exception
    mov     ecx,    [r9+r10*4]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_rdword],    ecx
    ret

read_nomemb_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    0
    test    r10,    r10
    js      tlb_exception
    mov     edx,    r9d
    xor     edx,    3
    movzx   ecx,    byte [rdx+r10*4]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_rdword],    ecx
    ret

read_nomemh_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    0
    test    r10,    r10
    js      tlb_exception
    mov     edx,    r9d
    xor     edx,    2
    movzx   ecx,    word [rdx+r10*4]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_rdword],    ecx
    ret

read_nomemd_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    0
    test    r10,    r10
    js      tlb_exception
    lea     rdx,    [r9+r10*4]
    mov     ecx,    [rdx+4]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_rdword],    ecx
    mov     ecx,    [rdx]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_rdword+4],    ecx
    ret

write_nomem_new:
    call    do_invalidate
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    1
    shl     r10,    2    ;carry = write protect bit
    jc      tlb_exception
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wword]
    mov     [r9+r10],    ecx
    ret

write_nomemb_new:
    call    do_invalidate
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    1
    shl     r10,    2    ;carry = write protect bit
    jc      tlb_exception
    mov     edx,    r9d
    xor     edx,    3
    mov     cl,     byte [rel g_dev_r4300_new_dynarec_hot_state_hs_cpu_byte]
    mov     [rdx+r10],    cl
    ret

write_nomemh_new:
    call    do_invalidate
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    1
    shl     r10,    2    ;carry = write protect bit
    jc      tlb_exception
    mov     edx,    r9d
    xor     edx,    2
    mov     cx,     word [rel g_dev_r4300_new_dynarec_hot_state_hs_cpu_hword]
    mov     [rdx+r10],    cx
    ret

write_nomemd_new:
    call    do_invalidate
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel g_dev_r4300_new_dynarec_hot_state_memory_map]
    mov     r10,    [r10+rcx*8]
    mov     eax,    1
    shl     r10,    2    ;carry = write protect bit
    jc      tlb_exception
    lea     rdx,    [r9+r10]
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wdword+4]
    mov     [rdx],    ecx
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wdword]
    mov     [rdx+4],    ecx
    ret

write_rdram_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     rdx,    [rel g_dev_r4300_new_dynarec_hot_state_ram_offset]
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wword]
    mov     [r9+rdx*4],    ecx
    jmp     _E12

write_rdramb_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     rdx,    [rel g_dev_r4300_new_dynarec_hot_state_ram_offset]
    mov     eax,    r9d
    xor     eax,    3
    mov     cl,     byte [rel g_dev_r4300_new_dynarec_hot_state_hs_cpu_byte]
    mov     [rax+rdx*4],    cl
    jmp     _E12

write_rdramh_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     rdx,    [rel g_dev_r4300_new_dynarec_hot_state_ram_offset]
    mov     eax,    r9d
    xor     eax,    2
    mov     cx,     word [rel g_dev_r4300_new_dynarec_hot_state_hs_cpu_hword]
    mov     [rax+rdx*4],    cx
    jmp     _E12

write_rdramd_new:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
    mov     rdx,    [rel g_dev_r4300_new_dynarec_hot_state_ram_offset]
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wdword+4]
    mov     [r9+rdx*4],    ecx
    mov     ecx,    [rel g_dev_r4300_new_dynarec_hot_state_wdword]
    mov     [r9+rdx*4+4],    ecx
    jmp     _E12

do_invalidate:
    mov     r9d,    [rel g_dev_r4300_new_dynarec_hot_state_mem_address]
_E12:
    ;r9d = address; r9 must survive for the caller
    mov     ecx,    r9d
    shr     ecx,    12
    lea     r10,    [rel invalid_code]
    cmp     byte [r10+rcx],    1
    je      _E13
    ;Stack parity differs between the call path (write_nomem*) and the
    ;jump path (write_rdram*), so align dynamically around the C call.
    push    r9
    mov     r12,    rsp
    and     rsp,    -16
    sub     rsp,    32
    mov     ARG1_REG,    ecx
    call    invalidate_block
    mov     rsp,    r12
    pop     r9
_E13:
    ret

tlb_exception:
    ;eax = read(0)/write(1)
    ;r9d = memory address
    ;[rsp+72] = faulting instruction address + flags
    mov     r8d,    [rsp+72]
    add     rsp,    72    ;unwind handler return address + stub frame
    mov     ARG1_REG,    r8d
    mov     ARG2_REG,    r9d
    mov     ARG3_REG,    eax
    CALL_C  new_dynarec_tlb_refill
    mov     ecx,    [rel next_interrupt]
    mov     CCREG,    [rel g_dev_r4300_new_dynarec_hot_state_cp0_regs+36]
    mov     [rel g_dev_r4300_new_dynarec_hot_state_last_count],    ecx
    sub     CCREG,    ecx
    jmp     rax

breakpoint:
    int     3
    ret

%ifidn __OUTPUT_FORMAT__, elf64
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
