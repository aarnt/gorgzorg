#ifndef GORGZORG_H
#define GORGZORG_H

#include <QObject>
#include <string>

class QTcpSocket;
class QTcpServer;
class QFile;

const QString ctn_VERSION="0.1";

class GorgZorg: public QObject
{
  Q_OBJECT
public:
  explicit GorgZorg();

private:
  QTcpSocket *m_tcpClient;
  QFile *m_localFile;
  QFile *m_newFile;
  QString m_fileName;
  QString m_currentPath;
  QString m_currentFileName;
  int m_delay;
  int m_port;

  QTcpServer *m_server;
  QTcpSocket *m_receivedSocket;
  QByteArray m_outBlock;
  QByteArray m_inBlock;
  qint64 m_loadSize;      //The size of each send data
  qint64 m_byteToWrite;   //The remaining data size
  qint64 m_byteReceived;  //The size that has been sent
  qint64 m_totalSize;     //Total file size
  int m_sendTimes;        //Used to mark whether to send for the first time, after the first connection signal is triggered, followed by manually calling

  bool prepareToSendFile(const QString &fName);

private slots:
  void acceptConnection();
  void readClient();

  void send();            //Transfer file header information
  void goOnSend(qint64);  //Transfer file contents

public:
  void connectAndSend(const QString &targetAddress, const QString &pathToGorg);
  void startServer();
  inline void setDelay(int delay) { m_delay = delay; }
  inline void setPort(int port) { m_port = port; }

  void showHelp();

signals:
  void endTransfer();

};

#endif // GORGZORG_H
