/*
 * UTreeModel - Qt list model for Boron block!
 * Copyright (C) 2019,2024  Karl Robillard
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "UTreeModel.h"


void cellToQString(UThread* ut, const UCell* val, QString& str)
{
    switch (ur_type(val))
    {
        case UT_NONE:
            str.clear();
            break;

        case UT_CHAR:
            str = QChar( int(ur_char(val)) );
            break;

        case UT_INT:
            str.setNum( qlonglong(ur_int(val)) );
            break;

        case UT_DOUBLE:
            str.setNum( ur_double(val) );
            break;

        case UT_STRING:
        case UT_FILE:
        {
            USeriesIter si;
            int len;
            ur_seriesSlice(ut, &si, val);
            len = si.end - si.it;
            switch (si.buf->form)
            {
                case UR_ENC_LATIN1:
                    str = QString::fromLatin1(si.buf->ptr.c + si.it, len);
                    break;
                case UR_ENC_UTF8:
                    str = QString::fromUtf8(si.buf->ptr.c + si.it, len);
                    break;
                case UR_ENC_UCS2:
                    str = QString::fromUtf16(si.buf->ptr.u16 + si.it, len);
                    break;
            }
        }
            break;

        default:
        {
            UBuffer buf;
            ur_strInit(&buf, UR_ENC_LATIN1, 0);
            ur_toStr(ut, val, &buf, 0);
            str = QString::fromLatin1(buf.ptr.c, buf.used);
            ur_strFree(&buf);
        }
            break;
    }
}


UTreeModel::UTreeModel(QObject* parent, UThread* thread, const UCell* hdr,
                       const UCell* data)
    : QAbstractItemModel(parent), ut(thread)
{
    _blkN = UR_INVALID_BUF;
    _hold = UR_INVALID_HOLD;
    _rows = 0;
    _cols = 1;

    if( hdr && ur_is(hdr, UT_BLOCK) )
    {
        QString str;
        UBlockIter bi;
        ur_blkSlice( ut, &bi, hdr );
        ur_foreach( bi )
        {
            cellToQString(ut, bi.it, str);
            _hdr.append(str);
        }
        if( _hdr.size() )
            _cols = _hdr.size();
    }

    if( data )
        setData( data );
}


UTreeModel::~UTreeModel()
{
    if( _hold != UR_INVALID_HOLD )
    {
        ur_release( _hold );
    }
}


static QVariant cellToVariant(UThread* ut, const UCell* cell )
{
    switch( ur_type(cell) )
    {
        case UT_CHAR:
            return QChar( int(ur_char(cell)) );
        case UT_INT:
            return qlonglong(ur_int(cell));
        case UT_DOUBLE:
            return ur_double(cell);
        default:
        {
            QString str;
            cellToQString(ut, cell, str);
            return str;
        }
    }
}


QVariant UTreeModel::data( const QModelIndex& index, int role ) const
{
    if( index.isValid() )
    {
        if( role == Qt::DisplayRole )
        {
            const UBuffer* blk = ur_buffer( _blkN );
            int i = (_cols * index.row()) + index.column();
            if( i < blk->used )
                return cellToVariant(ut, blk->ptr.cell + i );
        }
    }
    return QVariant();
}


QVariant UTreeModel::headerData( int section, Qt::Orientation /*orientation*/,
                                 int role ) const
{
    switch( role )
    {
        case Qt::DisplayRole:
            return _hdr.value( section );
    }
    return QVariant();
}


QModelIndex UTreeModel::index( int row, int column,
                               const QModelIndex& /*parent*/ ) const
{
    return createIndex( row, column );
}


QModelIndex UTreeModel::parent( const QModelIndex& /*index*/ ) const
{
    return QModelIndex();
}


int UTreeModel::rowCount( const QModelIndex& parent ) const
{
    if( parent.isValid() )
        return 0;
    return _rows;
}


int UTreeModel::columnCount( const QModelIndex& parent ) const
{
    if( parent.isValid() )
        return 0;
    return _cols;
}


#if 0
bool UTreeModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (row < _rows) {
        beginRemoveRows(parent, row, row + count - 1);

        UBuffer* blk = ur_buffer(_blkN);
        ur_arrErase(blk, row * _cols, count * _cols);
        _rows = blk->used / _cols;

        endRemoveRows();
        return true;
    }
    return false;
}


bool UTreeModel::insertCellRows(int row, int count, const UCell* cells)
{
    if (_blkN == UR_INVALID_BUF) {
        _blkN = ur_makeBlock(ut, ((count < 8) ? 8 : count) * _cols);
        _hold = ur_hold(_blkN);
    }

    beginInsertRows(QModelIndex(), row, row + count - 1);
    _rows += count;

    UBuffer* blk = ur_buffer(_blkN);
    row *= _cols;
    count *= _cols;
    ur_arrExpand(blk, row, count);
    memcpy(blk->ptr.cell + row, cells, sizeof(UCell) * count);

    endInsertRows();
    return true;
}
#endif


void UTreeModel::setData( const UCell* data )
{
    beginResetModel();

    if( _hold != UR_INVALID_HOLD )
        ur_release( _hold );

    if( ur_is(data, UT_BLOCK ) )
    {
        _blkN = data->series.buf;
        _hold = ur_hold( _blkN );

        const UBuffer* blk = ur_buffer( _blkN );
        _rows = blk->used / _cols;
    }
    else
    {
        _blkN = UR_INVALID_BUF;
        _hold = UR_INVALID_HOLD;
        _rows = 0;
    }

    endResetModel();
}


void UTreeModel::blockSlice( const QModelIndex& index, UCell* res )
{
    if( index.isValid() )
    {
        //const UBuffer* blk = ur_buffer( _blkN );
        int i = _cols * index.row(); // + index.column();
        //if( i < blk->used )
        {
            ur_setId(res, UT_BLOCK);
            ur_setSlice(res, _blkN, i, i + _cols);
        }
    }
    else
    {
        ur_setId(res, UT_NONE);
    }
}


//EOF
