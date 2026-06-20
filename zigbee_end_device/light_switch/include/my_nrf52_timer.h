#ifndef MY_NRF52_TIMER_H
#define MY_NRF52_TIMER_H

#include <stdint.h>

typedef struct {
    volatile uint32_t TASKS_START;      // Offset 0x000
    volatile uint32_t TASKS_STOP;       // Offset 0x004

    uint32_t RESERVED0[14];

    volatile uint32_t TASKS_CAPTURE0;       // Offset 0x040
    volatile uint32_t TASKS_CAPTURE1;       // Offset 0x044
    volatile uint32_t TASKS_CAPTURE2;       // Offset 0x048
    volatile uint32_t TASKS_CAPTURE3;       // Offset 0x04C
    volatile uint32_t TASKS_CAPTURE4;       // Offset 0x050
    volatile uint32_t TASKS_CAPTURE5;       // Offset 0x054
    
    uint32_t RESERVED1[300];             // Unused 0x008 - 0x507)
    
    volatile uint32_t BITMODE;          // Offset 0x508

    uint32_t RESERVED2;

    volatile uint32_t PRESCALER;        // Offset 0x510

    uint32_t RESERVED3[11]; 

    volatile uint32_t CC0;               // Offset 0x540
    volatile uint32_t CC1;               // Offset 0x544
    volatile uint32_t CC2;               // Offset 0x548
    volatile uint32_t CC3;               // Offset 0x54C
    volatile uint32_t CC4;               // Offset 0x550
    volatile uint32_t CC5;               // Offset 0x554
} My_TIMER_Type;

#define TIMER0_BASE     (0x40008000UL)
#define TIMER1_BASE     (0x40009000UL)

#define MY_TIMER0      ((My_TIMER_Type *) TIMER0_BASE)
#define MY_TIMER1      ((My_TIMER_Type *) TIMER1_BASE)

#endif // MY_NRF52_TIMER_H