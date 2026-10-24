cmake_minimum_required(VERSION 3.22)
project(KGlanceQT VERSION 0.3.0 LANGUAGES CXX)
set(KGLANCEQT_VERSION "${PROJECT_VERSION}-alpha.1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Xml DBus Concurrent MultimediaWidgets Network)
find_package(KF6Parts 6.0 REQUIRED)
find_package(KF6TextEditor 6.0 REQUIRED)
find_package(KF6KIO 6.0 REQUIRED)
find_package(KF6Archive 6.0 REQUIRED)
find_package(KF6WindowSystem 6.0 REQUIRED)
find_package(KF6BalooWidgets REQUIRED)
find_package(KF6FileMetaData 6.2 REQUIRED)
find_package(KF6WidgetsAddons 6.0 REQUIRED)

add_executable(kglanceqt src/main.cpp src/dolphinsetup.cpp src/dolphinsetup.h
    src/previewservice.h src/previewwindow.cpp src/previewwindow.h
    src/providers.cpp src/providers.h src/imageview.cpp src/imageview.h
    src/mediaview.cpp src/mediaview.h src/previewsurface.cpp src/previewsurface.h
    src/markdownview.cpp src/markdownview.h src/informationpanel.cpp src/informationpanel.h
    src/preferences.cpp src/preferences.h src/supportedtypesdialog.cpp src/supportedtypesdialog.h
    src/selectionfilmstrip.cpp src/selectionfilmstrip.h src/viewposition.cpp src/viewposition.h
    src/filedragbutton.cpp src/filedragbutton.h src/imageanimation.h
    src/filemonitor.cpp src/filemonitor.h src/imagecomparison.cpp src/imagecomparison.h
    src/filetriage.cpp src/filetriage.h src/previewtriage.cpp
    src/metadataeditor.cpp src/metadataeditor.h src/tableview.cpp src/tableview.h
    src/remotepreview.cpp src/remotepreview.h)
target_compile_options(kglanceqt PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(kglanceqt PRIVATE KGLANCEQT_VERSION="${KGLANCEQT_VERSION}")
target_link_libraries(kglanceqt PRIVATE Qt6::Widgets Qt6::Xml Qt6::DBus Qt6::Concurrent
    Qt6::MultimediaWidgets KF6::Parts KF6::TextEditor KF6::KIOWidgets
    KF6::Archive KF6::WindowSystem KF6::BalooWidgets KF6::WidgetsAddons KF6::FileMetaData)
install(TARGETS kglanceqt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(data/org.kglanceqt.KGlanceQT.desktop.in org.kglanceqt.KGlanceQT.desktop @ONLY)
configure_file(data/kglanceqt.desktop.in kglanceqt.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kglanceqt.KGlanceQT.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/kglanceqt.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES LICENSE ATTRIBUTION.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kglanceqt)
install(FILES README.md ROADMAP.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/kglanceqt)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    add_executable(provider_tests tests/provider_tests.cpp src/providers.cpp src/providers.h)
    target_include_directories(provider_tests PRIVATE src)
    target_link_libraries(provider_tests PRIVATE Qt6::Test Qt6::Gui KF6::Archive)
    add_test(NAME providers COMMAND provider_tests)
    add_executable(filemonitor_tests tests/filemonitor_tests.cpp src/filemonitor.cpp src/filemonitor.h)
    target_include_directories(filemonitor_tests PRIVATE src)
    target_link_libraries(filemonitor_tests PRIVATE Qt6::Test)
    add_test(NAME filemonitor COMMAND filemonitor_tests)
    add_executable(filetriage_tests tests/filetriage_tests.cpp src/filetriage.cpp src/filetriage.h)
    target_include_directories(filetriage_tests PRIVATE src)
    target_link_libraries(filetriage_tests PRIVATE Qt6::Test Qt6::Widgets KF6::KIOWidgets)
    add_executable(window_tests tests/window_tests.cpp src/previewwindow.cpp src/previewwindow.h
        src/providers.cpp src/providers.h src/imageview.cpp src/imageview.h
        src/mediaview.cpp src/mediaview.h src/previewsurface.cpp src/previewsurface.h
        src/markdownview.cpp src/markdownview.h src/informationpanel.cpp src/informationpanel.h
        src/preferences.cpp src/preferences.h src/dolphinsetup.cpp src/dolphinsetup.h
        src/supportedtypesdialog.cpp src/supportedtypesdialog.h
        src/selectionfilmstrip.cpp src/selectionfilmstrip.h src/viewposition.cpp src/viewposition.h
        src/filedragbutton.cpp src/filedragbutton.h src/imageanimation.h
        src/filemonitor.cpp src/filemonitor.h src/imagecomparison.cpp src/imagecomparison.h
        src/filetriage.cpp src/filetriage.h src/previewtriage.cpp
        src/metadataeditor.cpp src/metadataeditor.h src/tableview.cpp src/tableview.h
        src/remotepreview.cpp src/remotepreview.h)
    target_include_directories(window_tests PRIVATE src)
    target_link_libraries(window_tests PRIVATE Qt6::Test Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::Network
        Qt6::MultimediaWidgets KF6::Parts KF6::TextEditor KF6::KIOWidgets
        KF6::Archive KF6::WindowSystem KF6::BalooWidgets KF6::WidgetsAddons KF6::FileMetaData)
    if(DBUS_RUN_SESSION)
        add_test(NAME window COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:window_tests>)
    else()
        add_test(NAME window COMMAND window_tests)
    endif()
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test-config ${CMAKE_CURRENT_BINARY_DIR}/test-desktop)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test-config/user-dirs.dirs
        "XDG_DESKTOP_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/test-desktop\"\n")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/test-config/kwalletrc "[Wallet]\nEnabled=false\n")
    set_tests_properties(window PROPERTIES TIMEOUT 90)
    set_tests_properties(window PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-cache")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(shortcut_tests tests/shortcut_tests.cpp src/dolphinsetup.cpp src/dolphinsetup.h)
    target_include_directories(shortcut_tests PRIVATE src)
    target_link_libraries(shortcut_tests PRIVATE Qt6::Test Qt6::Gui Qt6::Xml)
    add_test(NAME shortcut COMMAND shortcut_tests)
    find_program(GDBUS gdbus)
    find_program(QDBUS qdbus6)
    if(DBUS_RUN_SESSION)
        add_test(NAME filetriage COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:filetriage_tests>)
        set_tests_properties(filetriage PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-trash-config;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-trash-data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-trash-cache")
        set_tests_properties(filetriage PROPERTIES TIMEOUT 60)
    endif()
    if(DBUS_RUN_SESSION AND GDBUS AND QDBUS)
        add_test(NAME dbus COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus_smoke.py $<TARGET_FILE:kglanceqt>)
        set_tests_properties(dbus PROPERTIES TIMEOUT 30)
    endif()
endif()
