/* C API smoke test against the simulator. */
#include <bulwark/bulwark.h>

#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            return 1;                                               \
        }                                                           \
    } while (0)

int main(void) {
    CHECK(strcmp(bw_version(), BULWARK_VERSION_STRING) == 0);
    bw_device* dev = bw_open_simulated(0);
    CHECK(dev);
    uint16_t vid = 0, pid = 0;
    CHECK(bw_get_ids(dev, &vid, &pid) == BW_OK && vid == 0x3537 && pid == 0x109B);
    CHECK(bw_get_profile_index(dev) == 1);
    CHECK(bw_set_profile_index(dev, 4) == BW_OK && bw_get_profile_index(dev) == 4);
    char* fw = bw_get_firmware_version(dev);
    CHECK(fw && strcmp(fw, "0251") == 0);
    bw_free(fw);
    char* json = bw_read_profile_json(dev, 1);
    CHECK(json && strstr(json, "KeyProfiles"));
    CHECK(bw_write_profile_json(dev, 2, json) == BW_OK);
    bw_free(json);
    CHECK(bw_write_profile_json(dev, 2, "not json") == BW_ERR_ARGUMENT);
    CHECK(bw_rumble(dev, 0, 0, 10, 10, 50) == BW_OK);
    bw_close(dev);
    puts("capi ok");
    return 0;
}
