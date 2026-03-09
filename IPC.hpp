#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

extern void Juke_HandleIPCMessage(const std::string& msg);

class IPCServer {
public:
    IPCServer() : serverSocket(INVALID_SOCKET), running(false) {}
    ~IPCServer() { Stop(); }

    bool Start(int port) {
        if (running) return false;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);

        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        running = true;
        acceptThread = std::thread(&IPCServer::AcceptLoop, this);
        return true;
    }

    void Stop() {
        running = false;
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
            serverSocket = INVALID_SOCKET;
        }
        {
            std::lock_guard<std::mutex> lk(mx);
            for (auto s : clients) closesocket(s);
            clients.clear();
        }
        if (acceptThread.joinable()) acceptThread.join();
        WSACleanup();
    }

    void Broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lk(mx);
        for (auto it = clients.begin(); it != clients.end(); ) {
            int r = send(*it, message.c_str(), (int)message.size(), 0);
            if (r == SOCKET_ERROR) {
                closesocket(*it);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    void AcceptLoop() {
        while (running) {
            sockaddr_in clientAddr{};
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket == INVALID_SOCKET) continue;

            {
                std::lock_guard<std::mutex> lk(mx);
                clients.push_back(clientSocket);
            }

            const char* msg = "Status:Injected\n";
            send(clientSocket, msg, (int)strlen(msg), 0);

            std::thread(&IPCServer::HandleClient, this, clientSocket).detach();
        }
    }

    void HandleClient(SOCKET clientSocket) {
        char buffer[2048];
        for (;;) {
            int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived <= 0) break;
            buffer[bytesReceived] = '\0';

            Juke_HandleIPCMessage(std::string(buffer));
        }

        closesocket(clientSocket);
        std::lock_guard<std::mutex> lk(mx);
        clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());
    }

    SOCKET serverSocket;
    std::atomic<bool> running;
    std::thread acceptThread;
    std::vector<SOCKET> clients;
    std::mutex mx;
};

extern IPCServer g_IPC;
