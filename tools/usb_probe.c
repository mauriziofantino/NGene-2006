/** @file usb_probe.c
 * @brief Inspect SiGe USB access without starting sample acquisition.
 */
#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>
/** @brief Enumerate devices, or inspect a SiGe VID/PID given in hexadecimal.
 * @return 0 if enumeration/access succeeds, 1 on error, 2 if the device is absent.
 */
int main(int argc, char **argv)
{
    libusb_context *context = NULL;
    libusb_device **list;
    libusb_device_handle *handle = NULL;
    const struct libusb_version *version = libusb_get_version();
    unsigned long vid = 0x16c0, pid = 0x072f;
    ssize_t count, i;
    int result, found = 0;
    if (argc == 3) {
        vid = strtoul(argv[1], NULL, 16);
        pid = strtoul(argv[2], NULL, 16);
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [hex-vid hex-pid]\n", argv[0]);
        return 1;
    }
    printf("libusb %u.%u.%u; SiGe %04lx:%04lx\n", version->major, version->minor, version->micro,
           vid, pid);
    result = libusb_init(&context);
    if (result < 0) {
        fprintf(stderr, "init: %s\n", libusb_error_name(result));
        return 1;
    }
    count = libusb_get_device_list(context, &list);
    if (count < 0) {
        fprintf(stderr, "enumeration: %s\n", libusb_error_name((int)count));
        libusb_exit(context);
        return 1;
    }
    printf("USB devices: %ld\n", (long)count);
    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) == 0 && desc.idVendor == vid &&
            desc.idProduct == pid) {
            struct libusb_config_descriptor *config = NULL;
            found = 1;
            result = libusb_get_active_config_descriptor(list[i], &config);
            if (result == 0) {
                printf("Active configuration: %u; interfaces: %u\n", config->bConfigurationValue,
                       config->bNumInterfaces);
                libusb_free_config_descriptor(config);
            }
            result = libusb_open(list[i], &handle);
            if (result == 0) {
                unsigned char identity[256];
                printf("Device release: %04x\n", desc.bcdDevice);
                if (desc.iManufacturer &&
                    libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, identity,
                                                       sizeof(identity)) > 0) {
                    printf("Manufacturer: %s\n", identity);
                }
                if (desc.iProduct && libusb_get_string_descriptor_ascii(
                                         handle, desc.iProduct, identity, sizeof(identity)) > 0) {
                    printf("Product: %s\n", identity);
                }
                result = libusb_claim_interface(handle, 2);
                printf("Claim interface 2: %s\n", libusb_error_name(result));
                if (result == 0)
                    libusb_release_interface(handle, 2);
                libusb_close(handle);
            } else {
                fprintf(stderr, "open: %s\n", libusb_error_name(result));
            }
            break;
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(context);
    if (!found) {
        puts("SiGe not found. No hardware transfer was tested.");
        return 2;
    }
    return result < 0 ? 1 : 0;
}
