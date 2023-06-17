set(MYLIBS wolverine::fmt-header-only wolverine::headers)

add_wolverine_library(
    NAME checkpoint
    USER nickchenyj
    TYPE signal
    SRCS main.cpp
    LIBS ${MYLIBS}
)
