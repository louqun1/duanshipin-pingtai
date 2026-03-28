/****************************************************************************
** Meta object code from reading C++ file 'HttpFlvStreamReader.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../backend/liveplayer/protocol/HttpFlvStreamReader.hpp"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'HttpFlvStreamReader.hpp' doesn't include <QObject>."
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
struct qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t {};
} // unnamed namespace

template <> constexpr inline auto backend::liveplayer::protocol::HttpFlvStreamReader::qt_create_metaobjectdata<qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "backend::liveplayer::protocol::HttpFlvStreamReader",
        "connected",
        "",
        "contentType",
        "statusCode",
        "dataChunkReceived",
        "chunk",
        "finished",
        "errorOccurred",
        "message",
        "logMessage",
        "handleMetaDataChanged",
        "handleReadyRead",
        "handleFinished"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'connected'
        QtMocHelpers::SignalData<void(const QString &, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'dataChunkReceived'
        QtMocHelpers::SignalData<void(const QByteArray &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 6 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'logMessage'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Slot 'handleMetaDataChanged'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'handleReadyRead'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'handleFinished'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<HttpFlvStreamReader, qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject backend::liveplayer::protocol::HttpFlvStreamReader::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>.metaTypes,
    nullptr
} };

void backend::liveplayer::protocol::HttpFlvStreamReader::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<HttpFlvStreamReader *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->connected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->dataChunkReceived((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 2: _t->finished(); break;
        case 3: _t->errorOccurred((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->logMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->handleMetaDataChanged(); break;
        case 6: _t->handleReadyRead(); break;
        case 7: _t->handleFinished(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (HttpFlvStreamReader::*)(const QString & , int )>(_a, &HttpFlvStreamReader::connected, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (HttpFlvStreamReader::*)(const QByteArray & )>(_a, &HttpFlvStreamReader::dataChunkReceived, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (HttpFlvStreamReader::*)()>(_a, &HttpFlvStreamReader::finished, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (HttpFlvStreamReader::*)(const QString & )>(_a, &HttpFlvStreamReader::errorOccurred, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (HttpFlvStreamReader::*)(const QString & )>(_a, &HttpFlvStreamReader::logMessage, 4))
            return;
    }
}

const QMetaObject *backend::liveplayer::protocol::HttpFlvStreamReader::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *backend::liveplayer::protocol::HttpFlvStreamReader::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7backend10liveplayer8protocol19HttpFlvStreamReaderE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int backend::liveplayer::protocol::HttpFlvStreamReader::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void backend::liveplayer::protocol::HttpFlvStreamReader::connected(const QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void backend::liveplayer::protocol::HttpFlvStreamReader::dataChunkReceived(const QByteArray & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void backend::liveplayer::protocol::HttpFlvStreamReader::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void backend::liveplayer::protocol::HttpFlvStreamReader::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void backend::liveplayer::protocol::HttpFlvStreamReader::logMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}
QT_WARNING_POP
