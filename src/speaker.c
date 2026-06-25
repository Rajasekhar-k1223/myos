#include "speaker.h"
#include "io.h"
#include "task.h"

// Play sound using built-in speaker
void speaker_play(uint32_t nFrequence) {
    if (nFrequence == 0) return;
    
    uint32_t Div;
    uint8_t tmp;

    // Set the PIT to the desired frequency
    Div = 1193180 / nFrequence;
    outb(0x43, 0xb6);
    outb(0x42, (uint8_t) (Div) );
    outb(0x42, (uint8_t) (Div >> 8));

    // And play the sound using the PC speaker
    tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }
}

// Shut up
void speaker_stop(void) {
    uint8_t tmp = inb(0x61) & 0xFC;
    outb(0x61, tmp);
}

// Make a beep
void speaker_beep(uint32_t freq, uint32_t duration_ms) {
    speaker_play(freq);
    task_sleep(duration_ms);
    speaker_stop();
}
