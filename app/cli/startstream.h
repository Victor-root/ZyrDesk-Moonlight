#pragma once

#include <QObject>
#include <QVariant>

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;

namespace CliStartStream
{

class Event;
class LauncherPrivate;

class Launcher : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE_D(m_DPtr, Launcher)

public:
    explicit Launcher(QString computer, QString app,
                      StreamingPreferences* preferences,
                      QObject *parent = nullptr);
    ~Launcher();
    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE void quitRunningApp();
    Q_INVOKABLE bool isExecuted() const;

signals:
    void searchingComputer();
    void searchingApp();
    void sessionCreated(QString appName, Session *session);
    void failed(QString text);
    // zyr: told apart from the rest, because it is the one failure that
    // is not a failure of this computer's: the far one simply does not
    // know us. Whoever started us pairs the two and starts again, and
    // « unreachable », which is what every failure returned, sent them
    // looking for a network fault that was never there.
    void notPaired(QString computerName);
    void appQuitRequired(QString appName);

private slots:
    void onComputerFound(NvComputer *computer);
    void onComputerUpdated(NvComputer *computer);
    void onTimeout();
    void onQuitAppCompleted(QVariant error);

private:
    QScopedPointer<LauncherPrivate> m_DPtr;
};

}
