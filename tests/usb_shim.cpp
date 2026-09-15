// Test-only libusb replacement. Never opens real USB hardware.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <libusb.h>
#include <unistd.h>

static bool started = false;
static uint32_t selected_rate = 0;
static unsigned int emitted = 0;
static bool probe_echo_pending = false;
static unsigned char pending_probe[20]{};
static unsigned char first_probe[20]{};
static bool first_probe_saved = false;
static uint32_t read32(const unsigned char *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
static void log(const char *message) {
  std::ofstream(std::getenv("MEATCAN_SHIM_LOG"), std::ios::app)
      << message << '\n';
}

extern "C" {
int libusb_init(libusb_context **context) {
  *context = reinterpret_cast<libusb_context *>(1);
  return 0;
}
void libusb_exit(libusb_context *) {}
ssize_t libusb_get_device_list(libusb_context *, libusb_device ***output) {
  static libusb_device *devices[] = {reinterpret_cast<libusb_device *>(2),
                                     nullptr};
  *output = devices;
  return 1;
}
void libusb_free_device_list(libusb_device **, int) {}
int libusb_get_device_descriptor(libusb_device *,
                                 libusb_device_descriptor *descriptor) {
  std::memset(descriptor, 0, sizeof(*descriptor));
  descriptor->idVendor = 0x1209;
  descriptor->idProduct = 0x2323;
  return 0;
}
int libusb_open(libusb_device *, libusb_device_handle **handle) {
  log("OPEN");
  *handle = reinterpret_cast<libusb_device_handle *>(3);
  return 0;
}
void libusb_close(libusb_device_handle *) { log("CLOSE"); }
int libusb_get_configuration(libusb_device_handle *, int *configuration) {
  *configuration = 0;
  return 0;
}
int libusb_set_configuration(libusb_device_handle *, int) {
  log("CONFIG");
  return 0;
}
int libusb_claim_interface(libusb_device_handle *, int) {
  log("CLAIM");
  return 0;
}
int libusb_release_interface(libusb_device_handle *, int) {
  log("RELEASE");
  return 0;
}
libusb_device *libusb_get_device(libusb_device_handle *) {
  return reinterpret_cast<libusb_device *>(2);
}
int libusb_get_max_packet_size(libusb_device *, unsigned char) { return 512; }
const char *libusb_error_name(int) { return "SHIM_INJECTED_ERROR"; }

int libusb_control_transfer(libusb_device_handle *, uint8_t type,
                            uint8_t request, uint16_t value, uint16_t index,
                            unsigned char *data, uint16_t length,
                            unsigned int) {
  // Validate the entire control header before inspecting request payloads.
  const bool capabilities = request == 4 && type == 0xc1 && length == 40;
  const bool timing = request == 1 && type == 0x41 && length == 20;
  const bool mode = request == 2 && type == 0x41 && length == 8;
  if (value != 0 || index != 0 || !data || !(capabilities || timing || mode)) {
    log("INVALID_CONTROL");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  if (mode && (read32(data) > 1 || read32(data + 4) > 1)) {
    log("INVALID_MODE_FLAGS");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  const char *stage = capabilities              ? "CAPS"
                      : timing                  ? "TIMING"
                      : data[0] && data[4] == 1 ? "START_LISTEN_ONLY"
                      : data[0]                 ? "START"
                                                : "STOP";
  log(stage);
  const char *failure = std::getenv("MEATCAN_SHIM_FAIL");
  if (failure && std::strcmp(failure, stage) == 0)
    return LIBUSB_ERROR_TIMEOUT;
  if (capabilities) {
    uint32_t caps[] = {1, 36000000, 2, 16, 1, 8, 4, 1, 1024, 1};
    if (std::getenv("MEATCAN_SHIM_NO_LISTEN"))
      caps[0] = 0;
    std::memcpy(data, caps, sizeof(caps));
  }
  if (timing) {
    selected_rate =
        36000000 / (read32(data + 16) *
                    (1 + read32(data) + read32(data + 4) + read32(data + 8)));
    if (std::getenv("MEATCAN_SHIM_LOG_RATE")) {
      std::string entry = "RATE " + std::to_string(selected_rate);
      log(entry.c_str());
    }
  }
  if (mode) {
    emitted = 0;
    started = data[0] != 0;
  }
  return length;
}

int libusb_bulk_transfer(libusb_device_handle *, unsigned char endpoint,
                         unsigned char *data, int length, int *transferred,
                         unsigned int) {
  *transferred = 0;
  if (!started) {
    log("PRE_START_BULK");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  if (endpoint == 0x02 && length == 20) {
    log("TX_PROBE");
    if (std::getenv("MEATCAN_SHIM_LOG_ECHO"))
      log(("PROBE_ID " + std::to_string(read32(data))).c_str());
    const char *ack_rate = std::getenv("MEATCAN_SHIM_ACK_RATE");
    if (ack_rate && selected_rate == std::strtoul(ack_rate, nullptr, 10)) {
      std::memcpy(pending_probe, data, sizeof(pending_probe));
      probe_echo_pending = true;
      const char *mismatch = std::getenv("MEATCAN_SHIM_BAD_ECHO");
      if (mismatch && std::strcmp(mismatch, "echo") == 0)
        pending_probe[0] ^= 0x80;
      if (mismatch && std::strcmp(mismatch, "id") == 0)
        pending_probe[4] ^= 1;
      if (mismatch && std::strcmp(mismatch, "dlc") == 0)
        pending_probe[8] = 1;
    } else if (std::getenv("MEATCAN_SHIM_STALE_ECHO")) {
      if (!first_probe_saved) {
        std::memcpy(first_probe, data, sizeof(first_probe));
        first_probe_saved = true;
      } else {
        std::memcpy(pending_probe, first_probe, sizeof(pending_probe));
        probe_echo_pending = true;
      }
    }
    *transferred = length;
    return 0;
  }
  if (endpoint != 0x81 || length != 512) {
    log("INVALID_BULK");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  const char *pattern = std::getenv("MEATCAN_SHIM_RX");
  const char *only_rate = std::getenv("MEATCAN_SHIM_RX_RATE");
  const bool matching_rate =
      !only_rate || selected_rate == std::strtoul(only_rate, nullptr, 10);
  if (probe_echo_pending) {
    std::memcpy(data, pending_probe, sizeof(pending_probe));
    probe_echo_pending = false;
    *transferred = sizeof(pending_probe);
    log("RX_PROBE_ECHO");
    return 0;
  }
  if (started && pattern && matching_rate &&
      (std::strcmp(pattern, "once") != 0 || emitted == 0)) {
    std::memset(data, 0, size_t(length));
    std::memset(data, 0xff, 4);
    data[4] = 0x23;
    data[5] = 0x01;
    data[8] = 1;
    data[12] = 0x42;
    if (std::strcmp(pattern, "echo") == 0)
      data[0] = 1;
    if (std::strcmp(pattern, "error") == 0)
      data[7] |= 0x20;
    if (std::strcmp(pattern, "mixed") == 0 && emitted < 2) {
      if (emitted == 0)
        data[0] = 1;
      else
        data[7] |= 0x20;
    }
    if (std::strcmp(pattern, "invalid_standard") == 0)
      data[5] = 0x08;
    emitted++;
    *transferred = 20;
    log("RX_FRAME");
    return 0;
  }
  usleep(10000);
  return LIBUSB_ERROR_TIMEOUT;
}
}
