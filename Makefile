# Cleopatra Fortune Plus — Naomi→DC port. Top-level build.
#
#   make            = make disc: shim → loader (patch table regenerates
#                     automatically from shim.map) → mastered GDI in build/
#   make release    = disc + "build/[GDI] Cleopatra Fortune Plus.zip"
#                     (gdi + 4 tracks, the exact set GDMENUCardManager wants).
#                     CONTAINS THE FULL COMMERCIAL ROM — local use only, never
#                     upload/commit (build/ is gitignored for this reason).
#   make deploy     = copy the disc to the SD card entry and dot_clean it.
#                     macOS writes ._* AppleDouble sidecars on FAT volumes and
#                     GDEMU picks the junk ._disc.gdi over the real one — this
#                     exact foot-gun cost the Phase-5 boot bring-up a day.
#                     Override the target with: make deploy CARD=/Volumes/GDEMU/11
#
# Requires: sh-elf toolchain at /opt/toolchains/dc, KOS at tools/kos
# (docs/kb/tooling.md), the ROM at repo root, 7zz (donor extraction).

CARD    = /Volumes/GDEMU/11
DISC    = build/disc.gdi build/track01.iso build/track02.raw \
          build/track03.iso build/track04.iso
ZIP     = build/[GDI] Cleopatra Fortune Plus.zip

.PHONY: disc release deploy test clean

disc:
	$(MAKE) -C shims
	. tools/kos/environ.sh && $(MAKE) -C loader
	python3 scripts/make_gdi.py

release: disc
	rm -f "$(ZIP)"
	cd build && zip -j "../$(ZIP)" disc.gdi track01.iso track02.raw \
	  track03.iso track04.iso
	@echo "NOTE: archive embeds the commercial ROM — do not upload."

deploy: disc
	test -d "$(CARD)"   # card mounted?
	cp $(DISC) "$(CARD)/"
	dot_clean -m "$(CARD)"
	@ls "$(CARD)" | grep '^\._' && { echo "AppleDouble junk survived!"; exit 1; } || true
	@echo "deployed to $(CARD)"

test:
	$(MAKE) -C shims test

clean:
	$(MAKE) -C shims clean
	$(MAKE) -C loader clean
