/*
 * Copyright (C) 1995 Paul Olav Tvete <paul@troll.no>
 * Copyright (C) 1997 Mario Weilguni <mweilguni@sime.com>
 * Copyright (C) 2000-2009 Stephan Kulow <coolo@kde.org>
 * Copyright (C) 2009-2010 Parker Coates <parker.coates@kdemail.net>
 *
 * License of original code:
 * -------------------------------------------------------------------------
 *   Permission to use, copy, modify, and distribute this software and its
 *   documentation for any purpose and without fee is hereby granted,
 *   provided that the above copyright notice appear in all copies and that
 *   both that copyright notice and this permission notice appear in
 *   supporting documentation.
 *
 *   This file is provided AS IS with no warranties of any kind.  The author
 *   shall have no liability with respect to the infringement of copyrights,
 *   trade secrets or any patents by this file or any part thereof.  In no
 *   event will the author be liable for any lost revenue or profits or
 *   other special, indirect and consequential damages.
 * -------------------------------------------------------------------------
 *
 * License of modifications/additions made after 2009-01-01:
 * -------------------------------------------------------------------------
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of 
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * -------------------------------------------------------------------------
 */

#include "mainwindow.h"

#include "dealer.h"
#include "dealerinfo.h"
#include "gameselectionscene.h"
#include "renderer.h"
#include "settings.h"
#include "statisticsdialog.h"
#include "version.h"
#include "view.h"

#include "KAbstractCardDeck"
#include "KCardTheme"
#include "KCardThemeWidget"

#include <KGameTheme>
#include <KGameThemeSelector>
#include <KStandardGameAction>

#include <KAction>
#include <KActionCollection>
#include <KConfigDialog>
#include <KDebug>
#include <KFileDialog>
#include <KGlobal>
#include <KIcon>
#include <KInputDialog>
#include <KLocale>
#include <KMessageBox>
#include <KRandom>
#include <KRecentFilesAction>
#include <KStandardDirs>
#include <KStatusBar>
#include <KTemporaryFile>
#include <KToggleAction>
#include <KToolInvocation>
#include <KXMLGUIFactory>
#include <KIO/NetAccess>

#include <QtCore/QList>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QDesktopWidget>
#include <QtGui/QStackedWidget>
#include <QtXml/QDomDocument>


MainWindow::MainWindow()
  : KXmlGuiWindow(0),
    m_view(0),
    m_dealer(0),
    m_selector(0),
    m_cardDeck(0)
{
    setObjectName( "MainWindow" );
    // KCrash::setEmergencySaveFunction(::saveGame);

    setupActions();

    foreach( const DealerInfo * di, DealerInfoList::self()->games() )
        foreach( int id, di->ids() )
            m_dealer_map.insert( id, di );
    m_dealer_it = m_dealer_map.constEnd();

    m_view = new PatienceView( this );
    setCentralWidget( m_view );

    QSize defaultSize = qApp->desktop()->availableGeometry().size() * 0.7;
    setupGUI(defaultSize, Create | Save | ToolBar | StatusBar | Keys);

    solverStatus = new QLabel(QString(), statusBar());
    solverStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusBar()->addWidget(solverStatus, 1);

    moveStatus = new QLabel(QString(), statusBar());
    moveStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addWidget(moveStatus, 0);

    if ( !Renderer::self()->setTheme( Settings::theme() ) )
    {
        Settings::setTheme( Settings::defaultThemeValue() );
        Renderer::self()->setTheme( Settings::theme() );
    }
}

MainWindow::~MainWindow()
{
    recent->saveEntries(KGlobal::config()->group( QString() ));

    Settings::self()->writeConfig();

    delete m_dealer;
    delete m_view;
}


void MainWindow::setupActions()
{
    KAction *a;

    // Game Menu
    a = actionCollection()->addAction("new_game");
    a->setText(i18nc("Start a new game of a different type","New &Game..."));
    a->setIcon( KIcon("document-new") );
    a->setShortcuts( KShortcut( Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_N ) );
    connect( a, SIGNAL(triggered(bool)), SLOT(slotShowGameSelectionScreen()) );

    a = actionCollection()->addAction("new_deal");
    a->setText(i18nc("Start a new game of without changing the game type", "New &Deal"));
    a->setIcon( KIcon("document-new") );
    a->setShortcuts( KShortcut( Qt::ControlModifier | Qt::Key_N ) );
    connect( a, SIGNAL(triggered(bool)), SLOT(newGame()) );

    a = actionCollection()->addAction("new_numbered_deal");
    a->setText(i18nc("Start a game by giving its particular number", "New &Numbered Deal..."));
    connect( a, SIGNAL(triggered(bool)), SLOT(chooseDeal()) );

    a = KStandardGameAction::restart(this, SLOT(restart()), actionCollection());
    a->setText(i18nc("Replay the current deal from the start", "Restart Deal"));

    KStandardGameAction::load(this, SLOT(openGame()), actionCollection());

    recent = KStandardGameAction::loadRecent(this, SLOT(openGame(const KUrl&)), actionCollection());
    recent->loadEntries(KGlobal::config()->group( QString() ));

    a = KStandardGameAction::saveAs(this, SLOT(saveGame()), actionCollection());
    a->setShortcut( KShortcut( Qt::ControlModifier | Qt::Key_S ) );

    a = actionCollection()->addAction("game_stats");
    a->setText(i18n("Statistics"));
    a->setIcon( KIcon("games-highscores") );
    connect( a, SIGNAL(triggered(bool)), SLOT(showStats()) );

    KStandardGameAction::quit(this, SLOT(close()), actionCollection());


    // Move Menu
    undo = KStandardGameAction::undo(this, SLOT(undoMove()), actionCollection());

    redo = KStandardGameAction::redo(this, SLOT(redoMove()), actionCollection());

    hintaction = KStandardGameAction::hint( 0, 0, actionCollection() );

    demoaction = KStandardGameAction::demo( 0, 0, actionCollection() );

    drawaction = actionCollection()->addAction("move_draw");
    drawaction->setText( i18nc("Take one or more cards from the deck, flip them, and place them in play", "Dra&w") );
    drawaction->setIcon( KIcon("kpat") );
    drawaction->setShortcut( Qt::Key_Space );

    dealaction = actionCollection()->addAction("move_deal");
    dealaction->setText( i18nc("Deal a new row of cards from the deck", "Dea&l Row") );
    dealaction->setIcon( KIcon("kpat") );
    dealaction->setShortcut( Qt::Key_Return );

    redealaction = actionCollection()->addAction("move_redeal");
    redealaction->setText( i18nc("Collect the cards in play, shuffle them and redeal them", "&Redeal") );
    redealaction->setIcon( KIcon("roll") );
    redealaction->setShortcut( Qt::Key_R );

    dropaction = actionCollection()->addAction("move_drop");
    dropaction->setText( i18nc("Automatically move cards to the foundation piles", "Dro&p") );
    dropaction->setIcon( KIcon("games-endturn") );
    dropaction->setShortcut( Qt::Key_P );


    // Settings Menu
    a = actionCollection()->addAction("select_deck");
    a->setText(i18n("Change Appearance..."));
    connect( a, SIGNAL(triggered(bool)), SLOT(configureAppearance()) );
    a->setShortcuts( KShortcut( Qt::Key_F10 ) );

    autodropaction = new KToggleAction(i18n("&Enable Autodrop"), this);
    actionCollection()->addAction("enable_autodrop", autodropaction);
    connect( autodropaction, SIGNAL(triggered(bool)), SLOT(enableAutoDrop(bool)) );
    autodropaction->setChecked( Settings::autoDropEnabled() );

    solveraction = new KToggleAction(i18n("E&nable Solver"), this);
    actionCollection()->addAction("enable_solver", solveraction);
    connect( solveraction, SIGNAL(triggered(bool)), SLOT(enableSolver(bool)) );
    solveraction->setChecked( Settings::solverEnabled() );

    rememberstateaction = new KToggleAction(i18n("&Remember State on Exit"), this);
    actionCollection()->addAction("remember_state", rememberstateaction);
    connect( rememberstateaction, SIGNAL(triggered(bool)), SLOT(enableRememberState(bool)) );
    rememberstateaction->setChecked( Settings::rememberStateOnExit() );


    // Help Menu
    gamehelpaction = actionCollection()->addAction("help_game");
    gamehelpaction->setIcon( KIcon("help-browser") );
    connect( gamehelpaction, SIGNAL(triggered(bool)), SLOT(helpGame()));
    gamehelpaction->setShortcuts( KShortcut( Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_F1 ) );


    // Hidden actions
    if (!qgetenv("KDE_DEBUG").isEmpty()) // developer shortcut
    {
        a = actionCollection()->addAction("themePreview");
        a->setText(i18n("Generate a theme preview image"));
        connect( a, SIGNAL(triggered(bool)), SLOT(generateThemePreview()) );
        a->setShortcuts( KShortcut( Qt::Key_F7 ) );

        a = actionCollection()->addAction("snapshot");
        a->setText(i18n("Take Game Preview Snapshots"));
        connect( a, SIGNAL(triggered(bool)), SLOT(slotSnapshot()) );
        a->setShortcuts( KShortcut( Qt::Key_F8 ) );

        a = actionCollection()->addAction("random_set");
        a->setText(i18n("Random Cards"));
        connect( a, SIGNAL(triggered(bool)), SLOT(slotPickRandom()) );
        a->setShortcuts( KShortcut( Qt::Key_F9 ) );
    }
}


void MainWindow::undoMove() {
    if( m_dealer )
        m_dealer->undo();
}

void MainWindow::redoMove() {
    if( m_dealer )
        m_dealer->redo();
}

void MainWindow::helpGame()
{
    if (m_dealer && m_dealer_map.contains(m_dealer->gameId()))
    {
        const DealerInfo * di = m_dealer_map.value(m_dealer->gameId());
        QString anchor = QString(di->name()).toLower();
        anchor = anchor.remove('\'').replace('&', "and").replace(' ', '-');
        KToolInvocation::invokeHelp(anchor);
    }
}

void MainWindow::enableAutoDrop(bool enable)
{
    Settings::setAutoDropEnabled( enable );
    if (m_dealer)
        m_dealer->setAutoDropEnabled(enable);
    updateGameActionList();
}

void MainWindow::enableSolver(bool enable)
{
    Settings::setSolverEnabled( enable );
    solverStatus->setText( QString() );
    if (m_dealer)
    {
        m_dealer->setSolverEnabled(enable);
        if(enable)
            m_dealer->startSolver();
    }
}

void MainWindow::enableRememberState(bool enable)
{
    Settings::setRememberStateOnExit( enable );
}

void MainWindow::newGame()
{
    if (m_dealer && m_dealer->allowedToStartNewGame())
        startRandom();
}

void MainWindow::restart()
{
    startNew(-1);
}

void MainWindow::startRandom()
{
    startNew(KRandom::random());
}

void MainWindow::startNew(int gameNumber)
{
    m_dealer->startNew(gameNumber);
    setGameCaption();
}

void MainWindow::slotPickRandom()
{
    QList<KCardTheme> themes = KCardTheme::findAll();
    KCardTheme theme = themes.at( KRandom::random() % themes.size() );
    Settings::setCardTheme( theme.dirName() );

    appearanceChanged();
}

void MainWindow::configureAppearance()
{
    const QString previewFormat = "back;10_spade,jack_diamond,queen_club,king_heart;1_spade";
    const QSet<QString> features = QSet<QString>() << "AngloAmerican" << "Backs1";

    if ( !KConfigDialog::showDialog("KPatAppearanceDialog") )
    {
        KConfigDialog * dialog = new KConfigDialog( this, "KPatAppearanceDialog", Settings::self() );

        dialog->addPage( new KCardThemeWidget( features, previewFormat ),
                         i18n("Card Deck"),
                         "games-config-theme",
                         i18n("Select a card deck")
                       );

        dialog->addPage( new KGameThemeSelector( this, Settings::self(), KGameThemeSelector::NewStuffEnableDownload ),
                         i18n("Game Theme"),
                         "games-config-theme",
                         i18n("Select a theme for non-card game elements")
                       );

        connect( dialog, SIGNAL(settingsChanged(QString)), this, SLOT(appearanceChanged()) );
        dialog->show();
    }
}


void MainWindow::appearanceChanged()
{
    if ( !Renderer::self()->theme().path().endsWith( Settings::theme() ) )
    {
        Renderer::self()->setTheme( Settings::theme() );
        m_view->resetCachedContent();
        if ( m_dealer )
            m_dealer->updateWonItem( true );
    }

    if ( m_cardDeck && Settings::cardTheme() != m_cardDeck->theme().dirName() )
    {
        KCardTheme theme( Settings::cardTheme() );
        if ( theme.isValid() )
        {
            m_cardDeck->setTheme( KCardTheme( theme ) );
            if ( m_dealer )
                m_dealer->relayoutScene();
        }
    }
}


void MainWindow::setGameCaption()
{
    QString caption;
    if ( m_dealer )
    {
        const DealerInfo * di = m_dealer_map.value( m_dealer->gameId() );
        caption = QString("%1 - %2").arg(di->name()).arg(m_dealer->gameNumber());
    }
    setCaption( caption );
}

void MainWindow::slotGameSelected(int id)
{
    if ( m_dealer_map.contains(id) )
    {
        newGameType(id);
        QTimer::singleShot(0 , this, SLOT( startRandom() ) );
    }
}

void MainWindow::newGameType(int id)
{
    // If we're replacing an existing DealerScene, record the stats of the
    // game already in progress.
    if ( m_dealer )
    {
        m_dealer->recordGameStatistics();
        delete m_dealer;
        m_view->setScene(0);
        m_dealer = 0;
    }

    if ( !m_cardDeck )
    {
        KCardTheme theme = KCardTheme( Settings::cardTheme() );
        if ( !theme.isValid() )
            theme = KCardTheme( Settings::defaultCardThemeValue() );

        m_cardDeck = new KStandardCardDeck( theme, this );
    }

    const DealerInfo * di = m_dealer_map.value(id, DealerInfoList::self()->games().first());
    m_dealer = di->createGame();
    m_dealer->setDeck( m_cardDeck );
    m_dealer->initialize();
    m_dealer->setGameId( di->ids().first() );
    m_dealer->setAutoDropEnabled( autodropaction->isChecked() );
    m_dealer->setSolverEnabled( solveraction->isChecked() );

    m_view->setScene( m_dealer );

    gamehelpaction->setText(i18n("Help &with %1", i18n(di->name()).replace('&', "&&")));

    connect(m_dealer, SIGNAL(solverStateChanged(QString)), SLOT(updateSolverDescription(QString)));
    connect(m_dealer, SIGNAL(updateMoves(int)), SLOT(slotUpdateMoves(int)));

    solverStatus->setText(QString());
    solverStatus->setVisible(true);
    moveStatus->setText(QString());
    moveStatus->setVisible(true);

    updateActions();
}

void MainWindow::slotShowGameSelectionScreen()
{
    if (!m_dealer || m_dealer->allowedToStartNewGame())
    {
        if (m_dealer)
        {
            m_dealer->recordGameStatistics();
            delete m_dealer;
            m_view->setScene(0);
            m_dealer = 0;
        }

        if (!m_selector)
        {
            m_selector = new GameSelectionScene(this);
            connect( m_selector, SIGNAL(gameSelected(int)), SLOT(slotGameSelected(int)) );
        }
        m_view->setScene(m_selector);

        gamehelpaction->setText(i18n("Help &with Current Game"));

        updateActions();

        setGameCaption();

        solverStatus->setVisible(false);
        moveStatus->setVisible(false);
    }
}

void MainWindow::updateActions()
{
    // Enable/disable application actions that aren't appropriate on game
    // selection screen.
    actionCollection()->action( "new_game" )->setEnabled( m_dealer );
    actionCollection()->action( "new_deal" )->setEnabled( m_dealer );
    actionCollection()->action( "new_numbered_deal" )->setEnabled( m_dealer );
    actionCollection()->action( "game_restart" )->setEnabled( m_dealer );
    actionCollection()->action( "game_save_as" )->setEnabled( m_dealer );
    gamehelpaction->setEnabled( m_dealer );

    // Initially disable game actions. They'll be reenabled through signals
    // if/when appropriate.
    undo->setEnabled( false );
    redo->setEnabled( false );
    hintaction->setEnabled( false );
    demoaction->setEnabled( false );
    drawaction->setEnabled( false );
    dealaction->setEnabled( false );
    redealaction->setEnabled( false );

    if ( m_dealer )
    {
        // If a dealer exists, connect the game actions to it.
        connect( m_dealer, SIGNAL(undoPossible(bool)), undo, SLOT(setEnabled(bool)) );
        connect( m_dealer, SIGNAL(redoPossible(bool)), redo, SLOT(setEnabled(bool)) );

        connect( hintaction, SIGNAL(triggered(bool)), m_dealer, SLOT(hint()) );
        connect( m_dealer, SIGNAL(hintPossible(bool)), hintaction, SLOT(setEnabled(bool)) );

        connect( demoaction, SIGNAL(triggered(bool)), m_dealer, SLOT(toggleDemo()) );
        connect( m_dealer, SIGNAL(demoActive(bool)), this, SLOT(toggleDemoAction(bool)) );
        connect( m_dealer, SIGNAL(demoPossible(bool)), demoaction, SLOT(setEnabled(bool)) );

        connect( dropaction, SIGNAL(triggered(bool)), m_dealer, SLOT(startManualDrop()) );

        if ( m_dealer->actions() & DealerScene::Draw )
        {
            connect( drawaction, SIGNAL(triggered(bool)), m_dealer, SLOT(newCards()) );
            connect( m_dealer, SIGNAL(newCardsPossible(bool)), drawaction, SLOT(setEnabled(bool)) );
        }
        else if ( m_dealer->actions() & DealerScene::Deal )
        {
            connect( dealaction, SIGNAL(triggered(bool)), m_dealer, SLOT(newCards()) );
            connect( m_dealer, SIGNAL(newCardsPossible(bool)), dealaction, SLOT(setEnabled(bool)) );
        }
        else if ( m_dealer->actions() & DealerScene::Redeal )
        {
            connect( redealaction, SIGNAL(triggered(bool)), m_dealer, SLOT(newCards()) );
            connect( m_dealer, SIGNAL(newCardsPossible(bool)), redealaction, SLOT(setEnabled(bool)) );
        }

        guiFactory()->unplugActionList( this, "dealer_options" );
        guiFactory()->plugActionList( this, "dealer_options", m_dealer->configActions() );
    }

    updateGameActionList();
}

void MainWindow::updateGameActionList()
{
    guiFactory()->unplugActionList( this, "game_actions" );

    dropaction->setEnabled( m_dealer && !m_dealer->autoDropEnabled() );

    if ( m_dealer )
    {
        QList<QAction*> actionList;
        if ( m_dealer->actions() & DealerScene::Hint )
            actionList.append( hintaction );
        if ( m_dealer->actions() & DealerScene::Demo )
            actionList.append( demoaction );
        if ( m_dealer->actions() & DealerScene::Draw )
            actionList.append( drawaction );
        if ( m_dealer->actions() & DealerScene::Deal )
            actionList.append( dealaction );
        if ( m_dealer->actions() & DealerScene::Redeal )
            actionList.append( redealaction );
        if ( !m_dealer->autoDropEnabled() )
            actionList.append( dropaction );
        guiFactory()->plugActionList( this, "game_actions", actionList );
    }
}

void MainWindow::toggleDemoAction(bool active) 
{
    demoaction->setChecked( active );
    demoaction->setIcon( KIcon( active ? "media-playback-pause" : "media-playback-start" ) );
}

void MainWindow::saveNewToolbarConfig()
{
    KXmlGuiWindow::saveNewToolbarConfig();
    updateGameActionList();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QFile savedState(KStandardDirs::locateLocal("appdata", saved_state_file));
    if (savedState.exists())
        savedState.remove();

    if ( m_dealer )
    {
        if ( Settings::rememberStateOnExit() )
        {
            QFile temp(m_dealer->save_it());
            temp.copy(savedState.fileName());
            temp.remove();
        }
        else
        {
            // If there's a game in progress and we aren't going to save it
            // then record its statistics, since the DealerScene will be destroyed
            // shortly.
            m_dealer->recordGameStatistics();
        }
    }

    KXmlGuiWindow::closeEvent(e);
}

void MainWindow::chooseDeal()
{
    if (m_dealer)
    {
        QString text = (m_dealer->gameId() == DealerInfo::FreecellId)
                       ? i18n("Enter a deal number (Freecell deals are numbered the same as those in the Freecell FAQ):")
                       : i18n("Enter a deal number:");
        bool ok;
        int number = KInputDialog::getInteger(i18n("Choose Numbered Deal"), 
                                              text,
                                              m_dealer->gameNumber(),
                                              1,
                                              INT_MAX,
                                              1,
                                              10,
                                              &ok, 
                                              this);

        if (ok && m_dealer->allowedToStartNewGame())
            startNew(number);
    }
}

bool MainWindow::openGame(const KUrl &url, bool addToRecentFiles)
{
    QString error;
    QString tmpFile;
    if(KIO::NetAccess::download( url, tmpFile, this ))
    {
        QFile of(tmpFile);
        of.open(QIODevice::ReadOnly);
        QDomDocument doc;

        if (doc.setContent(&of, &error))
        {
            if (doc.documentElement().tagName() == "dealer")
            {
                int id = doc.documentElement().attribute("id").toInt();
                if (m_dealer_map.contains(id))
                {
                    // Only ask for permission after we've determined the save game
                    // file is good.
                    if (!m_dealer || m_dealer->allowedToStartNewGame())
                    {
                        // If the file has a game number, then load it.
                        // If it only contains an ID, just launch a new game with that ID.
                        if (doc.documentElement().hasAttribute("number"))
                        {
                            // Only bother calling newGameType if we don't
                            // already have the right DealerScene open.
                            if ( !m_dealer || m_dealer_map.value(id) != m_dealer_map.value(m_dealer->gameId()) )
                                newGameType(id);
                            else
                                m_dealer->recordGameStatistics();

                            // for old spider and klondike
                            m_dealer->mapOldId(id);
                            m_dealer->openGame(doc);
                            setGameCaption();
                        }
                        else
                        {
                            slotGameSelected(id);
                        }

                        if ( addToRecentFiles )
                            recent->addUrl(url);
                    }
                }
                else
                {
                    error = i18n("The saved game is of an unknown type.");
                }
            }
            else
            {
                error = i18n("The file is not a KPatience saved game.");
            }
        }
        else
        {
            error = i18n("The following error occurred while reading the file:\n\"%1\"", error);
        }

        KIO::NetAccess::removeTempFile(tmpFile);
    }
    else
    {
        error = i18n("Unable to load the saved game file.");
    }

    if (!error.isEmpty())
        KMessageBox::error(this, error);

    return error.isEmpty();
}

void MainWindow::openGame()
{
    KUrl url = KFileDialog::getOpenUrl(KUrl("kfiledialog:///kpat"));
    if (!url.isEmpty())
        openGame(url);
}

void MainWindow::saveGame()
{
    if (m_dealer)
    {
        KUrl url = KFileDialog::getSaveUrl(KUrl("kfiledialog:///kpat"));
        QString tmpFile = m_dealer->save_it();
        KIO::NetAccess::upload(tmpFile, url, this);
        KIO::NetAccess::removeTempFile(tmpFile);
        recent->addUrl(url);
    }
}

void MainWindow::showStats()
{
    QPointer<StatisticsDialog> dlg = new StatisticsDialog(this);
    if (m_dealer)
        dlg->showGameType(m_dealer->oldId());
    dlg->exec();
    delete dlg;
}

void MainWindow::updateSolverDescription( const QString & text )
{
    solverStatus->setText( text );
}

void MainWindow::slotUpdateMoves(int moves)
{
    moveStatus->setText(i18np("1 move", "%1 moves", moves));
}

void MainWindow::slotSnapshot()
{
    if ( m_dealer_it == m_dealer_map.constEnd() )
    {
        // first call
        m_dealer_it = m_dealer_map.constBegin();
    }

    newGameType( m_dealer_it.key() );
    m_dealer->setAutoDropEnabled( false );
    startRandom();

    QTimer::singleShot( 1000, this, SLOT( slotSnapshot2() ) );
}

void MainWindow::slotSnapshot2()
{
    m_dealer->createDump().save( QString( "%1.png" ).arg( m_dealer->gameId() ) );

    ++m_dealer_it;
    if ( m_dealer_it != m_dealer_map.constEnd() )
        QTimer::singleShot( 0, this, SLOT( slotSnapshot() ) );
}


void MainWindow::generateThemePreview()
{
    const QSize previewSize( 240, 160 );
    m_view->setMinimumSize( previewSize );
    m_view->setMaximumSize( previewSize );

    QImage img( m_view->contentsRect().size(), QImage::Format_ARGB32 );
    img.fill( Qt::transparent );
    QPainter p( &img );

    foreach ( KCard * c, m_cardDeck->cards() )
        c->completeAnimation();

    m_view->render( &p );

    slotShowGameSelectionScreen();

    QRect leftHalf( 0, 0, m_view->contentsRect().width() / 2, m_view->contentsRect().height() );
    m_view->render( &p, leftHalf, leftHalf );

    p.end();

    img.save( "preview.png" );

    m_view->setMinimumSize( 0, 0 );
    m_view->setMaximumSize( QWIDGETSIZE_MAX, QWIDGETSIZE_MAX );
}

#include "mainwindow.moc"
