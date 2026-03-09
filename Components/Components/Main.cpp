#include "Main.hpp"
#include "../Includes.hpp"
#include "../Modules/Includes.hpp"

MainComponent::MainComponent() : Component("Main", "Interface to game interacton") { OnCreate(); }

MainComponent::~MainComponent() { OnDestroy(); }

void MainComponent::OnCreate() {}

void MainComponent::OnDestroy() {}

void MainComponent::Initialize() {

}

std::vector<std::function<void()>> MainComponent::GameFunctions;

class MainComponent Main {};
