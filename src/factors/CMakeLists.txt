add_wolverine_library(
    NAME factor
    USER nickchenyj
    TYPE signal
    SRCS factor.cpp
)

add_wolverine_library(
    NAME strat
    USER nickchenyj
    TYPE signal
    SRCS strat.cpp
)

add_wolverine_library(
    NAME stratpy
    USER nickchenyj
    TYPE pysig
    SRCS main.py
)