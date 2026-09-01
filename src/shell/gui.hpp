#pragma once

#include <QMainWindow>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QAction>

#include <memory>

#include "canvas.hpp"
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
    CanvasWindow* m_canvasWindow = nullptr;
    NodeGraphView* m_graphView = nullptr;
};