#include "app/app.h"

int main(int argc, char** argv) {
    // create application
    auto& application = App::instance();
    // configure the application (command line parsing, logging, platform setup)
    application.initialize(argc, argv);
    // create the main window and run the application event loop
    return application.run();
}
