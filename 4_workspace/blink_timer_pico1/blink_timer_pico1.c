#include "pico/stdlib.h"
#include "hardware/timer.h"

#if PICO_RP2350
#define LED_PIN 11      // Pico 2
#elif PICO_RP2040
#define LED_PIN 25      // Pico 1
#endif

bool heartbeat_callback(repeating_timer_t *t) {
    static bool state = 0;
    gpio_put(LED_PIN, state ^= 1);
    return true; // seguir llamando
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    repeating_timer_t timer;
    add_repeating_timer_ms(500, heartbeat_callback, NULL, &timer);

    while (true) {
        tight_loop_contents();   // no bloquea
    }
}
