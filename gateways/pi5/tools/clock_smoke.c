/* Copyright (c) 2026 Jonathan Yantis. MIT. Read-only build/clock smoke test. */
#define _GNU_SOURCE
#include <bluetooth/bluetooth.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/ptp_clock.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <systemd/sd-id128.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    bdaddr_t address = {{0}};
    char formatted[18], boot[SD_ID128_STRING_MAX];
    sd_id128_t id;
    struct timespec resolution;
    if (ba2str(&address, formatted) < 0 || sd_id128_get_boot(&id) < 0 ||
        clock_getres(CLOCK_MONOTONIC_RAW, &resolution) < 0)
        return 1;
    sd_id128_to_string(id, boot);
    int fd = open("/dev/ptp0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open /dev/ptp0");
        return 1;
    }
    struct ptp_clock_caps caps = {0};
    if (ioctl(fd, PTP_CLOCK_GETCAPS, &caps) < 0) {
        perror("PTP_CLOCK_GETCAPS");
        close(fd);
        return 1;
    }
    struct ptp_sys_offset_precise precise = {0};
    int precise_rc = ioctl(fd, PTP_SYS_OFFSET_PRECISE, &precise);
    int precise_errno = precise_rc < 0 ? errno : 0;
    struct ptp_sys_offset_extended ext = {.n_samples = 5};
    int extended_rc = ioctl(fd, PTP_SYS_OFFSET_EXTENDED, &ext);
    int extended_errno = extended_rc < 0 ? errno : 0;
    printf("{\"purpose\":\"read-only build and clock capabilities\","
           "\"boot_id\":\"%s\",\"bluez_format_check\":\"%s\","
           "\"nominal_monotonic_raw_resolution_ns\":%ld,"
           "\"phc_external_timestamp_channels\":%d,\"phc_pins\":%d,"
           "\"hardware_cross_timestamping\":%d,"
           "\"precise_ioctl_errno\":%d,\"extended_ioctl_errno\":%d,"
           "\"software_system_phc_read_brackets_ns\":[",
           boot, formatted, resolution.tv_nsec, caps.n_ext_ts, caps.n_pins,
           caps.cross_timestamping, precise_errno, extended_errno);
    if (extended_rc == 0) {
        for (unsigned i = 0; i < ext.n_samples; i++) {
            int64_t span = (ext.ts[i][2].sec - ext.ts[i][0].sec) * INT64_C(1000000000)
                         + (int64_t)ext.ts[i][2].nsec - ext.ts[i][0].nsec;
            printf("%s%" PRId64, i ? "," : "", span);
        }
    }
    puts("],\"radio_timing_measured\":false,\"output_accuracy_measured\":false}");
    close(fd);
    return 0;
}
