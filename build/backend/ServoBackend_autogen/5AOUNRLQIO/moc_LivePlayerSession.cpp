/****************************************************************************
** Meta object code from reading C++ file 'LivePlayerSession.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../backend/liveplayer/session/LivePlayerSession.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'LivePlayerSession.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t {};
} // unnamed namespace

template <> constexpr inline auto backend::liveplayer::session::LivePlayerSession::qt_create_metaobjectdata<qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "backend::liveplayer::session::LivePlayerSession",
        "stateChanged",
        "",
        "SessionState",
        "state",
        "message",
        "logMessage",
        "statsChanged",
        "bytesReceived",
        "audioTagCount",
        "videoTagCount",
        "scriptTagCount",
        "handleReaderConnected",
        "contentType",
        "statusCode",
        "handleReaderDataChunk",
        "chunk",
        "handleReaderError",
        "handleReaderFinished",
        "Idle",
        "Connecting",
        "Reading",
        "Playing",
        "Stopped",
        "Error"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'stateChanged'
        QtMocHelpers::SignalData<void(enum SessionState, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::QString, 5 },
        }}),
        // Signal 'logMessage'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Signal 'statsChanged'
        QtMocHelpers::SignalData<void(qint64, int, int, int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 8 }, { QMetaType::Int, 9 }, { QMetaType::Int, 10 }, { QMetaType::Int, 11 },
        }}),
        // Slot 'handleReaderConnected'
        QtMocHelpers::SlotData<void(const QString &, int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 13 }, { QMetaType::Int, 14 },
        }}),
        // Slot 'handleReaderDataChunk'
        QtMocHelpers::SlotData<void(const QByteArray &)>(15, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QByteArray, 16 },
        }}),
        // Slot 'handleReaderError'
        QtMocHelpers::SlotData<void(const QString &)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Slot 'handleReaderFinished'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'SessionState'
        QtMocHelpers::EnumData<enum SessionState>(3, 3, QMC::EnumIsScoped).add({
            {   19, SessionState::Idle },
            {   20, SessionState::Connecting },
            {   21, SessionState::Reading },
            {   22, SessionState::Playing },
            {   23, SessionState::Stopped },
            {   24, SessionState::Error },
        }),
    };
    return QtMocHelpers::metaObjectData<LivePlayerSession, qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject backend::liveplayer::session::LivePlayerSession::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>.metaTypes,
    nullptr
} };

void backend::liveplayer::session::LivePlayerSession::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<LivePlayerSession *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->stateChanged((*reinterpret_cast<std::add_pointer_t<enum SessionState>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->logMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->statsChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 3: _t->handleReaderConnected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->handleReaderDataChunk((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 5: _t->handleReaderError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->handleReaderFinished(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (LivePlayerSession::*)(SessionState , const QString & )>(_a, &LivePlayerSession::stateChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (LivePlayerSession::*)(const QString & )>(_a, &LivePlayerSession::logMessage, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (LivePlayerSession::*)(qint64 , int , int , int )>(_a, &LivePlayerSession::statsChanged, 2))
            return;
    }
}

const QMetaObject *backend::liveplayer::session::LivePlayerSession::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *backend::liveplayer::session::LivePlayerSession::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7session17LivePlayerSessionE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int backend::liveplayer::session::LivePlayerSession::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void backend::liveplayer::session::LivePlayerSession::stateChanged(SessionState _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void backend::liveplayer::session::LivePlayerSession::logMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void backend::liveplayer::session::LivePlayerSession::statsChanged(qint64 _t1, int _t2, int _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2, _t3, _t4);
}
QT_WARNING_POP
