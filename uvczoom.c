// clang -O2 -o uvczoom uvczoom.c -framework IOKit -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VID 0x291A
#define PID 0x3361
#define SET_CUR 0x01
#define GET_CUR 0x81
#define GET_MIN 0x82
#define GET_MAX 0x83
#define GET_RES 0x84
#define GET_DEF 0x87
#define CT_ZOOM_ABSOLUTE 0x0B
#define CT_PANTILT_ABSOLUTE 0x0D

typedef IOUSBDeviceInterface **Dev;

static Dev open_device(void) {
    CFMutableDictionaryRef m = IOServiceMatching("IOUSBHostDevice");
    int v = VID, p = PID;
    CFNumberRef nv = CFNumberCreate(NULL, kCFNumberIntType, &v);
    CFNumberRef np = CFNumberCreate(NULL, kCFNumberIntType, &p);
    CFDictionarySetValue(m, CFSTR("idVendor"), nv);
    CFDictionarySetValue(m, CFSTR("idProduct"), np);
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, m);
    if (!s) { fprintf(stderr, "camera not found\n"); exit(1); }
    IOCFPlugInInterface **plug = NULL;
    SInt32 score;
    kern_return_t kr = IOCreatePlugInInterfaceForService(s, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
    if (kr || !plug) { fprintf(stderr, "plugin failed: 0x%x\n", kr); exit(1); }
    Dev d = NULL;
    (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&d);
    (*plug)->Release(plug);
    if (!d) { fprintf(stderr, "device interface failed\n"); exit(1); }
    return d;
}

static int find_units(Dev d, int *vc_if, int *cam_id) {
    IOUSBConfigurationDescriptorPtr cfg;
    if ((*d)->GetConfigurationDescriptorPtr(d, 0, &cfg)) return -1;
    UInt8 *b = (UInt8 *)cfg, *end = b + USBToHostWord(cfg->wTotalLength);
    int in_vc = 0;
    *vc_if = *cam_id = -1;
    for (; b < end && b[0]; b += b[0]) {
        if (b[1] == 4) {
            in_vc = b[5] == 14 && b[6] == 1;
            if (in_vc && *vc_if < 0) *vc_if = b[2];
        } else if (in_vc && b[1] == 0x24 && b[2] == 2 && (b[4] | b[5] << 8) == 0x0201) {
            *cam_id = b[3];
            printf("camera terminal id=%d controls=", b[3]);
            for (int i = 0; i < b[14]; i++) printf("%02x", b[15 + i]);
            printf("\n");
        } else if (in_vc && b[1] == 0x24 && b[2] == 6) {
            printf("extension unit id=%d guid=", b[3]);
            for (int i = 0; i < 16; i++) printf("%02x", b[4 + i]);
            printf("\n");
        }
    }
    return (*vc_if < 0 || *cam_id < 0) ? -1 : 0;
}

static IOReturn ctl(Dev d, int dir_in, int req, int sel, int unit, int ifn, void *buf, int len) {
    IOUSBDevRequest r = {0};
    r.bmRequestType = dir_in ? 0xA1 : 0x21;
    r.bRequest = req;
    r.wValue = sel << 8;
    r.wIndex = unit << 8 | ifn;
    r.wLength = len;
    r.pData = buf;
    return (*d)->DeviceRequest(d, &r);
}

static void show(Dev d, int cam, int ifn, const char *name, int req) {
    UInt16 v = 0;
    IOReturn e = ctl(d, 1, req, CT_ZOOM_ABSOLUTE, cam, ifn, &v, 2);
    if (e) printf("zoom %s: error 0x%x\n", name, e);
    else printf("zoom %s: %d\n", name, USBToHostWord(v));
}

#define XU_UNIT 6
#define XU_FRAME_SEL 16
#define XU_FRAME_LEN 60

// Same payload AnkerWork logs as [XU SET]: mode (0 fixed, 2 solo), 1, fov.
static int set_frame(Dev d, int ifn, int mode, int fov) {
    UInt8 buf[XU_FRAME_LEN] = {(UInt8)mode, 1, (UInt8)fov};
    IOReturn e = ctl(d, 0, SET_CUR, XU_FRAME_SEL, XU_UNIT, ifn, buf, XU_FRAME_LEN);
    if (e) { fprintf(stderr, "frame set failed: 0x%x\n", e); return -1; }
    usleep(300000);
    return 0;
}

static void xu_dump(Dev d, int ifn) {
    int units[] = {6, 20};
    for (int u = 0; u < 2; u++) for (int sel = 1; sel < 32; sel++) {
        UInt8 info = 0, buf[256];
        UInt16 len = 0;
        if (ctl(d, 1, 0x86, sel, units[u], ifn, &info, 1)) continue;
        if (ctl(d, 1, 0x85, sel, units[u], ifn, &len, 2)) continue;
        len = USBToHostWord(len);
        printf("xu %d sel %d info=%02x len=%d cur=", units[u], sel, info, len);
        if (!len || len > sizeof buf || !(info & 1) || ctl(d, 1, GET_CUR, sel, units[u], ifn, buf, len)) { printf("-\n"); continue; }
        for (int i = 0; i < len; i++) printf("%02x", buf[i]);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    Dev d = open_device();
    int ifn, cam;
    if (find_units(d, &ifn, &cam)) { fprintf(stderr, "no camera terminal\n"); return 1; }
    printf("vc interface=%d\n", ifn);
    if (argc == 2 && !strcmp(argv[1], "xu")) {
        xu_dump(d, ifn);
        return 0;
    }
    int zoom = -1;
    if (argc == 3 && !strcmp(argv[1], "set")) {
        zoom = atoi(argv[2]);
    } else if ((argc == 2 || argc == 3) && !strcmp(argv[1], "solo")) {
        if (set_frame(d, ifn, 2, 115)) return 1;
        if (argc == 3) zoom = atoi(argv[2]);
    } else if (argc == 3 && !strcmp(argv[1], "fixed")) {
        int fov = atoi(argv[2]);
        if (fov != 78 && fov != 90 && fov != 115) { fprintf(stderr, "fixed takes 78, 90 or 115\n"); return 2; }
        if (set_frame(d, ifn, 0, fov)) return 1;
    } else if (argc == 3 && (!strcmp(argv[1], "pan") || !strcmp(argv[1], "tilt"))) {
        int deg = atoi(argv[2]);
        if (deg < -10 || deg > 10) { fprintf(stderr, "degrees must be -10 to 10\n"); return 2; }
        SInt32 pt[2] = {0};
        if (ctl(d, 1, GET_CUR, CT_PANTILT_ABSOLUTE, cam, ifn, pt, 8)) { fprintf(stderr, "pantilt read failed\n"); return 1; }
        pt[argv[1][0] == 't'] = (SInt32)USBToHostLong(deg * 3600);
        IOReturn e = ctl(d, 0, SET_CUR, CT_PANTILT_ABSOLUTE, cam, ifn, pt, 8);
        if (e) { fprintf(stderr, "pantilt set failed: 0x%x\n", e); return 1; }
        usleep(300000);
    } else if (argc != 1) {
        fprintf(stderr, "usage: uvczoom [set N | solo [N] | fixed 78|90|115 | pan DEG | tilt DEG | xu]\n");
        return 2;
    }
    if (zoom >= 0) {
        UInt16 v = USBToHostWord((UInt16)zoom);
        IOReturn e = ctl(d, 0, SET_CUR, CT_ZOOM_ABSOLUTE, cam, ifn, &v, 2);
        if (e) { fprintf(stderr, "set failed: 0x%x\n", e); return 1; }
    }
    UInt8 fr[XU_FRAME_LEN] = {0};
    if (!ctl(d, 1, GET_CUR, XU_FRAME_SEL, XU_UNIT, ifn, fr, XU_FRAME_LEN))
        printf("frame mode=%d fov=%d\n", fr[0], fr[2]);
    show(d, cam, ifn, "cur", GET_CUR);
    show(d, cam, ifn, "min", GET_MIN);
    show(d, cam, ifn, "max", GET_MAX);
    show(d, cam, ifn, "res", GET_RES);
    show(d, cam, ifn, "def", GET_DEF);
    SInt32 pt[2] = {0};
    IOReturn e = ctl(d, 1, GET_CUR, CT_PANTILT_ABSOLUTE, cam, ifn, pt, 8);
    if (e) printf("pantilt cur: error 0x%x\n", e);
    else printf("pan cur: %d tilt cur: %d\n", (SInt32)USBToHostLong(pt[0]), (SInt32)USBToHostLong(pt[1]));
    return 0;
}
