#include "shell/gui.hpp"

#include <QApplication>
#include <QLocale>
#include <QIcon>

#include <QFontDatabase>
#include <QCoreApplication>
#include <QDir>

int main(int argc, char *argv[])
{
#ifdef Q_OS_UNIX
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    
    // font
    QString fontPath = QCoreApplication::applicationDirPath() + "/assets/font.ttf";
    int fontId = QFontDatabase::addApplicationFont(fontPath);

    QString family = "Segoe UI"; // fallback if load fails
    if(fontId != -1)
    {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if(!families.isEmpty()) family = families.at(0);
    }
    else qDebug() << "Failed to load font from" << fontPath;

    QFont appFont(family);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    appFont.setHintingPreference(QFont::PreferFullHinting);

    constexpr int kTargetCapHeightPx = 9;
    appFont.setPixelSize(20);
    QFontMetrics fm(appFont);
    if(fm.capHeight() > 0)
    {
        int seedCap = fm.capHeight();
        int correctedPx = (int)((float)kTargetCapHeightPx / seedCap * 20.0f);
        appFont.setPixelSize(std::max(8, correctedPx));
    }

    app.setFont(appFont);

    // ui
    UserInterface w;
    w.show();

    // icon
    app.setWindowIcon(QIcon("komal.ico"));
    
    return QApplication::exec();
}
