cmake_minimum_required(VERSION 3.29)
project(DoomGVC)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(DoomGVC
    ErrorHandle.cpp
    ErrorHandle.h
    Lexer.cpp
    Lexer.h
    main.cpp
    Parser.cpp
    Parser.h
    Token.cpp
    Token.h)
