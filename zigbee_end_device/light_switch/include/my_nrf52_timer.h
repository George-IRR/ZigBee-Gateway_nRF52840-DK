#ifndef MY_NRF52_TIMER_H
#define MY_NRF52_TIMER_H

#include <stdint.h>

typedef struct {
    volatile uint32_t TASKS_START;      // Offset 0x000
    volatile uint32_t TASKS_STOP;       // Offset 0x004

    uint32_t RESERVED0[14];             // 0x008 - 0x03F

    volatile uint32_t TASKS_CAPTURE0;   // Offset 0x040
    volatile uint32_t TASKS_CAPTURE1;   // Offset 0x044
    volatile uint32_t TASKS_CAPTURE2;   // Offset 0x048
    volatile uint32_t TASKS_CAPTURE3;   // Offset 0x04C
    volatile uint32_t TASKS_CAPTURE4;   // Offset 0x050
    volatile uint32_t TASKS_CAPTURE5;   // Offset 0x054
    
    uint32_t RESERVED1[300];            // 0x058 - 0x507
    
    volatile uint32_t BITMODE;          // Offset 0x508
    volatile uint32_t MODE;             // Offset 0x50C
    volatile uint32_t PRESCALER;        // Offset 0x510

    uint32_t RESERVED3[11];             // 0x514 - 0x53F

    volatile uint32_t CC0;              // Offset 0x540
    volatile uint32_t CC1;              // Offset 0x544
    volatile uint32_t CC2;              // Offset 0x548
    volatile uint32_t CC3;              // Offset 0x54C
    volatile uint32_t CC4;              // Offset 0x550
    volatile uint32_t CC5;              // Offset 0x554
} My_TIMER_Type;

#define TIMER0_BASE     (0x40008000UL)
#define TIMER1_BASE     (0x40009000UL)
#define TIMER2_BASE     (0x4000A000UL)
#define TIMER3_BASE     (0x4001A000UL)
#define TIMER4_BASE     (0x4001B000UL)

#define MY_TIMER0      ((My_TIMER_Type *) TIMER0_BASE)
#define MY_TIMER1      ((My_TIMER_Type *) TIMER1_BASE)
#define MY_TIMER2      ((My_TIMER_Type *) TIMER2_BASE)
#define MY_TIMER3      ((My_TIMER_Type *) TIMER3_BASE)
#define MY_TIMER4      ((My_TIMER_Type *) TIMER4_BASE)

typedef enum {
    MY_TIMER_0 = 0,
    MY_TIMER_1,
    MY_TIMER_2,
    MY_TIMER_3,
    MY_TIMER_4
} my_timer_instance_t;

void my_timer_init(my_timer_instance_t timer_idx, uint8_t prescaler);
void my_timer_start(my_timer_instance_t timer_idx);
void my_timer_stop(my_timer_instance_t timer_idx);
uint32_t my_timer_get_value(my_timer_instance_t timer_idx, uint8_t channel);

#endif // MY_NRF52_TIMER_H