#include "pico/stdlib.h"
#include "hardware/timer.h"

bool heartbeat_callback(repeating_timer_t *t) {
    static bool state = 0;
    gpio_put(PICO_DEFAULT_LED_PIN, state ^= 1);
    return true; // seguir llamando
}

int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    repeating_timer_t timer;
    add_repeating_timer_ms(500, heartbeat_callback, NULL, &timer);

    while (true) {
        tight_loop_contents();   // no bloquea
    }
}
