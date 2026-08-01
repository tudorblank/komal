#include "komal.hpp"

#include <QApplication>
#include <QLocale>
#include <QIcon>

int main(int argc, char *argv[])
{
#ifdef Q_OS_UNIX
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    komal w;
    w.show();
    app.setWindowIcon(QIcon("komal.ico"));
    return QApplication::exec();
}
