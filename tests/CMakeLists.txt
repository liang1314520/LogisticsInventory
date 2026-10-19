find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_database
    tst_database.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_database PRIVATE logistics_database Qt6::Test)
add_test(NAME database COMMAND tst_database)

add_executable(tst_master_data_service
    tst_master_data_service.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_master_data_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME master_data COMMAND tst_master_data_service)

add_executable(tst_inbound_service
    tst_inbound_service.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_inbound_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME inbound COMMAND tst_inbound_service)

add_executable(tst_logistics_service
    tst_logistics_service.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_logistics_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME logistics COMMAND tst_logistics_service)

add_executable(tst_inventory_queries
    tst_inventory_queries.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_inventory_queries PRIVATE logistics_master_data Qt6::Test)
target_compile_definitions(tst_inventory_queries PRIVATE SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME inventory_queries COMMAND tst_inventory_queries)

add_executable(tst_report_service tst_report_service.cpp)
target_link_libraries(tst_report_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME report_service COMMAND tst_report_service)

add_executable(tst_batch_workflow_service
    tst_batch_workflow_service.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_batch_workflow_service PRIVATE logistics_master_data Qt6::Test)
add_test(NAME batch_workflow_service COMMAND tst_batch_workflow_service)

add_executable(tst_workflow_bar
    tst_workflow_bar.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_workflow_bar PRIVATE logistics_ui Qt6::Test Qt6::Widgets)
add_test(NAME workflow_bar COMMAND tst_workflow_bar)

add_executable(tst_main_window
    tst_main_window.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_main_window PRIVATE logistics_ui Qt6::Test Qt6::Widgets)
add_test(NAME main_window COMMAND tst_main_window)

add_executable(tst_document_dialogs
    tst_document_dialogs.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_document_dialogs PRIVATE logistics_ui Qt6::Test Qt6::Widgets)
add_test(NAME document_dialogs COMMAND tst_document_dialogs)

add_executable(tst_query_pages
    tst_query_pages.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_query_pages PRIVATE logistics_ui Qt6::Test Qt6::Widgets)
add_test(NAME query_pages COMMAND tst_query_pages)

add_executable(tst_end_to_end
    tst_end_to_end.cpp
    test_support.cpp
    test_support.h
)
target_link_libraries(tst_end_to_end PRIVATE logistics_master_data Qt6::Test)
add_test(NAME end_to_end COMMAND tst_end_to_end)
