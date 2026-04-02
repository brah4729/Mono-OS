; boot/context_switch.asm — Process context switch routine
;
; void context_switch(uint32_t* old_esp, uint32_t new_esp);
;
; Saves callee-saved registers (edi, esi, ebx, ebp) onto old stack,
; stores old ESP into *old_esp, loads new ESP, restores registers, returns.

[GLOBAL context_switch]

context_switch:
    ; Save callee-saved registers on current stack
    push ebp
    push ebx
    push esi
    push edi

    ; Load arguments
    ; [esp+20] = old_esp pointer (1st arg, after 4 pushes + return addr)
    ; [esp+24] = new_esp value   (2nd arg)
    mov eax, [esp+20]      ; eax = &old_esp
    mov edx, [esp+24]      ; edx = new_esp

    ; Save current stack pointer into *old_esp
    mov [eax], esp

    ; Switch to new stack
    mov esp, edx

    ; Restore callee-saved registers from new stack
    pop edi
    pop esi
    pop ebx
    pop ebp

    ; Return — pops EIP from new stack, continuing the new process
    ret
