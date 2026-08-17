#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include "cluster.h"
#include "NetworkClock.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    VehicleBackend backend;

    /* Wall-clock time, disciplined over SNTP and rendered in the zone named by
     * CLUSTER_TZ (default Africa/Cairo). Exposed the same way as Vehicle so the
     * QML has one convention for backend objects rather than two. */
    NetworkClock clock;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Vehicle", &backend);
    engine.rootContext()->setContextProperty("Clock", &clock);

    /* Desktop demo: with no motor_controller running the SPI reader never
     * produces a frame, so every readout sits at zero and the cluster looks
     * dead. Set CLUSTER_DEMO=1 to have the QML sweep the values instead.
     * Unset on the target, so the rig is never fed synthetic data. */
    engine.rootContext()->setContextProperty(
        "demoMode", qEnvironmentVariableIsSet("CLUSTER_DEMO"));

    /* Tuning aid: CLUSTER_SPEED=120 pins the speed there and holds it, so the
     * glow can be lined up against a number without chasing the demo sweep.
     * Negative when unset or unparseable, which is how the QML detects it. */
    bool speedOk = false;
    const float fixedSpeed = qEnvironmentVariable("CLUSTER_SPEED").toFloat(&speedOk);
    engine.rootContext()->setContextProperty(
        "fixedSpeed", speedOk ? fixedSpeed : -1.0f);

    const QUrl url(QStringLiteral("qrc:/qt/qml/Cluster/Main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        return -1;

    /* CLUSTER_SHOT=/path/out.png grabs the window and exits.
     *
     * Not a debug leftover -- it is the only way to see what this actually
     * renders on a Wayland session, where both X11 root capture and the
     * compositor's own screenshot interface are blocked. Everything else has to
     * be judged from a redrawing of the geometry, which cannot catch an element
     * that fails to draw at all: the two band icons loaded fine, reported the
     * right size and position, and were invisible.
     *
     * CLUSTER_SHOT_MS delays the grab, for animations that need to settle. */
    if (qEnvironmentVariableIsSet("CLUSTER_SHOT")) {
        const QString path = qEnvironmentVariable("CLUSTER_SHOT");
        bool msOk = false;
        const int ms = qEnvironmentVariable("CLUSTER_SHOT_MS").toInt(&msOk);
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (win) {
            QTimer::singleShot(msOk ? ms : 1200, win, [win, path]() {
                const QImage shot = win->grabWindow();
                if (shot.isNull() || !shot.save(path))
                    qWarning("CLUSTER_SHOT: could not write %s", qPrintable(path));
                else
                    qInfo("CLUSTER_SHOT: wrote %s (%dx%d)",
                          qPrintable(path), shot.width(), shot.height());
                qApp->quit();
            });
        }
    }

    return app.exec();
}
