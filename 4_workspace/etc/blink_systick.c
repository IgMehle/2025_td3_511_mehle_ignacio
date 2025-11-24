#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/regs/m0plus.h"
#include "hardware/irq.h"

volatile uint32_t ticks = 0;

// =============================================
// SysTick interrupt handler
// =============================================
void SysTick_Handler(void) {
    ticks++;
    if (ticks % 500 == 0) {   // cada 500 ms
        gpio_xor_mask(1u << LED_PIN);
    }
}

// =============================================
// SysTick initialization (1 ms tick)
// =============================================
void systick_init(uint32_t cpu_hz) {
    // SysTick usa CLKSOURCE = CPU
    uint32_t reload = (cpu_hz / 1000) - 1;   // 1 ms

    // Registros estándar ARM
    SysTick->LOAD = reload;
    SysTick->VAL  = 0;
    SysTick->CTRL = 
          SysTick_CTRL_CLKSOURCE_Msk |
          SysTick_CTRL_TICKINT_Msk   |
          SysTick_CTRL_ENABLE_Msk;
}


// =============================================
// main
// =============================================
int main() {
    stdio_init_all();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Obtener frecuencia real del CPU (SDK)
    uint32_t freq = clock_get_hz(clk_sys);

    systick_init(freq);

    while (1) {
        __wfi();   // dormir esperando interrupciones
    }
}

