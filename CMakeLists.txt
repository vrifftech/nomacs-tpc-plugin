cmake_minimum_required(VERSION 3.21)

project(QtTpcImagePlugin VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui)

qt_add_plugin(qtpcimageplugin
    CLASS_NAME TpcPlugin
)

target_sources(qtpcimageplugin PRIVATE
    src/tpcdecoder.cpp
    src/tpcdecoder.h
    src/tpchandler.cpp
    src/tpchandler.h
    src/tpcplugin.cpp
    src/tpcplugin.h
    src/tpc.json
)

set_target_properties(qtpcimageplugin PROPERTIES OUTPUT_NAME qtpc)

target_link_libraries(qtpcimageplugin PRIVATE Qt6::Core Qt6::Gui)
target_include_directories(qtpcimageplugin PRIVATE src)

include(GNUInstallDirs)
install(TARGETS qtpcimageplugin
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/imageformats"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/imageformats"
)


endif()
