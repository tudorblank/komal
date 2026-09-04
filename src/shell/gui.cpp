#include "gui.hpp"

UserInterface::UserInterface(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Komal Studio");
    resize(1400, 860);
    setStyleSheet(R"(
        QDockWidget
        {
            color: #d0d0d0;
            font-size: 11px;
        }

        QDockWidget::title
        {
            background: #111;
            padding: 4px;
        }
    )");
    m_project = std::make_shared<Project>();
    m_project->init();

    // --- Menu Bar ---
        //File
        QMenu *fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction(QIcon::fromTheme("document-new"),  "&New",   QKeySequence::New);
        fileMenu->addAction(QIcon::fromTheme("document-open"), "&Open",  QKeySequence::Open);
        fileMenu->addAction(QIcon::fromTheme("document-save"), "&Save",  QKeySequence::Save);
        fileMenu->addAction("Save &As...", QKeySequence::SaveAs);
        fileMenu->addSeparator();
        fileMenu->addAction("&Export...");
        fileMenu->addSeparator();
        fileMenu->addAction(QIcon::fromTheme("application-exit"), "&Quit", QKeySequence::Quit,
                            this, &QMainWindow::close);
        //Edit
        QMenu *editMenu = menuBar()->addMenu("&Edit");
        editMenu->addAction(QIcon::fromTheme("edit-undo"),  "&Undo",  QKeySequence::Undo);
        editMenu->addAction(QIcon::fromTheme("edit-redo"),  "&Redo",  QKeySequence::Redo);
        editMenu->addSeparator();
        editMenu->addAction(QIcon::fromTheme("edit-cut"),   "Cu&t",   QKeySequence::Cut);
        editMenu->addAction(QIcon::fromTheme("edit-copy"),  "&Copy",  QKeySequence::Copy);
        editMenu->addAction(QIcon::fromTheme("edit-paste"), "&Paste", QKeySequence::Paste);
        editMenu->addSeparator();
        editMenu->addAction("&Find...",    QKeySequence::Find);
        editMenu->addAction("&Replace...", QKeySequence::Replace);
        //View
        QMenu *viewMenu = menuBar()->addMenu("&View");
        viewMenu->addAction("Zoom &In",  QKeySequence::ZoomIn);
        viewMenu->addAction("Zoom &Out", QKeySequence::ZoomOut);
        viewMenu->addSeparator();
        QAction *showLeftPanel  = viewMenu->addAction("Show &Left Panel");
        QAction *showRightPanel = viewMenu->addAction("Show &Right Panel");
        showLeftPanel->setCheckable(true);
        showRightPanel->setCheckable(true);
        showLeftPanel->setChecked(true);
        showRightPanel->setChecked(true);

        menuBar()->addMenu("&Help")->addAction("&About");
        menuBar()->setStyleSheet(R"(
            QMenuBar
            {
                font-size: 11px;
                spacing: 6px;
                background: #111;
            }

            QMenuBar::item
            {
                padding: 4px 8px;
                margin: 1px;
                border-radius: 2px;
            }

            QMenuBar::item:selected
            {
                background: #2a2a2a;
            }

            QMenuBar::item:pressed
            {
                background: #3a3a3a;
            }
        )");
    //

    // --- Canvas Window ---
        m_canvasView = new CanvasView(m_project);

        QWidget *canvasContainer = QWidget::createWindowContainer(m_canvasView, this);
        canvasContainer->setMinimumSize(400, 300);
        canvasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        canvasContainer->setFocusPolicy(Qt::WheelFocus);

        canvasContainer->setAutoFillBackground(true);
        QPalette pal = canvasContainer->palette();
        pal.setColor(QPalette::Window, QColor(38, 38, 38));
        canvasContainer->setPalette(pal);

        // dock
        QDockWidget *canvasDock = new QDockWidget("Canvas View", this);
        canvasDock->setObjectName("CanvasDock");
        canvasDock->setWidget(canvasContainer);
        canvasDock->setFeatures(QDockWidget::NoDockWidgetFeatures);

        canvasDock->setAllowedAreas(Qt::AllDockWidgetAreas);
        addDockWidget(Qt::RightDockWidgetArea, canvasDock);

        connect(showRightPanel, &QAction::toggled, canvasDock, &QDockWidget::setVisible);
    //

    // --- Node Graph View ---
        m_graphView = new NodeGraphView(m_project);

        QDockWidget* graphDock = new QDockWidget("Graph View", this);
        graphDock->setObjectName("GraphDock");
        graphDock->setWidget(m_graphView);
        addDockWidget(Qt::LeftDockWidgetArea, graphDock);
        graphDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    //
    // even split for canvas + graph views
    resizeDocks({graphDock, canvasDock}, {width() / 2, width() / 2}, Qt::Horizontal);

    // --- Toolbar ---
        QToolBar *toolbar = new QToolBar("Toolbar", this);
        addToolBar(Qt::TopToolBarArea, toolbar);
        toolbar->setMovable(true);
        toolbar->setIconSize(QSize(17, 17));
        toolbar->setAllowedAreas(Qt::LeftToolBarArea | Qt::RightToolBarArea | Qt::TopToolBarArea | Qt::BottomToolBarArea);
        toolbar->setOrientation(Qt::Horizontal);

        QActionGroup *toolGroup = new QActionGroup(this);
        toolGroup->setExclusive(true);

        auto addTool = [&](const QString &themeIcon, const QString &name) -> QAction*
        {
            QIcon icon = QIcon::fromTheme(themeIcon, style()->standardIcon(QStyle::SP_FileIcon));

            QIcon fixedIcon;
            QPixmap px = icon.pixmap(QSize(17, 17), QIcon::Normal, QIcon::Off);
            fixedIcon.addPixmap(px, QIcon::Normal,   QIcon::Off);
            fixedIcon.addPixmap(px, QIcon::Normal,   QIcon::On);
            fixedIcon.addPixmap(px, QIcon::Active,   QIcon::Off);
            fixedIcon.addPixmap(px, QIcon::Active,   QIcon::On);
            fixedIcon.addPixmap(px, QIcon::Selected, QIcon::Off);
            fixedIcon.addPixmap(px, QIcon::Selected, QIcon::On);

            QAction *a = toolbar->addAction(fixedIcon, name);
            a->setToolTip(name);
            a->setCheckable(true);
            toolGroup->addAction(a);
            return a;
        };

        addTool("edit-cut", "Select");
        addTool("edit-cut", "Draw");
        addTool("edit-cut", "Cut");

        toolbar->addSeparator();

        connect(toolGroup, &QActionGroup::triggered, this, [this, toolGroup, canvasContainer](QAction *active)
        {
            for (QAction *a : toolGroup->actions())
                a->setEnabled(a != active);

            statusBar()->showMessage("Tool: " + active->text());

            canvasContainer->setFocus();
            m_canvasView->requestActivate();
        });

        toolbar->setStyleSheet(R"(
            QToolBar
            {
                background: #111;
            }

            QToolButton
            {
                background: transparent;
                border: none;
                padding: 6px;
                border-radius: 3px;
            }

            QToolButton:hover
            {
                background: #343434;
            }

            QToolButton:checked
            {
                background: #2a2a2a;
            }

            QToolButton:pressed
            {
                background: #161616;
            }
        )");

        toolbar->setPalette(QApplication::palette());
    //

    // --- Status Bar ---
        statusBar()->setSizeGripEnabled(false);
        statusBar()->showMessage("Ready");
        
        statusBar()->setStyleSheet(R"(
            QStatusBar
            {
                background: #111;
                color: #d0d0d0;
                border-top: 1px solid #222;
            }
        )");
    //
}

UserInterface::~UserInterface() 
{
    if(m_canvasView) {
        delete m_canvasView;
        m_canvasView = nullptr;
    }
    if(m_graphView) {
        delete m_graphView;
        m_graphView = nullptr;
    }
}