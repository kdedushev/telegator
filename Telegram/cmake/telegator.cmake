# Telegator sources: everything of our own lives in SourceFiles/telegator.

nice_target_sources(Telegram ${src_loc}
PRIVATE
    telegator/telegator_compose_buttons.cpp
    telegator/telegator_compose_buttons.h
    telegator/telegator_config.cpp
    telegator/telegator_config.h
    telegator/telegator_field.cpp
    telegator/telegator_field.h
    telegator/telegator_id_search.cpp
    telegator/telegator_id_search.h
    telegator/telegator_mono_copy.cpp
    telegator/telegator_mono_copy.h
    telegator/telegator_panel.cpp
    telegator/telegator_panel.h
    telegator/telegator_quick_replies.cpp
    telegator/telegator_quick_replies.h
    telegator/telegator_requisites_format.cpp
    telegator/telegator_requisites_format.h
)

# Built-in "Реквизиты" against their reference vectors: see TELEGATOR.md.
add_executable(test_telegator_requisites)
init_target(test_telegator_requisites "(tests)")

target_include_directories(test_telegator_requisites PRIVATE ${src_loc})

nice_target_sources(test_telegator_requisites ${src_loc}
PRIVATE
    telegator/telegator_requisites_format.cpp
    telegator/telegator_requisites_format.h
    telegator/telegator_requisites_format_test.cpp
)

target_link_libraries(test_telegator_requisites
PRIVATE
    desktop-app::lib_base
    desktop-app::external_qt
)

set_target_properties(test_telegator_requisites PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
