#include "my_nrf52_timer.h"
#include <zephyr/sys/__assert.h> 
#include <errno.h>

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

int my_timer_init(my_timer_instance_t timer_idx, uint8_t prescaler)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT(timer != NULL, "Invalid Timer instance!");
    if (!timer) {
        return -EINVAL;
    }
    
    timer->BITMODE   = 3;          // 32 bit configuration
    timer->PRESCALER = prescaler;  
    return 0;
}

int my_timer_start(my_timer_instance_t timer_idx)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT_NO_MSG(timer != NULL);
    if (!timer) {
        return -EINVAL;
    }
    
    timer->TASKS_START = 1;
    return 0;
}

int my_timer_stop(my_timer_instance_t timer_idx)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT_NO_MSG(timer != NULL);
    if (!timer) {
        return -EINVAL;
    }
    
    timer->TASKS_STOP = 1;
    return 0;
}

int my_timer_get_value(my_timer_instance_t timer_idx, uint8_t channel, uint32_t *out_value)
{
    My_TIMER_Type *timer = get_timer_pointer(timer_idx);
    __ASSERT(timer != NULL, "Invalid Timer instance!");
    __ASSERT(channel <= 5, "Invalid Timer channel!");
    __ASSERT(out_value != NULL, "Output pointer is NULL!");
    
    if (!timer || channel > 5 || !out_value) {
        return -EINVAL;
    }

    switch (channel) {
        case 0: timer->TASKS_CAPTURE0 = 1; *out_value = timer->CC0; break;
        case 1: timer->TASKS_CAPTURE1 = 1; *out_value = timer->CC1; break;
        case 2: timer->TASKS_CAPTURE2 = 1; *out_value = timer->CC2; break;
        case 3: timer->TASKS_CAPTURE3 = 1; *out_value = timer->CC3; break;
        case 4: timer->TASKS_CAPTURE4 = 1; *out_value = timer->CC4; break;
        case 5: timer->TASKS_CAPTURE5 = 1; *out_value = timer->CC5; break;
        default: return -EINVAL;
    }
    
    return 0;
}