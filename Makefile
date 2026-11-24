# 한 번에: make            (필요한 패키지 없으면 설치 → 빌드 → 실행, 폴더는 앱에서 "폴더 열기")
#          make DIR=~/폴더  (그 폴더를 바로 열어서 실행)
#          make test        (핵심 기능 자가 검사)
BUILD := build
DIR   ?=

.PHONY: run deps build test clean

run: build
	./$(BUILD)/inotify_tracker $(if $(DIR),"$(DIR)")

# 없는 것만 apt로 설치 (Qt6가 없는 배포판이면 Qt5로)
deps:
	@command -v g++ >/dev/null && command -v cmake >/dev/null || \
		sudo apt-get install -y g++ cmake
	@dpkg -s qt6-base-dev >/dev/null 2>&1 || dpkg -s qtbase5-dev >/dev/null 2>&1 || \
		sudo apt-get install -y qt6-base-dev || sudo apt-get install -y qtbase5-dev
	@fc-list :lang=ko 2>/dev/null | grep -q . || sudo apt-get install -y fonts-noto-cjk

build: deps
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: build
	./$(BUILD)/core_test

clean:
	rm -rf $(BUILD)
