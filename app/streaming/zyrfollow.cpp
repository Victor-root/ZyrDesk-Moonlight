#include "zyrfollow.h"

#include <QFile>

#include <SDL.h>

namespace
{
QString s_Path;
QMap<QString, StreamingPreferences::VideoCodecConfig> s_Codecs;

// What was last read and understood, so a line that has not moved costs
// nothing beyond the reading.
QString s_LastLine;

// zyr: the second file, the one naming the shape of the pointer, and the
// last word read from it.
QString s_PointerPath;
QString s_LastShape;

// The shapes this engine has made so far. Made once each and kept: a
// hand crossing the edge of a window names two of them a second, and
// making a cursor for every naming would be a shape built and thrown
// away sixty times a minute.
QMap<QString, SDL_Cursor*> s_Shapes;

// The word this engine knows, and what the system it runs on calls it.
// Anything outside this list leaves the pointer alone: whoever writes
// the file may be of a later build than this engine, and a pointer that
// vanished over a word would be worse than one that did not change.
SDL_SystemCursor shapeNamed(const QString& word, bool* known)
{
    static const QMap<QString, SDL_SystemCursor> shapes = {
        {"arrow", SDL_SYSTEM_CURSOR_ARROW},
        {"text", SDL_SYSTEM_CURSOR_IBEAM},
        {"hand", SDL_SYSTEM_CURSOR_HAND},
        {"wait", SDL_SYSTEM_CURSOR_WAIT},
        {"waitarrow", SDL_SYSTEM_CURSOR_WAITARROW},
        {"cross", SDL_SYSTEM_CURSOR_CROSSHAIR},
        {"sizewe", SDL_SYSTEM_CURSOR_SIZEWE},
        {"sizens", SDL_SYSTEM_CURSOR_SIZENS},
        {"sizenwse", SDL_SYSTEM_CURSOR_SIZENWSE},
        {"sizenesw", SDL_SYSTEM_CURSOR_SIZENESW},
        {"sizeall", SDL_SYSTEM_CURSOR_SIZEALL},
        {"no", SDL_SYSTEM_CURSOR_NO},
    };
    const auto found = shapes.constFind(word);
    *known = found != shapes.constEnd();
    return *known ? found.value() : SDL_SYSTEM_CURSOR_ARROW;
}
}

void ZyrFollow::follow(const QString& path,
                       const QMap<QString, StreamingPreferences::VideoCodecConfig>& codecs)
{
    s_Path = path;
    s_Codecs = codecs;
    s_LastLine.clear();
}

bool ZyrFollow::wanted()
{
    return !s_Path.isEmpty();
}

std::optional<ZyrFollow::Wanted> ZyrFollow::parse(const QString& line,
                                                  const QMap<QString, StreamingPreferences::VideoCodecConfig>& codecs)
{
    Wanted wanted = {};
    bool width = false, height = false, fps = false, bitrate = false, codec = false;
    for (const QString& field : line.split(' ', Qt::SkipEmptyParts)) {
        const int at = field.indexOf('=');
        if (at <= 0) {
            continue;
        }
        const QString key = field.left(at);
        const QString value = field.mid(at + 1);
        bool ok = false;
        if (key == "width") {
            wanted.width = value.toInt(&ok);
            width = ok;
        }
        else if (key == "height") {
            wanted.height = value.toInt(&ok);
            height = ok;
        }
        else if (key == "fps") {
            wanted.fps = value.toInt(&ok);
            fps = ok;
        }
        else if (key == "bitrate") {
            wanted.bitrateKbps = value.toInt(&ok);
            bitrate = ok;
        }
        else if (key == "codec") {
            codec = codecs.contains(value);
            if (codec) {
                wanted.codec = codecs.value(value);
            }
        }
        // A key this engine does not know belongs to whoever wrote it: a
        // file with one more word still says what it says.
    }

    // All five or nothing: a stream is not made over on half a description.
    if (!(width && height && fps && bitrate && codec) ||
        wanted.width <= 0 || wanted.height <= 0 || wanted.fps <= 0 || wanted.bitrateKbps <= 0) {
        return std::nullopt;
    }
    return wanted;
}

std::optional<ZyrFollow::Wanted> ZyrFollow::changed()
{
    if (s_Path.isEmpty()) {
        return std::nullopt;
    }

    QFile file(s_Path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    const QString line = QString::fromUtf8(file.readAll()).trimmed();
    if (line.isEmpty() || line == s_LastLine) {
        return std::nullopt;
    }

    // Held only once it has been understood: a line that says nothing
    // whole is read again at the next turn, in case it was caught half
    // written.
    auto wanted = parse(line, s_Codecs);
    if (wanted) {
        s_LastLine = line;
    }
    return wanted;
}

void ZyrFollow::followThePointer(const QString& path)
{
    s_PointerPath = path;
    s_LastShape.clear();
}

bool ZyrFollow::pointerWanted()
{
    return !s_PointerPath.isEmpty();
}

void ZyrFollow::pointAsTheFileSays()
{
    if (s_PointerPath.isEmpty()) {
        return;
    }

    QFile file(s_PointerPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const QString word = QString::fromUtf8(file.readAll()).trimmed().toLower();
    if (word.isEmpty() || word == s_LastShape) {
        return;
    }

    bool known = false;
    const SDL_SystemCursor which = shapeNamed(word, &known);
    if (!known) {
        // Held all the same, so an unknown word is looked at once and not
        // at every reading for the rest of the session.
        s_LastShape = word;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "zyr: the pointer was asked to be '%s', which this engine does not know",
                    qPrintable(word));
        return;
    }

    SDL_Cursor* shape = s_Shapes.value(word, nullptr);
    if (shape == nullptr) {
        shape = SDL_CreateSystemCursor(which);
        if (shape == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "zyr: the pointer could not be made '%s': %s",
                        qPrintable(word), SDL_GetError());
            return;
        }
        s_Shapes.insert(word, shape);
    }

    // Set whether or not the pointer is showing right now. Whether it
    // shows is the mouse mode's business and this engine's own switch;
    // what it looks like when it does is this.
    SDL_SetCursor(shape);
    s_LastShape = word;
}

void ZyrFollow::letThePointerGo()
{
    // The arrow first, so that whatever is left standing after this
    // session is the shape every other window on this computer expects.
    SDL_SetCursor(SDL_GetDefaultCursor());
    for (SDL_Cursor* shape : s_Shapes) {
        SDL_FreeCursor(shape);
    }
    s_Shapes.clear();
    s_LastShape.clear();
}
