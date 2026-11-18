# Adding to picocalc-extras

New pieces, fixes and better ideas are welcome. If you've written something
for your own PicoCalc program that another program could use, it may belong
here rather than in a copy of its own: one place to fix it, and everyone
who pins a release gets the fix.

## What a piece looks like

- **A folder of its own** (`mything/`), with the code and a `.cmake` file
  that a program brings in with one `include()`.
- **A README** in the folder: what it does, what it needs, and a section
  called **"Using it in a program"** with the lines to copy (CMake and C),
  like the one in [`bluetooth/README.md`](bluetooth/README.md).
- **The same ground as the rest:** an RP2350 PicoCalc, the Pico SDK, and
  Blair Leduc's [picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
  drivers where it touches the screen, keyboard or card. Say so in the
  README if it needs more (a Pico 2 W, the SDK's `develop` branch).
- **MIT-licensed**, like the rest, or under the licence of the code it's
  built on if that's what that code requires; say which at the top of
  each file.
- **A line in the table** in the main README.
- If you can, **a program that uses it** (yours, or a small one in
  `examples/`), so others have something to copy from.

## How to send it

Open a pull request, or an issue first if you'd like to talk it over.
Pieces are tried on a real PicoCalc before a release, and each release is
tagged so programs can pin it as a git submodule; a change that would
break programs pinned to the current release goes in the next major
version, and the release notes say what to change.

Fixes to an existing piece are just as welcome: a pull request with what
was wrong and how you found it.
