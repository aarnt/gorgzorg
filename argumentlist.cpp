#include <QCoreApplication>
#include "argumentlist.h"

/**
  @author S. Alan Ezust sae@mcs.suffolk.edu
  @since qt 3.2.1

  Obtain the command line arguments from the currently
  running QApplication
*/

ArgumentList::ArgumentList()
{
  if (qApp != nullptr)  /* a global pointer to the current qApplication */
    *this = qApp->arguments();
}

void ArgumentList::argsToStringlist(int argc, char * argv [])
{
  for (int i=0; i < argc; ++i)
  {
    *this += QString::fromLocal8Bit(argv[i]);
  }
}

bool ArgumentList::getSwitch (const QString &option)
{
  QMutableStringListIterator itr(*this);
  while (itr.hasNext())
  {
    if (option == itr.next())
    {
      itr.remove();
      return true;
    }
  }
  return false;
}

QString ArgumentList::getSwitchArg(const QString &option, const QString &defaultValue)
{
  if (isEmpty())
    return defaultValue;

  QMutableStringListIterator itr(*this);

  while (itr.hasNext())
  {
    if (option == itr.next())
    {
      itr.remove();
      if (itr.hasNext())
      {
        QString retval = itr.next();
        itr.remove();
        return retval;
      }
      else
      {
        return QString();
      }
    }
  }
  return defaultValue;
}
