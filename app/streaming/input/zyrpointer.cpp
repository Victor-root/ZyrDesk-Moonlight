#include "zyrpointer.h"

#include "zyrsystemkeys.h"

#include <QtGlobal>
#include <SDL.h>

#ifdef Q_OS_WIN32
#include <Limelight.h>

#include <climits>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
#ifdef Q_OS_WIN32

// The window this reads at, and whether it is reading.
HWND s_Window = nullptr;
bool s_Reading = false;

// What has gone by since the reading began: movements taken here, and
// movements left to the toolkit because its window really was the one at
// the front. Said when the reading stops, which answers on one line the
// only question this file exists for.
unsigned int s_Taken = 0;
unsigned int s_Left = 0;

// Asks the system for the mouse's own movement, named at this window so
// that it arrives while this window is not the one at the front.
//
// The same ask the toolkit makes, and the system keeps one per program
// for a kind of device: whichever asks last is the one that counts, and
// what this one adds is the naming. Given up the same way, which the
// toolkit then does again to no effect and no harm.
bool askForTheMovement(bool wanted)
{
    RAWINPUTDEVICE mouse = {};
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.dwFlags = wanted ? RIDEV_INPUTSINK : RIDEV_REMOVE;
    mouse.hwndTarget = wanted ? s_Window : nullptr;
    return RegisterRawInputDevices(&mouse, 1, static_cast<UINT>(sizeof(mouse))) != FALSE;
}

// A movement of the device, held to what the protocol carries.
//
// Bounded by hand rather than with the toolkit's, whose overloads cannot
// tell which of the two widths to take when the ends and the middle are
// not written the same.
short asFarAsItGoes(LONG moved)
{
    if (moved < SHRT_MIN) {
        return SHRT_MIN;
    }
    if (moved > SHRT_MAX) {
        return SHRT_MAX;
    }
    return static_cast<short>(moved);
}

#endif
}

void ZyrPointer::watch(void* window)
{
#ifdef Q_OS_WIN32
    // Only under the product that carries this window. What reaches this
    // file is a message read one step in front of the toolkit, and the
    // step in front is laid by that same mode; without it nothing here
    // would ever be handed anything, and asking the system for the
    // mouse's own movement would only take the ask away from the toolkit
    // for nobody's benefit.
    if (!ZyrSystemKeys::ours() || window == nullptr ||
        static_cast<HWND>(window) == s_Window) {
        return;
    }
    stopWatching();
    s_Window = static_cast<HWND>(window);
#else
    Q_UNUSED(window);
#endif
}

void ZyrPointer::stopWatching()
{
#ifdef Q_OS_WIN32
    if (s_Window == nullptr) {
        return;
    }
    setReading(false);
    s_Window = nullptr;
#endif
}

void ZyrPointer::setReading(bool reading)
{
#ifdef Q_OS_WIN32
    if (s_Window == nullptr || reading == s_Reading) {
        return;
    }
    if (reading) {
        if (!askForTheMovement(true)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "zyr: Windows refused the mouse's own movement (error %lu)",
                        GetLastError());
            return;
        }
        s_Taken = 0;
        s_Left = 0;
        s_Reading = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: a game's movement is read from the system itself, "
                    "this window not being the one at the front");
        return;
    }
    s_Reading = false;
    askForTheMovement(false);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "zyr: pointer: %u movements read from the system, %u left to the toolkit",
                s_Taken,
                s_Left);
#else
    Q_UNUSED(reading);
#endif
}

void ZyrPointer::sawRawInput(void* packet)
{
#ifdef Q_OS_WIN32
    if (!s_Reading || packet == nullptr) {
        return;
    }
    // Only ever what the toolkit is about to drop. It reads this very
    // packet after this one does, and keeps it when its window is the
    // one at the front, which is every use of this engine that is not
    // carried inside another program's window. Read in both places, a
    // game would be played at twice the speed of the hand.
    if (GetForegroundWindow() == s_Window) {
        s_Left++;
        return;
    }

    RAWINPUT read = {};
    UINT size = static_cast<UINT>(sizeof(read));
    if (GetRawInputData(static_cast<HRAWINPUT>(packet),
                        RID_INPUT,
                        &read,
                        &size,
                        static_cast<UINT>(sizeof(RAWINPUTHEADER))) == static_cast<UINT>(-1) ||
        read.header.dwType != RIM_TYPEMOUSE) {
        return;
    }

    // A place and not a movement, which is what a tablet and a remote
    // desktop send. Nothing here can turn one into the other without
    // knowing what it is a place on, and a game asked for movement.
    if (read.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
        return;
    }
    if (read.data.mouse.lLastX == 0 && read.data.mouse.lLastY == 0) {
        return;
    }
    s_Taken++;
    LiSendMouseMoveEvent(asFarAsItGoes(read.data.mouse.lLastX),
                         asFarAsItGoes(read.data.mouse.lLastY));
#else
    Q_UNUSED(packet);
#endif
}
