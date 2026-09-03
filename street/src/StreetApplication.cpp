// SPDX-License-Identifier: MIT
#include "CnaStreet/StreetApplication.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaStreet {

StreetApplication::StreetApplication()
    : graphics_(std::make_unique<GraphicsDeviceManager>(this))
{
}

StreetApplication::~StreetApplication() = default;

void StreetApplication::setFrameBudget(int frames) { frameBudget_ = frames; }

void StreetApplication::Initialize()
{
    Game::Initialize();
}

void StreetApplication::LoadContent()
{
    Game::LoadContent();
}

void StreetApplication::Update(GameTime& gameTime)
{
    Game::Update(gameTime);
    if (frameBudget_ > 0 && framesDrawn_ >= frameBudget_) Exit();
}

void StreetApplication::Draw(const GameTime& gameTime)
{
    getGraphicsDeviceProperty().Clear(Color(120, 150, 200, 255));
    Game::Draw(gameTime);
    ++framesDrawn_;
}

}  // namespace CnaStreet
