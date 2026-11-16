.syntax unified
.thumb
.global generator
 generator:
 bx pc
 nop
.arm
 push {r4,lr}
 ldr r2,[r0,#4]
 ldr r4,[r0,#8]
 mov r3,#100
1: strh r3,[r2],#2
 subs r4,r4,#1
 bne 1b
 mov r2,#1
 strb r2,[r0,#28]
 mov r0,#0
 pop {r4,lr}
 bx lr
