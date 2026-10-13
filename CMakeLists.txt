cmake_minimum_required(VERSION 3.16)
project(LogisticsInventory LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Sql Widgets)

add_library(logistics_database
    src/db/database.cpp
    src/db/database.h
    src/db/migrations/migrations.qrc
)
target_include_directories(logistics_database PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(logistics_database PUBLIC Qt6::Core Qt6::Sql)

add_library(logistics_master_data
    src/core/models.h
    src/core/result.h
    src/repositories/document_repository.cpp
    src/repositories/document_repository.h
    src/repositories/inventory_repository.cpp
    src/repositories/inventory_repository.h
    src/repositories/master_data_repository.cpp
    src/repositories/master_data_repository.h
    src/services/inbound_service.cpp
    src/services/inbound_service.h
    src/services/inventory_query_service.cpp
    src/services/inventory_query_service.h
    src/services/logistics_service.cpp
    src/services/logistics_service.h
    src/services/report_service.cpp
    src/services/report_service.h
    src/services/master_data_service.cpp
    src/services/master_data_service.h
)
target_include_directories(logistics_master_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(logistics_master_data PUBLIC logistics_database Qt6::Core Qt6::Sql)

add_library(logistics_ui
    src/app/application.cpp
    src/app/application.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/dialogs/inbound_dialog.cpp
    src/ui/dialogs/inbound_dialog.h
    src/ui/dialogs/logistics_dialog.cpp
    src/ui/dialogs/logistics_dialog.h
    src/ui/dialogs/master_data_dialog.cpp
    src/ui/dialogs/master_data_dialog.h
    src/ui/pages/batch_tracking_page.cpp
    src/ui/pages/batch_tracking_page.h
    src/ui/pages/dashboard_page.cpp
    src/ui/pages/dashboard_page.h
    src/ui/pages/inbound_page.cpp
    src/ui/pages/inbound_page.h
    src/ui/pages/inventory_page.cpp
    src/ui/pages/inventory_page.h
    src/ui/pages/logistics_page.cpp
    src/ui/pages/logistics_page.h
    src/ui/pages/master_data_page.cpp
    src/ui/pages/master_data_page.h
    src/ui/pages/reports_page.cpp
    src/ui/pages/reports_page.h
)
target_include_directories(logistics_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(logistics_ui PUBLIC logistics_master_data Qt6::Widgets)

add_executable(LogisticsInventory WIN32 src/app/main.cpp)
target_link_libraries(LogisticsInventory PRIVATE logistics_ui)

install(TARGETS LogisticsInventory RUNTIME DESTINATION .)
qt_generate_deploy_app_script(
    TARGET LogisticsInventory
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
