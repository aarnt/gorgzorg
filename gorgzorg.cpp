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

#ifndef Q_OS_WIN
  #include <sys/ioctl.h>
  #include <termios.h>
#else
  #include <conio.h>
#endif

#include <QDataStream>
#include <QTcpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDirIterator>
#include <QEventLoop>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <iostream>

/*
 * Sleeps given ms miliseconds
 */
/*void qSleep(int ms)
{
#ifdef Q_OS_WIN
    Sleep(uint(ms));
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
    nanosleep(&ts, NULL);
#endif
}*/

/*
 * Retrieves a char from stdin, with no need for an ENTER
 */
int readCharResponse()
{
  static bool initflag = false;
  static const int STDIN = 0;

  if (!initflag) {
    // Use termios to turn off line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
    initflag = true;
  }

  int nbbytes;
  ioctl(STDIN, FIONREAD, &nbbytes);  // 0 is STDIN
  return nbbytes;
}

/*
 * Asks user about strQuestion. The reply will be just 1 char size
 */
char question(const QString &strQuestion)
{
  QTextStream(stdout) << strQuestion;

#ifndef Q_OS_WIN
  while (!readCharResponse())
  {
    fflush(stdout);
  }

  return (getchar());
#else
  while (!_kbhit())
  {
    fflush(stdout);
  }

  return = _getch();
#endif
}

/*
 * GorgZorg class methods
 */

QString GorgZorg::getWorkingDirectory()
{
  QString res;
  QProcess pwd;
  QStringList params;

#ifndef Q_OS_WIN
  pwd.start(QLatin1String("pwd"), params);
#else
  pwd.start(QLatin1String("cd"), params);
#endif

  pwd.waitForFinished(-1);

  res = pwd.readAllStandardOutput();
  res.remove("\n");

  return res;
}

GorgZorg::GorgZorg()
{
  m_tcpClient = new QTcpSocket (this);
  m_sendTimes = 0;
  m_totalSent = 0;
  m_targetAddress = "";
  m_port = 10000;
  m_elapsedTime = new QElapsedTimer();
  m_alwaysAccept = false;
  m_askForAccept = true;
  m_singleTransfer = false;
  m_tarContents = false;
  m_zipContents = false;
  m_verbose = false;
  m_quitServer = false;

  QObject::connect(m_tcpClient, &QTcpSocket::readyRead, this, &GorgZorg::readResponse);
}

/*
 * Whenever a reply from the server comes
 */
void GorgZorg::readResponse()
{
  QTextStream qout(stdout);

  //What did we receive from the server?
  QString ret = m_tcpClient->readAll();

  if (ret == ctn_ZORGED_OK)
  {
    qout << QLatin1String("Zorged OK received") << Qt::endl;
    emit endTransfer();
  }
  else if (ret == ctn_ZORGED_OK_SEND)
  {
    qout << QLatin1String("Zorged OK SEND received") << Qt::endl;
    emit okSend();
  }
  else if (ret == ctn_ZORGED_CANCEL_SEND)
  {
    removeArchive();
    qout << QLatin1String("Zorged CANCEL received. Aborting send!") << Qt::endl;
    exit(0);
  }
}

/*
 * Returns true if IPv4 octects are well formed
 */
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

/*
 * Test if both client and server are running on a private IPv4 network
 */
bool GorgZorg::isLocalIP(const QString &ip)
{
  if (ip.startsWith("10.0") || ip.startsWith("127.0.0") ||
      ip.startsWith("172.16") || ip.startsWith("192.168"))
    return true;
  else
    return false;
}

/*
 * Creates a ".tar" or ".tar.gz" archive to send based on "-tar"/"-zip" command line params
 */
QString GorgZorg::createArchive(const QString &pathToArchive)
{
  QTextStream qout(stdout);
  bool asterisk = false;
  QString realPath;
  QString filter;

  if (pathToArchive.contains(QRegularExpression("\\*\\.*")))
  {
    asterisk = true;
    int cutName=pathToArchive.size()-pathToArchive.lastIndexOf(QDir::separator())-1;
    filter = pathToArchive.right(cutName);
    realPath = pathToArchive.left(pathToArchive.size()-cutName);
  }

  if (m_zipContents)
    qout << Qt::endl << QLatin1String("Compressing %1").arg(pathToArchive);
  else
    qout << Qt::endl << QLatin1String("Archiving %1").arg(pathToArchive);

  quint32 gen = QRandomGenerator::global()->generate();
  QString archiveFileName = QLatin1String("gorged_%1.tar").arg(QString::number(gen));

  if (m_zipContents)
  {
    archiveFileName += QLatin1String(".gz");
  }

  QProcess p;
  QStringList tarParams, findParams;

  if (m_zipContents)
    tarParams << QLatin1String("-czf");
  else
    tarParams << QLatin1String("-cf");

  if (asterisk)
  {
#ifndef Q_OS_WIN
    QString findCommand = QLatin1String("find ") + realPath +
        QLatin1String(" -name ") + QLatin1String("\"") + filter + QLatin1String("\"") +
        QLatin1String(" -exec tar ") + tarParams.at(0) + QLatin1String(" ") + archiveFileName + QLatin1String(" {} +");

    findParams << QLatin1String("-c");
    findParams << findCommand;
    p.execute(QLatin1String("/bin/sh"), findParams);
    p.waitForFinished(-1);
    p.close();
#else
    //QString findCommand = QLatin1String("find ") + realPath +
    params << QLatin1String("-name");
    params << filter;
    params << QLatin1String("-exec");
    params << QLatin1String("tar") + QLatin1String (tarParams.at(0) + QLatin1String(" ") + archiveFileName + QLatin1String(" {} +");
    p.start(QLatin1String("find"), params);
    p.waitForFinished(-1);
    p.close();
#endif
  }
  else
  {
    tarParams << archiveFileName;
    tarParams << pathToArchive;
    p.execute(QLatin1String("tar"), tarParams);
    p.waitForFinished(-1);
    p.close();
  }

  return archiveFileName;
}

/*
 * Removes any not sent archive
 */
void GorgZorg::removeArchive()
{
  if (QFile::exists(m_archiveFileName) && (m_archiveFileName.endsWith(".tar") || m_archiveFileName.endsWith(".tar.gz")))
  {
    QFile::remove(m_archiveFileName);
  }
}

/*
 * Opens the given file. If it cannot open, returns false
 */
bool GorgZorg::prepareToSendFile(const QString &fName)
{
  m_fileName = fName;
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
    m_localFile = new QFile(m_fileName);
    if (!m_localFile->open(QFile::ReadOnly))
    {
      qout << Qt::endl << QLatin1String("ERROR: %1 could not be opened").arg(m_fileName) << Qt::endl;
      return false;
    }
  }

  return true;
}

/*
 * Transfers a single file when traversing a directory passed by command line
 */
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
    {
      send(); // When sending for the first time, connectToHost initiates the connect signal to call send, and you need to call send after the second time
    }
  }
  else return;

  QEventLoop eventLoop;
  QObject::connect(this, &GorgZorg::endTransfer, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();
}

/*
 * Threaded methods to connect and send data to client
 *
 * It can send just one file or entire paths (with subpaths)
 * When sending paths, it can archive them with tar/gzip before sending
 *
 * connectAndSend is the main method called directly by the "-g" command line param
 */
void GorgZorg::connectAndSend(const QString &targetAddress, const QString &pathToGorg)
{
  QTextStream qout(stdout);
  m_targetAddress = targetAddress;
  QFileInfo fi(pathToGorg);
  bool asterisk = false;
  QString realPath;
  QString filter;

  if (pathToGorg.contains(QRegularExpression("\\*\\.*")))
  {
    asterisk = true;
    int cutName=pathToGorg.size()-pathToGorg.lastIndexOf(QDir::separator())-1;
    filter = pathToGorg.right(cutName);
    realPath = pathToGorg.left(pathToGorg.size()-cutName);

    if (realPath.isEmpty())
      realPath = getWorkingDirectory();
  }

  if (!asterisk && !fi.exists())
  {
    qout << Qt::endl << QLatin1String("ERROR: %1 could not be found!").arg(pathToGorg) << Qt::endl;
    exit(1);
  }

  if (!asterisk && fi.isFile())
  {
    if (m_tarContents || m_zipContents)
    {
      m_archiveFileName = createArchive(pathToGorg);            
      if (m_verbose) m_elapsedTime->start();

      sendFileHeader(m_archiveFileName);
    }
    else
    {
      if (m_verbose) m_elapsedTime->start();
      sendFileHeader(pathToGorg);
    }
  }
  else
  {
    if (m_tarContents || m_zipContents)
    {
      {
        m_archiveFileName = createArchive(pathToGorg);
        if (m_verbose) m_elapsedTime->start();
        sendFileHeader(m_archiveFileName);
      }
    }
    else
    {
      if (m_verbose) m_elapsedTime->start();

      if (asterisk)
        sendDirHeader(realPath);
      else
        sendDirHeader(pathToGorg);

      QDirIterator *it;

      //Loop thru the dirs/files on pathToGorg
      if (asterisk) //If user passed some name filter path (ex: *.mp3)
      {
        QStringList nameFilters;
        nameFilters << filter;
        it = new QDirIterator(realPath, nameFilters, QDir::AllEntries | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
      }
      else
        it = new QDirIterator(pathToGorg, QDir::AllEntries | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);

      while (it->hasNext())
      {
        QString traverse = it->next();
        if (traverse.endsWith(QLatin1String(".")) || traverse.endsWith(QLatin1String(".."))) continue;

        if (it->fileInfo().isDir())
          traverse = ctn_DIR_ESCAPE + traverse;

        sendFile(traverse);
      }
    }
  }

  sendEndOfTransfer();

  //Let's print some statistics if verbose is on
  if (m_verbose)
  {
    double duration = m_elapsedTime->elapsed() / 1000.0; //duration of send in seconds
    double bytesSent = (m_totalSent / 1024.0) / 1024.0; //sent bytes in MB
    double speed = bytesSent / duration;

    QString strDuration = QString::number(duration, 'f', 2);
    QString strBytesSent = QString::number(bytesSent, 'f', 2);
    QString strSpeed = QString::number(speed, 'f', 2);

    qout << Qt::endl << QLatin1String("Time elapsed: %1s").arg(strDuration) << Qt::endl;
    qout << QLatin1String("Bytes sent: %1 MB").arg(strBytesSent) << Qt::endl;
    qout << QLatin1String("Speed: %1 MB/s").arg(strSpeed) << Qt::endl;
  }

  removeArchive();
  qout << Qt::endl;
  exit(0);
}

/*
 * Sends END OF TRANSFER signal to the server (goodbye, cruel world!)
 */
void GorgZorg::sendEndOfTransfer()
{
  QTextStream qout(stdout);
  QObject::disconnect(m_tcpClient, &QTcpSocket::bytesWritten, this, &GorgZorg::goOnSend);

  //Tests if client is connected before going on
  if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
  {
    return;
  }

  m_loadSize = 4 * 1024; // The size of data sent each time
  m_byteToWrite = 0;
  m_totalSize = 0;
  m_totalSent += m_totalSize;

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = ctn_END_OF_TRANSFER;
  qout << Qt::endl << QLatin1String("Gorging goodbye...") << Qt::endl;

  out << qint64(0) << qint64(0) << m_currentFileName << true;

  m_totalSize += m_outBlock.size(); // The total size is the file size plus the size of the file name and other information
  m_byteToWrite += m_outBlock.size();
  m_totalSent += m_outBlock.size();

  out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
  out << m_totalSize << qint64(m_outBlock.size());

  m_tcpClient->write(m_outBlock); // Send the read file to the socket
  m_tcpClient->waitForBytesWritten(-1);
}

/*
 * Send file header information, so the server can opt to accept or deny transfer
 */
void GorgZorg::sendFileHeader(const QString &filePath)
{
  QTextStream qout(stdout);

  if (prepareToSendFile(filePath))
  {
    m_tcpClient->connectToHost(QHostAddress(m_targetAddress), m_port);
    m_tcpClient->waitForConnected(-1);

    if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
    {
      qout << Qt::endl << QLatin1String("ERROR: It seems there is no one zorging on %1:%2").arg(m_targetAddress).arg(m_port) << Qt::endl;
      removeArchive();
      exit(1);
    }

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
      m_totalSent += m_totalSize;
    }

    QDataStream out(&m_outBlock, QIODevice::WriteOnly);
    m_currentFileName = m_fileName;

    if (m_sendingADir)
    {
      QString aux = QLatin1String("Gorging header of dir %1").arg(m_currentFileName);
      qout << Qt::endl << aux.remove(ctn_DIR_ESCAPE) << Qt::endl;
    }
    else
    {
      qout << Qt::endl << QLatin1String("Gorging header of %1").arg(m_currentFileName) << Qt::endl;
    }

    out << qint64(0) << qint64(0) << m_currentFileName << false;

    m_totalSize += m_outBlock.size(); // The total size is the file size plus the size of the file name and other information
    m_byteToWrite += m_outBlock.size();
    m_totalSent += m_outBlock.size();

    out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
    out << m_totalSize << qint64(m_outBlock.size());

    m_tcpClient->write(m_outBlock); // Send the read file to the socket
    m_tcpClient->waitForBytesWritten(-1);

    QObject::connect(m_tcpClient, &QTcpSocket::bytesWritten, this, &GorgZorg::goOnSend);

    //Wait until server accepts the sending...
    QEventLoop eventLoop;
    QObject::connect(this, &GorgZorg::okSend, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    m_outBlock.clear();
    m_totalSent = 0;
    sendFileBody();

    QObject::disconnect(this, &GorgZorg::okSend, &eventLoop, &QEventLoop::quit);
    QObject::connect(this, &GorgZorg::endTransfer, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
  }
}

/*
 * Sends directory header information, so the server can opt to accept or deny transfer
 */
void GorgZorg::sendDirHeader(const QString &filePath)
{
  m_fileName = filePath;
  m_outBlock.clear();
  m_sendingADir = true;
  QTextStream qout(stdout);

  m_localFile = new QFile(m_fileName);
  m_tcpClient->connectToHost(QHostAddress(m_targetAddress), m_port);
  m_tcpClient->waitForConnected(-1);

  if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
  {
    qout << Qt::endl << QLatin1String("ERROR: It seems there is no one zorging on %1:%2").arg(m_targetAddress).arg(m_port) << Qt::endl;
    removeArchive();
    exit(1);
  }

  m_loadSize = 4 * 1024; // The size of data sent each time
  m_byteToWrite = 0;
  m_totalSize = 0;

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = m_fileName;

  QString aux = QLatin1String("Gorging header of dir %1").arg(m_currentFileName);
  qout << Qt::endl << aux.remove(ctn_DIR_ESCAPE) << Qt::endl;

  /* This is the beggining of a directory traverse send, so let's put 'false' in the last value (m_singleTransfer)
     of the header so GorgZorg can read it as "This is not a single transfer!" */
  out << qint64 (0) << qint64 (0) << m_currentFileName + QDir::separator() + QLatin1String(".") << false;

  m_totalSize += m_outBlock.size(); // The total size is the file size plus the size of the file name and other information
  m_byteToWrite += m_outBlock.size();
  m_totalSent += m_outBlock.size();

  out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
  out << m_totalSize << qint64(m_outBlock.size());

  m_tcpClient->write(m_outBlock); // Send the read file to the socket
  m_tcpClient->waitForBytesWritten(-1);

  QEventLoop eventLoop;
  QObject::connect(this, &GorgZorg::cancelSend, &eventLoop, &QEventLoop::quit);
  QObject::connect(this, &GorgZorg::okSend, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();

  m_outBlock.clear();
  m_totalSent = 0;
  m_sendTimes = 1;
  delete m_localFile;

  QObject::connect(this, &GorgZorg::endTransfer, &eventLoop, &QEventLoop::quit);
  eventLoop.exec();
  QObject::connect(m_tcpClient, &QTcpSocket::bytesWritten, this, &GorgZorg::goOnSend);
}

/*
 * Sends file contents (called by sendFileHeader). It starts the real file sending...
 */
void GorgZorg::sendFileBody()
{
  QTextStream qout(stdout);

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
    m_totalSent += m_totalSize;
  }

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = m_fileName;

  if (m_sendingADir)
  {
    QString aux = QLatin1String("Gorging dir %1").arg(m_currentFileName);
    qout << Qt::endl << aux.remove(ctn_DIR_ESCAPE) << Qt::endl;
  }
  else
  {
    qout << Qt::endl << QLatin1String("Gorging %1").arg(m_currentFileName) << Qt::endl;
  }

  m_byteToWrite += m_outBlock.size();
  m_totalSent += m_outBlock.size();
  m_outBlock = m_localFile->read(qMin(m_byteToWrite, m_loadSize));
  m_tcpClient->write(m_outBlock); // Send the read file to the socket
}

/*
 * Sends header information of the file that belongs to the path we are traversing
 */
void GorgZorg::send()
{
  QTextStream qout(stdout);

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
    m_totalSent += m_totalSize;
  }

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = m_fileName;

  if (m_sendingADir)
  {
    QString aux = QLatin1String("Gorging dir %1").arg(m_currentFileName);
    qout << Qt::endl << aux.remove(ctn_DIR_ESCAPE) << Qt::endl;
  }
  else
  {
    qout << Qt::endl << QLatin1String("Gorging %1").arg(m_currentFileName) << Qt::endl;
  }

  out << qint64(0) << qint64(0) << m_currentFileName << true;
  m_totalSize += m_outBlock.size(); // The total size is the file size plus the size of the file name and other information
  m_byteToWrite += m_outBlock.size();
  m_totalSent += m_outBlock.size();

  out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
  out << m_totalSize << qint64(m_outBlock.size());

  m_tcpClient->write(m_outBlock); // Send the read file to the socket
}

/*
 * This is the slot that is called multiple times by all sending methods until the file is completly sent to the server
 */
void GorgZorg::goOnSend(qint64 numBytes) // Start sending file content
{
  QTextStream qout(stdout);
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
      QString path = getWorkingDirectory();

      path.remove(QLatin1Char('\n'));
      path += QDir::separator() + m_currentFileName;

      if (path.endsWith(".tar")) QFile::remove(path);
    }

    if (!m_sendingADir)
    {
      m_localFile->close();
    }
  }
}

/*
 *  SERVER SIDE PART *****************************************************************
 */

/*
 * Starts listening for file transfers on port m_port of the given ipAddress
 */
void GorgZorg::startServer(const QString &ipAddress)
{
  m_totalSize = 0;
  m_byteReceived = 0;
  m_server = new QTcpServer(this);
  QString ip = ipAddress;
  QTextStream qout(stdout);

  if (ip.isEmpty())
  {
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    for (auto &address: QNetworkInterface::allAddresses())
    {
      if (address.protocol() == QAbstractSocket::IPv4Protocol && address != localhost)
      {
        if (!isLocalIP(address.toString())) continue;
        ip = address.toString();
      }
    }
  }

  if (ip.isEmpty())
  {
    qout << Qt::endl << QLatin1String("ERROR: No valid IP address could be found!") << Qt::endl;
    exit(1);
  }

  if (!m_server->listen(QHostAddress(ip), m_port))
  {
    //If we could not bind to this port...
    qout << QString("ERROR: %1 is unavailable or port %2 is already being used in this host!").arg(ip).arg(m_port) << Qt::endl;
    exit(1);
  }

  //Let's change the received files directory if the user especified one...
  if (!m_zorgPath.isEmpty())
  {
    QDir::setCurrent(m_zorgPath);
  }

  QObject::connect(m_server, &QTcpServer::newConnection, this, &GorgZorg::acceptConnection);

  qout << QLatin1String("Start zorging on %1:%2...").arg(ip).arg(m_port) << Qt::endl;
}

void GorgZorg::acceptConnection()
{
  QTextStream qout(stdout);
  qout << Qt::endl << QLatin1String("Connected, preparing to zorg files!") << Qt::endl;

  m_receivedSocket = m_server->nextPendingConnection();
  QObject::connect(m_receivedSocket, &QTcpSocket::readyRead, this, &GorgZorg::readClient);
}

/*
 * Whenever clients send bytes, readClient is called!
 */
void GorgZorg::readClient()
{
  QTextStream qout(stdout);

  if (m_byteReceived == 0) // just started to receive data, this data is file information
  {
    m_receivingADir = false;
    m_createMasterDir = false;

    //ui->receivedProgressBar->setValue(0);
    QDataStream in(m_receivedSocket);

    in >> m_totalSize >> m_byteReceived >> m_fileName >> m_singleTransfer;

    if (m_fileName == ctn_END_OF_TRANSFER)
    {
      m_byteReceived = 0;
      m_totalSize = 0;

      //Client is saying goodbye...
      qout << Qt::endl << QLatin1String("See you next time!") << Qt::endl << Qt::endl;

      if (m_quitServer)
        exit(0);
      else
        return;
    }

    double totalSize;
    QString strTotalSize;

    if (!m_alwaysAccept)
    {
      if (m_singleTransfer == false && m_askForAccept == false)
      {
        m_askForAccept = true;
      }
    }

    int cutName=m_fileName.size()-m_fileName.lastIndexOf(QDir::separator())-1;
    m_currentFileName = m_fileName.right(cutName);

    if (m_currentFileName == ".")
    {
      m_currentPath = m_fileName.remove(QString(QDir::separator())+QLatin1String("."));
      m_currentFileName = m_currentPath;
      m_createMasterDir = true;
    }
    else
      m_currentPath = m_fileName.left(m_fileName.size()-cutName);

    if (!m_createMasterDir)
    {
      if (m_totalSize >= 1073741824)
      {
        totalSize = (m_totalSize / 1024.0) / 1024.0;
        strTotalSize = QString::number(totalSize, 'f', 2) + " MB";
      }
      else
      {
        totalSize = m_totalSize / 1024.0;
        strTotalSize = QString::number(totalSize, 'f', 2) + " KB";
      }
    }

    QTextStream s(stdin);

    if (m_askForAccept && !m_alwaysAccept)
    {
      while(true)
      {
        QString query;

        if (m_createMasterDir)
          query = QLatin1String("\nDo you want to zorg dir %1 (y/N)? ").arg(m_currentFileName);
        else
          query = QLatin1String("\nDo you want to zorg %1 with %2 (y/N)? ").arg(m_currentFileName).arg(strTotalSize);

        char value = question(query);

        if (value == 'Y' || value == 'y')
        {
          m_askForAccept = true;
          m_receivedSocket->write(ctn_ZORGED_OK_SEND.toLatin1());
          m_receivedSocket->waitForBytesWritten(-1);
          break;
        }
        else if (value == 'N' || value == 'n' || value == '\n')
        {
          qout << Qt::endl << QLatin1String("Sending CANCEL_SEND...") << Qt::endl;
          m_receivedSocket->write(ctn_ZORGED_CANCEL_SEND.toLatin1());
          m_byteReceived = 0;
          m_totalSize = 0;

          return;
        }
      }
    }
    else if (!m_askForAccept || m_alwaysAccept)
    {
      m_askForAccept = true;
      m_receivedSocket->write(ctn_ZORGED_OK_SEND.toLatin1());
      m_receivedSocket->waitForBytesWritten(-1);
    }

    //ctn_DIR_ESCAPEdirectory/subdirectory
    if (m_currentPath.startsWith(ctn_DIR_ESCAPE))
    {
      m_receivingADir = true;
      m_currentPath.remove(ctn_DIR_ESCAPE);
    }

    qout << Qt::endl << QLatin1String("Zorging %1").arg(m_currentFileName) << Qt::endl;

    if (m_createMasterDir)
    {
      QProcess p;
      QStringList params;

#ifndef Q_OS_WIN
      params << QLatin1String("-p");
      params << m_currentPath;
      p.execute(QLatin1String("mkdir"), params);
#else
      params << m_currentPath;
      p.execute(QLatin1String("md"), params);
#endif

      m_byteReceived = 0;
      m_totalSize = 0;

      //Send an OK to the other side
      qout << QLatin1String("Zorging of master directory completed") << Qt::endl;
      m_receivedSocket->write(ctn_ZORGED_OK.toLatin1());

      if (m_singleTransfer == false && m_askForAccept == false)
        m_askForAccept = true;
      else
        m_askForAccept = false;

      return;
    }

    if (!m_currentPath.isEmpty())
    {
#ifndef Q_OS_WIN
      if (m_currentPath.startsWith(QDir::separator()))
        m_currentPath.remove(0,1);
#else
      QString drive;
      int s = m_currentPath.indexOf(QDir::separator());
      if (s > 0)
      {
        drive = m_currentPath.left(s+1);
        m_currentPath.remove(drive);
      }
#endif
      m_currentPath.remove(QLatin1String("..") + QString(QDir::separator()));
      m_currentPath.remove(QLatin1String(".") + QString(QDir::separator()));

#ifndef Q_OS_WIN
      if (!m_currentPath.isEmpty())
      {
        QProcess p;
        QStringList params;
        params << QLatin1String("-p");
        params << m_currentPath;
        p.execute(QLatin1String("mkdir"), params);
      }
#else
      if (!m_currentPath.isEmpty())
      {
        QProcess p;
        QStringList params;
        params << m_currentPath;
        p.execute(QLatin1String("md"), params);
      }
#endif
    }

    if (m_receivingADir)
    {
      QProcess p;
      QStringList params;

#ifndef Q_OS_WIN
      params << QLatin1String("-p");
      params << m_currentPath + QDir::separator() + m_currentFileName;
      p.execute(QLatin1String("mkdir"), params);
#else
      params << m_currentPath + QDir::separator() + m_currentFileName;
      p.execute(QLatin1String("md"), params);
#endif

      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();
    }
    else
    {
      if (m_currentPath.isEmpty())
        m_newFile = new QFile(m_currentFileName);
      else
        m_newFile = new QFile(m_currentPath + QDir::separator() + m_currentFileName);

      m_newFile->open(QFile::WriteOnly);
      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();

      m_newFile->write(m_inBlock);
      m_newFile->flush();
    }

    if (m_verbose) qout << QLatin1String("Received %1 bytes of %2").arg(QString::number(m_byteReceived)).arg(QString::number(m_totalSize)) << Qt::endl;
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
      m_newFile->close();
    }

    m_byteReceived = 0;
    m_totalSize = 0;

    if (!m_alwaysAccept)
    {
      if (m_singleTransfer == false && m_askForAccept == false)
        m_askForAccept = true;
      else
        m_askForAccept = false;
    }

    //Send an OK to the other side
    m_receivedSocket->write(ctn_ZORGED_OK.toLatin1());
  }
}

/*
 * Outputs help usage on terminal
 */
void GorgZorg::showHelp()
{
  QTextStream qout(stdout);
  qout << Qt::endl << QLatin1String("  GorgZorg, a simple CLI network file transfer tool") << Qt::endl;
  qout << Qt::endl << QLatin1String("    -c <IP>: Set GorgZorg server IP to connect to") << Qt::endl;
  qout << QLatin1String("    -d <path>: Set directory in which received files are saved") << Qt::endl;
  qout << QLatin1String("    -g <pathToGorg>: Set a filename or path to gorg (send)") << Qt::endl;
  qout << QLatin1String("    -h: Show this help") << Qt::endl;
  qout << QLatin1String("    -p <portnumber>: Set port to connect or listen to connections (default is 10000)") << Qt::endl;
  qout << QLatin1String("    -q: Quit zorging after transfer is complete") << Qt::endl;
  qout << QLatin1String("    -tar: Use tar to archive contents of path") << Qt::endl;
  qout << QLatin1String("    -v: Verbose mode. When gorging, show speed. When zorging, show bytes received") << Qt::endl;
  qout << QLatin1String("    -y: When zorging, automatically accept any incoming file/path") << Qt::endl;
  qout << QLatin1String("    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it") << Qt::endl;
  qout << QLatin1String("    -zip: Use gzip to compress contents of path") << Qt::endl;

  qout << Qt::endl << QLatin1String("  Version: ") << ctn_VERSION << Qt::endl << Qt::endl;

  qout << Qt::endl << QLatin1String("  Examples:") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send file /home/user/Projects/gorgzorg/LICENSE to IP 10.0.1.60 on port 45400") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 10.0.1.60 -g /home/user/Projects/gorgzorg/LICENSE -p 45400") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send contents of Test directory to IP 192.168.1.1 on (default) port 10000") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 192.168.1.1 -g Test") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send archived contents of Crucial directory to IP 172.16.20.21") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 172.16.20.21 -g Crucial -tar") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Send contents of filter expression in a gziped tarball to IP 192.168.0.100") << Qt::endl;
  qout << QLatin1String("    gorgzorg -c 192.168.0.100 -g '/home/user/Documents/*.txt' -zip") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Start a GorgZorg server on address 192.168.10.16:20000 using directory") << Qt::endl;
  qout << QLatin1String("    #\"/home/user/gorgzorg_files\" to save received files") << Qt::endl;
  qout << QLatin1String("    gorgzorg -p 20000 -z 192.168.10.16 -d ~/gorgzorg_files") << Qt::endl;
  qout << Qt::endl << QLatin1String("    #Start a GorgZorg server on address 172.16.11.43 on (default) port 10000") << Qt::endl;
  qout << QLatin1String("    #Always accept transfers and quit just after receiving one") << Qt::endl;
  qout << QLatin1String("    gorgzorg -z 172.16.11.43 -y -q") << Qt::endl << Qt::endl;
}
