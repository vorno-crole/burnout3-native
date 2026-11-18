# Host build of the port foundations (b3host adapter + GtMathPs2 on host SIMD) and their unit tests.
#   make test          build and run every test binary (fast + PS2-faithful GtMath)
#   make CXX=clang++   another compiler; make CXXFLAGS='-O0 -g -fsanitize=address,undefined' test
# -ffp-contract=off: the EE has no fused multiply-add; contraction would change results (b3host/fenv.h).
# Machine-specific paths (where the decomp snapshot, SDL3, the game data, the Windows toolchain live) go in a git-ignored
# `local.env` beside this Makefile -- or in the main checkout when this is a git worktree -- as `NAME ?= value` lines
# (local.env.example lists them); the shell scripts in tools/ read the same file (tools/local_env.sh). Without one,
# everything defaults to paths inside or beside the repo, or the system's packages (docs/BUILDING.md).
B3_LOCAL_ENV ?= $(firstword $(wildcard $(CURDIR)/local.env $(dir $(abspath $(shell git rev-parse --path-format=absolute --git-common-dir 2>/dev/null)))local.env))
ifneq ($(B3_LOCAL_ENV),)
include $(B3_LOCAL_ENV)
endif
# `make` alone builds the game and the launcher (build/native_play, build/libb3render.so, build/b3launcher) on every
# core: the first build compiles ~5,000 generated units (gen/native), so a serial make took very long (seen
# 2026-11-10 on a first build). `make -jN` still chooses; `make test` builds and runs the unit tests.
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
# the default job count: the cores, but at most one compiler per ~2 GB of available memory (Linux), so a big machine
# with less RAM does not run out
JOBS_DEFAULT := $(shell n=$(NPROC); m=$$(awk '/MemAvailable/ {print int($$2 / 2097152)}' /proc/meminfo 2>/dev/null); \
                  if [ -n "$$m" ] && [ "$$m" -ge 1 ] && [ "$$m" -lt "$$n" ]; then n=$$m; fi; echo $$n)
ifeq ($(filter -j% j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(JOBS_DEFAULT)
endif
.DEFAULT_GOAL := game
CXX ?= g++
CXXFLAGS ?= -O2 -g
# -msse2 on x86 only (mac1): the target of $(CXX) decides, so a cross compiler (CXX='clang++ --target=aarch64-...') and
# Apple Silicon (arm64) build the same sources with the portable / NEON paths (b3host/ps2float.h, b3host/fenv.h).
TARGET_ARCH_TRIPLE := $(shell $(CXX) $(CXXFLAGS) -dumpmachine 2>/dev/null)
X86_FLAGS := $(if $(filter x86_64% i686% i386% amd64%,$(TARGET_ARCH_TRIPLE)),-msse2)
REQFLAGS := -std=c++20 -Wall -Wextra -Werror=return-type -ffp-contract=off -fno-fast-math $(X86_FLAGS)
CPPFLAGS += -Iinclude -Iinclude/gtmath
BUILD := build
# boot2 (gen1, 2026-11-07): compiler and generator temporaries under build/, not a RAM-backed /tmp that other jobs
# fill (a native build failed there with "No space left on device")
export TMPDIR := $(abspath $(BUILD))/tmp
$(shell mkdir -p $(BUILD)/tmp)

# test builds: the version stamp (include/b3report/version.h): build/version_info.cpp from tools/release/version_info.sh
# (the VERSION file, the git commit, the date), rewritten only when its text changes -- so a new commit recompiles it alone
VERSION_CPP := $(BUILD)/version_info.cpp
$(shell tools/release/version_info.sh $(VERSION_CPP))
# the bug report's archive and contents (include/b3report: zip.h, bug_report.h, run_logs.h); zlib (deflate)
REPORT_SRC := src/b3report/zip.cpp src/b3report/bug_report.cpp $(VERSION_CPP)
REPORT_DEP := $(wildcard include/b3report/*.h)
HOST_SRC := src/b3host/host.cpp src/b3host/null_backend.cpp src/b3host/hw.cpp src/b3host/vubind.cpp
PORT_SRC := src/port/libm_overrides.cpp
PORT_OBJ := $(PORT_SRC:%.cpp=$(BUILD)/%.o)
HOST_OBJ := $(HOST_SRC:%.cpp=$(BUILD)/%.o)

TESTS := $(BUILD)/test_b3host $(BUILD)/test_abi $(BUILD)/test_gtmath_fast $(BUILD)/test_gtmath_ps2 $(BUILD)/test_ps2float \
         $(BUILD)/test_libm $(BUILD)/test_libm_more $(BUILD)/test_overrides $(BUILD)/test_libm_cost $(BUILD)/test_fenv $(BUILD)/test_gen_init

# Optional: a matched decomp source compiled unchanged through the port headers (decomp repo read-only).
# Pinned committed snapshot (git archive of a decomp commit), not the live tree the decomp's pipeline edits.
# Default: a snapshot or a clone beside the repo (../decomp-snap/boot2w, ../burnout3-retail-decomp); none: the build uses
# the committed generated code (gen/, NATIVE_FROM=bundle) and the decomp's optional checks skip.
DECOMP ?= $(firstword $(wildcard $(abspath ..)/decomp-snap/boot2w $(abspath ..)/burnout3-retail-decomp) $(abspath ..)/decomp-snap/boot2w)
# the live decomp clone, read only for the VU images a snapshot may lack
DECOMP_LIVE ?= $(abspath ..)/burnout3-retail-decomp
DECOMP_UNIT := $(DECOMP)/src/b3/_unclassified/B3EnvironmentManager_002BCEC0.cpp
# The retail ELF, for the libm oracle (tests/r5900_mini.h runs retail code from it); those checks skip without it.
# mac1: overridable, e.g. RETAIL_ELF=<disc folder>/SLUS_210.50 (the disc's own executable: the same file, sha256 e118dba9...).
RETAIL_ELF ?= $(DECOMP)/orig/SLUS_210.50
LIBM_DEFS := $(if $(wildcard $(RETAIL_ELF)),'-DB3_RETAIL_ELF="$(abspath $(RETAIL_ELF))"')
# mac5: as VU_STAMP below -- the binaries that compile LIBM_DEFS in depend on this stamp, rewritten when the path changes
# (a new DECOMP snapshot): test_overrides kept the deleted boot2m snapshot's ELF path and crashed reading an empty file
LIBM_STAMP := build/libm_defs.stamp
LIBM_STAMP_NOW := $(if $(wildcard $(LIBM_STAMP)),$(file <$(LIBM_STAMP)))
# public1: written when missing too (no retail ELF in a fresh checkout: LIBM_DEFS empty, so the comparison alone never
# wrote it, and "No rule to make target build/libm_defs.stamp")
ifneq ($(if $(wildcard $(LIBM_STAMP)),x$(strip $(LIBM_STAMP_NOW))),x$(strip $(LIBM_DEFS)))
$(shell mkdir -p build)
$(file >$(LIBM_STAMP),$(LIBM_DEFS))
endif
ifneq ($(wildcard $(DECOMP_UNIT)),)
TESTS += $(BUILD)/test_decomp_unit_fast $(BUILD)/test_decomp_unit_ps2
endif
# Decomp game functions through tools/decomp2host (docs/decomp-on-host.md): generated into gen/decomp (committed, so a checkout builds without the decomp) from the decomp.
GAME_SELECT := config/game_overrides.txt
GEN := gen/decomp
# mac2: GCC-only flags for the generated units under GCC; under clang (Apple Silicon) its counterpart of -fpermissive's
# address-of-a-temporary leniency instead (clang has no -fno-aggressive-loop-optimizations).
CXX_IS_CLANG := $(if $(findstring clang,$(shell $(CXX) --version 2>/dev/null | head -1)),1)
# gen1 (determinism, 2026-11-07): -flifetime-dse=1 under GCC. Its default (2) takes the clobber at a constructor's start
# as the object's birth and deletes -ftrivial-auto-var-init's zero store before it, so a local of a class with a
# user-provided constructor (CGtMatrix3x4, decomp classes) kept host stack bytes: UpdateSuspension 0x1CC900's wheel
# matrix w lanes, a new value every run (vu0) [C: GCC 15.2, -O1/-O2: a CGtMatrix3x4 local after a 0xA5 stack fill reads
# a5a5a5a5 with the default, 0 with =1 or =0]. Clang zeroes them anyway (no such pass).
GEN_CC_FLAGS := $(if $(CXX_IS_CLANG),-Wno-address-of-temporary,-fno-aggressive-loop-optimizations -flifetime-dse=1)
GENFLAGS := -ftrivial-auto-var-init=zero -fno-strict-aliasing $(GEN_CC_FLAGS) -fstrict-flex-arrays=0 -fno-access-control -fpermissive -w -I$(GEN)/include
ifneq ($(wildcard $(DECOMP)/config/proven_units.txt),)
TESTS += $(BUILD)/test_game_units_fast $(BUILD)/test_game_units_ps2 $(BUILD)/test_vu_units
endif
TESTS += $(BUILD)/test_eeasm_fast $(BUILD)/test_eeasm_ps2
# Calls between native and guest code (port7): probe3 (MWCC code, committed image) under r5900_mini, and, with the decomp,
# a generated non-leaf function (config/test_calls.txt, generated into $(BUILD)/gen_calls) against retail.
TESTS += $(BUILD)/test_calls
# The native backend (S0, docs/standalone.md §8): synthetic tests (no disc data, no decomp needed).
TESTS += $(BUILD)/test_native_backend $(BUILD)/test_native_platform $(BUILD)/test_hook_table
# The dev IPU model (movie1, src/native/ipu_model.h through hw_model_dev.h; not in the shipped game): hand-made bitstreams, no disc data.
TESTS += $(BUILD)/test_ipu_model
# The native RW Audio IOP executor (audio1, docs/audio.md §3.1) with the stub and the SPU2-equivalent mixer: synthetic
# tests; the mixer's own unit tests (test_spu2_mixer). The replay of a
# logged command stream (test B0) is `make test_rwa_iop_b0` (needs the log; skips without it), not part of `make test`.
TESTS += $(BUILD)/test_rwa_iop $(BUILD)/test_spu2_mixer
# B3ROUTE.IRX (the distortion router, docs/audio-b3route.md) beside the executor: RPC, core-1 IRQ cycle, EE round trip.
TESTS += $(BUILD)/test_b3route
CALLS_SELECT := config/test_calls.txt
CALLS_GEN := $(BUILD)/gen_calls
ifneq ($(wildcard $(DECOMP)/config/proven_units.txt),)
CALLS_DEP := $(CALLS_GEN)/manifest.tsv
CALLS_FLAGS = -fno-strict-aliasing $(GEN_CC_FLAGS) -fstrict-flex-arrays=0 -fno-access-control -fpermissive -w -DB3_GEN_CALLS -I$(CALLS_GEN)/include $(wildcard $(CALLS_GEN)/units/*.cpp)
endif

all: $(TESTS)

$(BUILD)/%.o: %.cpp $(wildcard include/*/*.h include/gtmath/b3/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_b3host: tests/test_b3host.cpp $(HOST_OBJ) tests/test.h
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(HOST_OBJ) -o $@
$(BUILD)/test_abi: tests/test_abi.cpp $(HOST_OBJ) tests/test.h
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(HOST_OBJ) -o $@
# gen1: the generated units' flags themselves (GENFLAGS, -O2): uninitialised locals are zero (determinism)
$(BUILD)/test_gen_init: tests/test_gen_init.cpp tests/test.h $(wildcard include/gtmath/b3/*.h include/b3host/*.h) Makefile
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) -O2 -DB3_PS2_FLOAT=1 $< -o $@
$(BUILD)/test_gtmath_fast: tests/test_gtmath.cpp tests/test.h $(wildcard include/gtmath/b3/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -DB3_PS2_FLOAT=0 $< -o $@
$(BUILD)/test_gtmath_ps2: tests/test_gtmath.cpp tests/test.h $(wildcard include/gtmath/b3/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -DB3_PS2_FLOAT=1 $< -o $@
$(BUILD)/test_ps2float: tests/test_ps2float.cpp tests/test.h include/b3host/ps2float.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -DB3_PS2_FLOAT=1 $< -o $@
# rt1: b3fp::hw (the host float unit under Ps2FloatEnv) against b3fp::ref, op by op (include/b3host/ps2float_hw.h)
TESTS += $(BUILD)/test_vufloat
$(BUILD)/test_vufloat: tests/test_vufloat.cpp tests/test.h include/b3host/fenv.h include/b3host/ps2float.h include/b3host/ps2float_hw.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(VU0_FP_ENV) $< -o $@
# mac1: Ps2FloatEnv (MXCSR on x86, FPCR on AArch64) against b3fp::ref
$(BUILD)/test_fenv: tests/test_fenv.cpp tests/test.h include/b3host/fenv.h include/b3host/ps2float.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD)/test_libm: tests/test_libm.cpp tests/test.h tests/r5900_mini.h tests/libm_oracle.h $(wildcard include/b3libm/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) $< -o $@
$(BUILD)/test_libm_more: tests/test_libm_more.cpp tests/test.h tests/r5900_mini.h tests/libm_oracle.h $(wildcard include/b3libm/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) $< -o $@

$(BUILD)/test_overrides: tests/test_overrides.cpp tests/test.h tests/r5900_mini.h tests/libm_oracle.h $(PORT_OBJ) $(HOST_OBJ) $(wildcard include/b3port/*.h include/b3libm/*.h include/b3host/*.h)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) $< $(PORT_OBJ) $(HOST_OBJ) -o $@

# Exact libm cost (docs: include/b3libm/ps2_core.h "Exact cost"): the acceptance test, and the tool that dumps the
# block-hit rows tools/libm_cost/fit.py fits the per-block costs from (not run by `make test`).
$(BUILD)/test_libm_cost: tests/test_libm_cost.cpp tests/test.h tests/r5900_mini.h tests/libm_cost_entries.h tests/libm_sweeps.h $(PORT_OBJ) $(HOST_OBJ) $(wildcard include/b3port/*.h include/b3libm/*.h include/b3host/*.h)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) $< $(PORT_OBJ) $(HOST_OBJ) -o $@
$(BUILD)/libm_cost_dump: tests/libm_cost_dump.cpp tests/r5900_mini.h tests/libm_cost_entries.h tests/libm_sweeps.h $(wildcard include/b3libm/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) -DB3LIBM_TRACE_BLOCKS -pthread $< -o $@

$(BUILD)/test_decomp_unit_%: tests/test_decomp_unit.cpp tests/test.h $(DECOMP_UNIT) $(wildcard include/gtmath/b3/*.h include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -DB3_PS2_FLOAT=$(if $(filter ps2,$*),1,0) '-DB3_DECOMP_UNIT="$(abspath $(DECOMP_UNIT))"' $< -o $@

ifneq ($(wildcard $(DECOMP)/config/proven_units.txt),)
$(GEN)/manifest.tsv: $(GAME_SELECT) tools/decomp2host/decomp2host.py $(wildcard src/libs/rwa/*.cpp src/libs/rwa/include/rwa/*.h)
	@# boot2 (2026-11-06): hermetic -- into an empty $(GEN).new, no DECOMP2HOST_CACHE, then synced with --delete,
	@# so nothing left in the committed tree (or a cache) reaches the output: any branch regenerates it byte for byte
	python3 -c "import shutil; shutil.rmtree('$(GEN).new', ignore_errors=True)"
	env -u DECOMP2HOST_CACHE python3 -I tools/decomp2host/decomp2host.py --decomp $(DECOMP) --out $(GEN).new --select $(GAME_SELECT) --objects $(BUILD)/gen_fast,$(BUILD)/gen_ps2
	rsync -rc --delete --exclude README.md --out-format='gen/decomp: %n' $(GEN).new/ $(GEN)/
	$(call check_returns_gen,$(GEN)/units)
	touch $@
else
# public1: no decomp (a code-only checkout, CI): the committed gen/decomp is used as it is. A fresh clone's file dates
# are arbitrary, so the rule above would try to regenerate it from a decomp that is not there.
$(GEN)/manifest.tsv: ;
endif
# port7: ~1000 generated units, so each is one object per float mode (built in parallel: `make test` runs make -j)
GEN_UNITS = $(wildcard $(GEN)/units/*.cpp)
GEN_OBJ_fast = $(patsubst $(GEN)/units/%.cpp,$(BUILD)/gen_fast/%.o,$(GEN_UNITS))
GEN_OBJ_ps2 = $(patsubst $(GEN)/units/%.cpp,$(BUILD)/gen_ps2/%.o,$(GEN_UNITS))
GEN_HDRS = $(wildcard include/gtmath/b3/*.h include/b3host/*.h)
$(BUILD)/gen_fast/%.o: $(GEN)/units/%.cpp $(GEN)/manifest.tsv $(GEN_HDRS)
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) -DB3_PS2_FLOAT=0 -c $< -o $@
$(BUILD)/gen_ps2/%.o: $(GEN)/units/%.cpp $(GEN)/manifest.tsv $(GEN_HDRS)
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) -DB3_PS2_FLOAT=1 -c $< -o $@
.SECONDEXPANSION:
$(BUILD)/test_game_units_%: tests/test_game_units.cpp tests/test.h $(GEN)/manifest.tsv $(HOST_SRC) $$(GEN_OBJ_$$*) $(GEN_HDRS)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) -DB3_PS2_FLOAT=$(if $(filter ps2,$*),1,0) $< $(GEN_OBJ_$*) $(HOST_SRC) -o $@

# VU0 decomp functions (inline asm via tools/decomp2host/eeasm.py) against retail code in tests/r5900_mini.h.
$(BUILD)/test_vu_units: tests/test_vu_units.cpp tests/test.h tests/r5900_mini.h $(GEN)/manifest.tsv $(HOST_SRC) $$(GEN_OBJ_ps2) $(GEN_HDRS)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) $(LIBM_DEFS) -DB3_PS2_FLOAT=1 $< $(GEN_OBJ_ps2) $(HOST_SRC) -o $@
$(CALLS_GEN)/manifest.tsv: $(CALLS_SELECT) tools/decomp2host/decomp2host.py
	python3 -I tools/decomp2host/decomp2host.py --decomp $(DECOMP) --out $(CALLS_GEN) --select $(CALLS_SELECT)
$(BUILD)/test_calls: tests/test_calls.cpp tests/test.h tests/r5900_mini.h tests/mini_backend.h tools/abi_probe/probe3.bin $(CALLS_DEP) $(HOST_SRC) $(wildcard include/gtmath/b3/*.h include/b3host/*.h)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) -DB3_PS2_FLOAT=1 -DB3_BOUNCE_CANARY=1 '-DB3_PROBE3_DIR="$(abspath tools/abi_probe)"' $< $(CALLS_FLAGS) $(HOST_SRC) -o $@
$(BUILD)/test_native_backend: tests/test_native_backend.cpp tests/test.h src/b3host/native_backend.cpp include/b3host/native_backend.h tools/native/crash_report.h $(HOST_SRC) $(wildcard include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3host/native_backend.cpp $(HOST_SRC) -o $@
# mod kit 2a: the hook table and the mod arena (src/native/hook_table.h), config/mod_hooks.txt's route prologue
$(BUILD)/test_hook_table: tests/test_hook_table.cpp tests/test.h tools/native/online_message.h tools/native/speed_units.h src/native/hook_table.cpp src/native/hook_table.h src/native/native_table.h src/b3host/native_backend.cpp $(HOST_SRC) $(wildcard include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -Isrc/native $< src/native/hook_table.cpp src/b3host/native_backend.cpp $(HOST_SRC) -o $@
# sdk2b (the code-mod SDK, docs/modding/DESIGN.md §2): the ABI's layout, the loader's refusals, replace / replace
# conflicts, the API on a synthetic hook table, the settings' recording and replay -- with test mods built from
# tests/codemods/testmod.c and the SDK header alone (one per MODE)
TESTS += $(BUILD)/test_code_mods
TESTMOD_MODES := good:1 conflict:2 failmain:3 badaddr:4 late:5 newer:6 nobuilt:7 platform:8 memory:9 badrow:10 files:11
TESTMODS := $(foreach m,$(TESTMOD_MODES),$(BUILD)/testmods/$(firstword $(subst :, ,$(m))).so)
$(BUILD)/testmods/%.so: tests/codemods/testmod.c sdk/include/b3mod_api.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -fPIC -shared -fvisibility=hidden -fexceptions -Isdk/include -DMODE=$(lastword $(subst :, ,$(filter $*:%,$(TESTMOD_MODES)))) $< -o $@
CODE_MODS_TEST_SRC := src/native/code_mods.cpp src/native/hook_table.cpp src/b3mod/codemod.cpp src/b3mod/mods.cpp src/b3host/native_backend.cpp $(HOST_SRC)
$(BUILD)/test_code_mods: tests/test_code_mods.cpp tests/test.h $(CODE_MODS_TEST_SRC) src/native/code_mods.h src/native/hook_table.h include/b3mod/codemod.h include/b3mod/mods.h sdk/include/b3mod_api.h $(wildcard include/b3host/*.h) $(TESTMODS)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -Isrc/native '-DB3_TESTMODS_DIR="$(abspath $(BUILD)/testmods)"' $< $(CODE_MODS_TEST_SRC) -ldl -o $@
# lua6 (script mods, docs/modding/DESIGN.md §10): the [script] manifest and hash, the sandbox, the instruction budget,
# the memory cap, the seeded random, the b3 API on a synthetic hook table. Lua 5.4 (src/third_party/lua-5.4.9) is one
# C++ unit, src/native/script_lua.cpp, compiled without warnings (the vendored source is left as it is).
LUA_UNIT := src/native/script_lua.cpp
LUA_SRC_DEP := $(wildcard src/third_party/lua-5.4.9/src/*.c src/third_party/lua-5.4.9/src/*.h)
$(BUILD)/obj_lua/script_lua.o: $(LUA_UNIT) $(LUA_SRC_DEP)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -w -c $< -o $@
TESTS += $(BUILD)/test_script_mods
SCRIPT_MODS_TEST_SRC := src/native/script_mods.cpp $(CODE_MODS_TEST_SRC)
$(BUILD)/test_script_mods: tests/test_script_mods.cpp tests/test.h $(SCRIPT_MODS_TEST_SRC) src/native/script_mods.h src/native/code_mods.h src/native/hook_table.h include/b3mod/codemod.h include/b3mod/mods.h sdk/include/b3mod_api.h $(wildcard include/b3host/*.h) $(BUILD)/obj_lua/script_lua.o
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -Isrc/native '-DB3_SCRIPTMODS_DIR="$(abspath $(BUILD)/scriptmods)"' $< $(SCRIPT_MODS_TEST_SRC) $(BUILD)/obj_lua/script_lua.o -ldl -o $@
$(BUILD)/test_ipu_model: tests/test_ipu_model.cpp tests/test.h src/native/ipu_model.h src/native/ipu_vlc.h src/native/hw_model.h src/native/hw_model_dev.h src/native/mpeg_dsp.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
$(BUILD)/test_native_platform: tests/test_native_platform.cpp tests/test.h tests/r5900_mini.h src/native/platform.h src/native/platform.cpp src/native/platform_crt.cpp src/native/platform_printf.cpp src/native/platform_iop.cpp src/native/platform_kernel.cpp src/native/platform_time.cpp src/native/platform_malloc.cpp src/native/platform_hw.cpp src/native/platform_sif.cpp src/native/platform_dbc.cpp src/native/platform_charge.cpp src/b3asset/discfs.cpp include/b3asset/discfs.h $(CHD_OBJ) src/native/platform_threads.cpp src/native/sampler.cpp src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp src/native/platform_vu0.cpp src/native/vu0_unit.cpp src/b3asset/vu1.cpp src/b3host/native_backend.cpp $(HOST_SRC) $(wildcard include/b3host/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) $(VU_DEFS) -DB3_PS2_FLOAT=1 -DB3_VU0_INTERP=1 $< src/native/platform.cpp src/native/platform_crt.cpp src/native/platform_printf.cpp src/native/platform_iop.cpp src/native/platform_kernel.cpp src/native/platform_time.cpp src/native/platform_malloc.cpp src/native/platform_hw.cpp src/native/platform_sif.cpp src/native/platform_dbc.cpp src/native/platform_charge.cpp src/b3asset/discfs.cpp $(CHD_OBJ) src/native/platform_threads.cpp src/native/sampler.cpp src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp src/native/platform_vu0.cpp src/native/vu0_unit.cpp src/b3asset/vu1.cpp src/b3host/native_backend.cpp $(HOST_SRC) -pthread -o $@
# The native memory card (boot2, src/native/save_card.h); reads the kit's 100% profile ($B3_SAVES_ROOT: make test passes
# SAVES; its checks of that profile are SKIPPED without it)
TESTS += $(BUILD)/test_save_card
$(BUILD)/test_save_card: tests/test_save_card.cpp tests/test.h src/native/save_card.h src/native/save_formats.h src/native/save_store.h src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp -o $@
RWA_IOP_SRC := src/audio/rwa_iop.cpp src/audio/spu2_stub_mixer.cpp src/audio/spu2_mixer_impl.cpp
RWA_IOP_DEP := $(RWA_IOP_SRC) src/native/rwa_iop.h src/audio/spu2_mixer.h src/audio/spu2_stub_mixer.h src/audio/spu2_mixer_impl.h src/audio/spu2_tables.inc
$(BUILD)/test_rwa_iop: tests/test_rwa_iop.cpp tests/test.h $(RWA_IOP_DEP)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(RWA_IOP_SRC) -o $@
# The SPU2-equivalent mixer (src/audio/spu2_mixer_impl.cpp): ADPCM, Gaussian, ADSR, loops, reverb, IRQ, MEMIN.
$(BUILD)/test_spu2_mixer: tests/test_spu2_mixer.cpp tests/test.h $(RWA_IOP_DEP)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(RWA_IOP_SRC) -o $@
B3ROUTE_SRC := src/audio/b3route.cpp
$(BUILD)/test_b3route: tests/test_b3route.cpp tests/test.h $(RWA_IOP_DEP) $(B3ROUTE_SRC) src/audio/b3route.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(RWA_IOP_SRC) $(B3ROUTE_SRC) -o $@
$(BUILD)/test_rwa_iop_b0: tests/test_rwa_iop_b0.cpp $(RWA_IOP_DEP)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(RWA_IOP_SRC) -o $@
#   make test_rwa_iop_b0 [B0_ARGS='--no-gap --no-spurious-0d [--real-mixer]'] (B3_SIF_LOG / B3_GAME_DIR override the defaults)
test_rwa_iop_b0: $(BUILD)/test_rwa_iop_b0
	$(BUILD)/test_rwa_iop_b0 $(B0_ARGS)
.PHONY: test_rwa_iop_b0
# The b3asm model (b3host/eeasm.h) and the translator's output for hand-written templates (tools/decomp2host/eeasm.py).
$(BUILD)/eeasm_cases.h: tests/eeasm_cases.txt tools/decomp2host/eeasm.py
	@mkdir -p $(BUILD)
	python3 -I tools/decomp2host/eeasm.py --emit-cases $< $@
$(BUILD)/test_eeasm_%: tests/test_eeasm.cpp tests/test.h tests/r5900_mini.h $(BUILD)/eeasm_cases.h $(wildcard include/b3host/*.h) $(LIBM_STAMP)
	$(CXX) $(CPPFLAGS) -I$(BUILD) $(REQFLAGS) $(CXXFLAGS) $(LIBM_DEFS) -DB3_PS2_FLOAT=$(if $(filter ps2,$*),1,0) $< -o $@

# Native renderer foundation (docs/native-renderer.md §10): b3asset decoders (portable C++, no SSE flags so it builds on
# Apple Silicon too), their tests, the offline dump tool, and b3gfx + b3view on SDL3 (`make render`, not part of `all`).
# The game install is read for the optional real-data tests and never written; outputs stay out of git.
# -ffp-contract=off as REQFLAGS (the same results with and without FMA: x86-64's baseline has none, so this changes
# nothing there; arm64 / an -mfma build no longer contracts a*b+c in the renderer either)
RENDER_FLAGS := -std=c++20 -Wall -Wextra -Werror=return-type -ffp-contract=off
B3ASSET_SRC := $(wildcard src/b3asset/*.cpp)
B3ASSET_OBJ := $(B3ASSET_SRC:%.cpp=$(BUILD)/render/%.o)
# nofallback1: the shipped programs (libb3render.so, b3launcher, b3modtool; tools/render/build_win.sh likewise) without
# the VU interpreter core (src/b3asset/vu1.cpp) and the particle layer that uses it (particleshade.cpp): nothing in them calls it. It is the reference that the renderer's own
# versions of the VU1 programs (b3asset/*shade.h) are checked against (b3vucheck*, vif1_gs_check, test_b3asset, ...)
# and the VU0 interpreter of a VU0_INTERP=1 dev build
B3ASSET_RT_SRC := $(filter-out src/b3asset/vu1.cpp src/b3asset/particleshade.cpp,$(B3ASSET_SRC))
B3ASSET_RT_OBJ := $(B3ASSET_RT_SRC:%.cpp=$(BUILD)/render/%.o)
# chd1: CHD disc images (include/b3asset/discfs.h): libchdr and its decompressors (src/native/third_party/libchdr) as
# one C unit, position-independent (also in libb3render.so), its symbols hidden outside the program or library that
# links it; in every program and test that links discfs
CHD_SRC := src/b3asset/chd_libchdr.c
CHD_OBJ := $(BUILD)/render/src/b3asset/chd_libchdr.o
$(CHD_OBJ): $(CHD_SRC) $(shell find src/native/third_party/libchdr -name '*.[ch]') src/native/third_party/dr_flac.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -fPIC -fvisibility=hidden -w -c $< -o $@
B3ASSET_OBJ += $(CHD_OBJ)
B3ASSET_RT_OBJ += $(CHD_OBJ)
GAME ?= $(CURDIR)/data/game
# the 100 % save card the save tests read (optional: they skip without it)
SAVES ?=
GAME_DEFS := $(if $(wildcard $(GAME)/DATA/GLOBAL.TXD),'-DB3_GAME_DIR="$(GAME)"')
# The game's VU1 microprogram images (decomp snapshot, read only) for the VU1 reference tests.
# The snapshot may lack them; the images are static extracts, so the live decomp's copy is read as a fallback.
VU_DIR ?= $(if $(wildcard $(DECOMP)/assembly/assets/vu),$(DECOMP)/assembly/assets/vu,$(DECOMP_LIVE)/assembly/assets/vu)
VU_DEFS := $(if $(wildcard $(VU_DIR)/game_0046F0D0.textbin.bin),'-DB3_VU_DIR="$(VU_DIR)"')
# mac5: the binaries that compile VU_DEFS in depend on this stamp, rewritten only when the definition changes -- after a
# new decomp snapshot (DECOMP) test_b3asset kept reading the old snapshot's VU images, and once that snapshot was
# deleted it failed 412 checks
VU_STAMP := $(BUILD)/vu_defs.stamp
VU_STAMP_NOW := $(if $(wildcard $(VU_STAMP)),$(file <$(VU_STAMP)))
ifneq ($(if $(wildcard $(VU_STAMP)),x$(strip $(VU_STAMP_NOW))),x$(strip $(VU_DEFS)))   # public1: also when missing
$(shell mkdir -p $(BUILD))
$(file >$(VU_STAMP),$(VU_DEFS))
endif
$(BUILD)/render/%.o: %.cpp $(wildcard include/b3asset/*.h include/b3gfx/*.h src/b3view/*.h src/b3gfx/shaders_gen.h)
	@mkdir -p $(dir $@)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $(RENDER_CPPFLAGS) -c $< -o $@
# betabytes: betaimport.cpp reads trafficseries' table (a traffic vehicle's donor)
$(BUILD)/render/src/b3asset/betaimport.o: tools/native/traffic_series.h
TESTS += $(BUILD)/test_b3asset $(BUILD)/test_hdpack $(BUILD)/test_discfs
# iso1: the disc's files from a folder or a disc image (include/b3asset/discfs.h): synthetic ISO 9660 images under build/
# (the real image against the extracted folder: B3_TEST_ISO=<.iso> B3_TEST_ISO_DIR=<folder> build/test_discfs)
$(BUILD)/test_discfs: tests/test_discfs.cpp tests/test.h tests/iso_writer.h tests/chd_writer.h include/b3asset/discfs.h $(BUILD)/render/src/b3asset/discfs.o $(CHD_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(BUILD)/render/src/b3asset/discfs.o $(CHD_OBJ) -pthread -lz -o $@
$(BUILD)/test_hdpack: tests/test_hdpack.cpp tests/test.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
# xboximport (1.3.0): Import Xbox textures (b3asset/xdvdfs.h, xboxtex.h, xboximport.h): synthetic XDVDFS images, Xbox
# textures, the detail transfer, the car paint mapping, the DDS writer and the import end to end (no disc data)
TESTS += $(BUILD)/test_xboximport
$(BUILD)/test_xboximport: tests/test_xboximport.cpp tests/test.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -pthread -lz -o $@
# carpaint (docs/remaster-features.md §15): the paint / livery map of a car atlas, synthetic palettes
TESTS += $(BUILD)/test_carpaint
$(BUILD)/test_carpaint: tests/test_carpaint.cpp tests/test.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/test_b3asset: tests/test_b3asset.cpp tests/test.h $(B3ASSET_OBJ) $(VU_STAMP)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $(GAME_DEFS) $(VU_DEFS) $< $(B3ASSET_OBJ) -o $@
# HUD / 2D reader (hud1): synthetic Gt2d event streams -> triangles and GS state
TESTS += $(BUILD)/test_hud
$(BUILD)/test_hud: tests/test_hud.cpp tests/test.h src/b3replay/hud.cpp src/b3replay/hud.h src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/globe.cpp src/b3replay/globe.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -o $@
# cam1: capture decoding rules on synthetic guest images (the view's own race car hidden while IsPlayerVisible is false)
TESTS += $(BUILD)/test_capture
$(BUILD)/test_capture: tests/test_capture.cpp tests/test.h src/b3replay/hud.cpp src/b3replay/hud.h src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/globe.cpp src/b3replay/globe.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -o $@
# font1: the live feed's lap / late-join rules keep every HUD texture (race2 through a real b3live ring)
TESTS += $(BUILD)/test_livelap
$(BUILD)/test_livelap: tests/test_livelap.cpp tests/test.h include/b3live/ring.h src/b3replay/hud.cpp src/b3replay/hud.h src/b3replay/capture.cpp src/b3replay/capture.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/hud.cpp src/b3replay/capture.cpp $(B3ASSET_OBJ) -lz -o $@
# mac1: keyboard -> DS2 report for the native game (src/native/input_keys.*; no SDL in the core)
TESTS += $(BUILD)/test_input_keys
$(BUILD)/test_input_keys: tests/test_input_keys.cpp tests/test.h src/native/input_keys.h src/native/input_keys.cpp src/native/input_report.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/native/input_keys.cpp src/native/input_report.cpp -o $@
# play1: the SDL3 event adapter on synthetic events (SDL's headers only: no SDL library is linked)
TESTS += $(BUILD)/test_input_keys_sdl
$(BUILD)/test_input_keys_sdl: tests/test_input_keys_sdl.cpp tests/test.h src/native/input_keys.h src/native/input_keys_sdl.h src/native/input_bindings.h src/native/pad_gyro.h src/native/input_keys.cpp src/native/input_report.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(SDL3_CFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/native/input_keys.cpp src/native/input_report.cpp -o $@
# remap1: control remapping (src/native/input_bindings.*): the defaults byte-identical, rebinding, conflicts, controls.ini
TESTS += $(BUILD)/test_input_bindings
$(BUILD)/test_input_bindings: tests/test_input_bindings.cpp tests/test.h src/native/input_bindings.h src/native/pad_gyro.h src/native/input_bindings.cpp src/native/input_keys.h src/native/input_keys.cpp src/native/input_report.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/native/input_bindings.cpp src/native/input_keys.cpp src/native/input_report.cpp -o $@
# pad1: gyro steering (src/native/pad_gyro.h, apply_gyro) and DualSense adaptive triggers (pad_triggers.h) on synthetic
# SDL sensor events (SDL's headers only: no SDL library is linked)
TESTS += $(BUILD)/test_pad_motion
$(BUILD)/test_pad_motion: tests/test_pad_motion.cpp tests/test.h src/native/pad_gyro.h src/native/pad_triggers.h src/native/input_keys_sdl.h src/native/input_bindings.h src/native/input_bindings.cpp src/native/input_keys.h src/native/input_keys.cpp src/native/input_report.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(SDL3_CFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/native/input_bindings.cpp src/native/input_keys.cpp src/native/input_report.cpp -o $@
# rumble1: the pads' actuator requests (src/native/pad_actuator.h) and the pad recording's rumble switches
TESTS += $(BUILD)/test_pad_actuator
$(BUILD)/test_pad_actuator: tests/test_pad_actuator.cpp tests/test.h src/native/pad_actuator.h tools/native/pad_record.h include/b3mod/codemod.h src/b3mod/codemod.cpp src/b3mod/mods.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3mod/codemod.cpp src/b3mod/mods.cpp -o $@
# settings1 (docs/pc-settings.md): the PC settings model, ini, HSET record and the page's menu model
TESTS += $(BUILD)/test_settings
$(BUILD)/test_settings: tests/test_settings.cpp tests/test.h include/b3settings/settings.h include/b3settings/page.h include/b3settings/i18n.h include/b3settings/safemode.h src/b3settings/settings.cpp src/b3settings/page.cpp src/b3settings/menu.cpp src/b3settings/safemode.cpp src/native/input_bindings.h src/native/input_bindings.cpp src/native/input_keys.h src/native/input_keys.cpp src/native/input_report.cpp
	$(CXX) -Iinclude -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/b3settings/settings.cpp src/b3settings/page.cpp src/b3settings/menu.cpp src/b3settings/safemode.cpp src/native/input_bindings.cpp src/native/input_keys.cpp src/native/input_report.cpp -o $@
# effects2 (src/b3replay/sparks.h): the particle effects' levels on hand-made captures (sparks, popcorn)
TESTS += $(BUILD)/test_effect_levels
$(BUILD)/test_effect_levels: tests/test_effect_levels.cpp tests/test.h src/b3replay/sparks.cpp src/b3replay/sparks.h src/b3replay/capture.h $(B3ASSET_OBJ) $(VU_STAMP)
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/b3gfx $(RENDER_FLAGS) $(CXXFLAGS) $(VU_DEFS) $< src/b3replay/sparks.cpp $(B3ASSET_OBJ) -o $@
# Device::resource_epoch: the 2D panels make their buffers and textures again after a track load (notice, skip hint, speedrun timer,
# car look panel, photo plates, mod overlay)
TESTS += $(BUILD)/test_panel_epoch
$(BUILD)/test_panel_epoch: tests/test_panel_epoch.cpp tests/test.h $(PHOTO_SRC) src/b3replay/skiphint.cpp src/b3replay/speedrunpanel.cpp src/b3replay/carlookpanel.cpp src/b3replay/carlook.cpp src/b3replay/modoverlay.cpp src/b3replay/nativepad.cpp src/b3replay/replaycams.cpp src/b3replay/finishcam.cpp $(B3ASSET_OBJ) src/b3replay/menumouse.cpp include/b3gfx/b3gfx.h
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(SDL3_CFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(PHOTO_SRC) src/b3replay/skiphint.cpp src/b3replay/speedrunpanel.cpp src/b3replay/carlookpanel.cpp src/b3replay/carlook.cpp src/b3replay/modoverlay.cpp src/b3replay/nativepad.cpp src/b3replay/replaycams.cpp src/b3replay/finishcam.cpp $(B3ASSET_OBJ) src/b3replay/menumouse.cpp $(SDL3_LIBS) -lz -pthread -o $@
# reflect2: the enhanced car look's live values (docs/car-look.md)
TESTS += $(BUILD)/test_carlook
$(BUILD)/test_carlook: tests/test_carlook.cpp tests/test.h src/b3replay/carlook.h src/b3replay/carlook.cpp include/b3gfx/b3gfx.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/carlook.cpp -o $@
# accessibility (remaster-features; docs/pc-settings.md): HUD COLOUR FILTER's daltonize matrices, SCREEN FLASHES' cap
TESTS += $(BUILD)/test_a11y
$(BUILD)/test_a11y: tests/test_a11y.cpp tests/test.h src/b3replay/a11y.h src/b3replay/a11y.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/a11y.cpp -o $@
# mac5: the video skip hint (remaster-features §2): the skip's condition, the hint's fade in game time, its picture
TESTS += $(BUILD)/test_skip_hint
PROMPTS_SRC := src/b3replay/prompts.cpp src/b3replay/glyphdraw.cpp src/native/input_keys.cpp src/native/input_report.cpp
PROMPTS_DEP := $(PROMPTS_SRC) src/b3replay/prompts.h src/b3replay/glyphdraw.h src/b3replay/hud.h src/native/input_keys.h
$(BUILD)/test_skip_hint: tests/test_skip_hint.cpp tests/test.h tools/native/movie_skip.h src/b3replay/skiphint.h src/b3replay/skiphint.cpp $(PROMPTS_DEP)
	$(CXX) -Iinclude -Itools/native -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/skiphint.cpp $(PROMPTS_SRC) -o $@
# mac5: the button prompts for the device in use (remaster-features §3): labels, key caps, the device switching, the
# glyph atlas and the substitution of the game's button draws (src/b3replay/prompts.h)
TESTS += $(BUILD)/test_prompts
$(BUILD)/test_prompts: tests/test_prompts.cpp tests/test.h $(PROMPTS_DEP)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< $(PROMPTS_SRC) -o $@
# photo mode (remaster-features §5): the free camera, its controls, entering / leaving, the pad withheld (photomode.h)
TESTS += $(BUILD)/test_photomode
PHOTO_SRC := src/b3replay/photomode.cpp src/b3replay/garagecam.cpp src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/photohud.cpp src/b3replay/notice.cpp src/b3replay/a11y.cpp $(PROMPTS_SRC) src/native/input_bindings.cpp
$(BUILD)/test_photomode: tests/test_photomode.cpp tests/test.h $(PHOTO_SRC) src/b3replay/photomode.h src/b3replay/photohud.h src/b3replay/prompts.h src/b3replay/notice.h src/b3replay/glyphdraw.h src/b3replay/capture.h
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(SDL3_CFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(PHOTO_SRC) -o $@
# garagecam1: the car pages' view round the car (src/b3replay/garagecam.h): hold / resume, clamps, the orbit, stamped input
TESTS += $(BUILD)/test_garagecam
$(BUILD)/test_garagecam: tests/test_garagecam.cpp tests/test.h src/b3replay/garagecam.cpp src/b3replay/garagecam.h src/b3replay/capture.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/garagecam.cpp -pthread -o $@
# orbitcam1: the race chase camera's look round the car (src/b3replay/orbitcam.h): the swing, the return, the turn, stamped input
TESTS += $(BUILD)/test_orbitcam
$(BUILD)/test_orbitcam: tests/test_orbitcam.cpp tests/test.h src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/orbitcam.h src/b3replay/garagecam.cpp src/b3replay/garagecam.h src/b3replay/capture.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/garagecam.cpp -pthread -o $@
# weather: the falling rain (src/b3replay/rain.h): deterministic, world-anchored drops, roofs, streaks at speed
TESTS += $(BUILD)/test_rain
$(BUILD)/test_rain: tests/test_rain.cpp tests/test.h src/b3replay/rain.h src/b3replay/coronas.h src/b3replay/capture.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -pthread -o $@
# weather: the rain's sound (src/b3replay/rain_audio.h): off untouched, fades, tunnels, speed, saturation
TESTS += $(BUILD)/test_rain_audio
$(BUILD)/test_rain_audio: tests/test_rain_audio.cpp tests/test.h src/b3replay/rain_audio.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -pthread -o $@
# finishcam1: the set pieces after the finish (src/b3replay/finishcam.h): VDB.XML's offsets, the shot timeline, the camera
TESTS += $(BUILD)/test_finishcam
# replaycams: the replay viewer's cameras (their maths)
TESTS += $(BUILD)/test_replaycams
$(BUILD)/test_replaycams: tests/test_replaycams.cpp tests/test.h src/b3replay/replaycams.cpp src/b3replay/replaycams.h src/b3replay/finishcam.cpp src/b3replay/finishcam.h src/b3replay/trackrays.cpp src/b3replay/trackrays.h src/b3replay/capture.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/replaycams.cpp src/b3replay/finishcam.cpp src/b3replay/trackrays.cpp $(B3ASSET_OBJ) -lz -pthread -o $@
$(BUILD)/test_finishcam: tests/test_finishcam.cpp tests/test.h src/b3replay/finishcam.cpp src/b3replay/finishcam.h src/b3replay/capture.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/finishcam.cpp $(B3ASSET_OBJ) -lz -pthread -o $@
# photohud1: photo mode's UI rendered over a picture into a PNG, offscreen (tools/native/photohud_preview.cpp); not a test
$(BUILD)/photohud_preview: tools/native/photohud_preview.cpp $(PHOTO_SRC) src/b3replay/photohud.h src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(RENDER_FLAGS) $(SDL3_CFLAGS) $(CXXFLAGS) $< $(PHOTO_SRC) src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -o $@
# fpsoverlay2: the FPS counter's numbers, graph scale, view cycle, layout at ten window shapes, notice / photo plate clearance
TESTS += $(BUILD)/test_fpspanel
$(BUILD)/test_fpspanel: tests/test_fpspanel.cpp tests/test.h src/b3replay/fpspanel.cpp src/b3replay/fpspanel.h $(PHOTO_SRC) src/b3replay/photohud.h src/b3replay/notice.h
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(SDL3_CFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/fpspanel.cpp $(PHOTO_SRC) -o $@
# fpsoverlay2: the FPS counter's three views rendered over a picture into a PNG, offscreen (tools/native/fpsoverlay_preview.cpp); not a test
$(BUILD)/fpsoverlay_preview: tools/native/fpsoverlay_preview.cpp src/b3replay/fpspanel.cpp src/b3replay/fpspanel.h $(PHOTO_SRC) src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(RENDER_FLAGS) $(SDL3_CFLAGS) $(CXXFLAGS) $< src/b3replay/fpspanel.cpp $(PHOTO_SRC) src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -o $@
# saveflow: the save notices' bubble rendered over a picture into PNGs, offscreen (tools/native/savebubble_preview.cpp); not a test
$(BUILD)/savebubble_preview: tools/native/savebubble_preview.cpp $(PHOTO_SRC) src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/native $(RENDER_FLAGS) $(SDL3_CFLAGS) $(CXXFLAGS) $< $(PHOTO_SRC) src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -o $@
# online-det: the pad withheld for the PC SETTINGS page on the VBlank grid, whatever the render thread's timing (page_hold.h)
TESTS += $(BUILD)/test_page_hold
$(BUILD)/test_page_hold: tests/test_page_hold.cpp tests/test.h tools/native/page_hold.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# photorow1: the pause menu's PHOTO MODE row -- the private row array and the index mapping (pause_rows.h)
TESTS += $(BUILD)/test_pause_rows
$(BUILD)/test_pause_rows: tests/test_pause_rows.cpp tests/test.h tools/native/pause_rows.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# dll1114: the renderer load's message per Windows error and its diagnosis' folder test (render_load.h)
TESTS += $(BUILD)/test_render_load
$(BUILD)/test_render_load: tests/test_render_load.cpp tests/test.h tools/native/render_load.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# wreckguard: the 64 crashed-traffic slots bounded -- which wreck is released (wreck_slots.h)
TESTS += $(BUILD)/test_wreck_slots
$(BUILD)/test_wreck_slots: tests/test_wreck_slots.cpp tests/test.h tools/native/wreck_slots.h
	$(CXX) -Itools/native -Iinclude -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# fastload1: FAST LOADING -- the loading detector, the pacer with a fake clock, the muted sound (fast_load.h)
TESTS += $(BUILD)/test_fast_load
$(BUILD)/test_fast_load: tests/test_fast_load.cpp tests/test.h tools/native/fast_load.h tools/native/save_screens.h src/b3replay/game_behind.h src/b3replay/fastforward.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# todrace: DAY / NIGHT per race on SINGLE EVENT's track page -- the choice's lifetime rules (race_time.h)
TESTS += $(BUILD)/test_race_time
$(BUILD)/test_race_time: tests/test_race_time.cpp tests/test.h tools/native/race_time.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# elim1: ELIMINATOR in SINGLE EVENT's SELECT MODE -- the rows and the modes they stand for (eliminator_rows.h)
TESTS += $(BUILD)/test_eliminator_rows
$(BUILD)/test_eliminator_rows: tests/test_eliminator_rows.cpp tests/test.h tools/native/eliminator_rows.h src/b3replay/records.h tools/native/race_choice.h
	$(CXX) -Itools/native -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# raceopts: SELECT OPTIONS' TRAFFIC / LAPS / RIVALS rows -- values, limits, lifetime, records kept apart (race_choice.h)
TESTS += $(BUILD)/test_stats_signals
$(BUILD)/test_stats_signals: tests/test_stats_signals.cpp tests/test.h tools/native/stats_signals_logic.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
TESTS += $(BUILD)/test_race_options
$(BUILD)/test_race_options: tests/test_race_options.cpp tests/test.h tools/native/race_choice.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# MORE RIVALS: the start spots for cars 7 and 8, the record word (more_rivals_grid.h)
TESTS += $(BUILD)/test_more_rivals
$(BUILD)/test_more_rivals: tests/test_more_rivals.cpp tests/test.h tools/native/more_rivals_grid.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# trafficcheck: GAMEPLAY's TRAFFIC CHECKING -- stages, latch, the check's rules, the push, the chain, records apart
TESTS += $(BUILD)/test_traffic_check
$(BUILD)/test_traffic_check: tests/test_traffic_check.cpp tests/test.h tools/native/traffic_check_rules.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# options3: every owner of the shared fixed spaces (arena regions, records word bits, HTD1 fields): a merge that takes the
# same bits or bytes twice fails here
# options3: DRIVER LABELS' layout (src/b3replay/driverlabels.h), CRASH CAMERA Off's held view (crashhold.h)
TESTS += $(BUILD)/test_driver_labels
$(BUILD)/test_driver_labels: tests/test_driver_labels.cpp tests/test.h src/b3replay/driverlabels.h src/b3replay/crashhold.h src/b3replay/capture.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# booststart (tools/native/boost_start.h): the game's BOOST START on a standing start
TESTS += $(BUILD)/test_boost_start
$(BUILD)/test_boost_start: tests/test_boost_start.cpp tests/test.h tools/native/boost_start.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/native -Igen/native/table -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# padskip (tools/native/pad_context.h, issue #14): the pad's context, not driving in the game's replays and films
TESTS += $(BUILD)/test_pad_context
$(BUILD)/test_pad_context: tests/test_pad_context.cpp tests/test.h tools/native/pad_context.h tools/native/movie_skip.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# taillights (src/b3replay/taillights.h, b3asset carshade): night / rain tail lights and the brake light's rise
TESTS += $(BUILD)/test_taillights
$(BUILD)/test_taillights: tests/test_taillights.cpp tests/test.h src/b3replay/taillights.h include/b3asset/carshade.h src/b3asset/carshade.cpp src/b3asset/vif.cpp
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3asset/carshade.cpp src/b3asset/vif.cpp -o $@
# settingsback (tools/native/settings_back.h): Back on the game's SETTINGS pages keeps the changes
TESTS += $(BUILD)/test_settings_back
$(BUILD)/test_settings_back: tests/test_settings_back.cpp tests/test.h tools/native/settings_back.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/native -Igen/native/table -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
TESTS += $(BUILD)/test_bit_owners
$(BUILD)/test_bit_owners: tests/test_bit_owners.cpp tests/test.h src/native/hook_table.h tools/native/race_choice.h tools/native/eliminator_rows.h tools/native/heavy_series.h tools/native/game_option_rules.h tools/native/game_mods.h src/b3replay/records.h tools/native/road_rage_parts.h tools/native/traffic_check_rules.h tools/native/traffic_series.h tools/native/extra_series.h
	$(CXX) -Iinclude -Isrc/native -Igen/native/table -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# nextstart: the game started again after APPLY AND RESTART (relaunch.h): the arguments, START AT, the Windows quoting
TESTS += $(BUILD)/test_relaunch
$(BUILD)/test_relaunch: tests/test_relaunch.cpp tests/test.h tools/native/relaunch.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# paintsel: the car paint panel on SELECT CAR -- the page's player, the colour-row rule, the prompt's fade (paint_select.h)
TESTS += $(BUILD)/test_paint_select
$(BUILD)/test_paint_select: tests/test_paint_select.cpp tests/test.h tools/native/paint_select.h tools/native/car_select.h tools/native/race_time.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# xboxglow: TUNNEL LIGHTS' Xbox looks -- the setting's value, the lamps, the glow masks (src/b3replay/xboxglow.h)
TESTS += $(BUILD)/test_xboxglow
$(BUILD)/test_xboxglow: tests/test_xboxglow.cpp tests/test.h src/b3replay/xboxglow.h include/b3asset/model.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# paint3: the carousel's cars whose picture is not their own -- the slots read, the resolver's, the host record (carousel_models.h)
TESTS += $(BUILD)/test_carousel_models
$(BUILD)/test_carousel_models: tests/test_carousel_models.cpp tests/test.h tools/native/carousel_models.h tools/native/car_select.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# paint3: the GPU ids a thumbnail-only car model gives back, the picture jobs' tick limit (src/b3replay/thumb_release.h)
TESTS += $(BUILD)/test_thumb_release
$(BUILD)/test_thumb_release: tests/test_thumb_release.cpp tests/test.h src/b3replay/thumb_release.h include/b3gfx/b3gfx.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# taxihead: a vehicle's car file by its header's relocated words (src/b3replay/carmatch.h: the taxis share 0x00..0x4C)
TESTS += $(BUILD)/test_carmatch
$(BUILD)/test_carmatch: tests/test_carmatch.cpp tests/test.h src/b3replay/carmatch.h
	@mkdir -p $(BUILD)
	$(CXX) -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# hudmods: RPM METER / BOOST BAR SHAKE on synthetic HUD frames (src/b3replay/hudmods.h)
TESTS += $(BUILD)/test_hudmods
$(BUILD)/test_hudmods: tests/test_hudmods.cpp tests/test.h src/b3replay/hudmods.cpp src/b3replay/hudmods.h src/b3replay/hud.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/hudmods.cpp -o $@
# hudmods: TAKEDOWN STREAK PITCH's rule (tools/native/takedown_pitch.h)
TESTS += $(BUILD)/test_takedown_pitch
$(BUILD)/test_takedown_pitch: tests/test_takedown_pitch.cpp tests/test.h tools/native/takedown_pitch.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# heavyseries: the HEAVYWEIGHTS series on SELECT CAR -- which events, which race, the replay's entry, records kept apart
# titlefast: the title's prompt and Start without the game's two-second wait (title_fast.h)
TESTS += $(BUILD)/test_title_fast
$(BUILD)/test_title_fast: tests/test_title_fast.cpp tests/test.h tools/native/title_fast.h $(wildcard include/b3host/*.h) src/native/hook_table.h
	$(CXX) -Iinclude -Itools/native -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# trafficseries: traffic vehicles as race cars, the TRAFFIC series (traffic_series.h)
TESTS += $(BUILD)/test_traffic_series
$(BUILD)/test_traffic_series: tests/test_traffic_series.cpp tests/test.h tools/native/traffic_series.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# customseries: players' car mods as extra cars, the CUSTOM series (extra_series.h)
TESTS += $(BUILD)/test_extra_series
$(BUILD)/test_extra_series: tests/test_extra_series.cpp tests/test.h tools/native/extra_series.h tools/native/traffic_series.h src/b3replay/records.h include/b3port/fileio.h src/native/disc_alias.h include/b3asset/car_layout.h
	$(CXX) -Iinclude -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
TESTS += $(BUILD)/test_heavy_series
$(BUILD)/test_heavy_series: tests/test_heavy_series.cpp tests/test.h tools/native/heavy_series.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# gamemods1: START AT's rules (boot_boost.h) and END VERY LONG EVENTS' checks (game_limits.h)
TESTS += $(BUILD)/test_boot_boost
$(BUILD)/test_boot_boost: tests/test_boot_boost.cpp tests/test.h tools/native/boot_boost.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
TESTS += $(BUILD)/test_game_limits
$(BUILD)/test_game_limits: tests/test_game_limits.cpp tests/test.h tools/native/game_limits.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# gameopts: GAMEPLAY's MIRROR MODE / CRASHBREAKER IN EVERY MODE -- stages, latch, record word, replay entry, folders
TESTS += $(BUILD)/test_game_options
# integfix: the records call without kinds (tools/native/records_guard.h): the game's own kinds-0 call untouched
TESTS += $(BUILD)/test_records_guard
$(BUILD)/test_records_guard: tests/test_records_guard.cpp tests/test.h tools/native/records_guard.h $(HOST_OBJ)
	$(CXX) $(CPPFLAGS) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< $(HOST_OBJ) -o $@
$(BUILD)/test_game_options: tests/test_game_options.cpp tests/test.h tools/native/game_option_rules.h src/b3replay/records.h
	$(CXX) -Itools/native $(REQFLAGS) $(CXXFLAGS) $< -o $@
# ghost1 (Time Attack ghosts, remaster-features §5): the .b3ghost format, the time sync, the recorder (src/b3replay/ghost.h)
TESTS += $(BUILD)/test_ghost
$(BUILD)/test_ghost: tests/test_ghost.cpp tests/test.h src/b3replay/ghost.cpp src/b3replay/ghost.h src/b3replay/capture.h src/b3replay/records.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/ghost.cpp -o $@
# speedrun1 (remaster-features §5 "Speedrun support"): the time text, the .b3split file, the tracker, the panel's rows
TESTS += $(BUILD)/test_speedrun
$(BUILD)/test_speedrun: tests/test_speedrun.cpp tests/test.h src/b3replay/speedrun.cpp src/b3replay/speedrun.h src/b3replay/capture.h include/b3host/ps2float.h include/b3host/ps2float_ref.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/speedrun.cpp -o $@
# traffic-popin (view.h): traffic fading in where it appears, out where it is removed and at the 275 m draw radius
TESTS += $(BUILD)/test_traffic_fade
$(BUILD)/test_traffic_fade: tests/test_traffic_fade.cpp tests/test.h src/b3replay/trafficfade.cpp src/b3replay/view.h src/b3replay/capture.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay -Isrc/b3gfx $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/trafficfade.cpp -o $@
# splitlayout1 (src/b3replay/splitlayout.h): the split screen's layouts -- views, projections, HUD placement, the setting
TESTS += $(BUILD)/test_split_layout
$(BUILD)/test_split_layout: tests/test_split_layout.cpp tests/test.h src/b3replay/splitlayout.cpp src/b3replay/splitlayout.h include/b3settings/settings.h include/b3settings/page.h src/b3settings/settings.cpp src/b3settings/page.cpp
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/splitlayout.cpp src/b3settings/settings.cpp src/b3settings/page.cpp -o $@
# custom soundtrack (remaster-features §6): tags, the folder, the decoders (src/native/third_party), the resampler, the
# player -- on synthetic bytes and a WAV tone made by the test
MUSIC_SRC := src/native/custom_music.cpp src/native/music_decoders.cpp
# nowplaying1 (remaster-features §6.2): another music app's song from the system's now-playing interface (Windows media
# sessions, Linux MPRIS with libdbus-1 loaded at run time; none on macOS), the poller and the popup's decisions; the
# test runs a fake MPRIS player on a private D-Bus when dbus-daemon is there
NOW_PLAYING_SRC := src/native/now_playing.cpp src/native/now_playing_mpris.cpp src/native/now_playing_win.cpp
TESTS += $(BUILD)/test_now_playing
$(BUILD)/test_now_playing: tests/test_now_playing.cpp tests/test.h src/native/now_playing.h src/native/dbus_dl.h src/native/custom_music.h $(NOW_PLAYING_SRC) $(MUSIC_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< $(NOW_PLAYING_SRC) $(MUSIC_SRC) -pthread -ldl -o $@
TESTS += $(BUILD)/test_custom_music
$(BUILD)/test_custom_music: tests/test_custom_music.cpp tests/test.h src/native/custom_music.h $(MUSIC_SRC) $(wildcard src/native/third_party/*.h src/native/third_party/*.c)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< $(MUSIC_SRC) -o $@
# save1 (remaster-features §8): the .sav, the PS2 / PCSX2 import formats, the save folder, the card's write-back
SAVE_SRC := src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp src/native/save_stats.cpp
TESTS += $(BUILD)/test_save_formats
$(BUILD)/test_save_formats: tests/test_save_formats.cpp tests/test.h src/native/pcsx2_cards.h src/native/save_card.h src/native/save_formats.h src/native/save_store.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< $(SAVE_SRC) -o $@
# The game's own stats from its save, and local achievements from them (remaster-features §5; src/native/save_stats.h)
TESTS += $(BUILD)/test_save_stats
$(BUILD)/test_save_stats: tests/test_save_stats.cpp tests/test.h src/native/save_card.h src/native/save_formats.h src/native/save_store.h src/native/save_stats.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< $(SAVE_SRC) -o $@
# replay1 (remaster-features §5 "Replay files"): the replay file's format, checksum, refusals, ops and folder
TESTS += $(BUILD)/test_replay_file
$(BUILD)/test_replay_file: tests/test_replay_file.cpp tests/test.h src/native/replay_file.h tools/native/replay_stream.h tools/native/replay_card.h src/native/save_card.h src/native/save_formats.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native -Itools/native $(REQFLAGS) $(CXXFLAGS) $< $(SAVE_SRC) -pthread -o $@
# save_tool: a save file's contents, import / export from the command line (tools/native/save_tool.cpp)
$(BUILD)/save_tool: tools/native/save_tool.cpp src/native/save_card.h src/native/save_formats.h src/native/save_store.h src/native/save_stats.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< $(SAVE_SRC) -o $@
save_tool: $(BUILD)/save_tool
.PHONY: save_tool
# mac5: the mouse and typed text in the game's menus (remaster-features §8; include/b3live/menu_input.h,
# src/b3replay/menumouse.h): records, actions, the presses' closed loop, the text mapping, hit bands, prompts, events
TESTS += $(BUILD)/test_menu_input
$(BUILD)/test_menu_input: tests/test_menu_input.cpp tests/test.h include/b3live/menu_input.h src/b3replay/menumouse.h src/b3replay/menumouse.cpp src/b3replay/garagecam.cpp src/b3replay/garagecam.h src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/orbitcam.h $(PROMPTS_DEP)
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< src/b3replay/menumouse.cpp src/b3replay/garagecam.cpp src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp $(PROMPTS_SRC) -pthread -o $@
# mac5 (remaster-features §8): the launcher / setup program beside the game (src/b3launcher; Windows:
# Burnout3Launcher.exe from tools/render/build_win.sh). `make launcher` (SDL3, not part of `make test`); its logic
# without SDL is test_launcher.
# photo2: the SHOWROOM page (src/b3launcher/showroom.h): its car list and turntable (core, test_showroom), its 3D view (b3gfx
# offscreen with the renderer's car model)
SHOWROOM_SRC := src/b3launcher/showroom.cpp
SHOWROOM_VIEW_SRC := src/b3launcher/showroom_view.cpp src/b3gfx/b3gfx_sdl.cpp src/b3view/scene.cpp src/b3view/carscene.cpp
LAUNCHER_CORE_SRC := $(SHOWROOM_SRC) src/b3launcher/core.cpp src/b3launcher/spawn.cpp src/b3launcher/gameart.cpp src/b3settings/settings.cpp src/b3settings/page.cpp src/b3settings/menu.cpp \
                     src/b3settings/safemode.cpp src/native/input_keys.cpp src/native/input_report.cpp src/native/input_bindings.cpp $(SAVE_SRC) $(MUSIC_SRC) $(NOW_PLAYING_SRC) src/b3mod/mods.cpp src/b3mod/codemod.cpp
LAUNCHER_SRC := $(LAUNCHER_CORE_SRC) src/b3mod/archive.cpp src/b3launcher/font.cpp src/b3launcher/draw.cpp src/b3launcher/main.cpp src/b3replay/prompts.cpp src/b3replay/glyphdraw.cpp \
                $(REPORT_SRC) src/b3report/sysinfo.cpp $(SHOWROOM_VIEW_SRC)
LAUNCHER_DEP := tools/native/extra_series.h tools/native/traffic_series.h $(wildcard include/b3port/app_icon*.h) include/b3port/file_url.h $(wildcard src/b3launcher/*.h src/b3launcher/third_party/*.h include/b3settings/*.h include/b3mod/*.h) src/native/input_keys.h src/native/input_bindings.h src/native/pad_gyro.h src/native/pad_triggers.h \
                src/native/input_keys_sdl.h src/native/save_card.h src/native/save_formats.h src/native/save_store.h src/native/save_stats.h src/native/pcsx2_cards.h \
                src/native/custom_music.h src/native/now_playing.h src/native/dbus_dl.h src/native/replay_file.h $(wildcard src/b3view/*.h include/b3gfx/*.h) src/b3gfx/shaders_gen.h
# update1: the update check (src/b3launcher/update.h: versions, the API's JSON, launcher.ini's update_* keys; the
# request itself, update_net.cpp, only in the launcher: WinHTTP / libcurl loaded at run time). test_launcher_update: canned answers, no network
LAUNCHER_CORE_SRC += src/b3launcher/update.cpp
LAUNCHER_SRC += src/b3launcher/update.cpp src/b3launcher/update_net.cpp src/b3launcher/sign.cpp src/b3launcher/oneclick.cpp
# 1.1 one-click updates: the signature check (sign.h); test_launcher_sign: ssh-keygen / Python-made vectors
TESTS += $(BUILD)/test_launcher_sign
$(BUILD)/test_launcher_sign: tests/test_launcher_sign.cpp tests/test.h src/b3launcher/sign.h src/b3launcher/sign.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3launcher/sign.cpp -o $@
TESTS += $(BUILD)/test_launcher_oneclick
$(BUILD)/test_launcher_oneclick: tests/test_launcher_oneclick.cpp tests/test.h src/b3launcher/oneclick.h src/b3launcher/oneclick.cpp src/b3launcher/sign.cpp src/b3launcher/update.cpp src/b3launcher/update_key.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3launcher/oneclick.cpp src/b3launcher/sign.cpp src/b3launcher/update.cpp -o $@
TESTS += $(BUILD)/test_launcher_update
$(BUILD)/test_launcher_update: tests/test_launcher_update.cpp tests/test.h src/b3launcher/update.h src/b3launcher/update.cpp include/b3port/file_url.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< src/b3launcher/update.cpp -o $@
# update2: the one-click update's install step (src/b3launcher/installer.h: a release .zip / .tar.gz unpacked, checked and
# swapped in with renames, previous-version/ and roll back). test_launcher_installer: packages written by the test
LAUNCHER_SRC += src/b3launcher/installer.cpp
# portable mode (1.3.0): the data folder's move between this computer's folder and <install>/appdata
# (src/b3launcher/portable.h; the path itself: include/b3port/user_dir.h). test_portable: scratch folders, no game data
LAUNCHER_SRC += src/b3launcher/portable.cpp
LAUNCHER_DEP += include/b3port/user_dir.h
PORTABLE_TEST_SRC := src/b3launcher/portable.cpp src/b3mod/archive.cpp src/b3mod/mods.cpp
TESTS += $(BUILD)/test_portable
$(BUILD)/test_portable: tests/test_portable.cpp tests/test.h src/b3launcher/portable.h include/b3port/user_dir.h include/b3port/fileio.h $(PORTABLE_TEST_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(PORTABLE_TEST_SRC) -lz -o $@
INSTALLER_TEST_SRC := src/b3launcher/installer.cpp src/b3mod/archive.cpp src/b3mod/mods.cpp
TESTS += $(BUILD)/test_launcher_installer
$(BUILD)/test_launcher_installer: tests/test_launcher_installer.cpp tests/test.h src/b3launcher/installer.h include/b3mod/archive.h include/b3mod/mods.h $(INSTALLER_TEST_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(INSTALLER_TEST_SRC) -lz -o $@
# issue #8: ADD TO STEAM (Settings > Launcher; src/b3launcher/steam.h: shortcuts.vdf, the grid's pictures, where Steam is;
# steam_art.h: the port's own pictures, the logo built in as include/b3port/steam_logo_data.h by
# tools/release/make_steam_logo.py). test_launcher_steam: made-up Steam folders in a temporary folder of its own
STEAM_SRC := src/b3launcher/steam.cpp src/b3launcher/steam_art.cpp
LAUNCHER_SRC += $(STEAM_SRC)
LAUNCHER_DEP += include/b3port/steam_logo_data.h
TESTS += $(BUILD)/test_launcher_steam
$(BUILD)/test_launcher_steam: tests/test_launcher_steam.cpp tests/test.h src/b3launcher/steam.h src/b3launcher/steam_art.h include/b3port/steam_logo_data.h $(STEAM_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(STEAM_SRC) -lz -o $@
TESTS += $(BUILD)/test_launcher
$(BUILD)/test_launcher: tests/test_launcher.cpp tests/test.h tests/iso_writer.h tests/chd_writer.h $(LAUNCHER_CORE_SRC) $(LAUNCHER_DEP) $(B3ASSET_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GAME_DEFS) $< $(LAUNCHER_CORE_SRC) $(B3ASSET_OBJ) -pthread -ldl -lz -o $@
$(BUILD)/b3launcher: $(LAUNCHER_SRC) $(LAUNCHER_DEP) $(B3ASSET_RT_OBJ) $(PROMPTS_DEP) $(REPORT_DEP)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(SDL3_CFLAGS) -Isrc/b3gfx $(LAUNCHER_SRC) $(B3ASSET_RT_OBJ) $(SDL3_LIBS) -lz -pthread -ldl -o $@
launcher: $(BUILD)/b3launcher
# photo2: the showroom's car list (the game's names, the files' order) and turntable
TESTS += $(BUILD)/test_showroom
$(BUILD)/test_showroom: tests/test_showroom.cpp tests/test.h tests/iso_writer.h $(SHOWROOM_SRC) src/b3launcher/showroom.h $(B3ASSET_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -Isrc/b3launcher $< $(SHOWROOM_SRC) $(B3ASSET_OBJ) -o $@
.PHONY: launcher
# test builds: the bug report's ZIP writer (read back through zlib), the run logs' names and rotation, the version
# string, and a whole report (what is in it, what is not)
TESTS += $(BUILD)/test_report
$(BUILD)/test_report: tests/test_report.cpp tests/test.h $(REPORT_SRC) $(REPORT_DEP) tools/native/run_log.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(REPORT_SRC) -lz -pthread -o $@
# modkit1 (the mod kit's loader, docs/modding/DESIGN.md §1): manifests, the load order, the overlay's resolution
# (case, priority, paths that would leave a mod's folder), the set hash -- on mod folders the test writes
TESTS += $(BUILD)/test_mods
# modkit1b: [requires], file conflicts, installing from crafted zips (zip-slip, links, sizes), the disc layer's overlay
MODS_TEST_SRC := src/b3mod/mods.cpp src/b3mod/archive.cpp src/b3asset/discfs.cpp src/b3report/zip.cpp
$(BUILD)/test_mods: tests/test_mods.cpp tests/test.h include/b3mod/mods.h include/b3mod/archive.h include/b3asset/discfs.h include/b3asset/car_layout.h $(MODS_TEST_SRC) $(CHD_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(MODS_TEST_SRC) $(CHD_OBJ) -lz -o $@
# modtool1 (the mod kit's asset tool, docs/modding/DESIGN.md §3, docs/modding/b3modtool.md): `make modtool` builds
# build/b3modtool; test_modtool checks b3patch, the string tables, PNG, a TXD written back, pack / verify on synthetic data
MODTOOL_SRC := $(wildcard src/b3modtool/*.cpp) src/b3mod/mods.cpp src/b3report/zip.cpp src/b3launcher/showroom.cpp   # models3c: car list / export name the cars as the showroom
MODTOOL_DEP := $(wildcard include/b3modtool/*.h include/b3mod/*.h include/b3asset/*.h include/b3tune/*.h) include/b3report/zip.h src/b3launcher/showroom.h
TESTS += $(BUILD)/test_modtool
$(BUILD)/test_modtool: tests/test_modtool.cpp tests/test.h $(MODTOOL_SRC) $(MODTOOL_DEP) $(B3ASSET_OBJ) | $(BUILD)/b3modtool
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) '-DB3_MODTOOL_BIN="$(abspath $(BUILD)/b3modtool)"' $< $(MODTOOL_SRC) $(B3ASSET_OBJ) -lz -o $@
# modsec: every car reader (read_car, car_paint_map, car_atlas_triangles, the mod tool's car_read / car_export /
# car_import) on a synthetic car cut at every length, built with AddressSanitizer (its own compile of the sources)
TESTS += $(BUILD)/test_car_truncation
ASAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer
CAR_TRUNC_OBJ := $(patsubst %.cpp,$(BUILD)/asan/%.o,tests/test_car_truncation.cpp $(MODTOOL_SRC) $(B3ASSET_SRC))
$(BUILD)/asan/%.o: %.cpp $(MODTOOL_DEP) tests/test.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(ASAN_FLAGS) -c $< -o $@
$(BUILD)/test_car_truncation: $(CAR_TRUNC_OBJ) $(CHD_OBJ)
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) $(CAR_TRUNC_OBJ) $(CHD_OBJ) -lz -o $@
# betaimport: b3asset::convert_prototype_car on synthetic prototype / donor files, every cut input (AddressSanitizer)
TESTS += $(BUILD)/test_carconvert
CARCONVERT_OBJ := $(patsubst %.cpp,$(BUILD)/asan/%.o,tests/test_carconvert.cpp $(B3ASSET_SRC))
$(BUILD)/asan/tests/test_carconvert.o $(BUILD)/asan/tests/test_car_truncation.o: tests/synthetic_car.h
$(BUILD)/test_carconvert: $(CARCONVERT_OBJ) $(CHD_OBJ)
	$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) $(CARCONVERT_OBJ) $(CHD_OBJ) -lz -pthread -o $@
# betaimport: b3asset::beta (the BETA SERIES import) on synthetic ISO images and a synthetic game folder
TESTS += $(BUILD)/test_betaimport
$(BUILD)/test_betaimport: tests/test_betaimport.cpp tests/test.h tests/iso_writer.h tests/synthetic_car.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -lz -pthread -o $@
# carpacks: Import car pack (src/b3launcher/carpack.h), synthetic cars
TESTS += $(BUILD)/test_carpack
$(BUILD)/test_carpack: tests/test_carpack.cpp tests/test.h tests/synthetic_car.h src/b3launcher/carpack.h include/b3asset/carprint.h tools/native/extra_series.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude -Itools/native $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -lz -pthread -o $@
$(BUILD)/b3modtool: tools/modding/b3modtool.cpp $(MODTOOL_SRC) $(MODTOOL_DEP) $(B3ASSET_RT_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(MODTOOL_SRC) $(B3ASSET_RT_OBJ) -lz -o $@
modtool: $(BUILD)/b3modtool
# tune4 (the tuning console, docs/modding/tuning.md; include/b3tune/tune.h): the registry's recording, ranges and
# steps, presets (parsing, precedence, load order), edits through a pad recording and a replay file, [tuning] mods
TESTS += $(BUILD)/test_tune
$(BUILD)/test_tune: tests/test_tune.cpp tests/test.h include/b3tune/tune.h include/b3mod/mods.h src/b3mod/mods.cpp include/b3mod/codemod.h src/b3mod/codemod.cpp tools/native/pad_record.h src/native/replay_file.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/b3mod/mods.cpp src/b3mod/codemod.cpp $(SAVE_SRC) -o $@
# tunenav1: the tuning console's browser (include/b3tune/browse.h): the mapping of the registry table's 705
# registrations, the tree, navigation, favourites / recent, search, the car page and its edits' recording and replay;
# docs/modding/tuning-options.md describes every one of the 705 under its place
TESTS += $(BUILD)/test_tune_browse
$(BUILD)/test_tune_browse: tests/test_tune_browse.cpp tests/test.h include/b3tune/browse.h include/b3tune/tune.h include/b3tune/tune_abi.h include/b3mod/mods.h src/b3mod/mods.cpp include/b3mod/codemod.h src/b3mod/codemod.cpp tools/native/pad_record.h src/native/replay_file.h docs/modding/file-formats/tuning-registry.md docs/modding/tuning-options.md src/b3replay/glyphdraw.cpp src/b3replay/glyphdraw.h src/b3replay/tunelayout.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/b3mod/mods.cpp src/b3mod/codemod.cpp $(SAVE_SRC) src/b3replay/glyphdraw.cpp -o $@
# tunenav2: the CHANGED page and resets (b3tune::Defaults, browse.h): what counts as changed, resets recorded in a pad
# recording and a replay file and played back to the same writes, the page's rows, the counts, RESET SECTION's values
TESTS += $(BUILD)/test_tune_changes
$(BUILD)/test_tune_changes: tests/test_tune_changes.cpp tests/test.h include/b3tune/browse.h include/b3tune/tune.h include/b3tune/tune_abi.h include/b3mod/mods.h src/b3mod/mods.cpp include/b3mod/codemod.h src/b3mod/codemod.cpp tools/native/pad_record.h src/native/replay_file.h $(SAVE_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/b3mod/mods.cpp src/b3mod/codemod.cpp $(SAVE_SRC) -o $@
.PHONY: modtool
# blender1: the Blender add-on (tools/blender/b3_car_tools, a front end on b3modtool; docs/modding/tutorials/09) as
# the zip Blender installs: $(BUILD)/b3_car_tools.zip. Its plain-Python modules are unit-tested in `make test`
# (tools/blender/test_b3_car_tools.py); the add-on in a real Blender: tools/modding/blender_check.sh
BLENDER_ADDON_SRC := $(wildcard tools/blender/b3_car_tools/*.py) tools/blender/b3_car_tools/blender_manifest.toml
$(BUILD)/b3_car_tools.zip: tools/blender/make_addon_zip.py $(BLENDER_ADDON_SRC)
	@mkdir -p $(BUILD)
	python3 -I tools/blender/make_addon_zip.py $@
blender-addon: $(BUILD)/b3_car_tools.zip
.PHONY: blender-addon
# models3c: the car / track export census against the renderer's decode (docs/modding/b3modtool.md "car export")
$(BUILD)/model_export_check: tools/modding/model_export_check.cpp $(MODTOOL_SRC) $(MODTOOL_DEP) $(B3ASSET_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(MODTOOL_SRC) $(B3ASSET_OBJ) -lz -o $@
# import1: the car import census (identity, decode-identical restrip, edits) (docs/modding/b3modtool.md "car import")
$(BUILD)/model_import_check: tools/modding/model_import_check.cpp $(MODTOOL_SRC) $(MODTOOL_DEP) $(B3ASSET_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(MODTOOL_SRC) $(B3ASSET_OBJ) -lz -o $@
# trackimport1: the track import census (the packet writer, identity, re-save, decode-identical restrip, edits)
# (docs/modding/b3modtool.md "track import")
$(BUILD)/track_import_check: tools/modding/track_import_check.cpp $(MODTOOL_SRC) $(MODTOOL_DEP) $(B3ASSET_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $< $(MODTOOL_SRC) $(B3ASSET_OBJ) -lz -o $@
# perf4: FRAMES IN FLIGHT Auto, the warm-up list's file, RENDER SCALE's supersampling cap
TESTS += $(BUILD)/test_perf4
$(BUILD)/test_perf4: tests/test_perf4.cpp tests/test.h include/b3gfx/frames_auto.h include/b3gfx/render_scale.h include/b3gfx/upload_batch.h include/b3gfx/driver_order.h include/b3gfx/b3gfx.h src/b3replay/warmcache.h src/b3replay/perfbisect.h
	$(CXX) -Iinclude -Isrc/b3replay $(REQFLAGS) $(CXXFLAGS) $< -o $@
# lat1: the display clock's schedule mode (src/b3replay/clock.h) on synthetic feeds
TESTS += $(BUILD)/test_clock
# mac3: with REQFLAGS (C++20): Apple clang's own default standard has no std::clamp (the M1's NOT BUILT)
$(BUILD)/test_clock: tests/test_clock.cpp tests/test.h src/b3replay/clock.h src/b3replay/motion_stats.h include/b3live/sched_stamp.h
	$(CXX) -Iinclude $(REQFLAGS) $(CXXFLAGS) $< -o $@
# inproc1 (§23): the in-process feed (include/b3live/inproc.h) into LiveFeed, against the file reader, with and without drops
TESTS += $(BUILD)/test_inproc
$(BUILD)/test_inproc: tests/test_inproc.cpp tests/test.h include/b3live/inproc.h include/b3live/ring.h src/b3replay/livefeed.cpp src/b3replay/garagecam.cpp src/b3replay/garagecam.h src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/orbitcam.h src/b3replay/finishcam.cpp src/b3replay/finishcam.h src/b3replay/replaycams.cpp src/b3replay/replaycams.h src/b3replay/live.h src/b3replay/hud.cpp src/b3replay/hud.h src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/globe.cpp src/b3replay/globe.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $(SDL3_CFLAGS) -Isrc/b3gfx $< src/b3replay/livefeed.cpp src/b3replay/garagecam.cpp src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/finishcam.cpp src/b3replay/replaycams.cpp src/b3replay/hud.cpp src/b3replay/capture.cpp src/b3replay/globe.cpp $(B3ASSET_OBJ) -lz -lpthread -o $@
$(BUILD)/b3assetdump: tools/render/b3assetdump.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/b3vucheck: tools/render/b3vucheck.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/b3vucheck_car: tools/render/b3vucheck_car.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/b3vucheck_trail: tools/render/b3vucheck_trail.cpp src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/hud.cpp src/b3replay/hud.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/capture.cpp src/b3replay/hud.cpp $(B3ASSET_OBJ) -lz -o $@
$(BUILD)/b3vucheck_corona: tools/render/b3vucheck_corona.cpp src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/coronas.cpp src/b3replay/coronas.h src/b3replay/hud.cpp src/b3replay/hud.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/capture.cpp src/b3replay/coronas.cpp src/b3replay/hud.cpp $(B3ASSET_OBJ) -lz -o $@
# Sparks (fx2): the spark program VU1 0x46D090 against b3asset/sparkshade.h (synthetic, or --capture)
$(BUILD)/b3scene_stats: tools/render/b3scene_stats.cpp src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/hud.cpp src/b3replay/hud.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/capture.cpp src/b3replay/hud.cpp $(B3ASSET_OBJ) -lz -o $@
$(BUILD)/b3vucheck_spark: tools/render/b3vucheck_spark.cpp src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/hud.cpp src/b3replay/hud.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/capture.cpp src/b3replay/hud.cpp $(B3ASSET_OBJ) -lz -o $@
TESTS += $(BUILD)/test_sparkshade
$(BUILD)/test_sparkshade: tests/test_sparkshade.cpp tests/test.h $(B3ASSET_OBJ) $(VU_STAMP)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $(VU_DEFS) $< $(B3ASSET_OBJ) -o $@
# vu0: the shared VU core in VU0's configuration, per instruction class, both timings
TESTS += $(BUILD)/test_vu0_core
$(BUILD)/test_vu0_core: tests/test_vu0_core.cpp tests/test.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
# Popcorn (fx2): the popcorn program VU1 0x47C6F0 against b3asset/popcornshade.h (synthetic, or --capture)
$(BUILD)/b3vucheck_popcorn: tools/render/b3vucheck_popcorn.cpp src/b3replay/capture.cpp src/b3replay/capture.h src/b3replay/hud.cpp src/b3replay/hud.h $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< src/b3replay/capture.cpp src/b3replay/hud.cpp $(B3ASSET_OBJ) -lz -o $@
TESTS += $(BUILD)/test_popcornshade
$(BUILD)/test_popcornshade: tests/test_popcornshade.cpp tests/test.h $(B3ASSET_OBJ) $(VU_STAMP)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $(VU_DEFS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/b3hdkeys: tools/render/b3hdkeys.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
$(BUILD)/b3envdump: tools/render/b3envdump.cpp $(B3ASSET_OBJ)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< $(B3ASSET_OBJ) -o $@
# SDL3 3.4.2: Ubuntu 26.04 packages unpacked into ../ext/sdl3-3.4.2 (tools/render/README: versions and hashes).
# macOS: make render SDL3_CFLAGS="$$(pkg-config --cflags sdl3)" SDL3_LIBS="$$(pkg-config --libs sdl3)" (Homebrew sdl3).
# Windows: tools/render/build_win.sh (the verified MSYS2 toolchain + the MSYS2 SDL3 package).
# mac1: on macOS the defaults come from Homebrew's sdl3 (pkg-config, else `brew --prefix sdl3`); its dylib has an
# absolute install name, so no rpath. tools/mac/build.sh passes the same flags explicitly.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3 2>/dev/null || echo -I$$(brew --prefix sdl3 2>/dev/null)/include)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3 2>/dev/null || echo -L$$(brew --prefix sdl3 2>/dev/null)/lib -lSDL3)
endif
# Linux: SDL3_PREFIX (an unpacked SDL3, e.g. Ubuntu's packages as tools/render/README says) when set, else the system's
# SDL3 through pkg-config (libsdl3-dev / SDL3-devel), else ../ext/sdl3-3.4.2/usr beside the repo.
ifeq ($(SDL3_PREFIX)$(UNAME_S),Linux)
ifneq ($(shell pkg-config --exists sdl3 2>/dev/null && echo y),)
SDL3_CFLAGS ?= $(shell pkg-config --cflags sdl3)
SDL3_LIBS ?= $(shell pkg-config --libs sdl3)
endif
endif
SDL3_PREFIX ?= $(abspath ..)/ext/sdl3-3.4.2/usr
SDL3_LIBDIR ?= $(SDL3_PREFIX)/lib/x86_64-linux-gnu
SDL3_CFLAGS ?= -I$(SDL3_PREFIX)/include
SDL3_LIBS ?= -L$(SDL3_LIBDIR) -lSDL3 -Wl,--disable-new-dtags,-rpath,$(SDL3_LIBDIR):$(SDL3_LIBDIR)/pulseaudio
# The Linux tester package (tools/release/package_linux.sh: make BUILD=<dir> LINUX_RELEASE=1 SDL3_PREFIX=<portable
# SDL3> play launcher): programs a typical distro runs -- glibc symbols no newer than 2.35 (Ubuntu 22.04;
# include/b3compat/glibc_compat.h, force-included), libstdc++ / libgcc linked in (no GLIBCXX requirement; their
# symbols not exported from libb3render.so), and libSDL3.so.0 found beside the program (an $ORIGIN RPATH: the
# package's own SDL3, which wins over LD_LIBRARY_PATH, e.g. Steam's runtime)
ifeq ($(LINUX_RELEASE),1)
override CXXFLAGS += -include $(abspath include/b3compat/glibc_compat.h) -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL
override SDL3_LIBS := -L$(SDL3_LIBDIR) -lSDL3 -Wl,--disable-new-dtags,-rpath,'$$ORIGIN'
endif
# The standalone's audio output (audio1, src/native/audio_device.*) on SDL3's dummy driver (no sound device needed);
# linked with SDL3 like test_panel_epoch
TESTS += $(BUILD)/test_audio_device
$(BUILD)/test_audio_device: tests/test_audio_device.cpp tests/test.h src/native/audio_device.cpp src/native/audio_device.h src/native/audio_queue.h $(RWA_IOP_DEP)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(SDL3_CFLAGS) $< src/native/audio_device.cpp $(RWA_IOP_SRC) $(SDL3_LIBS) -o $@
# audioq: a captured mix through the old drift guard and the output queue on a simulated timeline (WAVs and counts;
# tools/native/audio_sim.cpp); a tool, not a test
$(BUILD)/audio_sim: tools/native/audio_sim.cpp src/native/audio_queue.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(SDL3_CFLAGS) $< $(SDL3_LIBS) -o $@
test_audio_device: $(BUILD)/test_audio_device
	LD_LIBRARY_PATH=$(SDL3_LIBDIR)/pulseaudio$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH} $(BUILD)/test_audio_device   # libpulse's RUNPATH hides the exe's RPATH for its own deps
.PHONY: test_audio_device
B3VIEW_SRC := src/b3gfx/b3gfx_sdl.cpp src/b3view/scene.cpp src/b3view/carscene.cpp src/b3view/crashscene.cpp src/b3view/sun.cpp src/b3view/main.cpp
B3VIEW_OBJ := $(B3VIEW_SRC:%.cpp=$(BUILD)/render/%.o)
$(B3VIEW_OBJ): RENDER_CPPFLAGS = $(SDL3_CFLAGS) -Isrc/b3gfx
$(BUILD)/b3view: $(B3VIEW_OBJ) $(B3ASSET_OBJ)
	$(CXX) $(CXXFLAGS) $^ $(SDL3_LIBS) -o $@
# b3replay (docs/native-renderer.md §12): a recorded race from a scene capture in the native renderer.
B3REPLAY_SRC := src/b3gfx/b3gfx_sdl.cpp src/b3view/scene.cpp src/b3view/carscene.cpp src/b3view/crashscene.cpp src/b3view/sun.cpp src/b3replay/capture.cpp src/b3replay/hud.cpp src/b3replay/effects.cpp src/b3replay/coronas.cpp src/b3replay/sparks.cpp src/b3replay/globe.cpp src/b3replay/view.cpp src/b3replay/trafficfade.cpp src/b3replay/splitlayout.cpp src/b3replay/live.cpp src/b3replay/livefeed.cpp src/b3replay/nativepad.cpp src/b3replay/pcpage.cpp src/b3replay/nativeaudio.cpp src/native/input_keys.cpp src/native/input_report.cpp src/native/input_bindings.cpp src/b3replay/fpspanel.cpp src/b3replay/skiphint.cpp src/b3replay/racetime.cpp src/b3replay/paintsel.cpp src/b3replay/hudmods.cpp src/b3replay/prompts.cpp src/b3replay/glyphdraw.cpp src/b3replay/menumouse.cpp src/b3replay/pcsettings.cpp src/b3settings/settings.cpp src/b3settings/safemode.cpp src/b3settings/page.cpp src/b3settings/menu.cpp src/b3replay/notice.cpp src/b3replay/photomode.cpp src/b3replay/garagecam.cpp src/b3replay/orbitcam.cpp src/b3replay/trackrays.cpp src/b3replay/finishcam.cpp src/b3replay/replaycams.cpp src/b3replay/photohud.cpp src/b3replay/a11y.cpp src/b3replay/ghost.cpp src/b3replay/speedrun.cpp src/b3replay/speedrunpanel.cpp src/b3replay/tuneconsole.cpp src/b3replay/carlook.cpp src/b3replay/carlookpanel.cpp $(REPORT_SRC) src/b3report/sysinfo.cpp src/b3mod/mods.cpp src/b3mod/codemod.cpp src/b3replay/modoverlay.cpp src/b3replay/main.cpp
B3REPLAY_OBJ := $(B3REPLAY_SRC:%.cpp=$(BUILD)/render/%.o)
$(B3REPLAY_OBJ): RENDER_CPPFLAGS = $(SDL3_CFLAGS) -Isrc/b3gfx
$(B3REPLAY_OBJ): $(wildcard src/b3replay/*.h include/b3settings/*.h include/b3report/*.h include/b3mod/*.h include/b3tune/*.h include/b3asset/discfs.h) src/native/input_keys.h src/native/input_keys_sdl.h src/native/input_bindings.h src/native/pad_gyro.h src/native/pad_triggers.h   # explicit targets: a recipe-less pattern rule cancels the implicit rule instead of adding prerequisites
$(BUILD)/b3replay: $(sort $(B3VIEW_OBJ) $(B3REPLAY_OBJ)) $(B3ASSET_OBJ)
	$(CXX) $(CXXFLAGS) $(filter-out $(BUILD)/render/src/b3view/main.o,$^) $(SDL3_LIBS) -lz -o $@
# b3livefeed: a scene capture played into a live ring as src/gameview/scene_capture.cpp publishes it (tests b3replay --live, §13).
$(BUILD)/b3livefeed: tools/render/b3livefeed.cpp include/b3live/ring.h
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) $< -lz -o $@
# b3clocksim (live2, §20): the live viewer's display clock on a play session's recorded tick arrivals.
$(BUILD)/b3clocksim: tools/render/b3clocksim.cpp src/b3replay/clock.h
	$(CXX) $(CPPFLAGS) -std=c++20 $(CXXFLAGS) $< -o $@

# b3latsim (perf1, §33): the low-latency clock (step_sched) on a run's logged ticks, scored against the camera path.
$(BUILD)/b3latsim: tools/render/b3latsim.cpp src/b3replay/clock.h
	$(CXX) $(CPPFLAGS) -std=c++20 $(CXXFLAGS) $< -o $@

# pic1 (docs/native-renderer.md §32): the in-process renderer as a shared library off Windows (libb3render.so; Windows:
# b3render.dll from tools/render/build_win.sh), the same objects position-independent, main.cpp as the library
# (B3REPLAY_LIBRARY) plus the C ABI (src/b3replay/inproc.cpp); and the game view's replay proof (gameview_replay).
# effects3: the renderer (and only it) has the VU core again: the particle layer runs the game's program 0x46D750 on the
# captured arrays (src/b3asset/particleshade.cpp)
B3RENDER_SO_SRC := $(filter-out src/b3replay/main.cpp,$(B3REPLAY_SRC)) src/b3replay/inproc.cpp $(B3ASSET_RT_SRC) src/b3asset/vu1.cpp src/b3asset/particleshade.cpp
B3RENDER_SO_OBJ := $(B3RENDER_SO_SRC:%.cpp=$(BUILD)/render-pic/%.o) $(BUILD)/render-pic/b3render_main.o $(CHD_OBJ)
$(BUILD)/render-pic/%.o: %.cpp $(wildcard include/b3asset/*.h include/b3gfx/*.h include/b3live/*.h src/b3view/*.h src/b3replay/*.h src/b3gfx/shaders_gen.h include/b3settings/*.h include/b3report/*.h include/b3mod/*.h include/b3tune/*.h) src/native/input_keys.h src/native/input_keys_sdl.h src/native/input_bindings.h src/native/pad_gyro.h src/native/pad_triggers.h
	@mkdir -p $(dir $@)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) -fPIC $(SDL3_CFLAGS) -Isrc/b3gfx -c $< -o $@
# mac5: the same headers as every other render-pic object -- with only src/b3replay/*.h it kept a b3gfx.h change's old
# CarParams layout while view.o had the new one (an ODR mismatch: View's destructor crashed at the end of every run)
$(BUILD)/render-pic/b3render_main.o: src/b3replay/main.cpp $(wildcard include/b3asset/*.h include/b3gfx/*.h include/b3live/*.h src/b3view/*.h src/b3replay/*.h src/b3gfx/shaders_gen.h include/b3settings/*.h include/b3report/*.h include/b3mod/*.h include/b3tune/*.h) src/native/input_keys.h src/native/input_keys_sdl.h src/native/input_bindings.h src/native/pad_gyro.h src/native/pad_triggers.h
	@mkdir -p $(dir $@)
	$(CXX) -Iinclude $(RENDER_FLAGS) $(CXXFLAGS) -fPIC $(SDL3_CFLAGS) -Isrc/b3gfx -DB3REPLAY_LIBRARY -c $< -o $@
$(BUILD)/libb3render.so: $(B3RENDER_SO_OBJ)
	$(CXX) -shared $(CXXFLAGS) $^ $(SDL3_LIBS) -lz -lpthread -o $@
GAMEVIEW_SRC := src/gameview/scene_capture.cpp src/gameview/hud_capture.cpp
$(BUILD)/gameview_replay: tools/native/gameview_replay.cpp $(GAMEVIEW_SRC) include/b3gameview/gameview.h include/b3live/ring.h
	@mkdir -p $(BUILD)
	$(CXX) -Iinclude -std=gnu++20 -Wall -Wextra $(CXXFLAGS) $< $(GAMEVIEW_SRC) -lz -ldl -o $@
gameview: $(BUILD)/libb3render.so $(BUILD)/gameview_replay
.PHONY: gameview
# the native hook path (traced_entry -> watch hook -> capture records), without the generated table or a renderer
TESTS += $(BUILD)/test_gameview_native
$(BUILD)/test_gameview_native: tests/test_gameview_native.cpp tests/test.h tools/native/gameview_native.cpp tools/native/gameview_native.h $(GAMEVIEW_SRC) include/b3gameview/gameview.h include/b3host/native_backend.h src/b3host/native_backend.cpp $(HOST_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -Itests -Itools/native $< tools/native/gameview_native.cpp $(GAMEVIEW_SRC) src/b3host/native_backend.cpp $(HOST_SRC) -pthread -ldl -o $@

render: $(BUILD)/b3view $(BUILD)/b3assetdump $(BUILD)/b3vucheck $(BUILD)/b3vucheck_car $(BUILD)/b3vucheck_trail $(BUILD)/b3vucheck_corona $(BUILD)/b3vucheck_spark $(BUILD)/b3vucheck_popcorn $(BUILD)/b3envdump $(BUILD)/b3hdkeys $(BUILD)/b3replay $(BUILD)/b3livefeed $(BUILD)/b3clocksim $(BUILD)/b3latsim
shaders:
	tools/render/build_shaders.sh
.PHONY: render shaders

# mac1: the self-contained tests -- no decomp snapshot, no disc data needed (docs/mac.md). Built from committed
# sources only and all run (a failure does not stop the rest); a summary line per test, then
# $(BUILD)/mac-tests/summary.txt. Optional real-data checks: RETAIL_ELF=<disc>/SLUS_210.50 (libm / eeasm / platform
# against retail code in tests/r5900_mini.h), GAME=<disc folder> (test_b3asset's real-file checks).
# RUN= prefixes every test (the Linux cross check, tools/mac/cross_check.sh: RUN=qemu-aarch64).
#   exactness: the PS2 float model (b3fp / ps2f), Ps2FloatEnv, GtMathPs2 (VU0 model) in both modes, b3libm, b3asm, b3host
#   other: the renderer's decoders / shading models, HUD / capture readers, settings, clocks, audio DSP, native backend
MAC_TESTS_EXACT := test_ps2float test_fenv test_vufloat test_gtmath_ps2 test_gtmath_fast test_libm test_libm_more test_overrides \
                   test_libm_cost test_eeasm_ps2 test_eeasm_fast test_b3host test_abi
MAC_TESTS_OTHER := test_b3asset test_hdpack test_xboximport test_carpaint test_sparkshade test_popcornshade test_hud test_capture test_livelap \
                   test_settings test_effect_levels test_carlook test_clock test_perf4 test_inproc test_rwa_iop test_spu2_mixer test_b3route \
                   test_native_backend test_native_platform test_gameview_native test_save_card test_relaunch test_input_keys test_input_bindings test_pad_motion test_a11y test_skip_hint test_prompts test_photomode test_garagecam test_orbitcam test_rain test_rain_audio test_fpspanel test_panel_epoch test_page_hold test_pause_rows test_paint_select test_carousel_models test_thumb_release test_render_load test_ghost test_speedrun test_traffic_fade test_split_layout test_menu_input test_save_formats test_save_stats test_launcher test_launcher_installer test_portable test_launcher_steam test_discfs test_report test_mods test_modtool test_replay_file test_showroom test_fast_load test_race_time test_race_options test_hudmods test_xboxglow test_takedown_pitch test_eliminator_rows test_more_rivals test_heavy_series test_game_options test_title_fast test_finishcam test_bit_owners test_driver_labels test_stats_signals test_traffic_check test_traffic_series test_betaimport test_carpack test_boot_boost test_game_limits test_carmatch test_settings_back test_boost_start test_extra_series test_taillights test_replaycams test_pad_context
MAC_TESTS := $(MAC_TESTS_EXACT) $(MAC_TESTS_OTHER)
RUN ?=
mac-tests-build:
	@mkdir -p build $(BUILD)/mac-tests
	$(MAKE) --no-print-directory -k $(addprefix $(BUILD)/,$(MAC_TESTS)) || true
mac-tests: mac-tests-build
	@# tests write scratch files under build/ (fixed paths), so it must exist whatever BUILD is
	@mkdir -p build $(BUILD)/mac-tests; : > $(BUILD)/mac-tests/summary.txt; fail=0; \
	for g in exactness:"$(MAC_TESTS_EXACT)" other:"$(MAC_TESTS_OTHER)"; do \
	  grp=$${g%%:*}; for t in $${g#*:}; do \
	    log=$(BUILD)/mac-tests/$$t.log; \
	    if [ ! -x $(BUILD)/$$t ]; then line="NOT BUILT"; fail=1; \
	    else start=$$(date +%s); $(RUN) $(BUILD)/$$t > $$log 2>&1; rc=$$?; secs=$$(( $$(date +%s) - start )); \
	      if [ $$rc = 0 ] && grep -q ' 0 checks, 0 failures' $$log; then line="SKIP  (no checks ran: needs RETAIL_ELF / GAME)"; \
	      elif [ $$rc = 0 ]; then line="PASS  $$(tail -1 $$log)"; else line="FAIL rc=$$rc  $$(grep -m1 -E 'FAIL|failures' $$log)"; fail=1; fi; \
	      line="$$line  ($${secs}s)"; fi; \
	    printf '%-10s %-22s %s\n' "$$grp" "$$t" "$$line" | tee -a $(BUILD)/mac-tests/summary.txt; \
	  done; done; \
	echo "== $(BUILD)/mac-tests/summary.txt (logs beside it)"; exit $$fail
print-mac-tests:
	@echo $(MAC_TESTS)
.PHONY: mac-tests mac-tests-build print-mac-tests

gen: $(GEN)/manifest.tsv
test:
	@$(MAKE) --no-print-directory gen
	@$(MAKE) --no-print-directory -j$(NPROC) all $(TESTS) $(BUILD)/b3modtool   # b3modtool: the add-on test uses the real one with a disc; all: $(TESTS) is expanded at line 49, before the render tests are added
	@mkdir -p $(BUILD)/test-logs; rm -f $(BUILD)/test-logs/*.log
	@$(MAKE) --no-print-directory $(patsubst $(BUILD)/%,test-run/%,$(TESTS))
	@tests/test_summary.sh $(BUILD)/test-logs
	@echo "== tools/render/embed_shaders.py --check (src/b3gfx/shaders_gen.h made from the current shaders)"; python3 -I tools/render/embed_shaders.py --check src/b3gfx/shaders_gen.h src/b3gfx/shaders
	@echo "== compile-fail"; tests/compile_fail/check.sh $(CXX) $(CPPFLAGS) $(REQFLAGS)
	@echo "== eeasm.py --selftest"; python3 -I tools/decomp2host/eeasm.py --selftest > $(BUILD)/eeasm_selftest.log
	@echo "== tests/test_decomp2host.py"; python3 -I tests/test_decomp2host.py
	@echo "== tests/test_vu0_aot.py (vu0flags: the kept flag pushes, the census rules)"; python3 -I tests/test_vu0_aot.py
	@echo "== tools/native/check_returns.py --selftest (a fall-off is caught with -w in the flags)"; python3 -I tools/native/check_returns.py --selftest --cache $(BUILD)/check_returns/selftest -- $(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS)
	@echo "== tools/release/make_icons.py --check (the icon files match the drawing)"; python3 -I tools/release/make_icons.py --check
	@echo "== tools/release/make_steam_logo.py --check (the launcher's built-in logo matches docs/logo.png)"; python3 -I tools/release/make_steam_logo.py --check
	@echo "== tools/blender/test_b3_car_tools.py"; $(if $(wildcard $(GAME)/SLUS_210.50),B3_GAME_DIR="$${B3_GAME_DIR:-$(GAME)}") python3 -I tools/blender/test_b3_car_tools.py
	@# boot2 (2026-11-07): one set of kinds per extern "C" name in the port tracks (hard), and the generated
	@# native units' count as a watched number (report only: gen1's direct-link routing makes it fall)
	@echo "== tools/native/decl_check.py (src/libs)"; mkdir -p $(BUILD); python3 -I tools/native/decl_check.py > $(BUILD)/decl_check.txt; s=$$?; tail -1 $(BUILD)/decl_check.txt; [ $$s -eq 0 ] || { grep -v '^decl_check:' $(BUILD)/decl_check.txt | head -20; exit $$s; }
	@if [ -d build/gen_native/units ]; then echo "== decl_check (generated native units, report only)"; python3 -I tools/native/decl_check.py --root build/gen_native/units | tail -1 || true; fi

# One test binary's run (make test): its output in $(BUILD)/test-logs/<name>.log, one line here (the whole log on a
# failure). The binaries are independent (each its own scratch paths under build/ and the temp folder), so make's -j
# (the default job count above) runs them side by side. TEST_ENV_<name>: what one of them needs in its environment.
TEST_ENV_test_audio_device = LD_LIBRARY_PATH=$(SDL3_LIBDIR)/pulseaudio$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}
test-run/%:
	@log=$(BUILD)/test-logs/$*.log; \
	$(if $(wildcard $(GAME)/SLUS_210.50),B3_GAME_DIR="$${B3_GAME_DIR:-$(GAME)}") $(if $(SAVES),B3_SAVES_ROOT="$${B3_SAVES_ROOT:-$(SAVES)}") $(TEST_ENV_$*) \
	  $(BUILD)/$* > $$log 2>&1; rc=$$?; \
	if [ $$rc != 0 ]; then echo "== $(BUILD)/$*: FAIL (exit $$rc)"; cat $$log; exit 1; fi; \
	echo "== $(BUILD)/$*: $$(tail -1 $$log)"

clean:
	python3 -c "import shutil; shutil.rmtree('$(BUILD)', ignore_errors=True)"

.PHONY: all test clean gen

# Native backend (S0, docs/standalone.md §8): generated game code from boot.
#   make native     build/native_boot (boot coverage, the boot oracle); not part of `make test`
# The function table comes from a pinned decomp snapshot (git archive of a decomp commit: assembly/asm, config,
# src/nonmatching, orig), generated into build/native (names and addresses only; never committed).
# mac4 (with boot2): the function table comes from the same pinned snapshot as the generation (DECOMP), so the
# two cannot drift; it was s0native while DECOMP moved on
NATIVE_SNAP ?= $(DECOMP)
ZLIB_LIBS ?= $(if $(or $(filter Darwin,$(UNAME_S)),$(wildcard /usr/include/zlib.h)),-lz,)   # boot2: was for the retired snapshot restore's gzipped SIF logs; no native source uses zlib now (harmless)
# mac4 (2026-11-07): the committed native source bundle gen/native -- the generation of
# build/gen_native (units, include, manifest) plus the function table (table/functions.h, .tsv) -- so a machine without
# the decomp (the Mac, a code-only checkout) builds native_boot / native_play. NATIVE_FROM=decomp generates from
# $(DECOMP) / $(NATIVE_SNAP) into build/ (the default when $(DECOMP)/SNAPSHOT exists); NATIVE_FROM=bundle builds from
# gen/native as committed (no generation rules at all). Refreshed by `make native-bundle` (below), by whoever merges
# native changes (as gen/decomp).
NATIVE_BUNDLE := gen/native
NATIVE_FROM ?= $(if $(wildcard $(DECOMP)/SNAPSHOT),decomp,bundle)
ifeq ($(NATIVE_FROM),bundle)
NATIVE_GEN := $(NATIVE_BUNDLE)/table
else
NATIVE_GEN := $(BUILD)/native
endif
# vu0 (2026-11-07): retail's four VU0 microprograms as C++ (tools/native/vu0_aot.py, src/native/vu0_aot_rt.h).
# vu0n (2026-11-10, goal: VU0 fully native): the generated programs are committed with the rest of the generated
# game code (gen/native/vu0/vu0_aot_gen.cpp: the translated programs and each image's SHA-256, no microcode words), so
# every build -- a fresh clone without the disc too -- runs them. `make vu0-aot` regenerates the file from the decomp
# snapshot ($(DECOMP)/assembly/assets/vu) or from VU0_AOT_ELF=<SLUS_210.50>, and fails (the file unchanged) when an image
# does not match tools/native/vu0_aot_images.sha256; `make native-bundle` regenerates it with the bundle.
VU0_AOT_GEN := gen/native/vu0/vu0_aot_gen.cpp
VU0_AOT_ELF ?=
vu0-aot:
	@$(if $(VU0_AOT_ELF),test -f $(VU0_AOT_ELF) || { echo "vu0-aot: no $(VU0_AOT_ELF)"; exit 2; },test -d $(DECOMP)/assembly/assets/vu || { echo "vu0-aot: needs the decomp snapshot's VU images (DECOMP=$(DECOMP)) or VU0_AOT_ELF=<SLUS_210.50>"; exit 2; })
	@mkdir -p $(dir $(VU0_AOT_GEN))
	python3 -I tools/native/vu0_aot.py $(if $(VU0_AOT_ELF),--elf $(VU0_AOT_ELF),$(DECOMP)/assembly/assets/vu) $(VU0_AOT_GEN)
.PHONY: vu0-aot
# The VU0 interpreter (the shared VU core, src/b3asset/vu1.cpp) is compiled out of native_play / native_boot by default
# (the shipped game; package_win.sh / package_linux.sh build the default): a launch no generated program covers is
# fatal. VU0_INTERP=1 (dev builds: B3_VU0_AOT=0 / check, B3_VU0_TIMING=hw / both) compiles it in; the
# setting is stamped, so switching it rebuilds vu0_unit.o and relinks. The tests that use the interpreter as the oracle
# (test_vu0_core, test_native_platform) always have it.
VU0_INTERP ?= 0
VU0_INTERP_SRC := src/b3asset/vu1.cpp
VU0_INTERP_STAMP := $(BUILD)/native/vu0_interp.txt
$(shell python3 -c "import os, sys; p, v = sys.argv[1], sys.argv[2]; os.makedirs(os.path.dirname(p), exist_ok=True); (os.path.exists(p) and open(p).read() == v) or open(p, 'w').write(v)" '$(VU0_INTERP_STAMP)' '$(VU0_INTERP)')
NATIVE_SRC := src/b3host/native_backend.cpp src/native/native_table.cpp src/native/hook_table.cpp src/native/input_keys.cpp src/native/input_report.cpp src/native/platform.cpp src/native/platform_crt.cpp src/native/platform_printf.cpp src/native/platform_iop.cpp src/native/platform_kernel.cpp src/native/platform_time.cpp src/native/platform_malloc.cpp src/native/platform_hw.cpp src/native/platform_sif.cpp src/native/platform_disc.cpp src/native/platform_gtfs.cpp src/native/platform_distortion.cpp src/native/platform_devices.cpp src/native/platform_dbc.cpp src/native/platform_charge.cpp src/native/platform_threads.cpp src/native/sampler.cpp src/native/save_card.cpp src/native/save_formats.cpp src/native/save_store.cpp src/native/save_stats.cpp src/native/platform_rwa.cpp src/native/custom_music.cpp src/native/music_decoders.cpp src/native/now_playing.cpp src/native/now_playing_mpris.cpp src/native/now_playing_win.cpp src/audio/rwa_iop.cpp src/audio/b3route.cpp src/audio/spu2_stub_mixer.cpp src/audio/spu2_mixer_impl.cpp src/native/platform_movie.cpp src/native/mpeg2_decoder.cpp src/native/platform_vu0.cpp src/native/vu0_unit.cpp src/b3asset/discfs.cpp src/b3mod/mods.cpp src/b3mod/codemod.cpp src/native/code_mods.cpp src/native/script_mods.cpp src/native/script_lua.cpp src/b3settings/settings.cpp src/b3settings/safemode.cpp $(VU0_AOT_GEN) $(if $(filter 1,$(VU0_INTERP)),$(VU0_INTERP_SRC))
NATIVE_FLAGS = -Isrc/native -I$(NATIVE_GEN) -Itests -Itools/native -DB3_BOUNCE_CANARY=1 $(NATIVE_OPT)
# mac4 (boot2's condition): a stale bundle is loud -- a decomp-mode build of the native programs warns when gen/native
# was generated from another decomp commit than $(DECOMP)'s (refresh: make native-bundle, then commit gen/native)
ifeq ($(NATIVE_FROM),decomp)
ifneq ($(filter native play native_boot native_play %/native_boot %/native_play,$(MAKECMDGOALS)),)
NATIVE_BUNDLE_REV := $(word 2,$(shell cat $(NATIVE_BUNDLE)/SNAPSHOT 2>/dev/null))
DECOMP_REV := $(shell cat $(DECOMP)/SNAPSHOT 2>/dev/null)
ifneq ($(NATIVE_BUNDLE_REV),$(DECOMP_REV))
$(warning gen/native (the committed native bundle) is from decomp $(or $(NATIVE_BUNDLE_REV),?), this build's DECOMP from $(DECOMP_REV): run make native-bundle and commit gen/native)
endif
endif
endif
ifneq ($(NATIVE_FROM),bundle)
$(NATIVE_GEN)/functions.h: tools/native/gen_table.py
	python3 -I tools/native/gen_table.py --decomp $(NATIVE_SNAP) --out $(NATIVE_GEN)
endif
# boot1: the native backend links its own, larger generation (build/gen_native, never committed): every function of
# config/native_select.txt (gen/decomp's live selection plus every decomp function that generates and compiles alone,
# tools/native/native_select.py), generated together with --keep-going (units that fail together: build/gen_native/
# failed.tsv). The live table (gen/decomp, config/game_overrides.txt: the unit tests') is unchanged by it.
NATIVE_SELECT := config/native_select.txt
NGEN := $(if $(filter bundle,$(NATIVE_FROM)),$(NATIVE_BUNDLE),$(BUILD)/gen_native)
# boot2 (2026-11-07): NATIVE_OPT=-O1 (or -O0 -g) for iteration builds of the native binaries; empty = the -O2 of
# CXXFLAGS (proof runs). Comes last, so it wins.
NATIVE_OPT ?=
# the objects of a non-default NATIVE_OPT live in their own directory (gen_native_ps2-O1 ...), so the two never mix
NOBJ := $(BUILD)/gen_native_ps2$(if $(strip $(NATIVE_OPT)),$(subst $(eval) ,,$(strip $(NATIVE_OPT))))$(if $(filter bundle,$(NATIVE_FROM)),-bundle)
# (mac4: the bundle's objects apart from the decomp generation's: the two units of a name may differ)
NGENFLAGS = $(subst -I$(GEN)/include,-I$(NGEN)/include,$(GENFLAGS)) -DB3_BOUNCE_CANARY=1 $(NATIVE_OPT)   # boot2: native builds only (call.h)
# macintel1: <functional> first on macOS. A unit's own function-like macros (func_003BB7A0's dlmalloc `first(b)`) come
# before its trailing b3host/native_rows.h, and Xcode 16's libc++ <functional> calls `__f_.first()`, which they rewrite
ifeq ($(UNAME_S),Darwin)
NGENFLAGS += -include functional
endif
# + every function of a port track (decomp2host PORT_TRACKS: src/libs/rw RenderWare core, src/libs/rwa RW Audio), by
# its `// 0xADDR` marker
NATIVE_SELECT_ALL := $(NATIVE_GEN)/select.txt
PORT_LIB_SRC := $(wildcard src/libs/*/*.cpp src/libs/*/include/*.h src/libs/*/include/*/*.h)
ifneq ($(NATIVE_FROM),bundle)
# gen1 (option A): NATIVE_SINIT_RETAIL=1 leaves the static initialisers and their walker func_00100240 to
# the fallback mini (retail code at boot: their 16-byte constants' stale stack lanes as retail's). The stamp file holds
# the value, rewritten only when it changes, so switching it regenerates the selection.
NATIVE_SINIT_RETAIL ?= 0
SINIT_STAMP := $(NATIVE_GEN)/sinit_retail.txt
$(shell python3 -c "import os; p='$(SINIT_STAMP)'; v='$(NATIVE_SINIT_RETAIL)'; os.makedirs(os.path.dirname(p), exist_ok=True); (os.path.exists(p) and open(p).read() == v) or open(p, 'w').write(v)")
$(NATIVE_SELECT_ALL): $(NATIVE_SELECT) tools/native/native_select.py $(PORT_LIB_SRC) $(SINIT_STAMP)
	@mkdir -p $(dir $@)
	python3 -I tools/native/native_select.py --base $(NATIVE_SELECT) $(if $(filter 1,$(NATIVE_SINIT_RETAIL)),--sinit-retail) --out $@
# boot2: generated into $(NGEN).new and copied over $(NGEN) by content (rsync -c), so a unit whose text did not change
# keeps its time stamp and its object is not rebuilt (a port-track edit rebuilds its own unit, not all 4,500); objects
# depend on their unit and, through -MMD, on the headers they include.
# native3: the VU0 register binding baked into the native generation only (DECOMP2HOST_VUBIND, eeasm.VuBinding;
# gen/decomp never: there the host VU state is not the guest's without VU0 sync). NATIVE_VUBIND= turns it off.
NATIVE_VUBIND ?= config/vubind_native.txt
# MORE RIVALS: the game's per-car arrays as b3host::split_array in the native generation (decomp2host's last pass,
# tools/decomp2host/split_arrays.py; flat memory unless a race has more than 6 cars). SPLIT_ARRAYS= turns it off.
SPLIT_ARRAYS ?= config/split_arrays.tsv
# boot2: $(DECOMP)/SNAPSHOT too -- a new snapshot regenerates (switching DECOMP alone did not)
$(NGEN)/manifest.tsv: $(NATIVE_SELECT_ALL) tools/decomp2host/decomp2host.py tools/decomp2host/eeasm.py $(NATIVE_VUBIND) config/mod_hooks.txt $(SPLIT_ARRAYS) config/split_arrays_left.txt tools/decomp2host/split_arrays.py tools/decomp2host/split_census.py $(wildcard $(DECOMP)/SNAPSHOT)
	DECOMP2HOST_CXX="$(CXX)" DECOMP2HOST_SPLIT_ARRAYS=$(SPLIT_ARRAYS) DECOMP2HOST_NATIVE_ROWS=1 DECOMP2HOST_T31C=1 DECOMP2HOST_VUBIND=$(NATIVE_VUBIND) DECOMP2HOST_CACHE=$(BUILD)/d2h_cache python3 -I tools/decomp2host/decomp2host.py --decomp $(DECOMP) --out $(NGEN).new --select $(NATIVE_SELECT_ALL) --keep-going --objects $(BUILD)/gen_native_ps2
	rsync -rc --delete --out-format='gen_native: %n' $(NGEN).new/ $(NGEN)/
	$(call check_returns_native,$(NGEN)/units)
	touch $@
endif
# mac4: gen/native, regenerated hermetically (an empty gen/native.new, no DECOMP2HOST_CACHE) from $(DECOMP) and
# $(NATIVE_SNAP) with the selection of `make native`, checked for disc bytes (tools/native/bundle_check.py; with the ELF
# when RETAIL_ELF exists), then synced by content with --delete (README.md kept). Whoever merges native changes
# (selection, generator, port tracks, a new snapshot) runs it and commits gen/native, as with gen/decomp.
# vu0n: the bundle holds the VU0 microprograms as C++ too (vu0/vu0_aot_gen.cpp, as make vu0-aot writes it).
native-bundle:
	@test -f $(DECOMP)/SNAPSHOT || { echo "native-bundle: needs the decomp snapshot (DECOMP=$(DECOMP))"; exit 2; }
	python3 -c "import shutil; shutil.rmtree('$(NATIVE_BUNDLE).new', ignore_errors=True); shutil.rmtree('$(BUILD)/native_bundle', ignore_errors=True)"
	@mkdir -p $(BUILD)/native_bundle
	python3 -I tools/native/native_select.py --base $(NATIVE_SELECT) --out $(BUILD)/native_bundle/select.txt
	env -u DECOMP2HOST_CACHE DECOMP2HOST_CXX="$(CXX)" DECOMP2HOST_SPLIT_ARRAYS=$(SPLIT_ARRAYS) DECOMP2HOST_NATIVE_ROWS=1 DECOMP2HOST_T31C=1 DECOMP2HOST_VUBIND=$(NATIVE_VUBIND) python3 -I tools/decomp2host/decomp2host.py --decomp $(DECOMP) --out $(NATIVE_BUNDLE).new --select $(BUILD)/native_bundle/select.txt --keep-going
	python3 -I tools/native/gen_table.py --decomp $(NATIVE_SNAP) --out $(NATIVE_BUNDLE).new/table
	@# vu0n: retail's VU0 microprograms as C++ (as make vu0-aot), from the same snapshot
	@mkdir -p $(NATIVE_BUNDLE).new/vu0
	python3 -I tools/native/vu0_aot.py $(DECOMP)/assembly/assets/vu $(NATIVE_BUNDLE).new/vu0/vu0_aot_gen.cpp
	@# what it was generated from: the snapshot's name and its decomp commit (and the table's, when NATIVE_SNAP differs)
	python3 -c "import os; f=lambda d: os.path.basename(os.path.normpath(d)) + ' ' + open(os.path.join(d, 'SNAPSHOT')).read().strip(); g, t = f('$(DECOMP)'), f('$(NATIVE_SNAP)'); open('$(NATIVE_BUNDLE).new/SNAPSHOT', 'w').write(g + '\n' + ('' if t == g else 'table ' + t + '\n'))"
	python3 -I tools/native/bundle_check.py $(if $(wildcard $(RETAIL_ELF)),--elf $(RETAIL_ELF)) $(NATIVE_BUNDLE).new
	python3 -I tools/native/check_returns.py --cache $(BUILD)/check_returns/bundle --headers $(NATIVE_BUNDLE).new/include --headers include/b3host $(NATIVE_BUNDLE).new/units -- $(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(subst -I$(GEN)/include,-I$(NATIVE_BUNDLE).new/include,$(GENFLAGS)) -DB3_BOUNCE_CANARY=1 -DB3_PS2_FLOAT=1
	rsync -rc --delete --exclude README.md --out-format='gen/native: %n' $(NATIVE_BUNDLE).new/ $(NATIVE_BUNDLE)/
	python3 -c "import shutil; shutil.rmtree('$(NATIVE_BUNDLE).new')"
.PHONY: native-bundle
# genret: no generated non-void function falls off its end (tools/native/check_returns.py: each unit compiled with
# -Werror=return-type, which the generated units' -w hides; cached per unit by content plus the generated headers and
# include/b3host (a removed [[noreturn]] rechecks) under build/check_returns, so a rerun compiles only changed units).
# Hand-written code has -Werror=return-type already and the generated units change only when generated, so the
# generation runs it (the native generation above, native-bundle, gen/decomp's rule), not `make test` (which runs its
# selftest); `make check-returns` checks the current generations (NGEN: build/gen_native, or gen/native with
# NATIVE_FROM=bundle; and gen/decomp). It takes make's job tokens (jobserver), so `make -j8` stays at 8 jobs; without
# -j, or without a jobserver it can reach, it runs one compile at a time.
check_returns_native = python3 -I tools/native/check_returns.py --cache $(BUILD)/check_returns/native$(if $(filter bundle,$(NATIVE_FROM)),-bundle) --headers $(NGEN)/include --headers include/b3host $(1) -- $(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(NGENFLAGS) -DB3_PS2_FLOAT=1
check_returns_gen = python3 -I tools/native/check_returns.py --cache $(BUILD)/check_returns/decomp --headers $(GEN)/include --headers include/b3host $(1) -- $(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(GENFLAGS) -DB3_PS2_FLOAT=1
check-returns: $(NGEN)/manifest.tsv $(GEN)/manifest.tsv
	$(call check_returns_native,$(NGEN)/units)
	$(call check_returns_gen,$(GEN)/units)
.PHONY: check-returns
NGEN_UNITS = $(wildcard $(NGEN)/units/*.cpp)
NGEN_OBJ = $(patsubst $(NGEN)/units/%.cpp,$(NOBJ)/%.o,$(NGEN_UNITS))
$(NOBJ)/%.o: $(NGEN)/units/%.cpp | $(NGEN)/manifest.tsv
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) $(NGENFLAGS) -DB3_PS2_FLOAT=1 -MMD -MP -c $< -o $@
-include $(wildcard $(NOBJ)/*.d)
# boot2 (2026-11-07): the native binaries' own sources (platform, backend, host, gameview) as objects of the profile, so
# a link no longer recompiles them all (that was ~180 s of every iteration); same flags as the old inline compile
NSRC_OBJ = $(patsubst %.cpp,$(NOBJ)/obj/%.o,$(NATIVE_SRC) $(HOST_SRC))
NBOOT_OBJ = $(patsubst %.cpp,$(NOBJ)/obj/%.o,tools/native/gameview_native.cpp $(GAMEVIEW_SRC))
$(NOBJ)/obj/%.o: %.cpp | $(NATIVE_GEN)/functions.h $(NGEN)/manifest.tsv
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(NATIVE_FLAGS) $(REQFLAGS) $(CXXFLAGS) $(NGENFLAGS) $(FP_ENV_FLAGS) -DB3_PS2_FLOAT=1 -MMD -MP -c $< -o $@
# rt1: the two units on either side of the VU0 programs' float-environment switch (b3fp::hw, ps2float_hw.h:
# Vu0Unit sets Ps2FloatEnv, the generated programs compute under it) are compiled with -frounding-math -- GCC and clang
# (Apple's too) then treat float operations as depending on the environment, so none moves across the MXCSR / FPCR
# write; not only "the programs are called through a pointer". No LTO is used anywhere (it would need the same flag).
VU0_FP_ENV := -frounding-math
$(NOBJ)/obj/gen/native/vu0/vu0_aot_gen.o: FP_ENV_FLAGS = $(VU0_FP_ENV)
$(NOBJ)/obj/src/native/vu0_unit.o: FP_ENV_FLAGS = $(VU0_FP_ENV) -DB3_VU0_INTERP=$(VU0_INTERP)
$(NOBJ)/obj/src/native/vu0_unit.o: $(VU0_INTERP_STAMP)
# lua6: the Lua unit (the vendored source, compiled as it is: no warnings shown)
$(NOBJ)/obj/src/native/script_lua.o: FP_ENV_FLAGS = -w
-include $(shell find $(NOBJ)/obj -name '*.d' 2>/dev/null)
$(BUILD)/native_boot: tools/native/native_boot.cpp tools/native/gameview_native.cpp $(GAMEVIEW_SRC) include/b3gameview/gameview.h $(NATIVE_SRC) $(wildcard src/native/*.h) $(wildcard tools/native/*.h) $(NATIVE_GEN)/functions.h $(NGEN)/manifest.tsv $(HOST_SRC) $$(NGEN_OBJ) $(GEN_HDRS) include/b3host/native_backend.h $$(NSRC_OBJ) $$(NBOOT_OBJ) $(CHD_OBJ)
	$(CXX) $(CPPFLAGS) $(NATIVE_FLAGS) $(REQFLAGS) $(CXXFLAGS) $(NGENFLAGS) -DB3_PS2_FLOAT=1 $< $(NBOOT_OBJ) $(NSRC_OBJ) $(CHD_OBJ) $(NGEN_OBJ) $(ZLIB_LIBS) -pthread -ldl -o $@
# boot2: two passes, so units new in this generation are in the object list of the link (NGEN_OBJ is a wildcard)
native:
	$(MAKE) $(NGEN)/manifest.tsv
	$(MAKE) $(BUILD)/native_boot
.PHONY: native
# play1: native_play, the native game interactive (tools/native/native_play.cpp): native_boot's backend in real time,
# the game view in b3render's window, the window as the game's pad. Not part of `make native` (needs the renderer).
#   make play     build/native_play + build/libb3render.so (then: build/native_play --game <disc folder>)
# the device-state work (2026-11-10): the shipped native_play walks no DMA chain. HW_DEV=1 (dev builds) compiles in
# native_play's PATH3 check (src/native/hw_model_dev.h Path3Check: the hardware layer's PATH3 rules against a VIF code
# walk of every VIF1 chain, fatal on a violation, counted at the end) and B3NATIVE_VIF1_DUMP; stamped like VU0_INTERP.
HW_DEV ?= 0
HW_DEV_STAMP := $(BUILD)/native/hw_dev.txt
$(shell python3 -c "import os, sys; p, v = sys.argv[1], sys.argv[2]; os.makedirs(os.path.dirname(p), exist_ok=True); (os.path.exists(p) and open(p).read() == v) or open(p, 'w').write(v)" '$(HW_DEV_STAMP)' '$(HW_DEV)')
# nofallback1 (2026-11-14): no retail fallback in the game. NATIVE_FALLBACK=0 (the default; every release build: make,
# package_linux.sh, tools/native/build_win.sh, the Mac's) leaves the R5900 interpreter (tests/r5900_mini.h) and its
# retail-execution path out of native_play entirely (tools/native/native_fallback.h): a function with no native version
# stops the game with a message (native_play's stop path). NATIVE_FALLBACK=1 (developer / diagnostic builds) keeps the
# fallback: the exploration options (--explore-retail / EXPLORE_RETAIL, --explore-movies, --explore-return),
# B3NATIVE_DIFF, and a run that counts what would fall back ("fallback: N functions ran retail in the mini").
# Stamped like HW_DEV. native_boot (the proof tool, never shipped) always has it; the tests that run retail code in the
# mini (test_libm, test_native_platform, ...) are test-only and always do.
NATIVE_FALLBACK ?= 0
NATIVE_FALLBACK_STAMP := $(BUILD)/native/native_fallback.txt
$(shell python3 -c "import os, sys; p, v = sys.argv[1], sys.argv[2]; os.makedirs(os.path.dirname(p), exist_ok=True); (os.path.exists(p) and open(p).read() == v) or open(p, 'w').write(v)" '$(NATIVE_FALLBACK_STAMP)' '$(NATIVE_FALLBACK)')
$(BUILD)/native_play: $(HW_DEV_STAMP) $(NATIVE_FALLBACK_STAMP) tools/native/native_play.cpp $(NATIVE_SRC) $(wildcard src/native/*.h) $(wildcard tools/native/*.h) $(wildcard include/b3tune/*.h) $(NATIVE_GEN)/functions.h $(NGEN)/manifest.tsv $(HOST_SRC) $$(NGEN_OBJ) $(GEN_HDRS) include/b3host/native_backend.h $$(NSRC_OBJ) $$(NBOOT_OBJ) $(CHD_OBJ) $(VERSION_CPP) $(REPORT_DEP)
	$(CXX) $(CPPFLAGS) $(NATIVE_FLAGS) $(REQFLAGS) $(CXXFLAGS) $(NGENFLAGS) -DB3_PS2_FLOAT=1 -DB3_HW_DEV=$(HW_DEV) -DB3_NATIVE_FALLBACK=$(NATIVE_FALLBACK) tools/native/native_play.cpp $(VERSION_CPP) $(NBOOT_OBJ) $(NSRC_OBJ) $(CHD_OBJ) $(NGEN_OBJ) $(ZLIB_LIBS) -pthread -ldl -o $@
play:
	$(MAKE) $(NGEN)/manifest.tsv
	$(MAKE) $(BUILD)/native_play $(BUILD)/libb3render.so
.PHONY: play
# the default goal: the game and its launcher
game:
	$(MAKE) play
	$(MAKE) launcher
.PHONY: game

# vu0: the VU1 / GS check: two runs' B3NATIVE_DMA_DUMP streams through VIF1 + VU1 (b3asset), GS records compared
$(BUILD)/vif1_gs_check: tools/native/vif1_gs_check.cpp src/b3asset/vif.cpp src/b3asset/vu1.cpp include/b3asset/vif.h include/b3asset/vu1.h
	$(CXX) $(CPPFLAGS) $(REQFLAGS) $(CXXFLAGS) -DB3_PS2_FLOAT=1 $< src/b3asset/vif.cpp src/b3asset/vu1.cpp -o $@
# movie2: retail libmpeg in tests/r5900_mini.h on the native IPU model (tools/native/mpeg_play.cpp); run it under
# prlimit (it needs a memory cap):  prlimit --as=6442450944 build/mpeg_play ELF M2V [--max-pictures N]
$(BUILD)/mpeg_play: tools/native/mpeg_play.cpp tools/native/boot_oracle.h tests/r5900_mini.h src/native/hw_model.h src/native/hw_model_dev.h src/native/ipu_model.h src/native/mpeg_dsp.h src/native/ipu_vlc.h $(HOST_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native -Itests -Itools/native $(REQFLAGS) $(CXXFLAGS) $< $(HOST_SRC) -o $@
mpeg_play: $(BUILD)/mpeg_play
# mac5: the native MPEG-2 decoder against ffmpeg, picture by picture (tools/native/mpeg2_check.cpp)
$(BUILD)/mpeg2_check: tools/native/mpeg2_check.cpp src/native/mpeg2_decoder.cpp src/native/mpeg2_decoder.h src/native/mpeg_dsp.h src/native/ipu_vlc.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $< src/native/mpeg2_decoder.cpp -o $@
mpeg2_check: $(BUILD)/mpeg2_check
# macintel1: with REQFLAGS (C++20), as mac3 did for test_clock: Apple clang's default standard has no std::clamp
TESTS += $(BUILD)/test_mpeg2_decoder
$(BUILD)/test_mpeg2_decoder: tests/test_mpeg2_decoder.cpp tests/test.h src/native/mpeg2_decoder.cpp src/native/mpeg2_decoder.h src/native/mpeg_dsp.h src/native/ipu_vlc.h
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -Isrc/native $(REQFLAGS) $(CXXFLAGS) $(GAME_DEFS) $< src/native/mpeg2_decoder.cpp -o $@
.PHONY: mpeg_play

# mac5: LIBM_STAMP (top): the binaries with the retail ELF's path compiled in
$(BUILD)/libm_cost_dump $(BUILD)/test_calls $(BUILD)/test_libm $(BUILD)/test_libm_cost $(BUILD)/test_libm_more \
  $(BUILD)/test_native_platform $(BUILD)/test_overrides $(BUILD)/test_vu_units: $(LIBM_STAMP)
