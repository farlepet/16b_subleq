#include <stdio.h>
#include <stdlib.h>
#include <cpu.h>

int cpu_cycle(cpu_t *cpu) {
    if(!cpu) {
        printf("ERROR: CPU object is not initialized!\n");
        return 1;
    }
    if(!cpu->mem || cpu->mem_size == 0) {
        printf("ERROR: CPU memory not initialized!\n");
        return 1;
    }
    if(cpu->ip >= cpu->mem_size || cpu->ip < 0) {
        printf("ERROR: IP out of bounds of memory!\n");
        return 1;
    }

    int16_t a, b, c, A, B;
    // Store sections of opcodes into registers
    a = cpu->mem[cpu->ip];
    b = cpu->mem[cpu->ip+1];
    c = cpu->mem[cpu->ip+2]; // This could be moved to make the simulator _slightly_ faster

    // Store values at [a] and [b] in registers
    A = cpu->mem[a];
    B = cpu->mem[b];

    // Compute B - A and store result back into memory
    B = B - A;
    cpu->mem[b] = B;

    // B <= 0 ?
    if(B <= 0) {
        cpu->ip = c; // Jump to C
    } else {
        cpu->ip += 3; // Else go to next instruction
    }

    return 0;
}

cpu_t *cpu_create(int16_t mem_size, int16_t ip) {
    cpu_t *cpu = (cpu_t *)malloc(sizeof(cpu_t));
    cpu->mem = (int16_t *)malloc(mem_size * 2);
    cpu->mem_size = mem_size;
    cpu->ip = ip;
    return cpu;
}
