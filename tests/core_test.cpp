// 핵심 기능 자가 검사 (리눅스에서): ./build/core_test
#include <poll.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "Tracker.h"
#include "Watcher.h"

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);   \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void put(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

static bool has(const std::vector<FileChange>& v, char st, const std::string& path) {
    for (auto& c : v)
        if (c.status == st && c.path == path) return true;
    return false;
}

static std::vector<FsEvent> drain(Watcher& w) {
    std::vector<FsEvent> all;
    pollfd pfd{w.fd(), POLLIN, 0};
    while (poll(&pfd, 1, 300) > 0) {
        auto e = w.readEvents();
        all.insert(all.end(), e.begin(), e.end());
    }
    return all;
}

static bool hasEv(const std::vector<FsEvent>& v, EventKind k, const std::string& path,
                  const std::string& from = "") {
    for (auto& e : v)
        if (e.kind == k && e.path == path && e.fromPath == from) return true;
    return false;
}

int main() {
    char tmpl[] = "/tmp/tracker_test_XXXXXX";
    fs::path dir = mkdtemp(tmpl);
    std::string err;

    // ---- 변경사항 추적 ----
    put(dir / "a.txt", "one\ntwo\n");
    put(dir / "sub/b.txt", "bee\n");
    put(dir / ".trackignore", "*.o\nbuild/\n");
    Tracker r;
    CHECK(r.open(dir.string(), &err));  // 지금 상태가 기준
    CHECK(r.status().empty());

    put(dir / "x.o", "obj");
    put(dir / "build/y.txt", "ignored");
    put(dir / "new.txt", "n\n");
    put(dir / "a.txt", "one\n2\n");
    fs::remove_all(dir / "sub");
    auto st = r.status();
    CHECK(has(st, 'A', "new.txt"));
    CHECK(has(st, 'M', "a.txt"));
    CHECK(has(st, 'D', "sub/b.txt"));
    CHECK(has(st, 'D', "sub/"));
    CHECK(!has(st, 'A', "x.o"));
    CHECK(!has(st, 'A', "build/"));
    CHECK(!has(st, 'A', "build/y.txt"));
    CHECK(st.size() == 4);
    CHECK(!fs::exists(dir / ".tracker"));  // 감시 폴더에 아무것도 만들지 않는다

    auto d = Tracker::diffLines(r.before("a.txt").value_or(""), r.working("a.txt").value_or(""));
    CHECK(d.size() == 3 && d[0].op == ' ' && d[1].op == '-' && d[1].text == "two" && d[2].op == '+' &&
          d[2].text == "2");
    CHECK(r.before("sub/b.txt").value_or("") == "bee\n");  // 지워진 파일도 기준 내용은 남아 있다
    CHECK(!r.before("new.txt"));

    r.resetBaseline();
    CHECK(r.status().empty());

    put(dir / "big.bin", std::string(Tracker::kMaxKeep + 1, 'x'));
    r.resetBaseline();
    CHECK(!r.before("big.bin"));  // 너무 큰 파일은 내용을 안 담아 둔다
    CHECK(!r.working("big.bin"));  // 지금 내용도 통째로 읽지 않는다 (화면 멈춤 방지)
    CHECK(r.working("a.txt").value_or("") == "one\n2\n");
    fs::remove(dir / "big.bin");
    r.resetBaseline();

    fs::create_symlink("a.txt", dir / "link.txt");  // 바로가기도 변경사항에 나온다
    CHECK(has(r.status(), 'A', "link.txt"));
    fs::remove(dir / "link.txt");

    CHECK(Tracker::isBinary(std::string("a\0b", 3)));
    CHECK(!Tracker::isBinary("text"));

    // ---- inotify 감시 ----
    Watcher w([&](const std::string& p) { return r.isIgnored(p); });
    CHECK(w.start(dir.string()));
    put(dir / "c.txt", "c");
    fs::create_directories(dir / "d1/d2");
    auto ev = drain(w);
    CHECK(hasEv(ev, EventKind::Created, "c.txt"));
    CHECK(hasEv(ev, EventKind::Modified, "c.txt"));  // 내용을 써서 만들었으면 생성 + 수정
    CHECK(hasEv(ev, EventKind::Created, "d1"));
    CHECK(hasEv(ev, EventKind::Created, "d1/d2"));

    put(dir / "d1/d2/deep.txt", "x");
    CHECK(hasEv(drain(w), EventKind::Created, "d1/d2/deep.txt"));

    put(dir / "c.txt", "cc");
    CHECK(hasEv(drain(w), EventKind::Modified, "c.txt"));

    fs::rename(dir / "d1", dir / "moved");
    CHECK(hasEv(drain(w), EventKind::Moved, "moved", "d1"));
    put(dir / "moved/d2/after.txt", "y");  // 옮긴 폴더 안의 watch 경로도 바뀌었는지
    CHECK(hasEv(drain(w), EventKind::Created, "moved/d2/after.txt"));

    // 빈 파일 만들기(touch)는 생성만
    { std::ofstream f(dir / "empty.txt"); }
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Created, "empty.txt"));
    CHECK(!hasEv(ev, EventKind::Modified, "empty.txt"));

    // vim처럼 "만들기"와 "써서 닫기"가 다른 read로 갈려 와도 생성 + 수정
    {
        std::ofstream f(dir / "split.c");
        CHECK(hasEv(drain(w), EventKind::Created, "split.c"));
        f << "x";
    }
    CHECK(hasEv(drain(w), EventKind::Modified, "split.c"));

    // sed -i 처럼 임시 파일에 쓰고 바꿔치기하는 저장은 "수정 원래파일" 한 줄
    put(dir / "sedTMP1", "new");
    fs::rename(dir / "sedTMP1", dir / "split.c");
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Modified, "split.c"));
    CHECK(!hasEv(ev, EventKind::Created, "sedTMP1"));
    CHECK(!hasEv(ev, EventKind::Moved, "split.c", "sedTMP1"));

    // vim 저장 방식: 원본을 백업(~)으로 옮기고 → 새로 쓰고 → 백업 삭제 = "수정" 한 줄 (삭제 · 생성으로 안 보임)
    for (const char* body : {"", "int x;\n"}) {
        put(dir / "vim.c", "old\n");
        drain(w);
        fs::rename(dir / "vim.c", dir / "vim.c~");
        put(dir / "vim.c", body);
        fs::remove(dir / "vim.c~");
        ev = drain(w);
        CHECK(hasEv(ev, EventKind::Modified, "vim.c"));
        CHECK(!hasEv(ev, EventKind::Deleted, "vim.c"));
        CHECK(!hasEv(ev, EventKind::Created, "vim.c"));
    }

    // 정말로 백업 이름으로 옮겨 버리면 2초 뒤 삭제로 알린다
    put(dir / "gone.c", "x");
    drain(w);
    fs::rename(dir / "gone.c", dir / "gone.c~");
    drain(w);
    CHECK(w.hasPending());
    usleep(2200 * 1000);
    CHECK(hasEv(w.readEvents(), EventKind::Deleted, "gone.c"));
    fs::remove(dir / "gone.c~");

    // 만들자마자 이름을 바꾼 폴더도 새 이름으로 감시되는지
    fs::create_directories(dir / "quick");
    fs::rename(dir / "quick", dir / "quick2");
    drain(w);
    CHECK(w.takeError().empty());
    put(dir / "quick2/in.txt", "q");
    CHECK(hasEv(drain(w), EventKind::Created, "quick2/in.txt"));

    // 방금 만든 파일을 "새 이름"으로 바꾸면 수정이 아니라 이동 (바꿔치기 저장과 헷갈리면 안 됨)
    put(dir / "notes.txt", "hi");
    drain(w);
    fs::rename(dir / "notes.txt", dir / "notes.md");
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Moved, "notes.md", "notes.txt"));
    CHECK(!hasEv(ev, EventKind::Modified, "notes.md"));

    // 같은 묶음 안에서 만들고 바로 새 이름으로 바꿔도 이동
    put(dir / "fast.txt", "f");
    fs::rename(dir / "fast.txt", dir / "fast2.txt");
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Moved, "fast2.txt", "fast.txt"));
    CHECK(!hasEv(ev, EventKind::Modified, "fast2.txt"));

    // 오래된 파일을 이미 있는 파일 위로 옮기는 건 저장이 아니라 이동 (덮어쓰기)
    put(dir / "old.txt", "o");
    put(dir / "target.txt", "t");
    drain(w);
    usleep(2100 * 1000);
    fs::rename(dir / "old.txt", dir / "target.txt");
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Moved, "target.txt", "old.txt"));

    // 다른 폴더로 옮기기 = 이동
    fs::create_directories(dir / "box");
    put(dir / "item.txt", "i");
    drain(w);
    fs::rename(dir / "item.txt", dir / "box/item.txt");
    CHECK(hasEv(drain(w), EventKind::Moved, "box/item.txt", "item.txt"));

    // 이름을 바꾼 폴더 안 파일도 "이미 있던 파일"로 기억해서, 그 파일의 바꿔치기 저장은 수정으로 보인다
    fs::rename(dir / "box", dir / "box2");
    CHECK(hasEv(drain(w), EventKind::Moved, "box2", "box"));
    put(dir / "box2/sedTMP2", "new");
    fs::rename(dir / "box2/sedTMP2", dir / "box2/item.txt");
    ev = drain(w);
    CHECK(hasEv(ev, EventKind::Modified, "box2/item.txt"));
    CHECK(!hasEv(ev, EventKind::Moved, "box2/item.txt", "box2/sedTMP2"));

    fs::remove(dir / "c.txt");
    CHECK(hasEv(drain(w), EventKind::Deleted, "c.txt"));

    put(dir / "skip.o", "o");
    put(dir / ".main.c.swp", "vim");  // 편집기 임시 파일은 기본 무시
    put(dir / "main.c~", "backup");
    put(dir / ".config/QtProject.conf", "x");  // 앱 자신의 설정 파일도 무시
    ev = drain(w);
    CHECK(!hasEv(ev, EventKind::Created, "skip.o"));
    CHECK(!hasEv(ev, EventKind::Created, ".main.c.swp"));
    CHECK(!hasEv(ev, EventKind::Created, "main.c~"));
    CHECK(!hasEv(ev, EventKind::Created, ".config/QtProject.conf"));

    w.stop();
    fs::remove_all(dir);
    if (failures) std::printf("%d FAILED\n", failures);
    else std::printf("all passed\n");
    return failures ? 1 : 0;
}
