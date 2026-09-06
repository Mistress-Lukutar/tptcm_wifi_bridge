/**
 * @file   wifi_manager.c
 * @brief  WiFi connection manager with NVS credentials and AP provisioning
 *         portal (HTTP + wildcard DNS captive portal)
 * @author Mistress-Lukutar
 * @date   2026-09-03
 * @version v1.0.0
 */

/* Includes ------------------------------------------------------------------*/
#include "wifi_manager.h"
#include "main.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "sdkconfig.h"

/* Private defines -----------------------------------------------------------*/
#define WM_NVS_NAMESPACE "tptcm"    /**< NVS namespace for credentials */
#define WM_NVS_KEY_SSID "ssid"      /**< NVS key: network name */
#define WM_NVS_KEY_PASS "pass"      /**< NVS key: network password */
#define WM_MAX_SSID_LEN 32          /**< IEEE 802.11 SSID limit */
#define WM_MAX_PASS_LEN 64          /**< WPA2 passphrase limit */
#define WM_FORM_BUF_SIZE 256        /**< Max provisioning POST body size */
#define WM_DNS_PORT 53              /**< Captive portal DNS port */
#define WM_DNS_BUF_SIZE 512         /**< DNS packet buffer size */
#define WM_AP_IP "192.168.4.1"      /**< Provisioning AP address */
#define WM_STA_RETRY_PERIOD_US \
  (60U * 1000U * 1000U) /**< STA background retry period in provisioning */
#define WM_RESTART_DELAY_MS 1500    /**< Delay before reboot after save */

/* Private variables ---------------------------------------------------------*/
static const char* s_tag = "WIFI_MANAGER";

static char s_ssid[WM_MAX_SSID_LEN + 1] = {0};
static char s_pass[WM_MAX_PASS_LEN + 1] = {0};
static bool s_has_credentials = false;
static bool s_initialized = false;
static bool s_connected = false;
static bool s_provisioning_active = false;
static int s_retry_count = 0;

static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static volatile bool s_dns_stop = false;
static esp_timer_handle_t s_sta_retry_timer = NULL;
static WifiManager_ConnectedCb s_connected_cb = NULL;

/* Private function prototypes -----------------------------------------------*/
static void _wifiEventHandler(void* arg,
                              esp_event_base_t base,
                              int32_t id,
                              void* data);
static void _ipEventHandler(void* arg,
                            esp_event_base_t base,
                            int32_t id,
                            void* data);
static WifiManager_Status _loadCredentials(void);
static WifiManager_Status _saveCredentials(const char* ssid, const char* pass);
static void _startSta(void);
static void _startProvisioning(void);
static void _stopProvisioning(void);
static void _staRetryTimerCb(void* arg);
static void _dnsTask(void* arg);
static esp_err_t _httpRootGet(httpd_req_t* req);
static esp_err_t _httpWildcardGet(httpd_req_t* req);
static esp_err_t _httpSavePost(httpd_req_t* req);
static void _restartTask(void* arg);
static bool _urlDecode(const char* src, char* dst, size_t dst_size);
static bool _formGetField(const char* body,
                          const char* key,
                          char* out,
                          size_t out_size);

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  WiFi driver event handler (STA connect/disconnect, AP clients)
 */
static void _wifiEventHandler(void* arg,
                              esp_event_base_t base,
                              int32_t id,
                              void* data) {
  (void)arg;
  (void)base;
  (void)data;

  if (id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    return;
  }

  if (id == WIFI_EVENT_STA_DISCONNECTED) {
    s_connected = false;
    s_retry_count++;
    if (!s_provisioning_active &&
        s_retry_count > CONFIG_TPTCM_STA_MAX_RETRY) {
      ESP_LOGW(s_tag,
               "STA failed %d times, starting provisioning AP",
               s_retry_count);
      _startProvisioning();
    }
    if (!s_provisioning_active) {
      esp_wifi_connect();
    }
    /* In provisioning mode the retry timer drives reconnects. */
    return;
  }

  if (id == WIFI_EVENT_AP_STACONNECTED) {
    ESP_LOGI(s_tag, "Provisioning client connected");
  }
}

/**
 * @brief  IP event handler (station got an address)
 */
static void _ipEventHandler(void* arg,
                            esp_event_base_t base,
                            int32_t id,
                            void* data) {
  (void)arg;
  (void)base;

  if (id != IP_EVENT_STA_GOT_IP) {
    return;
  }

  ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
  char ip_str[16] = {0};
  esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));

  s_connected = true;
  s_retry_count = 0;
  ESP_LOGI(s_tag, "STA connected, IP: %s", ip_str);

  if (s_provisioning_active) {
    /* Router came back on its own: provisioning no longer needed. */
    _stopProvisioning();
  }

  if (s_connected_cb != NULL) {
    s_connected_cb(ip_str);
  }
}

/**
 * @brief  Load WiFi credentials from NVS, falling back to Kconfig defaults
 * @return WifiManager_Status error code
 */
static WifiManager_Status _loadCredentials(void) {
  nvs_handle_t nvs;
  if (nvs_open(WM_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    size_t len = sizeof(s_ssid);
    esp_err_t err = nvs_get_str(nvs, WM_NVS_KEY_SSID, s_ssid, &len);
    if (err == ESP_OK) {
      len = sizeof(s_pass);
      (void)nvs_get_str(nvs, WM_NVS_KEY_PASS, s_pass, &len);
    }
    nvs_close(nvs);
    if (err == ESP_OK && s_ssid[0] != '\0') {
      s_has_credentials = true;
      ESP_LOGI(s_tag, "Credentials loaded from NVS, SSID: %s", s_ssid);
      return WM_OK;
    }
  }

  /* Fall back to Kconfig defaults when NVS is empty. */
  if (strlen(CONFIG_TPTCM_WIFI_SSID) > 0U) {
    strncpy(s_ssid, CONFIG_TPTCM_WIFI_SSID, sizeof(s_ssid) - 1U);
    strncpy(s_pass, CONFIG_TPTCM_WIFI_PASSWORD, sizeof(s_pass) - 1U);
    s_has_credentials = true;
    ESP_LOGI(s_tag, "Using Kconfig default SSID: %s", s_ssid);
  }
  return WM_OK;
}

/**
 * @brief  Persist WiFi credentials in NVS
 * @param  ssid Network name (non-empty)
 * @param  pass Network password (may be empty for open networks)
 * @return WifiManager_Status error code
 */
static WifiManager_Status _saveCredentials(const char* ssid, const char* pass) {
  if (ssid == NULL || pass == NULL || ssid[0] == '\0') {
    return WM_ERR_INVALID;
  }

  nvs_handle_t nvs;
  if (nvs_open(WM_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    return WM_ERR_NVS;
  }
  esp_err_t err = nvs_set_str(nvs, WM_NVS_KEY_SSID, ssid);
  if (err == ESP_OK) {
    err = nvs_set_str(nvs, WM_NVS_KEY_PASS, pass);
  }
  if (err == ESP_OK) {
    err = nvs_commit(nvs);
  }
  nvs_close(nvs);
  return (err == ESP_OK) ? WM_OK : WM_ERR_NVS;
}

/**
 * @brief  Start station mode with the loaded credentials
 */
static void _startSta(void) {
  wifi_config_t cfg = {0};
  strncpy((char*)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid) - 1U);
  strncpy((char*)cfg.sta.password, s_pass, sizeof(cfg.sta.password) - 1U);
  cfg.sta.threshold.authmode =
      (s_pass[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(s_tag, "Connecting to SSID: %s", s_ssid);
}

/**
 * @brief  Periodic STA reconnect attempt while provisioning is active
 */
static void _staRetryTimerCb(void* arg) {
  (void)arg;
  if (s_provisioning_active && s_has_credentials && !s_connected) {
    esp_wifi_connect();
  }
}

/**
 * @brief  Start the provisioning access point, HTTP portal and DNS hijack
 */
static void _startProvisioning(void) {
  if (s_provisioning_active) {
    return;
  }

  /* Switch to APSTA so the radio can still reach the target network. */
  wifi_mode_t mode = WIFI_MODE_NULL;
  (void)esp_wifi_get_mode(&mode);
  bool wifi_running = (mode != WIFI_MODE_NULL);
  if (wifi_running && mode == WIFI_MODE_STA) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  }

  wifi_config_t ap_cfg = {0};
  strncpy((char*)ap_cfg.ap.ssid,
          CONFIG_TPTCM_AP_SSID,
          sizeof(ap_cfg.ap.ssid) - 1U);
  ap_cfg.ap.ssid_len = strlen(CONFIG_TPTCM_AP_SSID);
  strncpy((char*)ap_cfg.ap.password,
          CONFIG_TPTCM_AP_PASSWORD,
          sizeof(ap_cfg.ap.password) - 1U);
  ap_cfg.ap.max_connection = 2;
  ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (strlen(CONFIG_TPTCM_AP_PASSWORD) < 8U) {
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  }

  if (!wifi_running) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  if (!wifi_running) {
    ESP_ERROR_CHECK(esp_wifi_start());
  }

  /* HTTP portal */
  httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
  http_cfg.uri_match_fn = httpd_uri_match_wildcard;
  http_cfg.max_uri_handlers = 4;
  if (httpd_start(&s_httpd, &http_cfg) != ESP_OK) {
    ESP_LOGE(s_tag, "Failed to start provisioning HTTP server");
    return;
  }

  const httpd_uri_t uri_root = {.uri = "/",
                                .method = HTTP_GET,
                                .handler = _httpRootGet};
  const httpd_uri_t uri_save = {.uri = "/save",
                                .method = HTTP_POST,
                                .handler = _httpSavePost};
  const httpd_uri_t uri_wild = {.uri = "/*",
                                .method = HTTP_GET,
                                .handler = _httpWildcardGet};
  (void)httpd_register_uri_handler(s_httpd, &uri_root);
  (void)httpd_register_uri_handler(s_httpd, &uri_save);
  (void)httpd_register_uri_handler(s_httpd, &uri_wild);

  /* Wildcard DNS for captive portal auto-popup */
  s_dns_stop = false;
  if (xTaskCreate(_dnsTask, "wm_dns", 3072, NULL, 5, &s_dns_task) !=
      pdPASS) {
    s_dns_task = NULL;
    ESP_LOGW(s_tag, "Failed to start DNS task, portal popup unavailable");
  }

  /* Keep probing the saved network while the portal is up. */
  if (s_has_credentials && s_sta_retry_timer == NULL) {
    const esp_timer_create_args_t timer_args = {
        .callback = _staRetryTimerCb,
        .name = "wm_sta_retry",
    };
    if (esp_timer_create(&timer_args, &s_sta_retry_timer) == ESP_OK) {
      (void)esp_timer_start_periodic(s_sta_retry_timer,
                                     WM_STA_RETRY_PERIOD_US);
    }
  }

  s_provisioning_active = true;
  ESP_LOGI(s_tag,
           "Provisioning AP started: %s, portal http://%s/",
           CONFIG_TPTCM_AP_SSID,
           WM_AP_IP);
}

/**
 * @brief  Stop the provisioning AP, HTTP portal and DNS task
 */
static void _stopProvisioning(void) {
  if (!s_provisioning_active) {
    return;
  }
  s_provisioning_active = false;

  if (s_sta_retry_timer != NULL) {
    (void)esp_timer_stop(s_sta_retry_timer);
    (void)esp_timer_delete(s_sta_retry_timer);
    s_sta_retry_timer = NULL;
  }
  if (s_httpd != NULL) {
    (void)httpd_stop(s_httpd);
    s_httpd = NULL;
  }
  if (s_dns_task != NULL) {
    /* The task closes its socket and exits by itself within a second. */
    s_dns_stop = true;
  }
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_LOGI(s_tag, "Provisioning stopped");
}

/**
 * @brief  Captive portal DNS task: answer every query with the AP address.
 *         Exits by itself when s_dns_stop is set (checked once a second).
 */
static void _dnsTask(void* arg) {
  (void)arg;
  uint8_t rx_buf[WM_DNS_BUF_SIZE];

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(s_tag, "DNS socket failed");
    vTaskDelete(NULL);
    return;
  }

  struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
  (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(WM_DNS_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(s_tag, "DNS bind failed");
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  while (!s_dns_stop) {
    struct sockaddr_in src = {0};
    socklen_t src_len = sizeof(src);
    int len = recvfrom(sock,
                       rx_buf,
                       sizeof(rx_buf),
                       0,
                       (struct sockaddr*)&src,
                       &src_len);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; /* recv timeout: re-check the stop flag */
      }
      break;
    }
    if (len < 12) { /* DNS header minimum */
      continue;
    }

    /* Turn the query into a response: RA + no error, one answer. */
    rx_buf[2] = 0x81;
    rx_buf[3] = 0x80;
    rx_buf[6] = 0x00; /* ANCOUNT hi */
    rx_buf[7] = 0x01; /* ANCOUNT lo */

    /* Append the answer after the question section. */
    static const uint8_t answer[] = {
        0xC0, 0x0C,                   /* name pointer to the query */
        0x00, 0x01,                   /* type A */
        0x00, 0x01,                   /* class IN */
        0x00, 0x00, 0x00, 0x3C,       /* TTL 60 s */
        0x00, 0x04,                   /* RDLENGTH */
        192, 168, 4, 1                /* WM_AP_IP */
    };
    size_t resp_len = (size_t)len;
    if (resp_len + sizeof(answer) <= sizeof(rx_buf)) {
      memcpy(&rx_buf[resp_len], answer, sizeof(answer));
      resp_len += sizeof(answer);
      (void)sendto(sock,
                   rx_buf,
                   resp_len,
                   0,
                   (struct sockaddr*)&src,
                   src_len);
    }
  }

  close(sock);
  s_dns_task = NULL;
  vTaskDelete(NULL);
}

/**
 * @brief  GET / - provisioning form
 */
static esp_err_t _httpRootGet(httpd_req_t* req) {
  static const char page[] =
      "<!DOCTYPE html><html><head>"
      "<meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>TPTCM60 WiFi Setup</title>"
      "<style>body{font-family:sans-serif;max-width:22em;margin:2em auto;"
      "padding:0 1em}input,button{width:100%;box-sizing:border-box;"
      "padding:.6em;margin:.3em 0;font-size:1em}"
      "button{background:#0366d6;color:#fff;border:0;border-radius:4px}"
      "</style></head><body>"
      "<h2>TPTCM60 WiFi Setup</h2>"
      "<form method=\"post\" action=\"/save\">"
      "<label>SSID<input name=\"ssid\" maxlength=\"32\" required></label>"
      "<label>Password<input name=\"pass\" type=\"password\" maxlength=\"64\">"
      "</label>"
      "<button type=\"submit\">Save &amp; reboot</button>"
      "</form></body></html>";

  (void)httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief  Wildcard GET - redirect captive portal probes to the setup page
 */
static esp_err_t _httpWildcardGet(httpd_req_t* req) {
  (void)httpd_resp_set_status(req, "302 Found");
  (void)httpd_resp_set_hdr(req, "Location", "http://" WM_AP_IP "/");
  return httpd_resp_send(req, NULL, 0);
}

/**
 * @brief  POST /save - store credentials and reboot
 */
static esp_err_t _httpSavePost(httpd_req_t* req) {
  char body[WM_FORM_BUF_SIZE] = {0};
  char ssid[WM_MAX_SSID_LEN + 1] = {0};
  char pass[WM_MAX_PASS_LEN + 1] = {0};

  int content_len = req->content_len;
  if (content_len <= 0 || content_len >= (int)sizeof(body)) {
    (void)httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
    return ESP_FAIL;
  }

  int received = httpd_req_recv(req, body, content_len);
  if (received <= 0) {
    (void)httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                              "Receive failed");
    return ESP_FAIL;
  }
  body[received] = '\0';

  if (!_formGetField(body, "ssid", ssid, sizeof(ssid)) ||
      !_formGetField(body, "pass", pass, sizeof(pass)) || ssid[0] == '\0' ||
      (pass[0] != '\0' && strlen(pass) < 8U)) {
    (void)httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              "Invalid SSID or password");
    return ESP_FAIL;
  }

  if (_saveCredentials(ssid, pass) != WM_OK) {
    (void)httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                              "NVS write failed");
    return ESP_FAIL;
  }

  static const char done_page[] =
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "</head><body style=\"font-family:sans-serif;text-align:center;"
      "margin-top:3em\">"
      "<h2>Saved. Device is rebooting&hellip;</h2>"
      "<p>Connect your PC to the network you configured and print to "
      "port 9100.</p></body></html>";
  (void)httpd_resp_set_type(req, "text/html");
  esp_err_t err = httpd_resp_send(req, done_page, HTTPD_RESP_USE_STRLEN);

  /* Restart from a separate task so the HTTP response completes first. */
  (void)xTaskCreate(_restartTask, "wm_restart", 2048, NULL, 5, NULL);
  return err;
}

/**
 * @brief  Delayed reboot after credentials were saved
 */
static void _restartTask(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(WM_RESTART_DELAY_MS));
  esp_restart();
}

/**
 * @brief  In-place percent-decoding of a URL-encoded string
 * @param  src      Source string (URL-encoded)
 * @param  dst      Output buffer
 * @param  dst_size Output buffer size
 * @return true on success, false when the output did not fit
 */
static bool _urlDecode(const char* src, char* dst, size_t dst_size) {
  size_t out = 0;
  while (*src != '\0') {
    if (out + 1U >= dst_size) {
      return false;
    }
    char c = *src++;
    if (c == '%' && src[0] != '\0' && src[1] != '\0') {
      char hex[3] = {src[0], src[1], '\0'};
      dst[out++] = (char)strtol(hex, NULL, 16);
      src += 2;
    } else if (c == '+') {
      dst[out++] = ' ';
    } else {
      dst[out++] = c;
    }
  }
  dst[out] = '\0';
  return true;
}

/**
 * @brief  Extract and decode a field from an x-www-form-urlencoded body
 * @param  body     Form body ("ssid=...&pass=...")
 * @param  key      Field name
 * @param  out      Output buffer
 * @param  out_size Output buffer size
 * @return true when the field is present (possibly empty)
 */
static bool _formGetField(const char* body,
                          const char* key,
                          char* out,
                          size_t out_size) {
  size_t key_len = strlen(key);
  const char* p = body;
  while (*p != '\0') {
    if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
      const char* value = p + key_len + 1;
      const char* end = strchr(value, '&');
      size_t value_len = (end != NULL) ? (size_t)(end - value) : strlen(value);
      char encoded[WM_MAX_PASS_LEN + 1];
      if (value_len >= sizeof(encoded)) {
        return false;
      }
      memcpy(encoded, value, value_len);
      encoded[value_len] = '\0';
      return _urlDecode(encoded, out, out_size);
    }
    const char* next = strchr(p, '&');
    if (next == NULL) {
      break;
    }
    p = next + 1;
  }
  out[0] = '\0';
  return false;
}

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Initialize the WiFi manager (netif, event handlers, credentials)
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_Init(void) {
  if (s_initialized) {
    return WM_OK;
  }

  if (esp_netif_init() != ESP_OK) {
    return WM_ERR_WIFI;
  }
  if (esp_event_loop_create_default() != ESP_OK) {
    return WM_ERR_WIFI;
  }
  (void)esp_netif_create_default_wifi_sta();
  (void)esp_netif_create_default_wifi_ap();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init_cfg) != ESP_OK) {
    return WM_ERR_WIFI;
  }

  if (esp_event_handler_instance_register(WIFI_EVENT,
                                          ESP_EVENT_ANY_ID,
                                          _wifiEventHandler,
                                          NULL,
                                          NULL) != ESP_OK) {
    return WM_ERR_WIFI;
  }
  if (esp_event_handler_instance_register(IP_EVENT,
                                          IP_EVENT_STA_GOT_IP,
                                          _ipEventHandler,
                                          NULL,
                                          NULL) != ESP_OK) {
    return WM_ERR_WIFI;
  }

  if (_loadCredentials() != WM_OK) {
    return WM_ERR_NVS;
  }

  s_initialized = true;
  return WM_OK;
}

/**
 * @brief  Start the WiFi manager
 * @param  connected_cb Callback for "STA got IP" event (may be NULL)
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_Start(WifiManager_ConnectedCb connected_cb) {
  if (!s_initialized) {
    return WM_ERR_NOT_READY;
  }

  s_connected_cb = connected_cb;

  if (s_has_credentials) {
    _startSta();
  } else {
    ESP_LOGI(s_tag, "No credentials, starting provisioning AP");
    _startProvisioning();
  }
  return WM_OK;
}

/**
 * @brief  Check whether the station is connected and has an IP
 * @return true when connected
 */
bool WifiManager_IsConnected(void) {
  return s_connected;
}

/**
 * @brief  Erase saved WiFi credentials from NVS
 * @return WifiManager_Status error code
 */
WifiManager_Status WifiManager_ClearCredentials(void) {
  nvs_handle_t nvs;
  if (nvs_open(WM_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    return WM_ERR_NVS;
  }
  esp_err_t err = nvs_erase_key(nvs, WM_NVS_KEY_SSID);
  if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
    esp_err_t err2 = nvs_erase_key(nvs, WM_NVS_KEY_PASS);
    if (err2 != ESP_OK && err2 != ESP_ERR_NVS_NOT_FOUND) {
      err = err2;
    } else {
      err = nvs_commit(nvs);
    }
  }
  nvs_close(nvs);

  if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
    s_has_credentials = false;
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    return WM_OK;
  }
  return WM_ERR_NVS;
}
