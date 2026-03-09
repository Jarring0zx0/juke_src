#include "JukeUI.hpp"
#include "../../ImGui/imgui.h"
#include "../../OutWrapper.h"
#include "../../Components/Components/GUI.hpp"
#include "../../Bot/Bot.hpp"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

static std::atomic<bool> g_jukeUnloading{false};
static DWORD WINAPI juke_unload_thr(void*){
    g_JukeLog.Log("juke dll unloading");
    g_JukeLog.Log("__JUKE_EXIT__");
    Sleep(100);
    GUIComponent::Unload();
    HMODULE m=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)&juke_unload_thr,&m)) return 0;
    FreeLibraryAndExitThread(m,0);
    return 0;
}

static const char* kDefCfg=
R"(config = {
    "ToggleKey": "R",
    "BotType": "GGL_PPO",
    "POLICY_SIZE": {1024, 1024},
    "USE_SHARED_HEAD": "true",
    "SHARED_HEAD_SIZE": {1024, 1024},
    "ObsBuilder": "AdvancedObsPadded",
    "obsSize": 237,
    "tickSkip": 4,
    "actionDelay": 1
}
)";

static float c255(int v){ return (float)v/255.0f; }
static ImVec4 rgba(int r,int g,int b,float a){ return ImVec4(c255(r),c255(g),c255(b),a); }
static ImVec4 rgb(int r,int g,int b){ return rgba(r,g,b,1.0f); }
static bool ws(char c){ return c==' '||c=='\t'||c=='\r'||c=='\n'; }
static bool is09(char c){ return c>='0'&&c<='9'; }
static void parse_ints(const char* s, std::vector<int>& out) {
    out.clear();
    if (!s) return;
    const char* p = s;
    while (*p) {
        if (is09(*p) || (*p == '-' && is09(p[1]))) {
            out.push_back(std::atoi(p));
            if (*p == '-') p++;
            while (*p && is09(*p)) p++;
        }
        else {
            p++;
        }
    }
    if (out.empty()) out.push_back(1024);
}
static std::string cfg_err(const char* s){
    if(!s) return "config null";
    while(*s&&ws(*s)) ++s;
    if(!*s) return "config empty";
    int br=0,sq=0,pa=0; bool q=false; char qc=0;
    for(const char* p=s; *p; ++p){
        char c=*p;
        if(q){
            if(c=='\\'&&p[1]){ ++p; continue; }
            if(c==qc){ q=false; qc=0; }
            continue;
        }
        if(c=='"'||c=='\''){ q=true; qc=c; continue; }
        if(c=='{') ++br; else if(c=='}') --br;
        else if(c=='[') ++sq; else if(c==']') --sq;
        else if(c=='(') ++pa; else if(c==')') --pa;
        if(br<0||sq<0||pa<0) return "syntax error: closing bracket without opening";
    }
    if(q) return "syntax error: unterminated string";
    if(br||sq||pa) return "syntax error: unbalanced brackets";
    return {};
}

struct Tok{ int a,b; ImU32 col; };
static bool isaz(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }

static void toks(const char* s,std::vector<Tok>& out){
    out.clear();
    if(!s) return;
    const int n=(int)std::strlen(s);
    auto col_kw=IM_COL32(80,160,255,255);
    auto col_key=IM_COL32(255,220,90,255);
    auto col_str=IM_COL32(120,230,120,255);
    auto col_num=IM_COL32(140,200,255,255);
    auto col_p=IM_COL32(190,190,190,255);
    for(int i=0;i<n;){
        char c=s[i];
        if(c=='"'||c=='\''){
            const char qc=c; int j=i+1;
            for(;j<n;j++){
                if(s[j]=='\\'&&j+1<n){ j++; continue; }
                if(s[j]==qc){ j++; break; }
            }
            int k=j; while(k<n && (s[k]==' '||s[k]=='\t')) k++;
            bool is_key = (k<n && s[k]==':');
            out.push_back({i,j,is_key?col_key:col_str});
            i=j; continue;
        }
        if(is09(c)){
            int j=i+1; while(j<n && is09(s[j])) j++;
            out.push_back({i,j,col_num});
            i=j; continue;
        }
        if(isaz(c)){
            int j=i+1; while(j<n && (isaz(s[j])||is09(s[j]))) j++;
            if(j-i==6 && std::memcmp(s+i,"config",6)==0) out.push_back({i,j,col_kw});
            i=j; continue;
        }
        if(c=='{'||c=='}'||c=='['||c==']'||c==','||c==':'||c=='='){
            out.push_back({i,i+1,col_p});
            i++; continue;
        }
        i++;
    }
}

static float max_line_w(const char* s){
    if(!s) return 0.0f;
    float mw=0.0f; const char* p=s; const char* ls=p;
    for(;*p;p++){
        if(*p=='\n'){
            std::string line(ls,p);
            mw = (mw>ImGui::CalcTextSize(line.c_str()).x)?mw:ImGui::CalcTextSize(line.c_str()).x;
            ls=p+1;
        }
    }
    if(ls && *ls){
        mw = (mw>ImGui::CalcTextSize(ls).x)?mw:ImGui::CalcTextSize(ls).x;
    }
    return mw;
}
static int line_cnt(const char* s){
    if(!s||!*s) return 1;
    int c=1; for(const char* p=s; *p; ++p) if(*p=='\n') c++;
    return c;
}

static void draw_hl(const char* s, const ImVec2& itemMin, const ImVec2& itemMax, float sx, float sy){
    if(!s) return;
    std::vector<Tok> ts; toks(s,ts);
    if(ts.empty()) return;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    const ImGuiStyle& st=ImGui::GetStyle();
    const float lh=ImGui::GetTextLineHeight();
    dl->PushClipRect(itemMin,itemMax,true);
    for(const auto& t: ts){
        int line=0;
        for(int i=0;i<t.a;i++) if(s[i]=='\n') line++;
        int ls=t.a; while(ls>0 && s[ls-1]!='\n') ls--;
        int le=t.a; while(s[le] && s[le]!='\n') le++;
        std::string pre(s+ls, s+t.a);
        std::string seg(s+t.a, s+t.b);
        float x=itemMin.x + st.FramePadding.x + ImGui::CalcTextSize(pre.c_str()).x - sx;
        float y=itemMin.y + st.FramePadding.y + line*lh - sy;
        dl->AddText(ImVec2(x,y), t.col, seg.c_str());
    }
    dl->PopClipRect();
}

JukeUI::JukeUI(const std::string& name,const std::string& description,uint32_t states) : Module(name,description,states), showWindow(true){
    std::memset(configText,0,sizeof(configText));
    strncpy_s(configText,kDefCfg,sizeof(configText)-1);
}
JukeUI::~JukeUI(){}

void JukeUI::OnRender(){
    if(!showWindow) return;
    ImGuiIO& io=ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor=false;

    ImGuiStyle& st=ImGui::GetStyle();
    st.WindowRounding=6.0f; st.FrameRounding=4.0f; st.ScrollbarRounding=6.0f; st.GrabRounding=4.0f; st.TabRounding=4.0f;
    st.Colors[ImGuiCol_Text]=rgba(255,255,255,1.0f);
    st.Colors[ImGuiCol_TextDisabled]=rgba(180, 180, 180, 1);

    st.Colors[ImGuiCol_TitleBg]=rgba(84,0,0,1.0f);
    st.Colors[ImGuiCol_TitleBgActive]=rgba(84,0,0,1.0f);
    st.Colors[ImGuiCol_TitleBgCollapsed]=rgba(84,0,0,1.0f);

    st.Colors[ImGuiCol_WindowBg]=rgb(15, 15, 15);
    st.Colors[ImGuiCol_Border]=rgba(20,20,20,1.0f);

    st.Colors[ImGuiCol_FrameBg]=rgb(26, 26, 26);
    st.Colors[ImGuiCol_FrameBgHovered]=rgba(30, 30, 30, 1);
    st.Colors[ImGuiCol_FrameBgActive]=rgba(34, 34, 34, 1);

    st.Colors[ImGuiCol_Button]=rgba(45, 45, 45, 1);
    st.Colors[ImGuiCol_ButtonHovered]=rgba(0,13,117,0.6f);
    st.Colors[ImGuiCol_ButtonActive]=rgba(0,13,117,0.8f);

    st.Colors[ImGuiCol_Tab]=rgba(60,60,60,1.0f);
    st.Colors[ImGuiCol_TabHovered]=rgba(0,13,117,0.86f);
    st.Colors[ImGuiCol_TabActive]=rgba(0,13,117,0.92f);
    st.Colors[ImGuiCol_TabUnfocused]=rgba(60,60,60,1.0f);
    st.Colors[ImGuiCol_TabUnfocusedActive]=rgba(0,13,117,0.80f);

    st.Colors[ImGuiCol_ScrollbarBg]=rgba(44,44,44,1.0f);
    st.Colors[ImGuiCol_ScrollbarGrab]=rgba(80,80,80,1.0f);
    st.Colors[ImGuiCol_ScrollbarGrabHovered]=rgba(26, 26, 26, 0.6);
    st.Colors[ImGuiCol_ScrollbarGrabActive]=rgba(26, 26, 26, 0.6);

    st.Colors[ImGuiCol_TextSelectedBg]=rgb(58, 199, 255);

    ImGui::SetNextWindowSize(ImVec2(320,320), ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Juke v0.0.2 - love from dqbx & chief",&showWindow,ImGuiWindowFlags_NoCollapse)){
        if(ImGui::BeginTabBar("Tabs")){
            if(ImGui::BeginTabItem("main")){
                static char toggleKey[8]="R";
                static char botType[64]="GGL_PPO";
                static char policySize[32]="1024, 1024";
                static bool useSharedHead=true;
                static char sharedHeadSize[32]="1024, 1024";
                static char obsBuilder[64]="AdvancedObsPadded";
                static int obsSize=237;
                static int tickSkip=4;
                static int actionDelay=1;
                static char device[16]="cuda";
                static int activationIdx=0;
                static const char* activationOptions[] = {"relu", "leaky_relu", "tanh", "sigmoid", "elu", "selu", "gelu", "swish"};

                ImGui::TextUnformatted("Device"); ImGui::SameLine(); ImGui::SetNextItemWidth(60); ImGui::InputText("##Device", device, sizeof(device));
                ImGui::TextUnformatted("ToggleKey"); ImGui::SameLine(); ImGui::SetNextItemWidth(40); ImGui::InputText("##ToggleKey", toggleKey, sizeof(toggleKey));
                ImGui::TextUnformatted("BotType"); ImGui::SameLine(); ImGui::SetNextItemWidth(150); ImGui::InputText("##BotType", botType, sizeof(botType));
                ImGui::TextUnformatted("POLICY_SIZE"); ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputText("##POLICY_SIZE", policySize, sizeof(policySize));
                ImGui::TextUnformatted("USE_SHARED_HEAD"); ImGui::SameLine(); ImGui::Checkbox("##USE_SHARED_HEAD", &useSharedHead);
                if(useSharedHead){
                    ImGui::TextUnformatted("SHARED_HEAD_SIZE"); ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputText("##SHARED_HEAD_SIZE", sharedHeadSize, sizeof(sharedHeadSize));
                }
                ImGui::TextUnformatted("ObsBuilder"); ImGui::SameLine(); ImGui::SetNextItemWidth(190); ImGui::InputText("##ObsBuilder", obsBuilder, sizeof(obsBuilder));
                ImGui::TextUnformatted("obsSize"); ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputInt("##obsSize", &obsSize);
                ImGui::TextUnformatted("tickSkip"); ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputInt("##tickSkip", &tickSkip);
                ImGui::TextUnformatted("actionDelay"); ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputInt("##actionDelay", &actionDelay);
                ImGui::TextUnformatted("Activation"); ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::Combo("##Activation", &activationIdx, activationOptions, IM_ARRAYSIZE(activationOptions));

                ImGui::Spacing();
                if(ImGui::Button("load bot", ImVec2(-1,26.0f))){
                    std::vector<int> pVec, shVec;
                    parse_ints(policySize, pVec);
                    parse_ints(sharedHeadSize, shVec);

                    std::string cfg; cfg.reserve(512);
                    cfg += "config = {\n";
                    cfg += "    \"Device\": \"" + std::string(device) + "\",\n";
                    cfg += "    \"ToggleKey\": \"" + std::string(toggleKey) + "\",\n";
                    cfg += "    \"BotType\": \"" + std::string(botType) + "\",\n";

                    auto vec2str = [](const std::vector<int>& v) {
                        std::string s = "{";
                        for (size_t i = 0; i < v.size(); i++) {
                            s += std::to_string(v[i]);
                            if (i < v.size() - 1) s += ", ";
                        }
                        s += "}";
                        return s;
                    };

                    cfg += "    \"POLICY_SIZE\": " + vec2str(pVec) + ",\n";
                    cfg += "    \"USE_SHARED_HEAD\": \"" + std::string(useSharedHead?"true":"false") + "\",\n";
                    if(useSharedHead) cfg += "    \"SHARED_HEAD_SIZE\": " + vec2str(shVec) + ",\n";
                    cfg += "    \"ObsBuilder\": \"" + std::string(obsBuilder) + "\",\n";
                    cfg += "    \"obsSize\": " + std::to_string(obsSize) + ",\n";
                    cfg += "    \"tickSkip\": " + std::to_string(tickSkip) + ",\n";
                    cfg += "    \"actionDelay\": " + std::to_string(actionDelay) + ",\n";
                    cfg += "    \"Activation\": \"" + std::string(activationOptions[activationIdx]) + "\"\n";
                    cfg += "}\n";
                    strncpy_s(configText,cfg.c_str(),sizeof(configText)-1);
                    std::string e=cfg_err(configText);
                    if(!e.empty()){ std::string m="config error: "+e; jukeOut(m.c_str()); }
                    else {
                        static unsigned long long s_last=0;
                        unsigned long long now=(unsigned long long)GetTickCount64();
                        if(now - s_last < 150) return;
                        s_last=now;

                        jukeOut("load bot pressed");

                        JukeConfig newCfg;
                        std::string cfgStr(configText);

                        auto getVal = [&](const std::string& key) -> std::string {
                            size_t pos = cfgStr.find("\"" + key + "\":");
                            if (pos == std::string::npos) return "";
                            pos = cfgStr.find("\"", pos + key.length() + 3);
                            if (pos == std::string::npos) return "";
                            size_t end = cfgStr.find("\"", pos + 1);
                            if (end == std::string::npos) return "";
                            return cfgStr.substr(pos + 1, end - pos - 1);
                        };
                        auto getInt = [&](const std::string& key) -> int {
                            size_t pos = cfgStr.find("\"" + key + "\":");
                            if (pos == std::string::npos) return 0;
                            pos = cfgStr.find(":", pos);
                            size_t end = cfgStr.find_first_of(",}", pos);
                            try { return std::stoi(cfgStr.substr(pos + 1, end - pos - 1)); }
                            catch(...) { return 0; }
                        };
                        auto getBool = [&](const std::string& key) -> bool {
                            size_t pos = cfgStr.find("\"" + key + "\":");
                            if (pos == std::string::npos) return false;
                            pos = cfgStr.find("\"", pos + key.length() + 3);
                            if (pos == std::string::npos) return false;
                            size_t end = cfgStr.find("\"", pos + 1);
                            std::string val = (end == std::string::npos) ? std::string() : cfgStr.substr(pos + 1, end - pos - 1);
                            return val == "true";
                        };

                        newCfg.ToggleKey = getVal("ToggleKey");
                        newCfg.BotType = getVal("BotType");
                        newCfg.Device = getVal("Device");
                        if(newCfg.Device.empty()) newCfg.Device = "cuda";
                        newCfg.UseSharedHead = getBool("USE_SHARED_HEAD");
                        newCfg.ObsBuilder = getVal("ObsBuilder");
                        newCfg.ObsSize = getInt("obsSize");
                        newCfg.TickSkip = getInt("tickSkip");
                        newCfg.ActionDelay = getInt("actionDelay");
                        if(newCfg.UseSharedHead) newCfg.SharedHeadSize = shVec;
                        newCfg.PolicySize = pVec;
                        newCfg.Activation = getVal("Activation");
                        if(newCfg.Activation.empty()) newCfg.Activation = "relu";

                        wchar_t dllPath[MAX_PATH]{0};
                        HMODULE hm=nullptr;
                        if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCSTR)&juke_unload_thr,&hm)) hm=nullptr;
                        GetModuleFileNameW(hm, dllPath, MAX_PATH);
                        std::filesystem::path dllP(dllPath);
                        std::filesystem::path botDir = dllP.has_parent_path() ? (dllP.parent_path() / "Bot") : std::filesystem::path("Bot");
                        newCfg.BotPath = botDir.wstring();

                        char pbuf[512];
                        sprintf_s(pbuf,"botdir=%ls", newCfg.BotPath.c_str());
                        jukeOut(pbuf);

                        std::error_code ec;
                        if(!std::filesystem::exists(botDir, ec) || !std::filesystem::is_directory(botDir, ec)) {
                            jukeOut("load pressed; bot folder not found");
                        } else {
                            jukeOut("bot folder ok; applying cfg");
                            JukeBot::ApplyConfig(newCfg);
                            jukeOut("calling Inference_Init");
                            JukeBot::Inference_Init();
                            jukeOut("Inference_Init done");

                            g_BotEnabled = false;
                            char m[128];
                            sprintf_s(m,"bot loaded; toggle=%s", g_ToggleKey.c_str());
                            jukeOut(m);
                        }
                    }
                }
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("humanizer")){
                ImGui::TextUnformatted("coming soon");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("credits")){
                ImGui::TextColored(rgba(212, 187, 102, 0.86),"deqbx");
                ImGui::TextUnformatted("    - UI");
                ImGui::TextUnformatted("    - Compatibility");
                ImGui::TextUnformatted("    - Bug Fixes");
                ImGui::TextColored(rgba(212, 187, 102, 0.86), "chief0786");
                ImGui::SameLine();
                ImGui::TextUnformatted(" (AKA ");
                ImGui::SameLine();
                ImGui::TextColored(rgba(212, 187, 102, 0.86), "\"k\"");
                ImGui::SameLine();
                ImGui::TextUnformatted(")");
                ImGui::TextUnformatted("    - Bugtesting");
                ImGui::TextUnformatted("    - Memory Management");
                ImGui::TextUnformatted("    - SDK Porting");
                ImGui::TextUnformatted("This took us a long time to make");
                ImGui::Spacing();
                if(ImGui::Button("unload juke", ImVec2(-1,26.0f))){
                    if(!g_jukeUnloading.exchange(true)){
                        showWindow=false;
                        HANDLE th=CreateThread(nullptr,0,&juke_unload_thr,nullptr,0,nullptr);
                        if(th) CloseHandle(th);
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();;
        }
    }
    ImGui::End();
}
