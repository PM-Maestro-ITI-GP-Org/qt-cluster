#ifndef AIREADER_H
#define AIREADER_H

#include <QString>
#include <QThread>
#include <cstdint>

/* The three verdicts motor_ai_client publishes after each window it sends to
 * the AI server. Free text -- the strings come from the model, not from any
 * contract this project owns, so nothing here parses them beyond deciding
 * whether a verdict is a complaint (see VehicleBackend::aiAlert).            */
struct AiResults {
    QString anomaly;
    QString faultClass;
    QString predMaint;
    quint32 seq = 0;       /* producer_seq of the window this verdict is for */
    quint64 timestamp = 0; /* window start, microseconds                     */
};
Q_DECLARE_METATYPE(AiResults)

/* Second reader thread, alongside SpiReader. The two shared-memory regions are
 * written by different processes on the same guest and have no ordering
 * between them, so they are polled independently rather than joined: the AI
 * verdict for a window necessarily lands well after the rows it describes, and
 * pairing them would stall the gauges behind the model.
 *
 * Region is /motor_ai_result, published by motor_ai_client. Same seqlock
 * discipline as /motor_ctrl. */
class AiReader : public QThread {
    Q_OBJECT
public:
    explicit AiReader(QObject *parent = nullptr);
    void stop();

signals:
    void newResults(AiResults results);

protected:
    void run() override;

private:
    volatile bool m_running = true;
    void *m_shm = nullptr;
    size_t m_shm_size = 0;
};

#endif // AIREADER_H
