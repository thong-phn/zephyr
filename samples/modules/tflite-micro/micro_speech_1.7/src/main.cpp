#include "main_functions.hpp"

int main(int argc, char *argv[])
{
    setup();
    
    // Keep running the loop indefinitely for OpenAMP communication
    while (1) {
        loop();
    }
    
    return 0;
}