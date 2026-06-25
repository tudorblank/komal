#ifndef KOMAL_H
#define KOMAL_H

#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>

class CanvasWindow;

class komal : public QMainWindow
{
    Q_OBJECT
public:
    komal(QWidget *parent = nullptr);
    ~komal();

private:
    CanvasWindow *m_canvasWindow = nullptr;
};

#endif