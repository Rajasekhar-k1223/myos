#include <stdint.h>
#include <stddef.h>

void* sys_mmap_audio() {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(44));
    return (void*)ret;
}

int sys_audio_play(uint32_t len, int loop) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(45), "b"(len), "c"(loop));
    return ret;
}

int main() {
    uint8_t* audio_buf = sys_mmap_audio();
    if (!audio_buf) return 1;

    /* Generate a simple square wave (A4 at 440 Hz) */
    /* SB16 is configured for 22050 Hz in sb16_play_pcm */
    /* period = 22050 / 440 = 50 samples. */
    uint32_t len = 65536; 
    for (uint32_t i = 0; i < len; i++) {
        if ((i % 50) < 25) {
            audio_buf[i] = 160; 
        } else {
            audio_buf[i] = 96;  
        }
    }

    /* Start playback in a loop */
    sys_audio_play(len, 1);

    /* Yield forever */
    while (1) {
        __asm__ volatile("int $0x80" : : "a"(14), "b"(1000)); 
    }
    return 0;
}
