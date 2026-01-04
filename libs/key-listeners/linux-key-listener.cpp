#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <X11/Xlib.h>
#include <X11/extensions/record.h>

//#region Evdev to X11 Keycode Mapping
// evdev keycodes are offset by 8 from X11 keycodes
inline int evdevToX11Keycode(int evdevCode) {
    return evdevCode + 8;
}
//#endregion

//#region X11 Backend
Display *dpy = nullptr;
XRecordContext context = 0;
volatile bool running = true;

void x11Callback(XPointer, XRecordInterceptData *data) {
    if (!running) {
        if (data) XRecordFreeData(data);
        return;
    }
    
    if (!data || data->category != XRecordFromServer) {
        if (data) XRecordFreeData(data);
        return;
    }

    unsigned char *d = reinterpret_cast<unsigned char*>(data->data);
    int type = d[0];
    if (type != KeyPress && type != KeyRelease) {
        XRecordFreeData(data);
        return;
    }

    int keycode = d[1];
    unsigned int state = (d[28]) | (d[29] << 8) | (d[30] << 16) | (d[31] << 24);
    bool shift = (state & ShiftMask) != 0;
    bool ctrl = (state & ControlMask) != 0;
    bool alt = (state & Mod1Mask) != 0;

    const char* t = (type == KeyPress) ? "keydown" : "keyup";
    std::cout << "{\"type\":\"" << t << "\",\"keycode\":" << keycode
              << ",\"shift\":" << (shift ? "true" : "false")
              << ",\"ctrl\":" << (ctrl ? "true" : "false")
              << ",\"alt\":" << (alt ? "true" : "false")
              << "}" << std::endl;

    XRecordFreeData(data);
}

bool runX11Backend() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;

    int major, minor;
    if (!XRecordQueryVersion(dpy, &major, &minor)) { 
        XCloseDisplay(dpy); 
        return false; 
    }

    XRecordRange *range = XRecordAllocRange();
    range->device_events.first = KeyPress;
    range->device_events.last = KeyRelease;

    XRecordClientSpec clients = XRecordAllClients;
    context = XRecordCreateContext(dpy, 0, &clients, 1, &range, 1);
    XFree(range);
    if (!context) { 
        XCloseDisplay(dpy); 
        return false; 
    }

    XRecordEnableContext(dpy, context, x11Callback, nullptr);

    XRecordDisableContext(dpy, context);
    XRecordFreeContext(dpy, context);
    XCloseDisplay(dpy);
    return true;
}
//#endregion

//#region Evdev Backend (Wayland/Universal)
int evdevFd = -1;
struct libevdev *evdevDev = nullptr;

// Find keyboard device in /dev/input/
std::string findKeyboardDevice() {
    DIR *dir = opendir("/dev/input");
    if (!dir) return "";

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct libevdev *dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            close(fd);
            continue;
        }

        // Check if this device has keyboard capabilities
        bool hasKeys = libevdev_has_event_type(dev, EV_KEY) &&
                       libevdev_has_event_code(dev, EV_KEY, KEY_A) &&
                       libevdev_has_event_code(dev, EV_KEY, KEY_Z);

        libevdev_free(dev);
        close(fd);

        if (hasKeys) {
            closedir(dir);
            return path;
        }
    }

    closedir(dir);
    return "";
}

bool runEvdevBackend() {
    std::string devicePath = findKeyboardDevice();
    if (devicePath.empty()) {
        std::cerr << "No keyboard device found. Make sure you have permission to read /dev/input/" << std::endl;
        return false;
    }

    evdevFd = open(devicePath.c_str(), O_RDONLY);
    if (evdevFd < 0) {
        std::cerr << "Cannot open " << devicePath << ". Permission denied?" << std::endl;
        return false;
    }

    if (libevdev_new_from_fd(evdevFd, &evdevDev) < 0) {
        close(evdevFd);
        return false;
    }

    // Track modifier state
    bool shift = false, ctrl = false, alt = false;

    struct input_event ev;
    while (running) {
        int rc = libevdev_next_event(evdevDev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, &ev);
        
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_KEY) {
                // Update modifier state
                if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                    shift = (ev.value != 0);
                }
                if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
                    ctrl = (ev.value != 0);
                }
                if (ev.code == KEY_LEFTALT || ev.code == KEY_RIGHTALT) {
                    alt = (ev.value != 0);
                }

                // ev.value: 0 = release, 1 = press, 2 = repeat
                if (ev.value == 0 || ev.value == 1) {
                    const char* t = (ev.value == 1) ? "keydown" : "keyup";
                    int x11Keycode = evdevToX11Keycode(ev.code);
                    
                    std::cout << "{\"type\":\"" << t << "\",\"keycode\":" << x11Keycode
                              << ",\"shift\":" << (shift ? "true" : "false")
                              << ",\"ctrl\":" << (ctrl ? "true" : "false")
                              << ",\"alt\":" << (alt ? "true" : "false")
                              << "}" << std::endl;
                }
            }
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Re-sync - device state changed while we weren't reading
            while (libevdev_next_event(evdevDev, LIBEVDEV_READ_FLAG_SYNC, &ev) == LIBEVDEV_READ_STATUS_SYNC) {
                // Process sync events
            }
        } else if (rc == -EAGAIN) {
            // No events available, continue
            usleep(1000);
        } else {
            // Error
            break;
        }
    }

    libevdev_free(evdevDev);
    close(evdevFd);
    return true;
}
//#endregion

//#region Signal Handling
void signalHandler(int) {
    running = false;
    if (context && dpy) {
        XRecordDisableContext(dpy, context);
    }
    if (evdevDev) {
        libevdev_grab(evdevDev, LIBEVDEV_UNGRAB);
    }
}
//#endregion

//#region Backend Detection
bool isWaylandSession() {
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* xdgSessionType = std::getenv("XDG_SESSION_TYPE");
    
    // Check if WAYLAND_DISPLAY is set
    if (waylandDisplay && strlen(waylandDisplay) > 0) {
        return true;
    }
    
    // Check XDG_SESSION_TYPE
    if (xdgSessionType && strcmp(xdgSessionType, "wayland") == 0) {
        return true;
    }
    
    return false;
}

bool canUseX11() {
    Display *testDpy = XOpenDisplay(nullptr);
    if (testDpy) {
        int major, minor;
        bool hasRecord = XRecordQueryVersion(testDpy, &major, &minor);
        XCloseDisplay(testDpy);
        return hasRecord;
    }
    return false;
}
//#endregion

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    bool forceX11 = false;
    bool forceEvdev = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--x11") == 0) forceX11 = true;
        if (strcmp(argv[i], "--evdev") == 0 || strcmp(argv[i], "--wayland") == 0) forceEvdev = true;
    }

    // Determine which backend to use
    bool useEvdev = false;
    
    if (forceX11) {
        useEvdev = false;
    } else if (forceEvdev) {
        useEvdev = true;
    } else {
        // Auto-detect: prefer X11 on X11 sessions, evdev on Wayland
        if (isWaylandSession()) {
            useEvdev = true;
        } else if (canUseX11()) {
            useEvdev = false;
        } else {
            // Fallback to evdev if X11 is not available
            useEvdev = true;
        }
    }

    bool success = false;
    
    if (useEvdev) {
        success = runEvdevBackend();
        if (!success && !forceEvdev && canUseX11()) {
            success = runX11Backend();
        }
    } else {
        success = runX11Backend();
        if (!success && !forceX11) {
            success = runEvdevBackend();
        }
    }

    return success ? 0 : 1;
}