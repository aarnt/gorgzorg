/*
* This file is part of GorgZorg, a simple CLI network file transfer tool.
* It was strongly inspired by the following article:
* https://topic.alibabacloud.com/a/file-transfer-using-the-tcp-protocol-in-qt-can-be-looped-in-one-direction_8_8_10249539.html
* Copyright (C) 2021 Alexandre Albuquerque Arnt
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/

#ifndef GORGZORG_H
#define GORGZORG_H

#include <QObject>

class QTcpSocket;
class QTcpServer;
class QFile;
class QTimer;
class QElapsedTimer;

const QString ctn_VERSION="0.1";
const QString ctn_DIR_ESCAPE="<^dir$>:";

class GorgZorg: public QObject
{
  Q_OBJECT
public:
  explicit GorgZorg();

private:
  QTcpSocket *m_tcpClient;
  QTcpServer *m_server;
  QTcpSocket *m_receivedSocket;
  QByteArray m_outBlock;
  QByteArray m_inBlock;
  QFile *m_localFile;
  QFile *m_newFile;
  QString m_fileName;
  QString m_currentPath;
  QString m_currentFileName;
  QString m_targetAddress;
  QString m_archiveFileName; //Contains the random generated name of the archived path to send
  QTimer *m_connectionTimer;
  QElapsedTimer *m_timer;    //Counts ms since starting sending files

  int m_delay;
  int m_port;
  bool m_tarContents;
  bool m_sendingADir;
  bool m_receivingADir;
  bool m_verbose;
  qint64 m_loadSize;      //The size of each send data
  qint64 m_byteToWrite;   //The remaining data size
  qint64 m_byteReceived;  //The size that has been sent
  qint64 m_totalSize;     //Total file size
  qint64 m_totalSent;     //Total bytes sent
  int m_sendTimes;        //Used to mark whether to send for the first time, after the first connection signal is triggered, followed by manually calling

  bool prepareToSendFile(const QString &fName);
  QString createArchive(const QString &pathToArchive);
  void sendFile(const QString &filePath);

private slots:
  void acceptConnection();
  void readClient();
  void send();            //Transfer file header information
  void goOnSend(qint64);  //Transfer file contents
  void onTimeout();

public:
  void connectAndSend(const QString &targetAddress, const QString &pathToGorg);
  void startServer(const QString &ipAddress = "");
  void showHelp();
  static bool isValidIP(const QString &ip);
  static bool isLocalIP(const QString &ip);

  inline void setDelay(int delay) { m_delay = delay; }
  inline void setPort(int port) { m_port = port; }
  inline void setTarContents() { m_tarContents = true; }
  inline void setVerbose() { m_verbose = true; }

signals:
  void endTransfer();
};

#endif // GORGZORG_H
