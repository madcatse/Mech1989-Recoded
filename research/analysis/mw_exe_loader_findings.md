# MW.EXE loader findings

Status: first readable reconstruction from the `Original/MW.EXE` binary and
`research/disassembly/MW.EXE/MW.EXE.c`.

## Scope

`MW.EXE` is a small DOS MZ launcher, not the main game executable.

- File size: 4096 bytes.
- MZ image size from header: 3907 bytes.
- Header size: 512 bytes.
- Loaded image size: 3395 bytes.
- Extra tail after MZ image: 189 bytes, filled with `END OF MW_SHELL` and DOS EOF bytes.
- Entry point: `0006:09EE`, file offset `0x0C4E`.
- Relocations: 4 words, all inside the DOS `EXEC` parameter block.

The only embedded executable names are:

- `MW_MAIN.EXE` at file offset `0x0A43`, data offset `0x07E3`.
- `BTECH.EXE` at file offset `0x0A4F`, data offset `0x07EF`.

## High-level behavior

The loader:

1. Shrinks its own DOS memory block.
2. Allocates and immediately releases a large memory block as a memory
   availability check.
3. Performs an additional 512K allocation probe and stores a compact memory
   class flag.
4. Selects graphics mode from the command line, auto-detection, or an on-screen
   prompt.
5. Selects audio output from the command line, defaulting to Roland.
6. Clears the text screen.
7. Runs `MW_MAIN.EXE`.
8. If `MW_MAIN.EXE` exits nonzero, runs `BTECH.EXE`.
9. If `BTECH.EXE` exits nonzero, returns to `MW_MAIN.EXE`.
10. If either child exits with return code 0, exits to DOS.

This makes `MW.EXE` a shell between the campaign executable and the combat
executable.

## Data block

Offsets below are data-segment offsets for the loader's `CS == DS` segment.
For the corresponding file offset, add `0x260`.

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `0x07E3` | `MW_MAIN.EXE` ASCIIZ filename | Passed as `DS:DX` to `int 21h AH=4Bh` |
| `0x07EF` | `BTECH.EXE` ASCIIZ filename | Passed as `DS:DX` to `int 21h AH=4Bh` |
| `0x07F9` | DOS `EXEC` parameter block | Passed as `ES:BX` to `int 21h AH=4Bh` |
| `0x0807` | Command tail length byte | Initially `03` |
| `0x0808` | Child graphics argument byte | Rewritten before each `EXEC` |
| `0x0809` | Command tail spacer | Space |
| `0x080A` | Child audio argument byte | `S`, `T`, `A`, or `R` |
| `0x080B` | Command tail terminator | `0x0D` |
| `0x080C` | First byte of passed FCB-side data | Set to `1` before first launch, set to `2` after combat returns nonzero |
| `0x0811` | Memory class/probe flag | Set to `2` if 512K probe fails, `3` if it succeeds |
| `0x085A` | `CANT RELEASE MEMORY.` DOS `$` string | Printed if memory shrink fails |
| `0x0871` | `Please put DISK 1 in drive ` | Printed if `MW_MAIN.EXE` cannot be executed |
| `0x088D` | `Please put DISK 3 in drive ` | Printed if `BTECH.EXE` cannot be executed |
| `0x08A9` | `Not enough memory to continue.` | Printed if large memory probe fails |
| `0x08CA` | Title and graphics selection prompt | Printed with `int 21h AH=09h` |
| `0x0935` | Audio selection prompt | Present in data; command-line audio parsing avoids prompting |
| `0x09B1` | `: and press any key...` suffix | Printed after disk prompt and current drive letter |
| `0x09EA` | BTECH graphics mode byte | Copied to `0x0808` before launching `BTECH.EXE` |
| `0x09EB` | MW_MAIN graphics mode byte | Copied to `0x0808` before launching `MW_MAIN.EXE` |
| `0x09EC` | Parent PSP segment | Used to scan PSP command tail at `PSP:80h` |

## DOS EXEC parameter block

The block at `0x07F9` has the normal DOS `EXEC` layout:

```text
0x00  u16 environment_segment = 0
+0x02  far pointer to command tail: 0006:0807, relocated at load time
+0x06  far pointer to FCB1:         0006:080C, relocated at load time
+0x0A  far pointer to FCB2:         0006:0835, relocated at load time
```

The four relocation entries in the MZ header patch the segment words in this
block. The loader saves and restores `SS:SP` around `int 21h AH=4Bh`, which is
standard defensive practice for DOS `EXEC`.

The command tail passed to child programs is always three characters:

```text
<graphics> " " <audio> "\r"
```

Examples:

- `E R` means EGA-like graphics and Roland audio.
- `T T` means Tandy graphics and Tandy audio.
- `C A` is possible through the hidden command-line graphics mode `3`.

The byte at `0x080C` is not inside the command tail. It is passed through the
FCB area and appears to be a side-channel state flag:

- `1` before the first `MW_MAIN.EXE` launch.
- `2` after `BTECH.EXE` returns nonzero and control returns to `MW_MAIN.EXE`.

This should be verified from `MW_MAIN.EXE` PSP/FCB startup code.

## Command-line handling

The loader scans the original PSP command tail directly.

Graphics keys:

| Key | Effect |
| --- | --- |
| `1` | Set both child graphics bytes to `E` |
| `2` | Set both child graphics bytes to `T` |
| `3` | Set `BTECH.EXE` graphics byte to `C`; set `MW_MAIN.EXE` byte from video auto-detection |

Audio keys:

| Key | Effect |
| --- | --- |
| `S` / `s` | Set audio byte to `S`, internal speaker |
| `T` / `t` | Set audio byte to `T`, Tandy 1000 |
| `A` / `a` | Set audio byte to `A`, AdLib |
| `R` / `r` | Set audio byte to `R`, Roland MT-32 |
| none | Default to `R` |

The data contains an audio selection prompt, but the current loader control flow
does not appear to show it interactively. Audio is command-line-selected or
defaults to Roland.

## Graphics selection

Without a graphics command-line key, the loader calls the video-detection
routine at `1006:0C66`.

Observed return interpretation:

| Return | Meaning in loader |
| --- | --- |
| `0` | Unknown/unsupported; show graphics prompt |
| `1` | Tandy-like; set `T` |
| `2` | EGA-like; set `E` |
| `3` | EGA/VGA-like; set `E` |

The visible prompt lists only:

```text
1 - For EGA
2 - For Tandy
```

However, the key handler also accepts `3`, matching the hidden command-line
mode described above.

## Video detection routine

The routine at file offset `0x0EC6`, data/code offset `0x0C66`, performs these
checks:

1. Calls `int 15h` with `AX=C000h`.
2. Calls `int 10h AH=1Bh` with a local buffer at `0x0C26`.
3. If the video-state call succeeds, inspects returned capability bytes and can
   return `3` or `2`.
4. Calls `int 10h AH=12h BL=10h`, the EGA information query, and can return `2`.
5. Checks ROM signatures at `F000:FFFE` and `F000:C000`; if they match
   `0xFF` and `!`, returns `1`.
6. Otherwise returns `0`.

The exact hardware names for returns `2` and `3` need a DOSBox/hardware trace,
but the launcher treats both as `E`.

## Disk prompts

If DOS `EXEC` cannot load a child executable, the loader prints a disk prompt,
prints the current drive letter using `int 21h AH=19h`, then waits for a key.

- `MW_MAIN.EXE` failure prints the disk 1 prompt.
- `BTECH.EXE` failure prints the disk 3 prompt.
- `Esc` exits to DOS.
- Any other key retries the same executable.

## Exit-code protocol

The loader calls `int 21h AH=4Dh` after a child process returns.

Observed protocol:

- `MW_MAIN.EXE` return code `0`: exit to DOS.
- `MW_MAIN.EXE` nonzero: launch `BTECH.EXE`.
- `BTECH.EXE` return code `0`: exit to DOS.
- `BTECH.EXE` nonzero: set state byte `0x080C = 2` and launch `MW_MAIN.EXE`
  again.

This strongly suggests that `MW_MAIN.EXE` starts combat by exiting nonzero, and
`BTECH.EXE` returns nonzero when campaign control should resume.

## Open questions

- Confirm where `MW_MAIN.EXE` reads the command tail and/or FCB state bytes from
  its PSP.
- Confirm the exact meaning of the hidden `C` graphics byte passed to
  `BTECH.EXE`.
- Confirm the exact hardware distinction between video-detection returns `2`
  and `3`.
- Trace real DOSBox return codes from `MW_MAIN.EXE` and `BTECH.EXE` during a
  campaign-to-combat-to-campaign cycle.
