// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"

#include <memory>

namespace Microsoft::Xna::Framework {
    class GraphicsDeviceManager;
}

namespace CnaStreet {

/**
 * @brief The application: owns the window, the device and the city.
 *
 * A thin CNA @c Game subclass. Everything that is not lifecycle plumbing lives
 * in the systems it owns, so that the interesting code is testable without a
 * graphics device.
 */
class StreetApplication : public Microsoft::Xna::Framework::Game
{
public:
    StreetApplication();
    ~StreetApplication() override;

    /// Renders this many frames and exits. 0 (the default) runs until quit.
    void setFrameBudget(int frames);

protected:
    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> graphics_;
    int  frameBudget_ = 0;
    int  framesDrawn_ = 0;
};

}  // namespace CnaStreet
