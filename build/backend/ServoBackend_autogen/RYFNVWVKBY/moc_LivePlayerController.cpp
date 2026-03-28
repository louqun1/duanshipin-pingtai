/****************************************************************************
** Meta object code from reading C++ file 'LivePlayerController.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../backend/liveplayer/service/LivePlayerController.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'LivePlayerController.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto backend::liveplayer::service::LivePlayerController::qt_create_metaobjectdata<qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "backend::liveplayer::service::LivePlayerController",
        "playbackStateChanged",
        "",
        "PlaybackState",
        "state",
        "message",
        "streamUrlChanged",
        "url",
        "sessionLogAppended",
        "streamStatsChanged",
        "bytesReceived",
        "audioTagCount",
        "videoTagCount",
        "scriptTagCount",
        "attachVideoSurface",
        "QWidget*",
        "surface",
        "openStream",
        "stopStream",
        "handleSessionStateChanged",
        "backend::liveplayer::session::LivePlayerSession::SessionState",
        "Idle",
        "Connecting",
        "Reading",
        "Playing",
        "Stopped",
        "Error"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'playbackStateChanged'
        QtMocHelpers::SignalData<void(enum PlaybackState, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::QString, 5 },
        }}),
        // Signal 'streamUrlChanged'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Signal 'sessionLogAppended'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Signal 'streamStatsChanged'
        QtMocHelpers::SignalData<void(qint64, int, int, int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 10 }, { QMetaType::Int, 11 }, { QMetaType::Int, 12 }, { QMetaType::Int, 13 },
        }}),
        // Slot 'attachVideoSurface'
        QtMocHelpers::SlotData<void(QWidget *)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 16 },
        }}),
        // Slot 'openStream'
        QtMocHelpers::SlotData<void(const QString &)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Slot 'stopStream'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'handleSessionStateChanged'
        QtMocHelpers::SlotData<void(backend::liveplayer::session::LivePlayerSession::SessionState, const QString &)>(19, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 20, 4 }, { QMetaType::QString, 5 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'PlaybackState'
        QtMocHelpers::EnumData<enum PlaybackState>(3, 3, QMC::EnumIsScoped).add({
            {   21, PlaybackState::Idle },
            {   22, PlaybackState::Connecting },
            {   23, PlaybackState::Reading },
            {   24, PlaybackState::Playing },
            {   25, PlaybackState::Stopped },
            {   26, PlaybackState::Error },
        }),
    };
    return QtMocHelpers::metaObjectData<LivePlayerController, qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject backend::liveplayer::service::LivePlayerController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>.metaTypes,
    nullptr
} };

void backend::liveplayer::service::LivePlayerController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<LivePlayerController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->playbackStateChanged((*reinterpret_cast<std::add_pointer_t<enum PlaybackState>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->streamUrlChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->sessionLogAppended((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->streamStatsChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 4: _t->attachVideoSurface((*reinterpret_cast<std::add_pointer_t<QWidget*>>(_a[1]))); break;
        case 5: _t->openStream((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->stopStream(); break;
        case 7: _t->handleSessionStateChanged((*reinterpret_cast<std::add_pointer_t<backend::liveplayer::session::LivePlayerSession::SessionState>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (LivePlayerController::*)(PlaybackState , const QString & )>(_a, &LivePlayerController::playbackStateChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (LivePlayerController::*)(const QString & )>(_a, &LivePlayerController::streamUrlChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (LivePlayerController::*)(const QString & )>(_a, &LivePlayerController::sessionLogAppended, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (LivePlayerController::*)(qint64 , int , int , int )>(_a, &LivePlayerController::streamStatsChanged, 3))
            return;
    }
}

const QMetaObject *backend::liveplayer::service::LivePlayerController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *backend::liveplayer::service::LivePlayerController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer7service20LivePlayerControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int backend::liveplayer::service::LivePlayerController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void backend::liveplayer::service::LivePlayerController::playbackStateChanged(PlaybackState _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void backend::liveplayer::service::LivePlayerController::streamUrlChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void backend::liveplayer::service::LivePlayerController::sessionLogAppended(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void backend::liveplayer::service::LivePlayerController::streamStatsChanged(qint64 _t1, int _t2, int _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4);
}
QT_WARNING_POP
