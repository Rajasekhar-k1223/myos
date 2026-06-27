#ifndef VOICE_H
#define VOICE_H

void voice_init(void);
void voice_process_audio(const short* pcm_data, int samples);

#endif
