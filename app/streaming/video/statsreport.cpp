#include "statsreport.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <SDL.h>

namespace
{
QString s_Path;

// How often the line is written at most.
//
// Five times a second, which is what the standing-still number is worth:
// a picture that has stopped has to be seen to have stopped while it is
// still surprising, and a reader told a second later has already been
// stared at. The rest of the line is a window a second wide and simply
// repeats itself in between, which costs the reader nothing: it is the
// same reading it already had.
const Uint32 AT_MOST_EVERY_MS = 200;

// The last reading, kept so a reminder can write it again with only the
// time since the last frame moved on.
//
// Two threads reach it, the one that decodes and the one the reminder
// runs on, so it is held under a lock. Nothing here is on the path of a
// frame: the lock is taken five times a second and once a second.
QMutex s_Held;
VIDEO_STATS s_Stats = {};
QString s_Codec;
int s_Width = 0;
int s_Height = 0;
// How many seconds that window really covered, worked out when it was
// handed in and never again. Worked out afresh at every write, it would
// grow with the clock while the frames counted over it did not, and the
// rate and the bitrate would fall through the floor between two windows
// with nothing having changed.
double s_Over = 0.0;
bool s_Read = false;
Uint32 s_Written = 0;

// When the last frame reached the decoder, as the machine has counted
// milliseconds since it started, and nought until one has.
//
// Set without a lock and read without one: it is written on the path of
// every frame, it is one number, and a reader that catches it an instant
// late is a reader whose answer is an instant old. Nought never means
// « just now »: it means no frame has arrived at all.
SDL_atomic_t s_LastFrame;

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

// How long the picture has been standing still, as the line says it.
QString sinceTheLastFrame()
{
    const int at = SDL_AtomicGet(&s_LastFrame);
    return at == 0 ? QString() : QString::number(SDL_GetTicks() - (Uint32)at);
}

void put();
}

void StatsReport::reportTo(const QString& path)
{
    s_Path = path;
}

bool StatsReport::wanted()
{
    return !s_Path.isEmpty();
}

void StatsReport::aFrameArrived()
{
    if (s_Path.isEmpty()) {
        return;
    }
    // Nought is the one value this may not leave behind, being the word
    // for « no frame has ever arrived »: a machine that has been up for
    // exactly that millisecond says one instead.
    const Uint32 now = SDL_GetTicks();
    SDL_AtomicSet(&s_LastFrame, (int)(now == 0 ? 1 : now));
}

void StatsReport::tick()
{
    if (s_Path.isEmpty()) {
        return;
    }
    QMutexLocker holding(&s_Held);
    if (!s_Read || !SDL_TICKS_PASSED(SDL_GetTicks(), s_Written + AT_MOST_EVERY_MS)) {
        return;
    }
    put();
}

void StatsReport::write(const VIDEO_STATS& stats,
                        const char* codec,
                        int width,
                        int height)
{
    if (s_Path.isEmpty()) {
        return;
    }
    QMutexLocker holding(&s_Held);
    s_Stats = stats;
    s_Codec = QString(codec);
    s_Width = width;
    s_Height = height;
    // The seconds the window really covers, which is about one and never
    // exactly one: the window is flipped on the first frame to arrive
    // after a second has passed, and how late that is depends on the
    // frame rate. Dividing by a flat second would read as a rate that
    // rises and falls with nothing changing.
    s_Over = stats.measurementStartTimestamp == 0
                 ? 0.0
                 : (double)(SDL_GetTicks() - stats.measurementStartTimestamp) / 1000.0;
    s_Read = true;
    put();
}

namespace
{
// The line itself, from whatever was last handed in. The lock is held by
// whoever calls this.
void put()
{
    const VIDEO_STATS& stats = s_Stats;
    const int width = s_Width;
    const int height = s_Height;
    const double over = s_Over;
    s_Written = SDL_GetTicks();

    QString line;
    line += QString("codec=%1 ").arg(s_Codec);
    line += QString("width=%1 height=%2 ").arg(width).arg(height);
    // The one number here that is not a window: how long the picture has
    // been standing still, right now. Everything beside it is an average
    // over the second that has just passed, which is what a person reads
    // and is a second too late to say that a session has stopped moving.
    line += QString("since_frame_ms=%1 ").arg(sinceTheLastFrame());
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
}
