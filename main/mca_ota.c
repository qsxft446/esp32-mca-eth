#include "mca_ota.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

/* Заголовок образа, заголовок первого сегмента и описание приложения
 * идут в файле подряд - их проверяем до того, как писать во флеш. */
#define OTA_HDR_LEN (sizeof(esp_image_header_t) + \
                     sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
#define OTA_BUF     4096

static bool s_busy;

/* Причина последнего сброса: по сети иначе не отличить сбой прошивки
 * (panic, сторожевой таймер, просадка питания) от выдернутого USB. */
static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "ext";
    case ESP_RST_SW:        return "sw";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "unknown";
    }
}

static const char *state_name(esp_ota_img_states_t st)
{
    switch (st) {
    case ESP_OTA_IMG_NEW:            return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
    case ESP_OTA_IMG_VALID:          return "valid";
    case ESP_OTA_IMG_INVALID:        return "invalid";
    case ESP_OTA_IMG_ABORTED:        return "aborted";
    default:                         return "undefined";
    }
}

void mca_ota_init(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_app_desc_t *d = esp_app_get_description();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(run, &st);
    ESP_LOGI(TAG, "прошивка %s от %s %s, слот %s (%s), сброс: %s", d->version,
             d->date, d->time, run->label, state_name(st),
             reset_name(esp_reset_reason()));
    /* Загрузчик откатывает молча - след остаётся только здесь. */
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    if (bad)
        ESP_LOGW(TAG, "прошивка в слоте %s не подтвердилась и была "
                 "заменена прежней (откат)", bad->label);
}

void mca_ota_confirm(void)
{
    static bool done;
    if (done) return;
    done = true;
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(run, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        if (e == ESP_OK)
            ESP_LOGI(TAG, "новая прошивка подтверждена, отката не будет");
        else
            ESP_LOGE(TAG, "подтвердить прошивку не удалось: %s",
                     esp_err_to_name(e));
    }
}

/* Ответ: {"ok":..,"msg":"по-русски","en":"in English"} */
static esp_err_t reply(httpd_req_t *r, const char *status, bool ok,
                       const char *msg, const char *en)
{
    char buf[400];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":%s,\"msg\":\"%s\",\"en\":\"%s\"}",
                     ok ? "true" : "false", msg, en);
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

/* Принять ровно len байт тела запроса. Таймаут сокета повторяем:
 * страница шлёт файл без пауз, но сеть может придержать пакет. */
static int recv_all(httpd_req_t *r, char *buf, int len)
{
    int got = 0, retries = 0;
    while (got < len) {
        int n = httpd_req_recv(r, buf + got, len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++retries < 5) continue;
        if (n <= 0) return -1;
        got += n;
        retries = 0;
    }
    return got;
}

static esp_err_t h_ota_get(httpd_req_t *r)
{
    mca_ota_confirm();
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t *d = esp_app_get_description();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(run, &st);
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"ver\":\"%s\",\"date\":\"%s\",\"time\":\"%s\",\"idf\":\"%s\","
        "\"run\":\"%s\",\"next\":\"%s\",\"size\":%lu,\"state\":\"%s\","
        "\"reset\":\"%s\",\"up\":%llu}",
        d->version, d->date, d->time, d->idf_ver, run->label,
        next ? next->label : "", next ? (unsigned long)next->size : 0ul,
        state_name(st), reset_name(esp_reset_reason()),
        (unsigned long long)(esp_timer_get_time() / 1000000));
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

/* Проверка до записи: образ для ESP32-S3 и это та же программа (имя
 * проекта), а не случайный .bin - бутлоадер или чужая прошивка.
 * Целостность всего образа (контрольная сумма, SHA-256) проверит
 * esp_ota_end в конце. */
static bool header_ok(const uint8_t *b, const char **msg, const char **en)
{
    const esp_image_header_t *ih = (const esp_image_header_t *)b;
    const esp_app_desc_t *ad = (const esp_app_desc_t *)(b +
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (ih->magic != ESP_IMAGE_HEADER_MAGIC) {
        *msg = "это не образ прошивки ESP32 (нужен build/esp32_mca.bin)";
        *en  = "not an ESP32 firmware image (build/esp32_mca.bin is needed)";
        return false;
    }
    if (ih->chip_id != ESP_CHIP_ID_ESP32S3) {
        *msg = "образ собран не для ESP32-S3";
        *en  = "the image is not built for ESP32-S3";
        return false;
    }
    if (ad->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(ad->project_name, esp_app_get_description()->project_name,
                sizeof(ad->project_name)) != 0) {
        *msg = "это другая программа, не прошивка MCA";
        *en  = "this is a different program, not the MCA firmware";
        return false;
    }
    return true;
}

static esp_err_t h_ota_post(httpd_req_t *r)
{
    if (s_busy)
        return reply(r, "409 Conflict", false, "обновление уже идёт",
                     "an update is already in progress");
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part)
        return reply(r, "500 Internal Server Error", false,
                     "нет слота для обновления: прибор прошит со старой "
                     "разметкой, нужна прошивка по USB",
                     "no update slot: the device has the old partition "
                     "table, flash it over USB once");
    const int total = r->content_len;
    if (total < (int)OTA_HDR_LEN || total > (int)part->size)
        return reply(r, "400 Bad Request", false,
                     "размер файла не подходит для прошивки",
                     "the file size does not fit a firmware image");

    char *buf = malloc(OTA_BUF);
    if (!buf)
        return reply(r, "500 Internal Server Error", false,
                     "нет памяти под буфер", "out of memory for the buffer");
    s_busy = true;
    const char *msg = NULL, *en = NULL;
    esp_ota_handle_t h = 0;
    bool begun = false;
    esp_err_t e;
    int done = OTA_HDR_LEN, len = OTA_HDR_LEN;

    if (recv_all(r, buf, OTA_HDR_LEN) < 0) {
        msg = "связь прервалась при приёме файла";
        en  = "connection lost while receiving the file";
        goto fail;
    }
    if (!header_ok((const uint8_t *)buf, &msg, &en)) goto fail;

    /* Стирание по ходу записи: не держать флеш секунды на стирании
     * всего слота разом - обработка на ядре 1 стоит, пока идёт
     * операция с флешем. */
    e = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(e));
        msg = "не удалось начать запись во флеш";
        en  = "could not start writing to flash";
        goto fail;
    }
    begun = true;
    ESP_LOGW(TAG, "приём прошивки: %d байт в слот %s", total, part->label);

    for (;;) {
        e = esp_ota_write(h, buf, len);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(e));
            msg = "ошибка записи во флеш";
            en  = "flash write error";
            goto fail;
        }
        if (done >= total) break;
        len = total - done < OTA_BUF ? total - done : OTA_BUF;
        if (recv_all(r, buf, len) < 0) {
            msg = "связь прервалась при приёме файла";
            en  = "connection lost while receiving the file";
            goto fail;
        }
        done += len;
    }

    begun = false;
    e = esp_ota_end(h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(e));
        msg = e == ESP_ERR_OTA_VALIDATE_FAILED
            ? "образ повреждён: не сошлась контрольная сумма"
            : "не удалось завершить запись";
        en  = e == ESP_ERR_OTA_VALIDATE_FAILED
            ? "the image is corrupted: checksum mismatch"
            : "could not finish writing";
        goto fail;
    }
    e = esp_ota_set_boot_partition(part);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(e));
        msg = "не удалось выбрать новый слот для загрузки";
        en  = "could not select the new slot for boot";
        goto fail;
    }
    free(buf);
    ESP_LOGW(TAG, "прошивка записана в %s, перезапуск", part->label);
    reply(r, "200 OK", true,
          "прошивка записана, прибор перезапускается",
          "firmware written, the device is restarting");
    /* ответ должен уйти до перезапуска */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;

fail:
    if (begun) esp_ota_abort(h);
    free(buf);
    s_busy = false;
    ESP_LOGE(TAG, "обновление не выполнено: %s", msg);
    /* остаток тела не дочитан - соединение закрываем */
    httpd_resp_set_hdr(r, "Connection", "close");
    return reply(r, "400 Bad Request", false, msg, en);
}

void mca_ota_register(httpd_handle_t srv)
{
    static const httpd_uri_t get = {
        .uri = "/ota", .method = HTTP_GET, .handler = h_ota_get,
    };
    static const httpd_uri_t post = {
        .uri = "/ota", .method = HTTP_POST, .handler = h_ota_post,
    };
    esp_err_t e1 = httpd_register_uri_handler(srv, &get);
    esp_err_t e2 = httpd_register_uri_handler(srv, &post);
    if (e1 != ESP_OK || e2 != ESP_OK)
        ESP_LOGE(TAG, "обработчики /ota не зарегистрированы: %s / %s",
                 esp_err_to_name(e1), esp_err_to_name(e2));
}
