#include "core/conversation.h"
#include "core/message.h"
#include "core/sentinel_scanner.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <vector>

class MockInput : public InputSource {
public:
    std::string read_line() override {
        if (current_ >= inputs.size()) { eof_ = true; return ""; }
        return inputs[current_++];
    }
    bool is_eof() const override { return eof_; }
    std::vector<std::string> inputs;
    std::size_t current_ = 0;
    bool eof_ = false;
};

class MockOutput : public OutputSink {
public:
    void write(std::string_view text) override { buffer += text; }
    std::string buffer;
};

void Test1_EmptyConversation() {
    Conversation conv;
    assert(conv.size() == 0);
    assert(conv.begin() == conv.end());
    bool threw = false;
    try { conv.at(0); } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}

void Test2_SystemMessageOrder() {
    Conversation conv;
    conv.append(Message(Role::System, "sys"));
    conv.append(Message(Role::User, "usr"));
    assert(conv.at(0).role() == Role::System);
}

void Test3_RuleOfFiveCopy() {
    Conversation c1;
    c1.append(Message(Role::User, "test"));
    Conversation c2(c1);
    assert(c1.begin() != c2.begin()); 
    assert(c2.at(0).content() == "test");
}

void Test4_RuleOfFiveMove() {
    Conversation c1;
    c1.append(Message(Role::User, "test"));
    const Message* orig_ptr = c1.begin();
    Conversation c2(std::move(c1));
    assert(c2.begin() == orig_ptr); 
    assert(c1.size() == 0); 
}

void Test5_GrowthBehavior() {
    Conversation conv;
    const Message* initial_ptr = nullptr;
    std::size_t reallocations = 0;
    for (int i = 0; i < 100; ++i) {
        conv.append(Message(Role::User, std::to_string(i)));
        if (conv.begin() != initial_ptr) {
            reallocations++;
            initial_ptr = conv.begin();
        }
    }
    assert(conv.size() == 100);
    assert(reallocations <= 8); 
}

void Test6_ScannerCleanText() {
    SentinelScanner scanner("<|end|>");
    auto out = scanner.feed("hello");
    auto flushed = scanner.flush();
    assert(out.safe_text + flushed.safe_text == "hello");
    assert(!out.sentinel_found);
}

void Test7_ScannerSplitSentinel() {
    const std::string sentinel = "<|end_conversation|>";
    const std::string text = "Goodbye." + sentinel;
    for (std::size_t split = 0; split <= text.size(); ++split) {
        SentinelScanner scanner(sentinel);
        auto out1 = scanner.feed(text.substr(0, split));
        auto out2 = scanner.feed(text.substr(split));
        assert(out1.sentinel_found || out2.sentinel_found);
        assert(out1.safe_text + out2.safe_text == "Goodbye.");
    }
}

void Test8_ScannerFalseAlarms() {
    SentinelScanner scanner("<|end_conversation|>");
    auto out = scanner.feed("<|end_world|>");
    auto flushed = scanner.flush();
    assert(!out.sentinel_found);
    assert(out.safe_text + flushed.safe_text == "<|end_world|>");
}

void Test9_ScannerBoundedMemory() {
    SentinelScanner scanner("stop");
    std::string safe_total;
    for (int i = 0; i < 1000; ++i) {
        auto out = scanner.feed("a");
        safe_total += out.safe_text;
    }
    auto f = scanner.flush();
    assert(safe_total.size() + f.safe_text.size() == 1000);
}

void Test10_HarnessTurnLimit() {
    std::ofstream dummy("dummy.script");
    dummy << "role: assistant\nreply\n---\nrole: assistant\nreply\n---\n";
    dummy.close();

    HarnessConfig cfg; cfg.max_turns = 1;
    Harness harness(std::make_unique<ScriptedModelClient>("dummy.script"), cfg);
    MockInput in; in.inputs = {"hello", "hello again"};
    MockOutput out;
    auto reason = harness.run(in, out);
    assert(reason.kind == StopReason::Kind::TurnLimit);
}

void Test11_HarnessSentinelHalt() {
    std::ofstream dummy("dummy2.script");
    dummy << "role: assistant\nreply<|end_conversation|>\n---\n";
    dummy.close();

    HarnessConfig cfg; cfg.max_turns = 5;
    Harness harness(std::make_unique<ScriptedModelClient>("dummy2.script"), cfg);
    MockInput in; in.inputs = {"hello"};
    MockOutput out;
    auto reason = harness.run(in, out);
    assert(reason.kind == StopReason::Kind::Sentinel);
}

void Test12_TranscriptRoundTrip() {
    std::ofstream test_t("test_t.txt");
    test_t << "role: user\nhi\n---\nrole: assistant\nhello back\n---\n";
    test_t.close();
    
    ReplayModelClient replay("test_t.txt");
    Conversation conv; 
    conv.append(Message(Role::User, "hi"));
    auto msg = replay.generate(conv);
    assert(msg.content() == "hello back");
}

int main() {
    Test1_EmptyConversation();
    Test2_SystemMessageOrder();
    Test3_RuleOfFiveCopy();
    Test4_RuleOfFiveMove();
    Test5_GrowthBehavior();
    Test6_ScannerCleanText();
    Test7_ScannerSplitSentinel();
    Test8_ScannerFalseAlarms();
    Test9_ScannerBoundedMemory();
    Test10_HarnessTurnLimit();
    Test11_HarnessSentinelHalt();
    Test12_TranscriptRoundTrip();
    std::cout << "All 12 tests passed.\n";
    return 0;
}