#include "streaming/session.h"
#include "streaming/input/zyrsystemkeys.h"

#include <Limelight.h>
#include <SDL.h>

#define VK_0 0x30
#define VK_A 0x41

// These are real Windows VK_* codes
#ifndef VK_F1
#define VK_F1 0x70
#define VK_F13 0x7C
#define VK_NUMPAD0 0x60
#endif

// zyr: the four this file presses on its own, for gestures that never
// touch this computer's keyboard. Guarded one by one rather than in a
// block: whichever of them a platform header already spells, it spells
// the same, and taking one from there and three from here is how a
// keycode ends up meaning two things.
#ifndef VK_TAB
#define VK_TAB 0x09
#endif
#ifndef VK_SHIFT
#define VK_SHIFT 0x10
#endif
#ifndef VK_MENU
#define VK_MENU 0x12
#endif
#ifndef VK_MEDIA_PLAY_PAUSE
#define VK_MEDIA_PLAY_PAUSE 0xB3
#endif

void SdlInputHandler::performSpecialKeyCombo(KeyCombo combo)
{
    switch (combo) {
    case KeyComboQuit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quit key combo");

        // Push a quit event to the main loop
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
        break;

    case KeyComboUngrabInput:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse capture toggle combo");

        // Stop handling future input
        setCaptureActive(!isCaptureActive());

        // Force raise all keys to ensure they aren't stuck,
        // since we won't get their key up events.
        raiseAllKeys();
        break;

    case KeyComboToggleFullScreen:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected full-screen toggle combo");
        Session::s_ActiveSession->toggleFullscreen();

        // Force raise all keys just be safe across this full-screen/windowed
        // transition just in case key events get lost.
        raiseAllKeys();
        break;

    case KeyComboToggleStatsOverlay:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected stats toggle combo");

        // Toggle the stats overlay
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
                                                            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug));
        break;

    case KeyComboToggleMouseMode:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse mode toggle combo");

        // Uncapture input
        setCaptureActive(false);

        // Toggle mouse mode
        m_AbsoluteMouseMode = !m_AbsoluteMouseMode;

        // Recapture input
        setCaptureActive(true);
        break;

    case KeyComboToggleCursorHide:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected show mouse combo");

        if (!SDL_GetRelativeMouseMode()) {
            m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;
            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Cursor can only be shown in remote desktop mouse mode");
        }
        break;

    case KeyComboToggleMinimize:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected minimize combo");
        SDL_MinimizeWindow(m_Window);
        break;

    case KeyComboPasteText:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected type clipboard text combo");

        // Force raise all keys to ensure that none of them interfere
        // with the text we're going to type.
        raiseAllKeys();

        char* text;
        if (SDL_HasClipboardText() && (text = SDL_GetClipboardText()) != nullptr) {
            // Sending both CR and LF will lead to two newlines in the destination for
            // each newline in the source, so we fix up any CRLFs into just a single LF.
            for (char* c = text; *c != 0; c++) {
                if (*c == '\r' && *(c + 1) == '\n') {
                    // We're using strlen() rather than strlen() - 1 since we need to add 1
                    // to copy the null terminator which is not included in strlen()'s count.
                    memmove(c, c + 1, strlen(c));
                }
            }

            // Send this text to the PC
            LiSendUtf8TextEvent(text, (unsigned int)strlen(text));

            // SDL_GetClipboardText() allocates, so we must free
            SDL_free((void*)text);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No text in clipboard to paste!");
        }
        break;
    }

    case KeyComboTogglePointerRegionLock:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected pointer region lock toggle combo");
        m_PointerRegionLockActive = !m_PointerRegionLockActive;

        // Remember that the user changed this manually, so we don't mess with it anymore
        // during windowed <-> full-screen transitions.
        m_PointerRegionLockToggledByUser = true;

        // Apply the new region lock
        updatePointerRegionLock();
        break;

    case KeyComboToggleSystemKeys:
        // zyr: Alt+Tab, Échap, the Windows key and the screen key change
        // hands. Every key held on the far computer's behalf is given
        // back on the way, so one pressed on one side of the switch and
        // released on the other does not stay down over there. And the
        // toolkit is told again about Alt+F4, which is the one of these
        // it acts on itself.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected system keys toggle combo");
        ZyrSystemKeys::setTaking(!ZyrSystemKeys::taking());
        updateKeyboardGrabState();
        break;

    case KeyComboWindowAfter:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: the window after, on the far computer");
        zyrTheWindowAfter(false);
        break;

    case KeyComboWindowBefore:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: the window before, on the far computer");
        zyrTheWindowAfter(true);
        break;

    case KeyComboPlayPause:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: play or pause, on the far computer");
        zyrPlayOrPause();
        break;

    case KeyComboSlideOver:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: touchpad: the hand is up, letting the far computer's Alt go");
        zyrTheHandIsUp();
        break;

    default:
        Q_UNREACHABLE();
    }
}

// zyr: presses Alt+Tab on the far computer, or Alt+Maj+Tab to go the
// other way, and on that computer only.
//
// Sent straight down the session's own input stream, which is the whole
// point of it. The road it replaces was to type Alt+Tab on this
// computer's keyboard and have our own hook take it back: Windows keeps
// that combination for itself here, the hook is the one thing that ever
// takes it back, and the hook is only laid while the picture holds the
// keyboard. A gesture made on a touchpad that has been handed to the
// session should not depend on which of this machine's windows is in
// front, and on this road it does not.
void SdlInputHandler::zyrTheWindowAfter(bool back)
{
    // Alt is held from one step to the next, and that is what shows the
    // far computer's window list and keeps it up while the hand slides:
    // it is the same thing that holds it up under a hand that taps Tab
    // several times without letting Alt go. Pressed and released at every
    // step, the list opened and shut too fast to be read, and one changed
    // windows without seeing which one was coming.
    //
    // Everything else still down is given back. What brought us here is
    // Ctrl+Alt+Maj+O, sent by ZyrDesk, whose three modifiers have already
    // gone over as pressed: left alone, the far computer would be asked
    // for Ctrl+Alt+Maj+Tab, which is not what anyone did with their hand.
    // Our own Alt is taken out of that list first so the emptying does not
    // release it, and put back after, which is what lets the end of the
    // session release it if the hand never comes up.
    const bool held = m_ZyrHoldsAlt;
    m_KeysDown.remove(VK_MENU);
    raiseAllKeys();
    if (!held) {
        LiSendKeyboardEvent(0x8000 | VK_MENU, KEY_ACTION_DOWN, MODIFIER_ALT);
    }
    m_ZyrHoldsAlt = true;
    m_KeysDown.insert(VK_MENU);

    const char with = back ? (MODIFIER_ALT | MODIFIER_SHIFT) : MODIFIER_ALT;
    if (back) {
        LiSendKeyboardEvent(0x8000 | VK_SHIFT, KEY_ACTION_DOWN, with);
    }
    LiSendKeyboardEvent(0x8000 | VK_TAB, KEY_ACTION_DOWN, with);
    LiSendKeyboardEvent(0x8000 | VK_TAB, KEY_ACTION_UP, with);
    if (back) {
        LiSendKeyboardEvent(0x8000 | VK_SHIFT, KEY_ACTION_UP, MODIFIER_ALT);
    }
}

// zyr: lets go of the Alt a slide was holding, the hand having left the
// pad. That is what picks the window the list is showing.
//
// Nothing to do when no slide is holding one, which is most of the time:
// the end of a slide is said whether or not this engine was the one
// carrying it, and a session that started in the middle of a hand's
// gesture has no Alt of its own down.
void SdlInputHandler::zyrTheHandIsUp()
{
    if (!m_ZyrHoldsAlt) {
        return;
    }
    m_ZyrHoldsAlt = false;
    m_KeysDown.remove(VK_MENU);
    LiSendKeyboardEvent(0x8000 | VK_MENU, KEY_ACTION_UP, 0);
}

// zyr: the play/pause key, on the far computer alone.
//
// It travels this road for the reason Alt+Tab does: typed here, Windows
// hands it to whatever is playing on this computer, and the session never
// sees it.
void SdlInputHandler::zyrPlayOrPause()
{
    raiseAllKeys();
    LiSendKeyboardEvent(0x8000 | VK_MEDIA_PLAY_PAUSE, KEY_ACTION_DOWN, 0);
    LiSendKeyboardEvent(0x8000 | VK_MEDIA_PLAY_PAUSE, KEY_ACTION_UP, 0);
}

void SdlInputHandler::handleKeyEvent(SDL_KeyboardEvent* event)
{
    short keyCode;
    char modifiers;

    if (event->repeat) {
        // Ignore repeat key down events
        SDL_assert(event->state == SDL_PRESSED);
        return;
    }

    // Check for our special key combos
    if ((event->state == SDL_PRESSED) &&
            (event->keysym.mod & KMOD_CTRL) &&
            (event->keysym.mod & KMOD_ALT) &&
            (event->keysym.mod & KMOD_SHIFT)) {
        // First we test the SDLK combos for matches,
        // that way we ensure that latin keyboard users
        // can match to the key they see on their keyboards.
        // If nothing matches that, we'll then go on to
        // checking scancodes so non-latin keyboard users
        // can have working hotkeys (though possibly in
        // odd positions). We must do all SDLK tests before
        // any scancode tests to avoid issues in cases
        // where the SDLK for one shortcut collides with
        // the scancode of another.

        // zyr: said for every one of them, answered or not. A shortcut
        // that never arrives and one that arrives without being
        // recognised read exactly the same from the other end, and this
        // is the one line that tells them apart. The word touchpad is in
        // it so that the pad's own sift shows it: these are sent by
        // ZyrDesk for its gestures and by nothing else.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: touchpad: Ctrl+Alt+Maj held, name 0x%X place 0x%X",
                    (unsigned int) event->keysym.sym,
                    (unsigned int) event->keysym.scancode);

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.sym == m_SpecialKeyCombos[i].keyCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }

        for (int i = 0; i < KeyComboMax; i++) {
            if (m_SpecialKeyCombos[i].enabled && event->keysym.scancode == m_SpecialKeyCombos[i].scanCode) {
                performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                return;
            }
        }

        // zyr: and said when none of ours answers, rather than left to be
        // guessed from the absence of the line above.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: touchpad: none of ours answers to it, it goes to the far computer");
    }

    // Set modifier flags
    modifiers = 0;
    if (event->keysym.mod & KMOD_CTRL) {
        modifiers |= MODIFIER_CTRL;
    }
    if (event->keysym.mod & KMOD_ALT) {
        modifiers |= MODIFIER_ALT;
    }
    if (event->keysym.mod & KMOD_SHIFT) {
        modifiers |= MODIFIER_SHIFT;
    }
    if (event->keysym.mod & KMOD_GUI) {
        if (isSystemKeyCaptureActive()) {
            modifiers |= MODIFIER_META;
        }
    }

    // Set keycode. We explicitly use scancode here because GFE will try to correct
    // for AZERTY layouts on the host but it depends on receiving VK_ values matching
    // a QWERTY layout to work.
    if (event->keysym.scancode >= SDL_SCANCODE_1 && event->keysym.scancode <= SDL_SCANCODE_9) {
        // SDL defines SDL_SCANCODE_0 > SDL_SCANCODE_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_1) + VK_0 + 1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_A && event->keysym.scancode <= SDL_SCANCODE_Z) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_A) + VK_A;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F1 && event->keysym.scancode <= SDL_SCANCODE_F12) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F1) + VK_F1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F13 && event->keysym.scancode <= SDL_SCANCODE_F24) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F13) + VK_F13;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_KP_1 && event->keysym.scancode <= SDL_SCANCODE_KP_9) {
        // SDL defines SDL_SCANCODE_KP_0 > SDL_SCANCODE_KP_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_KP_1) + VK_NUMPAD0 + 1;
    }
    else {
        switch (event->keysym.scancode) {
            case SDL_SCANCODE_BACKSPACE:
                keyCode = 0x08;
                break;
            case SDL_SCANCODE_TAB:
                keyCode = 0x09;
                break;
            case SDL_SCANCODE_CLEAR:
                keyCode = 0x0C;
                break;
            case SDL_SCANCODE_KP_ENTER: // FIXME: Is this correct?
            case SDL_SCANCODE_RETURN:
                keyCode = 0x0D;
                break;
            case SDL_SCANCODE_PAUSE:
                keyCode = 0x13;
                break;
            case SDL_SCANCODE_CAPSLOCK:
                keyCode = 0x14;
                break;
            case SDL_SCANCODE_ESCAPE:
                keyCode = 0x1B;
                break;
            case SDL_SCANCODE_SPACE:
                keyCode = 0x20;
                break;
            case SDL_SCANCODE_PAGEUP:
                keyCode = 0x21;
                break;
            case SDL_SCANCODE_PAGEDOWN:
                keyCode = 0x22;
                break;
            case SDL_SCANCODE_END:
                keyCode = 0x23;
                break;
            case SDL_SCANCODE_HOME:
                keyCode = 0x24;
                break;
            case SDL_SCANCODE_LEFT:
                keyCode = 0x25;
                break;
            case SDL_SCANCODE_UP:
                keyCode = 0x26;
                break;
            case SDL_SCANCODE_RIGHT:
                keyCode = 0x27;
                break;
            case SDL_SCANCODE_DOWN:
                keyCode = 0x28;
                break;
            case SDL_SCANCODE_SELECT:
                keyCode = 0x29;
                break;
            case SDL_SCANCODE_EXECUTE:
                keyCode = 0x2B;
                break;
            case SDL_SCANCODE_PRINTSCREEN:
                keyCode = 0x2C;
                break;
            case SDL_SCANCODE_INSERT:
                keyCode = 0x2D;
                break;
            case SDL_SCANCODE_DELETE:
                keyCode = 0x2E;
                break;
            case SDL_SCANCODE_HELP:
                keyCode = 0x2F;
                break;
            case SDL_SCANCODE_KP_0:
                // See comment above about why we only handle SDL_SCANCODE_KP_0 here
                keyCode = VK_NUMPAD0;
                break;
            case SDL_SCANCODE_0:
                // See comment above about why we only handle SDL_SCANCODE_0 here
                keyCode = VK_0;
                break;
            case SDL_SCANCODE_KP_MULTIPLY:
                keyCode = 0x6A;
                break;
            case SDL_SCANCODE_KP_PLUS:
                keyCode = 0x6B;
                break;
            case SDL_SCANCODE_KP_COMMA:
                keyCode = 0x6C;
                break;
            case SDL_SCANCODE_KP_MINUS:
                keyCode = 0x6D;
                break;
            case SDL_SCANCODE_KP_PERIOD:
                keyCode = 0x6E;
                break;
            case SDL_SCANCODE_KP_DIVIDE:
                keyCode = 0x6F;
                break;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                keyCode = 0x90;
                break;
            case SDL_SCANCODE_SCROLLLOCK:
                keyCode = 0x91;
                break;
            case SDL_SCANCODE_LSHIFT:
                keyCode = 0xA0;
                break;
            case SDL_SCANCODE_RSHIFT:
                keyCode = 0xA1;
                break;
            case SDL_SCANCODE_LCTRL:
                keyCode = 0xA2;
                break;
            case SDL_SCANCODE_RCTRL:
                keyCode = 0xA3;
                break;
            case SDL_SCANCODE_LALT:
                keyCode = 0xA4;
                break;
            case SDL_SCANCODE_RALT:
                keyCode = 0xA5;
                break;
            case SDL_SCANCODE_LGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5B;
                break;
            case SDL_SCANCODE_RGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5C;
                break;
            case SDL_SCANCODE_APPLICATION:
                keyCode = 0x5D;
                break;
            case SDL_SCANCODE_AC_BACK:
                keyCode = 0xA6;
                break;
            case SDL_SCANCODE_AC_FORWARD:
                keyCode = 0xA7;
                break;
            case SDL_SCANCODE_AC_REFRESH:
                keyCode = 0xA8;
                break;
            case SDL_SCANCODE_AC_STOP:
                keyCode = 0xA9;
                break;
            case SDL_SCANCODE_AC_SEARCH:
                keyCode = 0xAA;
                break;
            case SDL_SCANCODE_AC_BOOKMARKS:
                keyCode = 0xAB;
                break;
            case SDL_SCANCODE_AC_HOME:
                keyCode = 0xAC;
                break;
            // zyr: the transport keys, which sit right after the browser
            // ones above and were simply missing. Every laptop carries
            // them on its function row, and a host that never feels them
            // is a host nothing can be paused on from here. The volume
            // and mute keys beside them are deliberately left out: those
            // are about the room the person is sitting in, not the one
            // they are watching.
            case SDL_SCANCODE_AUDIONEXT:
                keyCode = 0xB0;
                break;
            case SDL_SCANCODE_AUDIOPREV:
                keyCode = 0xB1;
                break;
            case SDL_SCANCODE_AUDIOSTOP:
                keyCode = 0xB2;
                break;
            case SDL_SCANCODE_AUDIOPLAY:
                keyCode = 0xB3;
                break;
            case SDL_SCANCODE_SEMICOLON:
                keyCode = 0xBA;
                break;
            case SDL_SCANCODE_EQUALS:
                keyCode = 0xBB;
                break;
            case SDL_SCANCODE_COMMA:
                keyCode = 0xBC;
                break;
            case SDL_SCANCODE_MINUS:
                keyCode = 0xBD;
                break;
            case SDL_SCANCODE_PERIOD:
                keyCode = 0xBE;
                break;
            case SDL_SCANCODE_SLASH:
                keyCode = 0xBF;
                break;
            case SDL_SCANCODE_GRAVE:
                keyCode = 0xC0;
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                keyCode = 0xDB;
                break;
            case SDL_SCANCODE_BACKSLASH:
                keyCode = 0xDC;
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                keyCode = 0xDD;
                break;
            case SDL_SCANCODE_APOSTROPHE:
                keyCode = 0xDE;
                break;
            case SDL_SCANCODE_NONUSBACKSLASH:
                keyCode = 0xE2;
                break;
            default:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Unhandled button event: %d",
                             event->keysym.scancode);
                return;
        }
    }

    // Track the key state so we always know which keys are down
    if (event->state == SDL_PRESSED) {
        m_KeysDown.insert(keyCode);
    }
    else {
        m_KeysDown.remove(keyCode);
    }

    LiSendKeyboardEvent(0x8000 | keyCode,
                        event->state == SDL_PRESSED ?
                            KEY_ACTION_DOWN : KEY_ACTION_UP,
                        modifiers);
}
