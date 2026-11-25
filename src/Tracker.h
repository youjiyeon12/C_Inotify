#pragma once
// 변경사항 추적: 폴더를 연 순간(또는 "기준 다시 잡기")의 상태를 메모리에 기억해 두고,
// 지금 상태와 비교해 생김 · 고쳐짐 · 사라짐과 바뀐 줄을 알려 준다. 디스크에는 아무것도 쓰지 않는다. Qt와 무관.
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// 경로 -> 내용 해시. 폴더는 "폴더/" 키에 "dir" 값으로 넣어 빈 폴더도 추적한다
using Snapshot = std::map<std::string, std::string>;

struct FileChange {
    char status;  // 'A' 생김, 'M' 고쳐짐, 'D' 사라짐
    std::string path;
};

struct DiffLine {
    char op;  // ' ' 그대로, '+' 추가, '-' 삭제
    std::string text;
};

// 기준 때 내용을 담아 두지 않은 이유 (before()가 nullopt일 때 화면 안내용)
enum class NoBefore { NotInBase, TooBig, Binary, OverBudget };

class Tracker {
public:
    bool open(const std::string& root, std::string* err);  // 열면서 지금 상태를 기준으로 잡는다
    bool isOpen() const { return !root_.empty(); }
    std::string root() const { return root_.string(); }
    void loadIgnore();  // .trackignore 다시 읽기
    bool isIgnored(const std::string& rel) const;

    void resetBaseline();                     // 지금 상태를 새 기준으로
    std::vector<FileChange> status();  // 기준 대비 지금 바뀐 것 (안 바뀐 파일은 다시 읽지 않는다)
    // 기준 때 내용. 폴더이거나 기준에 없거나 안 담아 둔 파일이면 nullopt (이유는 whyNoBefore)
    std::optional<std::string> before(const std::string& rel) const;
    NoBefore whyNoBefore(const std::string& rel) const;
    // 지금 내용. 폴더이거나 없거나 kMaxKeep보다 큰 파일이면 nullopt
    std::optional<std::string> working(const std::string& rel) const;
    size_t keptBytes() const { return keptBytes_; }
    void setKeepBudget(size_t bytes) { keepBudget_ = bytes; }  // 다음 resetBaseline부터 적용 (테스트용)

    static std::vector<FileChange> changes(const Snapshot& from, const Snapshot& to);
    static std::vector<DiffLine> diffLines(const std::string& a, const std::string& b);
    static bool isBinary(const std::string& data);
    static std::string hashOf(const std::string& data);
    static constexpr size_t kMaxKeep = 1024 * 1024;              // 이보다 큰 파일은 내용 비교 생략
    static constexpr size_t kKeepBudget = 64 * 1024 * 1024;      // 기준 내용을 담아 두는 전체 한도
    static constexpr uintmax_t kHashLimit = 16 * 1024 * 1024;    // 이보다 큰 파일은 읽지 않고 크기·시각으로 비교

private:
    // 파일을 다시 읽지 않으려고 기억해 두는 정보. 크기·수정 시각이 그대로면 해시를 재사용한다
    struct CacheEntry {
        uintmax_t size;
        std::filesystem::file_time_type mtime;
        std::string hash;
        bool settled;  // 해시를 잴 때 이미 수정된 지 오래였는지 (방금 쓰인 파일은 같은 시각에 또 바뀔 수 있어 다시 읽는다)
    };
    Snapshot scan(bool keepBase);

    std::filesystem::path root_;
    std::vector<std::string> ignore_;
    Snapshot base_;
    std::map<std::string, std::string> baseData_;  // 기준 때 파일 내용 (텍스트 · kMaxKeep 이하 · 한도 안에서만)
    std::map<std::string, NoBefore> baseSkipped_;  // 기준에 있지만 내용을 안 담아 둔 파일과 그 이유
    size_t keptBytes_ = 0;
    size_t keepBudget_ = kKeepBudget;
    std::unordered_map<std::string, CacheEntry> cache_;
};
