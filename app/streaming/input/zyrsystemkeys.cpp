#include "zyrsystemkeys.h"

#include "zyrpointer.h"

#include <QtGlobal>
#include <SDL.h>

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace
{
bool s_Ours = false;
bool s_Taking = false;
bool s_Focused = false;

#ifdef Q_OS_WIN32

HHOOK s_Hook = nullptr;

// The window this watches, and the handler it had before, so the messages
// it is watched through go on being answered as they always were.
HWND s_Window = nullptr;
WNDPROC s_TheirProc = nullptr;

// Which of Alt and Control a finger is holding, counted from the very
// stream this is filtering.
//
// Bit one for Alt, bit two for Control, either side of the keyboard. Read
// from the stream and not asked of the system: inside a low level hook the
// system is being asked about a key it has not finished with, and what it
// answers there is not a thing to rest a whole feature on.
unsigned int s_Held = 0;

// Which of the keys below this program is holding down on the far
// computer's behalf, one bit each.
//
// A key taken on the way down is taken on the way up as well, whatever has
// happened in between. Left to the ordinary answer, a focus lost between
// the two hands this computer a key released that it never saw pressed.
unsigned int s_Carried = 0;

// What the journal is owed, all of it read a moment later on a thread that
// may write to a file. Nothing here writes: the system holds every
// keystroke of the whole computer until this returns.
unsigned int s_SeenTab[2] = { 0, 0 };
unsigned int s_SeenAlt[2] = { 0, 0 };
unsigned int s_SeenWindows[2] = { 0, 0 };
unsigned int s_Sent = 0;
unsigned int s_PassedNotTaking = 0;
unsigned int s_PassedNoFocus = 0;
unsigned int s_PassedPlain = 0;
unsigned int s_Told = 0;
unsigned int s_Laid = 0;
unsigned int s_Comings = 0;

// The keys this steps in front of, and the bit that remembers each one
// while it is held.
const struct
{
    DWORD key;
    unsigned int bit;
    SDL_Scancode where;
    SDL_Keycode name;
} OURS[] = {
    { VK_TAB,      1, SDL_SCANCODE_TAB,         SDLK_TAB },
    { VK_ESCAPE,   2, SDL_SCANCODE_ESCAPE,      SDLK_ESCAPE },
    { VK_LWIN,     4, SDL_SCANCODE_LGUI,        SDLK_LGUI },
    { VK_RWIN,     8, SDL_SCANCODE_RGUI,        SDLK_RGUI },
    { VK_SNAPSHOT, 16, SDL_SCANCODE_PRINTSCREEN, SDLK_PRINTSCREEN },
    { VK_MEDIA_PLAY_PAUSE, 32, SDL_SCANCODE_AUDIOPLAY, SDLK_AUDIOPLAY },
};

// Whether the system itself calls this keystroke one of its own, which for
// every key but F10 means Alt was held with it.
//
// Free, cannot go stale, and above all cannot be lost: it comes with the
// keystroke instead of being remembered from an earlier one. The stream is
// kept beside it for Control, which no message name tells us about.
bool theSystemCallsItItsOwn(WPARAM what)
{
    return what == WM_SYSKEYDOWN || what == WM_SYSKEYUP;
}

// Whether the system would act on this key itself rather than hand it over.
//
// Tab and Échap on their own are ordinary keys and are left alone: a
// session where Tab moved nothing and Échap closed nothing would be a
// session nobody can work in. It is the company they keep that makes them
// the system's. The Windows key, the screen key and the key that plays and
// pauses keep no company: the system takes each of them alone, and takes
// the Windows key again with whatever follows it. The last of the three is
// taken furthest of all, being handed straight to whatever is playing on
// this computer without any window being consulted.
bool theSystemWouldEatIt(DWORD key, WPARAM what)
{
    bool alt = theSystemCallsItItsOwn(what) || (s_Held & 1);
    switch (key) {
    case VK_TAB:
        return alt;
    case VK_ESCAPE:
        return alt || (s_Held & 2);
    case VK_LWIN:
    case VK_RWIN:
    case VK_SNAPSHOT:
    case VK_MEDIA_PLAY_PAUSE:
        return true;
    default:
        return false;
    }
}

// The bit that remembers this key while it is held, or nought for a key
// that is none of our business.
unsigned int aKeyOfOurs(DWORD key)
{
    for (const auto& ours : OURS) {
        if (ours.key == key) {
            return ours.bit;
        }
    }
    return 0;
}

// Puts that key where every other key of this session goes.
//
// Pushed as one of the toolkit's own events rather than sent back out as a
// keystroke: a keystroke sent back out would be read by the system first,
// exactly as the one just taken was. The modifiers are read from the
// toolkit, which has them right because Alt, Control and Shift are never
// swallowed here and reach it as they always did.
void handItOver(DWORD key, bool up)
{
    for (const auto& ours : OURS) {
        if (ours.key != key) {
            continue;
        }
        SDL_Event event;
        SDL_zero(event);
        event.type = up ? SDL_KEYUP : SDL_KEYDOWN;
        event.key.timestamp = SDL_GetTicks();
        event.key.state = up ? SDL_RELEASED : SDL_PRESSED;
        event.key.repeat = 0;
        event.key.keysym.scancode = ours.where;
        event.key.keysym.sym = ours.name;
        event.key.keysym.mod = SDL_GetModState();
        SDL_PushEvent(&event);
        if (!up) {
            s_Sent++;
        }
        return;
    }
}

// Whether a keystroke typed right now would really come to this window.
//
// The focus and the front are two different things and this needs both.
// The program that carries this window joins its input to this one's and
// hands the focus back to the picture at every turn of its watch, and that
// succeeds whatever holds the front; so the focus alone answers yes while
// somebody is working in another program of this computer. A session did
// exactly that, and swallowed seventeen Alt+Tab meant for a window here.
//
// Asked as one question rather than as two answers compared: what the
// system gives back is the window that holds the keyboard inside the input
// the front belongs to, which is the whole of what is being asked. It is a
// reading of what the system already knows and waits on nobody.
bool theKeyboardIsReallyOurs()
{
    GUITHREADINFO front;
    front.cbSize = sizeof(front);
    if (!GetGUIThreadInfo(0, &front)) {
        return false;
    }
    return front.hwndFocus == s_Window;
}

LRESULT CALLBACK zyrKeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    const KBDLLHOOKSTRUCT* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

    // What a modifier is doing follows the hand and only the hand. A
    // keystroke another program sent is not a finger on a key, and
    // letting it drive this had it contradict the finger in front of it.
    // Nothing below reads it: where a keystroke goes is decided by where
    // the keyboard is, whoever produced it.
    const bool aFinger = (key->flags & LLKHF_INJECTED) == 0;
    if (aFinger) {
        switch (key->vkCode) {
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            s_Held = up ? (s_Held & ~1u) : (s_Held | 1u);
            s_SeenAlt[up ? 1 : 0]++;
            break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            s_Held = up ? (s_Held & ~2u) : (s_Held | 2u);
            break;
        case VK_TAB:
            s_SeenTab[up ? 1 : 0]++;
            break;
        case VK_LWIN:
        case VK_RWIN:
            s_SeenWindows[up ? 1 : 0]++;
            break;
        default:
            break;
        }
    }

    // Alt, Control and Shift are never swallowed, and that is the whole of
    // how ZyrDesk keeps its own shortcuts: they are held through the
    // system's own registration, which is served after this hook and never
    // sees a key taken here.
    const unsigned int bit = aKeyOfOurs(key->vkCode);
    if (bit == 0) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    if (up && (s_Carried & bit)) {
        // Taken on the way down, so taken on the way up, wherever the
        // focus has gone in between and whichever way the switch has been
        // thrown since.
        s_Carried &= ~bit;
        handItOver(key->vkCode, true);
        return 1;
    }

    if (!s_Taking) {
        s_PassedNotTaking++;
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    // Who produced a keystroke does not decide where it goes; where the
    // keyboard is does. This used to hand every injected one back to the
    // system, and that was one question too many: an Alt+Tab sent by a
    // program is aimed at the window that has the keyboard exactly as a
    // finger's is, and given back it acted on this computer instead. It
    // is what the program carrying this window sends when a hand asks for
    // the far computer's next window from a touchpad, and what an
    // on-screen keyboard sends for somebody who cannot use a real one.
    //
    // The distinction is kept where it belongs, above: what a modifier is
    // doing follows the hand, and letting an injected keystroke drive
    // that had this contradict the finger in front of it.
    if (!s_Focused || !theKeyboardIsReallyOurs()) {
        s_PassedNoFocus++;
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    if (up || !theSystemWouldEatIt(key->vkCode, wParam)) {
        s_PassedPlain++;
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    s_Carried |= bit;
    handItOver(key->vkCode, false);
    return 1;
}

void keyboardIsHere(bool here);

// Steps in front of this window's own messages, for the two that say the
// keyboard has come and gone.
//
// Both are sent to the window that gains or loses it, whatever holds the
// front, so they say the one thing that matters here and the toolkit's own
// reading cannot; see the header.
//
// And the mouse's own movement passes here too, this being the one place
// in the engine where this window's messages are seen before the toolkit
// reads them. It is a different subject and lives in its own file; what
// it needs from this one is the step in front, which there can only be
// one of. Left to the toolkit as well, since it drops the ones that
// matter and keeps the rest; see zyrpointer.h.
LRESULT CALLBACK zyrWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_SETFOCUS) {
        keyboardIsHere(true);
    }
    else if (message == WM_KILLFOCUS) {
        keyboardIsHere(false);
    }
    else if (message == WM_INPUT) {
        ZyrPointer::sawRawInput(reinterpret_cast<void*>(lParam));
    }
    return CallWindowProcW(s_TheirProc, window, message, wParam, lParam);
}

// Lays the hook, taking the old one off first so this one is the newest of
// the chain again; see the header.
void layItAgain()
{
    if (s_Hook != nullptr) {
        UnhookWindowsHookEx(s_Hook);
        s_Hook = nullptr;
    }
    s_Hook = SetWindowsHookExW(WH_KEYBOARD_LL, zyrKeyboardHookProc, GetModuleHandleW(nullptr), 0);
    s_Laid++;
    if (s_Hook == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: Windows refused the system key hook (error %lu)",
                    GetLastError());
    }
}

// Gives back whatever is being held down on the far computer's behalf.
//
// The far computer is told first: a session that keeps a Tab down because
// the focus left between the press and the release goes on believing it,
// and every key after it arrives there with a Tab held.
void giveBackWhatIsHeld()
{
    for (const auto& ours : OURS) {
        if (s_Carried & ours.bit) {
            s_Carried &= ~ours.bit;
            handItOver(ours.key, true);
        }
    }
}

// Takes the hook off, and gives back whatever it was holding down.
void takeItOff()
{
    giveBackWhatIsHeld();
    if (s_Hook != nullptr) {
        UnhookWindowsHookEx(s_Hook);
        s_Hook = nullptr;
    }
}

// The keyboard has come to this window, or left it.
//
// Coming lays the hook again, and that is not tidiness: the system serves
// these newest first, and whatever another program laid while the keyboard
// was elsewhere is served before an older one. A window resized, a full
// screen entered, a menu opened: each of those is a leaving and a coming,
// so each of them puts this back at the head.
//
// The keyboard decides this and the switch does not. A hook that came and
// went with the switch would forget which modifiers a finger is holding
// every time it came back, and would stop counting the keys it lets
// through, which is the very count that says why a key did not travel.
// What the switch decides is what the hook does with a key, not whether it
// sees one.
void keyboardIsHere(bool here)
{
    if (here == s_Focused) {
        return;
    }
    s_Focused = here;
    if (here) {
        s_Comings++;
        layItAgain();
    }
    else {
        takeItOff();
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "zyr: the session %s the keyboard",
                here ? "has" : "has lost");
}

#endif
}

void ZyrSystemKeys::begin(bool ours, bool taking)
{
    s_Ours = ours;
    s_Taking = ours && taking;
    if (ours) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: the system's keys are this engine's and it %s them for now; "
                    "this computer keeps Alt, Control and Shift either way",
                    s_Taking ? "takes" : "leaves");
    }
}

bool ZyrSystemKeys::ours()
{
    return s_Ours;
}

void ZyrSystemKeys::setTaking(bool taking)
{
    if (!s_Ours || taking == s_Taking) {
        return;
    }
    s_Taking = taking;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "zyr: the system's keys now go to %s",
                taking ? "the session" : "this computer");
#ifdef Q_OS_WIN32
    if (!taking) {
        giveBackWhatIsHeld();
    }
#endif
}

bool ZyrSystemKeys::taking()
{
    return s_Taking;
}

bool ZyrSystemKeys::hasTheKeyboard()
{
#ifdef Q_OS_WIN32
    return s_Ours && s_Taking && s_Focused && theKeyboardIsReallyOurs();
#else
    return false;
#endif
}

void ZyrSystemKeys::watch(void* window)
{
    if (!s_Ours || window == nullptr) {
        return;
    }
#ifdef Q_OS_WIN32
    if (s_Window == reinterpret_cast<HWND>(window)) {
        return;
    }
    stopWatching();
    s_Window = reinterpret_cast<HWND>(window);
    s_TheirProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(s_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(zyrWindowProc)));
    // Where it stands right now, nothing being said about it until it next
    // moves. The window is asked of the input this program shares with the
    // one that carries it, which is the only place that answer lives.
    keyboardIsHere(GetFocus() == s_Window);
#endif
}

void ZyrSystemKeys::stopWatching()
{
#ifdef Q_OS_WIN32
    if (s_Window == nullptr) {
        return;
    }
    keyboardIsHere(false);
    SetWindowLongPtrW(s_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_TheirProc));
    s_Window = nullptr;
    s_TheirProc = nullptr;
#endif
}

void ZyrSystemKeys::letGo()
{
    if (!s_Ours) {
        return;
    }
#ifdef Q_OS_WIN32
    stopWatching();
    tell();
    s_Held = 0;
    s_SeenTab[0] = s_SeenTab[1] = 0;
    s_SeenAlt[0] = s_SeenAlt[1] = 0;
    s_SeenWindows[0] = s_SeenWindows[1] = 0;
    s_Sent = 0;
    s_PassedNotTaking = 0;
    s_PassedNoFocus = 0;
    s_PassedPlain = 0;
    s_Told = 0;
    s_Laid = 0;
    s_Comings = 0;
#endif
}

void ZyrSystemKeys::tell()
{
#ifdef Q_OS_WIN32
    const unsigned int seen = s_Sent + s_PassedNotTaking + s_PassedNoFocus + s_PassedPlain;
    if (!s_Ours || s_Told == seen) {
        return;
    }
    s_Told = seen;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "zyr: system keys: Tab %u down %u up, Windows %u down %u up, Alt %u down %u up ; "
                "%u carried to the host ; passed: %u switch off, %u without the keyboard, "
                "%u plain ; hook laid %u times over %u comings of the keyboard, "
                "switch on %s, keyboard %s, holding %u",
                s_SeenTab[0], s_SeenTab[1], s_SeenWindows[0], s_SeenWindows[1],
                s_SeenAlt[0], s_SeenAlt[1],
                s_Sent, s_PassedNotTaking, s_PassedNoFocus, s_PassedPlain,
                s_Laid, s_Comings, s_Taking ? "the session" : "this computer",
                s_Focused ? "here" : "elsewhere", s_Carried);
#endif
}
