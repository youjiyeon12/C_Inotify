#include "Tracker.h"

#include <fnmatch.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

bool Tracker::open(const std::string& root, std::string* err) {
    std::error_code ec;
    fs::path r = fs::canonical(root, ec);
    if (ec || !fs::is_directory(r)) {
        if (err) *err = "폴더를 열 수 없어요: " + root;
        return false;
    }
    root_ = r;
    loadIgnore();
    resetBaseline();
    return true;
}

void Tracker::loadIgnore() {
    // 편집기가 만들었다 지우는 임시 파일 (vim 스왑 · 저장 시험 파일 · 사용 기록, 백업~, emacs 잠금)은 기본으로 뺀다
    // + 이 앱(Qt)이 폴더 고르기 창을 닫을 때 쓰는 설정 파일
    ignore_ = {".*.sw?", "*~", "4913", ".viminfo*", ".#*", "QtProject.conf*"};
    std::istringstream in(readFile(root_ / ".trackignore").value_or(""));
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '/'))
            line.pop_back();
        if (!line.empty() && line[0] != '#') ignore_.push_back(line);
    }
}

bool Tracker::isIgnored(const std::string& rel) const {
    // 경로의 앞부분(상위 폴더)마다 검사해서, 무시한 폴더 안의 것도 무시된다
    for (size_t end = rel.find('/');; end = rel.find('/', end + 1)) {
        std::string prefix = rel.substr(0, end);
        if (prefix.empty()) break;
        std::string base = prefix.substr(prefix.find_last_of('/') + 1);
        for (auto& pat : ignore_)
            if (fnmatch(pat.c_str(), prefix.c_str(), 0) == 0 || fnmatch(pat.c_str(), base.c_str(), 0) == 0)
                return true;
        if (end == std::string::npos) break;
    }
    return false;
}

std::string Tracker::hashOf(const std::string& data) {
    unsigned long long h = 1469598103934665603ULL;  // FNV-1a 64비트
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

bool Tracker::isBinary(const std::string& data) { return data.find('\0') != std::string::npos; }

Snapshot Tracker::scan(bool keepBase) {
    // 크기 · 수정 시각이 그대로인 파일은 다시 읽지 않고 지난번 해시를 쓴다 (git의 index와 같은 방식).
    // 기준을 잡을 때(keepBase)는 내용을 담아 둬야 하니 kMaxKeep 이하 파일은 읽는다
    Snapshot snap;
    std::unordered_map<std::string, CacheEntry> nextCache;
    nextCache.reserve(cache_.size());
    const auto now = fs::file_time_type::clock::now();
    std::error_code ec;
    const size_t cut = root_.generic_string().size() + 1;
    for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::string rel = it->path().generic_string().substr(cut);
        std::error_code sec;
        fs::file_status st = it->symlink_status(sec);
        if (rel.find('\n') != std::string::npos || isIgnored(rel)) {
            if (fs::is_directory(st)) it.disable_recursion_pending();
            continue;
        }
        if (fs::is_symlink(st)) {  // 바로가기는 가리키는 곳이 바뀌면 "고쳐짐"
            snap[rel] = "link:" + fs::read_symlink(it->path(), sec).string();
        } else if (fs::is_directory(st)) {
            snap[rel + "/"] = "dir";
        } else if (fs::is_regular_file(st)) {  // shortcut: 장치 · 소켓 파일은 추적 안 함
            std::error_code e1, e2;
            uintmax_t size = fs::file_size(it->path(), e1);
            auto mtime = fs::last_write_time(it->path(), e2);
            if (e1 || e2) continue;  // 그 사이 지워짐
            bool wantData = keepBase && size <= kMaxKeep;
            auto c = cache_.find(rel);
            if (!wantData && c != cache_.end() && c->second.settled && c->second.size == size &&
                c->second.mtime == mtime) {
                if (keepBase) baseSkipped_[rel] = NoBefore::TooBig;  // 기준을 잡는데 안 읽었다 = kMaxKeep보다 큼
                snap[rel] = c->second.hash;
                nextCache[rel] = std::move(c->second);
                continue;
            }
            std::string hash;
            if (size > kHashLimit) {
                // 아주 큰 파일(커지는 로그 등)은 매번 통째로 읽지 않는다. 크기나 수정 시각이 바뀌면 "고쳐짐"
                hash = "meta:" + std::to_string(size) + ":" + std::to_string(mtime.time_since_epoch().count());
            } else {
                auto data = readFile(it->path());
                if (!data) continue;
                hash = hashOf(*data);
                if (keepBase) {
                    if (data->size() > kMaxKeep) baseSkipped_[rel] = NoBefore::TooBig;
                    else if (isBinary(*data)) baseSkipped_[rel] = NoBefore::Binary;
                    else if (keptBytes_ + data->size() > keepBudget_) baseSkipped_[rel] = NoBefore::OverBudget;
                    else {
                        keptBytes_ += data->size();
                        baseData_[rel] = std::move(*data);
                    }
                }
            }
            if (keepBase && size > kHashLimit) baseSkipped_[rel] = NoBefore::TooBig;
            // 방금(2초 안) 쓰인 파일은 같은 시각 안에 또 바뀌어도 시각이 안 달라질 수 있어 다음에도 다시 읽는다
            bool settled = now - mtime > std::chrono::seconds(2);
            snap[rel] = hash;
            nextCache[rel] = {size, mtime, std::move(hash), settled};
        }
    }
    cache_ = std::move(nextCache);  // 사라진 파일 정보는 버린다
    return snap;
}

void Tracker::resetBaseline() {
    baseData_.clear();
    baseSkipped_.clear();
    keptBytes_ = 0;
    base_ = scan(true);
}

NoBefore Tracker::whyNoBefore(const std::string& rel) const {
    auto it = baseSkipped_.find(rel);
    return it == baseSkipped_.end() ? NoBefore::NotInBase : it->second;
}

std::vector<FileChange> Tracker::changes(const Snapshot& from, const Snapshot& to) {
    std::vector<FileChange> out;
    for (auto& [path, hash] : to) {
        auto it = from.find(path);
        if (it == from.end()) out.push_back({'A', path});
        else if (it->second != hash) out.push_back({'M', path});
    }
    for (auto& [path, hash] : from)
        if (!to.count(path)) out.push_back({'D', path});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.path < b.path; });
    return out;
}

std::vector<FileChange> Tracker::status() { return changes(base_, scan(false)); }

std::optional<std::string> Tracker::before(const std::string& rel) const {
    auto it = baseData_.find(rel);
    if (it == baseData_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> Tracker::working(const std::string& rel) const {
    if (!rel.empty() && rel.back() == '/') return std::nullopt;
    // 큰 파일(로그 등)은 통째로 읽지 않는다: 읽고 줄 비교를 그리느라 화면이 멈춘다
    std::error_code ec;
    auto size = fs::file_size(root_ / rel, ec);
    if (ec || size > kMaxKeep) return std::nullopt;
    return readFile(root_ / rel);
}

std::vector<DiffLine> Tracker::diffLines(const std::string& a, const std::string& b) {
    auto split = [](const std::string& s) {
        std::vector<std::string> v;
        std::istringstream in(s);
        for (std::string l; std::getline(in, l);) v.push_back(l);
        return v;
    };
    auto A = split(a), B = split(b);
    size_t n = A.size(), m = B.size();
    std::vector<DiffLine> out;
    // shortcut: LCS 표가 O(n*m)이라 아주 큰 파일은 "전부 삭제 + 전부 추가"로 보여 준다. 필요하면 Myers diff로
    if (n * m > 4000000) {
        for (auto& l : A) out.push_back({'-', l});
        for (auto& l : B) out.push_back({'+', l});
        return out;
    }
    std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            L[i][j] = A[i] == B[j] ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (A[i] == B[j]) {
            out.push_back({' ', A[i]});
            ++i, ++j;
        } else if (L[i + 1][j] >= L[i][j + 1]) {
            out.push_back({'-', A[i++]});
        } else {
            out.push_back({'+', B[j++]});
        }
    }
    while (i < n) out.push_back({'-', A[i++]});
    while (j < m) out.push_back({'+', B[j++]});
    return out;
}
