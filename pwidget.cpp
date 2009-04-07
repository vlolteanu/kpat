/*
   patience -- main program
   Copyright (C) 1995  Paul Olav Tvete <paul@troll.no>

   Permission to use, copy, modify, and distribute this software and its
   documentation for any purpose and without fee is hereby granted,
   provided that the above copyright notice appear in all copies and that
   both that copyright notice and this permission notice appear in
   supporting documentation.

   This file is provided AS IS with no warranties of any kind.  The author
   shall have no liability with respect to the infringement of copyrights,
   trade secrets or any patents by this file or any part thereof.  In no
   event will the author be liable for any lost revenue or profits or
   other special, indirect and consequential damages.


   Heavily modified by Mario Weilguni <mweilguni@sime.com>
*/

#include "pwidget.h"
#include "version.h"
#include "dealer.h"
#include "view.h"
#include "cardmaps.h"
#include "gamestatsimpl.h"
#include "demo.h"
#include "render.h"
#include <carddeckinfo.h>

#include <QTimer>
#include <QList>
#include <QDomDocument>
#include <QDesktopWidget>
#include <QStackedWidget>

#include <kapplication.h>
#include <klocale.h>
#include <kaction.h>
#include <kactioncollection.h>
#include <ktoggleaction.h>
#include <kstandardaction.h>
#include <kstandardgameaction.h>
#include <kdebug.h>
#include <kcarddialog.h>
#include <krandom.h>
#include <kinputdialog.h>
#include <kstandarddirs.h>
#include <kfiledialog.h>
#include <ktemporaryfile.h>
#include <kio/netaccess.h>
#include <kmessagebox.h>
#include <kstatusbar.h>
#include <kmenubar.h>
#include <ktoolinvocation.h>
#include <kglobal.h>
#include <kicon.h>
#include <kconfiggroup.h>
#include <kxmlguifactory.h>


static pWidget *current_pwidget = 0;

pWidget::pWidget()
  : KXmlGuiWindow(0),
    dill(0),
    m_dealer(0),
    m_bubbles(0)
{
    setObjectName( "pwidget" );
    current_pwidget = this;
    // KCrash::setEmergencySaveFunction(::saveGame);

    // Game
    KStandardGameAction::gameNew(this, SLOT(newGame()), actionCollection());
    KStandardGameAction::restart(this, SLOT(restart()), actionCollection());
    KStandardGameAction::load(this, SLOT(openGame()), actionCollection());
    recent = KStandardGameAction::loadRecent(this, SLOT(openGame(const KUrl&)), actionCollection());
    recent->loadEntries(KGlobal::config()->group( QString() ));
    KStandardGameAction::save(this, SLOT(saveGame()), actionCollection());
    KStandardGameAction::quit(this, SLOT(close()), actionCollection());

    // Move
    undo = KStandardGameAction::undo(this, SLOT(undoMove()), actionCollection());
    redo = KStandardGameAction::redo(this, SLOT(redoMove()), actionCollection());

    KAction *a;
    a = actionCollection()->addAction("choose_game");
    a->setText(i18n("&Choose Game..."));
    connect( a, SIGNAL( triggered( bool ) ), SLOT( chooseGame() ) );

    a = actionCollection()->addAction("change_game_type");
    a->setText(i18n("Change Game Type..."));
    connect( a, SIGNAL( triggered( bool ) ), SLOT( slotShowGameSelectionScreen() ) );

    a = actionCollection()->addAction("random_set");
    a->setText(i18n("Random Cards"));
    connect( a, SIGNAL( triggered( bool ) ), SLOT( slotPickRandom() ) );
    a->setShortcuts( KShortcut( Qt::Key_F9 ) );

    if (!qgetenv("KDE_DEBUG").isEmpty()) // developer shortcut
    {
        a = actionCollection()->addAction("snapshot");
        a->setText(i18n("Take Game Preview Snapshots"));
        connect( a, SIGNAL( triggered( bool ) ), SLOT( slotSnapshot() ) );
        a->setShortcuts( KShortcut( Qt::Key_F8 ) );
    }

    a = actionCollection()->addAction("select_deck");
    a->setText(i18n("Select Deck..."));
    connect( a, SIGNAL( triggered( bool ) ), SLOT( slotSelectDeck() ) );
    a->setShortcuts( KShortcut( Qt::Key_F10 ) );


    a = actionCollection()->addAction("game_stats");
    a->setText(i18n("Statistics"));
    connect( a, SIGNAL( triggered( bool ) ), SLOT(showStats()) );

    gamehelpaction = actionCollection()->addAction("help_game");
    connect( gamehelpaction, SIGNAL( triggered( bool ) ), SLOT(helpGame()));
    gamehelpaction->setShortcuts( KShortcut( Qt::Key_F2 ) );

    // Game type dependent actions
    hintaction = KStandardGameAction::hint( 0, 0, actionCollection() );

    demoaction = KStandardGameAction::demo( 0, 0, actionCollection() );

    dealaction = actionCollection()->addAction("move_deal");
    dealaction->setText( i18n("Dea&l") );
    dealaction->setIcon( KIcon("kpat") );
    dealaction->setShortcut( Qt::Key_Space );

    redealaction = actionCollection()->addAction("move_redeal");
    redealaction->setText( i18n("&Redeal") );
    redealaction->setIcon( KIcon("roll") );
    redealaction->setShortcut( Qt::Key_R );

    dropaction = actionCollection()->addAction("move_drop");
    dropaction->setText( i18n("Drop") );
    dropaction->setIcon( KIcon("legalmoves") );
    dropaction->setShortcut( Qt::Key_P );

    // Configuration actions
    KConfigGroup cg(KGlobal::config(), settings_group );

    autodropaction = new KToggleAction(i18n("&Enable Autodrop"), this);
    actionCollection()->addAction("enable_autodrop", autodropaction);
    connect( autodropaction, SIGNAL( triggered( bool ) ), SLOT(enableAutoDrop()) );
    autodropaction->setChecked( cg.readEntry("Autodrop", true) );

    solveraction = new KToggleAction(i18n("E&nable Solver"), this);
    actionCollection()->addAction("enable_solver", solveraction);
    connect( solveraction, SIGNAL( triggered( bool ) ), SLOT( enableSolver() ) );
    solveraction->setChecked( cg.readEntry("Solver", true) );

    rememberstateaction = new KToggleAction(i18n("&Remember State on Exit"), this);
    actionCollection()->addAction("remember_state", rememberstateaction);
    connect( rememberstateaction, SIGNAL( triggered( bool ) ), SLOT( enableRememberState() ) );
    rememberstateaction->setChecked( cg.readEntry("RememberStateOnExit", false) );

    foreach( const DealerInfo * di, DealerInfoList::self()->games() )
    {
        foreach( int id, di->ids )
            m_dealer_map.insert( id, di );
        if ( QString(di->name).toLower() == "freecell" )
            m_freeCellId = di->ids.first();
    }
    m_dealer_it = m_dealer_map.constEnd();

    m_cards = new cardMap();

    m_stack = new QStackedWidget;
    setCentralWidget( m_stack );

    QSize defaultSize = qApp->desktop()->availableGeometry().size() * 0.7;
    setupGUI(defaultSize, Create | Save | ToolBar | StatusBar | Keys);
    statusBar()->insertPermanentItem( "", 1, 0 );

    Render::loadTheme( KStandardDirs::locate( "data", "kpat/theme.svgz" ) );
}

pWidget::~pWidget()
{
    recent->saveEntries(KGlobal::config()->group( QString() ));

    delete dill;
    delete m_cards;
}

void pWidget::undoMove() {
    if( m_dealer )
        m_dealer->undo();
}

void pWidget::redoMove() {
    if( m_dealer )
        m_dealer->redo();
}

void pWidget::helpGame()
{
    if (m_dealer && m_dealer_map.contains(m_dealer->gameId()))
    {
        const DealerInfo * di = m_dealer_map.value(m_dealer->gameId());
        QString anchor = "game_" + QString(di->name);
        anchor = anchor.remove('\'').replace('&', "and").replace(' ', '_').toLower();

        KToolInvocation::invokeHelp(anchor);
    }
}

void pWidget::enableAutoDrop()
{
    bool drop = autodropaction->isChecked();
    KConfigGroup cg(KGlobal::config(), settings_group );
    cg.writeEntry( "Autodrop", drop);
    if ( m_dealer )
        m_dealer->setAutoDropEnabled(drop);
    updateGameActionList();
}

void pWidget::enableSolver()
{
    bool solver = solveraction->isChecked();
    KConfigGroup cg(KGlobal::config(), settings_group );
    cg.writeEntry( "Solver", solver );
    if ( m_dealer )
        m_dealer->setSolverEnabled(solver);
}

void pWidget::enableRememberState()
{
    KConfigGroup cg(KGlobal::config(), settings_group );
    cg.writeEntry( "RememberStateOnExit", rememberstateaction->isChecked() );
}

void pWidget::newGame()
{
    if (allowedToStartNewGame())
        startRandom();
}

void pWidget::restart()
{
    startNew(-1);
}

void pWidget::startRandom()
{
    startNew(KRandom::random());
}

void pWidget::startNew(long gameNumber)
{
    statusBar()->clearMessage();
    m_dealer->startNew(gameNumber);
    setGameCaption();
}

void pWidget::slotPickRandom()
{
    QString theme = CardDeckInfo::randomFrontName();
    kDebug(11111) << "theme" << theme;

    KSharedConfig::Ptr config = KGlobal::config();
    KConfigGroup cs(config, settings_group );
    CardDeckInfo::writeFrontTheme( cs, theme );

    cardMap::self()->updateTheme(cs);
    cardMap::self()->triggerRescale();
}

void pWidget::slotSelectDeck()
{
    KSharedConfig::Ptr config = KGlobal::config();
    KConfigGroup cs(config, settings_group);
    KCardWidget* cardwidget = new KCardWidget();
    cardwidget->readSettings(cs);
    QString deckName, oldDeckName;
    QString cardName, oldCardName;
    cardName = oldCardName = cardwidget->backName();
    deckName = oldDeckName = cardwidget->frontName();

    KCardDialog dlg(cardwidget);
    if (dlg.exec() == QDialog::Accepted)
    {
        // Always store the settings, as other things than only the deck may
        // have changed
        cardwidget->saveSettings(cs);
        cs.sync();

        deckName = cardwidget->backName();
        cardName = cardwidget->frontName();

        if (deckName != oldDeckName || cardName != oldCardName) {
            cardMap::self()->updateTheme(cs);
            cardMap::self()->triggerRescale();
        }
    }
}

void pWidget::setGameCaption()
{
    QString caption;
    if ( m_dealer )
    {
        const DealerInfo * di = m_dealer_map.value( m_dealer->gameId() );
        caption = QString("%1 - %2").arg(di->name).arg(m_dealer->gameNumber());
    }
    setCaption( caption );
}

bool pWidget::allowedToStartNewGame()
{
    // Check if the user is already running a game, and if she is,
    // then ask if she wants to abort it.
    return !m_dealer
           || !m_dealer->hasBeenStarted()
           || m_dealer->wasJustSaved()
           || m_dealer->isGameWon()
           || m_dealer->isGameLost()
           || KMessageBox::warningContinueCancel(0,
                                                 i18n("You are already running an unfinished game. "
                                                      "If you abort the old game to start a new one, "
                                                      "the old game will be registered as a loss in "
                                                      "the statistics file.\n"
                                                      "What do you want to do?"),
                                                 i18n("Abort Current Game?"),
                                                 KGuiItem(i18n("Abort Current Game")),
                                                 KStandardGuiItem::cancel(),
                                                 "careaboutstats"
                                                ) == KMessageBox::Continue;
}

void pWidget::slotGameSelected(int id)
{
    if ( m_dealer_map.contains(id) )
    {
        newGameType(id);
        QTimer::singleShot(0 , this, SLOT( startRandom() ) );
    }
}

void pWidget::newGameType(int id)
{
    slotUpdateMoves();
    kDebug(11111) << gettime() << "pWidget::newGameType\n";

    if ( !dill )
    {
        dill = new PatienceView(this, m_stack);
        m_stack->addWidget(dill);
    }

    // If we're replacing an existing DealerScene, record the stats of the
    // game already in progress.
    if ( m_dealer )
        m_dealer->recordGameStatistics();

    const DealerInfo * di = m_dealer_map.value(id, DealerInfoList::self()->games().first());
    m_dealer = di->createGame();
    m_dealer->setGameId( di->ids.first() );
    m_dealer->setAutoDropEnabled( autodropaction->isChecked() );
    m_dealer->setSolverEnabled( solveraction->isChecked() );

    dill->setScene( m_dealer );
    m_stack->setCurrentWidget(dill);

    gamehelpaction->setText(i18n("Help &with %1", QString(di->name).replace('&', "&&")));

    connect(m_dealer, SIGNAL(gameLost()), SLOT(gameLost()));
    connect(m_dealer, SIGNAL(gameInfo(QString)), SLOT(slotGameInfo(QString)));
    connect(m_dealer, SIGNAL(updateMoves()), SLOT(slotUpdateMoves()));
    connect(m_dealer, SIGNAL(gameSolverStart()), SLOT(slotGameSolverStart()));
    connect(m_dealer, SIGNAL(gameSolverWon()), SLOT(slotGameSolverWon()));
    connect(m_dealer, SIGNAL(gameSolverLost()), SLOT(slotGameSolverLost()));
    connect(m_dealer, SIGNAL(gameSolverUnknown()), SLOT(slotGameSolverUnknown()));

    updateActions();
}

void pWidget::slotShowGameSelectionScreen()
{
    if (allowedToStartNewGame())
    {
        if (m_dealer)
        {
            m_dealer->recordGameStatistics();
            m_dealer = 0;
        }

        if (!m_bubbles)
        {
            m_bubbles = new DemoBubbles(m_stack);
            m_stack->addWidget(m_bubbles);
            connect( m_bubbles, SIGNAL( gameSelected( int ) ), SLOT( slotGameSelected( int ) ) );
        }
        m_stack->setCurrentWidget(m_bubbles);

        gamehelpaction->setText(i18n("Help &with Current Game"));

        updateActions();

        setGameCaption();
    }
}

void pWidget::updateActions()
{
    // Enable/disable application actions that aren't appropriate on game
    // selection screen.
    actionCollection()->action( "game_new" )->setEnabled( m_dealer );
    actionCollection()->action( "game_restart" )->setEnabled( m_dealer );
    actionCollection()->action( "game_save" )->setEnabled( m_dealer );
    actionCollection()->action( "choose_game" )->setEnabled( m_dealer );
    actionCollection()->action( "change_game_type" )->setEnabled( m_dealer );
    gamehelpaction->setEnabled( m_dealer );

    // Initially disable game actions. They'll be reenabled through signals
    // if/when appropriate.
    undo->setEnabled( false );
    redo->setEnabled( false );
    hintaction->setEnabled( false );
    demoaction->setEnabled( false );
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

        connect( dealaction, SIGNAL(triggered(bool)), m_dealer, SLOT(dealNext()) );
        connect( m_dealer, SIGNAL(dealPossible(bool)), dealaction, SLOT(setEnabled(bool)) );

        connect( redealaction, SIGNAL(triggered(bool)), m_dealer, SLOT(redeal()) );
        connect( m_dealer, SIGNAL(redealPossible(bool)), redealaction, SLOT(setEnabled(bool)) );

        connect( dropaction, SIGNAL(triggered(bool)), m_dealer, SLOT(slotAutoDrop()) );
    }
    else
    {
        // Remove the game type specific options from the GUI.
        guiFactory()->unplugActionList( this, "dealer_options" );
        delete actionCollection()->action( "dealer_options" );
    }

    updateGameActionList();
}

void pWidget::updateGameActionList()
{
    guiFactory()->unplugActionList( this, "game_actions" );

    dropaction->setEnabled( m_dealer && !m_dealer->autoDrop() );

    if ( m_dealer )
    {
        QList<QAction*> actionList;
        if ( m_dealer->actions() & DealerScene::Hint )
            actionList.append( hintaction );
        if ( m_dealer->actions() & DealerScene::Demo )
            actionList.append( demoaction );
        if ( m_dealer->actions() & DealerScene::Deal )
            actionList.append( dealaction );
        if ( m_dealer->actions() & DealerScene::Redeal )
            actionList.append( redealaction );
        if ( !m_dealer->autoDrop() )
            actionList.append( dropaction );
        guiFactory()->plugActionList( this, "game_actions", actionList );
    }
}

void pWidget::toggleDemoAction(bool active) 
{
    demoaction->setChecked( active );
    demoaction->setIcon( KIcon( active ? "media-playback-pause" : "media-playback-start" ) );
}

void pWidget::saveNewToolbarConfig()
{
    KXmlGuiWindow::saveNewToolbarConfig();
    updateGameActionList();
}

void pWidget::closeEvent(QCloseEvent *e)
{
    QFile savedState(KStandardDirs::locateLocal("appdata", saved_state_file));
    if (savedState.exists())
        savedState.remove();

    if ( m_dealer )
    {
        if (rememberstateaction->isChecked())
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

void pWidget::slotGameInfo(const QString &text)
{
    statusBar()->showMessage(text, 3000);
}

void pWidget::slotUpdateMoves()
{
    int moves = m_dealer ? m_dealer->getMoves() : 0;
    statusBar()->changeItem( i18np("1 move", "%1 moves", moves), 1 );
}

void pWidget::chooseGame()
{
    QString text = (m_dealer && m_dealer->gameId() == m_freeCellId)
                   ? i18n("Enter a game number (Freecell deals are the same as in the Freecell FAQ):")
                   : i18n("Enter a game number:");
    bool ok;
    long number = KInputDialog::getText(i18n("Game Number"),
                                        text,
                                        QString::number(m_dealer->gameNumber()),
                                        0,
                                        this).toLong(&ok);

    if (ok && allowedToStartNewGame())
        startNew(number);
}

void pWidget::gameLost()
{
    statusBar()->showMessage( i18n( "This game is lost." ) );
}

bool pWidget::openGame(const KUrl &url, bool addToRecentFiles)
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
                    if (allowedToStartNewGame())
                    {
                        // If the file contains pile and card data, then load it.
                        // If it only contains an ID, just launch a new game with that ID.
                        if (doc.documentElement().hasChildNodes())
                        {
                            newGameType(id);
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

void pWidget::openGame()
{
    KUrl url = KFileDialog::getOpenUrl(KUrl("kfiledialog:///kpat"));
    if (!url.isEmpty())
        openGame(url);
}

void pWidget::saveGame()
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

void pWidget::showStats()
{
    GameStatsImpl dlg(this);
    if (m_dealer)
        dlg.showGameType(m_dealer->gameId());
    dlg.exec();
}

void pWidget::slotGameSolverWon()
{
    statusBar()->showMessage(i18n( "This game can still be won! Good luck to you." ));
}

void pWidget::slotGameSolverStart()
{
    statusBar()->showMessage(i18n( "Calculating..." ) );
}

void pWidget::slotGameSolverLost()
{
    statusBar()->showMessage(i18n( "Nope, this game cannot be won anymore." ));
}

void pWidget::slotGameSolverUnknown()
{
    statusBar()->showMessage( i18n( "Timeout while playing - unknown if it can be won" ) );
}

void pWidget::slotSnapshot()
{
    if ( m_dealer_it == m_dealer_map.constEnd() )
    {
        // first call
        m_dealer_it = m_dealer_map.constBegin();
    }

    newGameType(m_dealer_it.key());
    startRandom();
    m_dealer_it++;
    QTimer::singleShot( 200, this, SLOT( slotSnapshot2() ) );
}

void pWidget::slotSnapshot2()
{
    if ( m_dealer->waiting() )
    {
            QTimer::singleShot( 100, this, SLOT( slotSnapshot2() ) );
            return;
    }
    QImage img = QImage( dill->size(), QImage::Format_ARGB32 );
    img.fill( qRgba( 0, 0, 255, 0 ) );
    m_dealer->createDump( &img );
    img = img.scaled( 320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation );
    img.save( QString( "out_%1.png" ).arg( m_dealer->gameId() ) );
    if ( m_dealer_it != m_dealer_map.constEnd() )
        QTimer::singleShot( 200, this, SLOT( slotSnapshot() ) );
}

#include "pwidget.moc"
