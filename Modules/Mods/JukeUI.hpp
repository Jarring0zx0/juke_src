#pragma once
#include "../Module.hpp"
#include "../../DLL/IPC.hpp"

class JukeUI : public Module {
public:
    JukeUI(const std::string& name, const std::string& description, uint32_t states);
    ~JukeUI() override;

    void OnRender() override;

private:
    char configText[4096];
    bool showWindow;
};
