#pragma once
#include <RLGymCPP/ActionParsers/ActionParser.h>
#include <vector>

namespace RLGC {

class LookupAction : public ActionParser {
public:
    LookupAction();
    virtual Action ParseAction(int actionIdx, const Player& player, const GameState& state) override;
    virtual int GetActionAmount() override;

private:
    std::vector<Action> lookupTable;
    void MakeLookupTable();
};

}
