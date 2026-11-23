add_executable(pdfview_tests
    main.cpp
    test_pdfview.cpp
    test_actions.cpp
    test_scroll_motion.cpp
    test_panel_scroll.cpp
)
target_include_directories(pdfview_tests PRIVATE ${PROJECT_SOURCE_DIR}/deps/doctest)
target_link_libraries(pdfview_tests PRIVATE pdfview PkgConfig::PDFVIEW_POPPLER)
pdfview_warnings(pdfview_tests)

add_test(NAME pdfview_tests COMMAND pdfview_tests)
set_tests_properties(pdfview_tests PROPERTIES
    ENVIRONMENT "PDFVIEW_TEST_TMP=${CMAKE_CURRENT_BINARY_DIR}/tmp")
