/*
 * Copyright (C) 1995 Paul Olav Tvete <paul@troll.no>
 * Copyright (C) 2000-2009 Stephan Kulow <coolo@kde.org>
 * Copyright (C) 2010 Parker Coates <parker.coates@kdemail.net>
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

#ifndef PILE_H
#define PILE_H

#include "card.h"
class CardScene;
#include "speeds.h"

class QPropertyAnimation;
#include <QtGui/QGraphicsPixmapItem>


/***************************************

  Pile -- A pile on the board that can hold cards.

**************************************/

class Pile : public QObject, public QGraphicsPixmapItem
{
    Q_OBJECT
    Q_PROPERTY( qreal highlightedness READ highlightedness WRITE setHighlightedness )

public:
    explicit Pile( const QString & objectName = QString() );
    virtual ~Pile();

    CardScene  *cardScene() const;
    CardList      cards()  const { return m_cards; }

    virtual void moveCards(CardList &c, Pile *to = 0);
    void moveCardsBack(CardList& c, int duration = -1);

    void setAutoTurnTop( bool autoTurnTop ) { m_autoTurnTop = autoTurnTop; }
    bool autoTurnTop() const { return m_autoTurnTop; }

    void setHighlighted( bool flag );
    bool isHighlighted() const;

    void setGraphicVisible( bool visible );
    bool isGraphicVisible();

    void cardPressed(Card * card);

    Card *top() const;
    CardList topCardsDownTo( const Card * card ) const;

    void animatedAdd( Card *c, bool faceUp );
    void add( Card *c, int index = -1);
    void remove(Card *c);
    void clear();

    bool isEmpty() const { return m_cards.isEmpty(); }

    enum { Type = UserType + 2 };
    virtual int type() const { return Type; }

    virtual void setVisible(bool vis);

    int cardsLeft() const { return m_cards.count(); }

    int indexOf(const Card *c) const;
    Card *at(int index) const;

    virtual QPointF cardOffset( const Card *card ) const;

    // The spread properties.
    QSizeF spread() const    { return _spread; }
    void setSpread(QSizeF s) { _spread = s; } 
    void setSpread(qreal width, qreal height)  { _spread = QSizeF(width,height); }

    void setPilePos( qreal x, qreal y);
    QPointF pilePos() const;

    void setGraphicSize( QSize size );

    void setReservedSpace( const QSizeF &p) { m_reserved = p; }
    QSizeF reservedSpace() const { return m_reserved; }

    void setMaximumSpace( const QSizeF &s) { m_space = s; }
    QSizeF maximumSpace() const { return m_space; }

    virtual void layoutCards( int duration = DURATION_RELAYOUT );

public slots:
    virtual bool cardClicked(Card *c);
    virtual bool cardDoubleClicked(Card *c);
    virtual void relayoutCards();

signals:
    void clicked(Card *c);
    void doubleClicked(Card *c);
    void pressed(Card *c);

protected:
    virtual QPixmap normalPixmap( QSize size );
    virtual QPixmap highlightedPixmap( QSize size );

    CardList  m_cards;
    QTimer *m_relayoutTimer;

private:
    void setHighlightedness( qreal highlightedness );
    qreal highlightedness() const;
    void updatePixmap( QSize size );
    QPropertyAnimation *m_fadeAnimation;

    QSizeF    _spread;

    bool m_autoTurnTop;

    bool m_graphicVisible;
    bool m_highlighted;
    QPointF _pilePos;
    QSizeF m_reserved;
    QSizeF m_space;

    qreal m_highlightedness;
};

#endif
