//src/usb_console.h
#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Public function prototypes
void init_usb_console(void);
void process_usb_console_loop(void);

#ifdef __cplusplus
}
#endif

#endif // USB_CONSOLE_H
