find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(ExWidgetsNavigationHintTests tst_navigationhint.cpp)
target_link_libraries(ExWidgetsNavigationHintTests PRIVATE
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME ExWidgetsNavigationHintTests COMMAND ExWidgetsNavigationHintTests)

add_executable(ExWidgetsColorPickerDialogTests tst_colorpickerdialog.cpp)
target_link_libraries(ExWidgetsColorPickerDialogTests PRIVATE
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME ExWidgetsColorPickerDialogTests COMMAND ExWidgetsColorPickerDialogTests)

add_executable(ExWidgetsCarouselTests tst_carousel.cpp)
target_link_libraries(ExWidgetsCarouselTests PRIVATE
    ExWidgets::ExWidgets
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME ExWidgetsCarouselTests COMMAND ExWidgetsCarouselTests)
