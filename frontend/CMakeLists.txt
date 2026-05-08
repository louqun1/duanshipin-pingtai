add_library(FrontendWidgets
    FrontendWidgets.qrc
    linkmic/LinkMicTypes.hpp
    linkmic/RoomMemberDirectory.hpp
    linkmic/RoomMemberDirectory.cpp
    linkmic/LinkMicRoomState.hpp
    linkmic/LinkMicRoomState.cpp
    linkmic/LinkMicSession.hpp
    linkmic/LinkMicSession.cpp
    linkmic/LiveRoomSignalingClient.hpp
    linkmic/LiveRoomSignalingClient.cpp
    widgets/MainWindow.hpp
    widgets/MainWindow.cpp
    widgets/VideoOpenGLWidget.hpp
    widgets/VideoOpenGLWidget.cpp
    widgets/VideoPlayerWindow.hpp
    widgets/VideoPlayerWindow.cpp
    components/VideoCard/VideoCard.hpp
    components/VideoCard/VideoCard.cpp
    pages/AccountPage/AccountPage.hpp
    pages/AccountPage/AccountPage.cpp
    pages/HomePage/HomePage.hpp
    pages/HomePage/HomePage.cpp
    pages/StreamPage/StreamPage.hpp
    pages/StreamPage/StreamPage.cpp
    pages/UploadPage/UploadPage.hpp
    pages/UploadPage/UploadPage.cpp
)

find_package(Qt6WebSockets QUIET)
if(TARGET Qt6::WebSockets)
    message(STATUS "FrontendWidgets: Qt6 WebSockets enabled for StreamPage business signaling.")
else()
    message(WARNING
        "Qt6 WebSockets was not found. StreamPage will keep HTTP-FLV playback, "
        "but business WebSocket linkmic signaling will stay disabled in this build.")
endif()

target_include_directories(FrontendWidgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(FrontendWidgets
    PRIVATE
        ServoBackend
    PUBLIC
        Qt6::Network
        Qt6::Widgets
        Qt6::OpenGLWidgets
)

if(TARGET Qt6::WebSockets)
    target_link_libraries(FrontendWidgets PUBLIC Qt6::WebSockets)
    target_compile_definitions(FrontendWidgets PRIVATE FLASHPOINT_HAS_QT_WEBSOCKETS=1)
endif()
