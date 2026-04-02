; boot/idt.asm — IDT load, ISR and IRQ stubs

section .text
global idt_flush
extern isr_handler
extern irq_handler

; void idt_flush(uint32_t idt_ptr)
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; ---------------------------------------------------------------
; Macro for ISRs that do NOT push an error code
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0           ; Push dummy error code
    push dword %1          ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISRs that DO push an error code
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1          ; Push interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; Macro for IRQ stubs
%macro IRQ 2
global irq%1
irq%1:
    push dword 0           ; Push dummy error code
    push dword %2          ; Push interrupt number (32 + IRQ#)
    jmp irq_common_stub
%endmacro

; ---------------------------------------------------------------
; ISR stubs 0-31
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; IRQ stubs 0-15 (mapped to interrupts 32-47)
IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; ---------------------------------------------------------------
; System call stub (int 0x80)
ISR_NOERR 128

; ---------------------------------------------------------------
; Common ISR stub — saves state, calls C handler, restores state
isr_common_stub:
    pusha                  ; Push edi, esi, ebp, esp, ebx, edx, ecx, eax
    mov ax, ds
    push eax               ; Save the data segment descriptor

    mov ax, 0x10           ; Load the kernel data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp               ; Push pointer to registers_t
    call isr_handler
    add esp, 4             ; Clean up pushed pointer

    pop eax                ; Restore original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                   ; Pop edi, esi, ebp, esp, ebx, edx, ecx, eax
    add esp, 8             ; Clean up error code and ISR number
    iret                   ; Return from interrupt

; Common IRQ stub
irq_common_stub:
    pusha
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call irq_handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret
