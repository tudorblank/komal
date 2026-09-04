#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>
#include <QApplication>
#include <QWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QStatusBar>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QStyle>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QLabel>

#include <memory>

#include "canvasview.hpp"
#include "graphview.hpp"
#include "project.hpp"

class UserInterface : public QMainWindow
{
    Q_OBJECT
public:
    UserInterface(QWidget *parent = nullptr);
    ~UserInterface();

private:
    std::shared_ptr<Project> m_project = nullptr;
    CanvasView* m_canvasView = nullptr;
    NodeGraphView* m_graphView = nullptr;
};