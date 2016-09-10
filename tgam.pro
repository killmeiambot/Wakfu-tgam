
QT       += core gui

CONFIG(debug, debug|release): TARGET  = tgamd
CONFIG(release, debug|release): TARGET  = tgam



TEMPLATE    = lib
CONFIG     += plugin
DESTDIR = $$[QT_INSTALL_PLUGINS]/imageformats

PLUGIN_TYPE = imageformats
PLUGIN_CLASS_NAME = QTGAMPlugin
QTDIR_build:REQUIRES = "!contains(QT_CONFIG, no-tgam)"

HEADERS += qtgamhandler.h   main.h
SOURCES += qtgamhandler.cpp main.cpp 
OTHER_FILES += tgam.json
