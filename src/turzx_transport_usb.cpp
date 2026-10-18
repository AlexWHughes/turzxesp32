#include "turzx_factory.h"
#include "turzx_transport.h"

#if !TURZX_USE_UART

#if __has_include("usb/cdc_acm_host.h") && __has_include("usb/usb_host.h")

#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "cdc_host_descriptor_parsing.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

static constexpr uint16_t kWakeVid = 0x1a86;
static constexpr uint16_t kWakePid = 0xca88;
static constexpr uint16_t kDataVid = 0x0525;
static constexpr uint16_t kDataPid = 0xa4a7;
static constexpr uint16_t kDataVidAlt = 0x1d6b;  // Linux UDC gadget (some panels)
static constexpr uint16_t kDataPidAltA = 0x0121;
static constexpr uint16_t kDataPidAltB = 0x0106;

static char usbLogRing[180];

static int usbLogHook(const char *fmt, va_list args) {
  va_list copy;
  va_copy(copy, args);
  char line[140];
  vsnprintf(line, sizeof(line), fmt, copy);
  va_end(copy);
  if (strstr(line, "HUB") || strstr(line, "ENUM") || strstr(line, "USBH") ||
      strstr(line, "enum")) {
    const size_t used = strlen(usbLogRing);
    const size_t add = strlen(line);
    if (used + add + 1 >= sizeof(usbLogRing)) {
      usbLogRing[0] = '\0';
    }
    strlcat(usbLogRing, line, sizeof(usbLogRing));
  }
  return esp_rom_vprintf(fmt, args);
}

class TurzxUsbTransport : public TurzxTransport {
 public:
  bool begin() override {
    if (!installed_) {
      if (!installHost()) {
        setDiag("usb_host_install failed");
        return false;
      }
    }
    // One short probe only — retries happen in poll()/loop so WebUI stays alive.
    wakeDevice();
    return dataDev_ != nullptr;
  }

  void end() override {
    closeDevice(&wakeDev_);
    closeDevice(&dataDev_);
  }

  bool isReady() const override { return dataDev_ != nullptr; }

  bool hotplugPending() const override { return pendingOpen_ && dataDev_ == nullptr; }

  String diag() const override { return diag_; }

  bool poll() override {
    if (!installed_) {
      return begin();
    }
    if (dataDev_) {
      pendingOpen_ = false;
      return true;
    }
    // new_dev_cb / disconnect / ALL_FREE set pendingOpen_; main also polls on a timer.
    pendingOpen_ = false;
    wakeDevice();
    return dataDev_ != nullptr;
  }

  bool wakeDevice() override {
    dumpDeviceList();
    // Do not open the wake CDC here. Claiming it uses host channels and blocks
    // enumeration of the gadget on the hub's other port.
    probeBusIntoDiag(false);

    if (tryOpenData()) {
      pendingOpen_ = false;
      setDiag(String("gadget CDC open; bus=") + String(lastBusCount_) + probe_);
      return true;
    }

    // Hotplug: panel may sit on the hub without a fresh enumeration. Power-cycle
    // port 2 so the host re-assigns an address, then try CDC open again.
    if (probeBusIntoDiag(true)) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      probeBusIntoDiag(false);
      if (tryOpenData()) {
        pendingOpen_ = false;
        setDiag(String("gadget CDC open after cycle; bus=") + String(lastBusCount_) + probe_);
        return true;
      }
    }

    dumpDeviceList();
    setDiag(String("no gadget; ") + probe_ + " bus=" + String(lastBusCount_) +
            " log=" + usbLogRing);
    return false;
  }

  bool tryOpenData() {
    if (dataDev_) {
      return true;
    }
    tryOpenSimple(kDataVid, kDataPid, 0, &dataDev_, "data", false);
    if (!dataDev_) {
      tryOpenSimple(kDataVidAlt, kDataPidAltA, 0, &dataDev_, "data-alt", false);
    }
    if (!dataDev_) {
      tryOpenSimple(kDataVidAlt, kDataPidAltB, 0, &dataDev_, "data-alt2", false);
    }
    return dataDev_ != nullptr;
  }

  bool pulseWakeLikePython() {
    // Write-only open mirrors a simple pyserial open (no reader thread).
    tryOpenSimple(kWakeVid, kWakePid, 0, &wakeDev_, "wake", true);
    if (!wakeDev_) {
      return false;
    }
    setDiag(String("wake pulse; bus=") + String(lastBusCount_));
    // pyserial rtscts=True asserts RTS; default open also asserts DTR on many hosts.
    cdc_acm_host_set_control_line_state(wakeDev_, true, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    cdc_acm_host_set_control_line_state(wakeDev_, true, false);
    vTaskDelay(pdMS_TO_TICKS(50));
    cdc_acm_host_set_control_line_state(wakeDev_, true, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    closeDevice(&wakeDev_);
    return true;
  }

  size_t write(const uint8_t *data, size_t len) override {
    if (!dataDev_ || !data || len == 0) {
      return 0;
    }
    if (xSemaphoreTake(txMutex_, pdMS_TO_TICKS(3000)) != pdTRUE) {
      return 0;
    }
    size_t sent = 0;
    while (sent < len) {
      size_t chunk = len - sent;
      if (chunk > 512) {
        chunk = 512;
      }
      esp_err_t err = cdc_acm_host_data_tx_blocking(dataDev_, data + sent, chunk, 3000);
      if (err != ESP_OK) {
        Serial.printf("[turzx] TX err %s\n", esp_err_to_name(err));
        // Unplug often shows up as TX failure before (or without) a disconnect
        // event. Drop the handle so isReady() goes false and main can reopen.
        closeDevice(&dataDev_);
        pendingOpen_ = true;
        setDiag(String("tx-fail ") + esp_err_to_name(err) + "; awaiting hotplug");
        break;
      }
      sent += chunk;
      // Yield only. Servicing HTTP here lets a theme or test-pattern save mutate
      // the canvas and send another USB command in the middle of this frame.
      yield();
    }
    xSemaphoreGive(txMutex_);
    return sent;
  }

  size_t read(uint8_t *data, size_t len, uint32_t timeoutMs) override {
    if (!data || len == 0) {
      return 0;
    }
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs ? timeoutMs : 1);
    size_t got = 0;
    while (got < len) {
      if (xSemaphoreTake(rxMutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        while (got < len && rxHead_ != rxTail_) {
          data[got++] = rxRing_[rxTail_];
          rxTail_ = (rxTail_ + 1) % kRxRingSize;
        }
        xSemaphoreGive(rxMutex_);
      }
      if (got == len || xTaskGetTickCount() >= deadline) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    return got;
  }

  void flushInput() override {
    if (xSemaphoreTake(rxMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      rxHead_ = rxTail_ = 0;
      xSemaphoreGive(rxMutex_);
    }
  }

 private:
  static constexpr size_t kRxRingSize = 8192;

  static void hostLibTaskTrampoline(void *arg) {
    static_cast<TurzxUsbTransport *>(arg)->hostLibTask();
  }
  static void probeTaskTrampoline(void *arg) {
    static_cast<TurzxUsbTransport *>(arg)->probeTask();
  }
  static void probeEventTrampoline(const usb_host_client_event_msg_t *eventMsg, void *arg) {
    (void)eventMsg;
    (void)arg;
  }
  static bool rxTrampoline(const uint8_t *data, size_t dataLen, void *userArg) {
    return static_cast<TurzxUsbTransport *>(userArg)->onRx(data, dataLen);
  }
  static void devEventTrampoline(const cdc_acm_host_dev_event_data_t *event, void *userArg) {
    static_cast<TurzxUsbTransport *>(userArg)->onDevEvent(event);
  }
  static void newDevTrampoline(usb_device_handle_t usbDev) {
    // CDC driver already holds the device open for this callback — safe to read desc.
    auto *self = instance_;
    if (!self || !usbDev) {
      return;
    }
    const usb_device_desc_t *desc = nullptr;
    usb_device_info_t info = {};
    if (usb_host_get_device_descriptor(usbDev, &desc) != ESP_OK || !desc) {
      return;
    }
    (void)usb_host_device_info(usbDev, &info);

    char buf[48];
    snprintf(buf, sizeof(buf), " %04x:%04x/%u@%u", desc->idVendor, desc->idProduct,
             desc->bDeviceClass, info.dev_addr);
    Serial.printf("[turzx] CDC-driver saw%s\n", buf);
    if (self->seen_.length() < 80) {
      self->seen_ += buf;
    }

    // Capture interface layout so /api/status explains CDC open failures.
    self->appendIfaceLayout(usbDev, desc->idVendor, desc->idProduct);

    if (desc->idVendor == kWakeVid && desc->idProduct == kWakePid) {
      self->wakeAddr_ = info.dev_addr;
    } else if (desc->idVendor == kDataVid && desc->idProduct == kDataPid) {
      self->dataAddr_ = info.dev_addr;
    }
    self->setDiag(String("cdc-seen") + self->seen_ + self->layout_);
    self->pendingOpen_ = true;
  }

  void appendIfaceLayout(usb_device_handle_t usbDev, uint16_t vid, uint16_t pid) {
    const usb_config_desc_t *cfg = nullptr;
    if (usb_host_get_active_config_descriptor(usbDev, &cfg) != ESP_OK || !cfg) {
      layout_ += " cfg?";
      return;
    }
    char chunk[120];
    snprintf(chunk, sizeof(chunk), " %04x:%04x[n=%u", vid, pid, cfg->bNumInterfaces);
    layout_ = chunk;
    for (uint8_t num = 0; num < 8; ++num) {
      int offset = 0;
      const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cfg, num, 0, &offset);
      if (!intf) {
        continue;
      }
      snprintf(chunk, sizeof(chunk), " i%u:c%u/e%u", intf->bInterfaceNumber, intf->bInterfaceClass,
               intf->bNumEndpoints);
      if (layout_.length() + strlen(chunk) < 140) {
        layout_ += chunk;
      }
      const int intfOffset = offset;
      for (int e = 0; e < intf->bNumEndpoints; ++e) {
        int epOff = intfOffset;
        const usb_ep_desc_t *ep =
            usb_parse_endpoint_descriptor_by_index(intf, e, cfg->wTotalLength, &epOff);
        if (!ep) {
          continue;
        }
        snprintf(chunk, sizeof(chunk), "{t%u%c}", USB_EP_DESC_GET_XFERTYPE(ep),
                 USB_EP_DESC_GET_EP_DIR(ep) ? 'I' : 'O');
        if (layout_.length() + strlen(chunk) < 160) {
          layout_ += chunk;
        }
      }
    }
    layout_ += "]";

    // Same parser cdc_acm_host_open uses — distinguishes find vs parse failures in diag.
    const usb_device_desc_t *devDesc = nullptr;
    if (usb_host_get_device_descriptor(usbDev, &devDesc) == ESP_OK && devDesc) {
      cdc_parsed_info_t p0 = {};
      cdc_parsed_info_t p1 = {};
      esp_err_t parse0 = cdc_parse_interface_descriptor(devDesc, cfg, 0, &p0);
      esp_err_t parse1 = cdc_parse_interface_descriptor(devDesc, cfg, 1, &p1);
      snprintf(chunk, sizeof(chunk), " parse0=%s parse1=%s", esp_err_to_name(parse0),
               esp_err_to_name(parse1));
      if (layout_.length() + strlen(chunk) < 220) {
        layout_ += chunk;
      }
      free(p0.func);
      free(p1.func);
    }
  }

  // Arduino-ESP32 ≥3.3.11 builds with CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK.
  // A NULL enum_filter_cb is treated as "reject every device" → permanent bus=0.
  static bool enumFilterTrampoline(const usb_device_desc_t *devDesc, uint8_t *bConfigurationValue) {
    (void)devDesc;
    // Keep index 0 for GET_DESCRIPTOR. Patched enum.c then copies the
    // descriptor's real bConfigurationValue (2 on 0525:a4a7) before SET_CONFIGURATION.
    if (bConfigurationValue) {
      *bConfigurationValue = 1;
    }
    return true;
  }

  void setDiag(const String &msg) {
    diag_ = msg;
    Serial.printf("[turzx] %s\n", msg.c_str());
  }

  void releaseUsbSerialJtag() {
    // USJ and OTG share the PHY. On the OTG-debug build, stop CDC first.
#if !TURZX_LOG_UART
    Serial.println("[turzx] Releasing USB-Serial/JTAG — OTG serial will drop");
    Serial.flush();
    delay(50);
    Serial.end();
#endif
    usb_serial_jtag_ll_phy_enable_pad(false);
  }

  bool installHost() {
    rxMutex_ = xSemaphoreCreateMutex();
    txMutex_ = xSemaphoreCreateMutex();
    ctrlSem_ = xSemaphoreCreateBinary();
    if (!rxMutex_ || !txMutex_ || !ctrlSem_) {
      return false;
    }

    esp_log_set_vprintf(&usbLogHook);
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("HUB", ESP_LOG_INFO);
    esp_log_level_set("ENUM", ESP_LOG_INFO);
    esp_log_level_set("USBH", ESP_LOG_INFO);
    Serial.println("[turzx] Starting USB Host (host-lib owns PHY)...");
    Serial.flush();
    releaseUsbSerialJtag();
    // Drop any prior manual PHY so usb_host_install can configure HOST mode.
    if (phy_) {
      usb_del_phy(phy_);
      phy_ = nullptr;
    }

    usb_host_config_t hostCfg = {};
    hostCfg.skip_phy_setup = false;  // let USB Host Library set OTG HOST
    hostCfg.root_port_unpowered = false;
    hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    hostCfg.enum_filter_cb = &TurzxUsbTransport::enumFilterTrampoline;
#endif

    esp_err_t err = usb_host_install(&hostCfg);
    Serial.printf("[turzx] usb_host_install -> %s\n", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      setDiag(String("usb_host_install ") + esp_err_to_name(err));
      return false;
    }

    // Event task must run before devices can enumerate.
    if (xTaskCreatePinnedToCore(&TurzxUsbTransport::hostLibTaskTrampoline, "usb_lib", 4096, this, 5,
                                &hostTask_, 0) != pdPASS) {
      setDiag("usb_lib task failed");
      return false;
    }

    err = usb_host_lib_set_root_port_power(true);
    Serial.printf("[turzx] root_port_power(true) -> %s\n", esp_err_to_name(err));

    instance_ = this;

    // Probe client: brief open/close to read VID:PID into /api/status. Must never
    // hold devices across cdc_acm_host_open.
    const usb_host_client_config_t probeCfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async =
            {
                .client_event_callback = &TurzxUsbTransport::probeEventTrampoline,
                .callback_arg = this,
            },
    };
    if (usb_host_client_register(&probeCfg, &probeClient_) == ESP_OK) {
      xTaskCreatePinnedToCore(&TurzxUsbTransport::probeTaskTrampoline, "usb_probe", 3072, this, 2,
                              &probeTask_, 0);
    }

    const cdc_acm_host_driver_config_t cdcCfg = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 4,
        .xCoreID = 0,
        // Required for hotplug: without this, a panel plugged after boot is
        // never noticed until a blind retry, and often never opens.
        .new_dev_cb = &TurzxUsbTransport::newDevTrampoline,
    };
    err = cdc_acm_host_install(&cdcCfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      Serial.printf("[turzx] cdc_acm_host_install failed: %s\n", esp_err_to_name(err));
      return false;
    }

    installed_ = true;
    // Brief settle for hub enumerate — keep short so WebUI loop resumes.
    vTaskDelay(pdMS_TO_TICKS(400));
    dumpDeviceList();
    setDiag(String("host up; bus=") + String(lastBusCount_));
    return true;
  }

  void hostLibTask() {
    while (true) {
      uint32_t eventFlags = 0;
      usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
      if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
        usb_host_device_free_all();
      }
      if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
        // Keep pumping events so a replugged panel can re-enumerate.
        pendingOpen_ = true;
      }
    }
  }

  void dumpDeviceList() {
    uint8_t addrs[16] = {};
    int num = 0;
    esp_err_t err = usb_host_device_addr_list_fill(16, addrs, &num);
    lastBusCount_ = num;
    Serial.printf("[turzx] bus: err=%s count=%d", esp_err_to_name(err), num);
    for (int i = 0; i < num; ++i) {
      Serial.printf(" addr%u", addrs[i]);
    }
    Serial.println();
  }

  void probeTask() {
    while (probeClient_) {
      usb_host_client_handle_events(probeClient_, portMAX_DELAY);
    }
    vTaskDelete(nullptr);
  }

  bool probeBusIntoDiag(bool allowReset) {
    bool resetIssued = false;
    if (!probeClient_) {
      return false;
    }
    uint8_t addrs[16] = {};
    int num = 0;
    usb_host_device_addr_list_fill(16, addrs, &num);
    probe_ = "";
    for (int i = 0; i < num; ++i) {
      usb_device_handle_t dev = nullptr;
      esp_err_t oerr = usb_host_device_open(probeClient_, addrs[i], &dev);
      if (oerr != ESP_OK) {
        char buf[32];
        snprintf(buf, sizeof(buf), " @%u:open=%s", addrs[i], esp_err_to_name(oerr));
        probe_ += buf;
        continue;
      }
      const usb_device_desc_t *desc = nullptr;
      if (usb_host_get_device_descriptor(dev, &desc) == ESP_OK && desc) {
        char buf[40];
        snprintf(buf, sizeof(buf), " @%u:%04x:%04x/%u", addrs[i], desc->idVendor, desc->idProduct,
                 desc->bDeviceClass);
        probe_ += buf;
        if (desc->idVendor == 0x1a40 && desc->idProduct == 0x0101) {
          if (appendHubPorts(dev, allowReset)) {
            resetIssued = true;
          }
        }
      }
      usb_host_device_close(probeClient_, dev);
    }
    return resetIssued;
  }

  // Ownership of in-flight control transfers: first of (callback signal |
  // caller timeout) that moves state out of kLive owns cleanup when the other
  // side observes the final state. Avoids the previous intentional leak on
  // timeout (panel retry called hubCtrl every few seconds).
  enum CtrlXferState : int { kCtrlLive = 0, kCtrlSignaled = 1, kCtrlAbandoned = 2 };
  struct CtrlXferCtx {
    SemaphoreHandle_t sem = nullptr;
    volatile int state = kCtrlLive;
  };

  static void ctrlDone(usb_transfer_t *transfer) {
    auto *ctx = static_cast<CtrlXferCtx *>(transfer->context);
    if (!ctx) {
      usb_host_transfer_free(transfer);
      return;
    }
    const int prev = __atomic_exchange_n(&ctx->state, kCtrlSignaled, __ATOMIC_ACQ_REL);
    if (prev == kCtrlAbandoned) {
      usb_host_transfer_free(transfer);
      free(ctx);
      return;
    }
    if (ctx->sem) {
      xSemaphoreGive(ctx->sem);
    }
  }

  // Class request on an open hub. Returns bytes of data (IN) or 0 (OUT) on success, -1 on failure.
  int hubCtrl(usb_device_handle_t hub, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
              uint16_t wIndex, uint8_t *data, uint16_t wLength) {
    if (!probeClient_ || !hub || !ctrlSem_) {
      return -1;
    }
    usb_transfer_t *xfer = nullptr;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &xfer) != ESP_OK || !xfer) {
      return -1;
    }
    auto *ctx = static_cast<CtrlXferCtx *>(calloc(1, sizeof(CtrlXferCtx)));
    if (!ctx) {
      usb_host_transfer_free(xfer);
      return -1;
    }
    ctx->sem = ctrlSem_;
    ctx->state = kCtrlLive;
    xfer->device_handle = hub;
    xfer->bEndpointAddress = 0;
    xfer->timeout_ms = 500;
    xfer->callback = &TurzxUsbTransport::ctrlDone;
    xfer->context = ctx;
    xfer->num_bytes = sizeof(usb_setup_packet_t) + wLength;
    auto *req = reinterpret_cast<usb_setup_packet_t *>(xfer->data_buffer);
    req->bmRequestType = bmRequestType;
    req->bRequest = bRequest;
    req->wValue = wValue;
    req->wIndex = wIndex;
    req->wLength = wLength;
    const bool isIn = (bmRequestType & 0x80) != 0;
    if (!isIn && data && wLength) {
      memcpy(xfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
    }
    xSemaphoreTake(ctrlSem_, 0);
    if (usb_host_transfer_submit_control(probeClient_, xfer) != ESP_OK) {
      usb_host_transfer_free(xfer);
      free(ctx);
      return -1;
    }
    if (xSemaphoreTake(ctrlSem_, pdMS_TO_TICKS(800)) != pdTRUE) {
      const int prev = __atomic_exchange_n(&ctx->state, kCtrlAbandoned, __ATOMIC_ACQ_REL);
      if (prev == kCtrlSignaled) {
        (void)xSemaphoreTake(ctrlSem_, 0);
        usb_host_transfer_free(xfer);
        free(ctx);
      }
      // else callback still owns cleanup when the transfer completes
      return -1;
    }
    int got = -1;
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
      got = xfer->actual_num_bytes - static_cast<int>(sizeof(usb_setup_packet_t));
      if (got < 0) {
        got = 0;
      }
      if (isIn && data && got > 0) {
        const int copy = got < wLength ? got : wLength;
        memcpy(data, xfer->data_buffer + sizeof(usb_setup_packet_t), copy);
        got = copy;
      }
    }
    usb_host_transfer_free(xfer);
    free(ctx);
    return got;
  }

  bool appendHubPorts(usb_device_handle_t hub, bool allowReset) {
    bool resetIssued = false;
    uint8_t desc[12] = {};
    // GET_DESCRIPTOR, hub class descriptor 0x29.
    const int dn = hubCtrl(hub, 0xA0, 0x06, 0x2900, 0, desc, 9);
    int ports = (dn >= 3) ? desc[2] : 0;
    if (ports < 1 || ports > 7) {
      ports = 4;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), " ports=%d", ports);
    probe_ += buf;
    for (int p = 1; p <= ports; ++p) {
      uint8_t st[4] = {};
      // GET_STATUS, hub port. Bits: C connect, E enable, P power (bit 8), H high-speed (bit 10).
      const int sn = hubCtrl(hub, 0xA3, 0x00, 0, static_cast<uint16_t>(p), st, 4);
      if (sn < 2) {
        snprintf(buf, sizeof(buf), " p%d=err", p);
        probe_ += buf;
        continue;
      }
      const uint16_t s = static_cast<uint16_t>(st[0] | (st[1] << 8));
      snprintf(buf, sizeof(buf), " p%d:%04x%s%s%s%s", p, s, (s & 0x0001) ? "C" : "",
               (s & 0x0002) ? "E" : "", (s & 0x0100) ? "P" : "", (s & 0x0400) ? "H" : "");
      probe_ += buf;
      const bool connected = (s & 0x0001) != 0;
      // Port 2 is the gadget. Power-cycle it so the hub driver runs enumeration
      // itself. A raw PORT_RESET enables the port without assigning an address.
      if (allowReset && p == 2 && connected) {
        const int cleared = hubCtrl(hub, 0x23, 0x01, 8, static_cast<uint16_t>(p), nullptr, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
        const int powered = hubCtrl(hub, 0x23, 0x03, 8, static_cast<uint16_t>(p), nullptr, 0);
        if (cleared >= 0 && powered >= 0) {
          probe_ += "+cyc";
          resetIssued = true;
        }
      }
    }
    return resetIssued;
  }

  // Claim slot if it still holds hdl. TURNOFF re-enumerates, so end() and this
  // callback can both try to close the same CDC handle.
  bool takeHandle(cdc_acm_dev_hdl_t *slot, cdc_acm_dev_hdl_t hdl) {
    if (!slot || hdl == nullptr) {
      return false;
    }
    cdc_acm_dev_hdl_t expected = hdl;
    return __atomic_compare_exchange_n(slot, &expected, static_cast<cdc_acm_dev_hdl_t>(nullptr), false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }

  void onDevEvent(const cdc_acm_host_dev_event_data_t *event) {
    if (!event) {
      return;
    }
    if (event->type == CDC_ACM_HOST_ERROR) {
      Serial.printf("[turzx] CDC error %d — dropping handle for hotplug retry\n",
                    event->data.error);
      closeDevice(&dataDev_);
      pendingOpen_ = true;
      setDiag("cdc-error; awaiting hotplug");
      return;
    }
    if (event->type != CDC_ACM_HOST_DEVICE_DISCONNECTED) {
      return;
    }
    const cdc_acm_dev_hdl_t hdl = event->data.cdc_hdl;
    if (takeHandle(&dataDev_, hdl) || takeHandle(&wakeDev_, hdl)) {
      cdc_acm_host_close(hdl);
    }
    pendingOpen_ = true;
    setDiag("panel disconnected; awaiting hotplug");
  }

  bool onRx(const uint8_t *data, size_t dataLen) {
    if (xSemaphoreTake(rxMutex_, 0) != pdTRUE) {
      return true;
    }
    for (size_t i = 0; i < dataLen; ++i) {
      const size_t next = (rxHead_ + 1) % kRxRingSize;
      if (next == rxTail_) {
        break;
      }
      rxRing_[rxHead_] = data[i];
      rxHead_ = next;
    }
    xSemaphoreGive(rxMutex_);
    return true;
  }

  void tryOpenSimple(uint16_t vid, uint16_t pid, uint8_t iface, cdc_acm_dev_hdl_t *out,
                     const char *label, bool writeOnly) {
    if (!out || *out) {
      return;
    }
    const cdc_acm_host_device_config_t devCfg = {
        // Hotplug needs more than 500 ms — hub re-enumeration after a cable
        // plug often lands just after a short open times out.
        .connection_timeout_ms = 2000,
        .out_buffer_size = writeOnly ? 64 : 1024,
        .in_buffer_size = writeOnly ? 0 : 1024,
        .event_cb = &TurzxUsbTransport::devEventTrampoline,
        .data_cb = writeOnly ? nullptr : &TurzxUsbTransport::rxTrampoline,
        .user_arg = this,
    };
    esp_err_t err = cdc_acm_host_open(vid, pid, iface, &devCfg, out);
    lastOpenErr_ = err;
    if (err != ESP_OK) {
      *out = nullptr;
      Serial.printf("[turzx] open %s %04x:%04x iface=%u -> %s\n", label, vid, pid, iface,
                    esp_err_to_name(err));
      return;
    }
    Serial.printf("[turzx] opened %s %04x:%04x iface=%u\n", label, vid, pid, iface);
    cdc_acm_line_coding_t coding = {};
    coding.dwDTERate = 115200;
    coding.bDataBits = 8;
    cdc_acm_host_line_coding_set(*out, &coding);
    cdc_acm_host_set_control_line_state(*out, true, true);
  }

  void closeDevice(cdc_acm_dev_hdl_t *dev) {
    if (!dev || *dev == nullptr) {
      return;
    }
    const cdc_acm_dev_hdl_t hdl = *dev;
    if (takeHandle(dev, hdl)) {
      cdc_acm_host_close(hdl);
    }
  }

  static TurzxUsbTransport *instance_;
  TaskHandle_t hostTask_ = nullptr;
  TaskHandle_t probeTask_ = nullptr;
  usb_host_client_handle_t probeClient_ = nullptr;
  usb_phy_handle_t phy_ = nullptr;
  cdc_acm_dev_hdl_t wakeDev_ = nullptr;
  cdc_acm_dev_hdl_t dataDev_ = nullptr;
  SemaphoreHandle_t rxMutex_ = nullptr;
  SemaphoreHandle_t txMutex_ = nullptr;
  SemaphoreHandle_t ctrlSem_ = nullptr;
  uint8_t rxRing_[kRxRingSize] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  bool installed_ = false;
  bool pendingOpen_ = false;
  int lastBusCount_ = 0;
  uint8_t wakeAddr_ = 0;
  uint8_t dataAddr_ = 0;
  esp_err_t lastOpenErr_ = ESP_OK;
  String diag_;
  String seen_;
  String layout_;
  String probe_;
};

TurzxUsbTransport *TurzxUsbTransport::instance_ = nullptr;

TurzxTransport *createTurzxTransport() {
  static TurzxUsbTransport transport;
  return &transport;
}

#else

#include <HardwareSerial.h>

class TurzxUartTransportFallback : public TurzxTransport {
 public:
  bool begin() override {
    Serial.println("[turzx] USB Host CDC headers unavailable; using UART fallback");
    Serial1.begin(TURZX_UART_BAUD, SERIAL_8N1, TURZX_UART_RX, TURZX_UART_TX);
    ready_ = true;
    return true;
  }
  void end() override {
    Serial1.end();
    ready_ = false;
  }
  bool isReady() const override { return ready_; }
  size_t write(const uint8_t *data, size_t len) override { return Serial1.write(data, len); }
  size_t read(uint8_t *data, size_t len, uint32_t timeoutMs) override {
    Serial1.setTimeout(timeoutMs);
    return Serial1.readBytes(data, len);
  }
  void flushInput() override {
    while (Serial1.available()) {
      Serial1.read();
    }
  }

 private:
  bool ready_ = false;
};

TurzxTransport *createTurzxTransport() {
  static TurzxUartTransportFallback transport;
  return &transport;
}

#endif
#endif
