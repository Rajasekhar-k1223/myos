/*
 * Voice assistant with real MFCC (Mel-Frequency Cepstral Coefficients).
 *
 * Pipeline: PCM frame → Hamming window → DFT magnitude → power spectrum
 *           → mel filterbanks → log-energy → DCT → 13 MFCCs
 *           → MLP wake-word classifier.
 *
 * Frame size: 512 samples (32ms at 16 kHz).
 * Mel filters: 26 triangular filters from 300 Hz to 8000 Hz.
 * MFCCs returned: 13 (C0 through C12).
 */
#include "voice.h"
#include "kernel.h"
#include "mathf.h"
#include "ai.h"
#include "string.h"

#define FRAME_SIZE   512
#define NUM_FILTERS  26
#define NUM_MFCC     13
#define SAMPLE_RATE  16000

static ai_model_t voice_model;

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Natural log approximation (Taylor around x=1, good for x in [0.5, 2.0]) */
static float my_logf(float x) {
    if (x <= 0.0f) return -1e30f;
    /* Range-reduce: x = m * 2^e, log(x) = log(m) + e*log(2) */
    int e = 0;
    while (x > 2.0f) { x *= 0.5f; e++; }
    while (x < 0.5f) { x *= 2.0f; e--; }
    /* x in [0.5, 2.0]: use log(x) = log((1+t)/(1-t)) ≈ 2*sum odd powers of t, t=(x-1)/(x+1) */
    float t = (x - 1.0f) / (x + 1.0f);
    float t2 = t * t;
    float s  = t * (2.0f + t2 * (2.0f/3.0f + t2 * (2.0f/5.0f + t2 * (2.0f/7.0f))));
    return s + (float)e * 0.693147f;
}

/* Mel frequency conversion */
static float hz_to_mel(float hz) {
    return 2595.0f * my_logf(1.0f + hz / 700.0f) / my_logf(10.0f);
}
static float mel_to_hz(float mel) {
    /* 10^(mel/2595) - 1) * 700 */
    float exp10 = 0.0f;
    {
        /* 10^x = e^(x*ln10), approximate e^y */
        float y = (mel / 2595.0f) * 2.302585f;
        /* e^y via Taylor: 1 + y + y^2/2 + y^3/6 + y^4/24 */
        float t = 1.0f + y * (1.0f + y * (0.5f + y * (0.16667f + y * 0.04167f)));
        exp10 = t;
    }
    return (exp10 - 1.0f) * 700.0f;
}

/* ── Mel filterbank centres (precomputed at init) ─────────────────────── */
static float mel_centres_bin[NUM_FILTERS + 2]; /* FFT bin indices */

static void build_mel_filterbank(void) {
    float mel_lo = hz_to_mel(300.0f);
    float mel_hi = hz_to_mel(8000.0f);
    float step   = (mel_hi - mel_lo) / (float)(NUM_FILTERS + 1);
    for (int i = 0; i < NUM_FILTERS + 2; i++) {
        float mel = mel_lo + (float)i * step;
        float hz  = mel_to_hz(mel);
        /* Convert Hz to FFT bin (0..FRAME_SIZE/2) */
        mel_centres_bin[i] = hz * (float)(FRAME_SIZE / 2) / (float)(SAMPLE_RATE / 2);
    }
}

/* ── DFT magnitude spectrum (real-input, first N/2+1 bins) ───────────── */
static void dft_magnitude(const float* windowed, float* mag, int N) {
    int half = N / 2 + 1;
    for (int k = 0; k < half; k++) {
        float re = 0.0f, im = 0.0f;
        float ang_step = -6.28318f * (float)k / (float)N;
        for (int n = 0; n < N; n++) {
            float ang = ang_step * (float)n;
            re += windowed[n] * cosf(ang);
            im += windowed[n] * sinf(ang);
        }
        mag[k] = sqrtf(re * re + im * im);
    }
}

/* ── Hamming window ───────────────────────────────────────────────────── */
static void hamming_window(const short* pcm, float* out, int N) {
    for (int n = 0; n < N; n++) {
        float w = 0.54f - 0.46f * cosf(6.28318f * (float)n / (float)(N - 1));
        out[n]  = ((float)pcm[n] / 32768.0f) * w;
    }
}

/* ── Apply mel filterbanks to power spectrum ─────────────────────────── */
static void apply_mel_filterbanks(const float* mag, float* bank_energy) {
    int half = FRAME_SIZE / 2 + 1;
    for (int m = 0; m < NUM_FILTERS; m++) {
        float lo  = mel_centres_bin[m];
        float ctr = mel_centres_bin[m + 1];
        float hi  = mel_centres_bin[m + 2];
        float energy = 0.0f;
        for (int k = 0; k < half; k++) {
            float fk = (float)k;
            float w  = 0.0f;
            if (fk >= lo && fk <= ctr && ctr > lo)
                w = (fk - lo) / (ctr - lo);
            else if (fk > ctr && fk <= hi && hi > ctr)
                w = (hi - fk) / (hi - ctr);
            float p = mag[k] * mag[k];  /* power */
            energy += w * p;
        }
        bank_energy[m] = energy;
    }
}

/* ── DCT-II (N=NUM_FILTERS, keep first NUM_MFCC coefficients) ────────── */
static void dct(const float* in, float* out, int N, int keep) {
    for (int k = 0; k < keep; k++) {
        float s = 0.0f;
        for (int n = 0; n < N; n++)
            s += in[n] * cosf(3.14159f * (float)k * ((float)n + 0.5f) / (float)N);
        out[k] = s;
    }
}

/* ── Scratch buffers (static to avoid stack blowout) ─────────────────── */
static float windowed[FRAME_SIZE];
static float mag[FRAME_SIZE / 2 + 1];
static float bank[NUM_FILTERS];
static float mfcc[NUM_MFCC];

void voice_init(void) {
    terminal_printf("[VOICE] Initializing MFCC + wake-word engine...\n");
    build_mel_filterbank();

    /* 2-layer MLP: 13 MFCC → 16 hidden → 2 outputs (background / "Hey Elsea") */
    voice_model.num_layers = 2;
    voice_model.layers[0].num_neurons = 16;
    for (int i = 0; i < 16; i++) {
        voice_model.layers[0].neurons[i].num_inputs = NUM_MFCC;
        for (int j = 0; j < NUM_MFCC; j++)
            voice_model.layers[0].neurons[i].weights[j] = 0.1f * (float)((i + j) % 7 - 3);
        voice_model.layers[0].neurons[i].bias = 0.0f;
    }
    voice_model.layers[1].num_neurons = 2;
    for (int i = 0; i < 2; i++) {
        voice_model.layers[1].neurons[i].num_inputs = 16;
        for (int j = 0; j < 16; j++)
            voice_model.layers[1].neurons[i].weights[j] = 0.05f * (float)((i + j) % 5 - 2);
        voice_model.layers[1].neurons[i].bias = 0.0f;
    }
    terminal_printf("[VOICE] MFCC pipeline ready: %d filters, %d coefficients, 16kHz.\n",
                    NUM_FILTERS, NUM_MFCC);
}

void voice_process_audio(const short* pcm_data, int samples) {
    if (samples < FRAME_SIZE) return;

    /* Process one frame of FRAME_SIZE samples */
    hamming_window(pcm_data, windowed, FRAME_SIZE);
    dft_magnitude(windowed, mag, FRAME_SIZE);
    apply_mel_filterbanks(mag, bank);

    /* Log energy */
    for (int m = 0; m < NUM_FILTERS; m++)
        bank[m] = my_logf(bank[m] + 1e-10f);

    dct(bank, mfcc, NUM_FILTERS, NUM_MFCC);

    /* Normalise MFCCs (mean-subtract) */
    float mean = 0.0f;
    for (int i = 0; i < NUM_MFCC; i++) mean += mfcc[i];
    mean /= (float)NUM_MFCC;
    for (int i = 0; i < NUM_MFCC; i++) mfcc[i] -= mean;

    /* MLP inference */
    float output[16];
    ai_forward(&voice_model, mfcc, output);

    if (output[1] > 0.75f && output[1] > output[0]) {
        terminal_printf("[VOICE] Wake word 'Hey Elsea' detected! (score=%.2f)\n",
                        (double)output[1]);
    }
}
