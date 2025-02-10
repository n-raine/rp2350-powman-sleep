#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "hardware/powman.h"
#include "pico/stdlib.h"
#include "pico/sync.h" // for `__wfi()`

// Global state to make sure 
static uint64_t next_wakeup = 0;
static uint64_t const WAKEUP_INTERVAL = 5000;

// Example global state which needs to be preserved on reboot 
static uint32_t led_off_time = 250;
static uint32_t led_on_time = 500;


/**
 *  If something goes wrong, blink rapidly with this pattern.
 */
void error_blink() {
    while(1) {
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(150);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(150);
    }
}


/**
 *  Initialise the peripherals in a separate function since they need to be 
 *  initialised after every sleep reboot.
 */
void init_peripherals() {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}


/**
 *  Prepares the system for sleep by setting the powman alarm to self-wakeup.
 *  Also loads the `exit_sleep()` function into RAM to enable rebooting into
 *  it after wakeup.
 */
void do_sleep() {

    // TODO: Set powman timer alarm
    next_wakeup += WAKE_INTERVAL;
    powman_enable_alarm_wakeup_at_ms(next_wakeup);

    // TODO: Load exit_sleep() into RAM
    // This should be done by the linker/loader since that's what it was built for.

    // TODO: Set the new `exit_sleep()` RAM function as reboot vector
    powman_hw->boot[0] = 0x0; // TODO: magic number
    powman_hw->boot[1] = 0x0; // TODO: magic number ^ reboot addr
    powman_hw->boot[2] = 0x0; // TODO: set stack pointer
    powman_hw->boot[3] = 0x0; // TODO: set reboot addr. OR with 1 ensures we are running ARM thumb instructions rather than RISC-V

    powman_power_state p0_0 = 0x0f; // SW core, XIP cache, SRAM 0, SRAM 1
    powman_power_state p1_0 = 0x0f; // XIP cache, SRAM 0, SRAM 1

    bool valid = powman_configure_wakeup_state(p1_0, p0_0);
    if(!valid) error_blink(); // 

    powman_set_power_state(p1_0);
    __wfi();
    // Goes to sleep here and returns to original caller

    // Reboots into bootloader which runs the code from the boot vector above.
    // When that function returns, it goes back to the place this function was called.
}


/**
 *  This code should be run from RAM with a stack pointer set by `do_sleep()`
 *  When this function exits, it should pop the return address of `do_sleep()`
 *  and return to where that function was called from.
 *  Because of this, the stack space reserved in this function should be the same
 *  as what was reserved in `do_sleep()`, in this case, none since we don't have
 *  any local variables.
 */
void exit_sleep() {

    init_peripherals();

}


/**
 * Pretend like this is useful work which depends on some global state.
 * Blink the LED in different patterns to show that it works.
 */
void do_work() {
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(led_on_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    sleep_ms(led_off_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sleep_ms(led_on_time);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}


int main() {
    // Enter here from normal reboot when powman reset vector is empty.

    init_peripherals();

    // Set and start the powman timer once
    powman_timer_set_1khz_tick_source_lposc();
    powman_timer_set_ms(0x123456);
    next_wakeup = 0x123456 + WAKEUP_INTERVAL;
    powman_timer_start();

    /*
     * Infinite loop to do work and sleep
     */
    while(1) {

        // useful work
        do_work();

        // Go to sleep for some time
        do_sleep();

    }

}

