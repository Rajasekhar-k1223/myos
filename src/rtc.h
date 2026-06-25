#ifndef RTC_H
#define RTC_H

#include <stdint.h>

struct rtc_time {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
};

void rtc_read(struct rtc_time* t);

/* Fills buf with "YYYY-MM-DD HH:MM:SS\0" — buf must be >= 20 bytes */
void rtc_datetime_str(char* buf);

#endif
