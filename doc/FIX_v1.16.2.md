# v1.16.2 Live Decode Recovery

## Evidence

The user's screenshot showed the generic live initialization/decode error.
The local Config/scanner.log recorded this sequence twice:

```text
stream: decoder initialized codec=27
stream: frame width=1920 height=1080 format=0
stream: send packet error=-1094995529
```

Codec 27 is H.264. Error -1094995529 is AVERROR_INVALIDDATA. Initialization and
frame conversion had already succeeded. Commit 96ef29d treated every negative
send/receive result as fatal; the earlier decoder continued after bad packets.
This made a single malformed live video packet stop monitoring.

## Repair

Both official and BH3 Bilibili scanner paths now release/skip invalid packets
and continue draining after invalid decoded frames. No blanket error suppression
or decoder flush is introduced. Other fatal errors still stop scanning.
Only successfully converted video frames refresh the 10-second deadline;
corrupt-only input and cancellation remain bounded, including frame draining.
Skip messages are written to scanner.log without stream URLs or credentials.

CMake/displayed version, Windows FileVersion/ProductVersion and new artifact
names are synchronized to 1.16.2. Original EXEs, DLLs and account files are not
overwritten. Release distribution:
https://github.com/loqwe/MHY_Scanner2/releases/tag/v1.16.2.
The tag-triggered CI build is cancelled for this manual release so it cannot
replace the verified package. The publishing credential cannot edit workflows.

## Verification

New source guards fail on the old fatal-only handling and pass after the fix.
The real FFmpeg test encodes synthetic H.264 keyframes, injects malformed NAL
packets between them, reproduces the exact reported error, then verifies that
the same decoder outputs subsequent valid frames. Repeated malformed packets
do not reset the simulated no-valid-frame deadline. This passes using both the
packaged SDK DLLs and the user's original FFmpeg DLLs.

19 Python guards and ten C++/Qt regression tests pass; full MSVC x64 Release
build/install and package import checks pass. Windows EXE resource metadata is
verified as 1.16.2. UI tests run at 125%, 150% and 200% DPI.

No live account confirmation or real-room scan has been performed automatically.
The user's later local v1.16.2 run logged a skipped malformed video packet,
followed by successful Panda scan, passport scan and passport confirmation
(retcode=0). This corroborates recovery in that run, but is not a reproduction
of the original Issue #3 reporter's crash or proof of the game client's state.
Application startup smoke testing is deferred while the user's old instance
holds the single-instance mutex; no user process is stopped. The synthetic
recovery test is not claimed as a full live-room end-to-end test.

Patch base: 96ef29d812597cfef47bea35bbe67ce203ad7bda. Do not apply the incremental
v1.16.2 patch directly to the original d07fc80 baseline.
