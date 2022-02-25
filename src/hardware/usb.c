//
// Created by Lorenz on 20/02/2022.
//

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/lib/math.h"
#include "../include/hardware/usb.h"
#include "../include/hardware/resets.h"
#include "../include/hardware/irq.h"
#include "../include/hardware/timer.h"
#include "../include/hardware/uart.h"   // TODO: remove temp import

struct endpoint_struct endpoints[16];
static uint8_t usb_ctrl_buffer[256];
usb_device_t usb_device;    // Only one usb device

bool dev_con = false; // TODO: remove temp var

// TODO: remove temp function
void dev_connected(void){
    if(dev_con){
        dev_con = false;
        usb_enum_device(&usb_device);
    }
}

/**
 * Initializes the USB controller in host mode
 */
void usb_init(usb_device_t * device) {
    // Reset USB controller
    reset_subsystem(RESETS_RESET_USBCTRL);
    unreset_subsystem_wait(RESETS_RESET_USBCTRL);

    // Clear dpsram
    memset(usb_host_dpsram, 0, sizeof(*usb_host_dpsram));

    // Init endpoints
    usb_init_endpoints();

    // Connect the controller to the USB PHY
    usb_hw->usb_muxing = USB_MUXING_TO_PHY_BIT | USB_MUXING_SOFTCON_BIT;

    // Force VBUS detect
    usb_hw->usb_pwr = USB_PWR_VBUS_DETECT_BIT | USB_PWR_VBUS_DETECT_OVERRIDE_EN_BIT;

    // Setup irq handler
    //irq_set_handler(USBCTRL_IRQ, usb_irq);

    // Enable host mode
    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BIT | USB_MAIN_CTRL_HOST_NDEVICE_BIT;
    usb_hw->sie_ctrl =  USB_SIE_CTRL_KEEP_ALIVE_EN_BIT |
                        USB_SIE_CTRL_SOF_EN_BIT |
                        USB_SIE_CTRL_PULLDOWN_EN_BIT |
                        USB_SIE_CTRL_EP0_INT_1BUF_BIT;
    usb_hw->inte = USB_INTE_BUFF_STATUS_BIT      |
                   USB_INTE_HOST_CONN_DIS_BIT    |
                   USB_INTE_HOST_RESUME_BIT      |
                   USB_INTE_STALL_BIT            |
                   USB_INTE_TRANS_COMPLETE_BIT   |
                   USB_INTE_ERROR_RX_TIMEOUT_BIT |
                   USB_INTE_ERROR_DATA_SEQ_BIT   ;

    // Enable USB_CTRL Interrupt request
    irq_set_enabled(USBCTRL_IRQ, true);
}

static void usb_init_endpoints(void) {
    // Endpoint 0 (epx)
    endpoints[0].buffer_control = &usb_host_dpsram->epx_buff_ctrl;
    endpoints[0].endpoint_control = &usb_host_dpsram->epx_ctrl;
    endpoints[0].dps_data_buffer = &usb_host_dpsram->data_buffers[0];
    endpoints[0].interrupt_number = 0;

    // Interrupt endpoints
    for (int i = 1; i < 16; i++) {
        endpoints[i].buffer_control = &usb_host_dpsram->int_ep_buff_ctrl[i - 1].ctrl;
        endpoints[i].endpoint_control = &usb_host_dpsram->int_ep_ctrl[i - 1].ctrl;
        endpoints[i].dps_data_buffer = &usb_host_dpsram->data_buffers[64 * (i)];
        endpoints[i].interrupt_number = i;
    }
}

/**
 * USB IRQ handler
 */
void usb_irq(void) {
    uint32_t status = usb_hw->ints;

    // Device connected or disconnected
    if (status & USB_INTS_HOST_CONN_DIS_BIT) {
        if (device_speed()) {
            // USB device connected
            usb_device_attach(&usb_device);
        }
        else {
            // USB device disconnected
            usb_device_detach(&usb_device);
        }
        // Clear interrupt
        usb_hw->sie_status = USB_SIE_STATUS_SPEED_BITS;
    }

    if (status & USB_INTS_TRANS_COMPLETE_BIT)
    {
        handle_transfer_complete();
        usb_hw->sie_status = USB_SIE_STATUS_TRANS_COMPLETE_BIT;
    }

    if (status & USB_INTS_BUFF_STATUS_BIT)
    {
        usb_handle_buff_status();
    }

    if (status & USB_INTS_STALL_BIT)
    {
        // TODO: handle STALL irq
        uart_puts(uart0_hw, "stall");
        usb_hw->sie_status = USB_SIE_STATUS_STALL_REC_BIT;
    }

    if (status & USB_INTS_ERROR_RX_TIMEOUT_BIT)
    {
        // TODO: handle Timeout IRQ
        uart_puts(uart0_hw, "rx_timeout");
        usb_hw->sie_status = USB_SIE_STATUS_RX_TIMEOUT_BIT;
    }

    if (status & USB_INTS_ERROR_DATA_SEQ_BIT)
    {
        // TODO: handle data seq error
        uart_puts(uart0_hw, "data_seq");
        usb_hw->sie_status = USB_SIE_STATUS_DATA_SEQ_ERROR_BIT;
    }
}

extern void isr_irq5(void) {
    usb_irq();
}

/**
 * Device speed. Disconnected = 0, LS= 1, FS = 2
 *
 * @return Device speed
 */
static inline dev_speed_t device_speed(void) {
    return (usb_hw->sie_status & USB_SIE_STATUS_SPEED_BITS) >> USB_SIE_STATUS_SPEED_LSB;
}

/**
 * USB device attached event
 *
 * @param device
 */
void usb_device_attach(usb_device_t * device) {
    dev_con = true;
}

/**
 * USB device detached event
 *
 * @param device
 */
void usb_device_detach(usb_device_t * device) {
    dev_con = false;
}

/**
 * Enumerate and setup the USB device
 *
 * @param device
 */
void usb_enum_device(usb_device_t * device) {
    // Wait min 50 ms reset and wait again
    wait_ms(USB_RESET_DELAY);
    usb_reset_bus();
    wait_ms(USB_RESET_DELAY);

    //device->speed = device_speed();
    if (device_speed() == usb_disconnected) return; // Disconnected while waiting?

    // Get first 8 bytes of device descriptor to get endpoint 0 size
    usb_setup_data_t setup_request = (usb_setup_data_t) {
        .bm_request_type_bits = { .recipient = usb_bm_request_type_recipient_device,
                                  .type = usb_bm_request_type_type_standard,
                                  .direction = usb_bm_request_type_direction_dev_to_host
                                  },
        .b_request = usb_setup_req_b_req_type_get_descriptor,
        .w_value = usb_descriptor_types_device << 8,
        .w_index = 0,
        .w_length = 8
    };
    usb_send_control_transfer(0, &setup_request, usb_ctrl_buffer);
    device->max_packet_size_ep_0 = ((usb_device_descriptor_t *) usb_ctrl_buffer)->b_max_packet_size_0;
    endpoints[0].w_max_packet_size = device->max_packet_size_ep_0;  // Only one device, so set max_packet_size in endpoint struct

    // Reset the bus and wait
    usb_reset_bus();
    wait_ms(USB_RESET_DELAY);

    // Set new address
    setup_request = (usb_setup_data_t) {
            .bm_request_type_bits = { .recipient = usb_bm_request_type_recipient_device,
                    .type = usb_bm_request_type_type_standard,
                    .direction = usb_bm_request_type_direction_host_to_dev
            },
            .b_request = usb_setup_req_b_req_type_set_address,
            .w_value = USB_DEVICE_ADDRESS,
            .w_index = 0,
            .w_length = 0
    };
    usb_send_control_transfer(0, &setup_request, NULL);
    device->address = USB_DEVICE_ADDRESS;


    // Get full device descriptor
    setup_request = (usb_setup_data_t) {
            .bm_request_type_bits = { .recipient = usb_bm_request_type_recipient_device,
                    .type = usb_bm_request_type_type_standard,
                    .direction = usb_bm_request_type_direction_dev_to_host
            },
            .b_request = usb_setup_req_b_req_type_get_descriptor,
            .w_value = usb_descriptor_types_device << 8,
            .w_index = 0,
            .w_length = 18
    };
    usb_send_control_transfer(device->address, &setup_request, usb_ctrl_buffer);
    device->product_id = ((usb_device_descriptor_t *) usb_ctrl_buffer)->id_product;
    device->vendor_id = ((usb_device_descriptor_t *) usb_ctrl_buffer)->id_vendor;
    device->configuration_count = ((usb_device_descriptor_t *) usb_ctrl_buffer)->b_num_configurations;

    // Get first 9 bytes of configuration descriptor
    setup_request = (usb_setup_data_t) {
            .bm_request_type_bits = { .recipient = usb_bm_request_type_recipient_device,
                    .type = usb_bm_request_type_type_standard,
                    .direction = usb_bm_request_type_direction_dev_to_host
            },
            .b_request = usb_setup_req_b_req_type_get_descriptor,
            .w_value = (usb_descriptor_types_configuration << 8) | (USB_CONFIGURATION_NUMBER - 1),
            .w_index = 0,
            .w_length = 9
    };
    usb_send_control_transfer(device->address, &setup_request, usb_ctrl_buffer);
    device->interface_count = ((usb_configuration_descriptor_t *) usb_ctrl_buffer)->b_num_interfaces;
    // TODO: get full config descriptor?

    // Set configured
    setup_request = (usb_setup_data_t) {
            .bm_request_type_bits = { .recipient = usb_bm_request_type_recipient_device,
                    .type = usb_bm_request_type_type_standard,
                    .direction = usb_bm_request_type_direction_host_to_dev
            },
            .b_request = usb_setup_req_b_req_type_set_configuration,
            .w_value = USB_CONFIGURATION_NUMBER,
            .w_index = 0,
            .w_length = 0
    };
    usb_send_control_transfer(device->address, &setup_request, NULL);

    // TODO: parse configuration and  each interface
}

/**
 * Resets the usb bus
 */
static void usb_reset_bus(void) {
    usb_hw->sie_ctrl |= USB_SIE_CTRL_RESET_BUS_BIT;
}

/**
 * Send a setup packet with the given data
 *
 * @param device_address
 * @param setup_packet
 */
static void usb_setup_send(uint8_t device_address, usb_setup_data_t *setup_packet) {

    // Copy the packet into the setup packet buffer
    memcpy((void*)usb_host_dpsram->setup_packet, setup_packet, 8);

    // Configure endpoint 0 with setup info
    struct endpoint_struct * endpoint = &endpoints[0];
    usb_endpoint_init(endpoint, device_address, 0, 0, endpoint->w_max_packet_size, usb_data_flow_types_control_transfer, 0);
    endpoint->total_len = 8;
    endpoint->transfer_size = 8;
    endpoint->active = true;
    endpoint->setup = true;

    // Set device address
    usb_hw->dev_addr_ep_ctrl = device_address;

    // Send setup
    usb_hw->sie_ctrl =  USB_SIE_CTRL_KEEP_ALIVE_EN_BIT |
                        USB_SIE_CTRL_SOF_EN_BIT |
                        USB_SIE_CTRL_PULLDOWN_EN_BIT |
                        USB_SIE_CTRL_EP0_INT_1BUF_BIT |
                        USB_SIE_CTRL_SEND_SETUP_BIT |
                        USB_SIE_CTRL_START_TRANS_BIT;
}

/**
 * Send a control transfer
 *
 * @param device_address
 * @param setup_packet
 * @param data
 */
static void usb_send_control_transfer(uint8_t device_address, usb_setup_data_t *setup_packet, uint8_t * data) {
    // Use endpoint 0 for control transfers
    struct endpoint_struct * endpoint = &endpoints[0];

    // Send setup packet
    usb_setup_send(device_address, setup_packet);
    while(endpoint->active);

    // Data stage
    if (setup_packet->w_length) {
        usb_endpoint_transfer(device_address, endpoint, 0, data, setup_packet->w_length, setup_packet->bm_request_type_bits.direction);
        while(endpoint->active);
    }

    // Status stage
    usb_endpoint_transfer(device_address, endpoint, 0, NULL, 0, 1 - setup_packet->bm_request_type_bits.direction);
    while(endpoint->active);

    // TODO: Check if failed or stalled

}

/**
 * Start a transaction
 *
 * @param device_address
 * @param endpoint
 * @param endpoint_number
 * @param buffer
 * @param buffer_len
 * @param direction
 */
static void usb_endpoint_transfer(uint8_t device_address, struct endpoint_struct *endpoint, uint16_t endpoint_number,uint8_t * buffer, uint16_t buffer_len, uint8_t direction) {
    // Endpoint init
    usb_endpoint_init(endpoint, device_address, endpoint_number, direction, endpoint->w_max_packet_size, endpoint->transfer_type, 0);

    endpoint->len = 0;
    endpoint->total_len = buffer_len;
    endpoint->transfer_size = min(buffer_len, max(endpoint->w_max_packet_size, 64));
    endpoint->mem_data_buffer = buffer;

    // Check if it is the last buffer
    endpoint->last_buffer = (endpoint->len + endpoint->transfer_size == endpoint->total_len);
    endpoint->buffer_selector = 0;

    usb_endpoint_transfer_buffer(endpoint);

    // If endpoint 0 (non interrupt)
    if (endpoint->interrupt_number == 0){
        usb_hw->dev_addr_ep_ctrl = device_address | (endpoint_number << USB_ADDR_ENDP_ENDPOINT_LSB);
        usb_hw->sie_ctrl = USB_SIE_CTRL_KEEP_ALIVE_EN_BIT |
                           USB_SIE_CTRL_SOF_EN_BIT |
                           USB_SIE_CTRL_PULLDOWN_EN_BIT |
                           USB_SIE_CTRL_EP0_INT_1BUF_BIT |
                           USB_SIE_CTRL_START_TRANS_BIT |
                           (direction ? USB_SIE_CTRL_RECEIVE_DATA_BIT : USB_SIE_CTRL_SEND_DATA_BIT);
    }
}

/**
 * Setup the buffer control register
 *
 * @param endpoint
 */
static void usb_endpoint_transfer_buffer(struct endpoint_struct * endpoint) {
    uint32_t buffer_control_val = endpoint->transfer_size | USB_BUFF_CTRL_AVAILABLE_0_BIT;

    if (endpoint->receive == false) {
        // Copy data from the temp buffer in mem to the hardware buffer
        memcpy(endpoint->dps_data_buffer, &endpoint->mem_data_buffer[endpoint->len], endpoint->transfer_size);
        buffer_control_val |= USB_BUFF_CTRL_BUFF0_FULL_BIT;
    }

    buffer_control_val |= endpoint->pid ? USB_BUFF_CTRL_BUFF0_DATA_PID_BIT : 0;
    // TODO: toggle pid
    endpoint->pid ^= 1;

    if (endpoint->last_buffer)
        buffer_control_val |= USB_BUFF_CTRL_BUFF0_LAST_BIT;

    *(endpoint->buffer_control) = buffer_control_val;
}

/**
 * Init an endpoint with the given data
 *
 * @param endpoint
 * @param device_address
 * @param endpoint_number
 * @param direction 1 = RX / IN, 0 = TX / OUT
 * @param w_max_packet_size
 * @param transfer_type
 * @param b_interval
 */
static void usb_endpoint_init(struct endpoint_struct *endpoint, uint8_t device_address, uint8_t endpoint_number, uint8_t direction, uint16_t w_max_packet_size, usb_data_flow_types_t transfer_type, uint8_t b_interval) {
    endpoint->device_address = device_address;
    endpoint->endpoint_number = endpoint_number;

    endpoint->receive = direction;

    endpoint->pid = (endpoint_number == 0) ? 1u : 0u; // TODO: check (from _hw_endpoint_init())
    endpoint->w_max_packet_size = w_max_packet_size;
    endpoint->transfer_type = transfer_type;

    endpoint->active = true;


    uint32_t dpsram_offset = (uintptr_t)endpoint->dps_data_buffer ^ (uintptr_t)usb_host_dpsram;

    // Endpoint type in the ep ctrl reg uses a different numbering
    uint8_t endpoint_control_reg_transfer_type;
    switch (transfer_type) {
        case usb_data_flow_types_control_transfer:
            endpoint_control_reg_transfer_type = 0;
            break;
        case usb_data_flow_types_bulk_transfer:
            endpoint_control_reg_transfer_type = 2;
            break;
        case usb_data_flow_types_interrupt_transfer:
            endpoint_control_reg_transfer_type = 3;
            break;
        case usb_data_flow_types_isochronous_transfer:
            endpoint_control_reg_transfer_type = 1;
            break;
    }

    *(endpoint->endpoint_control) = USB_EP_CTRL_ENABLE_BIT |
                                    USB_EP_CTRL_INT_BUFFER_BIT |
                                    (endpoint_control_reg_transfer_type << USB_EP_CTRL_TYPE_LSB) |
                                    (dpsram_offset) |
                                    (b_interval ? (b_interval - 1) << USB_EP_CTRL_HOST_INT_INTERVAL_LSB : 0);

    if (b_interval) {
        // This is an interrupt endpoint
        // Set up interrupt endpoint address control register:
        usb_hw->host_int_ep_addr_ctrl[endpoint->interrupt_number] = device_address | (endpoint_number << USB_ADDR_ENDPN_ENDPOINT_LSB);

        // Finally, enable interrupt that endpoint
        usb_hw->int_ep_ctrl = 1 << endpoint->interrupt_number;
    }
}

/**
 * Handle buff_status irq for each endpoint (0-15)
 */
static void usb_handle_buff_status(void) {
    uint32_t completed_buffers = usb_hw->buff_status;

    for (int i = 0; i < 16; i++) {
        uint32_t bit = 1ul << (i*2);
        if (completed_buffers & bit) {
            usb_hw->buff_status = bit;
            bool done = usb_endpoint_transfer_continue(&endpoints[i]);
            if (done) {
                usb_endpoint_transfer_complete(&endpoints[i]);
            }
        }
    }
}

/**
 * Checks if a transaction is complete and prepare for a potential next
 *
 * @param endpoint
 * @return true: transaction is complete, false: not done yet
 */
static bool usb_endpoint_transfer_continue(struct endpoint_struct *endpoint) {
    // Get hardware buffer state and extract the amount of transferred bytes
    uint32_t buffer_ctrl = *(endpoint->buffer_control);
    uint16_t transferred_bytes = buffer_ctrl & USB_BUFF_CTRL_BUFF0_TRANSFER_LENGTH_BITS;

    // RP2040-E4 bug
    // Summary:     USB host writes to upper half of buffer status in single buffered mode
    // Workaround:  Shift endpoint control register to the right by 16 bits if the buffer selector is BUF1. You can use
    //              BUFF_CPU_SHOULD_HANDLE find the value of the buffer selector when the buffer was marked as done.
    if (endpoint->buffer_selector == 1) {
        buffer_ctrl = buffer_ctrl >> 16;
        *(endpoint->buffer_control) = buffer_ctrl;
    }
    endpoint->buffer_selector ^= 1ul;   // Flip buffer selector

    // Update the bytes sent or received
    if (endpoint->receive == false) {
        // Update bytes sent
        endpoint->len += transferred_bytes;
    }
    else {
        // Copy received data to the memory buffer at the correct index
        memcpy(&endpoint->mem_data_buffer[endpoint->len], endpoint->dps_data_buffer, transferred_bytes);
        endpoint->len += transferred_bytes;
    }

    // Check if less data is sent than the transfer size
    if (endpoint->receive && transferred_bytes < endpoint->transfer_size) {
        // Update the total length
        endpoint->total_len = endpoint->len;
    }

    uint16_t remaining_bytes = endpoint->total_len - endpoint->len;
    endpoint->transfer_size = min(remaining_bytes, max(endpoint->w_max_packet_size, 64));
    endpoint->last_buffer = (endpoint->len + endpoint->transfer_size == endpoint->total_len);

    // Done
    if (remaining_bytes == 0) {
        usb_endpoint_transfer_buffer(endpoint);
        return true;
    }

    // Not done yet
    return false;
}

/**
 * Resets the given endpoint
 *
 * @param endpoint
 */
static void usb_endpoint_transfer_complete(struct endpoint_struct *endpoint) {
    // Reset endpoint
    endpoint->total_len = 0;
    endpoint->len = 0;
    endpoint->transfer_size = 0;
    endpoint->mem_data_buffer = 0;
    endpoint->active = false;
    endpoint->setup = false;
}

/**
 * Handle transfer_complete IRQ
 */
static void handle_transfer_complete(void) {
    struct endpoint_struct * endpoint = &endpoints[0];
    if (endpoint->setup) {
        usb_endpoint_transfer_complete(endpoint);
    }
}