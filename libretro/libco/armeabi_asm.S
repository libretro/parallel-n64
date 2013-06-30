.arm
.align 4
.globl co_switch_arm
.globl _co_switch_arm
# r0: cothread to switch to, r1: current cothread
co_switch_arm:
_co_switch_arm:
   # Save state
   stmia r1!, {r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14}
   # Load state
   ldmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r15}

