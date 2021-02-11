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

  QObject::connect(m_tcpClient, SIGNAL(connected()), this, SLOT(send())); // When the connection is successful, start to transfer files
  QObject::connect(m_tcpClient, SIGNAL(bytesWritten(qint64)), this, SLOT(goOnSend(qint64)));
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
  QObject::connect(this, SIGNAL(endTransfer()), &eventLoop, SLOT(quit()));
  eventLoop.exec();
  qSleep(m_delay);
}

/*
 * Threaded methods to connect and send data to client
 */
void GorgZorg::connectAndSend(const QString &targetAddress, const QString &pathToGorg)
{
  m_targetAddress = targetAddress;
  QFileInfo fi(pathToGorg);

  if (fi.isFile())
  {
    sendFile(pathToGorg);
  }
  else
  {
    //Loop thru the files in the pathToGorg
    QDirIterator it(pathToGorg, QDir::AllEntries | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
      QString traverse = it.next();
      if (traverse == "." || traverse == ".." || it.fileInfo().isDir()) continue;

      sendFile(traverse);
    }
  }

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
  QTextStream qout(stdout);

  m_localFile = new QFile(fileName);
  if (!m_localFile->open(QFile :: ReadOnly))
  {
    qout << QString("ERROR: %1 could not be opened").arg(fName) << Qt::endl;
    return false;
  }

  return true;
}

// Send file header information
void GorgZorg::send()
{
  m_byteToWrite = m_localFile->size(); //The size of the remaining data
  m_totalSize = m_localFile->size();
  m_loadSize = 4 * 1024; // The size of data sent each time

  QDataStream out(&m_outBlock, QIODevice::WriteOnly);
  QString currentFileName = m_fileName;
  QTextStream qout(stdout);
  qout << Qt::endl << QString("Gorging %1").arg(currentFileName) << Qt::endl;

  out << qint64 (0) << qint64 (0) << currentFileName;

  m_totalSize += m_outBlock.size (); // The total size is the file size plus the size of the file name and other information
  m_byteToWrite += m_outBlock.size ();

  out.device()->seek(0); // Go back to the beginning of the byte stream to write a qint64 in front, which is the total size and file name and other information size
  out << m_totalSize << qint64(m_outBlock.size ());

  m_tcpClient->write(m_outBlock); // Send the read file to the socket

  /*ui->progressLabel->show ();
  ui->sendProgressBar->setMaximum(totalSize);
  ui->sendProgressBar->setValue(totalSize-byteToWrite);*/
}

void GorgZorg::goOnSend(qint64 numBytes) // Start sending file content
{
  m_byteToWrite-= numBytes; // Remaining data size
  m_outBlock = m_localFile->read(qMin(m_byteToWrite, m_loadSize));
  m_tcpClient->write(m_outBlock);
  //ui-> sendProgressBar-> setMaximum (totalSize);
  //ui-> sendProgressBar-> setValue (totalSize-byteToWrite);

  if (m_byteToWrite == 0) // Send completed
  {
    QTextStream qout(stdout);
    qout << QLatin1String("Gorging completed") << Qt::endl;
    emit endTransfer();
  }
}

/*
 *  SERVER SIDE PART *****************************************************************
 */

void GorgZorg::acceptConnection()
{
  QTextStream qout(stdout);
  qout << Qt::endl << "Connected, preparing to receive files!" << Qt::endl;

  m_receivedSocket = m_server->nextPendingConnection();
  QObject::connect(m_receivedSocket, SIGNAL(readyRead()), this, SLOT (readClient()));
}

void GorgZorg::readClient()
{
  QTextStream qout(stdout);

  if (m_byteReceived == 0) // just started to receive data, this data is file information
  {
    //ui-> receivedProgressBar-> setValue (0);
    QDataStream in(m_receivedSocket);
    in >> m_totalSize >> m_byteReceived >> m_fileName;

    int cutName=m_fileName.size()-m_fileName.lastIndexOf('/')-1;
    m_currentFileName = m_fileName.right(cutName);
    m_currentPath = m_fileName.left(m_fileName.size()-cutName);

    qout << Qt::endl << QString("Zorging %1").arg(m_currentFileName) << Qt::endl;
    //currentPath.remove(".");

    if (!m_currentPath.isEmpty() && m_currentPath != "./")
    {
      QProcess p;
      QStringList params;
      params << "-p";
      params << m_currentPath;
      p.execute("mkdir", params);
    }

    m_newFile = new QFile(m_currentFileName);
    m_newFile->open(QFile :: WriteOnly);
  }
  else // Officially read the file content
  {
    m_inBlock = m_receivedSocket->readAll();
    m_byteReceived += m_inBlock.size ();
    m_newFile->write(m_inBlock);
    m_newFile->flush();
  }

  //ui-> receivedProgressBar-> setMaximum (totalSize);
  //ui-> receivedProgressBar-> setValue (byteReceived);

  if (m_byteReceived == m_totalSize)
  {
    QTextStream qout(stdout);
    qout << QLatin1String("Zorging completed") << Qt::endl;
    m_inBlock.clear();

    if (!m_currentPath.isEmpty() && m_currentPath != "./")
    {
      QProcess p;
      QStringList params;
      params << m_currentFileName;
      m_currentPath.remove("./");
      params << m_currentPath;
      p.execute("mv", params);
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
    qout << "ERROR: No valid IP address could be found!" << Qt::endl;
    exit(1);
  }

  if (!m_server->listen(QHostAddress(ip), m_port))
  {
    //If we could not bind in this port...
    qout << QString("ERROR: Port %1 is already being used in this host!").arg(m_port) << Qt::endl;
    exit(1);
  }

  QObject::connect(m_server, SIGNAL(newConnection()), this, SLOT(acceptConnection()));

  qout << QString("Start zorging on %1:%2...").arg(ip).arg(m_port) << Qt::endl;
}

/*
 * Outputs help usage on terminal
 */
void GorgZorg::showHelp()
{
  QTextStream qout(stdout);

  qout << Qt::endl << "  GorgZorg, a simple network file transfer tool" << Qt::endl;
  qout << Qt::endl << "    -h: Show this help" << Qt::endl;
  qout << "    -c <IP>: Set IP or name to connect to" << Qt::endl;
  qout << "    -d <ms>: Set delay to wait between file transfers (in ms, default is 100)" << Qt::endl;
  qout << "    -g <relativepath>: Set a relative filename or relative path to gorg (send)" << Qt::endl;
  qout << "    -p <portnumber>: Set port to connect or listen to connections (default is 10000)" << Qt::endl;
  qout << "    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it" << Qt::endl;
  qout << Qt::endl << "  Version: " << ctn_VERSION << Qt::endl << Qt::endl;
  qout << Qt::endl << "  Examples:" << Qt::endl;
  qout << Qt::endl << "    #Send contents of Test directory to IP 192.168.0.1" << Qt::endl;
  qout << "    gorgzorg -c 192.168.0.1 -g Test" << Qt::endl;
  qout << Qt::endl << "    #Start listening on port 20000 with address 192.168.10.16" << Qt::endl;
  qout << "    gorgzorg -p 20000 -z 192.168.10.16" << Qt::endl << Qt::endl;
}
