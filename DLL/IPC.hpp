#pragma once
#include <windows.h>
#include <string>
#include <mutex>

class JukePipeClient {
public:
    JukePipeClient() : hPipe(INVALID_HANDLE_VALUE) {}
    ~JukePipeClient() { Disconnect(); }

    bool Connect() {
        if (hPipe != INVALID_HANDLE_VALUE) return true;
        const char* name="\\\\.\\pipe\\JukePipe";
        for(int i=0;i<30;i++){
            hPipe=CreateFileA(name,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(hPipe!=INVALID_HANDLE_VALUE) return true;
            DWORD e=GetLastError();
            if(e==ERROR_PIPE_BUSY){
                if(!WaitNamedPipeA(name,20)) return false;
                continue;
            }
            if(e==ERROR_FILE_NOT_FOUND){ Sleep(20); continue; }
            return false;
        }
        return false;
    }

    void Disconnect() {
        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
    }

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!Connect()) return;

        DWORD w=0;
        BOOL ok=WriteFile(hPipe, msg.c_str(), (DWORD)msg.length(), &w, NULL);
        if(ok) FlushFileBuffers(hPipe);
        else Disconnect();
    }

private:
    HANDLE hPipe;
    std::mutex mtx;
};

extern JukePipeClient g_JukeLog;
