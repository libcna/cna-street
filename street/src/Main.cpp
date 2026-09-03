// SPDX-License-Identifier: MIT
#include "CnaStreet/StreetApplication.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        CnaStreet::StreetApplication app;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--frames" && i + 1 < argc) app.setFrameBudget(std::atoi(argv[++i]));
        }
        app.Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "cna-street: %s\n", error.what());
        return 1;
    }
}
