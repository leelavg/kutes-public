/*
 * UTreeModel - Qt model for Boron block!
 * Copyright (C) 2019,2024  Karl Robillard
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "urlan.h"
//#include <boron/urlan.h>
#include <QAbstractItemModel>
#include <QStringList>

class UTreeModel : public QAbstractItemModel
{
public:

    UTreeModel(QObject* parent, UThread* thread, const UCell* hdr,
               const UCell* data = nullptr);
    ~UTreeModel();

    QVariant data(const QModelIndex& , int role) const;
    QVariant headerData(int section, Qt::Orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex& parent) const;
    QModelIndex parent(const QModelIndex& index) const;
    int rowCount(const QModelIndex& parent) const;
    int columnCount(const QModelIndex& parent) const;
  //bool removeRows(int row, int count, const QModelIndex& par = QModelIndex());

    void setData(const UCell* data);
    void blockSlice(const QModelIndex& index, UCell* res);
  //bool insertCellRows(int row, int count, const UCell* data);

protected:

    UThread* ut;
    UIndex _blkN;
    UIndex _hold;
    int    _rows;
    int    _cols;
    QStringList _hdr;
};

void cellToQString(UThread* ut, const UCell* val, QString& str);
