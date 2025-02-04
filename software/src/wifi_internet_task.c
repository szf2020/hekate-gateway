#include "internet_task_if.h"
#include "hekate_utils.h"
#include "time_ntp.h"
#include "free_rtos_memory.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "log.h"

#include "pico/cyw43_arch.h"

#define WIFI_TIMEOUT_CONNECT_MS 30000

static SemaphoreHandle_t wifi_init_done_sem;
static volatile bool time_set = false;

static struct udp_pcb *pcb_rxpt;
static struct udp_pcb *pcb_status;

TaskHandle_t wifi_task_handle;
#define WIFI_TASK_STACK_SIZE_WORDS 1024
StackType_t wifi_task_stack[WIFI_TASK_STACK_SIZE_WORDS];
StaticTask_t wifi_task_buffer;

static set_time_callback_t this_time_callback;

bool internet_task_send_udp(uint8_t *message, uint32_t size, const char *dst_ip, uint16_t port)
{
    ip_addr_t addr;
    if (!ipaddr_aton(dst_ip, &addr))
    {
        log_error("can not convert %s into ip address", dst_ip);
        return false;
    }
    else
    {
        log_info("LORA_LNS_IP: %s", dst_ip);
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    memcpy(p->payload, message, size);
    err_t er = udp_sendto(pcb_status, p, &addr, port);
    pbuf_free(p);
    if (er != ERR_OK)
    {
        log_error("Failed to send send_status_packet packet! error=%d", er);
    }
}

static void time_set_cb(struct tm time)
{
    ENSURE(this_time_callback);
    this_time_callback(time);
}

static void wifi_task(void *pvParameters)
{
    if (cyw43_arch_init())
    {
        log_error("fail: cyw43_arch_init");
    }
    cyw43_arch_enable_sta_mode();

    log_info("Connecting to Wi-Fi %s", WIFI_SSID);
#if SHOW_WIFI_PASSWORD
    log_info("Connecting to Wi-Fi Password %s", WIFI_PASSWORD);
#endif
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, WIFI_TIMEOUT_CONNECT_MS))
    {
        log_error("fail cyw43_arch_wifi_connect_timeout_ms");
    }
    else
    {
        log_info("WiFi connected");
    }

    log_info("start to get NTP Time");
    // time_npt_set_time(time_set_cb);
    pcb_status = udp_new();
    if (pcb_status == NULL)
    {
        log_error("udp_new failed: pcb_status");
    }
    else
    {
        log_info("udp_new for pcb_status successful");
    }
    pcb_rxpt = udp_new();
    if (pcb_rxpt == NULL)
    {
        log_error("udp_new failed: pcb_rxpt");
    }
    else
    {
        log_info("udp_new for pcb_rxpt successful");
    }
    xSemaphoreGive(wifi_init_done_sem);
    while (1)
    {
        cyw43_arch_poll();
        vTaskDelay(pdTICKS_TO_MS(1));
    }
    cyw43_arch_deinit();
}

bool internet_task_register_time_callback(set_time_callback_t time_callback)
{
    ENSURE_RET(time_callback, false);
    this_time_callback = time_callback;
}

bool internet_task_trigger_get_time(void)
{
    static bool wifi_initialized = false;

    if (!wifi_initialized)
    {
        if (xSemaphoreTake(wifi_init_done_sem, WIFI_TIMEOUT_CONNECT_MS) == pdTRUE)
        {
            wifi_initialized = true;
            time_npt_set_time(time_set_cb);
        }
        else
        {
            log_error("failed to take wifi_init_done_sem");
            return false;
        }
    }
    else
    {
        time_npt_set_time(time_set_cb);
    }

    return true;
}

void internet_task_print_task_stats(void)
{
    free_rtos_memory_print_usage(wifi_task_handle, "wifi_task", WIFI_TASK_STACK_SIZE_WORDS * sizeof(UBaseType_t));
}

bool internet_task_init(void)
{

    wifi_task_handle = xTaskCreateStatic(wifi_task,
                                         "WIFI_TASK",
                                         WIFI_TASK_STACK_SIZE_WORDS,
                                         NULL,
                                         1,
                                         wifi_task_stack,
                                         &wifi_task_buffer);
    if (wifi_task_handle == NULL)
    {
        log_error("xTaskCreate failed: wifi_task");
        return false;
    }

    wifi_init_done_sem = xSemaphoreCreateBinary();
    if (!wifi_init_done_sem)
    {
        log_error("xSemaphoreCreateBinary failed: wifi_init_done_sem");
    }
}