/*
* This file is part of GorgZorg, a simple multiplatform CLI network file transfer tool.
* It was strongly inspired by the following article:
* https://topic.alibabacloud.com/a/file-transfer-using-the-tcp-protocol-in-qt-can-be-looped-in-one-direction_8_8_10249539.html
* Copyright (C) 2021 Alexandre Albuquerque Arnt
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* Source code hosted on: https://github.com/aarnt/gorgzorg
*/

#include "gorgzorg.h"
#include <iostream>

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
#include <QTime>
#include <QRegularExpression>
#include <QElapsedTimer>

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

#ifndef Q_OS_WIN
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
#endif

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

  return _getch();
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
  m_block = ctn_BLOCK_SIZE;
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
  //What did we receive from the server?
  QString ret = m_tcpClient->readAll();
  //std::cout << "Received response: " << ret.toLatin1().data() << std::endl;

  if (ret == ctn_ZORGED_OK)
  {
    std::cout << "Zorged OK received" << std::endl;
    emit endTransfer();
  }
  else if (ret == ctn_ZORGED_OK_SEND)
  {
    std::cout << "Zorged OK SEND received" << std::endl;
    emit okSend();
  }
  else if (ret == ctn_ZORGED_OK_SEND_AND_ZORGED_OK) //The two previous msgs may arrive truncated!
  {
    emit okSend();
    emit endTransfer();
    std::cout << "Zorged OK SEND received" << std::endl;
    std::cout << "Zorged OK received" << std::endl;
  }
  else if (ret == ctn_ZORGED_CANCEL_SEND)
  {
    removeArchive();
    std::cout << "Zorged CANCEL received. Aborting send!" << std::endl;
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
 * Returns the SHELL environment variable, if not set defaults to sh.
 */
QString GorgZorg::getShell()
{
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString shell = env.value(QStringLiteral("SHELL"), QStringLiteral("/bin/sh"));
  return shell;
}

/*
 * Creates a ".tar" or ".tar.gz" archive to send based on "-tar"/"-zip" command line params
 */
QString GorgZorg::createArchive(const QString &pathToArchive)
{
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
    std::cout << std::endl << "Compressing " << pathToArchive.toLatin1().data();
  else
    std::cout << std::endl << "Archiving " << pathToArchive.toLatin1().data();

  QTime time = QTime::currentTime();
  QString random = QString::number(time.hour()) +
      QString::number(time.minute()) +
      QString::number(time.second()) +
      QString::number(time.msec());

  QString archiveFileName = QString("gorged_%1").arg(random);

  QProcess p;
  QStringList tarParams;

  if (m_zipContents)
    tarParams << QLatin1String("-czf");
  else
    tarParams << QLatin1String("-cf");

  if (asterisk)
  {
#ifndef Q_OS_WIN
    if (m_zipContents)
    {
      archiveFileName += QLatin1String(".tar.gz");
    }
    else
    {
      archiveFileName += QLatin1String(".tar");
    }

    QStringList findParams;
    QString findCommand = QLatin1String("find ") + realPath +
        QLatin1String(" -name ") + QLatin1String("\"") + filter + QLatin1String("\"") +
        QLatin1String(" -exec tar ") + tarParams.at(0) + QLatin1String(" ") + archiveFileName + QLatin1String(" {} +");

    findParams << QLatin1String("-c");
    findParams << findCommand;
    p.execute(getShell(), findParams);
    p.waitForFinished(-1);
    p.close();
#else
    realPath.remove(QChar('\''));
    filter.remove(QChar('\''));

    QString compressionLevel;
    if (m_zipContents)
    {
      compressionLevel = "-mx1";
    }
    else
    {
      compressionLevel = "-mx0";
    }

    archiveFileName += QLatin1String(".7z");

    //Get-ChildItem c:\temp\*.dll -Recurse | Compress-Archive -Update -CompressionLevel NoCompression -DestinationPath c:\temp\power.zip
    /*QString params = "powershell.exe -Command \"Get-ChildItem " + realPath + filter +
        " -Recurse | Compress-Archive -Update -CompressionLevel " + compressionLevel +
        " -DestinationPath .\\" + archiveFileName + "\"";*/

    //qout << Qt::endl << QLatin1String("powershell command: %1").arg(params) << Qt::endl;
    //qout << Qt::endl << QLatin1String("powershell output: %1").arg(p.readAllStandardOutput()) << Qt::endl;

    //First, let's find where 7zip is located
    QStringList wp;
    wp << "/R" << "\\Program Files" << "7z.exe";
    p.start("where", wp);
    p.waitForFinished(-1);

    QString pathTo7zip = p.readAllStandardOutput();
    pathTo7zip.remove("\r\n");
    //qout << Qt::endl << QLatin1String("where output: %1").arg(pathTo7zip);

    if (!pathTo7zip.contains("7z.exe"))
    {
      wp << "/R" << "\\Program Files (x86)" << "7z.exe";
      p.start("where", wp);
      p.waitForFinished(-1);
      pathTo7zip = p.readAllStandardOutput();
      pathTo7zip.remove("\r\n");
      //qout << Qt::endl << QLatin1String("where output: %1").arg(pathTo7zip);
    }

    if (pathTo7zip.contains("7z.exe"))
    {
      //"c:\Program Files\7-Zip\7z.exe" a c:\temp\test10.7zip c:\Qt\6.0.1\*.dll -r -mx1
      QString params = "\"" + pathTo7zip + "\" a " + archiveFileName + " " + realPath + filter + " -r " + compressionLevel;
      p.start(params);
      p.waitForFinished(-1);
      p.close();
    }

#endif
  }
  else
  {
    if (m_zipContents)
    {
      archiveFileName += QLatin1String(".tar.gz");
    }
    else
    {
      archiveFileName += QLatin1String(".tar");
    }

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
  if (!m_archiveFileName.isEmpty() && QFile::exists(m_archiveFileName) &&
      (m_archiveFileName.endsWith(".tar") || m_archiveFileName.endsWith(".tar.gz") ||
      m_archiveFileName.endsWith(".7z")))
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

  if (fName.startsWith(ctn_DIR_ESCAPE))
  {
    m_sendingADir = true;
  }
  else
  {
    m_localFile = new QFile(m_fileName);
    if (!m_localFile->open(QFile::ReadOnly))
    {
      std::cout << std::endl << "ERROR: " << m_fileName.toLatin1().data() << " could not be opened" << std::endl;
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

#ifdef Q_OS_WIN
    filter.remove(QChar('\''));
    realPath.remove(QChar('\''));
#endif

    //qout << Qt::endl << QLatin1String("Filter: %1").arg(filter) << Qt::endl;
    //qout << Qt::endl << QLatin1String("Real Path: %1").arg(realPath) << Qt::endl;

    if (realPath.isEmpty())
      realPath = getWorkingDirectory();
  }

  if (!asterisk && !fi.exists())
  {
    std::cout << std::endl << "ERROR: " << pathToGorg.toLatin1().data() << " could not be found!" << std::endl;
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

    std::cout << std::endl << "Time elapsed: " << strDuration.toLatin1().data() << "s" << std::endl;
    std::cout << "Bytes sent: " << strBytesSent.toLatin1().data() << " MB" << std::endl;
    std::cout << "Speed: " << strSpeed.toLatin1().data() << " MB/s" << std::endl;
  }

  removeArchive();
  std::cout << std::endl;
  exit(0);
}

/*
 * Sends END OF TRANSFER signal to the server (goodbye, cruel world!)
 */
void GorgZorg::sendEndOfTransfer()
{
  QObject::disconnect(m_tcpClient, &QTcpSocket::bytesWritten, this, &GorgZorg::goOnSend);

  //Tests if client is connected before going on
  if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
  {
    return;
  }

  m_loadSize = m_block * 1024; // The size of data sent each time
  m_byteToWrite = 0;
  m_totalSize = 0;
  m_totalSent += m_totalSize;

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = ctn_END_OF_TRANSFER;
  std::cout << std::endl << "Gorging goodbye..." << std::endl;

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
  if (prepareToSendFile(filePath))
  {
    m_tcpClient->connectToHost(QHostAddress(m_targetAddress), m_port);
    m_tcpClient->waitForConnected(-1);

    if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
    {
      std::cout << std::endl << "ERROR: It seems there is no one zorging on " <<
                   m_targetAddress.toLatin1().data() << ":" << QString::number(m_port).toLatin1().data() << std::endl;
      removeArchive();
      exit(1);
    }

    m_loadSize = m_block * 1024; // The size of data sent each time

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
      QString aux = QString("Gorging header of dir %1").arg(m_currentFileName);
      std::cout << std::endl << aux.remove(ctn_DIR_ESCAPE).toLatin1().data() << std::endl;
    }
    else
    {
      std::cout << std::endl << "Gorging header of " << m_currentFileName.toLatin1().data() << std::endl;
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
  m_localFile = new QFile(m_fileName);
  m_tcpClient->connectToHost(QHostAddress(m_targetAddress), m_port);
  m_tcpClient->waitForConnected(-1);

  if (m_tcpClient->state() == QAbstractSocket::UnconnectedState)
  {
    std::cout << std::endl << "ERROR: It seems there is no one zorging on " <<
                 m_targetAddress.toLatin1().data() << ":" << QString::number(m_port).toLatin1().data() << std::endl;
    removeArchive();
    exit(1);
  }

  m_loadSize = m_block * 1024; // The size of data sent each time
  m_byteToWrite = 0;
  m_totalSize = 0;

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  m_currentFileName = m_fileName;

  QString aux = QString("Gorging header of dir %1").arg(m_currentFileName);
  std::cout << std::endl << aux.remove(ctn_DIR_ESCAPE).toLatin1().data() << std::endl;

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
  m_loadSize = m_block * 1024; // The size of data sent each time

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
    QString aux = QString("Gorging dir %1").arg(m_currentFileName);
    std::cout << std::endl << aux.remove(ctn_DIR_ESCAPE).toLatin1().data() << std::endl;
  }
  else
  {
    std::cout << std::endl << "Gorging " << m_currentFileName.toLatin1().data() << std::endl;
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
  m_loadSize = m_block * 1024; // The size of data sent each time

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
    QString aux = QString("Gorging dir %1").arg(m_currentFileName);
    std::cout << std::endl << aux.remove(ctn_DIR_ESCAPE).toLatin1().data() << std::endl;
  }
  else
  {
    std::cout << std::endl << "Gorging " << m_currentFileName.toLatin1().data() << std::endl;
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
    //QTextStream qout(stdout);
    std::cout << "Gorging completed" << std::endl;

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
    std::cout << std::endl << "ERROR: No valid IP address could be found!" << std::endl;
    exit(1);
  }

  if (!m_server->listen(QHostAddress(ip), m_port))
  {
    //If we could not bind to this port...
    std::cout << "ERROR: " << ip.toLatin1().data() << " is unavailable or port " <<
                 QString::number(m_port).toLatin1().data() << " is already being used in this host!" << std::endl;
    exit(1);
  }

  //Let's change the received files directory if the user especified one...
  if (!m_zorgPath.isEmpty())
  {
    QDir::setCurrent(m_zorgPath);
  }

  QObject::connect(m_server, &QTcpServer::newConnection, this, &GorgZorg::acceptConnection);

  std::cout << "Start zorging on " << ip.toLatin1().data() << ":" << QString::number(m_port).toLatin1().data() << "..." << std::endl;
}

void GorgZorg::acceptConnection()
{
  std::cout << std::endl << "Connected, preparing to zorg files!" << std::endl;

  m_receivedSocket = m_server->nextPendingConnection();
  QObject::connect(m_receivedSocket, &QTcpSocket::readyRead, this, &GorgZorg::readClient);
}

/*
 * Whenever clients send bytes, readClient is called!
 */
void GorgZorg::readClient()
{
  if (m_byteReceived == 0) // just started to receive data, this data is file information
  {
    m_receivingADir = false;
    m_createMasterDir = false;

    //ui->receivedProgressBar->setValue(0);
    QDataStream in(m_receivedSocket);

    in >> m_totalSize >> m_byteReceived >> m_fileName >> m_singleTransfer;

    if (m_fileName == ctn_END_OF_TRANSFER)
    {
      m_masterDir.clear();
      m_byteReceived = 0;
      m_totalSize = 0;

      //Client is saying goodbye...
      std::cout << std::endl << "See you next time!" << std::endl << std::endl;

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

    //Check if we have to replace directory separators
    QChar here = QDir::separator();
    int i=m_fileName.indexOf(here);

    if (i == -1)
    {
      if (here == '/')
      {
        m_fileName.replace(QChar('\\'), QChar('/'));
      }
      else
      {
        m_fileName.replace(QChar('/'), QChar('\\'));
      }
    }

#ifdef Q_OS_WIN
    if (m_fileName.startsWith(QDir::separator()))
    {
      m_fileName.remove(0, 1);
    }
#endif

    //qout << Qt::endl << QLatin1String("Received: %1").arg(m_fileName) << Qt::endl;
    int cutName=m_fileName.size()-m_fileName.lastIndexOf(QDir::separator())-1;
    m_currentFileName = m_fileName.right(cutName);

    if (m_currentFileName == ".")
    {
      m_currentPath = m_fileName.remove(QString(QDir::separator())+QLatin1String("."));
      m_currentFileName = m_currentPath;
      m_createMasterDir = true;
    }
    else
    {
      m_currentPath = m_fileName.left(m_fileName.size()-cutName);
      //qout << QLatin1String("First Path: %1").arg(m_currentPath) << Qt::endl;
    }

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
          query = QString("\nDo you want to zorg dir %1 (y/N)? ").arg(m_currentFileName);
        else
          query = QString("\nDo you want to zorg %1 with %2 (y/N)? ").arg(m_currentFileName).arg(strTotalSize);

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
          std::cout << std::endl << "Sending CANCEL_SEND..." << std::endl;
          m_receivedSocket->write(ctn_ZORGED_CANCEL_SEND.toLatin1());
          m_receivedSocket->waitForBytesWritten(-1);
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

#ifdef Q_OS_WIN
      if (m_currentPath.startsWith(QDir::separator()))
      {
        m_currentPath.remove(0, 1);
      }
#endif
    }

    std::cout << std::endl << "Zorging " << m_currentFileName.toLatin1().data() << std::endl;

    if (m_createMasterDir)
    {

#ifndef Q_OS_WIN
      QProcess p;
      QStringList params;
      params << QLatin1String("-p");
      params << m_currentPath;
      p.execute(QLatin1String("mkdir"), params);
#else
      QDir daux;
      daux.mkpath(m_currentPath);
      //qout << QLatin1String("Master DIR: %1").arg(m_currentPath) << Qt::endl;
      m_masterDir = m_currentPath;
#endif

      m_byteReceived = 0;
      m_totalSize = 0;

      //Send an OK to the other side
      std::cout << "Zorging of master directory completed" << std::endl;
      m_receivedSocket->write(ctn_ZORGED_OK.toLatin1());
      m_receivedSocket->waitForBytesWritten(-1);

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
      bool hasDrive=m_currentPath.contains(QLatin1Char(':'));

      if (hasDrive)
      {
        int s = m_currentPath.indexOf(QDir::separator());
        if (s > -1)
        {
          m_winDrive = m_currentPath.left(s+1);
          m_currentPath.remove(m_winDrive);
        }
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
        QDir daux;
        if (!m_masterDir.isEmpty())
        {
          if (!QString(m_winDrive+m_currentPath).startsWith(m_masterDir))
            m_currentPath = m_masterDir + m_currentPath;
        }
        daux.mkpath(m_currentPath);
      }
#endif
    }

    if (m_receivingADir)
    {

#ifndef Q_OS_WIN
      QProcess p;
      QStringList params;
      params << QLatin1String("-p");
      params << m_currentPath + QDir::separator() + m_currentFileName;
      p.execute(QLatin1String("mkdir"), params);
#else
      QDir daux;
      if (!m_masterDir.isEmpty())
      {
        if (!QString(m_winDrive+m_currentPath).startsWith(m_masterDir))
          m_currentPath = m_masterDir + m_currentPath;
      }

      //qout << QLatin1String("Creating DIR: %1").arg(m_currentPath) << Qt::endl;
      daux.mkpath(m_currentPath + QDir::separator() + m_currentFileName);
#endif

      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();
    }
    else
    {
      if (m_currentPath.isEmpty())
      {
#ifndef Q_OS_WIN
        m_newFile = new QFile(m_currentFileName);
#else
        if (!m_masterDir.isEmpty())
        {
          if (!QString(m_winDrive+m_currentFileName).startsWith(m_masterDir))
            m_currentFileName = m_masterDir + m_currentFileName;
        }

        //qout << QLatin1String("Creating file: %1").arg(m_currentFileName) << Qt::endl;
        m_newFile = new QFile(m_currentFileName);
#endif
      }
      else
      {
#ifndef Q_OS_WIN
        m_newFile = new QFile(m_currentPath + QDir::separator() + m_currentFileName);
#else
        if (!m_masterDir.isEmpty())
        {
          if (!QString(m_winDrive+m_currentPath).startsWith(m_masterDir))
            m_currentPath = m_masterDir + m_currentPath;
        }

        //qout << QLatin1String("Creating file: %1").arg(m_currentPath + m_currentFileName) << Qt::endl;
        m_newFile = new QFile(m_currentPath + QDir::separator() + m_currentFileName);
#endif
      }

      //qout << Qt::endl << QLatin1String("Path: %1").arg(m_currentPath) << Qt::endl;
      //qout << QLatin1String("FileName: %1").arg(m_currentFileName) << Qt::endl;

      m_newFile->open(QFile::WriteOnly);
      m_inBlock = m_receivedSocket->readAll();
      m_byteReceived += m_inBlock.size();

      m_newFile->write(m_inBlock);
      m_newFile->flush();
    }

    if (m_verbose)
    {
      std::cout << "Received " << QString::number(m_byteReceived).toLatin1().data() << " bytes of " <<
                   QString::number(m_totalSize).toLatin1().data() << std::endl;
    }
  }
  else // Officially read the file content
  {
    m_inBlock = m_receivedSocket->readAll();
    m_byteReceived += m_inBlock.size();
    if (m_verbose)
    {
      std::cout << "Received again " << QString::number(m_byteReceived).toLatin1().data() << " bytes of " <<
                   QString::number(m_totalSize).toLatin1().data() << std::endl;
    }
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
    QString savedOn;
    if (m_zorgPath.isEmpty())
      savedOn = QDir::currentPath();
    else
      savedOn = m_zorgPath;

    std::cout << "Zorging completed" << std::endl;
    std::cout << "File saved on \"" << savedOn.toLatin1().data() << "\"" << std::endl;

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
    m_receivedSocket->waitForBytesWritten(-1);
  }
}

/*
 * Outputs help usage on terminal
 */
void GorgZorg::showHelp()
{
  std::cout << std::endl << "  GorgZorg, a simple multiplatform CLI network file transfer tool" << std::endl;
  std::cout << std::endl << "    -bs <number>: Set the block size value (in kilobytes) when sending data (default is 4)" << std::endl;
  std::cout << "    -c <IP>: Set GorgZorg server IP to connect to" << std::endl;
  std::cout << "    -d <path>: Set directory in which received files are saved" << std::endl;
  std::cout << "    -g <pathToGorg>: Set a filename or path to gorg (send)" << std::endl;
  std::cout << "    -h: Show this help" << std::endl;
  std::cout << "    -p <portnumber>: Set port to connect or listen to connections (default is 10000)" << std::endl;
  std::cout << "    -q: Quit zorging after transfer is complete" << std::endl;
  std::cout << "    -tar: Use tar to archive contents of path" << std::endl;
  std::cout << "    -v: Verbose mode. When gorging, show speed. When zorging, show bytes received" << std::endl;
  std::cout << "    --version: Show version information" << std::endl;
  std::cout << "    -y: When zorging, automatically accept any incoming file/path" << std::endl;
  std::cout << "    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it" << std::endl;
  std::cout << "    -zip: Use gzip to compress contents of path" << std::endl;

  std::cout << std::endl << "  Examples:" << std::endl;
  std::cout << std::endl << "    #Send file /home/user/Projects/gorgzorg/LICENSE to IP 10.0.1.60 on port 45400" << std::endl;
  std::cout << "    gorgzorg -c 10.0.1.60 -g /home/user/Projects/gorgzorg/LICENSE -p 45400" << std::endl;
  std::cout << std::endl << "    #Send contents of Test directory to IP 192.168.1.1 on (default) port 10000" << std::endl;
  std::cout << "    gorgzorg -c 192.168.1.1 -g Test" << std::endl;
  std::cout << std::endl << "    #Send archived contents of Crucial directory to IP 172.16.20.21" << std::endl;
  std::cout << "    gorgzorg -c 172.16.20.21 -g Crucial -tar" << std::endl;
  std::cout << std::endl << "    #Send contents of filter expression in a gziped tarball to IP 192.168.0.100 [1]" << std::endl;
  std::cout << "    gorgzorg -c 192.168.0.100 -g '/home/user/Documents/*.txt' -zip" << std::endl;
  std::cout << std::endl << "    #Start a GorgZorg server on address 192.168.10.16:20000 using directory" << std::endl;
  std::cout << "    #\"/home/user/gorgzorg_files\" to save received files" << std::endl;
  std::cout << "    gorgzorg -p 20000 -z 192.168.10.16 -d ~/gorgzorg_files" << std::endl;
  std::cout << std::endl << "    #Start a GorgZorg server on address 172.16.11.43 on (default) port 10000" << std::endl;
  std::cout << "    #Always accept transfers and quit just after receiving one" << std::endl;
  std::cout << "    gorgzorg -z 172.16.11.43 -y -q" << std::endl << std::endl;
  std::cout << std::endl;
  std::cout << "[1] On Windows systems, you'll need 7zip installed." << std::endl << std::endl;
}

/*
 * Outputs version information
 */
void GorgZorg::showVersion()
{
  std::cout << "GorgZorg version " << ctn_VERSION.toLatin1().data() << std::endl;
  std::cout << "  Licensed under the terms of GNU LGPL v2.1" << std::endl;
  std::cout << "  (c) Alexandre Arnt - https://tintaescura.com" << std::endl;
}
