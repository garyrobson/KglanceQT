build:
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DKGLANCEQT_WITH_SQLITE=ON
    cmake --build build -j 2

test: build
    ctest --test-dir build --output-on-failure

fmt:
    clang-format -i src/*.cpp src/*.h tests/*.cpp

fmt-check:
    clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp

package:
    makepkg --dir packaging/arch --force

# Owned nested KWin with two outputs, then mixed 1×/1.5× scaling.
wayland-test: build
    python3 tests/wayland_smoke.py build/window_tests

# Current compositor; activates only the test window and preserves output settings.
wayland-current-test: build
    python3 tests/wayland_smoke.py build/window_tests --current

benchmark: build
    python3 tests/benchmark.py build/window_tests

package-snapshot:
    python3 packaging/arch/build-snapshot.py
