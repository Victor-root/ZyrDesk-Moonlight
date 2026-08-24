#pragma once

// zyr: the one owner of this computer's system key combinations while a
// ZyrDesk session holds the keyboard.
//
// Alt+Tab, Alt+Maj+Tab and Alt+Échap never reach the window they are typed
// at: the system acts on them itself. A remote desktop wants the opposite,
// and the only way Microsoft documents is to step in front of every
// keystroke of the whole computer.
//
// Two of those already existed and neither could work here. SDL's own
// grab, which this mode deliberately leaves off, swallows Alt and Control
// whole; every shortcut ZyrDesk has is an Alt combination held through the
// system's own registration, which never sees a swallowed key, so for as
// long as that grab ran none of them worked. ZyrDesk's own, on the other
// side of the pipe, decided from where the front was, which a session's
// front does not stay: the shell's window switcher took it at the first
// key that got past, and every key after it was let through for want of a
// front, which opened that switcher again.
//
// This one is neither. It runs in the program that really receives the
// keyboard, it asks only whether that program has the focus, and it
// swallows Tab and Échap and nothing else: Alt, Control, Shift and the
// Windows key travel on untouched, so ZyrDesk keeps its own shortcuts and
// this computer keeps its Start menu.
//
// It is laid again at every gaining of the focus, and that is not tidiness.
// The system calls these hooks newest first, and anything installed after
// ours is served before it; a window resized, a full screen entered or a
// floating menu opened is exactly when another program lays one, and the
// journal caught what that costs, a Tab that never reached us at all.
// Laid again, ours is the newest again.

class ZyrSystemKeys
{
public:
    // Whether this mode is the one in force, which is asked once and kept.
    static void setInForce(bool inForce);
    static bool inForce();

    // Watches this window for the keyboard coming and going.
    //
    // Watched at the window itself, and this is the whole of what one
    // round cost. The toolkit decides it has the keyboard by comparing its
    // own window with the one the system calls the front, and this window
    // is carried inside another program's for the length of a session,
    // which makes it a child; the system gives the front to the head of a
    // family and never to a member of it. So the toolkit says the keyboard
    // is gone the first time it goes and can never say it is back, and the
    // journal caught exactly that: five keys carried, the keyboard lost
    // when a menu opened, and not one word for the twenty seconds after.
    //
    // The system tells this window itself, in two messages that owe
    // nothing to the front. Those are what is read.
    static void watch(void* window);
    static void stopWatching();

    // The session is over.
    static void letGo();

    // What has been seen and carried since the last time this was asked,
    // said once a second on the thread that runs the session.
    static void tell();
};
