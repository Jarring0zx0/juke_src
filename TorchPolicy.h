#pragma once
#include <torch/torch.h>
#include <torch/script.h>
#include <filesystem>
#include <memory>

namespace Juke {
struct TorchPolicy {
    torch::nn::Sequential shared{nullptr};
    torch::nn::Sequential policy{nullptr};
    std::shared_ptr<torch::jit::Module> jitShared;
    std::shared_ptr<torch::jit::Module> jitPolicy;
    bool useJit = false;

    torch::Device device{torch::kCPU};
    bool useShared = false;
    bool loaded = false;

    bool useGPU = false;
    int obsSize = 237;
    torch::Tensor inCpu;
    torch::Tensor inGpu;

    bool Load(const std::filesystem::path& dir, bool useSharedHead, bool useGPU,
        const std::vector<int>& policySize, const std::vector<int>& sharedHeadSize, int obsSize, const std::string& activation="relu");
    int Act(const torch::Tensor& obs);
};
}
