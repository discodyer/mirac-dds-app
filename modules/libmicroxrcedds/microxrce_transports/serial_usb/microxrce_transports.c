#include <uxr/client/transport.h>

#include <zephyr/kernel.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "microxrce_transports.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_DECLARE(DDS, LOG_LEVEL_INF);

#define RING_BUF_SIZE CONFIG_USB_CDC_ACM_RINGBUF_SIZE

char uart_in_buffer[RING_BUF_SIZE];
char uart_out_buffer[RING_BUF_SIZE];

struct ring_buf out_ringbuf, in_ringbuf;

static void uart_fifo_callback(const struct device *dev, void *user_data){ 
    ARG_UNUSED(user_data);
    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            int recv_len;
            char buffer[64];
            size_t len = MIN(ring_buf_space_get(&in_ringbuf), sizeof(buffer));

            if (len > 0){
                recv_len = uart_fifo_read(dev, buffer, len);
                ring_buf_put(&in_ringbuf, buffer, recv_len);
            }

        }

        if (uart_irq_tx_ready(dev)) {			
            char buffer[64];
            int rb_len;

            rb_len = ring_buf_get(&out_ringbuf, buffer, sizeof(buffer));

            if (rb_len == 0) {
                uart_irq_tx_disable(dev);
                continue;
            }

            uart_fifo_fill(dev, buffer, rb_len);
        }
    }
}

bool zephyr_transport_open(struct uxrCustomTransport * transport){
    zephyr_transport_params_t * params = (zephyr_transport_params_t*) transport->args;
    
    int ret;
    uint32_t baudrate, dtr = 0U;

    params->uart_dev = device_get_binding("CDC_XRCE_DDS");
    if (!params->uart_dev) {
        LOG_ERR("CDC ACM device not found.");
        return false;
    }

    ret = usb_enable(NULL);
    if (ret != 0 && ret != -EALREADY) {
        printk("Failed to enable USB\n");
        return false;
    }

    ring_buf_init(&out_ringbuf, sizeof(uart_out_buffer), uart_out_buffer);
    ring_buf_init(&in_ringbuf, sizeof(uart_in_buffer), uart_in_buffer);

    LOG_INF("Waiting for agent connection.");

    while (true) {
        uart_line_ctrl_get(params->uart_dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr) {
            break;
        } else {
            /* Give CPU resources to low priority threads. */
            k_sleep(K_MSEC(100));
        }
    }

    LOG_INF("Serial port connected!.");

    /* They are optional, we use them to test the interrupt endpoint */
    ret = uart_line_ctrl_set(params->uart_dev, UART_LINE_CTRL_DCD, 1);
    if (ret) {
        LOG_ERR("Failed to set DCD, ret code %d.", ret);
    }

    ret = uart_line_ctrl_set(params->uart_dev, UART_LINE_CTRL_DSR, 1);
    if (ret) {
        LOG_ERR("Failed to set DSR, ret code %d.", ret);
    }

    /* Wait 1 sec for the host to do all settings */
    k_busy_wait(1000*1000);

    ret = uart_line_ctrl_get(params->uart_dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
    if (ret) {
        LOG_ERR("Failed to get baudrate, ret code %d.", ret);
    }

    uart_irq_callback_set(params->uart_dev, uart_fifo_callback);

    /* Enable rx interrupts */
    uart_irq_rx_enable(params->uart_dev);

    return true;
}

bool zephyr_transport_close(struct uxrCustomTransport * transport){
    zephyr_transport_params_t * params = (zephyr_transport_params_t*) transport->args;
    (void)params;
    return true;
}

size_t zephyr_transport_write(struct uxrCustomTransport* transport, const uint8_t * buf, size_t len, uint8_t * err){
    zephyr_transport_params_t * params = (zephyr_transport_params_t*) transport->args;

    size_t wrote;
    
    wrote = ring_buf_put(&out_ringbuf, buf, len);
    
    if (wrote > 0) {
        uart_irq_tx_enable(params->uart_dev);
    }
    
    // Remove blocking wait - return immediately for non-blocking operation
    return wrote;
}

size_t zephyr_transport_read(struct uxrCustomTransport* transport, uint8_t* buf, size_t len, int timeout, uint8_t* err){
    zephyr_transport_params_t * params = (zephyr_transport_params_t*) transport->args;

    size_t read = 0;
    int spent_time_us = 0;
    const int timeout_us = timeout * 1000;  // Convert timeout to microseconds
    const int poll_interval_us = 100;       // 100us polling interval like ArduPilot

    while(ring_buf_is_empty(&in_ringbuf) && spent_time_us < timeout_us){
        k_usleep(poll_interval_us);
        spent_time_us += poll_interval_us;
    }

    uart_irq_rx_disable(params->uart_dev);
    read = ring_buf_get(&in_ringbuf, buf, len);
    uart_irq_rx_enable(params->uart_dev);

    return read;
}
