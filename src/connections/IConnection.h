#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class IConnection : public QObject
{
    Q_OBJECT
public:
    explicit IConnection(QObject *parent = nullptr) : QObject(parent) {}
    ~IConnection() override = default;

    virtual QString displayName() const = 0;
    virtual bool isConnected() const = 0;

public slots:
    virtual void connectSession() = 0;
    virtual void disconnectSession() = 0;
    virtual void writeData(const QByteArray &data) = 0;
    virtual void setTerminalSize(int rows, int columns)
    {
        (void)rows;
        (void)columns;
    }

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &message);
};
