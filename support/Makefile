.ONESHELL:
.DELETE_ON_ERROR:
# add "-x" for debugging any target
.SHELLFLAGS := -eu -o pipefail -c
SHELL := bash
MAKEFLAGS += --warn-undefined-variables
MAKEFLAGS += --no-builtin-rules
MAKEFLAGS += --no-print-directory

all: help

##@ Commands
.PHONY: help
help: ## Display this help.
	@awk 'BEGIN {
		FS = ":.*##";
		printf "\nUsage:\n  make \033[36m<target>\033[0m\n"
	}
	/^[a-zA-Z_0-9-]+:.*?##/ {
		printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2
	}
	/^##@/ {
		printf "\n\033[1m%s\033[0m\n", substr($$0, 5)
	}' $(MAKEFILE_LIST)

.PHONY: icon
icon: ## kutes svg to Icons.
	@GIT_ROOT=$(shell git rev-parse --show-toplevel)
	@SUPPORT_DIR=$${GIT_ROOT}/support
	@RES_DIR=$${GIT_ROOT}/src/res
	@pushd $${SUPPORT_DIR}
	@# librsvg2-tools
	@rsvg-convert -w 1024 -h 1024 --keep-aspect-ratio kutes.drawio.svg -o kutes.png
	@# from https://github.com/idesis-gmbh/png2icons
	@png2icons kutes.png kutes -allp -bz -i
	@cp kutes.ico $${RES_DIR}/logo256.ico
	@cp kutes.ico $${RES_DIR}/logo48.ico
	@cp kutes.icns $${RES_DIR}/logo.icns
	@rm kutes.png kutes.ico kutes.icns
	@popd

.PHONY: cp-clangfmt
cp-clangfmt: ## copies .clang-format file from vendor/nappgui_src
	@pushd ../
	@cp vendor/nappgui_src/*/.clang-format .clang-format
	@popd

.PHONY: check-vendor
check-vendor: ## deps in vendor folder should be updated when corresponding sha is changed.
	@pushd ../
	@COUNT=$$(git diff --name-only -G 'URL_HASH SHA256=' CMakeLists.txt | wc -l)
	@VENDOR=$$(git diff --name-only vendor/ | wc -l)
	@if [[ $${COUNT} -ne 0 && $${VENDOR} -eq 0 ]]; then echo Expected changes in [vendor/] folder && false; fi;
	@popd

.PHONY: clean-cmakecache
clean-cmakecache: ## clean CMakeCache.txt files in all build directories.
	@pushd ../
	@find . -type f -name CMakeCache.txt -exec rm -v {} +
	@popd

.PHONY: clean-cmakefiles
clean-cmakefiles: ## clean CMakeFiles folders in all build directories.
	@pushd ../
	@find . -type d -name CMakeFiles -exec rm -rf -v {} +
	@popd

.PHONY: clean-cmake
clean-cmake: clean-cmakecache clean-cmakefiles ## clean CMakeCache.txt files and CMakeFiles folders in all build directories.

FIX_DIR :=
.PHONY: lowercase-headers
lowercase-headers: ## lowercase the windows related included header names.
	@test "x$(FIX_DIR)" != x
	@pushd $(FIX_DIR)
	@grep -rln '#include <Windows.h>' | xargs sed -i 's/Windows.h/windows.h/'
	@grep -rln '#include <ShlObj.h>' | xargs sed -i 's/ShlObj.h/shlobj.h/'
	@grep -rln '#include <VersionHelpers.h>' | xargs sed -i 's/VersionHelpers.h/versionhelpers.h/'
	@grep -rln '#include <Commctrl.h>' | xargs sed -i 's/Commctrl.h/commctrl.h/'
	@grep -rln '#include <Richedit.h>' | xargs sed -i 's/Richedit.h/richedit.h/'
	@grep -rln '#include <WinInet.h>' | xargs sed -i 's/WinInet.h/wininet.h/'
	@popd

PATCH_FILE :=
.PHONY: create-patch
create-patch: ## creates patch file from vendor folder changes
	@test "x${PATCH_FILE}" != x
	@pushd ../
	@git diff vendor/ | sed 's,vendor/nappgui_src/cache/,,g' > support/patches/${PATCH_FILE}
	@popd

EXTRA_ARGS :=
.PHONY: linux64-c
linux64-c: ## configure ninja multi-config build for linux64 target host.
	@pushd ../
	@cmake -G "Ninja Multi-Config" -S . -B build-linux64 $(EXTRA_ARGS)
	@ln -srf build-linux64/compile_commands.json compile_commands.json
	@popd

.PHONY: linux64-b
linux64-b: ## build for linux64 target host.
	@pushd ../
	@cmake --build build-linux64 $(EXTRA_ARGS)
	@ln -srf build-linux64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-mingwamd64-c
cross-mingwamd64-c: ## configure ninja multi-config build for windows amd64 system host.
	@pushd ../
	@cmake -G "Ninja Multi-Config" -S . -B build-cross-mingwamd64 \
    --toolchain support/cross-mingwamd64-toolchain.cmake \
    -DNATIVE_NRC_PATH=build-linux64/tools/build/Debug/bin/nrc $(EXTRA_ARGS)
	@ln -srf build-cross-mingwamd64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-mingwamd64-b
cross-mingwamd64-b: ## build for windows amd64 system host.
	@pushd ../
	@cmake --build build-cross-mingwamd64 $(EXTRA_ARGS)
	@ln -srf build-cross-mingwamd64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-osxaarch64-c
cross-osxaarch64-c: ## configure ninja multi-config build for MacOSX aarch64 system host.
	@pushd ../
	@cmake -G "Ninja Multi-Config" -S . -B build-cross-osxaarch64 \
    --toolchain support/cross-osxaarch64-toolchain.cmake \
    -DNATIVE_NRC_PATH=build-linux64/tools/build/Debug/bin/nrc $(EXTRA_ARGS)
	@ln -srf build-cross-osxaarch64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-osxaarch64-b
cross-osxaarch64-b: ## build for MacOSX aarch64 system host.
	@pushd ../
	@cmake --build build-cross-osxaarch64 $(EXTRA_ARGS)
	@ln -srf build-cross-osxaarch64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-osxamd64-c
cross-osxamd64-c: ## configure ninja multi-config build for MacOSX amd64 system host.
	@pushd ../
	@cmake -G "Ninja Multi-Config" -S . -B build-cross-osxamd64 \
    --toolchain support/cross-osxamd64-toolchain.cmake \
    -DNATIVE_NRC_PATH=build-linux64/tools/build/Debug/bin/nrc $(EXTRA_ARGS)
	@ln -srf build-cross-osxamd64/compile_commands.json compile_commands.json
	@popd

.PHONY: cross-osxamd64-b
cross-osxamd64-b: ## build for MacOSX amd64 system host.
	@pushd ../
	@cmake --build build-cross-osxamd64 $(EXTRA_ARGS)
	@ln -srf build-cross-osxamd64/compile_commands.json compile_commands.json
	@popd

