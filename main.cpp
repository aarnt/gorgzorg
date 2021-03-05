/*
* This file is part of GorgZorg, a simple CLI multiplatform network file transfer tool.
* Copyright (C) 2021 Alexandre Albuquerque Arnt
*
* https://github.com/aarnt/gorgzorg
*/

#include <QCoreApplication>
#include <QFileInfo>

#include <iostream>
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

  if (argList->count() == 1)
  {
    gz.showHelp();
    exit(1);
  }

  if (argList->getSwitch("-h"))
  {
    gz.showHelp();
    exit(0);
  }
  else if (argList->getSwitch("--version"))
  {
    gz.showVersion();
    exit(0);
  }

  aux = argList->getSwitchArg(QLatin1String("-p"));
  if (!aux.isEmpty())
  {
    bool ok;
    int port = aux.toInt(&ok);

    if (port <= 0 || port > 65535)
    {
      std::cout << "ERROR: Valid port numbers are between 1 and 65535!" << std::endl;
      exit(1);
    }
    else
      gz.setPort(port);
  }

  if (argList->getSwitch("-y")) gz.setAlwaysAccept();

  if (argList->getSwitch("-q")) gz.setQuitServer();

  //Has the user set a directory to copy received files?
  if (argList->contains("-d"))
  {
    aux = argList->getSwitchArg(QLatin1String("-d"));
    QFileInfo d(aux);

    if (!d.isDir() || !d.exists())
    {
      std::cout << "ERROR: " << aux.toLatin1().data() << " is not a valid directory!" << std::endl;
      exit(1);
    }

    gz.setZorgPath(aux);
  }

  if (argList->getSwitch("-v")) gz.setVerbose();   

  if (argList->contains(QLatin1String("-z")))
  {
    aux = argList->getSwitchArg(QLatin1String("-z"));

    if (!aux.isEmpty())
    {
      if (!GorgZorg::isValidIP(aux))
      {
        std::cout << "ERROR: Your are trying to listen on an invalid IPv4 IP!" << std::endl;
        exit(1);
      }

      if (!GorgZorg::isLocalIP(aux))
      {
        std::cout << "ERROR: GorgZorg can only run on a local network!" << std::endl;
        exit(1);
      }
    }

    gz.startServer(aux);
  }
  else if (argList->contains(QLatin1String("-c")))
  {
    aux = argList->getSwitchArg("-c");
    if (!aux.isEmpty())
    {
      target=aux;

      if (!GorgZorg::isValidIP(aux))
      {
        std::cout << "ERROR: Your are trying to connect to an invalid IPv4 IP!" << std::endl;
        exit(1);
      }

      if (!GorgZorg::isLocalIP(target))
      {
        std::cout << "ERROR: GorgZorg can only run on a local network!" << std::endl;
        exit(1);
      }
    }
    else
    {
      std::cout << "ERROR: You should specify an IP to connect to!" << std::endl;
      exit(1);
    }

    //Checks if user wants path to be "tared"
    if (argList->getSwitch(QLatin1String("-tar")))
    {
      gz.setTarContents();
    }

    //Checks if user wants path to be "ziped"
    if (argList->getSwitch(QLatin1String("-zip")))
    {
      gz.setZipContents();
    }

    aux = argList->getSwitchArg(QLatin1String("-g"));
    if (!aux.isEmpty())
    {
      if (aux == "." || aux == ".." || aux == "./" || aux == "../")
      {
        std::cout << "ERROR: This path is not compatible!" << std::endl;
        exit(1);
      }

      pathToGorg=aux;
    }
    else
    {
      std::cout << "ERROR: You should specify a filename or path to gorg (send)!" << std::endl;
      exit(1);
    }

    if (!target.isEmpty() && !pathToGorg.isEmpty())
    {
      gz.connectAndSend(target, pathToGorg);
    }
  }
  else //If user did not set neither '-z' or '-c' params, let's print app help
  {
    gz.showHelp();
    exit(0);
  }

  return a.exec();
}
