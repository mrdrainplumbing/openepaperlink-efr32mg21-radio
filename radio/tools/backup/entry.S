.section .entry,"ax"
.global start
start:
    rsil a2, 15
    movi a1, 0x3fca4000
    call0 run
1:  j 1b
