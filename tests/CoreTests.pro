QT += core sql
QT -= gui

TEMPLATE = app
TARGET = mhgu_core_tests
CONFIG += console c++17

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
INCLUDEPATH += $$PWD/../src

SOURCES += core_tests.cpp ../src/mhgu_save.cpp ../src/game_data.cpp ../src/transfer_forms.cpp \
    ../src/loadout_search.cpp ../src/loadout_format.cpp
HEADERS += ../src/mhgu_save.hpp ../src/game_data.hpp ../src/transfer_forms.hpp \
    ../src/loadout_search.hpp ../src/loadout_format.hpp
