#pragma once

// zyr: the one owner of this computer's system key combinations while a
// ZyrDesk session holds the keyboard.
//
// Alt+Tab, Alt+Maj+Tab, Alt+Échap and the Windows key never reach the
// window they are typed at: the system acts on them itself. A remote
// desktop wants the opposite, and the only way Microsoft documents is to
// step in front of every keystroke of the whole computer.
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
// swallows Tab, Échap and the Windows key and nothing else: Alt, Control
// and Shift travel on untouched, so ZyrDesk keeps its own shortcuts.
//
// It is laid again at every gaining of the focus, and that is not tidiness.
// The system calls these hooks newest first, and anything installed after
// ours is served before it; a window resized, a full screen entered or a
// floating menu opened is exactly when another program lays one, and the
// journal caught what that costs, a Tab that never reached us at all.
// Laid again, ours is the newest again.
//
// # Taking them, and letting them go
//
// Taking them all the time is not what a session wants either: the hand
// that reaches for Alt+Tab is sometimes reaching for a window of this very
// computer, and the Windows key sometimes means this Start menu. So the
// taking is a switch, thrown from the menu of the program that carries
// this window and never from a preference file: while it is off, every one
// of these keys is left to this computer exactly as though no session were
// running, and the far computer hears none of them.
//
// Which side the switch starts on is the whole of the difference between
// the two modes on the command line. Nothing else about them differs.

class ZyrSystemKeys
{
public:
    // This mode is the one running, and which side its switch starts on.
    // Asked once, from the command line.
    static void begin(bool ours, bool taking);

    // Whether this mode is the one running at all.
    static bool ours();

    // Whether the keys are being taken right now, and the switch for it.
    //
    // Turning it off gives back on the spot whatever is being held down on
    // the far computer's behalf: a Tab pressed while the switch was on and
    // released after it went off would otherwise stay down over there for
    // ever.
    static void setTaking(bool taking);
    static bool taking();

    // Whether a key typed at this very instant would be taken here.
    //
    // What the rest of the engine has to ask before it sends the Windows
    // key or the Meta modifier onward. Its own answer to that question is
    // read from two window flags, and neither can be true here: a window
    // carried inside another program's is never the one the system calls
    // the front, and this mode leaves the toolkit's keyboard grab off on
    // purpose. Asked there, the Windows key was dropped for the length of
    // every session, which is the fault this replaces.
    static bool hasTheKeyboard();

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
