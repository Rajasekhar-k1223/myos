#include "ai.h"
#include "kernel.h"
#include "string.h"

void ai_init(void) {
    terminal_printf("[AI] Neural Network Inference Engine Initialized.\n");
}

/* Very simple mock PRNG for weight initialization */
static uint32_t seed = 0x12345678;
static float random_float(void) {
    seed = (1103515245 * seed + 12345);
    int r = (seed >> 16) & 0x7FFF;
    return ((float)r / 32767.0f) * 2.0f - 1.0f; // -1.0 to 1.0
}

/* Basic ReLU activation */
static float relu(float x) {
    return (x > 0.0f) ? x : 0.0f;
}

void ai_model_init_random(ai_model_t* model) {
    if (!model) return;
    for (int l = 0; l < model->num_layers; l++) {
        ai_layer_t* layer = &model->layers[l];
        for (int n = 0; n < layer->num_neurons; n++) {
            ai_neuron_t* neuron = &layer->neurons[n];
            neuron->bias = random_float();
            for (int i = 0; i < neuron->num_inputs; i++) {
                neuron->weights[i] = random_float();
            }
        }
    }
}

void ai_forward(ai_model_t* model, float* input, float* output) {
    if (!model || !input || !output) return;
    
    float current_input[16];
    float current_output[16];
    
    /* Copy initial input */
    for (int i = 0; i < 16; i++) current_input[i] = input[i];

    for (int l = 0; l < model->num_layers; l++) {
        ai_layer_t* layer = &model->layers[l];
        for (int n = 0; n < layer->num_neurons; n++) {
            ai_neuron_t* neuron = &layer->neurons[n];
            float sum = neuron->bias;
            for (int i = 0; i < neuron->num_inputs; i++) {
                sum += current_input[i] * neuron->weights[i];
            }
            current_output[n] = relu(sum);
        }
        /* Outputs of this layer become inputs for the next */
        for (int n = 0; n < layer->num_neurons; n++) {
            current_input[n] = current_output[n];
        }
    }
    
    /* Copy final output */
    for (int i = 0; i < model->layers[model->num_layers - 1].num_neurons; i++) {
        output[i] = current_output[i];
    }
}
