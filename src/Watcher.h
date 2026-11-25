#pragma once
// inotify 래퍼: 폴더 하나를 하위 폴더까지 통째로 감시한다. Qt와 무관해서 테스트에서 바로 쓸 수 있다.
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class EventKind { Created = 0, Modified = 1, Deleted = 2, Moved = 3, Overflow = 4 };

struct FsEvent {
    EventKind kind;
    std::string path;      // 감시 폴더 기준 상대 경로
    std::string fromPath;  // Moved일 때 원래 경로
    bool isDir = false;
};

class Watcher {
public:
    // ignore(상대경로)가 true면 그 파일·폴더는 감시하지도 알리지도 않는다
    explicit Watcher(std::function<bool(const std::string&)> ignore = {});
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    bool start(const std::string& root);
    void stop();
    int fd() const { return fd_; }
    size_t watchCount() const { return wdPath_.size(); }
    // 백업 이름으로 옮겨져 "다시 만들어질지" 기다리는 파일이 있으면 true → 2초 뒤 readEvents()를 한 번 더 불러 줄 것
    bool hasPending() const { return !replacing_.empty(); }
    // fd가 읽을 수 있을 때 부른다. 쌓인 이벤트를 전부 읽어서 돌려준다 (non-blocking)
    std::vector<FsEvent> readEvents();
    // 마지막 오류(감시 한도 초과 등)를 꺼내고 비운다
    std::string takeError();

private:
    bool addRecursive(const std::string& absDir, std::vector<FsEvent>* found);
    void renameDir(const std::string& fromAbs, const std::string& toAbs);
    void forgetDir(const std::string& abs);
    void renameKnown(const std::string& fromRel, const std::string& toRel);
    void forgetKnown(const std::string& rel);
    std::string rel(const std::string& abs) const;
    std::string absOf(const std::string& rel) const;

    int fd_ = -1;
    std::string root_;
    std::unordered_map<int, std::string> wdPath_;  // watch descriptor -> 절대 경로
    std::function<bool(const std::string&)> ignore_;
    std::string err_;
    bool isFreshTemp(const std::string& rel, const std::vector<FsEvent>& batch) const;
    std::string lastCreated_;      // 생성 직후의 첫 닫기 판단용 (한 번 쓰면 비움)
    std::string lastCreatedEver_;  // 바꿔치기 저장 판단용
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> replacing_;  // 백업 이름으로 옮겨진 원본
    // 지금 있는 파일(상대 경로). 이름 바꾸기가 "이미 있던 파일을 덮어쓴 저장"인지 "그냥 이동"인지 가르는 데 쓴다
    std::unordered_set<std::string> files_;
    std::chrono::steady_clock::time_point lastCreatedAt_;
};
