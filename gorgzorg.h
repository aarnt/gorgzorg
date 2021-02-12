/*
* This file is part of GorgZorg, a simple CLI network file transfer tool.
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
  QString m_targetAddress;
  QTimer *m_timer;
  int m_delay;  
  int m_port;
  bool m_tarContents;

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

  inline void setDelay(int delay) { m_delay = delay; }
  inline void setPort(int port) { m_port = port; }
  inline void setTarContents() { m_tarContents = true; }

signals:
  void endTransfer();
};

#endif // GORGZORG_H
