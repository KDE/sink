# - Try to find readline include dirs and libraries 
#
# Usage of this module as follows:
#
#     find_package(Readline)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  Readline_ROOT_DIR         Set this variable to the root installation of
#                            readline if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  READLINE_FOUND            System has readline, include and lib dirs found
#  Readline_INCLUDE_DIR      The readline include directories. 
#  Readline_LIBRARY          The readline library.

find_path(Readline_ROOT_DIR
   NAMES include/readline/readline.h
   )

find_path(Readline_INCLUDE_DIR
   NAMES readline/readline.h
   HINTS ${Readline_ROOT_DIR}/include
   )

find_library(Readline_LIBRARY
   NAMES readline
   HINTS ${Readline_ROOT_DIR}/lib
   )

set(Readline_VERSION Readline_VERSION-NOTFOUND)
if (Readline_INCLUDE_DIR)
  if(EXISTS "${Readline_INCLUDE_DIR}/readline/readline.h")
    file(STRINGS "${Readline_INCLUDE_DIR}/readline/readline.h" _Readline_HEADER_CONTENTS REGEX "#define RL_VERSION_[A-Z]+")
    string(REGEX REPLACE ".*#define RL_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" Readline_VERSION_MAJOR "${_Readline_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define RL_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" Readline_VERSION_MINOR "${_Readline_HEADER_CONTENTS}")
    set(Readline_VERSION ${Readline_VERSION_MAJOR}.${Readline_VERSION_MINOR})
    unset(_Readline_HEADER_CONTENTS)
  endif()
endif()

find_package_handle_standard_args(Readline FOUND_VAR Readline_FOUND
    REQUIRED_VARS Readline_LIBRARY Readline_INCLUDE_DIR Readline_ROOT_DIR
    VERSION_VAR Readline_VERSION)
