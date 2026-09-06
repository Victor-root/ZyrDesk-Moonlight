#pragma once

// zyr: the movement a game is played with, read from the system itself.
//
// A game is played with movement and not with a place, so this engine
// asks its toolkit for relative mouse mode, and the toolkit asks the
// system for the mouse's own movement on the raw channel Windows keeps
// for it. That channel is delivered to one window, and the toolkit
// throws away every packet that reaches a window it believes has not the
// keyboard.
//
// What it believes is read from one thing: whether its window is the one
// the system calls the front. A window carried inside another program's
// is a child, and the system gives the front to the head of a family and
// never to a member of it, so that answer is false for the whole of a
// session and can never turn true. Every movement of a game was dropped
// inside the toolkit, before anything left this computer: the far
// pointer never moved, and having nothing to draw was mistaken for a
// pointer that was not being drawn.
//
// This is the same fault the system keys had and the same answer, one
// floor lower. The system tells this window itself, in a message that
// owes nothing to the front, and Windows has a flag for exactly this
// case: input asked for with a window named, delivered even while that
// window is not the one at the front. So the movement is read here and
// sent on, and only ever the movement the toolkit is about to drop: a
// window that really is at the front is left entirely to it, which is
// every use of this engine outside this product.
//
// The movement is the device's own and owes nothing to where the pointer
// stands, which is what a game wants and is also a way of taking a hand
// that was never offered. The window carrying this one draws its own
// buttons over the picture and does not always cover the screen: a hand
// on one of those, or on this computer's own task bar, has left the far
// computer, and sending its movement onward would drive two pointers
// with one hand. So the movement is read only while the window under the
// pointer is this one.
//
// Only the movement, and deliberately not the cage that goes with it.
// Relative mouse mode also shuts the pointer on a point so that it
// cannot leave the picture or carry a click elsewhere, and the toolkit
// gates that on the very same answer, so under this product it never
// held anything either. It cannot be repaired here: the system lets one
// program shut the pointer in and it is the one at the front, which this
// one is not. It belongs to whoever carries this window, who is at the
// front and knows where the picture stands.
class ZyrPointer
{
public:
    // The window this reads at. Taken and let go with the session, and
    // read at the window itself rather than at the thread: a message is
    // what carries this and a message goes to a window.
    static void watch(void* window);
    static void stopWatching();

    // Whether the movement of a game is what is wanted right now, which
    // is exactly when the toolkit's relative mouse mode is on. Thrown
    // after it and never before: asking the system for this input names
    // the window it goes to, and the toolkit asks for the same input
    // without naming one, so whichever asks last is the one that counts.
    static void setReading(bool reading);

    // A raw input packet this window was sent, offered before the
    // toolkit sees it.
    static void sawRawInput(void* packet);
};
