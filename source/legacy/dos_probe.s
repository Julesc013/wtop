/* Minimal 8086 loader observation, not a process monitor.
 * SPDX-License-Identifier: GPL-3.0-only
 * GNU as --32 --defsym WTOP_DOS2=0|1; no CRT or DOS2 API in DOS1 build.
 * DOS1 uses COM entry CS=PSP, DS set explicitly, INT20 return.
 * DOS2 uses MZ CS=data/code segment, independent SS:SP, INT21/4c exit.
 * INT12 reports BIOS conventional-memory size, not free DOS memory. */
.intel_syntax noprefix
.code16
.arch i8086
.section .text
.global _start
.macro PRINT label
    mov dx, OFFSET \label
    mov ah, 0x09
    int 0x21
.endm
_start:
    push cs
    pop ds
    cld
    PRINT heading
    int 0x12
    call unsigned_decimal
.if WTOP_DOS2
    PRINT version_heading
    mov ax, 0x3000
    int 0x21
    mov WORD PTR reported_version, ax
    mov WORD PTR reported_oem, bx
    xor ah, ah
    call unsigned_decimal
    PRINT minor_heading
    mov ax, WORD PTR reported_version
    mov al, ah
    xor ah, ah
    call unsigned_decimal
    PRINT oem_heading
    mov ax, WORD PTR reported_oem
    mov al, ah
    xor ah, ah
    call unsigned_decimal
.else
    PRINT no_version
.endif
    PRINT ending
.if WTOP_DOS2
    mov ax, 0x4c00
    int 0x21
.else
    int 0x20
.endif
/* AX is one unsigned 16-bit observation. Bounded five digits plus '$'.
 * DS is the application segment; this routine preserves all modified registers. */
unsigned_decimal:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, OFFSET digits_end
    mov bx, 10
next_digit:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov BYTE PTR [si], dl
    or ax, ax
    jnz next_digit
    mov dx, si
    mov ah, 0x09
    int 0x21
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
heading:
.if WTOP_DOS2
    .ascii "{\"schema\":\"wtop.loader-probe/1\",\"profile\":\"dos2plus-x86\",\"format\":\"MZ\","
.else
    .ascii "{\"schema\":\"wtop.loader-probe/1\",\"profile\":\"dos1-x86\",\"format\":\"COM\","
.endif
    .ascii "\"observations\":[{\"id\":\"bios.int12.conventional_kib\",\"state\":\"observed\",\"native_code\":0,\"value\":\"$"
.if WTOP_DOS2
version_heading:
    .ascii "\"},{\"id\":\"dos.int21_30.major\",\"state\":\"observed\",\"native_code\":0,\"value\":\"$"
minor_heading:
    .ascii "\"},{\"id\":\"dos.int21_30.minor\",\"state\":\"observed\",\"native_code\":0,\"value\":\"$"
oem_heading:
    .ascii "\"},{\"id\":\"dos.int21_30.oem_raw\",\"state\":\"observed\",\"native_code\":0,\"value\":\"$"
reported_version: .word 0
reported_oem: .word 0
.else
no_version:
    .ascii "\"},{\"id\":\"dos.version\",\"state\":\"unsupported\",\"native_code\":0,\"value\":\"not-queried-on-dos1$"
.endif
ending:
    .ascii "\"},{\"id\":\"process.cpu\",\"state\":\"unsupported\",\"native_code\":0,\"value\":\"no-provider\"}],\"qualification\":\"not-established\"}\r\n$"
digits: .space 5
 digits_end: .byte '$'
