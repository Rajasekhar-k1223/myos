#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg | 0x80); /* disable NMI while reading */
    return inb(CMOS_DATA);
}

static int rtc_updating(void) {
    outb(CMOS_ADDR, 0x0A);
    return inb(CMOS_DATA) & 0x80;
}

static uint8_t bcd2bin(uint8_t v) {
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

void rtc_read(struct rtc_time* t) {
    struct rtc_time a, b;
    /* Double-read until two consecutive reads agree (avoids mid-update reads) */
    do {
        while (rtc_updating());
        a.second = cmos_read(0x00); a.minute = cmos_read(0x02);
        a.hour   = cmos_read(0x04); a.day    = cmos_read(0x07);
        a.month  = cmos_read(0x08); a.year   = cmos_read(0x09);

        while (rtc_updating());
        b.second = cmos_read(0x00); b.minute = cmos_read(0x02);
        b.hour   = cmos_read(0x04); b.day    = cmos_read(0x07);
        b.month  = cmos_read(0x08); b.year   = cmos_read(0x09);
    } while (a.second != b.second || a.minute != b.minute);

    *t = b;

    uint8_t status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) { /* BCD mode */
        t->second = bcd2bin(t->second);
        t->minute = bcd2bin(t->minute);
        t->hour   = (uint8_t)(bcd2bin(t->hour & 0x7F) | (t->hour & 0x80));
        t->day    = bcd2bin(t->day);
        t->month  = bcd2bin(t->month);
        t->year   = bcd2bin((uint8_t)t->year);
    }

    if (!(status_b & 0x02) && (t->hour & 0x80)) /* 12-hour PM */
        t->hour = (uint8_t)(((t->hour & 0x7F) + 12) % 24);

    t->year = (uint16_t)(t->year + (t->year < 70 ? 2000 : 1900));
}

void rtc_datetime_str(char* buf) {
    struct rtc_time t;
    rtc_read(&t);
    /* Write YYYY-MM-DD HH:MM:SS */
    buf[0]  = '0' + (t.year / 1000) % 10;
    buf[1]  = '0' + (t.year / 100)  % 10;
    buf[2]  = '0' + (t.year / 10)   % 10;
    buf[3]  = '0' + (t.year)        % 10;
    buf[4]  = '-';
    buf[5]  = '0' + t.month  / 10; buf[6]  = '0' + t.month  % 10;
    buf[7]  = '-';
    buf[8]  = '0' + t.day    / 10; buf[9]  = '0' + t.day    % 10;
    buf[10] = ' ';
    buf[11] = '0' + t.hour   / 10; buf[12] = '0' + t.hour   % 10;
    buf[13] = ':';
    buf[14] = '0' + t.minute / 10; buf[15] = '0' + t.minute % 10;
    buf[16] = ':';
    buf[17] = '0' + t.second / 10; buf[18] = '0' + t.second % 10;
    buf[19] = '\0';
}
