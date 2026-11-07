# MCA on ESP32-S3 + AD9226

[🇷🇺 Русский](README.md) · **🇬🇧 English** ·
[project site](https://qsxft446.github.io/esp32-Multi-Channel-Analyzer/en.html)

A multichannel pulse-height analyzer (gamma spectrometer) built on an
ESP32-S3-N16R8 and a 12-bit AD9226 ADC board. The ADC stream is captured by
hardware (LCD_CAM + GDMA), the second core does the signal processing, and
control and plots are served as a web page over the network.

**This copy targets the Waveshare ESP32-S3-ETH board** (ESP32-S3R8,
on-board W5500). The pinout in “Wiring” is for it. The device works
**over Ethernet only**, powered over PoE: there is no WiFi in this
firmware, and the ADC clock comes from the camera block (CAM_CLK, see
“ADC frequencies”).

ESP-IDF 5.4.4.

> The web interface is in English or Russian: the RU / EN switch is in the
> header, next to the ADC frequency. Firmware console messages are in
> Russian; where this document quotes one, a translation is given in brackets.

![Spectrum](docs/spectrum.png)

*Spectrum: 2 min 11 s of acquisition, 6705 cps, dead time 1.1 %, CLK 16 MHz,
4096 channels. Selected peak: centroid at channel 665, FWHM 45.7 channels,
resolution 6.88 %.*

![Prototype](docs/hardware.jpg)

*Prototype: AD9226 board and an ESP32-S3-N16R8 module board, data and CLK
wired with jumper wires.*

## Build and flash

```
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

### Updating over the network

Firmware with the two-slot partition table (`partitions.csv`) accepts a new
version over the network. **The first time — over USB**: the previous table
had a single slot. Settings survive this — the `nvs`
partition stayed where it was.

After that, no cable is needed, by any of these:

- the “Settings” page, “Firmware” section: choose `build/esp32_mca.bin`,
  “Upload and restart”;
- `python tools/ota.py <ip>` — sends `build/esp32_mca.bin`, waits for the
  restart and checks that the new slot is running;
- `curl --data-binary @build/esp32_mca.bin http://<ip>/ota`.

Before writing, the device checks that the file is an ESP32-S3 image of this
same program (a bootloader, a partition table or unrelated files are
rejected), and after writing it checks the checksum of the whole image. The
write goes to the free slot; the running one is not touched.

**Rollback.** The new firmware counts as good after the first request to
`/stat` (an open page sends it) or `/ota`. If the device restarts before
that — crashes, hangs, never gets on the network — the bootloader brings
back the previous firmware by itself, and the console shows
`прошивка в слоте … не подтвердилась и была заменена прежней (откат)`
(firmware in slot … was not confirmed and was replaced by the previous one).

`GET /ota` returns the version, slot, state and the reason of the last reset
(`reset`: `poweron`, `usb`, `sw`, `panic`, `task_wdt`, `brownout` …) with the
uptime `up`: this shows over the network whether the device restarted by
itself.

**There is no password protection**: anyone who sees the device on the
network can flash it — just as they can change the other settings. Keep the
device on your own network.

**Network.** Connect the board with a cable to a network with DHCP (power
over PoE or from USB). The router assigns the address, and it is printed
in the console: `Ethernet подключён: http://<ip>/` (Ethernet connected).
There is no WiFi in this firmware, so without a DHCP address the device is
not reachable over the network — look at the address and errors in the
console over USB.

## Wiring

AD9226 board (connector P2) → Waveshare ESP32-S3-ETH. “Pin” is the header
number from the Waveshare pinout: 1–20 is the row starting at GPIO20,
21–40 the row starting at VBUS.

| ESP32-S3 | Pin | Signal | P2 |
|---|---|---|---|
| GPIO38 | 14 | DATA0 | 5 |
| GPIO39 | 12 | DATA1 | 8 |
| GPIO40 | 11 | DATA2 | 7 |
| GPIO41 | 10 | DATA3 | 10 |
| GPIO42 | 9 | DATA4 | 9 |
| GPIO47 | 5 | DATA5 | 12 |
| GPIO48 | 4 | DATA6 | 11 |
| GPIO1 | 25 | DATA7 | 14 |
| GPIO2 | 26 | DATA8 | 13 |
| GPIO15 | 29 | DATA9 | 16 |
| GPIO17 | 34 | DATA10 | 15 |
| GPIO21 | 35 | DATA11 | 18 |
| GPIO3 | 27 | DATA12/OTR | 17 |
| GPIO16 | 32 | **CLK (output)** | 6 |

+5 V to pins 1–2 (from the ESP32-S3-ETH: VBUS, pin 40, when powered from
USB), ground to 3/4/19/20, plus a separate short, thick ground wire
between the boards (board GND: pins 3, 8, 13, 18, 23, 28, 33, 38).

**The data lines run along both header rows:** this board has no run of
consecutive free pins. GPIO4–7 are taken by the microSD slot, 9–14 by the
W5500, 33–37 by the chip's Octal PSRAM, 19–20 by USB, 43–44 by UART0 (the
console), 0/45/46 are boot pins. OTR is on GPIO3: this boot pin selects
the JTAG source only when an eFuse is burned, otherwise its level at start
does not matter. LCD_CAM itself does not care about the line order, and the
data-line check in diagnostics assembles the word from the `MCA_DATA_PINS`
list.

**No CLK → PCLK jumper is needed.** The capture is clocked from the same
GPIO16 pin that outputs CLK: the pin's input is enabled as well and routed
through the GPIO matrix to the camera interface PCLK. This is the same
point the jumper used to come to — right at the pin, before the resistor.
To bring the jumper back, set a separate `PIN_CAM_PCLK_IN` in
`main/mca_config.h`, e.g. 18 (pin 31, next to GPIO16).

### How to route CLK

```
GPIO16 ──[ 47 Ω ]──── CLK wire ──────── CLK  (AD9226)
                      GND wire ──────── GND  (AD9226)
                      (twisted together)
```

- A single CLK wire goes to the ADC. Two long wires to one CLK point on the
  ADC board form a loop — a ready-made antenna.
- A 33–68 Ω series resistor **right at the GPIO16 pin**: PCLK is read from
  the pin, i.e. before the resistor. If PCLK were taken after the resistor
  (as with a jumper in the wrong place), the edge gets a step and the
  capture may trigger twice per clock.
- Twist the CLK wire with a ground wire; keep everything short and away
  from the module antenna (the end of the module without the metal shield).

This is about a clean clock at the ADC. On boards with WiFi the CLK
wiring also matters for the link — see “CLK interference with WiFi”.

## Ethernet (WIZnet W5500)

On the ESP32-S3-ETH the W5500 chip is soldered on the board, nothing needs
to be connected. It is **the only way to reach the device**: there is no WiFi
in this firmware (no access point, no client — the radio is never turned on),
power comes over PoE.

| W5500 | ESP32-S3 |
|---|---|
| SCLK | GPIO13 |
| MOSI | GPIO11 |
| MISO | GPIO12 |
| SCS (CS) | GPIO14 |
| INT | GPIO10 |
| RST | GPIO9 |

Pins follow the Waveshare schematic and are changed in `main/mca_config.h`
(`PIN_ETH_*`). The SPI bus runs at 20 MHz. The address comes from DHCP and is printed in the
console: `Ethernet подключён: http://<ip>/` (Ethernet connected), and shown
on the display, if one is connected.

On the “Settings” page (`/settings`, the old `/net` and `/wifi` addresses work too) the
“Network” (address settings) and “Firmware” (update over the network) cards sit side by side. If the W5500 did
not answer at start, the console shows `W5500 не отвечает - сети нет`
(W5500 does not respond - no network).

**Static address.** In the “Network” section of the “Settings” page —
“address from the router (DHCP)” or “static address” with address, mask,
gateway and DNS fields (an empty DNS equals the gateway). The device checks
the mask and that the gateway is in the same subnet. So that a mistake does
not lose the device:

- a new static address is applied at once but written to memory only after
  confirmation: the page opens on the new address by itself and confirms it.
  If there is no confirmation within 60 s (the address is not on your
  network, wrong gateway), the device returns to the previous settings; a
  restart within those 60 s brings them back too;
- the **BOOT** button on the board, held for 5 s while running (not at power
  on), switches back to DHCP;
- the current address is shown on the display and in the console.

## SSD1306 128x64 display (optional)

A small I2C OLED display shows:

- at the top — the device IP address (or `NO CABLE`, `WAITING FOR DHCP`,
  `NO ETHERNET`);
- on the next line — `RUN` (inverted, spectrum acquisition running) with
  the current CPS, `STOP` or `SCOPE` (oscilloscope open), and the ADC rate
  in MHz on the right;
- at the bottom — a CPS plot by acquisition seconds over the last 128 s,
  newest on the right; the scale runs from the minimum to the maximum on
  screen, both labelled. While acquisition is stopped, the plot does not
  move.

The layout also suits a two-colour display (the top 16 rows yellow, the
rest blue): both text lines lie entirely in the yellow band, the plot in the
blue one. On a single-colour display it looks the same.

| Display | ESP32-S3-ETH | Pin |
|---|---|---|
| SDA | GPIO18 | 31 |
| SCL | GPIO46 | 6 |
| VCC | 3V3 | 36 |
| GND | GND | 38 |

There are no other free pins on the header. GPIO46 is a boot pin, but at a
normal start its level does not matter; the pull-up on the display module
can only get in the way of entering the bootloader with the BOOT button —
then disconnect the display (it does not affect flashing over the USB cable
or over the network). GPIO45 does not fit: a pull-up at start would switch
the flash supply voltage.

The device finds address 0x3C or 0x3D by itself; the display can be
connected on the fly — it is looked for every 10 s. Pins, bus speed
(400 kHz) and 180° rotation (`MCA_OLED_FLIP`) are in `main/mca_config.h`.
The frame is drawn even without a display: `http://<ip>/oled.pbm` is what
is on the screen, as a picture. If the picture is shifted by a couple of
pixels with garbage at the edge, it is a 1.3" display with an SH1106
controller, not an SSD1306.

## MCA emulation over the network (shproto protocol)

The device answers over the network as an MCA speaking the binary
**shproto** protocol — PC programs that work with such analyzers over the
network (for example [BecqMoni](https://github.com/Am6er/BecqMoni)) see it
as a regular analyzer. It is the same stream that used to go over the COM
port, only through a TCP socket; the console is now free.

- **Where it is set** — “Settings” → “PC software link” (the panel above
  “Firmware”): the “MCA emulation over the network” checkbox and the TCP
  port. By default emulation is on, port 8234. Turning it on and off takes
  effect at once and is saved.
- **Port** — 1024 to 65535 (lower ports are system ones, 80 is taken by the
  device page). A change is saved in the device memory and restarts the
  server at once: a connected program is disconnected and has to be given
  the new port. Change it if 8234 is taken or blocked in your network.
- **State** is shown in the same place and refreshed every 3 s: “listening
  on port N”, “program connected from <address>” or “port not open” (taken,
  or the server is restarting — it retries every 2 s).
- **Connecting from BecqMoni** — the “AtomSpectra TCP” device (that is its
  name in the program list). The device is found by search: it announces
  itself on the network (mDNS, service `_atomspectra._tcp`, name
  `MCA AD9226 xxxx`, where xxxx is the end of the serial number). Or enter
  the device address and port by hand. There is one connection: a new one
  replaces the previous one (after a dropped link the program reconnects,
  while the old socket may still look alive).
- **Stream.** While a program is connected, a status packet and the whole
  spectrum go every second — even when acquisition is stopped: BecqMoni
  listens to the stream before sending commands and decides from the
  acquisition time in the status whether acquisition is running.
- **Commands:** `-inf`, `-cal` (40 registers: 0–10 are the device energy
  calibration, see below; 11–38 are stored in the device memory; 39 is the
  serial number derived from the MAC), `-sta [sec] [-r] [-s]`, `-sto`, `-sho`, `-stt`,
  `-rst`, `-mode 0`, `-spd` (no speed over the network — only the answer),
  `-frq` (nearest higher frequency from the table), `-reboot`. Every
  executed command is answered with the text `-ok` — exactly what BecqMoni
  waits for when starting and stopping acquisition. Commands are parsed in
  the middle of a spectrum upload as well, so the answer does not wait for
  the end of the pass. Factory tuning commands (`-ris`, `-fall`, `-U`, `-V`,
  `-nos`, `-pileup`…) are not executed and are answered with
  `-err not supported`: processing parameters are set on the device page.
- **During acquisition**, every second — a status packet (acquisition time,
  processing load, CPS, rejected pulses — pile-ups and off-scale events), then the
  whole spectrum: 8192 channels in packets of 64. `-sta` starts spectrum
  acquisition on the device itself, `-sto` stops it.
- **Dead time.** Programs compute it as
  `(spectrum sum + rejected) · (RISE + FALL + 1) / F`. After every detected
  pulse the device is blind for the peak search and the re-arm, so in the
  `-inf` answer `RISE` = “Peak search”, and `FALL` is chosen
  so that `RISE + FALL + 1` is the average number of dead-time samples per
  pulse (measured; while there are few pulses — “Re-arm”). The
  program’s live time then matches the device. In BecqMoni the dead time is
  read only by the dead-time button in the device settings: acquire a few
  seconds with a source first, then press it; press it again after
  changing the ADC frequency or processing parameters.
- **The energy calibration is shared with BecqMoni**, as with the original
  device: registers 0–9 of the `-cal` command hold five coefficients of the
  “channel → keV” polynomial (each a 64-bit number in two registers, high
  half first; an empty one is `FFFFFFFF FFFFFFFF`), register 10 holds the
  CRC32 of lines 0–9. The format was checked against the BecqMoni source
  (`AtomSpectraTcpCalibrationReader`).
  - **Reading:** the registers are built from the device calibration from
    the page (the calibration button on “Spectrum”) if it was made with the
    current processing settings; otherwise they are empty and BecqMoni
    reports that there is no calibration. BecqMoni reads it when
    acquisition starts and with the calibration read button in the device
    settings.
  - **Writing:** `-cal N hhhhhhhh` for registers 0–9, then register 10 — on
    it the coefficients become the device calibration (shown on the page as
    “BecqMoni”, used for the dose). A calibration with a gap in the
    coefficients or with fewer than two is rejected with `-err invalid
    calibration`, and the previous one is kept. What was written is read
    back bit for bit. BecqMoni itself does not offer writing the calibration
    over the network (only over a COM port), so over the network it is done
    with commands from its console.
  - **Channels:** over the network BecqMoni treats the polynomial as over
    8192 channels and takes the calibration only with 8192 channels in its
    settings. With “Channels” below 8192 the device converts the polynomial
    both ways: its channel x is stretched over mul = 8192/N channels, the
    middle being channel k = mul·x + (mul − 1)/2. The simplest is to keep
    “Channels” at 8192 — then the coefficients pass as they are.
- **Channels — as set in the device “Channels” setting
  (2048/4096/8192).** The protocol and programs always hold 8192 channels:
  BecqMoni keeps the spectrum in an 8192-element array and itself sums
  `8192/N` adjacent channels to get the N channels from its settings.
  Therefore, when N is below 8192, the device stretches each of its
  channels over `8192/N` adjacent ones, splitting the count without
  remainder — and BecqMoni with **the same N in its settings** sums them
  back into exactly the device channels. Set the same number of channels in
  BecqMoni as on the device: on a mismatch the spectrum is stretched or
  compressed. Events above the scale (channels N to 8191) go to “rejected”,
  so that the “total pulses” and the dead time in the program stay correct.

PC check: `python tools/emu_check.py <device address> [port]` (default
port 8234) — connects the way BecqMoni does, sends commands and parses
the answers according to the rules of the reference protocol (CRC,
escaping, `-inf`/`-cal` format, order of status and spectrum) and of
BecqMoni (answers to commands), and the calibration in `-cal` by the
BecqMoni rules against the device calibration. With `--cal-write` it also
writes a test calibration the way BecqMoni does, checks it and **restores
the previous device calibration**. The check stops and starts acquisition
and returns it to the previous state at the end; it does not reset the
spectrum.

## Web interface

- **Language** — English or Russian, RU / EN buttons in the header next to
  the ADC frequency. The choice is remembered in the browser; without a
  choice the browser language is used (Russian for `ru`, otherwise English).
- **Tab icon** — `/favicon.ico` (`main/favicon.ico`, embedded in the
  firmware): the same atom as in the header, 16/32/48 px, at 16 and 32 px
  the lines are thickened, or the orbits vanish.
- **Spectrum** — acquisition, lin/log scale, “channels / keV” X axis,
  analysis of the selected peak (centroid, FWHM, resolution). A peak is
  selected with the mouse right on the plot: a click on a peak — the range
  around it (the maximum within ±4 % of the click point, a fit, centroid
  ±1.5 FWHM; checked on 7 Ra-226 spectra — 140 clicks from −4 to +4 % off
  the line, all hit it), a drag — your own range, a double click — reset;
  the result is labelled above the peak, the channel, energy and count are
  shown under the cursor. File export: XML
  (ResultDataFile, opens in BecqMoni), CSV (channel, count), N42
  (ANSI N42.42), SPE (SpectraLine).
- **Energy calibration** — a block under the spectrum plot. Choose the
  isotope (Ra-226, Cs-137, Cs-137 + K-40, Th-232, Co-60) and “Calibrate”: the
  page searches for the isotope lines (trying pairs “two lines — two
  peaks”: for the right straight line the other lines land on peaks within
  ~1 keV, for a false one the deviations are several times larger; the
  scale zero no further than ±80 keV), fits each peak (centroid — a
  parabola over the log of the top, FWHM — at half maximum above a
  straight background at ±3σ) and builds the scale E = c0 + c1·k
  (+ c2·k² with four or more lines). “Save to settings” writes the
  calibration to the device; until then it is only on the page and marked
  “not saved”. “Show calibration lines” marks the isotope lines on the
  plot: the ones used for the calibration — bright and labelled, the other
  known lines of the same isotope — faint. Under “Calibration” too are the scale,
  the table of lines and the c0–c4 fields: the coefficients can be entered
  by hand (E = c0 + c1·k + … + c4·k⁴, keV; the scale must grow with the
  channel number over the whole spectrum) with “Apply”, then “Save to
  settings”. Calibration by points (up to 4) for any isotope is there too:
  with “Calibration” open, a click on a peak in the spectrum (or a drag
  over it) fits the peak and puts its centroid into the row chosen with its
  number button or into the first free one; you enter the line energy and
  press “Build from points” (1 point — a line through zero, 2–3 — a line,
  4 — a parabola; the energies must grow with the channel number). The
  browser remembers the points, the plot marks them with numbers. The
  calibration goes into files with the degree it has (XML,
  N42, SPE — up to the 4th). “Cs-137 + K-40” looks only for 662 and
  1461 keV, both required: with the barium X-ray the pair “32 + 1461” fitted
  the spectrum as neatly as the right “32 + 662”. “Calibrate” does not take a spectrum below 50 thousand events: on a sparse
  spectrum (right after “Reset”) false matches collect as many lines; of up
  to eight best matches the one with consistent widths is taken (FWHM grows
  roughly as the square root of energy) — on spectra thinned to 100
  thousand events the lines are found correctly, on smaller ones the search
  refuses rather than errs. The device stores the
  calibration (`/calib`, NVS) together with a signature of the settings:
  if the method, windows, L, G, “codes per channel”, polarity or rate
  change, the block says the calibration must be made again, and the keV
  scale is not applied. With a calibration the peak meter shows the
  energy, FWHM in keV and the **resolution from the line energy**: the
  percentage “width / position in channels” depends on the scale zero (from
  +30 to −24 keV for different methods) and comparing methods by it is
  unfair. The X axis switches to keV, XML, N42 and SPE files are exported
  with the calibration (as BecqMoni writes it: a polynomial from the lowest
  power), CSV — with an energy column. Checked (2026-11-06): on 14 Ra-226
  spectra and 4 Cs-137 ones the lines are found correctly, deviations are
  at most 1 keV, FWHM agrees with an independent “Gaussian + straight
  line” fit within 1–3 %; on the device — 4 Ra-226 lines, 609 keV:
  46.5 keV (7.63 %). Limitation: for lines with close neighbours (352 next
  to 295, 1120 next to 1238 keV) the edge of the background window touches
  the neighbouring peak, and the FWHM in the calibration table is
  underestimated by 5–11 % (synthetic spectrum); for 609 and 662 keV it is
  exact, ±1 %.
- **Settings** — a separate page (link at the top): “Network” (address:
  DHCP or static), “Firmware” (version and update over the network) and
  “ADC settings” sections. The “Spectrum” and “Oscilloscope”
  tabs no longer carry settings or diagnostics — the oscilloscope fetches the
  windows, threshold and polarity it needs from the device by itself every
  5 s.
- **ADC settings.** The top of the table holds the sample rate, threshold,
  polarity and spectrum scale; the rest (trapezoid windows, measurement
  method, pile-up rejection, MCA emulation) is folded into “Advanced”, and
  the browser remembers whether it is open. The windows L, G, search and
  re-arm are in samples. They can be left alone: 5-minute acquisitions at
  16 MHz with L = 14, 20 and 28 samples gave the same 6.9–7.0 % resolution on
  the ~358 peak — on this board the trapezoid noise is much smaller than the
  peak width, and the resolution is set by the detector.
- **Pulse shape filter** — a card on the “Settings” page, enabled on
  demand and off by default: with a good PMT and its own power supply it is
  not needed. The shape measure is the effective pulse width: area / height
  in the “before / after peak” window (the same two numbers as for
  integration; with the filter on they are not dimmed in the table).
  Scintillations have almost the same width at any energy, PMT noise bursts
  are narrower. “Learn reference” — with a running acquisition (background
  is enough, a source is faster)
  the device takes 2000 pulses above three thresholds, the median of their
  width becomes the reference and is saved in the device memory. The filter
  rejects pulses whose width is farther from the reference than the
  “tolerance” (25 % by default). The reference is valid only for the window
  and ADC rate it was learned at: if they change, the filter passes
  everything and the card asks to learn the reference again. Below the
  buttons there are two plots. “Reference pulse” — the mean shape of the
  learned pulses (saved in the device memory together with the reference),
  with the last pulse within the tolerance (green) and the last one outside
  it (red) on top, each scaled to its height, with height and width; the
  light band is the measurement window. “Pulse width” — a histogram of
  area / height: green within the tolerance, red outside, the band is
  reference ± tolerance, the share outside the tolerance below. The card
  shows how many pulses were checked and rejected since “Reset” (`/stat`:
  `shchk`, `shrej`). At 20 MHz and ~5000 cps the filter works without chunk
  losses, but the core headroom is small (measurements — “Findings from
  debugging”, “Pulse shape filter”).
- **CPS monitor** — on the same tab, below the spectrum: current CPS, mean
  over the whole history with its error, last interval, a plot by averaging
  intervals with a moving average, an interval table and CSV export. The
  plot shows a time window — the last 30 s by default (“window” selector:
  30 s … 6 h) — and follows new data; wheel — zoom, drag — move into the
  past (the window then stays put), double click — back to the window and
  now. It used to show the whole history by default and got more and more
  compressed as the history grew. The history is a
  6-hour ring at one sample per second in the device PSRAM (~216 KB,
  `main/mca_hist.c`): it accumulates while the spectrum is acquired, even
  with the page closed; the page only fetches new samples. The CPS of an
  interval is the counts divided by the time acquisition actually ran
  (without lost chunks). A plot step of 0.1, 0.25 and 0.5 s comes from the
  fast history: 0.1 s samples over the last 10 minutes (60 KB of PSRAM),
  written by a 100 ms timer in `main/main.c` (a sample is the events and
  the acquisition time of the step; the sample number runs as a counter,
  so a late call does not shift it into the next tenth). The page fetches
  it 5 times a second while such a step is chosen. A 0.25 s step is made of
  2–3 samples of 0.1 s (0.2 or 0.3 s), CPS by their real time. Checked on
  the device (2026-11-06, ~120 cps): the sample step is exactly 0.1 s
  (294 of 294), over 29 seconds 3579 events by the fast history against
  3586 by the per-second one — the difference is at the second edges (the
  timer and the one-second tick have different phases). “Clear history”
  clears both without touching the spectrum; “Reset” does not touch the
  history. A gap in the plot appears only where
  there was no acquisition. The plot used to break sometimes in the middle
  of acquisition too: the time of a sample was computed from the moment of
  each page request, network delay shifted neighbouring samples, and a
  second stayed empty. Now the offset of the device clock against the
  browser is one for the whole history, and the device stamps a sample with
  the scheduled time of the one-second tick — exactly 1 s apart (it used to
  be the moment of writing, which wandered by a fraction of the main loop's
  100 ms step).
- **Device clock** — there is no real-time clock. The time comes from SNTP
  (`pool.ntp.org`) when there is internet access; otherwise any open page
  gives the device the browser time when the clock is unset or off by more
  than 2 s (and repeats hourly). The time and its source are shown in the
  monitor header; a spectrum export without a time from the page takes it
  from the device clock.
- **Oscilloscope** — edge triggering (auto
  and normal modes, time base 64–32768 samples, amplitude triggering —
  “amplitude from–to”: show only pulses whose height above
  the baseline is within the given range of ADC codes, X axis in µs or in
  samples from the trigger point), a ruler with A/B/C markers (start, peak,
  end of the pulse; the “set before / after peak” button writes them into
  the integration parameters right away).
- **Oscilloscope “codes/div” scale** — as on an ordinary oscilloscope: auto
  or 1, 2, 5 … 500 ADC codes per division, 8 divisions. The baseline sits on
  the first line from the bottom (from the top for downward pulses), the
  scale does not change from frame to frame, so the baseline noise of a few
  codes can be examined. A pulse above the screen is clipped, and how far it
  went is shown at the edge. The browser remembers the choice.
- **“Fix baseline”** — remembers the current baseline level, and from then
  on the oscilloscope draws from it rather than from the baseline of each
  frame: the baseline stays put on screen, and its slow drift shows under
  the plot as “drift”. Pressing again returns to per-frame measurement.
- **Start/Stop apply to the open tab:** on “Spectrum” — spectrum acquisition
  (together with “Reset”, the status line — CPS, events, dead time,
  load — and only the acquisition time next to the buttons; the time runs
  only while the spectrum is being acquired), on “Oscilloscope” — only the oscilloscope: pause holds the last
  frame. Acquisition and the oscilloscope do not run together — the device
  has one mode. **The tabs only switch the view:** opening “Oscilloscope”
  does not start it. “Start” there, while acquisition is running, first
  warns that acquisition will stop (the accumulated spectrum is kept), and
  only after you agree stops acquisition and starts the oscilloscope; “Start”
  on “Spectrum” returns the device to acquisition. The device mode used to
  change as soon as the tab was opened: the oscilloscope started by itself,
  and acquisition silently paused. The header chip shows what the device is
  actually doing.
- **Diagnostics** — a separate page (`/diag`) with two cards:
  - **Device log** — firmware messages (ESP_LOG) since start: the device
    keeps the last 256 lines in PSRAM (`main/mca_log.c`), the page fetches
    new ones every second. Coloured by level: red (with the line
    highlighted) — errors, yellow — warnings, light — messages, grey —
    debug. The tag is coloured by subsystem: ADC, network, firmware,
    settings, display, emulation, system; numbers, addresses and values in
    the text are highlighted. The level and subsystem
    checkboxes (with line counts) hide what is not needed, “follow” scrolls
    to new lines, “Save” writes the log to a file.
    The log is visible even when the console is silent because of MCA
    emulation on COM: emulation turns off only printing to the UART, not the
    collection. Lines from before `app_main` (bootloader, core start) are not
    in the log — only in the console.
  - **ADC diagnostics** — sample stream and its real rate, per-bit
    statistics of the data lines, last-second profile (including
    CPU cycles per sample by stage — copy, difference, threshold and
    events — against the budget of 240 MHz / sample rate), processing load
    on core 1 (the same “load” as in the spectrum status line), core 0 load
    (from the FreeRTOS idle task) with the share of
    the capture interrupt (FreeRTOS counters do not see it — it is
    counted separately) and the busiest tasks with their core, chunk loss log
    with the device mode, data-line check.

![Single pulse](docs/scope-pulse.png)

*Oscilloscope paused, time base 512 samples (32 µs at 16 MHz): a single
pulse 83 codes high, baseline noise 1.1 codes RMS.*

![Pulses and pile-up](docs/scope-pulses.png)

*About 420 µs: pulses of different heights, on the right — a pile-up of two
consecutive pulses.*

![Long time base and settings](docs/scope-long.png)

*Time base 32768 samples (2 ms at 16 MHz) and the processing settings table:
parameters of the other measurement method are dimmed.*

## Dose rate from the spectrum

When the device holds an energy calibration for the current settings, it
computes the dose rate: each event adds the weight G(E) of its channel
(`main/mca_dose.c`, the per-channel weight table is rebuilt when the
calibration or the settings change). The status line shows the dose rate
in µSv/h with the same window as CPS (accumulation up to 30 s with a change
detector, see “CPS and dose rate” below), the tooltip has the window
length, counting error and µR/h. The device takes the weight sums and the
acquisition time as one snapshot under one lock: the acquisition time in
the statistics updates once a second, which in a short window would give
jumps of up to 10 %. The
“Dose rate” block under the calibration the mean since “Reset” with its
counting error. A calibration made with other settings — no dose, the
block says so.

- **G(E)** is a 5th-degree polynomial in ln E fitted on a Monte Carlo
  model of the chosen NaI(Tl) block crystal (`tools/dosemodel`: photoabsorption
  with iodine K X-ray escape, Klein–Nishina Compton, pairs; NIST XCOM cross
  sections) so that the sum of weights over the spectrum gives H*(10)
  (ICRP 74, table from the OpenMC data, MIT licence) and air kerma (NIST μen/ρ) for
  33 keV … 3 MeV photons in a uniform isotropic field. The model
  resolution is 47.2 keV at 609 keV, the threshold 25 keV, above 3 MeV the
  weight grows as E.
- **Model accuracy** (computed against true, 50 keV … 3 MeV): isotropic —
  3 % on average, at most 14 %; source at the side — at most 17 %; at the
  end face below 200 keV — down to −35 % (soft radiation is absorbed near
  the surface, and the end face is smaller than the side). At 662 keV:
  −5 / −6 / −12 %. A plain “k·E” weight would err from −38 to +112 %. The
  housing, reflector, PMT and surroundings are not in the model.
- **Model against the device** (2026-11-06): the “662 peak / Compton
  300–450 keV” ratio for Cs-137 is 2.29 in the model, 2.08 in the device
  spectrum (no background subtracted). Below 250 keV the device sees 2.5
  times more than the model — scatter from the table, walls and
  backscatter; for the dose these are real photons, G accounts for them.
- **Two counting paths** (2026-11-06, Ra-226, ~4600 cps, 56 s): the device
  by events — 6.53 µSv/h and 627 µR/h, the PC from the exported spectrum
  and calibration with the same G from `tools/dosemodel` — 6.48 µSv/h and
  622 µR/h; the difference is events above channel 2048 (in the dose, not
  in the export) and different snapshot moments.
- **Correction k** is a factor to both values (0.05 … 20, kept in the
  device): a dosimeter next to the detector, “Reset”, a few minutes, the
  dosimeter reading — “Fit k”. A flash write, as for any settings, costs
  lost chunks.
- **KCl check** (2026-11-07, `tools/dosemodel/mc_kcl.py`): 400 g of food
  grade KCl in a Ø114×45 mm jar (bulk density 0.87 g/cm³, wall 0.5 mm),
  the detector end face on the lid centre, a lead castle with a copper
  cup; empty-castle background 1950 s, sample 1992 s, no losses. 400 g of
  KCl emit 709 photons of 1461 keV per second (16.6 Bq/g, yield 10.66 %).
  The net peak is 3.51 ± 0.05 cps, the NaI Ø40×40 model gives
  2.37–2.67 cps (the crystal depth in the housing, 2–6 mm, is unknown):
  32–48 % more measured. CsI Ø40×40 (3.44–3.95) or NaI ~Ø45×45
  (3.33–3.82) would give as much; the block is taken as NaI Ø40×40, the
  difference went into the correction k = 0.712 (middle of 0.675–0.761;
  statistics ±1.5 %). A measurement through a spacer does not pin the
  crystal depth: with 20 mm the count ratio changes by only 3.6 % for a
  depth of 1→8 mm. It also shows the energy scale drifted by −4…−5 % over
  the whole range (511 at 486, K-40 at 1390, 2614 at 2516 keV) — PMT gain
  after the calibration; G(E) takes the energy from the scale, so the scale
  should be refreshed before dose measurements.
- Cosmic muons leave more than 3 MeV and go off scale, they are not in the
  dose: at background the device shows less than a dosimeter (at sea level
  the muon part is about 0.03 µSv/h).
- **The detector block** is chosen under “Calibration”: NaI(Tl) Ø25×25,
  30×40, 30×70, 40×40, 63×63, 80×80 (`tools/dosemodel/fit_blocks.py`,
  each with its own G and its own correction k; the former single
  correction went to 40×40). Model H*(10) error from 50 keV to 3 MeV:

  | Block | isotropic, mean / max | side, max | end face, max | 662 keV: iso / side / end |
  |---|---|---|---|---|
  | Ø25×25 | 4.8 / 17 % | 16 % | 34 % | −8 / −10 / −14 % |
  | Ø30×40 | 3.8 / 15 % | 11 % | 47 % | −5 / −5 / −19 % |
  | Ø30×70 | 3.4 / 15 % | 19 % | 66 % | −5 / −1 / −37 % |
  | Ø40×40 | 3.2 / 14 % | 17 % | 35 % | −5 / −6 / −12 % |
  | Ø63×63 | 2.0 / 11 % | 16 % | 34 % | −2 / −3 / −13 % |
  | Ø80×80 | 1.5 / 10 % | 16 % | 34 % | −1 / −3 / −14 % |

  The long Ø30×70 shows 7 cm² from the end against 21 cm² from the side —
  it underestimates soft radiation at the end face most of all. The block
  housing is not in the model: thick-walled blocks overestimate the dose
  below ~60 keV.
- **Everything about calibration is under the “Calibration” button**:
  energy, lines, points, coefficients, dose rate, block, dosimeter
  correction, KCl sample; outside — a one-line summary (“energy: Ra-226 ·
  dose: NaI 40×40, k 0.712”).
- **KCl sample on the page**: “Store background” (the browser keeps the
  empty-castle spectrum and time), the sample — “Compute from sample”. The
  page fits the net K-40 peak (Gaussian + line in a −4…+4.5σ window,
  centroid and width by a grid search, area by linear least squares) and
  computes the same peak with the Monte Carlo model of the chosen block’s
  crystal for a jar under the end face (2 million photons, ~1 s), offers
  k = model / measured and warns of a scale shift above 2 %.
  `tools/check_kcl.py` checks the page core against Python: the model on
  four geometries (0.0…2.6 % apart with a 3σ statistical tolerance), the
  peak area on the sample spectra (`tools/kcl_test.json`) — 3.639 against
  3.589 cps from `curve_fit` in the same window (+1.4 %); it fails on a ×2
  salt attenuation and on a wrong Gaussian normalisation. The area depends
  on the fit window: −3…+3.5σ — 3.47, −5…+5.5σ — 3.63, −7…+8σ — 3.51 cps,
  that is ±2–3 % systematics (the former k = 0.712 used a −7…+8σ window
  and a 4.5 mm gap; the page with a 5 mm gap gives 0.665 ± 0.013).
- A crystal of another size or CsI needs another G: add it to
  `tools/dosemodel/fit_blocks.py` (CsI needs its own cross sections and
  density), run it and rewrite `G_H10`, `G_KA` in `main/mca_dose.c` and the
  `KDET` list on the page.

## Pulse shape filter

**Why.** A noisy PMT or a noisy high-voltage supply adds short pulses of a
different shape to the scintillations: dark pulses at the threshold and rare
fast bursts of large height. By amplitude the trapezoid cannot tell them from
real events, and they land in the spectrum — as a mountain at the threshold
that hides low-energy lines, and as a flat background over the whole scale.
The shape filter looks not at the height but at the pulse width — area /
height in the “before / after peak” window. Scintillations have almost the
same width at any energy (set by the crystal and the amplifier), noise
bursts are 2–3 times narrower. Pulses whose width is farther from the
reference than the tolerance do not go into the spectrum.

**When it is not needed.** Factory detection units with a precision
low-noise high-voltage supply and a good PMT do not need the filter: they
have few noise pulses above the threshold, and the extra check only costs
core time. That is why it is off by default. Turn it on when the spectrum
has a mountain at the threshold that does not go away with the threshold and
the oscilloscope shows narrow single bursts.

**How to enable.** “Settings” page, “Pulse shape filter” card: start the
acquisition (background will do, a source is faster), “Learn reference”
(the device takes 2000 pulses above three thresholds and saves the mean
width and shape), tick “filter on”. The default tolerance is 25 %. The
width is measured from the baseline before the rise — 32 samples ending L
before the threshold crossing. A narrow tolerance also cuts real small
pulses: on Ra-226 (model on raw frames) 12 % loses a quarter of the pulses
in channels 25–40, 25 % loses 5 %, above channel 40 about 1 %. After
updating from v1.2.0 or earlier the reference must be learned anew: the
width was measured differently there, and the old reference is dropped on
load. The
reference is valid for the “before / after peak” window and the ADC rate it
was learned at; if they change, the filter passes everything and asks to
learn the reference again. The card shows the reference pulse with the last
accepted and rejected ones and a width histogram with the tolerance band;
while the reference is being learned, the mean pulse is redrawn as it goes
(on the device the shape settled by ~130 pulses). The card refreshes 10 times
per second (the next request goes only after the answer to the previous one;
the device answers in 47 ms on average); this does not affect chunk losses:
with the card open and closed the losses are the same (measured 2026-11-04,
20 MHz, ~5000 cps, with settings that already overload the core —
convolution with a 114-sample window).

**Which pulses go into the reference** — like reference recording in
BecqMoni: the line “reference: height from … to … codes, pulses …” (0 — above
three thresholds / no upper limit; defaults 0, 0, 2000). Clipped pulses (top
cut by the ADC limit) never go into the reference. Each pulse enters the mean
scaled to its height — large ones do not dominate.

**Measurement method “convolution with the reference”** (after BecqMoni):
the height is the one at which the reference pulse fits the pulse best; the
baseline is 32 samples before the rise starts (ending L before the threshold
crossing). The scale is that of integration: height × the mean of the
reference over the “before / after peak” window, so switching between them
does not stretch the spectrum. It needs a reference for this window and
rate, without one the amplitude is computed by the trapezoid; a new
reference clears the spectrum. If the tail of the reference does not fit in
the chunk, the height is fitted on the part that does. On the noisy PMT of
this device, after the baseline fix (2026-11-04), convolution is better than
both integration and the trapezoid — see “Findings from debugging”,
“Convolution with the reference: model versus device”. The reference is
learned only from running acquisition: after a reboot press “Start” first.

**Check on the device** (2026-11-05): noisy PMT, weak Cs-137 in a lead
castle, 20 MHz, trapezoid L = 10, G = 51, threshold 16, window 13 + 36,
tolerance 32 %; reference learned anew, 900 s each with and without the
filter, the spectrum reset before each run. Alongside — the same measurement
before the width baseline fix (2026-11-04, 333 s each, same settings, the
source placed nearly the same — 662 keV peak without the filter 16.9 and
16.1 cps).

![Cs-137 spectrum with and without the shape filter](docs/shape-filter-spectrum.en.png)

| Channels | Without filter, cps | With filter, cps | Kept | Before the baseline fix |
|---|---|---|---|---|
| below 12 (at the threshold) | 14.45 ± 0.13 | 0.82 | 6 % | 5 % |
| 12–25 (Ba X-ray 32 keV) | 7.27 ± 0.09 | 4.42 | 61 % | 56 % |
| 26–99 | 15.67 ± 0.13 | 13.06 | 83 % | 74 % |
| 100–369 (Compton) | 26.98 ± 0.17 | 26.26 | 97 % | 94 % |
| 370–454 (662 keV peak) | 17.24 ± 0.14 | 17.30 | 100 % | 98.5 % |
| above the peak | 4.16 ± 0.07 | 3.92 | 94 % | 94 % |

Without the filter the barium X-ray (32 keV) drowns in the noise at the
threshold; with the filter it is a separate peak (Gaussian fit 2.35 cps,
2.26 before the fix). The filter now does not touch the 662 keV peak at all:
by a Gaussian fit 16.12 → 15.87 cps, resolution 9.94 → 9.65 %. In total 23 %
of pulses are rejected (25 % before the fix). No chunk losses.

Why the fix gave less here than on Ra-226: this check has a “before peak”
window of 13, not 10, and the old baseline (8 samples before the window,
13–21 samples before the peak) reached the rise less often. The model on raw
Ra-226 frames with these same settings: the old baseline rejected 6.5 % of
events, the new one 2.9 %, above channel 160 1.4 % and 0.2 % — as for the
662 keV peak on the device. What the filter still removes in channels 12–99
is mostly the noise of this PMT (on Ra-226 real pulses of this height lose
1–7 %); the share of real pulses on Cs-137 was not measured directly — the
source is too weak for raw frames.

![Pulse shape: reference, accepted and rejected](docs/shape-filter-pulses.en.png)

The reference is the mean of 2000 pulses aligned at the middle of the rise
(learned at 16 MHz; the examples at 20 MHz, the axis is in microseconds): a
round top ~0.5 µs after the middle of the rise, decay to half another ~1.4 µs
later. The rejected ones are the mean of 71 pulses, one per page request:
rise ~0.4 µs, decay to half in ~0.45 µs; among them there are small ones at
the threshold (20–30 codes) and tall ones, up to the top of the ADC. Where the
tall fast pulses come from was not checked (an assumption — muons in the PMT
glass or discharges in it).

**Cost.** The measure is computed once per event. With integration the filter
takes the peak and the window sum from the integration itself. At 20 MHz and
~5000 cps there are no losses with the filter (5 min), but the core headroom
is small — about 1.1 cycles per sample out of 12; with a noticeably higher
count rate watch chunk losses on the “Diagnostics” page, if there are losses
use 16 MHz. Details and measurements — “Findings from debugging”, “Pulse
shape filter”.

## Architecture

**Capture.** LCD_CAM in camera mode + GDMA, a ring of 16 buffers of 2046
samples. The size is almost the limit: the length field of a GDMA
descriptor is 12 bits (up to 4095 bytes). With 8192 bytes the mask gave
zero, the controller got a zero-size buffer and silently delivered no
chunks at all. The queue depth equals the number of buffers: an index in a
deeper queue would point to an already overwritten buffer.

**Cores.** Core 1 — the processing task (`dsp`). Core 0 — network (W5500,
TCP/IP), web server and the capture interrupt.

**Processing.** Recursive trapezoidal filter. The window difference is
computed with ESP32-S3 vector instructions (PIE), 8 samples at a time; at
start it is self-checked against the plain code, and if the check fails the
plain code is used. Events are detected on the trapezoid output; the
amplitude is taken from the trapezoid top or by integrating the samples
around the peak minus the baseline. Pile-up rejection, dead time,
hysteresis. The histogram has 8192 channels in PSRAM; 2048/4096/8192 are
shown; “Codes per channel” sets the scale.

**Only what is open is processed:** the spectrum is not computed in
oscilloscope mode.

### ADC frequencies

In this copy CLK comes from the camera block (CAM_CLK, `MCA_CLK_GEN_CAM 1`)
with integer dividers from 160 and 240 MHz: 1, 2, 4, 5, 6.67, 8, 8.89, 10,
11.43, 12, 13.33, 15, 16, 17.14, 20 and 24 MHz. LEDC (`MCA_CLK_GEN_CAM 0`, only
80/K) corrupts the data on this board at 16 MHz — see “A CLK generator
from the camera block”.

Measured on this board (2026-10-28, ~3500 cps, 5 s per rate): in spectrum
mode the processing task takes ~72 % of core 1 at 16 MHz and ~89 % at
20 MHz, no chunk losses; the load grows in proportion to the rate and with
the count rate. In the oscilloscope at 20 MHz the queue reaches 16/16 and
losses happen (on the chunks where the window is captured). Hot-loop
estimate (`tools/hotloop.py`): ~5.4 cycles per sample while waiting for an
event (1.75 — vector difference, 3.6 — search).

After 2026-11-04 (the chunk tail is moved with `memcpy`, not `memmove`; the
chunk is processed right in the DMA buffer) at 20 MHz and ~4000 cps: 7.55
cycles per sample of 12, processing 63 %, no losses; with convolution — 7.71
and 64 %. It used to be 10.88 and 91 %. Details — “Findings from debugging”,
“Processing core budget”.

**24 MHz** (240/10, 50 % square wave; added 2026-11-05). Measurement:
Ra-226, ~4800 cps, integration, shape filter off, the same windows in time
(1.2 times more samples at 24 MHz: L 13, G 44, window 14 + 48), 306 s each:

| | 20 MHz | 24 MHz |
|---|---|---|
| stream, million samples/s (set) | 19.985 (20) | 23.982 (24) |
| processing load | 68.4 % | 81.6 % |
| cycles per sample / budget | 7.52 / 12 | 7.47 / 10 |
| chunk queue peak | 7 of 16 | 9 of 16 |
| chunks lost | 0 | 0 |
| main peak: channel, FWHM | 202.0, 9.75 % | 203.1, 9.75 % |

Data lines are alive at both rates, none stuck. The spectrum is the same
(4820 cps, regions within 3 %, the peak shifted by 0.5 % — the windows in
samples are rounded). Resolution did not grow with the rate: the detector
sets it. Core headroom at 24 MHz is ~18 %; convolution and the shape filter
add another 1–2 % (per-event work), at a noticeably higher count rate watch
the losses in “Diagnostics”. Queue headroom is 1.36 ms of stream (1.64 ms at
20 MHz).

The page gets the frequency list from the firmware (`/cfg`, field `fl`),
its own for each CLK generator. The frequency is stored in the device
memory in hertz.

## Findings from debugging

### CLK interference with WiFi

*For boards with WiFi; this copy has no WiFi.* The ADC clock is a square
wave, and its high harmonics land in the 2.4 GHz WiFi band: the CLK wires
radiate next to the module antenna, packets are lost, the link slows down
(at 16 MHz device answers took up to 1–3 s). Working over WiFi is possible,
but with limits: a fixed router channel 20 MHz wide that no strong harmonic
of the chosen rate falls into (for example, channels 4–9 for 16 MHz, 5–8 for
20 MHz), a short CLK wire twisted with ground, and the antenna away from the
wires. To work without regard to the clock — Ethernet.

### A CLK generator from the camera block — the main one in this copy

`adc_clk.c` contains a second generator: the CAM_CLK output of the LCD_CAM
block, integer dividers from 160 and 240 MHz (up to 24 MHz, including 12,
15, 17.14). It is enabled with `MCA_CLK_GEN_CAM 1` in `mca_config.h`. The
calculation promised that at 17.14 MHz (240/14) channel 6 would be clean —
assuming a 50 % square wave. In practice, with capture **stopped**, ping
went 0.3–1.25 s with losses and the interface stopped opening: much worse
than 16 MHz from LEDC. So CAM_CLK is not a clean square wave (the
documentation does not promise the duty cycle of this output). We went back
to LEDC. That was on the previous board with WiFi; this copy has no WiFi, and
CAM_CLK is the main generator (`MCA_CLK_GEN_CAM 1`): see the measurement
below.

**On the Waveshare ESP32-S3-ETH, 16 MHz from LEDC corrupts the data.**
Measured over the network, raw oscilloscope frames with the same wiring and
signal:

| Generator, frequency | Runs | Jumps > 200 codes | Samples 0/4095 | OTR |
|---|---|---|---|---|
| LEDC, 1–13.33 and 20 MHz | 11 | 0–0.02 % | 0 | quiet |
| LEDC, 16 MHz (80/5, duty 40/60) | 7 | 7–47 % | 2–16 % | fires |
| CAM_CLK, 16 MHz (160/10) | 5 | 0 % | 0 | quiet |
| CAM_CLK, 8–20 MHz | 5 | 0 % | 0 | quiet |

OTR is driven by the AD9226 itself and is read from the GPIO, bypassing the
camera interface, so it is the conversion in the ADC that breaks, not the
capture. The odd LEDC dividers 80/7 and 80/9 (also not 50 %) work cleanly —
what exactly is wrong with the 16 MHz clock from LEDC is not known yet: it
needs an oscilloscope on the CLK pin at the AD9226.

The generator switch takes into account that resetting LCD_CAM also stops
this clock, so the module clock source and divider are set only by
`adc_clk` (`adc_clk_cam_apply`), and `adc_cap` calls it after the reset.

### TCP send buffer

By default `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` = 5760 bytes (4 segments). A
16–32 KB `/scope` response took 6 “burst — acknowledgement” round trips,
and a lost tail cost a timer retransmission (1.5 s). It is set to 16384 —
the response goes in one or two round trips. This is a limit, not
pre-allocated memory.

### Oscilloscope

- **Window sized to the time base: up to 8192 in internal RAM, longer in
  PSRAM.** The device collects exactly what the page requests: the time
  base plus the trapezoid margin, with one fifth of the screen before the
  trigger (the prehistory used to be always 6144 samples). At 20 MHz with
  the windows in PSRAM chunks were lost — only in the oscilloscope and
  only during `/scope` requests: writing one capture buffer into the
  window took up to 0.9 ms against a 0.1 ms budget. The PSRAM data cache
  is 32 KB, the window up to 64 KB, and the web was reading the snapshot
  from there at the same time. Now two windows of up to 8400 samples
  (2 × 17 KB) live in internal RAM and two full ones in PSRAM for the
  16384 and 32768 time bases; with those, losses are possible at 20 MHz.
  (The window was once moved to internal RAM because of page freezes —
  that time the cause turned out to be CLK harmonics in WiFi.)
- **The snapshot is sent to the network straight from its buffer**,
  without a copy: while the response is being sent no new frames are
  published, and the next window is collected in another buffer.
- **Pre-trigger history without copying.** Up to 6 KB used to be copied
  per chunk, with a budget of 128 µs per chunk at 16 MHz. Now pointers to
  the four previous capture buffers are kept (history up to 6144 samples),
  and the copy is made once on trigger. The edge is searched only when the
  history is complete — otherwise the first frames after capture start
  (it restarts on tab switch and after pause) would jump across the screen.
  Every capture start is marked as a stream gap: the spectrum filter and
  the oscilloscope history are reset. If the queue is almost full, the
  trigger is skipped: otherwise the history would be taken from a buffer
  that DMA is already overwriting.
- **Edge search with step 4 + exact refinement.** Four times cheaper than a
  full scan, same trigger point. A model against a full scan on 4000
  random chunks: 0 differences on real edges; differences only where the
  full scan triggered on a noise spike shorter than 4 samples.
- **Lock-free snapshot.** The web copies the snapshot under a “reading”
  flag; publishing does not swap buffers meanwhile and just skips the
  frame. The web used to hold a mutex during the copy, and the processing
  task took it on every chunk — that caused losses.
- **Amplitude triggering.** Triggering is still on the edge, but after it
  the device measures the pulse height: the maximum over 256 samples after
  the edge minus the mean of 64 samples before it (ADC codes, taking
  polarity into account). Outside the “from–to” range the frame is dropped
  as soon as those 256 samples are in, without waiting for the end of the
  window, and the device waits for the next edge. The height of the shown
  pulse and the number of dropped ones since the previous frame are printed
  under the plot.
- **Frames go over WebSocket (`/ws`), losslessly compressed.** The page
  sends its settings there (time base, level, amplitude, pause), and the
  device pushes a frame only when the snapshot is new and the previous
  frame has gone out, at most 20 times per second. The page used to poll
  `/scope` and in normal mode got the same frame over and over; over
  Ethernet (W5500) that was 3 Mbit/s at the 16384 time base and 4 Mbit/s
  at 32768 — close to the module's SPI limit. Samples are sent as 4-bit
  differences (`main/scope_codec.c`): a difference of −7…7 is one nibble,
  otherwise an escape and the full sample. On the signal model a frame is
  3.4–4 times smaller: 16384 — 33 → 8 KB, 32768 — 66 → 20 KB. The message
  is cut into 4 KB pieces, so compression runs on the fly without a large
  buffer. At start the device self-tests the codec; if that fails, frames
  go uncompressed (the page understands both). Under the plot you can see
  “stream … kbit/s” and which way it goes.
- **Frames go to one window — the one where the oscilloscope is running.**
  The page sends its settings every 2 s, also when its oscilloscope is
  paused or the spectrum is open. Such a message from a second window used
  to take the stream over and stop it: in the window being watched the
  picture froze for up to 2 s. Now a paused window does not touch the
  stream, and a new connection does not interrupt a running one. If the
  oscilloscope is running in two windows at once, frames switch between
  them every 2 s — watch in one.
- **The page also draws at most 20 times per second**, at any time base.
  Large frames arrive over TCP in bursts; when each was drawn at once, the
  browser could not keep up with 32768 points, frames piled up, and the
  refresh rate first raced and then dropped. Now a frame that arrives
  early waits, and the next one replaces it — the freshest is drawn.
  “Refresh … /s” is the number of frames drawn per second.
- If the WebSocket does not open, the page polls `/scope` as before: the
  response is binary (7 × int32 + uint16 samples), the next request right
  after drawing, at most every 50 ms. The device prepares snapshots at
  most every 25 ms.

### Pulse shape filter

- **What PMT noise looks like** (noisy PMT, background without a source,
  2026-11-03, 20 MHz, 360
  oscilloscope frames of 32768 samples, ~0.6 s). Baseline: σ ≈ 1.6 codes,
  swing over 26 µs — 10 codes, 0.4 ms — 15, 1.6 ms — 18 (the baseline also
  wanders slowly by a few codes). Single spikes of 1–2 samples, ~3300 per
  second, 8–9 codes high: the trapezoid with L = 12 damps them L times, they
  do not reach the spectrum. Above the threshold there are two kinds of
  pulses: scintillations — FWHM ~40 samples, rise ~10, area / height 50 ± 4
  at any height from 40 to 4000 codes; and narrow ones — width ~10, rise ~3,
  area / height 15–33, nearly all 15–60 codes high: they make the pile at
  the start of the spectrum. That the narrow ones are PMT noise or pickup and
  not events is a conclusion from their shape; their source was not checked.
- **Why the measure is area / height.** It is computed once per event, not
  per sample. The measure's baseline is the eight samples right before the
  window, not the common baseline: so the tail of the previous pulse does
  not change the width.
- **What it costs and how it fit into 20 MHz.** Temporary cycle counters
  (2026-11-04, 20 MHz): ~650 cycles per event — peak search 323 (48
  samples), window sum 259 (58 samples), the rest ~64. With a source (~5000
  cps) at 20 MHz the filter lost ~130 chunks per second. What did not help:
  32-bit division instead of 64-bit (same cost); a two-pass peak search and
  summing two samples per read (slower: 460 and 280); vector instructions for
  the maximum and the sum (no better than plain code in cycles per sample,
  and the build with them by itself added +0.07 cycles and losses without the
  filter) — all reverted. What helped:
  - with integration, the peak and the sum of the “before / after peak”
    window are already found by `amp_integrate` — the filter takes them
    instead of searching again;
  - both `amp_integrate` and the filter look for the signal peak no earlier
    than “threshold crossed minus L” (the threshold is crossed no later than
    L samples after the start of the rise). Integration used to search
    L + G + 5 samples back from the trapezoid peak (~100 samples). On the model
    the spectrum does not change.

  Measurements at 20 MHz with a source, filter off / on for 20 s (device
  settings: integration, L = 21, G = 78, tolerance 10 %):

  | Build | Filter | Chunks lost | Cycles per sample (of 12) |
  |---|---|---|---|
  | before | off | 0 | 10.91 |
  | before | on | ~2700 per 21 s | 11.08–11.13 |
  | peak and sum from integration | on | 113–155 per 21 s | 10.95–10.97 |
  | + search from the threshold crossing | off | 0 | 10.84 |
  | + search from the threshold crossing | on | 0 | 10.88 |
  | same, trapezoid | off / on | 0 / 0 | 10.64 / 10.82 |

  Five minutes at 20 MHz with the filter (~5000 cps, 1.58 million checked
  pulses): 0 losses, load at most 90.8 %. The headroom is about 1.1 cycles
  per sample: with a noticeably higher count rate 20 MHz may not be enough
  even without the filter. At 16 MHz (budget 15 cycles) with the filter it is
  11.24 cycles, no losses.
- **What it rejects.** Everything below is on background, ~330 cps. The
  reference on this PMT, learned on background, is 34.1 samples (window
  13 + 36 + 1, 10–90 % spread 28.7–36.1), tolerance 25 %: 25.7 % of the
  checked pulses were rejected (3 min, 59 870 pulses). From the device map
  (later replaced by the reference pulse and the width histogram):
  channels 1–15 — 89 %, 15–40 — 11–14 %, 40–100 — 6–9 %, 100–200 — 3.5 %,
  above 200 — under 1 %, and all rejected ones are narrower than the band,
  none wider. The model (`dspmodel.py`) on the captured frames with the same
  reference: reference from its own events 33.6, rejected 18.3 % (32 of 175 —
  few events), also almost all below channel 25. With a source (~5000 cps)
  13–14 % are outside the tolerance; the reference at 20 MHz is 34.0 samples,
  at 16 MHz 31.1 (the same pulse, fewer samples on it).
- **A chunk loss that did not repeat.** In the first run on the device, 31
  chunks were lost ~1 s before the reference learning finished: one chunk
  took 14.4 ms to process against a budget of ~0.1 ms (loss log,
  `/diag/data`). A second learning run, each action one at a time (saving
  settings, “Reset”, clearing the map, enabling the filter) and two 3-minute
  acquisitions (filter with the map and without) gave no losses. The cause
  was not found. (This is on background; the losses with a source at 20 MHz
  are a different matter, see “What it costs”.)
- **A kink at the reference top.** At first the reference was accumulated
  with pulses aligned at their highest sample. Real pulses have a wide,
  almost flat top (~10 samples within 0.97–1.00), and noise throws the
  position of the maximum around: for 59 pulses from 0.7 to 13.8 samples after
  the middle of the rise. The rise on the left is steep, the decay on the
  right is gentle — the mean got a step left of the top (0.84 → 0.93 → 1.00)
  that the pulses do not have. Now pulses are aligned at the first rise
  sample above half the height: the top is smooth (0.961 → 0.983 → 0.995 →
  1.000 → 0.997). The model does not reproduce the kink (all its pulses have
  the same top), so it was checked on the device. This is display only: it
  does not affect the filter decision.
- **The height is the maximum, not a smoothed top.** The maximum of several
  nearly equal samples is biased up by noise (~2 codes): for a 300-code
  pulse this is under 1 %, for a 20–30-code pulse (barium X-ray) 7–10 %, and
  small real pulses come out narrower. Smoothing the top would fix this, but
  a check on 71 accepted and 71 rejected pulses showed worse separation: noise
  bursts have a sharp top, and smoothing widens them more.

  | Height from | Small accepted (<60 codes) | Rejected: 95 % / max | Gap |
  |---|---|---|---|
  | 1 sample (kept) | 23.0–35.2 | 19.7 / 22.4 | 1.35 |
  | 3 samples | 25.2–35.6 | 22.2 / 26.9 | 1.24 |
  | 5 samples | 26.4–36.3 | 23.1 / 31.6 | 1.21 |

  Gap — 5 % of accepted widths to 95 % of rejected. The tolerance (25–40 %)
  covers the narrower width of small pulses. *The real cause of the
  narrower width is the baseline lying on the rise; below.*
- **The width baseline lay on the rise** (analysis 2026-11-04: 2200 raw
  Ra-226 frames, 20 MHz, 17 872 events through the firmware model with the
  device settings). The width was taken from a baseline of “8 samples right
  before the ‘before peak’ window”. The pulse top is rounded, noise shifts
  the maximum a few samples later, and those 8 samples landed on the rise —
  on average at 60 % of the height: the width came out ~11 instead of ~33,
  and the filter rejected a real pulse. The mean shape of such “narrow”
  pulses matches the accepted ones, the width with the baseline before the
  rise is 32.5 versus 33.2; real bursts with that baseline — ~15. Now the
  baseline is 32 samples ending L before the threshold crossing, as in
  convolution.

  Share of real pulses rejected (tail 15–25 samples after the peak ≥ 0.42 of
  the height, no pile-up; tolerance 40 %, window 10 + 36):

  | Height, codes | 16–25 | 25–37 | 37–60 | 60–100 | 100–400 | 400+ |
  |---|---|---|---|---|---|---|
  | baseline before the window (old) | 55 % | 44 % | 17 % | 12 % | 5 % | 0.4 % |
  | baseline from the threshold crossing | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % |

  The “tail” yardstick also takes its baseline before the rise, so the
  zeros are somewhat optimistic; the main evidence is the identical shape
  of rejected and accepted pulses. Bursts (tail < 0.30) near the threshold
  are passed as before: 3.5–4 % versus 5–7 %.

  Device versus model (device settings: threshold 20, window 12 + 40,
  tolerance 12 %, Ra-226): the model on raw frames rejects 21.7 % with the
  old baseline and 5.5 % with the new one; the device with the new firmware
  — 5.6 % (300 s, 1.43 million events), reference width 36.3 on the device
  and 36.1 in the model. The Ra-226 peak with and without the filter is the
  same (FWHM 9.75 %), above channel 80 99 % remains. The test is
  `tools/test_dsp.py`, “rounded top”: it goes red on the old baseline (5.8 %
  rejected at 20 codes versus 0).
- **Pearson correlation instead of width** (as BecqMoni selects) was checked
  on the same events and not adopted. Threshold 0.8: it loses 7 % of small
  real pulses (16–25 codes), catches bursts near the threshold slightly
  better (passes 2–3 % versus 3.5–4 %), pile-ups noticeably better (16 %
  versus 1.3 %), but pile-ups are only 1.7 % of events at ~4000 cps.
  Threshold 0.9 already loses 24 % of small pulses. For small pulses the
  correlation drops more than white noise would give (median 0.90 against
  an expected 0.95): the shape varies from pulse to pulse.

### Processing core budget: memmove and the chunk copy

Measurements 2026-11-04: 20 MHz, Ra-226, ~4000 cps, integration, window
10 + 50, shape filter on; cycles per sample by stage from “Diagnostics”,
90–120 s each, the spectrum reset before each run.

| Build | Copy | Difference | Threshold and events | Total of 12 | Processing |
|---|---|---|---|---|---|
| before (`6e929fe`) | 1.43 | 2.15 | 7.29 | 10.88 | 90.7 % |
| chunk in place | 0.29 | 2.20 | 5.06 | 7.55 | 63.0 % |
| same, but always copying | 1.58 | 2.15 | 5.07 | 8.80 | 73.3 % |
| same + old DMA buffers | 1.58 | 2.15 | 5.07 | 8.79 | 73.3 % |
| chunk in place, tail via `memmove` | 0.29 | 2.20 | 7.29 | 9.78 | 81.6 % |

- **Chunk copy.** Each chunk used to be copied whole from the DMA buffer into
  the work buffer — 1.43 cycles per sample. Now the DMA buffers have room for
  the previous chunk's tail (200 samples) before the data, and processing runs
  right in them; only the tail is copied: 0.29. If the queue is already 12 of
  16 (DMA will soon write into the same buffer) or pulses go down (they must
  be inverted, and the DMA buffer is read by others) — the old path with a
  copy.
- **memmove.** An unexpected and the largest finding: the old move of the
  tail to the start of the work buffer with `memmove` (400 bytes per chunk)
  cost **2.2 cycles per sample** — almost a fifth of the budget at 20 MHz.
  `memcpy` and `memmove` here come from the chip ROM, and the ROM `memmove` is
  slow: with it “threshold and events” is 7.29, with `memcpy` 5.07, everything
  else the same. Two misses on the way: it is not function inlining (in both
  builds everything is in one function in internal instruction RAM) and not
  the placement of the DMA buffers.
- **The spectrum is the same**: scale ×1.0000, shape match 0.9999, count rates
  by region within statistics, share rejected by the filter 18.9 → 19.0 %.
- **Which load to show.** There were three numbers and they disagreed.
  Cycles measured by parts of the processing task loop (2026-11-05, 20 MHz,
  Cs-137, spectrum mode, 10 s, 9767 chunks per second):

  | What | Share of core 1 | Cycles per chunk |
  |---|---|---|
  | chunk processing (`mca_dsp_process`) | 56.9 % | ~14 000 |
  | service work around it (settings requests, diagnostics, profile) | 6.8 % | ~1 660 |
  | going to sleep until the next chunk and waking up | 12.6 % | ~3 100 |
  | total — core 1 load by FreeRTOS | 76.3 % | |

  The shape filter has nothing to do with it: with and without it core 1 is
  75.4 %, processing 6.83 cycles per sample (~70 events per second on
  Cs-137). 98 203 chunks of 98 234 arrived at an empty queue: the task went
  to sleep and woke up for each. If a chunk is already waiting, getting it
  costs 375 cycles — so under load, when the queue does not empty, there is
  no sleeping and these 12.6 % disappear. Earlier measurements show the same:
  before the “chunk in place” fix processing took 90.7 % of cycles, ~7 %
  around it — and there were no losses; with the sleeps losses would have
  started at ~80 %. So there is one number everywhere — **processing load**:
  the share of time the task works on chunks (~63 %). It grows with the work
  and approaches 100 % when losses start; core 1 load by FreeRTOS is
  overstated at low load. Yellow from 85 %.

### Core 0: headroom for moving work

Core 0 runs the network (W5500), the web server, the display and the capture
interrupt. With no pages open it is busy ~1 % with tasks and ~4 % with the
capture interrupt (~9800 times per second at 20 MHz); with the shape filter
card open (`/shape` 10 times per second) — ~14 %. Check: a temporary task on
core 0 (priority above the web server) took a given share of the core, the
page polled `/shape` 10 times per second, acquisition ran:

| Added on core 0 | Core 0 total (+ interrupt) | `/shape` answer, median / 95 % | Chunk losses |
|---|---|---|---|
| 0 % | 14.0 % + 4.2 % | 57 / 60 ms | 0 |
| 20 % | 32.7 % + 4.2 % | 58 / 62 ms | 0 |
| 40 % | 52.8 % + 4.1 % | 60 / 64 ms | 0 |
| 60 % | 71.0 % + 4.1 % | 61 / 64 ms | 0 |
| 80 % | 87.7 % + 4.1 % | 60 / 66 ms | 0 |

Neither the web server's answers, nor capture, nor processing on core 1
suffered. Conclusion: core 0 has ~70 % of the core spare. Event processing
(amplitude, shape filter, histogram) at 5000 cps is, by estimate (~1500–2700
cycles per event), 3–6 % of a core, so it can be moved to core 0 with a large
margin. But after the changes above core 1 already has ~4.4 cycles per sample
spare at 20 MHz (63 %), so the move is needed only for a noticeably higher
count rate or heavy event processing.

### Convolution with the reference: model versus device

- **Model** (white noise 1.6 codes, identical pulses, L = 10, G = 51, window
  13 + 36): FWHM at 30 codes — trapezoid 5.64 %, convolution with an 8-sample
  baseline 7.04 %, 16 — 5.28 %, 32 — 4.18 %, 64 — 3.52 %; at 400 codes
  0.72 % versus ~0.85 %. The BecqMoni variant — a zero-mean reference, no
  baseline needed — 2.81 % at 400 codes and 6.82 % at 30. A 32-sample baseline
  was chosen.
- **Device** (2026-11-04, noisy PMT, Cs-137 in a lead castle, 20 MHz, L = 10,
  G = 51, window 13 + 36, shape filter on, 331 s each, reference 34.4 samples
  from 2000 pulses):

  | Method | 662 keV peak, FWHM | Ba X-ray 32 keV, peak / valley |
  |---|---|---|
  | integration | 8.74 % | 0.94 (no peak) |
  | convolution with the reference | 11.05 % | 0.90 (no peak) |
  | trapezoid | 9.61 % | 2.20 |

  No chunk losses, load 84.3–84.5 % in all three. On the device convolution is
  worse both at 662 keV and at the threshold — the model did not show this.
  An assumption (not checked): the model has white noise and identical
  pulses, while the real baseline wanders slowly by a few codes (the swing
  grows with the window: 10 codes over 26 µs, 18 over 1.6 ms) and the shape
  varies from pulse to pulse. Long-window methods (convolution, integration)
  pick this up, the trapezoid — a difference of two windows — does not. For
  low-energy lines on this PMT the trapezoid is better. *This assumption did
  not hold — the cause is below.*
- **Cause: the baseline lay on the rise** (analysis 2026-11-04 on 19 thousand
  isolated pulses from raw scope frames, Ra-226, 20 MHz, window 10 + 50). The
  convolution baseline was taken right before the “before peak” window, and
  the rise of this PMT (~20 samples) is longer than that window (10): the
  baseline lay on the rise and the height was underestimated by an amount
  that depends on the rise shape — the reference started at −0.40 instead of
  0. Main peak FWHM on these pulses: trapezoid 9.25 %, convolution as it was
  11.75 % (8-sample baseline — 21 %, 64 — 9.5 %), with the baseline ending L
  before the threshold crossing — 9.00 %. The threshold is crossed no later
  than L samples after the rise starts, so such a baseline never touches the
  rise whatever the window. The reference is now learned with the same
  baseline.
- **Spectrum stretch.** The convolution height is the pulse height, while
  integration gives the mean over the window, about 0.6 of the height with a
  10 + 50 window. So switching stretched the spectrum 1.5–2 times (device,
  Ra-226: 1.62). Now convolution outputs height × the mean of the reference
  over the integration window — what integration of a clean pulse of this
  shape would give.
- **Chunk end.** If the tail of the reference did not fit in the chunk, the
  event was measured by the trapezoid — in its scale, and a second peak grew
  above the real one (model, long rise: ~0.4 % of events, FWHM 6.30 % instead
  of 0.45 %). Now the height is fitted on the part of the reference that fits
  in the chunk.
- Both cases are in `tools/test_dsp.py` (level 12, “long rise”); both checks
  go red on the old code: baseline before the window — the reference differs
  from the generator by 0.069, FWHM 2.93 % versus 0.62 % for the trapezoid;
  fallback to the trapezoid — 6.30 %.
- **Device after the fix** (2026-11-04, noisy PMT, Ra-226, ~4300 cps, 20 MHz,
  L = 11, G = 37, threshold 16, window 10 + 36, shape filter on, tolerance
  40 %, reference learned anew; 300 s each, spectrum reset before each run):

  | Method | Main peak, channel | FWHM | Load | Chunk losses |
  |---|---|---|---|---|
  | trapezoid | 270.4 | 10.75 % | 63.5 % | 0 |
  | integration | 215.7 | 10.00 % | 63.8 % | 0 |
  | convolution with the reference | 225.1 | 9.50 % | 65.3 % | 0 |

  Convolution to integration scale ×1.043 (both by the peak and by stretching
  the whole spectrum; shape match 0.998). The remaining 4 % is an assumption:
  the integration baseline (running average) slightly lowers the amplitude;
  on the model the same integration gives 17.61 versus 18.85 with an exact
  baseline. *Since 2026-11-06 the integration baseline is before the rise,
  as for convolution (model: 19.19, convolution 19.19), see “609 keV
  resolution”.*

### 609 keV resolution (Ra-226): method and window

Selection 2026-11-06: noisy PMT, Ra-226, ~4900 cps, 24 MHz, 10 minutes per
run, spectrum reset before each. **The comparison is in keV**: each run is
calibrated by the 352, 609, 1120 and 1764 keV lines (E = a·channel + b, the
lines lie on the straight line within 5 keV), resolution is the FWHM of the
609 keV peak in keV. The percentage “width in channels / position in
channels” depends on the scale zero offset — from +30 to −24 keV for
different methods — and comparing methods by it is unfair.

| Method | FWHM 609, keV | % of 609 keV | In the peak, cps |
|---|---|---|---|
| trapezoid L10 G13 (as it was) | 69.9 | 11.47 | 296 |
| trapezoid L10 G40 | 63.0 | 10.33 | 290 |
| trapezoid L48 G98 | 55.2 | 9.07 | 276 |
| convolution 19 + 152 | 54.0 | 8.86 | 288 |
| integration 12 + 120 | 57.9 | 9.50 | 269 |
| integration 19 + 152, running baseline | 49.4 | 8.12 | 261 |
| integration 19 + 152, baseline before the rise | 49.0 | 8.04 | 262 |
| integration 26 + 152 | 49.6 | 8.14 | 265 |
| integration 19 + 200 (two runs) | 48.3 / 48.1 | 7.93 / 7.89 | 255 / 251 |
| **integration 19 + 250** | **47.5** | **7.80** | 241 |
| integration 19 + 320 | 47.7 | 7.83 | 228 |
| final: 19 + 250, shape filter 25 %, 0.25 codes per channel | 47.2 | 7.75 | 238 |

A repeat of the same run differed by 0.2 keV, the fit error is about
0.3 keV. No chunk losses in any run, processing load 80–83 %.

- **The pulse** (3000 raw oscilloscope frames of 32768 samples): baseline
  noise 1.3 codes; rise 10–90 % — 0.5 µs (12 samples), from 10 % to the top
  — 18; the top stays within 2 % of the maximum for only 5 samples; decay to
  50 % in 1.4 µs, to 10 % in 4.8 µs, to 1 % in 12.5 µs (a slow tail).
- **Height follows the charge poorly.** The flash shape varies from pulse to
  pulse, and the height of a rounded top is a poor measure of energy. A
  trapezoid with a short L measures exactly that; as L grows it approaches
  integration (L48 — 55 keV). Best is the full charge: integration with a
  window to the end of the tail.
- **Trapezoid G larger than L + rise.** The trapezoid subtracts the sum of L
  samples taken G samples back; with G = 13 and a rise of ~24 samples that
  subtracted window landed on the pulse's own rise, and the amplitude
  depended on its shape: G = 40 gives 63.0 keV instead of 69.9.
- **“Before peak” 19**: from 10 % of the rise to the top is 18 samples; 12
  cuts the rise (57.9 keV), 26 adds nothing. **“After peak” 250** (10.4 µs):
  the tail drops to ~3 % by then, no gain beyond, but more pile-up — 241 →
  228 cps in the peak at 320. The window is set in samples: at 20 MHz the
  same time is 16 + 208.
- **The shape filter** does not affect the 609 keV peak (48.9 versus
  49.0 keV); it is needed near the threshold.
- **In the code.** The integration baseline is now 32 samples before the
  rise, as for convolution and the width measure (the running one is only a
  fallback): scale zero −21 → −16.5 keV, resolution 49.4 → 49.0 keV (at the
  edge of the error). “Baseline 2^N” is limited to 1–16: the device had 55,
  and shifting a 32-bit number by 32 or more is undefined in C. The
  remaining scale zero of −16 keV is probably NaI non-proportionality (an
  assumption, not checked).

### Other

- Pinning the TCP/IP task to core 0 made things worse: the web waited
  longer for its core. Left unpinned, but the processing task now has
  priority 19, above TCP/IP (18): when TCP/IP moved to core 1 it
  preempted the processing (which had 10), and at 20 MHz the capture
  queue holds only 1.6 ms.
- Processing statistics and parameters are under a spinlock, not a
  mutex. “Apply” and “Reset” held the mutex while clearing the histogram
  in PSRAM and printing to the console, and the processing task waited
  milliseconds — guaranteed losses. Now the processing task itself clears
  the spectrum and restarts the filter, the histogram in pieces of 1024
  channels per chunk; during these ~0.8 ms of the stream the spectrum is
  not acquired and the acquisition time does not run. “Reset” now also
  zeroes the above-scale event counter, which used to accumulate.
- The spectrum is read without locking; events are written to the histogram
  without a mutex, and the accumulated counts are flushed once per chunk.
- **GPIO matrix constant levels come from ESP-IDF, not as numbers.** On
  the ESP32-S3 the constant one is `0x38` and the constant zero is `0x3C`
  (`soc/esp32s3/include/soc/gpio_pins.h`). The firmware used to have
  `0x30` — the zero of the classic ESP32, but on the S3 it is the
  ordinary pin GPIO48. The “constant zero” on the frame sync input and
  the spare data lines actually read GPIO48. On the author's board it
  idles low, so everything worked by chance; on boards with an LED on
  GPIO48 (ESP32-S3 Super Mini, Arduino Nano ESP32) the camera never saw
  a frame start, and not a single chunk arrived although the clock was
  running. Found when the project was being repeated. The values now come
  from ESP-IDF, and the build checks that the constant does not match a
  real pin number. Unused CAM data lines are tied to zero so they do not
  pick up noise; if the zero is not a zero, diagnostics shows it (the
  “high bits 12–15” line).
- Rates “per second” (sample rate, CPS, chunks) are computed over the
  actually elapsed time: the one-second tick comes from the lowest-priority
  main loop, and under core 0 load (for example, spectrum upload over COM)
  it stretched to 1.1–1.2 s, overstating the rate by 10–20 %.
- CPS and dose rate on the spectrum page — for finding a source by hand
  and for a precise number at rest at the same time: the `main/cps_win.c`
  window — accumulation + a CUSUM change detector. Every 100 ms (a timer
  in `main/main.c`) comes an interval of the fast history: events,
  milliseconds of acquisition, dose weight sums. While the count is steady
  the window grows up to 30 s. Two accumulated sums of the Poisson
  log-likelihood ratio — “the rate rose 1.5 times” and “fell 1.5 times”
  against the window mean; a sum above 8 is a change: the window is cut to
  the intervals since the sum started to grow, and the number is computed
  from the new data at once. A single noisy interval does not convince the
  detector, a real step does within fractions of a second or seconds. The
  page fetches CPS and dose from `/cps` 10 times a second; the tooltip
  shows the window length and the counting error.

  Model (`tools/cpswin.py`, a line-by-line port of `cps_win.c`; response —
  median (90 %) seconds until the number passes half of the step):

  | Background | ×1.3 | ×1.5 | ×2 | ×10 | removed /2 | at rest |
  |---|---|---|---|---|---|---|
  | 30 cps | 7.1 (19) | 2.8 (4.8) | 0.9 (1.4) | 0.2 (0.3) | 2.2 (3.2) | ±3.5 % |
  | 300 cps | 0.8 (1.5) | 0.3 (0.5) | 0.1 (0.2) | 0.1 (0.2) | 0.2 (0.3) | ±1.2 % |

  For comparison a 10 s window at 30 cps: 5 s response to any step, ±5.5 %
  at rest; a window like the former fast CPS (100 ms … 1 s, cut on a 3σ
  single interval) — ±27 % at rest: with ~3 events per 100 ms a
  single-interval test often fires on noise. A false CUSUM cut happens once
  in ~20 min. A weak step (×1.3 at 30 cps) is caught slowly: such a rise is
  hard to tell from noise.

  Check against the device (2026-11-07, Ra-226, ~4700 cps, 60 s): the
  device returns in `/cps` the fast history sample number, the window
  length and its count; the model run over the same `/hist?f=1` history
  gave the same integers in 162 polls of 162 (`python tools/cpswin.py <ip>
  60`); with a window of 299 instead of 300 — 49 mismatches of 49. A
  second time the Ra-226 source was brought to the detector and removed
  again and again (background ~300, at the detector ~4500 cps), 90 s: the
  detector fired 43 times, 292 polls of 292 matched. The number catches the
  source within the same second: brought — in the second with 2611 counts
  the fast CPS is already 4591 (window 0.4 s); removed — in the second with
  1269 counts it is already 300 (window 0.6 s); at rest the window grows
  back to 30 s. While acquisition is stopped the window is not touched, and
  CPS and dose show 0.

## Console diagnostics

By default the profile is not printed to the console — only the line
`потеряно чанков: N, при последней потере шёл /…` (chunks lost: N, the
request in progress at the last loss was /…), and only in the second when
losses happened. The “Diagnostics” page always shows the
profile. To enable printing: `#define MCA_PROF_LOG 1` in
`main/mca_config.h`. Then every second:

```
prof: q=4/16 work=0.90 wait=0.23 busy 75% lost=0 | sp 1x23 sc 10x14 st 1x1 | heap 125k/87k load 71% cps 2242
prof: cpu0 5% cpu1 86% | dsp(1) 85.4% httpd(0) 2.2% main(0) 0.9% tiT(*) 0.5%
```

| Field | Meaning |
|---|---|
| `q` | peak chunk queue depth; 16/16 — losses will follow |
| `work` / `wait` | longest chunk processing / wait for a chunk, ms |
| `busy` | share of time the processing task is busy, in any mode |
| `lost` | chunks lost per second |
| `sp`, `sc`, `st` | `/spectrum`, `/scope`, `/stat` requests per second × the longest, ms |
| `heap` | free internal memory / its all-time minimum |
| `load` | load inside spectrum processing |
| `cpu0`, `cpu1` | core load from FreeRTOS accounting (interrupt time counts toward the interrupted task) |
| `task(core)` | busiest tasks; `*` — not pinned to a core |

For each loss there is a line `ПОТЕРЯ +n q=… шёл /ep Xms work=…` (LOSS):
which request was being served at that moment and for how long.

## Other ESP32-S3 boards

The firmware is built for the ESP32-S3-N16R8 (DevKitC) but also starts
on other ESP32-S3 boards:

- **Flash.** The firmware header says 4 MB: if it says more than the
  board has, ESP-IDF stops the start (`ESP_ERR_FLASH_SIZE_NOT_MATCH`).
  On 16 MB there is only a warning at start.
- **PSRAM.** Without it, or with another type (Quad, as on N4R2), the
  device does not crash but works without it: no 6-hour CPS history and
  no 16384 and 32768 oscilloscope time bases. To get everything, set your
  PSRAM type in `idf.py menuconfig` (Component config → ESP PSRAM).
- **Pins.** This copy is wired for the Waveshare ESP32-S3-ETH (see
  “Wiring”). For another board, change the pins in `main/mca_config.h`.
  Avoid GPIO0, 45, 46 (boot), 19–20 (USB), 26–37 (flash and PSRAM on
  modules with Octal PSRAM).

## Bring-up on new hardware

1. Frequency 1–2 MHz, “Oscilloscope” tab, “Auto” mode,
   Start. Feed something known to the input. Expect a sensible trace. If
   the data is all zeros — check the R32–R35
   jumpers on the AD9226 board. If there are no chunks while PCLK is alive,
   the camera interface control-signal mode does not fit: the `s_mode`
   fields in `main/adc_cap.c`. On this board the working mode is all
   polarities non-inverted, frame sync tied to constant zero; it was found
   by trying all 48 variants (variant 16), after which the search was
   removed from the firmware. On the “Diagnostics” page, the “high
   bits 12–15” line must be 0, and “Check data lines” must show D0–D11
   toggling.
2. Signal from the PMT: a flat baseline between pulses, headroom on the
   ADC scale.
3. Raise the frequency step by step, checking the oscilloscope and `lost`
   in the console (or on the “Diagnostics” page) at each step. Data
   jumping to 0 and 4095 and OTR firing at one rate means the clock reaches
   the ADC in bad shape (as with LEDC at 16 MHz, see “A CLK generator from
   the camera block”).
4. Spectrum: threshold above the trapezoid noise (the “filter
   noise” line under the oscilloscope), L and G from the pulse shape,
   “Codes per channel” so the peaks fit the scale.

## Tools (`tools/`)

- `test_dsp.py` — 12 levels of processing checks on a model, including the
  vector version against the plain one, the oscilloscope frame compression
  against the page decoder, the splitting of the CPS history into intervals
  and the timing of its samples under network delay (the page functions run
  in node), the shape filter on a signal with scintillations and noise
  bursts. `python tools/test_dsp.py`.
- `dspmodel.py`, `scopemodel.py`, `scopecodec.py` — models of the
  processing, the oscilloscope and its frame compression.
- `hotloop.py` — hot-loop cycle estimate from the disassembly.
- `mkpage.py` — builds `build/page_test.html` with the device page and stub
  responses; open with `python -m http.server 8777 --directory build`.
- `emu_check.py` — checks the MCA emulation over the network from a PC
  (`python tools/emu_check.py <device address> [port]`; `--selftest`
  checks the codec only).
- `check_i18n.py` — checks that every label of the device pages has an
  English pair (`python tools/check_i18n.py`).
- `cpswin.py` — the fast CPS and dose window (a port of `main/cps_win.c`):
  the step response model and a check against the device on the fast
  history (`python tools/cpswin.py` / `python tools/cpswin.py <ip> 60`).
- `check_kcl.py` — the KCl sample of the spectrum page: the 1461 keV peak
  model and the net peak fit against `tools/dosemodel` and scipy
  (`python tools/check_kcl.py`).
- `check_cal.py` — the energy calibration of the spectrum page: takes its
  core out of `mca_web.c` and runs it in node on synthetic Ra-226 and
  Cs-137 spectra with a known scale and on a real device spectrum
  (`cal_ra226_trap.json`) (`python tools/check_cal.py`).
- `dosemodel/` — dose rate: `mc.py` — Monte Carlo response of a NaI
  crystal, `fitg.py` — the G(E) fit and its accuracy by energies and
  directions (`cd tools/dosemodel && python fitg.py`); data — NIST XCOM,
  NIST air μen/ρ, ICRP 74 H*(10) from OpenMC (`photons_H10.txt`);
  `mc_kcl.py` — the expected 1461 keV peak from a KCl jar under the
  crystal end face.
- `ota.py` — flashing over the network (`python tools/ota.py <ip>`, see
  “Updating over the network”).
