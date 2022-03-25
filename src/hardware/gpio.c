//
// Created by Lorenz on 18/10/2021.
//

#include <stdint.h>
#include <stdbool.h>
#include "../include/hardware/gpio.h"
#include "../include/hardware/resets.h"
#include "../include/hardware/irq.h"

void (* gpio_callback_functions [GPIO_COUNT * GPIO_EVENT_TYPE_COUNT]) (void);

/**
 * Disable the gpio subsystem
 */
void gpio_disable(void)
{
    reset_subsystem(RESETS_RESET_IO_BANK0 | RESETS_RESET_PAD_BANK0);
}

/**
 * Enable the gpio subsystem
 */
void gpio_enable(void)
{
    unreset_subsystem_wait(RESETS_RESET_IO_BANK0 | RESETS_RESET_PAD_BANK0);
}

/**
 * Assign a function to a gpio pin. See the GPIO function table (Table 289)
 *
 * @param gpio
 * @param function
 */
void gpio_set_function(uint8_t gpio, uint8_t function)
{
    pads_bank0_hw->gpio[gpio] &= ~PADS_BANK0_GPIO_OD;   // Clear output disable
    pads_bank0_hw->gpio[gpio] |= PADS_BANK0_GPIO_IE;    // Set input enable
    io_bank0_hw->io[gpio].ctrl = function;              // Clear all bits and set the FUNCSEL bits (4:0)
}

/**
 * Enable or disable the internal pullup
 *
 * @param gpio
 * @param enable
 */
void gpio_set_pullup(uint8_t gpio, bool enable) {
    if (enable) {
        pads_bank0_hw->gpio[gpio] |= PADS_BANK0_GPIO_PUE;
    }
    else {
        pads_bank0_hw->gpio[gpio] &= ~PADS_BANK0_GPIO_PUE;
    }
}

/**
 * Enable or disable the internal pulldown
 *
 * @param gpio
 * @param enable
 */
void gpio_set_pulldown(uint8_t gpio, bool enable) {
    if (enable) {
        pads_bank0_hw->gpio[gpio] |= PADS_BANK0_GPIO_PDE;
    }
    else {
        pads_bank0_hw->gpio[gpio] &= ~PADS_BANK0_GPIO_PDE;
    }
}

// TODO: move to iqr.c
extern void isr_irq13(void) {
    gpio_irq_handler();
}

/**
 * GPIO interrupt, call the relevant handlers
 */
void gpio_irq_handler(void) {
    for (int gpio = 0; gpio < GPIO_COUNT; gpio++) {
        uint32_t reg = io_bank0_hw->proc0_irq_ctrl.ints[gpio / 8];
        for (int i = 0; (i < 32) && reg; i++) {
            if (reg & 1ul){
                // Call the configured callback function
                gpio_callback_functions[gpio * i]();
            }
            reg >>= 1;
        }
        // Clear the interrupt
        io_bank0_hw->intr[gpio / 8] = reg;
    }
}

void gpio_set_irq_enabled(uint8_t gpio, gpio_irq_event_t event, void (*callback) (void)) {
    // Clear
    io_bank0_hw->intr[gpio / 8] = event << 4 * (gpio % 8);
    // Set callback function
    gpio_callback_functions[gpio * 5 + event] = callback;
    // Enable
    io_bank0_hw->proc0_irq_ctrl.inte[gpio / 8] |= (event << 4 * (gpio % 8));    //
    // Make sure io interrupts are enabled
    irq_set_enabled(IO_BANK0_BASE, true);

}

void gpio_set_irq_disabled(uint8_t gpio, gpio_irq_event_t event) {
    io_bank0_hw->proc0_irq_ctrl.inte[gpio / 8] &= ~(event << 4 * (gpio % 8));
}
