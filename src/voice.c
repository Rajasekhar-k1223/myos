#include "voice.h"
#include "kernel.h"
#include "ai.h"
#include "ac97.h"

static ai_model_t voice_model;

void voice_init(void) {
    terminal_printf("[VOICE] Initializing Voice Assistant...\n");
    
    /* Setup a dummy AI model for wake-word detection */
    voice_model.num_layers = 2;
    voice_model.layers[0].num_neurons = 16;
    for (int i=0; i<16; i++) voice_model.layers[0].neurons[i].num_inputs = 16;
    
    voice_model.layers[1].num_neurons = 2; // [0] = background, [1] = "Hey Elsea"
    for (int i=0; i<2; i++) voice_model.layers[1].neurons[i].num_inputs = 16;
    
    ai_model_init_random(&voice_model);
    
    terminal_printf("[VOICE] AI Wake-word model loaded.\n");
}

/* Process incoming microphone PCM data */
void voice_process_audio(const short* pcm_data, int samples) {
    if (samples < 16) return;
    
    /* Extract basic audio features (e.g., energy buckets) */
    float features[16];
    for (int i = 0; i < 16; i++) {
        float sum = 0;
        for (int j = 0; j < samples/16; j++) {
            float val = pcm_data[i*(samples/16) + j] / 32768.0f;
            sum += val * val; // energy
        }
        features[i] = sum;
    }
    
    /* Run AI inference */
    float output[16];
    ai_forward(&voice_model, features, output);
    
    if (output[1] > 0.8f && output[1] > output[0]) {
        terminal_printf("[VOICE] Wake word detected!\n");
        /* In a real implementation, we would start capturing the command here */
    }
}
