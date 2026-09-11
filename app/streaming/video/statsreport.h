#pragma once

#include <QString>

#include "decoder.h"

// A machine readable line of what a session is costing, written for
// whatever started this engine.
//
// The numbers already exist: they drive the overlay this engine can draw
// over its own picture. What did not exist was a way for a program driving
// this one to read them, and drawing them over the picture is no use to a
// caller that means to show them in its own window, over its own frame, in
// its own words.
//
// A file rather than the output streams. Those already carry the log of a
// session, kept for reading afterwards when something has gone wrong; one
// line a second of numbers would drown it. The file holds the last reading
// and nothing else, is replaced whole rather than appended to, so a reader
// never catches half a line, and is only written when a path was asked for.
class StatsReport
{
public:
    // Where to put it, or nothing, which is the default and writes no
    // file at all.
    static void reportTo(const QString& path);
    static bool wanted();

    // One reading, replacing the one before it.
    static void write(const VIDEO_STATS& stats,
                      const char* codec,
                      int width,
                      int height);

    // A frame has reached the decoder.
    //
    // What this is for is the one number in the line that cannot be
    // averaged and cannot wait: how long the picture has been standing
    // still. Everything else here is a window a second wide, which is
    // the right shape for numbers somebody reads and the wrong one for
    // the moment a session stops moving.
    static void aFrameArrived();

    // Writes the last reading again, with that number brought up to
    // date, unless one has just been written.
    //
    // Called from a reminder and not from the frames, which is the whole
    // of why it exists: a picture that has frozen submits no frames at
    // all, so nothing on the decoding path is running to say so.
    static void tick();
};
