add_wolverine_library(
    NAME snapshot
    USER nickchenyj
    TYPE signal
    SRCS main.cpp
    LIBS wolverine::fmt-header-only wolverine::wl-headers
)
