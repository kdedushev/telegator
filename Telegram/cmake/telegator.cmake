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
    telegator/telegator_requisites.cpp
    telegator/telegator_requisites.h
)
