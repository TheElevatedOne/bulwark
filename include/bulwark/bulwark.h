/* bulwark - open configuration library for GameSir G7-family controllers.
 *
 * Prototype C API. Profiles are exchanged as JSON in the same shape GameSir Nexus exports.
 * Strings returned by the library are malloc'd; free them with bw_free().
 */
#ifndef BULWARK_H
#define BULWARK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BULWARK_VERSION_MAJOR 0
#define BULWARK_VERSION_MINOR 1
#define BULWARK_VERSION_PATCH 0
#define BULWARK_VERSION_STRING "0.1.0"

#if defined(BULWARK_BUILDING_SHARED)
#define BW_API __attribute__((visibility("default")))
#else
#define BW_API
#endif

typedef struct bw_device bw_device;

enum {
    BW_OK = 0,
    BW_ERR_NOT_FOUND = -1,
    BW_ERR_IO = -2,        /* USB error or no reply */
    BW_ERR_REFUSED = -3,   /* read-only, or profile layout not verified */
    BW_ERR_ARGUMENT = -4,
};

BW_API const char* bw_version(void);

/* Opens the first USB controller. Without allow_writes the device is opened read-only.
 * The kernel driver (xone/xpad) is detached while the handle is open. NULL on failure. */
BW_API bw_device* bw_open_usb(int allow_writes);
/* Opens a simulated G7 Pro (pid 0 = 3537:109B, GIP app mode). */
BW_API bw_device* bw_open_simulated(uint16_t pid);
/* Closes the device. If commands were sent, the controller leaves GIP app mode about 2 s later; bw_close then
 * waits for that (up to 8 s) and switches it back, so the kernel driver keeps a working gamepad. */
BW_API void bw_close(bw_device* dev);

/* Last error message for this handle (or the last failed open when dev is NULL). Do not free. */
BW_API const char* bw_last_error(const bw_device* dev);

BW_API int bw_get_ids(bw_device* dev, uint16_t* vid, uint16_t* pid);
/* Returns the active profile slot (1-4) or a negative error. Setting accepts 1-4. */
BW_API int bw_get_profile_index(bw_device* dev);
BW_API int bw_set_profile_index(bw_device* dev, int slot);
/* Firmware version as reported by the controller, e.g. "0251". NULL on error. Free with bw_free(). */
BW_API char* bw_get_firmware_version(bw_device* dev);

/* Reads a profile slot (1-4, 5 = Shift layer) as JSON. Returns NULL on error. Free with bw_free().
 * Writing re-reads the slot first and only changes the fields the library can write (name, vibration
 * levels); any other difference makes it fail with BW_ERR_REFUSED. */
BW_API char* bw_read_profile_json(bw_device* dev, int slot);
BW_API int bw_write_profile_json(bw_device* dev, int slot, const char* json);

BW_API int bw_rumble(bw_device* dev, uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int duration_ms);

BW_API void bw_free(void* p);

#ifdef __cplusplus
}
#endif

#endif /* BULWARK_H */
