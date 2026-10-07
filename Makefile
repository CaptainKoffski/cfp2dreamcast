# Cleopatra Fortune Plus — Naomi→DC port. Top-level build.
#
#   make            = make disc: shim → loader (patch table regenerates
#                     automatically from shim.map) → mastered GDI in build/
#   make cdi        = build/cdi/cleopatra.cdi + README.txt: burnable audio/data
#                     MIL-CD for CD-R testers (scripts/make_cdi.py). Rebuilds
#                     shim + loader with the CD cart FAD, masters, then cleans
#                     the objects again so a later `make disc` can't pick them up.
#   make release    = cdi + disc + "build/[GDI] Cleopatra Fortune Plus.zip"
#                     (gdi + 4 tracks, the exact set GDMENUCardManager wants)
#                     + "build/[CDI] Cleopatra Fortune Plus.zip" (cdi + README).
#                     BOTH CONTAIN THE FULL COMMERCIAL ROM — local use only, never
#                     upload/commit (build/ is gitignored for this reason).
#   make deploy     = copy the disc to the SD card entry and dot_clean it.
#                     macOS writes ._* AppleDouble sidecars on FAT volumes and
#                     GDEMU picks the junk ._disc.gdi over the real one — this
#                     exact foot-gun cost the Phase-5 boot bring-up a day.
#                     Override the target with: make deploy CARD=/Volumes/GDEMU/09
#
# Requires: sh-elf toolchain at /opt/toolchains/dc, KOS at tools/kos
# (docs/kb/tooling.md), the ROM at repo root, 7zz (donor extraction);
# cdi also cdi4dc, mkdcdisc, mkisofs (tooling.md §CDI mastering).

CARD    = /Volumes/GDEMU/09
DISC    = build/disc.gdi build/track01.iso build/track02.raw \
          build/track03.iso build/track04.iso
ZIP     = build/[GDI] Cleopatra Fortune Plus.zip
CDI_ZIP = build/[CDI] Cleopatra Fortune Plus.zip
# CD cart FAD = 150 + 11702 (session-2 data track) + 1792 (IP+FS+loader
# region) -- keep in sync with scripts/make_cdi.py CART_FAD_CD (it checks the
# mark SHIM_CDXA bakes into the shim).
CD_CART_FAD = 13644
CDI_DEFS = -DCART_FAD=$(CD_CART_FAD) -DSHIM_CDXA=1

.PHONY: disc cdi release deploy test test-vmu test-vmu-play clean

disc:
	$(MAKE) -C shims
	. tools/kos/environ.sh && $(MAKE) -C loader
	python3 scripts/make_gdi.py

# CART_FAD is a compile-line knob make's freshness check can't see, hence the
# clean on both sides (make_gdi.py/make_cdi.py check the baked marks too).
cdi:
	$(MAKE) clean
	$(MAKE) -C shims CDI_DEFS='$(CDI_DEFS)'
	. tools/kos/environ.sh && $(MAKE) -C loader CDI_DEFS='$(CDI_DEFS)'
	python3 scripts/make_cdi.py
	$(MAKE) clean

# Sequenced in the recipe (not as prerequisites) so -j can't interleave the
# two FAD builds.
release:
	$(MAKE) cdi
	$(MAKE) disc
	rm -f "$(ZIP)" "$(CDI_ZIP)"
	cd build && zip -j "../$(ZIP)" disc.gdi track01.iso track02.raw \
	  track03.iso track04.iso
	cd build/cdi && zip -j "../../$(CDI_ZIP)" cleopatra.cdi README.txt
	@echo "NOTE: both archives embed the commercial ROM — do not upload."

deploy: disc
	test -d "$(CARD)"   # card mounted?
	cp $(DISC) "$(CARD)/"
	dot_clean -m "$(CARD)"
	@ls "$(CARD)" | grep '^\._' && { echo "AppleDouble junk survived!"; exit 1; } || true
	@echo "deployed to $(CARD)"

test:
	$(MAKE) -C shims test
	python3 scripts/test_maple_literals.py

# VMU-safety canary runs (spec: docs/superpowers/specs/2026-07-26-vmu-safety-design.md):
# test-vmu = unattended 90 s attract; test-vmu-play = headed, tester plays then quits.
test-vmu:
	scripts/test_vmu_untouched.sh attract

test-vmu-play:
	scripts/test_vmu_untouched.sh play

clean:
	$(MAKE) -C shims clean
	. tools/kos/environ.sh && $(MAKE) -C loader clean   # loader/Makefile includes KOS rules even for clean
