set(MYLIBS wolverine::fmt-header-only wolverine::headers)

add_wolverine_library(
    NAME csstock
    USER nickchenyj
    TYPE signal
    SRCS csstock.cpp
    LIBS ${MYLIBS}
)
