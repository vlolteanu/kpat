/*
 * Copyright (C) 1995 Paul Olav Tvete <paul@troll.no>
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

#include "dealer.h"

#include "gamestate.h"
#include "renderer.h"
#include "speeds.h"
#include "version.h"
#include "view.h"
#include "patsolve/patsolve.h"

#include "KCardTheme"

#include <KConfigGroup>
#include <KDebug>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KTemporaryFile>
#include <KIO/NetAccess>

#include <QtCore/QCoreApplication>
#include <QtCore/QMutex>
#include <QtCore/QStack>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QGraphicsSceneMouseEvent>
#include <QtGui/QGraphicsView>
#include <QtGui/QPainter>
#include <QtGui/QStyleOptionGraphicsItem>
#include <QtXml/QDomDocument>

#include <cmath>

#define DEBUG_HINTS 0


class SolverThread : public QThread
{
public:
    SolverThread( Solver * solver )
      : m_solver( solver ),
        m_result( Solver::UnableToDetermineSolvability )
    {
    }

    virtual void run()
    {
        m_result = Solver::UnableToDetermineSolvability;
        m_result = m_solver->patsolve();
    }

    void abort()
    {
        {
            QMutexLocker lock( &m_solver->endMutex );
            m_solver->m_shouldEnd = true;
        }
        wait();
        m_result = Solver::SearchAborted; // perhaps he managed to finish, but we won't listen
    }

    Solver::ExitStatus result() const
    {
        return m_result;
    }

private:
    Solver * m_solver;
    Solver::ExitStatus m_result;
};


class DealerScene::DealerScenePrivate
{
public:
    bool _won;
    quint32 _id;
    bool _gameRecorded;
    bool gameStarted;
    bool wasJustSaved;
    QGraphicsPixmapItem *wonItem;
    bool gothelp;
    bool toldAboutLostGame;
    bool gameWasEverWinnable;
    // we need a flag to avoid telling someone the game is lost
    // just because the winning animation moved the cards away
    bool toldAboutWonGame;
    int myActions;
    Solver *m_solver;
    SolverThread *m_solver_thread;
    QList<MOVE> winMoves;
    int neededFutureMoves;
    QTimer *updateSolver;
    int loadedMoveCount;
    KCard *peekedCard;
    QTimer *demotimer;
    QTimer *dropTimer;
    bool demo_active;
    bool stop_demo_next;
    qreal dropSpeedFactor;

    bool dropInProgress;
    bool dealInProgress;

    bool takeStateQueued;
    bool hintQueued;
    bool demoQueued;

    QList<MoveHint*> hints;

    QStack<GameState*> undoStack;
    GameState * currentState;
    QStack<GameState*> redoStack;

    QList<PatPile*> patPiles;
    QSet<KCard*> cardsNotToDrop;
};

int DealerScene::moveCount() const
{
    return d->loadedMoveCount + d->undoStack.size();
}


void DealerScene::saveGame(QDomDocument &doc)
{
    QDomElement dealer = doc.createElement("dealer");
    doc.appendChild(dealer);
    dealer.setAttribute("id", gameId());
    dealer.setAttribute("options", getGameOptions());
    dealer.setAttribute("number", QString::number(gameNumber()));
    dealer.setAttribute("moves", moveCount());
    dealer.setAttribute("started", d->gameStarted);
    QString data = getGameState();
    if (!data.isEmpty())
        dealer.setAttribute("data", data);

    foreach( PatPile * p, patPiles() )
    {
        QDomElement pile = doc.createElement("pile");
        pile.setAttribute("index", p->index());
        pile.setAttribute("z", p->zValue());

        foreach(const KCard * c, p->cards() )
        {
            QDomElement card = doc.createElement("card");
            card.setAttribute("suit", getSuit( c ));
            card.setAttribute("value", getRank( c ));
            card.setAttribute("faceup", c->isFaceUp());
            pile.appendChild(card);
        }
        dealer.appendChild(pile);
     }

    d->wasJustSaved = true;
}

void DealerScene::openGame(QDomDocument &doc)
{
    resetInternals();

    QDomElement dealer = doc.documentElement();

    QString options = dealer.attribute("options");

    // Before KDE4.3, KPat didn't store game specific options in the save
    // file. This could cause crashes when loading a Spider game with a
    // different number of suits than the current setting. Similarly, in 
    // Klondike the number of cards drawn from the deck was forgotten, but
    // this never caused crashes. Fortunately, in Spider we can count the
    // number of suits ourselves. For Klondike, there is no way to recover
    // that information.
    if (gameId() == 17 && options.isEmpty())
    {
        QSet<int> suits;
        QDomNodeList cardElements = dealer.elementsByTagName("card");
        for (int i = 0; i < cardElements.count(); ++i)
            suits.insert(cardElements.item(i).toElement().attribute("suit").toInt());
        options = QString::number(suits.count());
    }

    setGameOptions(options);
    gamenumber = dealer.attribute("number").toInt();
    d->loadedMoveCount = dealer.attribute("moves").toInt();
    d->gameStarted = bool(dealer.attribute("started").toInt());

    QDomNodeList pileNodes = dealer.elementsByTagName("pile");

    QMultiHash<quint32,KCard*> cards;
    foreach ( KCard * c, deck()->cards() )
        cards.insert( c->id(), c );

    foreach (PatPile *p, patPiles())
    {
        p->clear();
        for (int i = 0; i < pileNodes.count(); ++i)
        {
            QDomElement pile = pileNodes.item(i).toElement();
            if (pile.attribute("index").toInt() == p->index())
            {
                QDomNodeList cardNodes = pile.elementsByTagName("card");
                for (int j = 0; j < cardNodes.count(); ++j)
                {
                    QDomElement card = cardNodes.item(j).toElement();
                    int s = card.attribute("suit").toInt();
                    int r = card.attribute("value").toInt();

                    KCard * c = cards.take( ( s << 4 ) + r );
                    Q_ASSERT( c );

                    c->setFaceUp(card.attribute("faceup").toInt());
                    p->add(c);
                }
                break;
            }
        }
        p->layoutCards( 0 );
    }
    setGameState( dealer.attribute("data") );

    emit updateMoves( moveCount());

    takeState();
}

// ================================================================
//                         class DealerScene


DealerScene::DealerScene()
  : _autodrop(true),
    gamenumber(0)
{
    setItemIndexMethod(QGraphicsScene::NoIndex);

    d = new DealerScenePrivate();
    d->demo_active = false;
    d->stop_demo_next = false;
    d->_won = false;
    d->_gameRecorded = false;
    d->gameStarted = false;
    d->wasJustSaved = false;
    d->gothelp = false;
    d->myActions = 0;
    d->m_solver = 0;
    d->m_solver_thread = 0;
    d->neededFutureMoves = 1;
    d->toldAboutLostGame = false;
    d->toldAboutWonGame = false;
    d->updateSolver = new QTimer(this);
    d->updateSolver->setInterval( 250 );
    d->updateSolver->setSingleShot( true );
    d->loadedMoveCount = 0;
    d->peekedCard = 0;
    connect( d->updateSolver, SIGNAL(timeout()), SLOT(stopAndRestartSolver()) );

    d->demotimer = new QTimer(this);
    connect( d->demotimer, SIGNAL(timeout()), SLOT(demo()) );
    d->dropSpeedFactor = 1;
    d->dropInProgress = false;

    d->dealInProgress = false;

    d->takeStateQueued = false;
    d->hintQueued = false;
    d->demoQueued = false;
    connect( this, SIGNAL(cardAnimationDone()), this, SLOT(animationDone()) );

    d->dropTimer = new QTimer( this );
    connect( d->dropTimer, SIGNAL(timeout()), this, SLOT(drop()) );
    d->dropTimer->setSingleShot( true );

    d->wonItem  = new QGraphicsPixmapItem( 0, this );
    d->wonItem->setZValue( 2000 );
    d->wonItem->hide();

    d->currentState = 0;

    _usesolver = true;

    connect( this, SIGNAL(cardDoubleClicked(KCard*)), this, SLOT(tryAutomaticMove(KCard*)) );
    // Make rightClick == doubleClick. See bug #151921
    connect( this, SIGNAL(cardRightClicked(KCard*)), this, SLOT(tryAutomaticMove(KCard*)) );
}

DealerScene::~DealerScene()
{
    clearHighlightedItems();

    clearHints();

    disconnect();
    if ( d->m_solver_thread )
        d->m_solver_thread->abort();
    delete d->m_solver_thread;
    d->m_solver_thread = 0;
    delete d->m_solver;
    d->m_solver = 0;
    qDeleteAll( d->undoStack );
    delete d->currentState;
    qDeleteAll( d->redoStack );
    delete d->wonItem;

    delete d;
}


void DealerScene::addPile( KCardPile * pile )
{
    d->patPiles.clear();

    KCardScene::addPile( pile );
}


void DealerScene::removePile( KCardPile * pile )
{
    d->patPiles.clear();

    KCardScene::removePile( pile );
}


QList<PatPile*> DealerScene::patPiles() const
{
    if ( d->patPiles.isEmpty() )
    {
        foreach( KCardPile * p, piles() )
        {
            PatPile * pp = dynamic_cast<PatPile*>( p );
            if ( pp )
                d->patPiles << pp;
        }
    }

    return d->patPiles;
}


void DealerScene::moveCardsToPile( QList<KCard*> cards, KCardPile * pile, int duration )
{
    PatPile * newPile = dynamic_cast<PatPile*>( pile );
    foreach ( KCard * c, cards )
    {
        PatPile * oldPile = dynamic_cast<PatPile*>( c->pile() );
        if ( oldPile && oldPile->isFoundation() && !newPile->isFoundation() )
            d->cardsNotToDrop.insert( c );
        else
            d->cardsNotToDrop.remove( c );
    }

    KCardScene::moveCardsToPile( cards, pile, duration );

    if ( !d->dropInProgress && !d->dealInProgress )
    {
        d->gameStarted = true;
        takeState();
    }
}


QList<MoveHint*> DealerScene::hints() const
{
    return d->hints;
}

void DealerScene::hint()
{
    if ( isCardAnimationRunning() )
    {
        d->hintQueued = true;
        return;
    }

    d->dropTimer->stop();

    if ( !highlightedItems().isEmpty() )
    {
        clearHighlightedItems();
        return;
    }

    clearHints();

    if ( d->winMoves.count() )
    {
        MOVE m = d->winMoves.first();
#if DEBUG_HINTS
        if ( m.totype == O_Type )
            fprintf( stderr, "move from %d out (at %d) Prio: %d\n", m.from,
                     m.turn_index, m.pri );
        else
            fprintf( stderr, "move from %d to %d (%d) Prio: %d\n", m.from, m.to,
                     m.turn_index, m.pri );
#endif

        MoveHint *mh = solver()->translateMove( m );
        if ( mh ) {
            newHint( mh );
            kDebug() << "win move" << mh->pile()->objectName() << mh->card()->objectName() << mh->priority();
        }
        getHints();
    } else {
        getHints();
    }

    QList<QGraphicsItem*> toHighlight;
    foreach ( MoveHint * h, d->hints )
        toHighlight << h->card();
    setHighlightedItems( toHighlight );

    clearHints();
}

void DealerScene::getSolverHints()
{
    if ( d->m_solver_thread && d->m_solver_thread->isRunning() )
        d->m_solver_thread->abort();

    solver()->translate_layout();
    solver()->patsolve( 1 );

    QList<MOVE> moves = solver()->firstMoves;
    QListIterator<MOVE> mit( moves );

    while ( mit.hasNext() )
    {
        MOVE m = mit.next();
#if DEBUG_HINTS
        if ( m.totype == O_Type )
            fprintf( stderr, "   move from %d out (at %d) Prio: %d\n", m.from,
                     m.turn_index, m.pri );
        else
            fprintf( stderr, "   move from %d to %d (%d) Prio: %d\n", m.from, m.to,
                     m.turn_index, m.pri );
#endif

        MoveHint *mh = solver()->translateMove( m );
        if ( mh )
            newHint( mh );
    }
}

void DealerScene::getHints()
{
    if ( solver() )
    {
        getSolverHints();
        return;
    }

    foreach (PatPile * store, patPiles())
    {
        if (store->isFoundation() || store->isEmpty())
            continue;

        QList<KCard*> cards = store->cards();
        while (cards.count() && !cards.first()->isFaceUp())
            cards.erase(cards.begin());

        QList<KCard*>::Iterator iti = cards.begin();
        while (iti != cards.end())
        {
            if (allowedToRemove(store, (*iti)))
            {
                foreach (PatPile * dest, patPiles())
                {
                    int cardIndex = store->indexOf(*iti);
                    if (cardIndex == 0 && dest->isEmpty() && !dest->isFoundation())
                        continue;

                    if (!checkAdd(dest, dest->cards(), cards))
                        continue;

                    if (dest->isFoundation())
                    {
                        newHint(new MoveHint(*iti, dest, 127));
                    }
                    else
                    {
                        QList<KCard*> cardsBelow = cards.mid(0, cardIndex);
 
                        // if it could be here as well, then it's no use
                        if ((cardsBelow.isEmpty() && !dest->isEmpty()) || !checkAdd(store, cardsBelow, cards))
                        {
                            newHint(new MoveHint(*iti, dest, 0));
                        }
                        else if (checkPrefering(dest, dest->cards(), cards)
                                 && !checkPrefering(store, cardsBelow, cards))
                        { // if checkPrefers says so, we add it nonetheless
                            newHint(new MoveHint(*iti, dest, 10));
                        }
                    }
                }
            }
            cards.erase(iti);
            iti = cards.begin();
        }
    }
}

void DealerScene::clearHints()
{
    qDeleteAll( d->hints );
    d->hints.clear();
}

void DealerScene::newHint(MoveHint *mh)
{
    d->hints.append(mh);
}

void DealerScene::startNew(int gameNumber)
{
    if (gameNumber != -1)
        gamenumber = qBound( 1, gameNumber, INT_MAX );

    // Only record the statistics and reset gameStarted if  we're starting a
    // new game number or we're restarting a game we've already won.
    if (gameNumber != -1 || d->_won)
    {
        recordGameStatistics();
        d->_gameRecorded = false;
        d->gameStarted = false;
    }

    if ( isCardAnimationRunning() )
    {
        QTimer::singleShot( 100, this, SLOT( startNew() ) );
        return;
    }

    resetInternals();
    d->loadedMoveCount = 0;
    d->wasJustSaved = false;

    emit updateMoves( 0 );

    d->dealInProgress = true;
    foreach( KCardPile * p, piles() )
        p->clear();
    restart();
    d->dealInProgress = false;

    takeState();
    update();
}

void DealerScene::resetInternals()
{
    stopDemo();
    clearHighlightedItems();

    d->_won = false;
    d->wonItem->hide();

    qDeleteAll( d->undoStack );
    d->undoStack.clear();
    delete d->currentState;
    d->currentState = 0;
    qDeleteAll( d->redoStack );
    d->redoStack.clear();

    d->gameWasEverWinnable = false;
    d->toldAboutLostGame = false;
    d->toldAboutWonGame = false;

    d->gothelp = false;

    d->dealInProgress = false;

    d->dropInProgress = false;
    d->dropSpeedFactor = 1;
    d->cardsNotToDrop.clear();

    foreach (KCard * c, deck()->cards())
    {
        c->disconnect( this );
        c->stopAnimation();
    }

    emit solverStateChanged( QString() );
    emit hintPossible( true );
    emit demoPossible( true );
}

QPointF posAlongRect( qreal distOnRect, const QRectF & rect )
{
    if ( distOnRect < rect.width() )
        return rect.topLeft() + QPointF( distOnRect, 0 );
    distOnRect -= rect.width();
    if ( distOnRect < rect.height() )
        return rect.topRight() + QPointF( 0, distOnRect );
    distOnRect -= rect.height();
    if ( distOnRect < rect.width() )
        return rect.bottomRight() + QPointF( -distOnRect, 0 );
    distOnRect -= rect.width();
    return rect.bottomLeft() + QPointF( 0, -distOnRect );
}

void DealerScene::won()
{
    if (d->_won)
        return;

    d->_won = true;
    d->toldAboutWonGame = true;

    stopDemo();
    recordGameStatistics();

    emit demoPossible( false );
    emit hintPossible( false );
    emit newCardsPossible( false );
    emit undoPossible( false );

    qreal speed = sqrt( width() * width() + height() * height() ) / ( DURATION_WON );

    QRectF justOffScreen = sceneRect().adjusted( -deck()->cardWidth(), -deck()->cardHeight(), 0, 0 );
    qreal spacing = 2 * ( justOffScreen.width() + justOffScreen.height() ) / deck()->cards().size();
    qreal distOnRect = 0;

    foreach ( KCard *c, deck()->cards() )
    {
        distOnRect += spacing;
        QPointF pos2 = posAlongRect( distOnRect, justOffScreen );
        QPointF delta = c->pos() - pos2;
        qreal dist = sqrt( delta.x() * delta.x() + delta.y() * delta.y() );

        c->setFaceUp( true );
        c->animate( pos2, c->zValue(), 1, 0, true, false, dist / speed );
    }

    connect(deck(), SIGNAL(cardAnimationDone()), this, SLOT(showWonMessage()));
}

void DealerScene::showWonMessage()
{
    disconnect(deck(), SIGNAL(cardAnimationDone()), this, SLOT(showWonMessage()));

    // It shouldn't be necessary to stop the demo yet again here, but we
    // get crashes if we don't. Will have to look into this further.
    stopDemo();

    // Hide all cards to prevent them from showing up accidentally if the 
    // window is resized.
    foreach ( KCard * c, deck()->cards() )
        c->hide();

    updateWonItem();
    d->wonItem->show();
}

void DealerScene::updateWonItem( bool force )
{
    const qreal aspectRatio = Renderer::self()->aspectRatioOfElement("message_frame");
    int boxWidth;
    int boxHeight;
    if (width() < aspectRatio * height())
    {
        boxWidth = width() * 0.7;
        boxHeight = boxWidth / aspectRatio;
    }
    else
    {
        boxHeight = height() * 0.7;
        boxWidth = boxHeight * aspectRatio;
    }

    // Only regenerate the pixmap if the new one is a significantly different size.
    if ( force || qAbs( d->wonItem->boundingRect().width() - boxWidth ) > 20 )
    {
        QRect contentsRect = QRect( 0, 0, boxWidth, boxHeight );
        QPixmap pix = Renderer::self()->renderElement( "message_frame", contentsRect.size() );

        QString text;
        if ( d->gothelp )
            text = i18n( "Congratulations! We have won." );
        else
            text = i18n( "Congratulations! You have won." );

        QFont font;
        int fontsize = 36;
        font.setPointSize( fontsize );
        int twidth = QFontMetrics( font ).width( text );
        while ( twidth > boxWidth * 0.9 && fontsize > 5 )
        {
            fontsize--;
            font.setPointSize( fontsize );
            twidth = QFontMetrics( font ).width ( text );
        }

        QPainter p( &pix );
        p.setFont( font );
        p.setPen( Renderer::self()->colorOfElement("message_text_color") );
        p.drawText( contentsRect, Qt::AlignCenter, text );
        p.end();

        d->wonItem->setPixmap( pix );
    }

    d->wonItem->setPos( QPointF( ( width() - d->wonItem->sceneBoundingRect().width() ) / 2,
                                 ( height() - d->wonItem->sceneBoundingRect().height() ) / 2 ) + sceneRect().topLeft() );
}


bool DealerScene::allowedToAdd( const KCardPile * pile, const QList<KCard*> & cards ) const
{
    const PatPile * p = dynamic_cast<const PatPile*>( pile );
    return p && checkAdd( p, p->cards(), cards );
}


bool DealerScene::allowedToRemove( const KCardPile * pile, const KCard * card ) const
{
    const PatPile * p = dynamic_cast<const PatPile*>( pile );
    if ( !p )
        return false;

    QList<KCard*> cards = p->topCardsDownTo( card );
    return !cards.isEmpty() && checkRemove( p, cards );
}


bool DealerScene::checkAdd( const PatPile * pile, const QList<KCard*> & oldCards, const QList<KCard*> & newCards ) const
{
    Q_UNUSED( pile )
    Q_UNUSED( oldCards )
    Q_UNUSED( newCards )
    return false;
}


bool DealerScene::checkRemove(const PatPile * pile, const QList<KCard*> & cards) const
{
    Q_UNUSED( pile )
    Q_UNUSED( cards )
    return false;
}


bool DealerScene::checkPrefering( const PatPile * pile, const QList<KCard*> & oldCards, const QList<KCard*> & newCards ) const
{
    Q_UNUSED( pile )
    Q_UNUSED( oldCards )
    Q_UNUSED( newCards )
    return false;
}


void DealerScene::mousePressEvent( QGraphicsSceneMouseEvent * e )
{
    clearHighlightedItems();

    stopDemo();

    KCard * card = qgraphicsitem_cast<KCard*>( itemAt( e->scenePos() ) );

    if ( d->peekedCard )
    {
        e->accept();
    }
    else if ( e->button() == Qt::RightButton
              && card
              && card->pile()
              && card != card->pile()->top()
              && cardsBeingDragged().isEmpty()
              && !isCardAnimationRunning() )
    {
        e->accept();
        d->peekedCard = card;
        QPointF pos2( card->x() + deck()->cardWidth() / 3.0, card->y() - deck()->cardHeight() / 4.0 );
        card->setZValue( card->zValue() + 0.1 );
        card->animate( pos2, card->zValue(), 1.1, 20, card->isFaceUp(), false, DURATION_FANCYSHOW );
    }
    else
    {
        KCardScene::mousePressEvent( e );
    }
}

void DealerScene::mouseReleaseEvent( QGraphicsSceneMouseEvent * e )
{
    clearHighlightedItems();

    if ( e->button() == Qt::RightButton && d->peekedCard && d->peekedCard->pile() )
    {
        e->accept();
        d->peekedCard->pile()->layoutCards( DURATION_FANCYSHOW );
        d->peekedCard = 0;
    }
    else
    {
        KCardScene::mouseReleaseEvent( e );
    }
}

void DealerScene::mouseDoubleClickEvent( QGraphicsSceneMouseEvent * e )
{
    clearHighlightedItems();

    if ( d->demo_active )
    {
        e->accept();
        stopDemo();
    }
    else
    {
        KCardScene::mouseDoubleClickEvent( e );
    }
}

bool DealerScene::tryAutomaticMove( KCard * c )
{
    if (c->isAnimated())
        return false;

    if (c == c->pile()->top() && c->isFaceUp() && allowedToRemove(c->pile(), c)) {
        KCardPile *tgt = findTarget(c);
        if (tgt) {
            moveCardToPile( c , tgt, DURATION_MOVE );
            return true;
        }
    }
    return false;
}


void DealerScene::undo()
{
    undoOrRedo( true );
}


void DealerScene::redo()
{
    undoOrRedo( false );
}


void DealerScene::undoOrRedo( bool undo )
{
    clearHighlightedItems();
    stopDemo();

    if ( isCardAnimationRunning() )
        return;

    QStack<GameState*> & from = undo ? d->undoStack : d->redoStack;
    QStack<GameState*> & to = undo ? d->redoStack : d->undoStack;

    if ( !from.isEmpty() && d->currentState )
    {
        to.push( d->currentState );
        d->currentState = from.pop();
        setState( d->currentState );

        emit updateMoves( moveCount() );
        emit undoPossible( !d->undoStack.isEmpty() );
        emit redoPossible( !d->redoStack.isEmpty() );

        if ( d->toldAboutLostGame ) // everything's possible again
        {
            hintPossible( true );
            demoPossible( true );
            d->toldAboutLostGame = false;
            d->toldAboutWonGame = false;
        }

        if ( d->currentState->solvability == Solver::SolutionExists )
        {
            d->winMoves = d->currentState->winningMoves;
            emit solverStateChanged( i18n("Solver: This game is winnable.") );
        }
        else if ( d->currentState->solvability == Solver::NoSolutionExists )
        {
            d->winMoves.clear();
            if ( d->gameWasEverWinnable )
                emit solverStateChanged( i18n("Solver: This game is no longer winnable.") );
            else
                emit solverStateChanged( i18n("Solver: This game cannot be won.") );
        }
        else if ( d->currentState->solvability == Solver::UnableToDetermineSolvability )
        {
            d->winMoves.clear();
            emit solverStateChanged( i18n("Solver: Unable to determine if this game is winnable.") );
        }
        else
        {
            emit solverStateChanged( QString() );
            if ( d->m_solver )
                startSolver();
        }
    }
}


void DealerScene::takeState()
{
    if ( !isDemoActive() )
    {
        d->winMoves.clear();
        clearHints();
    }

    if ( isCardAnimationRunning() )
    {
        d->takeStateQueued = true;
        return;
    }


    GameState * newState = getState();

    if ( d->currentState && *newState == *(d->currentState) )
    {
        delete newState;
        return;
    }

    if ( d->currentState )
    {
        d->undoStack.push( d->currentState );
        qDeleteAll( d->redoStack );
        d->redoStack.clear();
    }
    d->currentState = newState;

    emit redoPossible( false );
    emit undoPossible( !d->undoStack.isEmpty() );
    emit updateMoves( moveCount() );

    d->wasJustSaved = false;
    if ( isGameWon() )
    {
        won();
        return;
    }

    if ( !d->toldAboutWonGame && !d->toldAboutLostGame && isGameLost() )
    {
        emit hintPossible( false );
        emit demoPossible( false );
        emit solverStateChanged( i18n( "Solver: This game is lost." ) );
        d->toldAboutLostGame = true;
        stopDemo();
        return;
    }

    if ( !isDemoActive() && !isCardAnimationRunning() )
    {
        if ( d->m_solver )
            startSolver();

        d->dropTimer->start( speedUpTime( TIME_BETWEEN_MOVES ) );
    }
}


GameState *DealerScene::getState()
{
    QSet<CardState> cardStates;
    cardStates.reserve( deck()->cards().size() );
    foreach (PatPile * p, patPiles())
    {
        foreach (KCard * c, p->cards())
        {
            CardState s;
            s.card = c;
            s.pile = c->pile();
            if (!s.pile) {
                kDebug() << c->objectName() << "has no valid parent.";
                Q_ASSERT(false);
                continue;
            }
            s.cardIndex = s.pile->indexOf( c );
            s.faceUp = c->isFaceUp();
            s.takenDown = d->cardsNotToDrop.contains( c );
            cardStates << s;
        }
    }

    return new GameState( cardStates, getGameState() );
}


void DealerScene::setState( GameState * state )
{
    d->cardsNotToDrop.clear();

    QSet<KCard*> cardsFromFoundations;
    foreach (PatPile *p, patPiles())
    {
        if ( p->isFoundation() )
            foreach (KCard *c, p->cards())
                cardsFromFoundations.insert( c );
        p->clear();
    }

    QMap<KCard*,int> cardIndices;
    foreach ( const CardState & s, state->cards )
    {
        KCard *c = s.card;
        c->setFaceUp( s.faceUp );
        s.pile->add( c );
        cardIndices.insert( c, s.cardIndex );

        PatPile * p = dynamic_cast<PatPile*>(c->pile());
        if ( s.takenDown || (cardsFromFoundations.contains( c ) && !(p && p->isFoundation())) )
            d->cardsNotToDrop.insert( c );
    }

    foreach( PatPile * p, patPiles() )
    {
        foreach ( KCard * c, p->cards() )
            p->swapCards( p->indexOf( c ), cardIndices.value( c ) );

        p->layoutCards(0);
    }

    // restore game-specific information
    setGameState( state->gameData );
}

PatPile *DealerScene::findTarget(KCard *c)
{
    if (!c)
        return 0;

    foreach (PatPile *p, patPiles())
    {
        if (!p->isFoundation())
            continue;
        if (allowedToAdd(p, QList<KCard*>() << c))
            return p;
    }
    return 0;
}


void DealerScene::setAutoDropEnabled(bool a)
{
    _autodrop = a;
    d->dropTimer->start( TIME_BETWEEN_MOVES );
}

void DealerScene::startManualDrop()
{
    d->dropInProgress = true;
    d->dropTimer->start( TIME_BETWEEN_MOVES );
}

void DealerScene::setSolverEnabled(bool a)
{
    _usesolver = a;
}


bool DealerScene::drop()
{
    if (!autoDropEnabled() && !d->dropInProgress)
        return false;

    if (!cardsBeingDragged().isEmpty() || isCardAnimationRunning() || d->undoStack.isEmpty() ) {
        d->dropTimer->start( speedUpTime( TIME_BETWEEN_MOVES ) );
        return true;
    }

    d->dropInProgress = true;

    clearHighlightedItems();
    clearHints();
    getHints();

    foreach ( const MoveHint *mh, d->hints )
    {
        if ( mh->pile() && mh->pile()->isFoundation() && mh->priority() > 120 && !d->cardsNotToDrop.contains( mh->card() ) )
        {
            KCard *t = mh->card();
            QList<KCard*> cards = mh->card()->pile()->cards();
            while ( !cards.isEmpty() && cards.first() != t )
                cards.removeFirst();

            QMap<KCard*,QPointF> oldPositions;
            foreach ( KCard *c, cards )
                oldPositions.insert( c, c->pos() );

            moveCardsToPile( cards, mh->pile(), DURATION_MOVE );

            bool animationStarted = false;
            int count = 0;
            foreach ( KCard *c, cards )
            {
                c->completeAnimation();
                QPointF destPos = c->pos();
                c->setPos( oldPositions.value( c ) );

                int duration = speedUpTime( DURATION_AUTODROP + count++ * DURATION_AUTODROP / 10 );
                c->animate( destPos, c->zValue(), 1, 0, c->isFaceUp(), true, duration );

                animationStarted |= c->isAnimated();
            }

            d->dropSpeedFactor *= AUTODROP_SPEEDUP_FACTOR;

            if ( animationStarted )
                connect( deck(), SIGNAL(cardAnimationDone()), this, SLOT(waitForAutoDrop()) );

            return true;
        }
    }
    clearHints();
    d->dropSpeedFactor = 1;
    d->dropInProgress = false;

    return false;
}

int DealerScene::speedUpTime( int delay ) const
{
    if ( delay < DURATION_AUTODROP_MINIMUM )
        return delay;
    else
        return qMax<int>( delay * d->dropSpeedFactor, DURATION_AUTODROP_MINIMUM );
}

void DealerScene::stopAndRestartSolver()
{
    if ( d->toldAboutLostGame || d->toldAboutWonGame ) // who cares?
        return;

    if ( d->m_solver_thread && d->m_solver_thread->isRunning() )
    {
        d->m_solver_thread->abort();
    }

    if ( isCardAnimationRunning() )
    {
        startSolver();
        return;
    }

    slotSolverEnded();
}

void DealerScene::slotSolverEnded()
{
    if ( d->m_solver_thread && d->m_solver_thread->isRunning() )
        return;

    d->m_solver->translate_layout();
    d->winMoves.clear();
    emit solverStateChanged( i18n("Solver: Calculating...") );
    if ( !d->m_solver_thread ) {
        d->m_solver_thread = new SolverThread( d->m_solver );
        connect( d->m_solver_thread, SIGNAL(finished()), this, SLOT(slotSolverFinished()));
    }
    d->m_solver_thread->start(_usesolver ? QThread::IdlePriority : QThread::NormalPriority );
}

void DealerScene::slotSolverFinished()
{
    if ( !d->m_solver_thread || d->m_solver_thread->isRunning() )
        return;

    Solver::ExitStatus ret = d->m_solver_thread->result();

    switch ( ret )
    {
    case Solver::SolutionExists:
        d->winMoves = d->m_solver->winMoves;
        d->gameWasEverWinnable = true;
        emit solverStateChanged( i18n("Solver: This game is winnable.") );
        break;
    case Solver::NoSolutionExists:
        if ( d->gameWasEverWinnable )
            emit solverStateChanged( i18n("Solver: This game is no longer winnable.") );
        else
            emit solverStateChanged( i18n("Solver: This game cannot be won.") );
        break;
    case Solver::UnableToDetermineSolvability:
        emit solverStateChanged( i18n("Solver: Unable to determine if this game is winnable.") );
        break;
    case Solver::SearchAborted:
        startSolver();
        break;
    case Solver::MemoryLimitReached:
        break;
    }

    d->currentState->solvability = ret;
    if ( ret == Solver::SolutionExists )
        d->currentState->winningMoves = d->winMoves;
}


void DealerScene::waitForAutoDrop()
{
    disconnect( deck(), SIGNAL(cardAnimationDone()), this, SLOT(waitForAutoDrop()) );

    takeState();
}

int DealerScene::gameNumber() const
{
    return gamenumber;
}

void DealerScene::stopDemo()
{
    if ( isCardAnimationRunning()) {
        d->stop_demo_next = true;
        return;
    } else d->stop_demo_next = false;

    foreach ( KCard * c, deck()->cards() )
    {
        c->completeAnimation();
    }

    d->demotimer->stop();
    d->demo_active = false;
    emit demoActive( false );
}

bool DealerScene::isDemoActive() const
{
    return d->demo_active;
}

void DealerScene::toggleDemo()
{
    if ( !isDemoActive() )
        demo();
    else
        stopDemo();
}

MoveHint *DealerScene::chooseHint()
{
    if ( d->winMoves.count() )
    {
        MOVE m = d->winMoves.takeFirst();
#if DEBUG_HINTS
        if ( m.totype == O_Type )
            fprintf( stderr, "move from %d out (at %d) Prio: %d\n", m.from,
                     m.turn_index, m.pri );
        else
            fprintf( stderr, "move from %d to %d (%d) Prio: %d\n", m.from, m.to,
                     m.turn_index, m.pri );
#endif
        MoveHint *mh = solver()->translateMove( m );

        if ( mh )
            d->hints.append( mh );
        return mh;
    }

    if (d->hints.isEmpty())
        return 0;

    qSort( d->hints.begin(), d->hints.end() );
    return d->hints[0];
}


void DealerScene::animationDone()
{
    if ( d->takeStateQueued )
    {
        d->takeStateQueued = false;
        takeState();
    }

    if ( d->hintQueued )
    {
        d->hintQueued = false;
        hint();
    }

    if ( d->demoQueued )
    {
        d->demoQueued = false;
        demo();
    }
}


void DealerScene::demo()
{
    if ( isCardAnimationRunning() )
    {
        d->demoQueued = true;
        return;
    }

    if (d->stop_demo_next) {
        stopDemo();
        return;
    }
    d->stop_demo_next = false;
    d->demo_active = true;
    d->gothelp = true;
    d->gameStarted = true;
    clearHighlightedItems();
    clearHints();
    getHints();
    d->demotimer->stop();

    MoveHint *mh = chooseHint();
    if (mh) {
        kDebug(mh->pile()->top()) << "Moving" << mh->card()->objectName()
                                  << "from the" << mh->card()->pile()->objectName()
                                  << "pile to the" << mh->pile()->objectName()
                                  << "pile, putting it on top of" << mh->pile()->top()->objectName();
        kDebug(!mh->pile()->top()) << "Moving" << mh->card()->objectName()
                                   << "from the" << mh->card()->pile()->objectName()
                                   << "pile to the" << mh->pile()->objectName()
                                   << "pile, which is empty";
        Q_ASSERT(mh->card()->pile() == 0
                 || allowedToRemove(mh->card()->pile(), mh->card()));

        QList<KCard*> empty;
        QList<KCard*> cards = mh->card()->pile()->cards();
        bool after = false;
        for (QList<KCard*>::Iterator it = cards.begin(); it != cards.end(); ++it) {
            if (*it == mh->card())
                after = true;
            if (after)
                empty.append(*it);
        }

        Q_ASSERT(!empty.isEmpty());

        QMap<KCard*,QPointF> oldPositions;

        foreach (KCard *c, empty) {
            c->completeAnimation();
            c->setFaceUp(true);
            oldPositions.insert(c, c->pos());
        }

        Q_ASSERT(mh->card());
        Q_ASSERT(mh->card()->pile());
        Q_ASSERT(mh->pile());
        Q_ASSERT(mh->card()->pile() != mh->pile());
        Q_ASSERT(mh->pile()->isFoundation() || allowedToAdd(mh->pile(), empty));

        moveCardsToPile( empty, mh->pile(), DURATION_MOVE );

        foreach (KCard *c, empty) {
            c->completeAnimation();
            QPointF destPos = c->pos();
            c->setPos(oldPositions.value(c));
            c->animate(destPos, c->zValue(), 1, 0, c->isFaceUp(), true, DURATION_DEMO);
        }

        newDemoMove(mh->card());

    } else {
        KCard *t = newCards();
        if (t) {
            newDemoMove(t);
        } else if (isGameWon()) {
            won();
            return;
        } else {
            stopDemo();
            slotSolverEnded();
            return;
        }
    }

    emit demoActive( true );
    takeState();
}

KCard *DealerScene::newCards()
{
    return 0;
}

void DealerScene::newDemoMove(KCard *m)
{
    if ( m->isAnimated() )
        connect(m, SIGNAL(animationStopped(KCard*)), SLOT(waitForDemo(KCard*)));
    else
        waitForDemo( 0 );
}

void DealerScene::waitForDemo(KCard *t)
{
    if ( t )
    {
        t->disconnect(this, SLOT(waitForDemo(KCard*)) );
        takeState();
    }
    d->demotimer->start(250);
}

void DealerScene::setSolver( Solver *s) {
    delete d->m_solver;
    delete d->m_solver_thread;
    d->m_solver = s;
    d->m_solver_thread = 0;
}

bool DealerScene::isGameWon() const
{
    foreach (PatPile *p, patPiles())
    {
        if (!p->isFoundation() && !p->isEmpty())
            return false;
    }
    return true;
}

void DealerScene::startSolver() const
{
    if(_usesolver)
        d->updateSolver->start();
}


bool DealerScene::isGameLost() const
{
    if ( solver() )
    {
        if ( d->m_solver_thread && d->m_solver_thread->isRunning() )
            d->m_solver_thread->abort();

        solver()->translate_layout();
        return solver()->patsolve( neededFutureMoves() ) == Solver::NoSolutionExists;
    }
    return false;
}

void DealerScene::recordGameStatistics()
{
    // Don't record the game if it was never started, if it is unchanged since
    // it was last saved (allowing the user to close KPat after saving without
    // it recording a loss) or if it has already been recorded.//         takeState(); // copying it again
    if ( d->gameStarted && !d->wasJustSaved && !d->_gameRecorded )
    {
        int id = oldId();

        QString totalPlayedKey = QString("total%1").arg( id );
        QString wonKey = QString("won%1").arg( id );
        QString winStreakKey = QString("winstreak%1").arg( id );
        QString maxWinStreakKey = QString("maxwinstreak%1").arg( id );
        QString loseStreakKey = QString("loosestreak%1").arg( id );
        QString maxLoseStreakKey = QString("maxloosestreak%1").arg( id );

        KConfigGroup config(KGlobal::config(), scores_group);

        int totalPlayed = config.readEntry( totalPlayedKey, 0 );
        int won = config.readEntry( wonKey, 0 );
        int winStreak = config.readEntry( winStreakKey, 0 );
        int maxWinStreak = config.readEntry( maxWinStreakKey, 0 );
        int loseStreak = config.readEntry( loseStreakKey, 0 );
        int maxLoseStreak = config.readEntry( maxLoseStreakKey, 0 );

        ++totalPlayed;

        if ( d->_won )
        {
            ++won;
            ++winStreak;
            maxWinStreak = qMax( winStreak, maxWinStreak );
            loseStreak = 0;
        }
        else
        {
            ++loseStreak;
            maxLoseStreak = qMax( loseStreak, maxLoseStreak );
            winStreak = 0;
        }

        config.writeEntry( totalPlayedKey, totalPlayed );
        config.writeEntry( wonKey, won );
        config.writeEntry( winStreakKey, winStreak );
        config.writeEntry( maxWinStreakKey, maxWinStreak );
        config.writeEntry( loseStreakKey, loseStreak );
        config.writeEntry( maxLoseStreakKey, maxLoseStreak );

        d->_gameRecorded = true;
    }
}

void DealerScene::relayoutScene()
{
    KCardScene::relayoutScene();

    if ( d->wonItem->isVisible() )
        updateWonItem();
}

void DealerScene::setGameId(int id) { d->_id = id; }
int DealerScene::gameId() const { return d->_id; }

void DealerScene::setActions(int actions) { d->myActions = actions; }
int DealerScene::actions() const { return d->myActions; }

QList<QAction*> DealerScene::configActions() const { return QList<QAction*>(); }

Solver *DealerScene::solver() const { return d->m_solver; }

int DealerScene::neededFutureMoves() const { return d->neededFutureMoves; }
void DealerScene::setNeededFutureMoves( int i ) { d->neededFutureMoves = i; }


QString DealerScene::save_it()
{
    // If the game has been won, there's no current state to save.
    if ( d->_won )
        return QString();

    KTemporaryFile file;
    file.setAutoRemove(false);
    file.open();
    QDomDocument doc("kpat");
    saveGame(doc);
    QTextStream stream (&file);
    stream << doc.toString();
    stream.flush();
    file.flush();
    // .close will make fileName() return ""
    QString filename = file.fileName();
    file.close();
    return filename;
}

QImage DealerScene::createDump() const
{
    const QSize previewSize( 480, 320 );

    foreach ( KCard * c, deck()->cards() )
        c->completeAnimation();

    QMultiMap<qreal,QGraphicsItem*> itemsByZ;
    foreach ( QGraphicsItem * item, items() )
    {
        Q_ASSERT( item->zValue() >= 0 );
        itemsByZ.insert( item->zValue(), item );
    }

    QImage img( contentArea().size().toSize(), QImage::Format_ARGB32 );
    img.fill( Qt::transparent );
    QPainter p( &img );

    foreach ( QGraphicsItem * item, itemsByZ )
    {
        if ( item->isVisible() )
        {
            p.save();
            p.setTransform( item->deviceTransform( p.worldTransform() ), false );
            item->paint( &p, 0 );
            p.restore();
        }
    }

    p.end();

    img = img.scaled( previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation );

    QImage img2( previewSize, QImage::Format_ARGB32 );
    img2.fill( Qt::transparent );
    QPainter p2( &img2 );
    p2.drawImage( (img2.width() - img.width()) / 2, (img2.height() - img.height()) / 2, img );
    p2.end();

    return img2;
}

void DealerScene::mapOldId(int) {}


int DealerScene::oldId() const
{
    return gameId();
}


bool DealerScene::allowedToStartNewGame()
{
    // Check if the user is already running a game, and if she is,
    // then ask if she wants to abort it.
    return !d->gameStarted
           || d->wasJustSaved
           || d->toldAboutWonGame
           || d->toldAboutLostGame
           || KMessageBox::warningContinueCancel(0,
                     i18n("A new game has been requested, but there is already a game in progress.\n\n"
                          "A loss will be recorded in the statistics if the current game is abandoned."),
                     i18n("Abandon Current Game?"),
                     KGuiItem(i18n("Abandon Current Game")),
                     KStandardGuiItem::cancel(),
                     "careaboutstats"
                    ) == KMessageBox::Continue;
}

void DealerScene::addCardForDeal( KCardPile * pile, KCard * card, bool faceUp, QPointF startPos )
{
    Q_ASSERT( card );
    Q_ASSERT( pile );

    card->setFaceUp( faceUp );
    pile->add( card );
    m_initDealPositions.insert( card, startPos );
}


void DealerScene::startDealAnimation()
{
    qreal speed = sqrt( width() * width() + height() * height() ) / ( DURATION_DEAL );
    foreach ( PatPile * p, patPiles() )
    {
        p->layoutCards(0);
        foreach ( KCard * c, p->cards() )
        {
            if ( !m_initDealPositions.contains( c ) )
                continue;

            QPointF pos2 = c->pos();
            c->setPos( m_initDealPositions.value( c ) );

            QPointF delta = c->pos() - pos2;
            qreal dist = sqrt( delta.x() * delta.x() + delta.y() * delta.y() );
            int duration = qRound( dist / speed );
            c->animate( pos2, c->zValue(), 1, 0, c->isFaceUp(), false, duration );
        }
    }
    m_initDealPositions.clear();
}



#include "dealer.moc"
