#include "sim7020_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "log.h"

#include <string.h>

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#define PRINT_RAW_RCV_UART 1

#define UART_ID uart1
#define UART_TX_PIN 8
#define UART_RX_PIN 9

#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define SIM_PWR_PIN 28
#define SIM_EN_PIN 19
#define SIM_RESET_PIN 26

const uint sim_pwr_key = SIM_PWR_PIN;
const uint sim_en_key = SIM_EN_PIN;
const uint sim_reset_key = SIM_RESET_PIN;

#define MAX_UART_RESPONSE 128
typedef struct uart_response_s
{
    char response[MAX_UART_RESPONSE];
    uint32_t size;
} uart_response_t;

static uart_response_t current_response;

#define QUEUE_LENGTH 32
#define PKT_SIZE MAX_UART_RESPONSE
static QueueHandle_t uart_rx_packet_queue;

static void put_response_to_queue()
{

    if (xQueueSendFromISR(uart_rx_packet_queue,
                          (void *)&current_response,
                          (TickType_t)0) != pdPASS)
    {
        log_error("fail to add something to queue :lora_rx_packet_queue");
    }
}

bool wait_for_resp(uint32_t timeout_ms, char *expected_result)
{
    uart_response_t uart_response;
    uint32_t timeout_cnt = 0;
    while (true)
    {
        if (timeout_cnt > timeout_ms)
        {
            return false;
        }
        if (xQueueReceive(uart_rx_packet_queue, &(uart_response), pdTICKS_TO_MS(100)) == pdPASS)
        {

            if (strstr(uart_response.response, expected_result) != NULL)
            {
                return true;
            }
            log_info(uart_response.response);
        }
        timeout_cnt += 100;
    }
    log_error("wait_for_resp timeout");
    return false;
}

void on_uart_rx()
{
    static uint32_t uart_cnt = 0;
    while (uart_is_readable(UART_ID))
    {
        uint8_t ch = uart_getc(UART_ID);
#if PRINT_RAW_RCV_UART == 1
        printf("%c", ch);
#endif
        if (uart_cnt >= MAX_UART_RESPONSE)
        {
            log_error("uart recv overflow");
            uart_cnt = 0;
        }
        current_response.response[uart_cnt] = ch;
        if (ch == '\n')
        {
            current_response.response[uart_cnt] = '\0';
            current_response.size = uart_cnt;
            put_response_to_queue();
            memset(&current_response, 0, sizeof(current_response));
            uart_cnt = 0;
        }
        else
        {
            uart_cnt++;
        }
    }
}

static bool sim_uart_init()
{

    if (!uart_init(UART_ID, BAUD_RATE))
    {
        log_error("uart_init SIM7020 failed");
        return false;
    }
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    int __unused actual = uart_set_baudrate(UART_ID, BAUD_RATE);

    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
    return true;
}

static void sim_gpio_init()
{
    // Initialize LoRa enable pin
    gpio_init(sim_pwr_key);
    gpio_init(sim_en_key);
    gpio_init(sim_reset_key);

    gpio_set_dir(sim_pwr_key, GPIO_OUT);
    gpio_set_dir(sim_en_key, GPIO_OUT);
    gpio_set_dir(sim_reset_key, GPIO_OUT);

    gpio_put(sim_reset_key, false); // no reset
    gpio_put(sim_pwr_key, true);    // power key down

    sleep_ms(100);
    gpio_put(sim_en_key, true);
    sleep_ms(100);

    gpio_put(sim_pwr_key, false); // release power key
    sleep_ms(100);
}

static void enable_sim_module()
{

    gpio_put(sim_pwr_key, true); // press power key
    sleep_ms(2000);
    gpio_put(sim_pwr_key, false); // release power key
    sleep_ms(1000);
}

static bool send_cmd_check_recv(char *cmd, char *expected_result, uint32_t timeout)
{
    BaseType_t res = xQueueReset(uart_rx_packet_queue);
    if (res != pdPASS)
    {
        log_error("fail to reset queue: uart_rx_packet_queue");
    }
    uart_puts(UART_ID, cmd);
    if (!wait_for_resp(timeout, expected_result))
    {
        log_error("%s not found in response for %s", expected_result, cmd);
        return false;
    }
    return true;
}

static void set_apn()
{
    send_cmd_check_recv("AT+CFUN=0\r\n", "READY", 10000);
    send_cmd_check_recv("AT*MCGDEFCONT=\"IP\",\"iot.1nce.net\"\r\n", "OK", 10000);
    send_cmd_check_recv("AT+CFUN=1\r\n", "READY", 10000);
}

static void ntp_example()
{
    send_cmd_check_recv("AT\r\n", "OK", 1000);
    send_cmd_check_recv("AT+CMEE=2\r\n", "OK", 1000); // extended error report
    send_cmd_check_recv("AT+CPIN?\r\n", "READY", 1000);
    set_apn();
    uart_puts(UART_ID, "AT+CGCONTRDP\r\n");
    send_cmd_check_recv("AT+CGCONTRDP\r\n", "OK", 5000);

#if CHINESE_NTP == 1
    send_cmd_check_recv("AT+CSNTPSTART=\"jp.ntp.org.cn\",\"+32\"\r\n", "OK", 20000);
#else
    send_cmd_check_recv("AT+CSNTPSTART=\"pool.ntp.org\",\"+48\"\r\n", "+CSNTP:", 20000);
#endif

    send_cmd_check_recv("AT+CCLK?\r\n", "+CCLK:", 5000);
    send_cmd_check_recv("AT+CSNTPSTOP\r\n", "OK", 5000);
}

static void sim7020_task(void *pvParameters)
{
    memset(&current_response, 0, sizeof(current_response));
    log_info("sim7020_task started");
    sim_uart_init();
    log_info("sim uart initialized");
    sim_gpio_init();
    log_info("sim gpio initialized");
    enable_sim_module();
    log_info("sim enabled");
    ntp_example();
    while (true)
    {
        vTaskDelay(pdTICKS_TO_MS(1000));
    }
}

static void sim7020_task_responses(void *pvParameters)
{
    uart_response_t uart_response;
    while (true)
    {
        if (xQueueReceive(uart_rx_packet_queue, &(uart_response), 100) == pdPASS)
        {
            log_info(uart_response.response);
        }
    }
}

bool sim7020_task_init(void)
{

    uart_rx_packet_queue = xQueueCreate(QUEUE_LENGTH, sizeof(uart_response_t));
    if (!uart_rx_packet_queue)
    {
        log_error("create uart_rx_packet_queue  failed");
    }

    BaseType_t ret = xTaskCreate(sim7020_task,
                                 "SIM7020_TASK",
                                 1024 * 8,
                                 NULL,
                                 1,
                                 NULL);
    if (ret != pdPASS)
    {
        log_error("xTaskCreate failed: sending_task");
        return false;
    }

#if 0
    ret = xTaskCreate(sim7020_task_responses,
                      "SIM7020_TASK_RSP",
                      1024 * 8,
                      NULL,
                      1,
                      NULL);
#endif
    return true;
}