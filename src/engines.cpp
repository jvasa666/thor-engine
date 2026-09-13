#include "thor/engines.hpp"
#include "thor/models.hpp"

#include <iostream>
#include <map>
#include <mutex>
#include <string>

namespace thor {

namespace {

class DialogueEngine : public Engine {
public:
    std::string name() const override { return "Dialogue"; }
    std::string status() const override { return "ready"; }
    EngineResult execute(const EngineRequest& r) override {
        EngineResult out;
        if (r.action == "Say") {
            auto it = r.arguments.find("message");
            if (it != r.arguments.end())
                if (auto p = std::get_if<std::string>(&it->second.data))
                    std::cout << "[Dialogue] " << *p << "\n";
        }
        return out;
    }
};

class CodexEngine : public Engine {
public:
    std::string name() const override { return "Codex"; }
    std::string status() const override { return "in-memory"; }
    EngineResult execute(const EngineRequest& r) override {
        EngineResult out;
        auto get = [&](const char* k) -> std::string {
            auto it = r.arguments.find(k);
            if (it == r.arguments.end()) return {};
            if (auto p = std::get_if<std::string>(&it->second.data)) return *p;
            if (auto p = std::get_if<std::int64_t>(&it->second.data)) return std::to_string(*p);
            return {};
        };
        if (r.action == "Store_Knowledge") {
            std::lock_guard<std::mutex> lk(mu_);
            kb_[get("Key")] = get("Value");
        } else if (r.action == "Retrieve_Knowledge") {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = kb_.find(get("Key"));
            if (it == kb_.end()) out.success = false;
            else out.output["value"] = Value{it->second};
        }
        return out;
    }
private:
    std::mutex mu_;
    std::map<std::string, std::string> kb_;
};

class ObserveEngine : public Engine {
public:
    std::string name() const override { return "Observe"; }
    std::string status() const override { return "monitoring"; }
    EngineResult execute(const EngineRequest&) override {
        EngineResult out;
        ValueMap m; m["Threats_Detected"] = Value{false};
        out.patch.metrics["Observe_Result"] = make_map(std::move(m));
        return out;
    }
};

} // namespace

std::shared_ptr<Engine> make_dialogue_engine() {
    return std::make_shared<DialogueEngine>();
}
std::shared_ptr<Engine> make_codex_engine() {
    return std::make_shared<CodexEngine>();
}
std::shared_ptr<Engine> make_observe_engine() {
    return std::make_shared<ObserveEngine>();
}

} // namespace thor
