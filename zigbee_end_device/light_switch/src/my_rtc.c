#include <my_rtc.h>
#include <zephyr/sys/__assert.h> 
#include <errno.h>

static My_RTC_Type* get_rtc_pointer(my_rtc_instance_t rtc_idx)
{
    switch (rtc_idx) {
        case MY_RTC_0: return MY_RTC0;
        case MY_RTC_1: return MY_RTC1;
        case MY_RTC_2: return MY_RTC2;
        default:       return ((void*)0); 
    }
}

int my_rtc_init(my_rtc_instance_t rtc_idx, uint16_t prescaler)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT(rtc != NULL, "Invalid RTC instance!");
    if (!rtc) {
        return -EINVAL;
    }

    rtc->PRESCALER = prescaler;
    rtc->TASKS_CLEAR = 1;
    return 0;
}

int my_rtc_start(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT_NO_MSG(rtc != NULL);
    if (!rtc) {
        return -EINVAL;
    }

    rtc->TASKS_START = 1; 
    return 0;
}

int my_rtc_stop(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT_NO_MSG(rtc != NULL);
    if (!rtc) {
        return -EINVAL;
    }

    rtc->TASKS_STOP = 1;
    return 0;
}

int my_rtc_clear(my_rtc_instance_t rtc_idx)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT_NO_MSG(rtc != NULL);
    if (!rtc) {
        return -EINVAL;
    }

    rtc->TASKS_CLEAR = 1;
    return 0;
}

int my_rtc_get_value(my_rtc_instance_t rtc_idx, uint32_t *out_value)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT(rtc != NULL, "Invalid RTC instance!");
    __ASSERT(out_value != NULL, "Output pointer is NULL!");
    
    if (!rtc || !out_value) {
        return -EINVAL;
    }

    *out_value = rtc->COUNTER;
    return 0;
}

int my_rtc_start_compare(my_rtc_instance_t rtc_idx, uint8_t channel, uint32_t compare_value)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    
    __ASSERT(rtc != NULL, "Invalid RTC instance!");
    __ASSERT(channel <= 3, "Invalid RTC channel!");
    __ASSERT(compare_value <= 0x00FFFFFF, "CC value exceeds 24-bit resolution!");

    if (!rtc || channel > 3 || compare_value > 0x00FFFFFF) {
        return -EINVAL;
    }

    rtc->CC[channel] = compare_value;
    rtc->EVTENSET = (1 << (16 + channel));
    return 0;
}

int my_rtc_get_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel, bool *out_event)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT(rtc != NULL, "Invalid RTC instance!");
    __ASSERT(channel <= 3, "Invalid RTC channel!");
    __ASSERT(out_event != NULL, "Output pointer is NULL!");
    
    if (!rtc || channel > 3 || !out_event) {
        return -EINVAL;
    }

    *out_event = (rtc->EVENTS_COMPARE[channel] == 1);
    return 0;
}

int my_rtc_clear_compare_event(my_rtc_instance_t rtc_idx, uint8_t channel)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT(rtc != NULL, "Invalid RTC instance!");
    __ASSERT(channel <= 3, "Invalid RTC channel!");
    
    if (!rtc || channel > 3) {
        return -EINVAL;
    }

    rtc->EVENTS_COMPARE[channel] = 0;
    return 0;
}

int my_rtc_enable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT_NO_MSG(rtc != NULL);
    if (!rtc) {
        return -EINVAL;
    }

    rtc->INTENSET = mask;
    return 0;
}

int my_rtc_disable_interrupt(my_rtc_instance_t rtc_idx, uint32_t mask)
{
    My_RTC_Type *rtc = get_rtc_pointer(rtc_idx);
    __ASSERT_NO_MSG(rtc != NULL);
    if (!rtc) {
        return -EINVAL;
    }

    rtc->INTENCLR = mask;
    return 0;
}