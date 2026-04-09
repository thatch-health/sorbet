#include "doctest/doctest.h"
#include "common/FileOps.h"
#include "common/common.h"
#include "common/strings/formatting.h"
#include "main/lsp/LSPConfiguration.h"
#include "main/lsp/LSPMessage.h"
#include "main/lsp/LSPOutput.h"
#include "main/lsp/MessageQueueState.h"
#include "main/lsp/watchman/WatchmanProcess.h"
#include "spdlog/sinks/null_sink.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <thread>

using namespace std;
using namespace std::chrono_literals;

namespace sorbet::realmain::lsp::watchman::test {
namespace {

auto nullSink = make_shared<spdlog::sinks::null_sink_mt>();
auto logger = make_shared<spdlog::logger>("watchman-process-test", nullSink);

shared_ptr<LSPConfiguration> makeConfig(string_view rootPath) {
    options::Options opts;
    opts.rawInputDirNames.emplace_back(string(rootPath));
    opts.runLSP = true;
    return make_shared<LSPConfiguration>(opts, make_shared<LSPOutputToVector>(), logger, false);
}

string makeTempDir() {
    array<char, sizeof("/tmp/watchman-process-test-XXXXXX")> dirTemplate{};
    memcpy(dirTemplate.data(), "/tmp/watchman-process-test-XXXXXX", dirTemplate.size());
    auto *dir = mkdtemp(dirTemplate.data());
    REQUIRE_NE(dir, nullptr);
    return string(dir);
}

int waitForPidFile(string_view pidFile) {
    for (auto deadline = chrono::steady_clock::now() + 5s; chrono::steady_clock::now() < deadline;
         this_thread::sleep_for(10ms)) {
        if (!FileOps::exists(string(pidFile))) {
            continue;
        }

        return stoi(FileOps::read(string(pidFile)));
    }

    return -1;
}

ProcessStatus waitForProcessStatus(int pid, ProcessStatus expectedStatus) {
    for (auto deadline = chrono::steady_clock::now() + 5s; chrono::steady_clock::now() < deadline;
         this_thread::sleep_for(10ms)) {
        auto status = processExists(pid);
        if (status == expectedStatus) {
            return status;
        }
    }

    return processExists(pid);
}

} // namespace

TEST_CASE("WatchmanProcess stops its subprocess on teardown") {
    auto tempDir = makeTempDir();
    auto scriptPath = fmt::format("{}/fake-watchman.sh", tempDir);
    auto pidFile = fmt::format("{}/fake-watchman.pid", tempDir);

    FileOps::write(scriptPath, fmt::format("#!/bin/sh\nprintf '%s\\n' \"$$\" > \"{}\"\nexec /bin/cat\n", pidFile));
    REQUIRE_EQ(chmod(scriptPath.c_str(), 0755), 0);

    MessageQueueState messageQueue;
    absl::Mutex messageQueueMutex;
    absl::Notification initializedNotification;
    initializedNotification.Notify();
    auto config = makeConfig(tempDir);

    {
        WatchmanProcess process(logger, scriptPath, tempDir, vector<string>({"rb", "rbi"}), messageQueue,
                                messageQueueMutex, initializedNotification, config, "");

        auto pid = waitForPidFile(pidFile);
        REQUIRE_GT(pid, 0);
        REQUIRE_EQ(waitForProcessStatus(pid, ProcessStatus::Running), ProcessStatus::Running);
    }

    auto pid = stoi(FileOps::read(pidFile));
    CHECK_EQ(waitForProcessStatus(pid, ProcessStatus::Missing), ProcessStatus::Missing);

    FileOps::removeFile(pidFile);
    FileOps::removeFile(scriptPath);
    FileOps::removeDir(tempDir);
}

} // namespace sorbet::realmain::lsp::watchman::test
