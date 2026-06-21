#ifndef MY_RTC_H
#define MY_RTC_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t TASKS_START;      // 0x000
    volatile uint32_t TASKS_STOP;       // 0x004
    volatile uint32_t TASKS_CLEAR;      // 0x008
    volatile uint32_t TASKS_TRIGOVRFLW; // 0x00C
    
    uint32_t RESERVED0[60];             
    
    volatile uint32_t EVENTS_TICK;      // 0x100
    volatile uint32_t EVENTS_OVRFLW;    // 0x104
    
    uint32_t RESERVED1[14];             
    
    volatile uint32_t EVENTS_COMPARE[4];// 0x140
    
    uint32_t RESERVED2[109];            
    
    volatile uint32_t INTENSET;         // 0x304
    volatile uint32_t INTENCLR;         // 0x308
    
    uint32_t RESERVED3[13];             
    
    volatile uint32_t EVTEN;            // 0x340
    volatile uint32_t EVTENSET;         // 0x344
    volatile uint32_t EVTENCLR;         // 0x348
    
    uint32_t RESERVED4[110];            
    
    volatile uint32_t COUNTER;          // 0x504
    volatile uint32_t PRESCALER;        // 0x508
    
    uint32_t RESERVED5[13];             
    
    volatile uint32_t CC[4];            // 0x540
} My_RTC_Type;

#define MY_RTC0  ((My_RTC_Type *) 0x4000B000)
#define MY_RTC1  ((My_RTC_Type *) 0x40011000)
#define MY_RTC2  ((My_RTC_Type *) 0x40024000)

typedef enum {
    MY_RTC_0 = 0,
    MY_RTC_1,
    MY_RTC_2
} my_rtc_instance_t;

void     my_rtc_init(my_rtc_instance_t rtc_idx, uint16_t prescaler);
void     my_rtc_start(my_rtc_instance_t rtc_idx);
void     my_rtc_stop(my_rtc_instance_t rtc_idx);
void     my_rtc_clear(my_rtc_instance_t rtc_idx);
uint32_t my_rtc_get_value(my_rtc_instance_t rtc_idx);
void     my_rtc_start_compare(my_rtc_instance_t rtc_idx, uint8_t channel, uint32_t compare_value);
bool     my_rtc_get_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel);
void     my_rtc_clear_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel);
void     my_rtc_enable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask);
void     my_rtc_disable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask);

#define MY_RTC_INT_TICK_MASK      (1UL << 0)
#define MY_RTC_INT_OVRFLW_MASK    (1UL << 1)
#define MY_RTC_INT_COMPARE0_MASK  (1UL << 16)
#define MY_RTC_INT_COMPARE1_MASK  (1UL << 17)
#define MY_RTC_INT_COMPARE2_MASK  (1UL << 18)
#define MY_RTC_INT_COMPARE3_MASK  (1UL << 19)

#endif // MY_RTC_H