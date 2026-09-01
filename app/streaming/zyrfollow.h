#pragma once

#include "settings/streamingpreferences.h"

#include <QMap>
#include <QString>

#include <optional>

// zyr: the settings this engine follows while it streams.
//
// Everything a stream is made of reaches this engine on its command line
// and is read once: the size, the rate, the codec, the bitrate. Changing
// any of them meant a new engine, which is a window that goes and comes
// back, seconds of nothing, and a session the person has to be told
// about. The product that drives this engine wants what its reference
// does, which is a setting that acts the moment it is chosen.
//
// So this engine follows a file. Whoever started it names the file on the
// command line and replaces it whole whenever a setting changes; the
// streaming loop reads it a few times a second, and a line that differs
// from what the stream is makes the stream over in place: same window,
// same process, same session. A file rather than a pipe or a socket, for
// the reason the statistics are a file too: it can be read with the eyes,
// it survives whoever wrote it, and it is the shape everything else
// between the two programs already has.
//
// One line, `key=value` fields separated by spaces, the same shape as the
// line of statistics this engine writes: width, height, fps, bitrate and
// codec, the codec in the very words the command line takes. A file that
// is missing, empty or unreadable says nothing, and nothing is done.
class ZyrFollow
{
public:
    // What the file says a stream should be.
    struct Wanted {
        int width;
        int height;
        int fps;
        int bitrateKbps;
        StreamingPreferences::VideoCodecConfig codec;
    };

    // Which file to follow, and the words a codec is spelled in on the
    // command line, which are the words the file spells it in too. The
    // default is no file, and no following at all.
    static void follow(const QString& path,
                       const QMap<QString, StreamingPreferences::VideoCodecConfig>& codecs);
    static bool wanted();

    // What the file says now, when it says something it did not say at
    // the last reading. Nothing when it has not changed, is missing, or
    // says nothing this engine understands whole: a line caught half
    // written is read again at the next turn.
    static std::optional<Wanted> changed();

    // Reads one line the way the file is written. Apart from the file so
    // it can be tried on its own.
    static std::optional<Wanted> parse(const QString& line,
                                       const QMap<QString, StreamingPreferences::VideoCodecConfig>& codecs);
};
