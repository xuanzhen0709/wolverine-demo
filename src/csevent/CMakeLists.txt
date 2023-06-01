set(MYLIBS wolverine::fmt-header-only wolverine::headers)

add_wolverine_library(
    NAME csevent
    USER nickchenyj
    TYPE signal
    SRCS csevent.cpp
    LIBS ${MYLIBS}
)
