#include "web_server.h"
#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                       "<title>Modbus Diag</title></head><body>"
                       "<h1>Modbus Diagnostic Tool</h1>"
                       "<p>API endpoints:</p>"
                       "<ul>"
                       "<li><a href='/report'>/report</a> - Diagnostic report</li>"
                       "<li><a href='/data'>/data</a> - Export data</li>"
                       "</ul>"
                       "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t report_handler(httpd_req_t *req)
{
    /* Serve the latest report from SPIFFS */
    FILE *f = fopen("/spiffs/report.json", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No report available");
        return ESP_FAIL;
    }
    /* Use chunked transfer for large reports */
    char buf[512];
    httpd_resp_set_type(req, "application/json");
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, len);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  /* End chunked response */
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t report = { .uri = "/report", .method = HTTP_GET, .handler = report_handler };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &report);

    ESP_LOGI(TAG, "Web server started on port %d", WEB_SERVER_PORT);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
