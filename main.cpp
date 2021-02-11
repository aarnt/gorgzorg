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

  if (argList->count() == 1)
  {
    gz.showHelp();
    exit(1);
  }

  aux = argList->getSwitchArg(QLatin1String("-p"));
  if (!aux.isEmpty())
  {
    bool ok;
    int port = aux.toInt(&ok);

    if (port <= 0 || port > 65535)
    {
      qout << QLatin1String("ERROR: Valid port numbers are between 1 and 65535!") << Qt::endl;
      exit(1);
    }
    else
      gz.setPort(port);
  }

  if (argList->contains(QLatin1String("-z")))
  {
    aux = argList->getSwitchArg(QLatin1String("-z"));

    if (!aux.isEmpty() &&
        !aux.startsWith(QLatin1String("10.0")) &&
        !aux.startsWith(QLatin1String("127.0.0")) &&
        !aux.startsWith(QLatin1String("172.16")) &&
        !aux.startsWith(QLatin1String("192.168")))
    {
      qout << QLatin1String("ERROR: GorgZorg can only be run in a local network!") << Qt::endl;
      exit(1);
    }

    gz.startServer(aux);
  }
  else
  {
    if (argList->getSwitch("-h"))
    {
      gz.showHelp();
      exit(0);
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
        qout << QLatin1String("ERROR: GorgZorg can only be run in a local network!") << Qt::endl;
        exit(1);
      }
    }
    else
    {
      qout << QLatin1String("ERROR: You should specify an IP to connect to!") << Qt::endl;
      exit(1);
    }

    aux = argList->getSwitchArg("-d");
    if (!aux.isEmpty())
    {
      bool ok;
      int delay = aux.toInt(&ok);

      if (delay != 0)
      {
        gz.setDelay(delay);
      }
      else
      {
        qout << QLatin1String("ERROR: You should specify a delay value!") << Qt::endl;
        exit(1);
      }
    }

    //Checks if the user wants path to be "tared"
    if (argList->getSwitch(QLatin1String("-tar")))
    {
      gz.setTarContents();
    }

    aux = argList->getSwitchArg(QLatin1String("-g"));
    if (!aux.isEmpty())
    {
      pathToGorg=aux;
      if (pathToGorg.startsWith(QLatin1String("/")))
      {
        qout << QLatin1String("ERROR: GorgZorg only works with relative files or relative paths!") << Qt::endl;
        exit(1);
      }
    }
    else
    {
      qout << QLatin1String("ERROR: You should specify a relative filename or relative path to gorg (send)!") << Qt::endl;
      exit(1);
    }

    if (!target.isEmpty() && !pathToGorg.isEmpty())
    {
      gz.connectAndSend(target, pathToGorg);
      qout << Qt::endl;
    }
  }

  return a.exec();
}
