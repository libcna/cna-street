# Warning policy for cna-street's own targets only.
#
# Deliberately not applied globally: CNA, sharp-runtime, easy-gl and the
# vendored SDL are compiled under their own projects' settings, and turning a
# stricter policy on for them would make this project's build fail on someone
# else's code.
include_guard(GLOBAL)

add_library(cna_street_warnings INTERFACE)
add_library(CnaStreet::Warnings ALIAS cna_street_warnings)

if(MSVC)
    target_compile_options(cna_street_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(cna_street_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wdouble-promotion -Wformat=2)
endif()

option(CNA_STREET_WERROR "Treat cna-street's own warnings as errors" OFF)
if(CNA_STREET_WERROR)
    if(MSVC)
        target_compile_options(cna_street_warnings INTERFACE /WX)
    else()
        target_compile_options(cna_street_warnings INTERFACE -Werror)
    endif()
endif()
