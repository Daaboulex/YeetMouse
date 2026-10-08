#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fail(const char *what) {
    perror(what);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <vendor hex> <name>\n", argv[0]);
        return 2;
    }
    char *end;
    unsigned long vendor = strtoul(argv[1], &end, 16);
    if (*argv[1] == '\0' || *end != '\0' || vendor > 0xffff) {
        fprintf(stderr, "%s is not a 16-bit hex vendor id\n", argv[1]);
        return 2;
    }

    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0)
        return fail("/dev/uinput");
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_REL) < 0 || ioctl(fd, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(fd, UI_SET_RELBIT, REL_Y) < 0)
        return fail("capabilities");

    struct uinput_setup setup;
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = (__u16) vendor;
    setup.id.product = 0x0001;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "%s", argv[2]);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
        return fail("create");

    sleep(1);
    if (ioctl(fd, UI_DEV_DESTROY) < 0)
        return fail("destroy");
    close(fd);
    return 0;
}
