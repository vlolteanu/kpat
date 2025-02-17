/*
 * Copyright (C) 1995 Paul Olav Tvete <paul@troll.no>
 * Copyright (C) 2000-2009 Stephan Kulow <coolo@kde.org>
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

// own
#include "dealer.h"
#include "dealerinfo.h"
#include "kpat_debug.h"
#include "kpat_version.h"
#include "mainwindow.h"
#include "patsolve/solverinterface.h"
// KCardGame
#include <KCardDeck>
#include <KCardTheme>
// KF
#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <Kdelibs4ConfigMigrator>
#endif
// Qt
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTime>
// Std
#include <climits>

static DealerScene *getDealer(int wanted_game, const QString &name)
{
    const auto games = DealerInfoList::self()->games();
    for (DealerInfo *di : games) {
        if ((wanted_game < 0) ? (di->untranslatedBaseName().toString() == name) : di->providesId(wanted_game)) {
            DealerScene *d = di->createGame();
            Q_ASSERT(d);
            d->setDeck(new KCardDeck(KCardTheme(), d));
            d->initialize();

            if (!d->solver()) {
                qCCritical(KPAT_LOG) << "There is no solver for" << di->baseName();
                return nullptr;
            }

            return d;
        }
    }
    return nullptr;
}

// A function to remove all nonalphanumeric characters from a string
// and convert all letters to lowercase.
QString lowerAlphaNum(const QString &string)
{
    QString result;
    for (int i = 0; i < string.size(); ++i) {
        QChar c = string.at(i);
        if (c.isLetterOrNumber())
            result += c.toLower();
    }
    return result;
}

int main(int argc, char **argv)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("kpat");

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    Kdelibs4ConfigMigrator migrate(QStringLiteral("kpat"));
    migrate.setConfigFiles(QStringList() << QStringLiteral("kpatrc"));
    migrate.setUiFiles(QStringList() << QStringLiteral("kpatui.rc"));
    migrate.migrate();
#endif
    KAboutData aboutData(QStringLiteral("kpat"),
                         i18n("KPatience"),
                         QStringLiteral(KPAT_VERSION_STRING),
                         i18n("KDE Patience Game"),
                         KAboutLicense::GPL_V2,
                         i18n("© 1995 Paul Olav Tvete\n© 2000 Stephan Kulow"),
                         QString(),
                         QStringLiteral("https://apps.kde.org/kpat"));

    aboutData.addAuthor(i18n("Paul Olav Tvete"), i18n("Author of original Qt version"), QStringLiteral("paul@troll.no"));
    aboutData.addAuthor(i18n("Mario Weilguni"), i18n("Initial KDE port"), QStringLiteral("mweilguni@kde.org"));
    aboutData.addAuthor(i18n("Matthias Ettrich"), QString(), QStringLiteral("ettrich@kde.org"));
    aboutData.addAuthor(i18n("Rodolfo Borges"), i18n("New game types"), QStringLiteral("barrett@9hells.org"));
    aboutData.addAuthor(i18n("Peter H. Ruegg"), QString(), QStringLiteral("kpat@incense.org"));
    aboutData.addAuthor(i18n("Michael Koch"), i18n("Bug fixes"), QStringLiteral("koch@kde.org"));
    aboutData.addAuthor(i18n("Marcus Meissner"), i18n("Shuffle algorithm for game numbers"), QStringLiteral("mm@caldera.de"));
    aboutData.addAuthor(i18n("Tom Holroyd"), i18n("Initial patience solver"), QStringLiteral("tomh@kurage.nimh.nih.gov"));
    aboutData.addAuthor(i18n("Stephan Kulow"), i18n("Rewrite and current maintainer"), QStringLiteral("coolo@kde.org"));
    aboutData.addAuthor(i18n("Erik Sigra"), i18n("Klondike improvements"), QStringLiteral("sigra@home.se"));
    aboutData.addAuthor(i18n("Josh Metzler"), i18n("Spider implementation"), QStringLiteral("joshdeb@metzlers.org"));
    aboutData.addAuthor(i18n("Maren Pakura"), i18n("Documentation"), QStringLiteral("maren@kde.org"));
    aboutData.addAuthor(i18n("Inge Wallin"), i18n("Bug fixes"), QStringLiteral("inge@lysator.liu.se"));
    aboutData.addAuthor(i18n("Simon Hürlimann"), i18n("Menu and toolbar work"), QStringLiteral("simon.huerlimann@huerlisi.ch"));
    aboutData.addAuthor(i18n("Parker Coates"), i18n("Cleanup and polish"), QStringLiteral("coates@kde.org"));
    aboutData.addAuthor(i18n("Shlomi Fish"), i18n("Integration with Freecell Solver and further work"), QStringLiteral("shlomif@cpan.org"));
    aboutData.addAuthor(i18n("Michael Lang"), i18n("New game types"), QStringLiteral("criticaltemp@protonmail.com"));

    // Create a KLocale earlier than normal so that we can use i18n to translate
    // the names of the game types in the help text.
    QMap<QString, int> indexMap;
    QStringList gameList;
    const auto games = DealerInfoList::self()->games();
    for (const DealerInfo *di : games) {
        KLocalizedString localizedKey = di->untranslatedBaseName();
        // QT5 const QString translatedKey = lowerAlphaNum( localizedKey.toString( tmpLocale ) );
        // QT5 gameList << translatedKey;
        // QT5 indexMap.insert( translatedKey, di->baseId() );
        indexMap.insert(di->baseIdString(), di->baseId());
    }
    gameList.sort();
    const QString listSeparator = i18nc("List separator", ", ");

    QCommandLineParser parser;
    KAboutData::setApplicationData(aboutData);
    KCrash::initialize();

    parser.addOption(
        QCommandLineOption(QStringList() << QStringLiteral("solvegame"), i18n("Try to find a solution to the given savegame"), QStringLiteral("file")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("solve"), i18n("Dealer to solve (debug)"), QStringLiteral("num")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("start"), i18n("Game range start (default 0:INT_MAX)"), QStringLiteral("num")));
    parser.addOption(
        QCommandLineOption(QStringList() << QStringLiteral("end"), i18n("Game range end (default start:start if start given)"), QStringLiteral("num")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("gametype"),
                                        i18n("Skip the selection screen and load a particular game type. Valid values are: %1", gameList.join(listSeparator)),
                                        QStringLiteral("game")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("testdir"), i18n("Directory with test cases"), QStringLiteral("directory")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("generate"), i18n("Generate random test cases")));
    parser.addPositionalArgument(QStringLiteral("file"), i18n("File to load"));

    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("kpat")));

    QString savegame = parser.value(QStringLiteral("solvegame"));
    if (!savegame.isEmpty()) {
        QFile of(savegame);
        of.open(QIODevice::ReadOnly);
        QDomDocument doc;
        doc.setContent(&of);

        DealerScene *f;
        QString id_attr = doc.documentElement().attribute(QStringLiteral("id"));
        if (!id_attr.isEmpty()) {
            f = getDealer(id_attr.toInt(), QString());
            f->loadLegacyFile(&of);
        } else {
            of.seek(0);
            QXmlStreamReader xml(&of);
            if (!xml.readNextStartElement()) {
                qCritical() << "Failed to read XML" << savegame;
            }
            f = getDealer(DealerInfoList::self()->gameIdForFile(xml), QString());
            of.seek(0);
            f->loadFile(&of, false);
        }
        f->solver()->translate_layout();

        int ret = f->solver()->patsolve();
        if (ret == SolverInterface::SolutionExists)
            fprintf(stdout, "won\n");
        else if (ret == SolverInterface::NoSolutionExists)
            fprintf(stdout, "lost\n");
        else
            fprintf(stdout, "unknown\n");

        return 0;
    }

    QString testdir = parser.value(QStringLiteral("testdir"));
    if (!testdir.isEmpty()) {
        if (parser.isSet(QStringLiteral("generate"))) {
            for (int dealer = 0; dealer < 20; dealer++) {
                DealerScene *f = getDealer(dealer, QString());
                if (!f)
                    continue;
                int count = 100;
                QElapsedTimer mytime;
                while (count) {
                    if (f->deck())
                        f->deck()->stopAnimations();
                    int i = QRandomGenerator::global()->bounded(INT_MAX);
                    f->startNew(i);
                    mytime.start();
                    f->solver()->translate_layout();
                    int ret = f->solver()->patsolve();
                    if (ret == SolverInterface::SolutionExists) {
                        fprintf(stdout, "%d: %d won (%lld ms)\n", dealer, i, mytime.elapsed());
                        count--;
                        QFile file(QStringLiteral("%1/%2-%3-1").arg(testdir).arg(dealer).arg(i));
                        file.open(QFile::WriteOnly);
                        f->saveLegacyFile(&file);
                    } else if (ret == SolverInterface::NoSolutionExists) {
                        fprintf(stdout, "%d: %d lost (%lld ms)\n", dealer, i, mytime.elapsed());
                        count--;
                        QFile file(QStringLiteral("%1/%2-%3-0").arg(testdir).arg(dealer).arg(i));
                        file.open(QFile::WriteOnly);
                        f->saveLegacyFile(&file);
                    } else {
                        fprintf(stdout, "%d: %d unknown (%lld ms)\n", dealer, i, mytime.elapsed());
                    }
                }
            }
        }
        return 0;
    }

    bool ok = false;
    QString wanted_name;
    int wanted_game = -1;
    if (parser.isSet(QStringLiteral("solve"))) {
        wanted_name = parser.value(QStringLiteral("solve"));
        ok = true;
        bool isInt = false;
        wanted_game = wanted_name.toInt(&isInt);
        if (!isInt)
            wanted_game = -1;
    }
    if (ok) {
        ok = false;
        int end_index = -1;
        if (parser.isSet(QStringLiteral("end")))
            end_index = parser.value(QStringLiteral("end")).toInt(&ok);
        if (!ok)
            end_index = -1;
        ok = false;
        int start_index = -1;
        if (parser.isSet(QStringLiteral("start")))
            start_index = parser.value(QStringLiteral("start")).toInt(&ok);
        if (!ok) {
            start_index = 0;
        }
        if (end_index == -1)
            end_index = start_index;
        DealerScene *f = getDealer(wanted_game, wanted_name);
        if (!f)
            return 1;

        QElapsedTimer mytime;
        for (int i = start_index; i <= end_index; i++) {
            mytime.start();
            f->deck()->stopAnimations();
            f->startNew(i);
            f->solver()->translate_layout();
            int ret = f->solver()->patsolve();
            if (ret == SolverInterface::SolutionExists)
                fprintf(stdout, "%d won (%lld ms)\n", i, mytime.elapsed());
            else if (ret == SolverInterface::NoSolutionExists)
                fprintf(stdout, "%d lost (%lld ms)\n", i, mytime.elapsed());
            else
                fprintf(stdout, "%d unknown (%lld ms)\n", i, mytime.elapsed());
        }
        fprintf(stdout, "all_moves %ld\n", all_moves);
        return 0;
    }

    QString gametype = parser.value(QStringLiteral("gametype")).toLower();
    QFile savedState(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1String("/" saved_state_file));

    MainWindow *w = new MainWindow;
    if (!parser.positionalArguments().isEmpty()) {
        if (!w->loadGame(QUrl::fromLocalFile(parser.positionalArguments().at(0)), true))
            w->slotShowGameSelectionScreen();
    } else if (indexMap.contains(gametype)) {
        w->slotGameSelected(indexMap.value(gametype));
    } else if (savedState.exists()) {
        if (!w->loadGame(QUrl::fromLocalFile(savedState.fileName()), false))
            w->slotShowGameSelectionScreen();
    } else {
        w->slotShowGameSelectionScreen();
    }
    w->show();

    const KDBusService dbusService(KDBusService::Multiple);

    return app.exec();
}
