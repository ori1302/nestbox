#include "usb_port.h"
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <atomic>
#include <cstring>

// Interfaces are registered as these objects are constructed, so this order is the order the computer sees.
USBCDC console(0);

namespace usb {

namespace {
constexpr const char* DRIVE_PARTITION = "drive";
constexpr auto DRIVE_SUBTYPE = esp_partition_subtype_t(0x40);   // partitions.csv: a raw FAT image
constexpr uint32_t SECTOR = 512;
constexpr size_t BOOT_SIGNATURE = 510;     // 0x55 0xAA ends a FAT boot sector; erased flash reads 0xFF

USBMSC drive;
const uint8_t* image = nullptr;            // the drive partition, memory-mapped: reads never stall the other core
uint32_t image_size = 0;
esp_partition_mmap_handle_t mapping;
std::atomic<bool> mounted{false}, suspended{false};
std::atomic<uint32_t> suspended_at{0};

int32_t on_read(uint32_t lba, uint32_t offset, void* buf, uint32_t n) {
    const uint32_t at = lba * SECTOR + offset;
    if (!image || at + n > image_size) return -1;
    memcpy(buf, image + at, n);
    return int32_t(n);
}

int32_t on_write(uint32_t, uint32_t, uint8_t*, uint32_t) { return -1; }

// Runs on the USB event task: flags only.
void on_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT:          // a host configured the device
            mounted = true;
            suspended = false;
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            mounted = false;
            suspended = false;
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            suspended_at = millis();
            suspended = true;
            break;
        case ARDUINO_USB_RESUME_EVENT:
            suspended = false;
            break;
        default:
            break;
    }
}

bool map_drive() {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, DRIVE_SUBTYPE, DRIVE_PARTITION);
    const void* ptr = nullptr;
    if (p && esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &ptr, &mapping) == ESP_OK) {
        image = static_cast<const uint8_t*>(ptr);
        image_size = p->size;
    }
    return image && image[BOOT_SIGNATURE] == 0x55 && image[BOOT_SIGNATURE + 1] == 0xAA;
}
}  // namespace

bool begin() {
    bool ok = map_drive();
    USB.productName("NestBox");
    USB.manufacturerName("NestBox");
    USB.onEvent(on_event);
    console.begin();
    drive.vendorID("NestBox");
    drive.productID("NestBox");
    drive.productRevision(NESTBOX_USB_REVISION);
    drive.onRead(on_read);
    drive.onWrite(on_write);
    drive.isWritable(false);
    ok = ok && drive.begin(image_size / SECTOR, SECTOR);
    drive.mediaPresent(ok);                // without an image the slot is empty and nothing mounts
    USB.begin();
    return ok;
}

bool host_asleep(uint32_t now) { return mounted && suspended && now - suspended_at >= ASLEEP_MS; }

const char* status() {
    if (!mounted) return "usb: no computer (power only, or not set up yet)";
    return suspended ? "usb: computer asleep (bus suspended)" : "usb: computer awake";
}

}  // namespace usb
