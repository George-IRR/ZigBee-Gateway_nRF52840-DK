#include <my_rtc.h>

static My_RTC_Type* get_rtc_pointer(my_rtc_instance_t rtc_idx)
{
    switch (rtc_idx) {
        case MY_RTC_0: return MY_RTC0;
        case MY_RTC_1: return MY_RTC1;
        case MY_RTC_2: return MY_RTC2;
        default:       return ((void*)0); 
    }
}

void my_rtc_init(my_rtc_instance_t rtc_idx, uint16_t prescaler)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return; 

    rtc->PRESCALER = prescaler;
    rtc->TASKS_CLEAR = 1;
}

void my_rtc_start(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return;

    rtc->TASKS_START = 1; 
}

void my_rtc_stop(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return;

    rtc->TASKS_STOP = 1;
}

void my_rtc_clear(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return;

    rtc->TASKS_CLEAR = 1;
}

uint32_t my_rtc_get_value(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    return rtc ? rtc->COUNTER : 0;
}

void my_rtc_start_compare(my_rtc_instance_t rtc_idx, uint8_t channel, uint32_t compare_value)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    
    // If value is bigger than 24 bits, return
    if (!rtc || channel > 3 || compare_value > 0x00FFFFFF) return;

    rtc->CC[channel] = compare_value;
    
    rtc->EVTENSET = (1 << (16 + channel));
}

bool my_rtc_get_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    
    if (!rtc || channel > 3) return false;

    return (rtc->EVENTS_COMPARE[channel] == 1);
}

void my_rtc_clear_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc || channel > 3) return;

   rtc->EVENTS_COMPARE[channel] = 0;
}

void my_rtc_enable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return;

    rtc->INTENSET = mask;
}

void my_rtc_disable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    if (!rtc) return;

    rtc->INTENCLR = mask;
}