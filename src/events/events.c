//
// Created by Lorenz on 4/03/2022.
//
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/events/events.h"
#include "../include/ui/ui.h"

event_t events_queue[EVENT_QUEUE_SIZE];
int event_queue_front;
int event_queue_rear;
int event_items;

/**
 * Initializes and empties the event queue
 */
void event_init_queue(void) {
    event_items = 0;
    event_queue_front = -1;
    event_queue_rear = -1;
    memset(events_queue, 0, EVENT_QUEUE_SIZE);
}

/**
 * Adds an event to the queue
 *
 * @param event_type
 * @param callback event handler
 */
void event_add(event_type_t event_type, void (* callback) (void)) {
    event_enqueue((event_t) {event_type, callback});
}

/**
 * Runs all the events in the queue.
 * This function should be called periodically
 *
 * TODO: Add max duration?
 */
void event_task(void) {
    event_t event;
    while (!event_queue_is_empty()) {
        event = event_dequeue();
        switch (event.event_type) {
            // TODO: Use a subscribe model instead?
            case event_usb_device_attached:
                break;
            case event_usb_device_detached:
                ui_data_changed_event_handler(ui_data_source_keyboard);
                break;
            case event_usb_host_hid_load_driver:
                ui_data_changed_event_handler(ui_data_source_keyboard);
                break;
            case event_usb_host_hid_report_available:
                break;
            case event_storage_initialized:
                ui_data_changed_event_handler(ui_data_source_storage);
                break;
            case event_sd_card_disconnected:
                ui_data_changed_event_handler(ui_data_source_storage);
                break;
            case event_sd_card_released:
                ui_data_changed_event_handler(ui_data_source_storage);
                break;
            case event_storage_block_written:
                ui_data_changed_event_handler(ui_data_source_storage);
                break;
        }
        if (event.callback != NULL) {
            event.callback();
        }
    }
}

/**
 * Adds an event in front of the queue
 *
 * @param event
 */
static void event_enqueue(event_t event) {
    if (!event_queue_is_full()) {
        // Update front index
        if (++event_queue_front >= EVENT_QUEUE_SIZE) {
            event_queue_front = 0;
        }
        event_items++;
        // Add the event to the front of the queue
        events_queue[event_queue_front] = event;
    }
    else {
        // Shouldn't happen
        // Increase the queue size if this happens
    }
}

/**
 * Returns and removes the last item from the queue
 *
 * @return
 */
static event_t event_dequeue(void) {
    if (!event_queue_is_empty()) {
        // Update rear index
        if(++event_queue_rear >= EVENT_QUEUE_SIZE) {
            event_queue_rear = 0;
        }
        event_items--;
        return events_queue[event_queue_rear];
    }

    return (event_t) {-1, NULL};
}

/**
 * Checks if the queue is full
 *
 * @return
 */
static inline bool event_queue_is_full(void) {
    return event_items >= EVENT_QUEUE_SIZE - 1;
}

/**
 * Checks if the queue is empty
 *
 * @return
 */
static inline bool event_queue_is_empty(void) {
    return event_items <= 0;
}

