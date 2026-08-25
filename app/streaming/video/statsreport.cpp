#include "statsreport.h"

#include <QSaveFile>
#include <SDL.h>

namespace
{
QString s_Path;

// What a number means when there is nothing to divide by.
//
// A window with no decoded frame in it is not a decoding time of nought,
// it is no reading at all, and a bar that draws nought is telling the
// person something untrue. Said as the empty word, which a reader knows
// how to leave blank.
QString each(double total, unsigned int over)
{
    return over == 0 ? QString() : QString::number(total / over, 'f', 2);
}
}

void StatsReport::reportTo(const QString& path)
{
    s_Path = path;
}

bool StatsReport::wanted()
{
    return !s_Path.isEmpty();
}

void StatsReport::write(const VIDEO_STATS& stats,
                        const char* codec,
                        int width,
                        int height)
{
    if (s_Path.isEmpty()) {
        return;
    }

    // The seconds the window really covers, which is about one and never
    // exactly one: the window is flipped on the first frame to arrive
    // after a second has passed, and how late that is depends on the
    // frame rate. Dividing by a flat second would read as a rate that
    // rises and falls with nothing changing.
    const double over =
        stats.measurementStartTimestamp == 0
            ? 0.0
            : (double)(SDL_GetTicks() - stats.measurementStartTimestamp) / 1000.0;

    QString line;
    line += QString("codec=%1 ").arg(codec);
    line += QString("width=%1 height=%2 ").arg(width).arg(height);
    // Worked out here from the frames and the seconds they came over,
    // rather than read out of the window handed in: the rates a window
    // carries are only ever filled by the routine that merges two of
    // them into a third, and the one that has just closed holds nought
    // in them.
    line += QString("fps=%1 ")
                .arg(over <= 0 ? QString()
                               : QString::number(stats.receivedFrames / over, 'f', 1));
    // The four the person watches. Decoding and rendering are this
    // computer's, the host's own time is the far one's, and the round trip
    // is what lies between them.
    line += QString("decode_ms=%1 ").arg(each(stats.totalDecodeTime, stats.decodedFrames));
    line += QString("render_ms=%1 ").arg(each(stats.totalRenderTime, stats.renderedFrames));
    line += QString("host_ms=%1 ")
                .arg(each((double)stats.totalHostProcessingLatency / 10,
                          stats.framesWithHostProcessingLatency));
    // The round trip is asked of the connection here rather than read out
    // of the window handed in, and that is not a preference. It is a live
    // reading and not something a window accumulates: nothing ever writes
    // it into one, and the only place it is ever set is the routine that
    // merges two windows into a third. The overlay reads it because it
    // draws such a merge; a window that has just closed carries a nought
    // there whatever the link is really doing, and a nought reads as no
    // measurement at all.
    uint32_t rtt = 0;
    uint32_t rttVariance = 0;
    if (!LiGetEstimatedRttInfo(&rtt, &rttVariance)) {
        rtt = 0;
    }
    line += QString("network_ms=%1 ").arg(rtt == 0 ? QString() : QString::number(rtt));
    line += QString("network_variance_ms=%1 ")
                .arg(rtt == 0 ? QString() : QString::number(rttVariance));
    // What actually came down the wire, rather than what was asked for:
    // the two part company on a link that cannot carry the ask, and it is
    // the first that says why a picture looks the way it does.
    line += QString("bitrate_mbps=%1 ")
                .arg(over <= 0 ? QString()
                               : QString::number((double)stats.totalBytes * 8 / over / 1000000, 'f', 2));
    line += QString("dropped_network_pct=%1 ")
                .arg(stats.totalFrames == 0
                         ? QString()
                         : QString::number((double)stats.networkDroppedFrames / stats.totalFrames * 100,
                                           'f', 2));
    line += QString("dropped_jitter_pct=%1")
                .arg(stats.decodedFrames == 0
                         ? QString()
                         : QString::number((double)stats.pacerDroppedFrames / stats.decodedFrames * 100,
                                           'f', 2));

    // Replaced whole. A reader that opens this file between two writes
    // gets the reading before or the reading after, never half of each.
    QSaveFile file(s_Path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    file.write(line.toUtf8());
    file.write("\n");
    file.commit();
}
