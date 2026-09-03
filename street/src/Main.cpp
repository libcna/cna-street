// SPDX-License-Identifier: MIT
#include "CnaStreet/StreetApplication.hpp"

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    try
    {
        CnaStreet::StreetApplication application;
        if (!application.configure(argc, argv)) return 0;
        application.Run();
        return 0;
    }
    catch (const std::exception& failure)
    {
        // Anything that escapes this far is a start-up failure the user needs
        // named rather than a silent exit code: a missing asset, a shader that
        // will not compile, a device the renderer cannot create.
        std::fprintf(stderr, "cna-street: %s\n", failure.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "cna-street: an unknown error occurred during start-up\n");
        return 1;
    }
}
