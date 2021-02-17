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

#include <QCoreApplication>
#include <QTextStream>
#include <QFileInfo>

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

  if (argList->getSwitch("-h"))
  {
    gz.showHelp();
    exit(0);
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

  if (argList->getSwitch("-y")) gz.setAlwaysAccept();

  if (argList->getSwitch("-q")) gz.setQuitServer();

  //Has the user set a directory to copy received files?
  if (argList->contains("-d"))
  {
    aux = argList->getSwitchArg(QLatin1String("-d"));
    QFileInfo d(aux);

    if (!d.isDir() || !d.exists())
    {
      qout << QLatin1String("ERROR: %1 is not a valid directory!").arg(aux) << Qt::endl;
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
        qout << QLatin1String("ERROR: Your are trying to listen on an invalid IPv4 IP!") << Qt::endl;
        exit(1);
      }

      if (!GorgZorg::isLocalIP(aux))
      {
        qout << QLatin1String("ERROR: GorgZorg can only run on a local network!") << Qt::endl;
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
        qout << QLatin1String("ERROR: Your are trying to connect to an invalid IPv4 IP!") << Qt::endl;
        exit(1);
      }

      if (!GorgZorg::isLocalIP(target))
      {
        qout << QLatin1String("ERROR: GorgZorg can only run on a local network!") << Qt::endl;
        exit(1);
      }
    }
    else
    {
      qout << QLatin1String("ERROR: You should specify an IP to connect to!") << Qt::endl;
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
        qout << QLatin1String("ERROR: This path is not compatible!") << Qt::endl;
        exit(1);
      }

      pathToGorg=aux;
    }
    else
    {
      qout << QLatin1String("ERROR: You should specify a relative filename or relative path to gorg (send)!") << Qt::endl;
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
