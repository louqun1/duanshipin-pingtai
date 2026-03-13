add_library(ServoBackend STATIC
    domain/user/User.hpp
    repository/user/UserRepository.hpp
    infrastructure/database/InMemoryUserRepository.hpp
    infrastructure/database/InMemoryUserRepository.cpp
    service/auth/AuthResult.hpp
    service/auth/AuthService.hpp
    service/auth/AuthService.cpp
    controller/auth/AuthController.hpp
    controller/auth/AuthController.cpp
)

target_include_directories(ServoBackend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServoBackend
    PUBLIC
        Qt6::Core
)
