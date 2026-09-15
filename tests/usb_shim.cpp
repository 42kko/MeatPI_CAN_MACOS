// Test-only libusb replacement. Never opens real USB hardware.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <libusb.h>
#include <unistd.h>

static bool started = false;
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
  const char *stage = capabilities ? "CAPS"
                      : timing     ? "TIMING"
                      : data[0]    ? "START"
                                   : "STOP";
  log(stage);
  const char *failure = std::getenv("MEATCAN_SHIM_FAIL");
  if (failure && std::strcmp(failure, stage) == 0)
    return LIBUSB_ERROR_TIMEOUT;
  if (capabilities) {
    uint32_t caps[] = {0, 36000000, 2, 16, 1, 8, 4, 1, 1024, 1};
    std::memcpy(data, caps, sizeof(caps));
  }
  if (mode)
    started = data[0] != 0;
  return length;
}

int libusb_bulk_transfer(libusb_device_handle *, unsigned char endpoint,
                         unsigned char *, int length, int *transferred,
                         unsigned int) {
  *transferred = 0;
  if (!started) {
    log("PRE_START_BULK");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  if (endpoint != 0x81 || length != 512) {
    log("INVALID_BULK");
    return LIBUSB_ERROR_INVALID_PARAM;
  }
  usleep(10000);
  return LIBUSB_ERROR_TIMEOUT;
}
}
