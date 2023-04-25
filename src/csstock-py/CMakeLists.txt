set(SIG_USERNAME nickchenyj)
set(SIG_NAME csstock)

add_wolverine_library(
    NAME ${SIG_USERNAME}-${SIG_NAME}_py
    TYPE pysig
    SRCS ${SIG_NAME}.py
)
