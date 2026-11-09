# compiler-rt builtins for XenevaOS (AArch64 PE/COFF).
#
# Vendored from compiler-rt 21.1.8 (Apache-2.0 WITH LLVM-exception; see
# LICENSE.TXT).  Produces the archive clang expects under the name
# libclang_rt.builtins-aarch64.a:
#
#   * GENERIC_SOURCES and GENERIC_TF_SOURCES from lib/builtins/CMakeLists.txt
#   * aarch64/fp_mode.c
#   * the LSE outline-atomic helpers generated from aarch64/lse.S
#
# The TF sources compile to empty objects here: this target has 64-bit
# long double and no __float128, so int_types.h never defines CRT_HAS_TF_MODE.
# They are kept because they are part of upstream's aarch64 source list and
# would become live if the target ever gains IEEE binary128.
#
# Deliberately not built:
#   * cpu_model/aarch64.c -- needs the MSVC SDK (windows.h); this target does
#                            not use IFUNC-based LSE dispatch.  A local
#                            aarch64_have_lse_atomics.c supplies the runtime
#                            flag with the portable zero (LDXR/STXR) default.
#   * aarch64/emupac.cpp  -- the SipHash headers are absent from the release
#                            tarball, and PAC emulation is unused here.
#   * enable_execute_stack.c, emutls.c, eprintf.c -- OS hooks, not part of
#                            the arithmetic set.
#   * clear_cache.c -- its Windows path calls FlushInstructionCache, while
#                      XenevaOS keeps SCTLR_EL1.UCI clear.  EL0 cannot run
#                      DC CVAU/IC IVAU until a cache-flush syscall exists.

TOOLCHAIN ?= llvm

ifeq ($(TOOLCHAIN),llvm)
CC = clang
AR = llvm-ar
else
$(error compiler-rt builtins require clang targeting aarch64-unknown-windows)
endif

TARGET = libclang_rt.builtins-aarch64.a
ROOT = lib/builtins
OBJDIR = obj

# Keep the Windows target macros: int_lib.h needs _WIN32 to select its COFF
# alias path.  -ffreestanding makes int_util.c use __builtin_trap() instead of
# abort(), so the library still has no libc dependency.
COMMON = -target aarch64-unknown-windows -ffreestanding -fno-builtin \
         -fno-stack-protector -fno-stack-check -fno-strict-aliasing -O2 \
         -Iinclude -I$(ROOT) -I$(ROOT)/aarch64

GENERIC_SRCS = \
  absvdi2.c absvsi2.c absvti2.c adddf3.c addsf3.c addvdi3.c addvsi3.c \
  addvti3.c apple_versioning.c ashldi3.c ashlti3.c ashrdi3.c ashrti3.c \
  bswapdi2.c bswapsi2.c clzdi2.c clzsi2.c clzti2.c cmpdi2.c cmpti2.c \
  comparedf2.c comparesf2.c ctzdi2.c ctzsi2.c ctzti2.c divdc3.c divdf3.c \
  divdi3.c divmoddi4.c divmodsi4.c divmodti4.c divsc3.c divsf3.c divsi3.c \
  divti3.c extendsfdf2.c extendhfsf2.c extendhfdf2.c ffsdi2.c ffssi2.c \
  ffsti2.c fixdfdi.c fixdfsi.c fixdfti.c fixsfdi.c fixsfsi.c fixsfti.c \
  fixunsdfdi.c fixunsdfsi.c fixunsdfti.c fixunssfdi.c fixunssfsi.c \
  fixunssfti.c floatdidf.c floatdisf.c floatsidf.c floatsisf.c floattidf.c \
  floattisf.c floatundidf.c floatundisf.c floatunsidf.c floatunsisf.c \
  floatuntidf.c floatuntisf.c int_util.c lshrdi3.c lshrti3.c moddi3.c \
  modsi3.c modti3.c muldc3.c muldf3.c muldi3.c mulodi4.c mulosi4.c muloti4.c \
  mulsc3.c mulsf3.c multi3.c mulvdi3.c mulvsi3.c mulvti3.c negdf2.c negdi2.c \
  negsf2.c negti2.c negvdi2.c negvsi2.c negvti2.c os_version_check.c \
  paritydi2.c paritysi2.c parityti2.c popcountdi2.c popcountsi2.c \
  popcountti2.c powidf2.c powisf2.c subdf3.c subsf3.c subvdi3.c subvsi3.c \
  subvti3.c trampoline_setup.c truncdfhf2.c truncdfsf2.c truncsfhf2.c \
  ucmpdi2.c ucmpti2.c udivdi3.c udivmoddi4.c udivmodsi4.c udivmodti4.c \
  udivsi3.c udivti3.c umoddi3.c umodsi3.c umodti3.c
TF_SRCS = \
  addtf3.c comparetf2.c divtc3.c divtf3.c extenddftf2.c extendhftf2.c \
  extendsftf2.c fixtfdi.c fixtfsi.c fixtfti.c fixunstfdi.c fixunstfsi.c \
  fixunstfti.c floatditf.c floatsitf.c floattitf.c floatunditf.c \
  floatunsitf.c floatuntitf.c multc3.c multf3.c powitf2.c subtf3.c \
  trunctfdf2.c trunctfhf2.c trunctfsf2.c

C_SRCS = $(addprefix $(ROOT)/,$(GENERIC_SRCS) $(TF_SRCS)) \
         $(ROOT)/aarch64/fp_mode.c
OBJS = $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(C_SRCS)) \
       $(OBJDIR)/aarch64_have_lse_atomics.o

all: $(TARGET)

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -c $< -o $@

# Outline atomics read this byte to choose between LSE instructions and the
# LDXR/STXR loop.  cpu_model/aarch64.c normally provides it, but that needs
# the MSVC SDK; the flag stays zero here, which selects the portable fallback.
$(OBJDIR)/aarch64_have_lse_atomics.o: aarch64_have_lse_atomics.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -c $< -o $@

# One recipe builds all 125 outline-atomic variants (cas covers 16 bytes;
# the other operations stop at 8).  They live behind a stamp file so the
# archive can glob the results at link time.
$(OBJDIR)/oa/.stamp: $(ROOT)/aarch64/lse.S
	@mkdir -p $(OBJDIR)/oa
	@set -e; for pat in cas swp ldadd ldclr ldeor ldset; do \
	  for size in 1 2 4 8 16; do \
	    if [ $$pat != cas ] && [ $$size = 16 ]; then continue; fi; \
	    for model in 1 2 3 4 5; do \
	      $(CC) $(COMMON) -DL_$$pat -DSIZE=$$size -DMODEL=$$model \
	        -c $(ROOT)/aarch64/lse.S -o $(OBJDIR)/oa/$$pat$$size-$$model.o; \
	    done; \
	  done; \
	done
	@touch $@

$(TARGET): $(OBJS) $(OBJDIR)/oa/.stamp
	$(AR) rcs $@ $(OBJS) $(OBJDIR)/oa/*.o

clean:
	rm -rf $(OBJDIR) $(TARGET)
