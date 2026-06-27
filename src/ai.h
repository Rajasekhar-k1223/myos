#ifndef AI_H
#define AI_H

#include <stdint.h>

/* A simple Multi-Layer Perceptron (MLP) node for basic inference */
typedef struct {
    int   num_inputs;
    float weights[16];
    float bias;
} ai_neuron_t;

typedef struct {
    int         num_neurons;
    ai_neuron_t neurons[16];
} ai_layer_t;

typedef struct {
    int        num_layers;
    ai_layer_t layers[4];
} ai_model_t;

void  ai_init(void);
void  ai_model_init_random(ai_model_t* model);
void  ai_forward(ai_model_t* model, float* input, float* output);

#endif
