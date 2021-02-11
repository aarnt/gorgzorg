#include <QCoreApplication>
#include <QDirIterator>
#include <QTextStream>

#include "gorgzorg.h"
#include "argumentlist.h"

int main(int argc, char *argv[])
{
  QCoreApplication a(argc, argv);
  ArgumentList *argList = new ArgumentList(argc, argv);  
  GorgZorg gz;
  QString target;
  QString pathToGorg;
  QString aux;
  QTextStream qout(stdout);

  aux = argList->getSwitchArg("-p");
  if (!aux.isEmpty())
  {
    bool ok;
    int port = aux.toInt(&ok);

    if (port != 0)
      gz.setPort(port);
  }

  if (argList->getSwitch(QStringLiteral("-z")) || argList->count() == 1)
  {
    gz.startServer();
  }
  else
  {
    if (argList->getSwitch("-h"))
    {
      gz.showHelp();
    }

    aux = argList->getSwitchArg("-c");
    if (!aux.isEmpty())
    {
      target=aux;
      if (!target.startsWith("10.0") &&
          !target.startsWith("127.0.0") &&
          !target.startsWith("172.16") &&
          !target.startsWith("192.168"))
      {
        qout << "ERROR: GorgZorg can only be run in a local network!" << Qt::endl;
        exit(1);
      }
    }

    aux = argList->getSwitchArg("-d");
    if (!aux.isEmpty())
    {
      bool ok;
      int delay = aux.toInt(&ok);

      if (delay != 0)
        gz.setDelay(delay);
    }

    aux = argList->getSwitchArg("-g");
    if (!aux.isEmpty())
    {
      pathToGorg=aux;
      if (pathToGorg.startsWith("/"))
      {
        qout << "ERROR: GorgZorg only works with relative files or paths!" << Qt::endl;
        exit(1);
      }
    }

    if (!target.isEmpty() && !pathToGorg.isEmpty())
    {
      gz.connectAndSend(target, pathToGorg);
    }
  }

  return a.exec();
}
