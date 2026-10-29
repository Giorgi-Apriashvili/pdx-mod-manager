// Catch2 entry point for the application-layer tests: a QGuiApplication (the
// models use the clipboard and style hints) on the offscreen platform, so no
// window ever appears.

#include <catch2/catch_session.hpp>

#include <QGuiApplication>

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
