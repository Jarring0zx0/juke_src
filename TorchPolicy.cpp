#include "TorchPolicy.h"
#include "Blacklist/Blacklist.hpp"
#include <torch/csrc/api/include/torch/serialize.h>
#include <torch/serialize.h>
#include <torch/script.h>
#include <fstream>
#include <vector>
#include <mutex>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

extern "C" void Juke_LogA(const char* s);
extern "C" void jukeOut(const char* s);

namespace Juke {

static void LogBoth(const char* s){
	if(!s) return;
	Juke_LogA(s);
	jukeOut(s);
}
static bool Exists(const std::filesystem::path& p){ std::error_code ec; return std::filesystem::exists(p, ec); }

static bool HasKey(const std::filesystem::path& path, const char* key, torch::Device dev){
	try {
		torch::serialize::InputArchive ar;
		ar.load_from(path.string(), dev);
		auto ks = ar.keys();
		for (auto& k : ks) if (k == key) return true;
		return false;
	} catch (...) {
		return false;
	}
}

static void BuildMLP(torch::nn::Sequential& seq, int in, const std::vector<int>& hs, bool ln, int out, bool softmax, const std::string& activation="relu"){
	seq = torch::nn::Sequential();
	int cur = in;
	for (int h : hs) {
		seq->push_back(torch::nn::Linear(cur, h));
		if (ln) seq->push_back(torch::nn::LayerNorm(torch::nn::LayerNormOptions({(int64_t)h})));

		if (activation == "leaky_relu") {
			seq->push_back(torch::nn::LeakyReLU(torch::nn::LeakyReLUOptions().negative_slope(0.01)));
		} else if (activation == "tanh") {
			seq->push_back(torch::nn::Tanh());
		} else if (activation == "sigmoid") {
			seq->push_back(torch::nn::Sigmoid());
		} else if (activation == "elu") {
			seq->push_back(torch::nn::ELU(torch::nn::ELUOptions().alpha(1.0)));
		} else if (activation == "selu") {
			seq->push_back(torch::nn::SELU());
		} else if (activation == "gelu") {
			seq->push_back(torch::nn::GELU());
		} else if (activation == "swish" || activation == "silu") {
			seq->push_back(torch::nn::SiLU());
		} else {

			seq->push_back(torch::nn::ReLU());
		}

		cur = h;
	}
	if (out > 0) {
		seq->push_back(torch::nn::Linear(cur, out));
		if (softmax) seq->push_back(torch::nn::Softmax(-1));
	}
}

bool TorchPolicy::Load(const std::filesystem::path& dir, bool useSharedHead, bool useGPU,
	const std::vector<int>& policySize, const std::vector<int>& sharedHeadSize, int viewObsSize, const std::string& activation){
	jukeOut("TorchPolicy::Load enter");
	static std::once_flag s_thOnce;
	try{
		std::call_once(s_thOnce, []{
			torch::set_num_threads(1);
			torch::set_num_interop_threads(1);
		});
	}catch(...){ jukeOut("TorchPolicy::Load call_once fail"); }

	this->loaded = false;
	this->useShared = useSharedHead;
	this->useGPU = useGPU;
	if (useGPU) {
		try { this->device = torch::Device(torch::kCUDA); }
		catch (...) { this->device = torch::Device(torch::kCPU); this->useGPU = false; }
	} else {
		this->device = torch::Device(torch::kCPU);
	}

	auto pick = [&](std::initializer_list<const char*> f) {
		for (auto x : f) {
			std::error_code ec;
			if (std::filesystem::exists(dir / x, ec)) return dir / x;
		}
		return dir / *(f.begin());
	};

	const auto polPath = pick({"POLICY.lt","POLICY.LT","POLICY_OPTIM.lt","POLICY_OPTIM.LT"});
	const auto shPath  = pick({"SHARED_HEAD.lt","SHARED_HEAD.LT","SHARED_HEAD_OPTIM.lt","SHARED_HEAD_OPTIM.LT"});

	{
		char b[1024];

		try{
			sprintf_s(b,"TorchPolicy::Load dir=%s", dir.string().c_str());
			jukeOut(b);
			sprintf_s(b,"TorchPolicy::Load policy=%s", polPath.string().c_str());
			jukeOut(b);
		}catch(...){ jukeOut("TorchPolicy::Load path print fail"); }
	}

	if (!Exists(polPath)) { LogBoth("TorchPolicy::Load: missing policy"); return false; }

	if (Juke::CheckBlacklist(polPath, shPath)) {
		return false;
	}

	if (useShared && !Exists(shPath)) { LogBoth("TorchPolicy::Load: missing shared"); return false; }

	this->obsSize = viewObsSize;
	const int nActions = 90;

	const std::vector<int>& sh = sharedHeadSize;
	const std::vector<int>& ph = policySize;

	auto extract_state_dict_from_jit = [](const torch::jit::Module& m) -> std::unordered_map<std::string, torch::Tensor> {
		std::unordered_map<std::string, torch::Tensor> sd;
		for (const auto& p : m.named_parameters()) {
			sd[p.name] = p.value;
		}
		for (const auto& b : m.named_buffers()) {
			sd[b.name] = b.value;
		}
		return sd;
	};

	torch::jit::Module jitSh, jitPol;
	bool jitShLoaded = false, jitPolLoaded = false;
	try{
		LogBoth("TorchPolicy::Load: trying torch::jit::load");
		if(useShared){
			jitSh = torch::jit::load(shPath.string(), torch::kCPU);
			jitSh.eval();
			jitShLoaded = true;
		}
		jitPol = torch::jit::load(polPath.string(), torch::kCPU);
		jitPol.eval();
		jitPolLoaded = true;
	}catch(const std::exception& e){
		LogBoth("TorchPolicy::Load jit::load failed");
		LogBoth(e.what());
		jitShLoaded = false;
		jitPolLoaded = false;
	}catch(...){
		LogBoth("TorchPolicy::Load jit::load failed (unknown)");
		jitShLoaded = false;
		jitPolLoaded = false;
	}

	bool jitForwardWorks = false;
	if(jitPolLoaded){
		try{
			torch::NoGradGuard ng;
			auto cpuOpts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
			auto testIn = torch::zeros({1, viewObsSize}, cpuOpts);
			if(useShared && jitShLoaded) jitSh.forward({testIn});
			jitPol.forward({testIn});
			jitForwardWorks = true;
			LogBoth("TorchPolicy::Load jit forward works!");
		}catch(const std::exception& e){
			LogBoth("TorchPolicy::Load jit forward failed (will extract state_dict)");
			LogBoth(e.what());
			jitForwardWorks = false;
		}catch(...){
			jitForwardWorks = false;
		}
	}

	if(jitPolLoaded && jitForwardWorks){
		if(useShared && jitShLoaded){
			jitShared = std::make_shared<torch::jit::Module>(std::move(jitSh));
			if(useGPU) jitShared->to(device);
		}
		jitPolicy = std::make_shared<torch::jit::Module>(std::move(jitPol));
		if(useGPU) jitPolicy->to(device);
		useJit = true;

		auto cpuOpts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
		if (this->useGPU) cpuOpts = cpuOpts.pinned_memory(true);
		inCpu = torch::empty({1, viewObsSize}, cpuOpts);
		if (this->useGPU) inGpu = torch::empty({1, viewObsSize}, torch::TensorOptions().dtype(torch::kFloat32).device(device));
		else inGpu = torch::Tensor();

		loaded = true;
		LogBoth("TorchPolicy::Load jit ok (forward works)");
		return true;
	}

	std::unordered_map<std::string, torch::Tensor> sharedStateDict, policyStateDict;
	if(jitPolLoaded){
		LogBoth("TorchPolicy::Load extracting state_dict from jit modules");
		if(useShared && jitShLoaded){
			sharedStateDict = extract_state_dict_from_jit(jitSh);
			char buf[128];
			sprintf_s(buf, "shared state_dict: %d entries", (int)sharedStateDict.size());
			LogBoth(buf);
		}
		policyStateDict = extract_state_dict_from_jit(jitPol);
		char buf[128];
		sprintf_s(buf, "policy state_dict: %d entries", (int)policyStateDict.size());
		LogBoth(buf);
	}

    //MF doom
        //brutal mogged 
	const bool policySoftmax = true;
	LogBoth("policy_softmax=1");

	auto load_module_file = [&](auto& mod, const std::filesystem::path& pth) -> bool {
		try {
			auto in = std::ifstream(pth, std::ios::binary);
			if(!in) return false;
			in >> std::noskipws;
			torch::load(mod, in, device);
			return true;
		} catch (const std::exception& e) {
			LogBoth(e.what());
			return false;
		} catch (...) {
			LogBoth("torch::load(module): unknown exception");
			return false;
		}
	};

	auto apply_state_map = [&](torch::nn::Module& m, const std::unordered_map<std::string, torch::Tensor>& sd) -> bool {

		torch::NoGradGuard no_grad;

		std::unordered_map<std::string, torch::Tensor> pmap;
		std::unordered_map<std::string, torch::Tensor> bmap;
		for (const auto& kv : m.named_parameters(true)) pmap.emplace(kv.key(), kv.value());
		for (const auto& kv : m.named_buffers(true)) bmap.emplace(kv.key(), kv.value());

		int copied=0, missing=0, unexpected=0, mismatch=0;
		for (const auto& kv : sd) {
			const auto& k = kv.first;
			auto src = kv.second;

			auto pit = pmap.find(k);
			if (pit != pmap.end()) {
				auto dst = pit->second;
				if (src.device() != device || src.scalar_type() != dst.scalar_type())
					src = src.to(device, dst.scalar_type(), false, false);
				if (!src.is_contiguous()) src = src.contiguous();
				if (src.sizes() != dst.sizes()) { mismatch++; continue; }
				dst.copy_(src);
				copied++;
				continue;
			}

			auto bit = bmap.find(k);
			if (bit != bmap.end()) {
				auto dst = bit->second;
				if (src.device() != device || src.scalar_type() != dst.scalar_type())
					src = src.to(device, dst.scalar_type(), false, false);
				if (!src.is_contiguous()) src = src.contiguous();
				if (src.sizes() != dst.sizes()) { mismatch++; continue; }
				dst.copy_(src);
				copied++;
				continue;
			}

			unexpected++;
		}

		for (const auto& kv : pmap) if (!sd.count(kv.first)) missing++;
		for (const auto& kv : bmap) if (!sd.count(kv.first)) missing++;

		char msg[256];
		sprintf_s(msg,"state_dict: copied=%d missing=%d unexpected=%d mismatch=%d", copied, missing, unexpected, mismatch);
		LogBoth(msg);

		return copied>0 && mismatch==0;
	};

	auto load_state_dict_file = [&](auto& mod, const std::filesystem::path& pth) -> bool {

		try {
			std::ifstream f(pth, std::ios::binary);
			if(!f){ LogBoth("state_dict: open failed"); return false; }

			unsigned char hdr[4]{0,0,0,0};
			f.read((char*)hdr, 4);
			const std::streamsize got = f.gcount();
			f.clear();
			f.seekg(0, std::ios::beg);

			if(got == 4 && hdr[0] == 'P' && hdr[1] == 'K'){
				LogBoth("state_dict: zip archive; fallback unsupported (export TorchScript or ensure jit load works)");
				return false;
			}

			std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if(data.empty()){ LogBoth("state_dict: empty file"); return false; }

			torch::IValue iv = torch::pickle_load(data);
			if(!iv.isGenericDict()){
				LogBoth("state_dict: not a dict");
				return false;
			}

			auto gd = iv.toGenericDict();
			std::unordered_map<std::string, torch::Tensor> sd;
			sd.reserve((size_t)gd.size());

			int kept=0, skipped=0;
			for(const auto& it : gd){
				if(!it.key().isString() || !it.value().isTensor()){ skipped++; continue; }
				sd.emplace(it.key().toStringRef(), it.value().toTensor());
				kept++;
			}

			{
				char msg[192];
				sprintf_s(msg,"state_dict: entries=%d skipped=%d", kept, skipped);
				LogBoth(msg);
			}

			return apply_state_map(*mod, sd);
		} catch (const std::exception& e) {
			LogBoth(e.what());
			return false;
		} catch (...) {
			LogBoth("state_dict: unknown exception");
			return false;
		}
	};

	try {
		if (useShared) {
			BuildMLP(shared, viewObsSize, sh, true, 0, false, activation);
			shared->to(device);
			bool ok = false;

			if(!sharedStateDict.empty()){
				LogBoth("shared: trying jit-extracted state_dict");
				ok = apply_state_map(*shared, sharedStateDict);
			}

			if(!ok){
				LogBoth("shared: trying module-load");
				ok = load_module_file(shared, shPath);
			}

			if(!ok){
				LogBoth("shared module-load failed; trying state_dict file");
				ok = load_state_dict_file(shared, shPath);
			}
			if(!ok) return false;
			shared->eval();
		} else {
			shared = nullptr;
		}

		const int polIn = useShared ? sh.back() : viewObsSize;
		BuildMLP(policy, polIn, ph, true, nActions, policySoftmax, activation);
		policy->to(device);
		bool ok2 = false;

		if(!policyStateDict.empty()){
			LogBoth("policy: trying jit-extracted state_dict");
			ok2 = apply_state_map(*policy, policyStateDict);
		}

		if(!ok2){
			LogBoth("policy: trying module-load");
			ok2 = load_module_file(policy, polPath);
		}

		if(!ok2){
			LogBoth("policy module-load failed; trying state_dict file");
			ok2 = load_state_dict_file(policy, polPath);
		}
		if(!ok2) return false;
		policy->eval();

		auto cpuOpts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
		if (this->useGPU) cpuOpts = cpuOpts.pinned_memory(true);
		inCpu = torch::empty({1, viewObsSize}, cpuOpts);
		if (this->useGPU) inGpu = torch::empty({1, viewObsSize}, torch::TensorOptions().dtype(torch::kFloat32).device(device));
		else inGpu = torch::Tensor();

		loaded = true;
		LogBoth("TorchPolicy::Load ok");
		return true;
	} catch (const std::exception& e) {
		LogBoth(e.what());
		loaded = false;
		return false;
	} catch (...) {
		LogBoth("TorchPolicy::Load: unknown exception");
		loaded = false;
		return false;
	}
}

int TorchPolicy::Act(const torch::Tensor& obs){
	if (!loaded) return 0;
	torch::NoGradGuard ng;
	if (!obs.defined()) return 0;

	auto toT = [](const torch::IValue& iv)->torch::Tensor{
		if(iv.isTensor()) return iv.toTensor();
		if(iv.isTuple()){
			auto t=iv.toTuple();
			if(t && !t->elements().empty() && t->elements()[0].isTensor()) return t->elements()[0].toTensor();
		}
		return torch::Tensor();
	};

	torch::Tensor o = obs;
	if (o.dim() == 2) o = o.squeeze(0);

	if (o.numel() == obsSize) {
		if (!inCpu.defined() || inCpu.numel() != obsSize) {
			auto cpuOpts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
			if (useGPU) cpuOpts = cpuOpts.pinned_memory(true);
			inCpu = torch::empty({1, obsSize}, cpuOpts);
			if (useGPU) inGpu = torch::empty({1, obsSize}, torch::TensorOptions().dtype(torch::kFloat32).device(device));
		}

		if (o.is_cuda() || o.scalar_type() != torch::kFloat32) {
			o = o.to(torch::kCPU, torch::kFloat32, false, false);
		}
		o = o.contiguous();

		std::memcpy(inCpu.data_ptr<float>(), o.data_ptr<float>(), (size_t)obsSize * sizeof(float));

		torch::Tensor x;
		if (useGPU) {
			inGpu.copy_(inCpu, true);
			x = inGpu;
		} else {
			x = inCpu;
		}

		if(useJit && jitPolicy){
			torch::Tensor y=x;
			if(useShared && jitShared) y = toT(jitShared->forward({y}));
			torch::Tensor out = toT(jitPolicy->forward({y}));
			return out.defined() ? out.argmax(-1).item<int>() : 0;
		}

		if (useShared && shared) x = shared->forward(x);
		torch::Tensor out = policy->forward(x);
		return out.argmax(-1).item<int>();
	}

	torch::Tensor x = o;
	if (x.dim() == 1) x = x.unsqueeze(0);
	if (x.scalar_type() != torch::kFloat32 || x.device() != device) x = x.to(device, torch::kFloat32);

	if(useJit && jitPolicy){
		torch::Tensor y=x;
		if(useShared && jitShared) y = toT(jitShared->forward({y}));
		torch::Tensor out = toT(jitPolicy->forward({y}));
		if(out.dim()==2) out = out.squeeze(0);
		return out.defined() ? out.argmax(-1).item<int>() : 0;
	}

	if (useShared && shared) x = shared->forward(x);
	torch::Tensor out = policy->forward(x);
	if (out.dim() == 2) out = out.squeeze(0);
	return out.argmax(-1).item<int>();
}

}
