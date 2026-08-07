#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "cluster.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    VehicleBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Vehicle", &backend);

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

    return app.exec();
}
