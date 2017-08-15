/*
 * Cantata
 *
 * Copyright (c) 2011-2017 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "dynamicplaylists.h"
#include "config.h"
#include "support/utils.h"
#include "widgets/icons.h"
#include "models/roles.h"
#include "gui/settings.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QTimer>
#include <QIcon>
#include <QUrl>
#include <QNetworkProxy>
#include <signal.h>
#include <stdio.h>

const QString RulesPlaylists::constExtension=QLatin1String(".rules");
const QString RulesPlaylists::constRuleKey=QLatin1String("Rule");
const QString RulesPlaylists::constArtistKey=QLatin1String("Artist");
const QString RulesPlaylists::constSimilarArtistsKey=QLatin1String("SimilarArtists");
const QString RulesPlaylists::constAlbumArtistKey=QLatin1String("AlbumArtist");
const QString RulesPlaylists::constComposerKey=QLatin1String("Composer");
const QString RulesPlaylists::constCommentKey=QLatin1String("Comment");
const QString RulesPlaylists::constAlbumKey=QLatin1String("Album");
const QString RulesPlaylists::constTitleKey=QLatin1String("Title");
const QString RulesPlaylists::constGenreKey=QLatin1String("Genre");
const QString RulesPlaylists::constDateKey=QLatin1String("Date");
const QString RulesPlaylists::constRatingKey=QLatin1String("Rating");
const QString RulesPlaylists::constDurationKey=QLatin1String("Duration");
const QString RulesPlaylists::constNumTracksKey=QLatin1String("NumTracks");
const QString RulesPlaylists::constFileKey=QLatin1String("File");
const QString RulesPlaylists::constExactKey=QLatin1String("Exact");
const QString RulesPlaylists::constExcludeKey=QLatin1String("Exclude");
const QChar RulesPlaylists::constRangeSep=QLatin1Char('-');
const QChar RulesPlaylists::constKeyValSep=QLatin1Char(':');

RulesPlaylists::RulesPlaylists(const QString &iconFile, const QString &dir)
    : rulesDir(dir)
{
    icn.addFile(":"+iconFile+".svg");
    loadLocal();
}

QVariant RulesPlaylists::headerData(int, Qt::Orientation, int) const
{
    return QVariant();
}

int RulesPlaylists::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : entryList.count();
}

bool RulesPlaylists::hasChildren(const QModelIndex &parent) const
{
    return !parent.isValid();
}

QModelIndex RulesPlaylists::parent(const QModelIndex &) const
{
    return QModelIndex();
}

QModelIndex RulesPlaylists::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || !hasIndex(row, column, parent) || row>=entryList.count()) {
        return QModelIndex();
    }

    return createIndex(row, column);
}

QVariant RulesPlaylists::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        switch (role) {
        case Cantata::Role_TitleText:
            return title();
        case Cantata::Role_SubText:
            return descr();
        case Qt::DecorationRole:
            return icon();
        }
        return QVariant();
    }

    if (index.parent().isValid() || index.row()>=entryList.count()) {
        return QVariant();
    }

    switch (role) {
    case Qt::ToolTipRole:
        if (!Settings::self()->infoTooltips()) {
            return QVariant();
        }
    case Qt::DisplayRole:
        return entryList.at(index.row()).name;
    case Cantata::Role_SubText: {
        const Entry &e=entryList.at(index.row());
        return tr("%n Rule(s)", "", e.rules.count())+(e.haveRating() ? tr(" – Rating: %1..%2")
                              .arg((double)e.ratingFrom/Song::Rating_Step).arg((double)e.ratingTo/Song::Rating_Step) : QString());
    }
    default:
        return QVariant();
    }
}

Qt::ItemFlags RulesPlaylists::flags(const QModelIndex &index) const
{
    if (index.isValid()) {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }
    return Qt::NoItemFlags;
}

RulesPlaylists::Entry RulesPlaylists::entry(const QString &e)
{
    if (!e.isEmpty()) {
        QList<Entry>::Iterator it=find(e);
        if (it!=entryList.end()) {
            return *it;
        }
    }

    return Entry();
}

bool RulesPlaylists::save(const Entry &e)
{
    if (e.name.isEmpty()) {
        return false;
    }

    QString string;
    QTextStream str(&string);
    if (e.numTracks >= minTracks() && e.numTracks <= maxTracks()) {
        str << constNumTracksKey << constKeyValSep << e.numTracks << '\n';
    }
    if (e.ratingFrom!=0 || e.ratingTo!=0) {
        str << constRatingKey << constKeyValSep << e.ratingFrom << constRangeSep << e.ratingTo << '\n';
    }
    if (e.minDuration!=0 || e.maxDuration!=0) {
        str << constDurationKey << constKeyValSep << e.minDuration << constRangeSep << e.maxDuration << '\n';
    }
    foreach (const Rule &rule, e.rules) {
        if (!rule.isEmpty()) {
            str << constRuleKey << '\n';
            Rule::ConstIterator it(rule.constBegin());
            Rule::ConstIterator end(rule.constEnd());
            for (; it!=end; ++it) {
                str << it.key() << constKeyValSep << it.value() << '\n';
            }
        }
    }

    if (isRemote()) {
        return saveRemote(string, e);
    }

    QFile f(Utils::dataDir(rulesDir, true)+e.name+constExtension);
    if (f.open(QIODevice::WriteOnly|QIODevice::Text)) {
        QTextStream out(&f);
        out.setCodec("UTF-8");
        out << string;
        updateEntry(e);
        return true;
    }
    return false;
}

void RulesPlaylists::updateEntry(const Entry &e)
{
    QList<RulesPlaylists::Entry>::Iterator it=find(e.name);
    if (it!=entryList.end()) {
        entryList.replace(it-entryList.begin(), e);
        QModelIndex idx=index(it-entryList.begin(), 0, QModelIndex());
        emit dataChanged(idx, idx);
    } else {
        beginInsertRows(QModelIndex(), entryList.count(), entryList.count());
        entryList.append(e);
        endInsertRows();
    }
}

void RulesPlaylists::del(const QString &name)
{
    QList<RulesPlaylists::Entry>::Iterator it=find(name);
    if (it==entryList.end()) {
        return;
    }
    QString fName(Utils::dataDir(rulesDir, false)+name+constExtension);
    bool isCurrent=currentEntry==name;

    if (!QFile::exists(fName) || QFile::remove(fName)) {
        if (isCurrent) {
            stop();
        }
        beginRemoveRows(QModelIndex(), it-entryList.begin(), it-entryList.begin());
        entryList.erase(it);
        endRemoveRows();
        return;
    }
}

QList<RulesPlaylists::Entry>::Iterator RulesPlaylists::find(const QString &e)
{
    QList<RulesPlaylists::Entry>::Iterator it(entryList.begin());
    QList<RulesPlaylists::Entry>::Iterator end(entryList.end());

    for (; it!=end; ++it) {
        if ((*it).name==e) {
            break;
        }
    }
    return it;
}

void RulesPlaylists::loadLocal()
{
    beginResetModel();
    entryList.clear();
    currentEntry=QString();

    // Load all current enttries...
    QString dirName=Utils::dataDir(rulesDir);
    QDir d(dirName);
    if (d.exists()) {
        QStringList rulesFiles=d.entryList(QStringList() << QChar('*')+constExtension);
        foreach (const QString &rf, rulesFiles) {
            QFile f(dirName+rf);
            if (f.open(QIODevice::ReadOnly|QIODevice::Text)) {
                QStringList keys=QStringList() << constArtistKey << constSimilarArtistsKey << constAlbumArtistKey << constDateKey
                                               << constExactKey << constAlbumKey << constTitleKey << constGenreKey << constFileKey << constExcludeKey;

                Entry e;
                e.name=rf.left(rf.length()-constExtension.length());
                Rule r;
                QTextStream in(&f);
                in.setCodec("UTF-8");
                QStringList lines = in.readAll().split('\n', QString::SkipEmptyParts);
                foreach (const QString &line, lines) {
                    QString str=line.trimmed();

                    if (str.isEmpty() || str.startsWith('#')) {
                        continue;
                    }

                    if (str==constRuleKey) {
                        if (!r.isEmpty()) {
                            e.rules.append(r);
                            r.clear();
                        }
                    } else if (str.startsWith(constRatingKey+constKeyValSep)) {
                        QStringList vals=str.mid(constRatingKey.length()+1).split(constRangeSep);
                        if (2==vals.count()) {
                            e.ratingFrom=vals.at(0).toUInt();
                            e.ratingTo=vals.at(1).toUInt();
                        }
                    } else if (str.startsWith(constDurationKey+constKeyValSep)) {
                        QStringList vals=str.mid(constDurationKey.length()+1).split(constRangeSep);
                        if (2==vals.count()) {
                            e.minDuration=vals.at(0).toUInt();
                            e.maxDuration=vals.at(1).toUInt();
                        }
                    } else {
                        foreach (const QString &k, keys) {
                            if (str.startsWith(k+constKeyValSep)) {
                                r.insert(k, str.mid(k.length()+1));
                            }
                        }
                    }
                }
                if (!r.isEmpty()) {
                    e.rules.append(r);
                    r.clear();
                }
                entryList.append(e);
            }
        }
    }
    endResetModel();
}
