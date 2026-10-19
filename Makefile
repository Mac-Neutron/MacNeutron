.PHONY: build test smoke app release bridge bridge-check presenter presenter-check dxmt-tests dxmt-tests-arm64ec dxmt-check dxil-corpus wine-arm64 wine-arm64-export wine-arm64-tests wine-arm64-check media-check lanes-check wine-arm64-winetests

APP = build/MacNeutron.app
# Every Windows-side binary is built with MacNeutron's own llvm-mingw (Clang); dxmt/toolchain.sh builds it once.
MINGW_BIN = $(shell sh dxmt/toolchain.sh)
# The arm64 Windows side's CPU, wine-arm64/build.sh's MACNEUTRON_CPU (the M1 baseline for generic). -O3 and
# ThinLTO throughout (ARM64EC's needs our llvm-mingw's lld, wine-arm64/patches/llvm/0002).
MACNEUTRON_CPU ?= apple-m5
PE_MARCH = $(if $(filter generic,$(MACNEUTRON_CPU)),-march=armv8.5-a+fp16fml+aes+sha3,-mcpu=$(MACNEUTRON_CPU))
MINGW = $(MINGW_BIN)/x86_64-w64-mingw32-clang -O3 -flto=thin -static -s
MINGWXX = $(MINGW_BIN)/x86_64-w64-mingw32-clang++ -O3 -flto=thin -static -s
MINGW_A64 = $(MINGW_BIN)/aarch64-w64-mingw32-clang -O3 -flto=thin -static -s $(PE_MARCH)
BRIDGE = build/bridge
PRESENTER = build/presenter

build:
	swift build -c release

test:
	swift test

# The launcher on wine.app in a tool folder assembled with `macneutron install`, and the install itself (gates L5,
# L6; real Wine, no Steam). See Tests/Smoke/smoke.sh.
smoke: build bridge wine-arm64
	sh Tests/Smoke/smoke.sh

# The Steam bridge (docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md): steam.exe and its test
# helper for the arm64 runtime in $(BRIDGE)/arm64, and the x64 steamprobe.exe, which runs under FEX.
bridge:
	mkdir -p $(BRIDGE)/arm64/tests
	$(MINGW) -fms-extensions -mwindows -o $(BRIDGE)/steamprobe.exe bridge/probe.c
	$(MINGW_A64) -mwindows -o $(BRIDGE)/arm64/steam.exe bridge/steam.c -ladvapi32 -lntdll -luser32 -lshell32 -lshlwapi -lole32
	$(MINGW_A64) -mwindows -o $(BRIDGE)/arm64/tests/helper.exe bridge/tests/helper.c -ladvapi32 -lshell32 -luser32
	$(MINGW_A64) -o $(BRIDGE)/arm64/tests/helper-console.exe bridge/tests/helper.c -ladvapi32 -lshell32 -luser32

# steam.exe on wine.app, directly and through the launcher (real Wine, no Steam).
bridge-check: build bridge wine-arm64
	sh bridge/probe.sh --redact-self-test
	sh bridge/check.sh

# The MetalFX presenter's test programs: present_loop.exe for Wine, cmaa2_check for the Mac (CMAA2, no Wine). The
# presenter itself is built into wine.app (wine-arm64/build.sh), where winemetal.so loads it.
presenter:
	mkdir -p $(PRESENTER)
	$(MINGW) -o $(PRESENTER)/present_loop.exe presenter/tests/present_loop.c -ld3d11 -ldxgi -luser32 -lgdi32 -ldxguid -luuid
	/usr/bin/clang -fobjc-arc -O3 -o $(PRESENTER)/cmaa2_check presenter/tests/cmaa2_check.m -framework Foundation \
		-framework Metal -framework QuartzCore

# The presenter in wine.app through the launcher on DXMT (real Wine, no Steam).
presenter-check: build wine-arm64 presenter
	sh presenter/check.sh

# D3D12, D3D11 and D3D10 test programs for our DXMT, built in parallel (one compiler per core), and dxbc.exe, which
# compiles the D3D11 and D3D10 ones' shaders into dxmt/tests/shaders/d3d11 (run on the macOS runtime: dxmt/tests/dxbc.cpp).
DXMT_TESTS = $(patsubst dxmt/tests/%.cpp,build/dxmt-tests/%.exe,$(wildcard dxmt/tests/d3d1[012]_*.cpp)) \
	build/dxmt-tests/dxbc.exe
dxmt-tests:
	mkdir -p build/dxmt-tests
	$(MAKE) -s -j$(shell sysctl -n hw.ncpu) $(DXMT_TESTS)
build/dxmt-tests/%.exe: dxmt/tests/%.cpp dxmt/tests/d3d12_common.hpp dxmt/tests/d3d11_common.hpp
	$(MINGWXX) -std=c++17 -o $@ $< -ld3d12 -ld3d11 -ld3d10 -ld3dcompiler -ldxgi -luser32 -lpsapi

# The same programs and present_loop for ARM64EC, for the arm64 runtime (arm64 DXMT spec §7), built in parallel.
MINGW_EC = $(MINGW_BIN)/arm64ec-w64-mingw32-clang -O3 -flto=thin -static -s $(PE_MARCH)
MINGWXX_EC = $(MINGW_BIN)/arm64ec-w64-mingw32-clang++ -O3 -flto=thin -static -s $(PE_MARCH)
DXMT_TESTS_EC = $(patsubst dxmt/tests/%.cpp,build/dxmt-tests-arm64ec/%.exe,$(wildcard dxmt/tests/d3d1[012]_*.cpp)) \
	build/dxmt-tests-arm64ec/present_loop.exe
dxmt-tests-arm64ec:
	mkdir -p build/dxmt-tests-arm64ec
	$(MAKE) -s -j$(shell sysctl -n hw.ncpu) $(DXMT_TESTS_EC)
build/dxmt-tests-arm64ec/%.exe: dxmt/tests/%.cpp dxmt/tests/d3d12_common.hpp dxmt/tests/d3d11_common.hpp
	$(MINGWXX_EC) -std=c++17 -o $@ $< -ld3d12 -ld3d11 -ld3d10 -ld3dcompiler -ldxgi -luser32 -lpsapi
build/dxmt-tests-arm64ec/present_loop.exe: presenter/tests/present_loop.c
	$(MINGW_EC) -o $@ $< -ld3d11 -ldxgi -luser32 -lgdi32 -ldxguid -luuid

# Translate a folder of captured DXIL shaders offline (DIR=~/dxil-smite2); never commit a game's shaders.
dxil-corpus: wine-arm64
	build/wine-arm64/dxil-translate "$(DIR)"

# Our DXMT in wine.app through the launcher against D3DMetal in the frozen Rosetta reference
# (tools/freeze-rosetta-reference.sh), with the x64 test programs under FEX, then the ARM64EC ones (real Wine, no
# Steam); see dxmt/check.sh.
dxmt-check: build wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter
	sh dxmt/tests/build_test.sh
	sh dxmt/check.sh
	MACNEUTRON_ARM64_TESTS=build/dxmt-tests-arm64ec MACNEUTRON_ARM64_LOOP=build/dxmt-tests-arm64ec/present_loop.exe DXMT_CHECK_WORK="$${TMPDIR:-/tmp}/macneutron dxmt arm64ec" sh dxmt/check.sh

# A development MacNeutron.app, ad hoc signed, with the CLI, wine.app, the microVM's host tools (eac-vm/host/stage.sh)
# and steam.exe inside. Never open it on this Mac:
# its start installs into the real tool folder. Needs the signing variables (it builds wine.app); make release
# builds the notarized one.
app: build bridge wine-arm64
	@! ps -axo comm= | LC_ALL=C /usr/bin/grep -qF "$(abspath $(APP))/" || { echo 'app: quit the MacNeutron running from $(APP) first' >&2; exit 1; }
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Helpers $(APP)/Contents/Resources
	cp App/Info.plist $(APP)/Contents/Info.plist
	cp .build/release/MacNeutronApp $(APP)/Contents/MacOS/MacNeutron
	cp .build/release/macneutron $(APP)/Contents/Helpers/macneutron
	codesign --force --sign - $(APP)/Contents/Helpers/macneutron
	cp -c -R build/wine-arm64/wine.app $(APP)/Contents/Helpers/wine.app
	sh eac-vm/host/stage.sh $(APP)/Contents/Helpers/vm
	mkdir -p $(APP)/Contents/Resources/vm && mv $(APP)/Contents/Helpers/vm/run-*.sh $(APP)/Contents/Helpers/vm/dxmt-translator $(APP)/Contents/Resources/vm/
	# With the release's identity, as eac-vm/host/build.sh signs it: the TPM keys a release made trust it, no password.
	codesign --force --sign "$${MACNEUTRON_SIGN_IDENTITY:--}" --entitlements eac-vm/host/hv.plist $(APP)/Contents/Helpers/vm/macneutron-vm
	codesign --force --sign - $(APP)/Contents/Helpers/vm/gvproxy  # the upstream universal binary comes unsigned
	sh eac-vm/setup-tools/build.sh > /dev/null && cp build/vm-setup-tools/mkfs.erofs $(APP)/Contents/Helpers/mkfs.erofs
	codesign --force --sign - $(APP)/Contents/Helpers/mkfs.erofs
	cp $(BRIDGE)/arm64/steam.exe $(APP)/Contents/Resources/steam.exe
	codesign --force --sign - $(APP)

# The release: notarized MacNeutron.app, its zip and the source archive (spec §6.3). VERSION=x.y.z; needs the
# signing and notary variables and the network. A bad VERSION stops it before anything is built.
release:
	@sh release/release.sh --check-version "$(VERSION)"
	$(MAKE) build bridge wine-arm64
	sh release/release.sh "$(VERSION)"

# Native arm64 Wine 11.19 with our patches, FEX for x64 code and our DXMT for ARM64X
# (docs/superpowers/specs/2026-10-02-macneutron-native-arm64-design.md §5, §6; 2026-10-03-macneutron-arm64-dxmt-design.md).
# First run: shallow clones of Wine, FEX and DXMT, an arm64 LLVM build and some compiling; see wine-arm64/build.sh.
wine-arm64:
	sh wine-arm64/build.sh

# Commits made in build/wine-arm64-src/wine, fex and dxmt back into wine-arm64/patches/wine, fex and dxmt.
wine-arm64-export:
	sh wine-arm64/export.sh

# Test programs for the arm64 stack, built in parallel. The file name's prefix picks the compiler (arm64-, arm64ec-,
# x64-); a program that needs more flags sets WA_FLAGS_<name> (arm64ec-viewec: -lonecore), which comes last. x64-bench, a
# benchmark (gate G4), is built -O2: the later -O wins. winshot is a Mac program: it reads a window's pixels off the screen.
WA_TESTS = $(patsubst wine-arm64/tests/%.c,build/wine-arm64-tests/%.exe,$(wildcard wine-arm64/tests/*.c)) \
	$(patsubst wine-arm64/tests/%.cpp,build/wine-arm64-tests/%.exe,$(wildcard wine-arm64/tests/*.cpp))
WA_FLAGS = -O1 -fms-extensions -D_WIN32_WINNT=0x0A00
WA_FLAGS_arm64ec-viewec = -lonecore
WA_FLAGS_x64-bench = -O2
WA_FLAGS_arm64-fonts-tls = -lgdi32 -lsecur32 -ldwrite -lcrypt32
WA_FLAGS_arm64-x18v = -lntdll
WA_FLAGS_arm64-x18path = -lntdll
WA_FLAGS_arm64-media-mf = -lmfplat -lmfreadwrite -lole32
WA_FLAGS_x64-sync = -lsynchronization
WA_FLAGS_arm64-xcall = -O2 -fno-builtin -lshlwapi
WA_FLAGS_arm64-crt = -fno-builtin
WA_FLAGS_x64-steamclient = -municode -lwintrust
# The media programs (video playback spec §8) built for x64 too, from the same source: games are x64, run under FEX.
WA_MEDIA_X64 = $(patsubst wine-arm64/tests/arm64-media-%.c,build/wine-arm64-tests/x64-media-%.exe,\
	$(wildcard wine-arm64/tests/arm64-media-*.c))
wine-arm64-tests:
	mkdir -p build/wine-arm64-tests
	$(MAKE) -s -j$(shell sysctl -n hw.ncpu) $(WA_TESTS) $(WA_MEDIA_X64) build/wine-arm64-tests/x64-x18path.exe build/wine-arm64-tests/winshot \
		build/wine-arm64-tests/arm64-sync.exe build/wine-arm64-tests/arm64ec-sync.exe \
		build/wine-arm64-tests/arm64ec-xcall.exe build/wine-arm64-tests/x64-xcall.exe \
		build/wine-arm64-tests/arm64ec-crt.exe build/wine-arm64-tests/x64-crt.exe
build/wine-arm64-tests/arm64-%.exe: wine-arm64/tests/arm64-%.c
	$(MINGW_BIN)/aarch64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_$(basename $(@F)))
build/wine-arm64-tests/arm64ec-%.exe: wine-arm64/tests/arm64ec-%.c
	$(MINGW_BIN)/arm64ec-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_$(basename $(@F)))
build/wine-arm64-tests/x64-%.exe: wine-arm64/tests/x64-%.c
	$(MINGW_BIN)/x86_64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_$(basename $(@F)))
build/wine-arm64-tests/x64-%.exe: wine-arm64/tests/x64-%.cpp
	$(MINGW_BIN)/x86_64-w64-mingw32-clang++ $(WA_FLAGS) -static -o $@ $< $(WA_FLAGS_$(basename $(@F)))
# arm64-x18path's source built for x64 too: its paths under FEX (ship-base spec §9, T2).
build/wine-arm64-tests/x64-x18path.exe: wine-arm64/tests/arm64-x18path.c
	$(MINGW_BIN)/x86_64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-x18path)
# The measurement lanes (check.sh's lanes step): x64-sync's and arm64-xcall's sources in all three lanes, ARM64,
# ARM64EC and x64 (arm64-xcall.exe and x64-sync.exe come from the rules above).
build/wine-arm64-tests/arm64-sync.exe: wine-arm64/tests/x64-sync.c
	$(MINGW_BIN)/aarch64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_x64-sync)
build/wine-arm64-tests/arm64ec-sync.exe: wine-arm64/tests/x64-sync.c
	$(MINGW_BIN)/arm64ec-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_x64-sync)
build/wine-arm64-tests/arm64ec-xcall.exe: wine-arm64/tests/arm64-xcall.c
	$(MINGW_BIN)/arm64ec-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-xcall)
build/wine-arm64-tests/x64-xcall.exe: wine-arm64/tests/arm64-xcall.c
	$(MINGW_BIN)/x86_64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-xcall)
# arm64-crt's source in the other two lanes (batch Task 6): msvcrt's string routines on ARM64EC and from x64 code.
build/wine-arm64-tests/arm64ec-crt.exe: wine-arm64/tests/arm64-crt.c
	$(MINGW_BIN)/arm64ec-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-crt)
build/wine-arm64-tests/x64-crt.exe: wine-arm64/tests/arm64-crt.c
	$(MINGW_BIN)/x86_64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-crt)
build/wine-arm64-tests/x64-media-%.exe: wine-arm64/tests/arm64-media-%.c
	$(MINGW_BIN)/x86_64-w64-mingw32-clang $(WA_FLAGS) -o $@ $< $(WA_FLAGS_arm64-media-$*)
build/wine-arm64-tests/winshot: wine-arm64/tools/winshot.c
	/usr/bin/clang -O1 -o $@ $< -framework CoreGraphics -framework ImageIO -framework CoreFoundation

# The arm64 runtime on this Mac: boots, runs native ARM64 code, leaves nothing behind (spec §7.3). Needs
# MACNEUTRON_SIGN_IDENTITY and MACNEUTRON_PROVISIONING_PROFILE (the build signs the runtime), and the frozen Rosetta
# reference (MACNEUTRON_REFERENCE, tools/freeze-rosetta-reference.sh): gate G4's baseline runs on its own launcher,
# and the dxmt-* steps' D3DMetal reference is its GPTK (dxmt/check.sh).
wine-arm64-check: build bridge wine-arm64 wine-arm64-tests dxmt-tests presenter dxmt-tests-arm64ec wine-arm64-winetests
	sh wine-arm64/tests/mode_test.sh
	sh wine-arm64/tests/profile_test.sh
	sh wine-arm64/tests/translator_key_test.sh
	sh wine-arm64/tests/prefer_native_test.sh build/wine-arm64/wine.app
	sh wine-arm64/tests/licences_test.sh build/wine-arm64/wine.app
	sh wine-arm64/tests/licences_test.sh --self-test build/wine-arm64/wine.app
	sh wine-arm64/check.sh

# Video playback in games (docs/superpowers/specs/2026-10-08-macneutron-video-playback-design.md §8): the media
# programs on wine.app, arm64 and x64 under FEX, in a fresh prefix, on Wine's own test clips read in place; the media-*
# steps of wine-arm64/check.sh, which its full run leaves out until they pass. Needs the signing variables.
media-check: wine-arm64 wine-arm64-tests
	sh wine-arm64/check.sh media-mf

# The measurement lanes (batch Task 2): x64-sync and arm64-xcall as ARM64, ARM64EC and x64 programs (x64 under FEX),
# timed in the prefix's server's mode (msync on); gated only on each program's PASS and its number of time rows. Each
# `info <program> <row> <ns>` line is a median; wine-arm64/tools/lanes_report.py turns three runs into a table.
lanes-check: wine-arm64 wine-arm64-tests
	sh wine-arm64/check.sh lanes

# Wine's own conformance tests for check.sh's winetests step (batch Task 8): ntdll, kernel32, atl, atl100, msvcirt and
# (batch Task 6) msvcrt's test programs for the arm64, arm64ec and x64 lanes in build/wine-arm64-tests/winetests/.
# wine-build is configured --disable-tests, and an ARM64X tree links each test as one ARM64X exe, which runs only its
# ARM64 view, so they come from two test-only trees on wine-build's tools (build/wine-arm64-src/wine-tests-arm64,
# wine-tests-ec), neither staged into wine.app. To configure one again, remove its folder.
wine-arm64-winetests: wine-arm64
	sh wine-arm64/winetests.sh
