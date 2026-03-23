/****************************************************************************
** Meta object code from reading C++ file 'PlayerController.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../backend/playercontroller/service/PlayerController.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PlayerController.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto backend::playercontroller::service::PlayerController::qt_create_metaobjectdata<qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "backend::playercontroller::service::PlayerController",
        "mediaChanged",
        "",
        "videoId",
        "title",
        "creator",
        "duration",
        "playbackStateChanged",
        "PlaybackState",
        "state",
        "message",
        "playbackProgressChanged",
        "positionMs",
        "durationMs",
        "playbackVolumeChanged",
        "volume",
        "muted",
        "ijkPlayerCreated",
        "ijkPlayerOpenRequested",
        "attachVideoSurface",
        "QWidget*",
        "surface",
        "openMedia",
        "requestPlay",
        "requestPause",
        "requestTogglePlayback",
        "requestSeek",
        "requestSetVolume",
        "requestToggleMute",
        "requestStop",
        "releasePlaybackResources",
        "Idle",
        "Opening",
        "Prepared",
        "Playing",
        "Paused",
        "Stopped",
        "Error"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'mediaChanged'
        QtMocHelpers::SignalData<void(const QString &, const QString &, const QString &, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 4 }, { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'playbackStateChanged'
        QtMocHelpers::SignalData<void(enum PlaybackState, const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 }, { QMetaType::QString, 10 },
        }}),
        // Signal 'playbackProgressChanged'
        QtMocHelpers::SignalData<void(qint64, qint64)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 12 }, { QMetaType::LongLong, 13 },
        }}),
        // Signal 'playbackVolumeChanged'
        QtMocHelpers::SignalData<void(int, bool)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 }, { QMetaType::Bool, 16 },
        }}),
        // Signal 'ijkPlayerCreated'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'ijkPlayerOpenRequested'
        QtMocHelpers::SignalData<void(const QString &, const QString &)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 4 },
        }}),
        // Slot 'attachVideoSurface'
        QtMocHelpers::SlotData<void(QWidget *)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 21 },
        }}),
        // Slot 'openMedia'
        QtMocHelpers::SlotData<void(const QString &, const QString &, const QString &, const QString &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 4 }, { QMetaType::QString, 5 }, { QMetaType::QString, 6 },
        }}),
        // Slot 'requestPlay'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestPause'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestTogglePlayback'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestSeek'
        QtMocHelpers::SlotData<void(int)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Slot 'requestSetVolume'
        QtMocHelpers::SlotData<void(int)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Slot 'requestToggleMute'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestStop'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'releasePlaybackResources'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'PlaybackState'
        QtMocHelpers::EnumData<enum PlaybackState>(8, 8, QMC::EnumIsScoped).add({
            {   31, PlaybackState::Idle },
            {   32, PlaybackState::Opening },
            {   33, PlaybackState::Prepared },
            {   34, PlaybackState::Playing },
            {   35, PlaybackState::Paused },
            {   36, PlaybackState::Stopped },
            {   37, PlaybackState::Error },
        }),
    };
    return QtMocHelpers::metaObjectData<PlayerController, qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject backend::playercontroller::service::PlayerController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>.metaTypes,
    nullptr
} };

void backend::playercontroller::service::PlayerController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PlayerController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->mediaChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 1: _t->playbackStateChanged((*reinterpret_cast<std::add_pointer_t<enum PlaybackState>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->playbackProgressChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2]))); break;
        case 3: _t->playbackVolumeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 4: _t->ijkPlayerCreated(); break;
        case 5: _t->ijkPlayerOpenRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 6: _t->attachVideoSurface((*reinterpret_cast<std::add_pointer_t<QWidget*>>(_a[1]))); break;
        case 7: _t->openMedia((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 8: _t->requestPlay(); break;
        case 9: _t->requestPause(); break;
        case 10: _t->requestTogglePlayback(); break;
        case 11: _t->requestSeek((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 12: _t->requestSetVolume((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->requestToggleMute(); break;
        case 14: _t->requestStop(); break;
        case 15: _t->releasePlaybackResources(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)(const QString & , const QString & , const QString & , const QString & )>(_a, &PlayerController::mediaChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)(PlaybackState , const QString & )>(_a, &PlayerController::playbackStateChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)(qint64 , qint64 )>(_a, &PlayerController::playbackProgressChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)(int , bool )>(_a, &PlayerController::playbackVolumeChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)()>(_a, &PlayerController::ijkPlayerCreated, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (PlayerController::*)(const QString & , const QString & )>(_a, &PlayerController::ijkPlayerOpenRequested, 5))
            return;
    }
}

const QMetaObject *backend::playercontroller::service::PlayerController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *backend::playercontroller::service::PlayerController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend16playercontroller7service16PlayerControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int backend::playercontroller::service::PlayerController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 16)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 16;
    }
    return _id;
}

// SIGNAL 0
void backend::playercontroller::service::PlayerController::mediaChanged(const QString & _t1, const QString & _t2, const QString & _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 1
void backend::playercontroller::service::PlayerController::playbackStateChanged(PlaybackState _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void backend::playercontroller::service::PlayerController::playbackProgressChanged(qint64 _t1, qint64 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void backend::playercontroller::service::PlayerController::playbackVolumeChanged(int _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void backend::playercontroller::service::PlayerController::ijkPlayerCreated()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void backend::playercontroller::service::PlayerController::ijkPlayerOpenRequested(const QString & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}
QT_WARNING_POP
