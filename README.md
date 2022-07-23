# Manganato Downloader
Multithreaded manga downloader which downloads images of every chapter of a given manga. Images will be stored in mangas > manga-name > chapter > [number].[ext]

## Setup
- Ensure openssl 3.0 is installed
    - linux is probably `libssl-dev`
    - macos use `brew install openssl`
        - ensure it is in the path
    - no idea for windows
- Ensure `tbb` is installed for threading
- (macOS) use `homebrew`'s  `llvm`
    - To use OpenMP
- `make setup`
     - (macOS) `make setup-mac` instead to get cmake to use homebrew's llvm
- `make`

## Usage
- `build/manganato-downloader /<path>` where path is `https://manganato.com/<path>`
    - e.g. `build/manganato /manga-cs979853`

# Hacks
- C style of assigning `void*` to a pointer of another type is not allowd in c++
    - Add a C cast to that type in `_deps/lexbor-src/source/lexbor/css/stylesheet.h:33`
