build:
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build -j 2

test: build
    ctest --test-dir build --output-on-failure

fmt:
    clang-format -i src/*.cpp src/*.h tests/*.cpp

fmt-check:
    clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp

package:
    makepkg --dir packaging/arch --force
