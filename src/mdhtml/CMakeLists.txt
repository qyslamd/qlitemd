set(MDHTML_TARGET qlitemd_mdhtml)

add_library(${MDHTML_TARGET} STATIC
    md_assembler.cpp
    md_assembler.h
    md_parser.cpp
    md_parser.h
    md_text.cpp
    md_text.h
    md_types.h
)

target_include_directories(${MDHTML_TARGET}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(${MDHTML_TARGET} PRIVATE md4c)
target_compile_features(${MDHTML_TARGET} PUBLIC cxx_std_17)
set_target_properties(${MDHTML_TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(${MDHTML_TARGET} PRIVATE /utf-8 /W4 /WX- /EHsc)
endif()

add_library(qlitemd::mdhtml ALIAS ${MDHTML_TARGET})
