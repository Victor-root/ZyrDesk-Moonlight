#include "zyrfollow.h"

#include <QFile>

namespace
{
QString s_Path;
QMap<QString, StreamingPreferences::VideoCodecConfig> s_Codecs;

// What was last read and understood, so a line that has not moved costs
// nothing beyond the reading.
QString s_LastLine;
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
