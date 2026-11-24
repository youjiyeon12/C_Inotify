#include "Watcher.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
// IN_CLOSE_WRITE를 "수정"으로 쓴다: IN_MODIFY는 write()마다 와서 너무 시끄럽다
constexpr uint32_t kMask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO |
                           IN_ONLYDIR | IN_DONT_FOLLOW | IN_EXCL_UNLINK;

bool underDir(const std::string& path, const std::string& dir) {
    return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 &&
                           path[dir.size()] == '/');
}
}  // namespace

Watcher::Watcher(std::function<bool(const std::string&)> ignore) : ignore_(std::move(ignore)) {}

Watcher::~Watcher() { stop(); }

bool Watcher::start(const std::string& root) {
    stop();
    root_ = fs::path(root).lexically_normal().string();
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ < 0) {
        err_ = std::string("inotify를 열 수 없어요: ") + std::strerror(errno);
        return false;
    }
    if (!addRecursive(root_, nullptr)) {
        std::string e = err_;
        stop();
        err_ = e;
        return false;
    }
    return true;
}

void Watcher::stop() {
    if (fd_ >= 0) close(fd_);  // 닫으면 커널이 watch를 전부 정리한다
    fd_ = -1;
    wdPath_.clear();
}

std::string Watcher::takeError() {
    std::string e;
    e.swap(err_);
    return e;
}

std::string Watcher::rel(const std::string& abs) const {
    return abs.size() > root_.size() ? abs.substr(root_.size() + 1) : std::string();
}

std::string Watcher::absOf(const std::string& r) const { return r.empty() ? root_ : root_ + "/" + r; }

bool Watcher::addRecursive(const std::string& absDir, std::vector<FsEvent>* found) {
    int wd = inotify_add_watch(fd_, absDir.c_str(), kMask);
    if (wd < 0) {
        // 만들자마자 지워지거나 옮겨진 폴더(ENOENT)는 흔한 일이라 조용히 넘긴다
        if (errno == ENOENT && absDir != root_) return false;
        err_ = errno == ENOSPC
                   ? "감시 개수 한도를 넘었어요 (sysctl fs.inotify.max_user_watches 를 늘려 주세요)"
                   : "감시 실패 " + absDir + ": " + std::strerror(errno);
        return false;
    }
    wdPath_[wd] = absDir;
    // watch를 먼저 걸고 훑는다: 그 사이에 생긴 파일도 둘 중 하나로는 잡힌다
    std::error_code ec;
    for (fs::directory_iterator it(absDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::string abs = it->path().string();
        std::string r = rel(abs);
        if (ignore_ && ignore_(r)) continue;
        std::error_code sec;
        bool dir = it->symlink_status(sec).type() == fs::file_type::directory;
        if (found) found->push_back({EventKind::Created, r, "", dir});
        if (dir) addRecursive(abs, found);  // 하위 폴더 실패는 건너뛰고 계속
    }
    return true;
}

void Watcher::renameDir(const std::string& fromAbs, const std::string& toAbs) {
    // 폴더가 트리 안에서 옮겨지면 watch는 그대로 살아 있으니 경로 이름만 바꿔 준다
    for (auto& [wd, path] : wdPath_)
        if (underDir(path, fromAbs)) path = toAbs + path.substr(fromAbs.size());
}

void Watcher::forgetDir(const std::string& abs) {
    for (auto it = wdPath_.begin(); it != wdPath_.end();) {
        if (underDir(it->second, abs)) {
            inotify_rm_watch(fd_, it->first);
            it = wdPath_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Watcher::isFreshTemp(const std::string& rel, const std::vector<FsEvent>& batch) const {
    for (auto& e : batch)
        if (e.path == rel && e.kind == EventKind::Created) return true;
    return rel == lastCreatedEver_ && std::chrono::steady_clock::now() - lastCreatedAt_ < std::chrono::seconds(2);
}

std::vector<FsEvent> Watcher::readEvents() {
    std::vector<FsEvent> out;
    if (fd_ < 0) return out;
    // 백업 이름으로 옮겨진 뒤 2초 안에 다시 안 만들어졌으면 정말 사라진 것
    // shortcut: 다음 이벤트가 올 때 알린다. 그 사이 아무 일이 없으면 삭제 표시가 늦어질 수 있음
    auto now = std::chrono::steady_clock::now();
    for (auto it = replacing_.begin(); it != replacing_.end();) {
        if (now - it->second > std::chrono::seconds(2)) {
            out.push_back({EventKind::Deleted, it->first, "", false});
            it = replacing_.erase(it);
        } else {
            ++it;
        }
    }
    // MOVED_FROM은 짝(MOVED_TO)이 올 때까지 cookie로 붙잡아 둔다
    std::unordered_map<uint32_t, FsEvent> pendingFrom;
    alignas(inotify_event) char buf[64 * 1024];

    for (;;) {
        ssize_t n = read(fd_, buf, sizeof buf);
        if (n <= 0) break;  // EAGAIN: 다 읽음
        for (char* p = buf; p < buf + n;) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            p += sizeof(inotify_event) + ev->len;

            if (ev->mask & IN_Q_OVERFLOW) {
                out.push_back({EventKind::Overflow, "", "", false});
                continue;
            }
            if (ev->mask & IN_IGNORED) {  // 폴더가 지워져 커널이 watch를 뗐다
                wdPath_.erase(ev->wd);
                continue;
            }
            auto dirIt = wdPath_.find(ev->wd);
            if (dirIt == wdPath_.end() || ev->len == 0) continue;
            std::string abs = dirIt->second + "/" + ev->name;
            std::string r = rel(abs);
            bool isDir = ev->mask & IN_ISDIR;
            if (ignore_ && ignore_(r)) {
                // vim 등의 저장: 원본을 무시 대상 이름(test.c~)으로 옮겨 두고 새로 쓴다 → 삭제가 아니라 곧 다시 만들어질 파일
                if (ev->mask & IN_MOVED_TO) {
                    auto from = pendingFrom.find(ev->cookie);
                    if (from != pendingFrom.end() && !from->second.isDir) {
                        replacing_[from->second.fromPath] = std::chrono::steady_clock::now();
                        pendingFrom.erase(from);
                    }
                }
                continue;
            }

            if (ev->mask & IN_MOVED_FROM) {
                pendingFrom[ev->cookie] = {EventKind::Moved, "", r, isDir};
            } else if (ev->mask & IN_MOVED_TO) {
                auto from = pendingFrom.find(ev->cookie);
                if (from != pendingFrom.end()) {
                    FsEvent e = from->second;
                    pendingFrom.erase(from);
                    e.path = r;
                    // 임시 파일에 쓰고 진짜 이름으로 바꿔치기(sed -i, 여러 편집기의 저장)는 "수정" 한 번으로 보인다
                    if (!isDir && isFreshTemp(e.fromPath, out)) {
                        out.erase(std::remove_if(out.begin(), out.end(),
                                                 [&](const FsEvent& x) { return x.path == e.fromPath; }),
                                  out.end());
                        out.push_back({EventKind::Modified, r, "", false});
                        continue;
                    }
                    if (isDir) {
                        renameDir(absOf(e.fromPath), abs);
                        // 원래 폴더에 watch를 걸기 전에 옮겨졌으면 새 이름으로 건다
                        bool watched = std::any_of(wdPath_.begin(), wdPath_.end(),
                                                   [&](auto& kv) { return kv.second == abs; });
                        if (!watched) addRecursive(abs, nullptr);
                    }
                    out.push_back(e);
                } else {  // 바깥에서 들어온 것 = 새로 생김
                    out.push_back({EventKind::Created, r, "", isDir});
                    if (isDir) addRecursive(abs, &out);
                }
            } else if (ev->mask & IN_CREATE) {
                if (!isDir && replacing_.count(r)) continue;  // 저장 중 다시 만들어진 원본: 닫힐 때 "수정"으로 알린다
                out.push_back({EventKind::Created, r, "", isDir});
                lastCreated_ = lastCreatedEver_ = r;
                lastCreatedAt_ = std::chrono::steady_clock::now();
                if (isDir) addRecursive(abs, &out);
            } else if (ev->mask & IN_DELETE) {
                out.push_back({EventKind::Deleted, r, "", isDir});
            } else if (ev->mask & IN_CLOSE_WRITE) {
                if (replacing_.erase(r)) {  // 백업 후 새로 쓴 원본 = 수정
                    if (out.empty() || out.back().path != r || out.back().kind != EventKind::Modified)
                        out.push_back({EventKind::Modified, r, "", false});
                    continue;
                }
                // 방금 만든 빈 파일을 닫은 것(touch 등)은 "생성"만. 내용을 써서 닫았으면 "수정"도 알린다
                bool justCreated = r == lastCreated_ &&
                                   std::chrono::steady_clock::now() - lastCreatedAt_ < std::chrono::seconds(1);
                if (r == lastCreated_) lastCreated_.clear();
                std::error_code ec;
                if (justCreated && fs::file_size(abs, ec) == 0 && !ec) continue;
                // 같은 묶음에서 방금 수정으로 알린 파일이면 또 알리지 않는다
                if (!out.empty() && out.back().path == r && out.back().kind == EventKind::Modified) continue;
                out.push_back({EventKind::Modified, r, "", isDir});
            }
        }
    }

    // shortcut: 짝이 같은 read 묶음에 안 오면 (트리 밖으로 나간 것으로 보고) 삭제 처리. 드물게 이동이 삭제+생성으로 보일 수 있음
    for (auto& [cookie, e] : pendingFrom) {
        if (e.isDir) forgetDir(absOf(e.fromPath));
        out.push_back({EventKind::Deleted, e.fromPath, "", e.isDir});
    }
    return out;
}
