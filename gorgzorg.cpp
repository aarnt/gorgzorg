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

#include "gorgzorg.h"

#include <QDataStream>
#include <QTcpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QFile>
#include <QTextCodec>
#include <QTextStream>
#include <QProcess>
#include <QDirIterator>
#include <QEventLoop>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

void qSleep(int ms)
{
#ifdef Q_OS_WIN
    Sleep(uint(ms));
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
    nanosleep(&ts, NULL);
#endif
}

GorgZorg::GorgZorg()
{
  QTextCodec::setCodecForLocale(QTextCodec::codecForName ("GBK"));
  m_tcpClient = new QTcpSocket (this);
  m_sendTimes = 0;  
  m_targetAddress = "";
  m_delay = 100;
  m_port = 10000;
  m_timer = new QTimer(this); // This timer controls if there is someone listening on the other side
  m_timer->setSingleShot(true);
  m_tarContents = true; //false;
  m_verbose = false;

  QObject::connect(m_timer, &QTimer::timeout, this, &GorgZorg::onTimeout);
  QObject::connect(m_tcpClient, &QTcpSocket::connected, this, &GorgZorg::send); // When the connection is successful, start to transfer files
  QObject::connect(m_tcpClient, &QTcpSocket::bytesWritten, this, &GorgZorg::goOnSend);
}

bool GorgZorg::isValidIP(const QString &ip)
{
  bool res = false;

  if (ip == "0.0.0.0" || ip == "255.255.255.255")
    return res;

  QRegularExpression re("^(\\d+).(\\d+).(\\d+).(\\d+)$");
  QRegularExpressionMatch rem = re.match(ip);

  if (rem.hasMatch())
  {
    bool ok;
    int oc1 = rem.captured(1).toInt(&ok);
    int oc2 = rem.captured(2).toInt(&ok);
    int oc3 = rem.captured(3).toInt(&ok);
    int oc4 = rem.captured(4).toInt(&ok);

    if ((oc1 >= 0 && oc1 <= 255) && (oc2 >= 0 && oc2 <= 255) &&
      (oc3 >= 0 && oc3 <= 255) && (oc4 >= 0 && oc4 <= 255))
      res = true;
  }

  return res;
}

bool GorgZorg::isLocalIP(const QString &ip)
{
  if (ip.startsWith("10.0") || ip.startsWith("127.0.0") ||
      ip.startsWith("172.16") || ip.startsWith("192.168"))
    return true;
  else
    return false;
}

void GorgZorg::onTimeout()
{
  //If after 5 seconds, there is no byte written, let's abort gorging...
  if (m_totalSize == 0)
  {
    if (m_tarContents) //If there is an archived file that was not sent, let's remove it
    {
      if (QFile::exists(m_archiveFileName) && m_archiveFileName.endsWith(".tar"))
        QFile::remove(m_archiveFileName);
    }

    QTextStream qout(stdout);
    qout << QLatin1String("ERROR: It seems there is no one zorging on %1:%2").arg(m_targetAddress).arg(m_port) << Qt::endl;
    exit(1);
  }
}

void GorgZorg::sendFile(const QString &filePath)
{
  if (prepareToSendFile(filePath))
  {
    if (m_sendTimes == 0) // Only the first time it is sent, it happens when the connection generates the signal connect
    {
      m_tcpClient->connectToHost(QHostAddress(m_targetAddress), m_port);
      m_sendTimes = 1;
    }
    else
      send(); // When sending for the first time, connectToHost initiates the connect signal to call send, and you need to call send after the second time
  }

  QEventLoop eventLoop;
  QObject::connect(this, &GorgZorg::endTransfer, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();
  qSleep(m_delay);
}

/*
 * Threaded methods to connect and send data to client
 *
 * It can send just one file or entire paths (with subpaths)
 * When sending paths, it can archive them with tar before sending
 */
void GorgZorg::connectAndSend(const QString &targetAddress, const QString &pathToGorg)
{
  QTextStream qout(stdout);
  m_targetAddress = targetAddress;
  QFileInfo fi(pathToGorg);

  if (!fi.exists())
  {
    qout << QLatin1String("ERROR: %1 could not be found!").arg(pathToGorg) << Qt::endl;
    exit(1);
  }

  m_timer->start(3000);

  /*if (fi.isFile())
  {
    sendFile(pathToGorg);
  }*/
  //else
  {
    if (m_tarContents)
    {
      quint32 gen = QRandomGenerator::global()->generate();
      m_archiveFileName = QLatin1String("gorged_%1.tar").arg(QString::number(gen));

      QProcess tar;
      QStringList params;
      params << QLatin1String("-cf");
      params << m_archiveFileName;
      params << pathToGorg;
      tar.execute(QLatin1String("tar"), params);

      sendFile(m_archiveFileName);
    }
    else
    {
      //Loop thru the dirs/files on pathToGorg
      QDirIterator it(pathToGorg, QDir::AllEntries | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
      while (it.hasNext())
      {
        QString traverse = it.next();
        if (traverse.endsWith(QLatin1String(".")) || traverse.endsWith(QLatin1String(".."))) continue;

        if (it.fileInfo().isDir())
          traverse = ctn_DIR_ESCAPE + traverse;

        sendFile(traverse);
      }
    }
  }

  qout << Qt::endl;
  exit(0);
}

/*
 * Opens the given file. If it cannot open, returns false
 */
bool GorgZorg::prepareToSendFile(const QString &fName)
{
  m_fileName = fName;
  QString fileName = fName;
  m_loadSize = 0;
  m_byteToWrite = 0;
  m_totalSize = 0;
  m_outBlock.clear();
  m_sendingADir = false;
  QTextStream qout(stdout);

  if (fName.startsWith(ctn_DIR_ESCAPE))
  {
    m_sendingADir = true;
  }
  else
  {
    m_localFile = new QFile(fileName);
    if (!m_localFile->open(QFile::ReadOnly))
    {
      qout << QLatin1String("ERROR: %1 could not be opened").arg(fileName) << Qt::endl;
      return false;
    }
  }

  return true;
}

// Send file header information
void GorgZorg::send()
{
  m_loadSize = 4 * 1024; // The size of data sent each time

  if (m_sendingADir)
  {
    m_byteToWrite = 0;
    m_totalSize = 0;
  }
  else
  {
    m_byteToWrite = m_localFile->size(); //The size of the remaining data
    m_totalSize = m_localFile->size();
  }

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = m_fileName;
  QTextStream qout(stdout);

  if (m_sendingADir)
  {
    QString aux = QLatin1String("Gorging dir %1").arg(m_currentFileName);
    qout << Qt::endl << aux.remove(ctn_DIR_ESCAPE) << Qt::endl;
  }
  else
  {
    qout << Qt::endl << QLatin1String("Gorging %1").arg(m_currentFileName) << Qt::endl;
  }

  out << qint64 (0) << qint64 (0) << m_currentFileName;

  m_totalSize += m_outBlock.size (); // The total size is the file size plus the size of the file name and other information
  m_byteToWrite += m_outBlock.size ();

  out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
  out << m_totalSize << qint64(m_outBlock.size ());

  m_tcpClient->write(m_outBlock); // Send the read file to the socket

  //ui->sendProgressBar->setMaximum(totalSize);
  //ui->sendProgressBar->setValue(totalSize-byteToWrite);
}

void GorgZorg::goOnSend(qint64 numBytes) // Start sending file content
{
  m_byteToWrite -= numBytes; // Remaining data size

  if (m_sendingADir)
  {
    m_outBlock.resize(0);
    m_tcpClient->write(m_outBlock);
  }
  else
  {
    m_outBlock = m_localFile->read(qMin(m_byteToWrite, m_loadSize));
    m_tcpClient->write(m_outBlock);
  }

  //ui-> sendProgressBar->setMaximum(totalSize);
  //ui-> sendProgressBar->setValue(totalSize-byteToWrite);

  if (m_byteToWrite == 0) // Send completed
  {
    QTextStream qout(stdout);
    qout << QLatin1String("Gorging completed") << Qt::endl;

    //If we gorged a tared file, let's remove it!
    if (m_tarContents)
    {
      QProcess pwd;
      QStringList params;
      pwd.start(QLatin1String("pwd"), params);
      pwd.waitForFinished(-1);
      QString path = pwd.readAllStandardOutput();
      path.remove(QLatin1Char('\n'));
      path += QDir::separator() + m_currentFileName;

      if (path.endsWith(".tar")) QFile::remove(path);
    }

    if (!m_sendingADir)
    {
      m_localFile->close();
    }

    emit endTransfer();
  }
}

/*
 *  SERVER SIDE PART *****************************************************************
 */

void GorgZorg::acceptConnection()
{
  QTextStream qout(stdout);
  qout << Qt::endl << QLatin1String("Connected, preparing to zorg files!");

  m_receivedSocket = m_server->nextPendingConnection();
  QObject::connect(m_receivedSocket, &QTcpSocket::readyRead, this, &GorgZorg::readClient);
}

void GorgZorg::readClient()
{
  QTextStream qout(stdout);

  if (m_byteReceived == 0) // just started to receive data, this data is file information
  {
    //ui->receivedProgressBar->setValue(0);
    QDataStream in(m_receivedSocket);
    in >> m_totalSize >> m_byteReceived >> m_fileName;

    int cutName=m_fileName.size()-m_fileName.lastIndexOf('/')-1;
    m_currentFileName = m_fileName.right(cutName);
    m_currentPath = m_fileName.left(m_fileName.size()-cutName);
    m_receivingADir = false;

    //ctn_DIR_ESCAPEdirectory/subdirectory
    if (m_currentPath.startsWith(ctn_DIR_ESCAPE))
    {
      m_receivingADir = true;
      m_currentPath.remove(ctn_DIR_ESCAPE);
    }

    qout << Qt::endl << QLatin1String("Zorging %1").arg(m_currentFileName) << Qt::endl;

    if (!m_currentPath.isEmpty()) //&& m_currentPath != "./")
    {
      m_currentPath.remove("../");
      m_currentPath.remove("./");
      QProcess p;
      QStringList params;
      params << QLatin1String("-p");
      params << m_currentPath;
      p.execute(QLatin1String("mkdir"), params);
    }

    if (m_receivingADir)
    {
      QProcess p;
      QStringList params;
      params << QLatin1String("-p");
      params << m_currentPath + QDir::separator() + m_currentFileName;
      p.execute(QLatin1String("mkdir"), params);

      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();
      if (m_verbose) qout << QLatin1String("Received %1 bytes of %2").arg(QString::number(m_byteReceived)).arg(QString::number(m_totalSize)) << Qt::endl;
    }
    else
    {
      if (m_currentPath.isEmpty())
        m_newFile = new QFile(m_currentFileName);
      else
        m_newFile = new QFile(m_currentPath + QDir::separator() + m_currentFileName);

      m_newFile->open(QFile :: WriteOnly);
      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();
    }
  }
  else // Officially read the file content
  {
    m_inBlock = m_receivedSocket->readAll();
    m_byteReceived += m_inBlock.size();
    if (m_verbose) qout << QLatin1String("Received again %1 bytes of %2").arg(QString::number(m_byteReceived)).arg(QString::number(m_totalSize)) << Qt::endl;

    if (!m_receivingADir)
    {
      m_newFile->write(m_inBlock);
      m_newFile->flush();
    }
  }

  //ui-> receivedProgressBar->setMaximum(totalSize);
  //ui-> receivedProgressBar->setValue(byteReceived);

  if (m_byteReceived == m_totalSize)
  {
    QTextStream qout(stdout);
    qout << QLatin1String("Zorging completed") << Qt::endl;
    m_inBlock.clear();

    if (!m_receivingADir)
    {
      m_newFile->flush();
      m_newFile->close();
    }

    m_byteReceived = 0;
    m_totalSize = 0;
  }
}

void GorgZorg::startServer(const QString &ipAddress)
{
  m_totalSize = 0;
  m_byteReceived = 0;
  m_server = new QTcpServer(this);
  m_server->setMaxPendingConnections(1);
  QString ip = ipAddress;
  QTextStream qout(stdout);

  if (ip.isEmpty())
  {
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    for (auto &address: QNetworkInterface::allAddresses())
    {
      if (address.protocol() == QAbstractSocket::IPv4Protocol && address != localhost)
      {
        if (!address.toString().startsWith("10.0") &&
          !address.toString().startsWith("127.0.0") &&
          !address.toString().startsWith("172.16") &&
          !address.toString().startsWith("192.168")) continue;

        ip = address.toString();
      }
    }
  }

  if (ip.isEmpty())
  {
    qout << QLatin1String("ERROR: No valid IP address could be found!") << Qt::endl;
    exit(1);
  }

  if (!m_server->listen(QHostAddress(ip), m_port))
  {
    //If we could not bind to this port...
    qout << QString("ERROR: %1 is unavailable or port %2 is already being used in this host!").arg(ip).arg(m_port) << Qt::endl;
    exit(1);
  }

  QObject::connect(m_server, &QTcpServer::newConnection, this, &GorgZorg::acceptConnection);

  qout << QLatin1String("Start zorging on %1:%2...").arg(ip).arg(m_port) << Qt::endl;
}

/*
 * Outputs help usage on terminal
 */
void GorgZorg::showHelp()
{
  QTextStream qout(stdout);

  qout << Qt::endl << QLatin1String("  GorgZorg, a simple CLI network file transfer tool") << Qt::endl;
  qout << Qt::endl << QLatin1String("    -h: Show this help") << Qt::endl;
  qout << QLatin1String("    -c <IP>: Set IP or name to connect to") << Qt::endl;
  qout << QLatin1String("    -d <ms>: Set delay to wait between file transfers (in ms, default is 100)") << Qt::endl;
  qout << QLatin1String("    -tar: Use tar to archive contents of relative path") << Qt::endl;
  qout << QLatin1String("    -g <relativepath>: Set a relative filename or relative path to gorg (send)") << Qt::endl;
  qout << QLatin1String("    -p <portnumber>: Set port to connect or listen to connections (default is 10000)") << Qt::endl;
  qout << QLatin1String("    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it") << Qt::endl;
  qout << Qt::endl << QLatin1String("  Version: ") << ctn_VERSION << Qt::endl << Qt::endl;
  qout << Qt::endl << QLatin1String("  Examples:") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send contents of Test directory to IP 192.168.1.1") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 192.168.1.1 -g Test") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send archived contents of Crucial directory to IP 172.16.20.21") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 172.16.20.21 -g Crucial -tar") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Start a GorgZorg server on address 192.168.10.16:20000") << Qt::endl;
  qout << QLatin1String("    gorgzorg -p 20000 -z 192.168.10.16") << Qt::endl << Qt::endl;
}
