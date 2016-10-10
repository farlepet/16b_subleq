#ifndef CPU_H
#define CPU_H

#include <stdint.h>

typedef struct {
    int16_t ip;       // Instruction Pointer
    int16_t mem_size; // Memory size
    int16_t *mem;     // Memory
} cpu_t;

int cpu_cycle(cpu_t *);              // Simulate a single cycle of the CPU
cpu_t *cpu_create(int16_t, int16_t); // Create a CPU instance

#endif
