#include "my_nrf52_timer.h"
#include <zephyr/sys/__assert.h>
#include <stddef.h>

static My_TIMER_Type* get_timer_pointer(my_timer_instance_t timer_idx)
{
    switch (timer_idx) {
        case MY_TIMER_0: return MY_TIMER0;
        case MY_TIMER_1: return MY_TIMER1;
        case MY_TIMER_2: return MY_TIMER2;
        case MY_TIMER_3: return MY_TIMER3;
        case MY_TIMER_4: return MY_TIMER4;
        default:         return ((void*)0); 
    }
}

void my_timer_init(my_timer_instance_t timer_idx, uint8_t prescaler)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT(timer != NULL, "Invalid Timer instance!");
    
    if (timer != ((void*)0)) {
        timer->BITMODE   = 3;          // 32 bit configuration
        timer->PRESCALER = prescaler;  
    }
}

void my_timer_start(my_timer_instance_t timer_idx)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT_NO_MSG(timer != NULL);
    
    if (timer != ((void*)0)) {
        timer->TASKS_START = 1;
    }
}

void my_timer_stop(my_timer_instance_t timer_idx)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT_NO_MSG(timer != NULL);
    
    if (timer != ((void*)0)) {
        timer->TASKS_STOP = 1;
    }
}

uint32_t my_timer_get_value(my_timer_instance_t timer_idx, uint8_t channel)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT(timer != NULL, "Invalid Timer instance!");
    __ASSERT(channel <= 5, "Invalid Timer channel! Must be between 0 and 5.");
    
    if (timer == ((void*)0)) {
        return 0;
    }

    switch (channel) {
        case 0: timer->TASKS_CAPTURE0 = 1; return timer->CC0;
        case 1: timer->TASKS_CAPTURE1 = 1; return timer->CC1;
        case 2: timer->TASKS_CAPTURE2 = 1; return timer->CC2;
        case 3: timer->TASKS_CAPTURE3 = 1; return timer->CC3;
        case 4: timer->TASKS_CAPTURE4 = 1; return timer->CC4;
        case 5: timer->TASKS_CAPTURE5 = 1; return timer->CC5;
        default: return 0;
    }
}