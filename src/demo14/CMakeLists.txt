set(SIG_USERNAME nickchenyj)
set(SIG_NAME demo14)

set(MYSRCS ${SIG_NAME}.cpp)
set(MYLIBS wolverine::fmt-header-only wolverine::headers)

add_wolverine_library(
    NAME ${SIG_USERNAME}-${SIG_NAME}
    TYPE signal
    SRCS ${MYSRCS}
    LIBS ${MYLIBS}
)
