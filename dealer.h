/*
 * Copyright (C) 1995 Paul Olav Tvete <paul@troll.no>
 * Copyright (C) 2000-2009 Stephan Kulow <coolo@kde.org>
 * Copyright (C) 2009 Parker Coates <parker.coates@gmail.com>
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

#ifndef DEALER_H
#define DEALER_H

#include "card.h"
class CardDeck;
class CardState;
class DealerScene;
class MoveHint;
#include "pile.h"
class Solver;
#include "view.h"

#include <KRandomSequence>

#include <QtCore/QSet>
#include <QtGui/QAction>
#include <QtGui/QGraphicsScene>
class QDomDocument;


typedef QList<Pile*> PileList;
typedef QList<CardState> CardStateList;

struct State
{
    CardStateList cards;
    QString gameData;
};

class DealerScene : public PatienceGraphicsScene
{
    Q_OBJECT

public:
    enum { None = 0, Hint = 1, Demo = 2, Draw = 4, Deal = 8, Redeal = 16 } Actions;

    DealerScene();
    ~DealerScene();

    void resizeScene( const QSize & size );
    QRectF contentArea() const;

    void addPile(Pile *p);
    void removePile(Pile *p);

    void setLayoutMargin( qreal margin );
    qreal layoutMargin() const;

    void setLayoutSpacing( qreal spacing );
    qreal layoutSpacing() const;

    virtual bool checkRemove( int checkIndex, const Pile *c1, const Card *c) const;
    virtual bool checkAdd   ( int checkIndex, const Pile *c1, const CardList& c2) const;
    virtual bool checkPrefering( int checkIndex, const Pile *c1, const CardList& c2) const;

    bool isMoving(Card *c) const;
    bool cardsAreMoving() const { return !movingCards.empty(); }
    void setWaiting(bool w);
    int waiting() const { return _waiting; }

    // use this for autodrop times
    int speedUpTime( int delay ) const;

    void createDump( QPaintDevice * );

    void setAutoDropEnabled(bool a);
    bool autoDrop() const { return _autodrop; }

    void setGameNumber(int gmn);
    int gameNumber() const;

    void setGameId(int id);
    int gameId() const;

    void setActions(int actions);
    int actions() const;

    virtual QList<QAction*> configActions() const;
    CardDeck * cardDeck() const;

    void setSolverEnabled(bool a);
    void setSolver( Solver *s);
    Solver *solver() const;
    void startSolver() const;
    void unlockSolver() const;
    void finishSolver() const;

    void setNeededFutureMoves(int);
    int neededFutureMoves() const;

    bool demoActive() const;
    virtual bool isGameLost() const;
    virtual bool isGameWon() const;
    bool isInitialDeal() const;
    bool allowedToStartNewGame();
    int getMoves() const;

    QString save_it();
    void saveGame(QDomDocument &doc);
    void openGame(QDomDocument &doc);
    virtual void mapOldId(int id);
    void recordGameStatistics();

    virtual void restart() = 0;

signals:
    void undoPossible(bool poss);
    void redoPossible(bool poss);
    void hintPossible(bool poss);
    void demoPossible(bool poss);
    void newCardsPossible(bool poss);

    void gameWon(bool withhelp);
    void demoActive(bool en);
    void updateMoves(int moves);
    void gameLost();

    void gameSolverReset();
    void gameSolverStart();
    void gameSolverWon();
    void gameSolverLost();
    void gameSolverUnknown();

public slots:
    void relayoutScene();
    void relayoutPiles();

    void startNew(int gameNumber = -1);
    void hint();
    void showWonMessage();

    void undo();
    void redo();

protected:
    virtual void mouseDoubleClickEvent ( QGraphicsSceneMouseEvent * mouseEvent );
    virtual void mouseMoveEvent ( QGraphicsSceneMouseEvent * mouseEvent );
    virtual void mousePressEvent ( QGraphicsSceneMouseEvent * mouseEvent );
    virtual void mouseReleaseEvent ( QGraphicsSceneMouseEvent * mouseEvent );

    virtual bool cardClicked(Card *);
    virtual bool pileClicked(Pile *);
    virtual bool cardDblClicked(Card *);

    virtual void drawForeground ( QPainter * painter, const QRectF & rect );

    State *getState();
    void setState(State *);
    void eraseRedo();

    void setMarkedItems( QSet<MarkableItem*> s = QSet<MarkableItem*>() );

    Pile *findTarget(Card *c);
    Pile *targetPile();

    virtual void getHints();
    void getSolverHints();
    void newHint(MoveHint *mh);
    void clearHints();
    // it's not const because it changes the random seed
    virtual MoveHint *chooseHint();

    virtual void stopDemo();

    void won();
    void updateWonItem();

    // reimplement these to store and load game-specific information in the state structure
    virtual QString getGameState() { return QString(); }
    virtual void setGameState( const QString & ) {}

    // reimplement these to store and load game-specific options in the saved game file
    virtual QString getGameOptions() const { return QString(); }
    virtual void setGameOptions( const QString & ) {}

    virtual void newDemoMove(Card *m);
    void considerGameStarted();

    CardDeck * deck;
    PileList piles;
    QList<MoveHint*> hints;

protected slots:
    virtual void demo();
    void waitForDemo(Card *);
    void waitForWonAnim(Card *c);
    void toggleDemo();


    void slotSolverEnded();
    void slotSolverFinished();
    void slotAutoDrop();

    void takeState();
    virtual Card *newCards();
    virtual bool startAutoDrop();

    void addCardForDeal( Pile * pile, Card * card, bool faceUp, QPointF startPos );
    void startDealAnimation();

private:
    QSet<MarkableItem*> m_markedItems;

    bool moved;
    CardList movingCards;

    QPointF moving_start;
    bool _autodrop;
    bool _usesolver;
    int _waiting;
    int gamenumber;

    class DealerScenePrivate;
    DealerScenePrivate *d;

    QMap<Card*,QPointF> m_initDealPositions;

private slots:
    void waitForAutoDrop(Card *);
    void stopAndRestartSolver();
};

#endif
