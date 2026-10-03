#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Start the composite USB device (HID keyboard + CDC ACM). Safe to call once. */
int usb_composite_start(void);
/* Tee a log line into the RAM ring buffer (dumped with "!log" over CDC). */
void usb_composite_log_tee(const char *s);
#ifdef __cplusplus
}
#endif
