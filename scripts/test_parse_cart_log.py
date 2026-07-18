import subprocess, sys, textwrap, os
sys.path.insert(0, os.path.dirname(__file__))
import parse_cart_log as p

SAMPLE = textwrap.dedent("""\
    CARTDMA src=00000000 dest=0c020000 len=100000
    JVSREPORT buttons=ffff
    CARTDMA src=00200000 dest=0c400000 len=40000
    CARTDMA src=00200000 dest=0c400000 len=40000
    CARTPIO offset=00000450
    WATERMARK region=main used=a12340 size=2000000
    WATERMARK region=main used=b00000 size=2000000
    SERIALPOKE addr=5f7018 data=00000001
    JVSREPORT buttons=fdff
""")

def test_parse_dedups_and_sorts_dma():
    r = p.parse_text(SAMPLE)
    assert [d["src"] for d in r["dma"]] == [0x0, 0x200000]   # deduped, sorted
    assert r["dma"][1]["len"] == 0x40000

def test_pio_and_serial_and_watermark():
    r = p.parse_text(SAMPLE)
    assert r["pio"] == [0x450]
    assert r["serial"] == [{"addr": 0x5f7018, "data": 0x1}]
    assert r["watermarks"]["main"] == 0xb00000   # max over the two lines

def test_checks_pass_on_valid_sample():
    r = p.parse_text(SAMPLE)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["dest_in_ram"] is True
    assert names["len_aligned_32"] is True
    assert names["beyond_boot_read"] is True   # src=0x200000 >= 0x100000

def test_check_flags_misaligned_len():
    bad = "CARTDMA src=00000000 dest=0c020000 len=100001\n"   # not a multiple of 0x20
    r = p.parse_text(bad)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["len_aligned_32"] is False

def test_check_flags_dest_out_of_ram():
    bad = "CARTDMA src=00000000 dest=00000000 len=20\n"       # dest not in main RAM
    r = p.parse_text(bad)
    names = {name: ok for name, ok, _ in r["checks"]}
    assert names["dest_in_ram"] is False

def test_cli_writes_csv(tmp_path):
    log = tmp_path / "in.log"; log.write_text(SAMPLE)
    out = tmp_path / "out.csv"
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "parse_cart_log.py"),
                    str(log), "--csv", str(out)], check=True)
    rows = out.read_text().strip().splitlines()
    assert rows[0] == "cart_offset,length,dest,mode"
    assert "0x00000000,0x100000,0x0c020000,DMA" in rows
    assert any(r.endswith(",PIO") for r in rows)
