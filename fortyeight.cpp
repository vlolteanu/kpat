#include <qdialog.h>
#include "fortyeight.h"
#include <klocale.h>
#include "deck.h"
#include "pile.h"
#include <assert.h>
#include <kdebug.h>

HorLeftPile::HorLeftPile( int _index, Dealer* parent)
    : Pile(_index, parent)
{
}

QSize HorLeftPile::cardOffset( bool _spread, bool, const Card *) const
{
    if (_spread)
        return QSize(-Pile::HSPREAD, 0);

    return QSize(0, 0);
}

Fortyeight::Fortyeight( KMainWindow* parent, const char* name)
        : Dealer(parent,name)
{
    deck = new Deck(0, this, 2);
    connect(deck, SIGNAL(clicked(Card*)), SLOT(deckClicked(Card*)));
    deck->move(600, 450);

    pile = new HorLeftPile(13, this);
    pile->setAddFlags(Pile::addSpread);
    pile->move(510, 450);

    for (int i = 0; i < 8; i++) {

        target[i] = new Pile(9 + i, this);
        target[i]->move(8+80*i, 10);
        target[i]->setRemoveFlags(Pile::disallow);
        target[i]->setCheckIndex(0);
        target[i]->setTarget(true);

        stack[i] = new Pile(1 + i, this);
        stack[i]->move(8+80*i, 110);
        stack[i]->setAddFlags(Pile::addSpread);
        stack[i]->setRemoveFlags(Pile::autoTurnTop);
        stack[i]->setCheckIndex(1);

    }

    setActions(Dealer::Hint | Dealer::Demo);
}

//-------------------------------------------------------------------------//

void Fortyeight::restart()
{
    lastdeal = false;
    deck->collectAndShuffle();
    deal();
}

void Fortyeight::deckClicked(Card *)
{
    if (deck->isEmpty()) {
        if (lastdeal)
            return;
        lastdeal = true;
        while (!pile->isEmpty())
            deck->add(pile->at(pile->cardsLeft()-1), true, false);
    }
    Card *c = deck->nextCard();
    pile->add(c, true, true);
    int x = int(c->x());
    int y = int(c->y());
    c->move(deck->x(), deck->y());
    c->flipTo(x, y, 8);
    kdDebug() << "left " << deck->cardsLeft() << endl;
}

bool Fortyeight::CanPutStack(const Pile *c1, const CardList &c2) const
{
    assert(c2.count() == 1);
    Card *c = c2.first();

    // only aces in empty spaces
    if (c1->isEmpty())
        return true;

    // ok if in sequence, same suit
    return (c1->top()->suit() == c->suit())
          && (c1->top()->value() == (c->value()+1));
}

bool Fortyeight::CanPutTarget(const Pile *c1, const CardList &c2) const
{
    Card *c = c2.first();

    if (c1->isEmpty())
        return (c->value() == Card::Ace);

    // ok if in sequence, alternate colors
    return ((c1->top()->value()+1) == c->value())
            && (c1->top()->suit() == c->suit());
}

void Fortyeight::getHints()
{
    CardList empty;
    CardList candidates;
    if (pile->top()) {
        candidates.append(pile->top());
    }
    for (int i = 0; i < 8; i++) {
        if (stack[i]->top())
            candidates.append(stack[i]->top());
    }
    for (CardList::Iterator it = candidates.begin(); it != candidates.end(); ++it)
    {
        empty.clear();
        empty.append(*it);
        for (int i = 0; i < 8; i++) {
            if (CanPutTarget(target[i], empty)) {
                newHint(new MoveHint(*it, target[i]));
                continue;
            }
            if (!CanPutStack(stack[i], empty))
                continue;

            if ((*it)->source() != pile) {

                if ((*it)->source()->cardsLeft() == 1)
                    continue;

                Card *c1 = (*it)->source()->at((*it)->source()->indexOf(*it)-1);

                bool perfect =  (c1->suit() == (*it)->suit()
                                 && (c1->value() == ((*it)->value()+1)));

                if (perfect)
                    continue;
            }
            newHint(new MoveHint(*it, stack[i]));
        }
    }
}

Card *Fortyeight::demoNewCards()
{
    if (deck->isEmpty() && lastdeal)
        return 0;
    deckClicked(0);
    return pile->top();
}

bool Fortyeight::checkAdd(int index, const Pile *c1, const CardList &c2) const
{
    switch (index) {
        case 0:
            return CanPutTarget(c1, c2);
        case 1:
            return CanPutStack(c1, c2);
        default:
            return false;
    }
}

void Fortyeight::deal()
{
    for (int r = 0; r < 4; r++)
    {
        for (int column = 0; column < 8; column++)
        {
            if (false) { // doesn't look
                stack[column]->add(deck->nextCard(), true, true);
                stack[column]->top()->turn(true);
            } else {
                stack[column]->add(deck->nextCard(), false, true);
            }
        }
    }
    pile->add(deck->nextCard(), false, false);
}

static class LocalDealerInfo8 : public DealerInfo
{
public:
    LocalDealerInfo8() : DealerInfo(I18N_NOOP("F&orty and Eight"), 8) {}
    virtual Dealer *createGame(KMainWindow *parent) { return new Fortyeight(parent); }
} ldi9;

//-------------------------------------------------------------------------//

#include "fortyeight.moc"

//-------------------------------------------------------------------------//

