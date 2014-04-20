#ifndef QHEXEDITDATA_H
#define QHEXEDITDATA_H

/*
 * Piece Chain Data Structure Documentation From:
 * 1) http://www.catch22.net/tuts/piece-chains
 * 2) http://www.catch22.net/tuts/memory-techniques-part-1
 * 3) http://www.catch22.net/tuts/memory-techniques-part-2
 */

#include <QtCore>
#include <QUndoStack>
#include <QUndoCommand>

class QHexEditData : public QObject
{
    Q_OBJECT

    public:
        enum ActionType { None = 0, Insert = 1, Remove = 2, Replace = 3 };

    private:
        class QHexEditDataDevice: public QIODevice
        {
            public:
                QHexEditDataDevice(QObject* parent = 0): QIODevice(parent), _hexeditdata(nullptr) { }
                void setData(QHexEditData* hexeditdata) { this->_hexeditdata = hexeditdata; }
                virtual qint64 size() { return this->_hexeditdata->length(); }

            protected:
                qint64 readData(char *data, qint64 maxlen)
                {
                    if(!this->_hexeditdata)
                        return 0;

                    QByteArray ba = this->_hexeditdata->read(this->pos(), maxlen);

                    if(!ba.isEmpty())
                    {
                        memcpy(data, ba.data(), ba.length());
                        return ba.length();
                    }

                    return -1;
                }

                qint64 writeData(const char *data, qint64 len)
                {
                    if(!this->_hexeditdata)
                        return 0;

                    this->_hexeditdata->replace(this->pos(), QByteArray::fromRawData(data, len));
                    return len; /* Append if needed */
                }

            private:
                QHexEditData* _hexeditdata;
        };

    private:
        class ModifiedItem
        {
            public:
                ModifiedItem(qint64 pos, qint64 len, bool mod = true): _pos(pos), _len(len), _mod(mod) {}
                qint64 pos() const { return this->_pos; }
                qint64 length() const { return this->_len; }
                bool modified() const { return this->_mod; }
                void updatePos(qint64 amt) { this->_pos += amt; }
                void updateLen(qint64 amt) { this->_len += amt; }

            private:
                qint64 _pos;
                qint64 _len;
                bool _mod;
        };

        class ByteRef
        {
            public:
                ByteRef(qint64 pos, QHexEditData* o): _owner(o), _pos(pos) { }
                ByteRef(const ByteRef& br): _owner(br._owner), _pos(br._pos) { }
                ByteRef& operator =(uchar b) { this->_owner->replace(this->_pos, b); return *this; }
                operator uchar() { return this->_owner->at(this->_pos); }

            private:
                QHexEditData* _owner;
                qint64 _pos;
        };

        typedef QList<ModifiedItem*> ModifyList;

    public:
        class AbstractCommand: public QUndoCommand
        {
            public:
                AbstractCommand(qint64 pos, QHexEditData* owner, QUndoCommand* parent = 0): QUndoCommand(parent), _owner(owner), _notify(true), _pos(pos) { }

            public:
                bool canNotify() const { return this->_notify; }
                void setNotify(bool b) { this->_notify = b; }
                qint64 pos() const { return this->_pos; }
                QHexEditData* owner() const { return this->_owner; }

            protected:
                void notifyDataChanged(qint64 offset, qint64 size, QHexEditData::ActionType reason)
                {
                    this->owner()->_dirtybuffer = true;
                    emit this->owner()->dataChanged(offset, size, reason);
                }

            private:
                QHexEditData* _owner;
                bool _notify;
                qint64 _pos;
        };

        class ModifyRangeCommand: public AbstractCommand
        {
            public:
                ModifyRangeCommand(int index, qint64 pos, const ModifyList& oldml, const ModifyList& newml, QHexEditData* owner, QUndoCommand* parent = 0): AbstractCommand(pos, owner, parent), _index(index), _oldml(oldml), _newml(newml)
                {
                    this->_oldlength = this->_newlength = 0;

                    for(int i = 0; i < oldml.length(); i++)
                        this->_oldlength += oldml[i]->length();

                    for(int i = 0; i < newml.length(); i++)
                        this->_newlength += newml[i]->length();
                }

            protected:
                qint64 index() const { return this->_index; }
                qint64 oldLength() const { return this->_oldlength; }
                qint64 newLength() const { return this->_newlength; }

            private:
                int _index;
                qint64 _oldlength;
                qint64 _newlength;

            protected:
                QHexEditData::ModifyList _oldml;
                QHexEditData::ModifyList _newml;
        };

        class InsertCommand: public ModifyRangeCommand
        {
            public:
                InsertCommand(int index, qint64 pos, const ModifyList& oldml, const ModifyList& newml, QHexEditData* owner, bool opt, QUndoCommand* parent = 0): ModifyRangeCommand(index, pos, oldml, newml, owner, parent), _optimized(opt) { }
                virtual int id() const { return QHexEditData::Insert; }

                virtual bool mergeWith(const QUndoCommand* command)
                {
                    const InsertCommand* ic = dynamic_cast<const InsertCommand*>(command);

                    if(ic->optimized())
                    {
                        ModifiedItem* oldmi = nullptr;

                        if(this->_newml.length() == 1) /* Single Item */
                            oldmi = this->_newml[0];
                        else /* Splitted Span in Three Parts, the second is the new one, update length */
                            oldmi = this->_newml[1];

                        ModifiedItem* newmi = ic->_newml.last();

                        oldmi->updateLen(newmi->length());
                        this->notifyDataChanged(ic->pos(), ic->newLength(), QHexEditData::Insert);
                        return true;
                    }

                    return false;
                }

                virtual void undo()
                {
                    for(int i = 0; i < this->_newml.length(); i++)
                        this->owner()->_modlist.removeAt(this->index());

                    for(int i = 0; i < this->_oldml.length(); i++)
                        this->owner()->_modlist.insert(this->index() + i, this->_oldml[i]);

                    /* Update HexEditData's length */
                    this->owner()->_length -= this->newLength();
                    this->owner()->_length += this->oldLength();

                    if(this->canNotify())
                        this->notifyDataChanged(this->pos(), this->oldLength(), QHexEditData::Insert);
                }

                virtual void redo()
                {
                    if(this->_optimized)
                        return; /* Don't modify the list if this is an optimized insertion */

                    for(int i = 0; i < this->_oldml.length(); i++)
                        this->owner()->_modlist.removeAt(this->index());

                    for(int i = 0; i < this->_newml.length(); i++)
                        this->owner()->_modlist.insert(this->index() + i, this->_newml[i]);

                    if(this->canNotify())
                        this->notifyDataChanged(this->pos(), this->newLength(), QHexEditData::Insert);
                }

            private:
                bool optimized() const { return this->_optimized; }

            private:
                bool _optimized;
        };

        class RemoveCommand: public ModifyRangeCommand
        {
            public:
                RemoveCommand(int index, qint64 pos, const ModifyList& oldml, const ModifyList& newml, QHexEditData* owner, QUndoCommand* parent = 0): ModifyRangeCommand(index, pos, oldml, newml, owner, parent) { }
                virtual int id() const { return QHexEditData::Remove; }

                virtual void undo()
                {
                    this->owner()->_modlist.erase(this->owner()->_modlist.begin() + this->index(), this->owner()->_modlist.begin() + (this->index() + this->_newml.length()));

                    for(int i = 0; i < this->_oldml.length(); i++)
                        this->owner()->_modlist.insert(this->index() + i, this->_oldml.at(i));

                    /* Update HexEditData's length */
                    this->owner()->_length -= this->newLength();
                    this->owner()->_length += this->oldLength();

                    if(this->canNotify())
                        this->notifyDataChanged(this->pos(), this->oldLength(), QHexEditData::Remove);
                }

                virtual void redo()
                {
                    this->owner()->_modlist.erase(this->owner()->_modlist.begin() + this->index(), this->owner()->_modlist.begin() + (this->index() + this->_oldml.length()));

                    for(int i = 0; i < this->_newml.length(); i++)
                        this->owner()->_modlist.insert(this->index() + i, this->_newml.at(i));

                    if(this->canNotify())
                        this->notifyDataChanged(this->pos(), this->newLength(), QHexEditData::Remove);
                }
        };

        class ReplaceCommand: public AbstractCommand
        {
            public:
                ReplaceCommand(qint64 pos, qint64 len, const QByteArray& ba, QHexEditData* owner, QUndoCommand* parent = 0): AbstractCommand(pos, owner, parent), _len(len), _data(ba), _remcmd(nullptr), _inscmd(nullptr) { }
                virtual int id() const { return QHexEditData::Replace; }

                virtual void undo()
                {
                    this->_inscmd->undo();
                    this->_remcmd->undo();

                    if(this->canNotify())
                        emit this->notifyDataChanged(this->pos(), this->_data.length(), QHexEditData::Replace);
                }

                virtual void redo()
                {
                    if(this->_remcmd && this->_inscmd)
                    {
                        this->_remcmd->redo();
                        this->_inscmd->redo();

                    }
                    else
                    {
                        qint64 l = qMin(this->_len, this->owner()->length());

                        this->_remcmd = this->owner()->internalRemove(this->pos(), l, QHexEditData::Replace);

                        if(this->_remcmd)
                        {
                            this->_remcmd->setNotify(false); /* Do not emit signal */
                            this->_remcmd->redo();
                        }

                        this->_inscmd = this->owner()->internalInsert(this->pos(), this->_data, QHexEditData::Replace);

                        if(this->_inscmd)
                        {
                            this->_inscmd->setNotify(false); /* Do not emit signal */
                            this->_inscmd->redo();
                        }
                    }

                    if(this->canNotify())
                        this->notifyDataChanged(this->pos(), this->_data.length(), QHexEditData::Replace);
                }

            private:
                qint64 _len;
                QByteArray _data;
                RemoveCommand* _remcmd;
                InsertCommand* _inscmd;
        };

    private:
        explicit QHexEditData(QIODevice* iodevice, QObject *parent = 0);
        ~QHexEditData();

    public:
        QUndoStack* undoStack();
        QIODevice* device();
        uchar at(qint64 pos);
        qint64 indexOf(const QByteArray& ba, qint64 start);
        void append(const QByteArray& ba);
        void insert(qint64 pos, uchar ch);
        void insert(qint64 pos, const QByteArray& ba);
        void remove(qint64 pos, qint64 len);
        void replace(qint64 pos, uchar b);
        void replace(qint64 pos, qint64 len, uchar b);
        void replace(qint64 pos, const QByteArray& ba);
        void replace(qint64 pos, qint64 len, const QByteArray& ba);
        QByteArray read(qint64 pos, qint64 len);
        QString readString(qint64 pos, qint64 maxlen = -1);
        quint16 readUInt16(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        quint32 readUInt32(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        quint64 readUInt64(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        qint16 readInt16(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        qint32 readInt32(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        qint64 readInt64(qint64 pos, QSysInfo::Endian endian = QSysInfo::ByteOrder);
        qint64 length() const;

    public slots:
        bool save();
        bool saveTo(QIODevice* iodevice);

    public:
        static QHexEditData* fromDevice(QIODevice* iodevice);
        static QHexEditData* fromFile(QString filename);
        static QHexEditData* fromMemory(const QByteArray& ba);

    private:
        InsertCommand* internalInsert(qint64 pos, const QByteArray& ba, QHexEditData::ActionType act);
        RemoveCommand* internalRemove(qint64 pos, qint64 len, QHexEditData::ActionType); /* TODO: QHexEditData::internalRemove(): Optimization Needed */
        QHexEditData::ModifiedItem *modifiedItem(qint64 pos, qint64 *datapos = nullptr, int* index = nullptr);
        qint64 updateBuffer(const QByteArray& ba);
        void bufferizeData(qint64 pos);
        bool inBuffer(qint64 pos);
        bool needsBuffering(qint64 pos);
        bool canOptimize(QHexEditData::ActionType at, qint64 pos);
        void recordAction(QHexEditData::ActionType at, qint64 pos);

    signals:
        void dataChanged(qint64 offset, qint64 size, QHexEditData::ActionType reason);

    private:        
        static const qint64 BUFFER_SIZE;
        bool _dirtybuffer;
        ModifyList _modlist;
        QIODevice* _iodevice;
        QByteArray _modbuffer;
        QByteArray _buffereddata;
        qint64 _buffereddatapos;
        qint64 _length;
        qint64 _devicelength;
        qint64 _lastpos;
        QHexEditData::ActionType _lastaction;
        QHexEditData::QHexEditDataDevice _hexeditdatadevice;
        QUndoStack _undostack;
        QReadWriteLock _rwlock;

    friend class QHexEditData::AbstractCommand;
};

#endif // QHEXEDITDATA_H
