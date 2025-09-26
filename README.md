## Foreword

Well, this was a fun little experiment while it lasted. Putting up a README only to inform the project is being ABANDONED.

Tired of all the typing of kubectl commands I wanted a simple GUI to abstract kubectl still providing a pass-through but leaving it only after creating ~10-15% of what I wanted.

## Acknowledgements

Kutes is built on these:

1. [NAppGUI](https://github.com/frang75/nappgui_src)
2. [yyjson](https://github.com/ibireme/yyjson)
3. [boron](https://codeberg.org/wickedsmoke/boron)
4. [rax](https://github.com/jcorporation/rax)
5. [cpm](https://github.com/cpm-cmake/CPM.cmake/)

Thanks to all the authors and contributors of above projects!

## Halt

Some of the reasons that I can think of why the project is halted (decreasing order of impactness).

### Non technical
1. I gave k9s another try and settled with it for most of the operations
2. Heavy development cycle at work and effort on kutes went silent
3. Project management became hard to catch up with NAppGUI since Kutes hooks into SDK (maybe can fork and apply rather than using CPM patches)

### Technical
1. Next feature is to support list and watch, however Kutes infers the kube resource based on Json output and kubectl doesn't provide a way to list a single resource, like lack of `kubectl get pod --limit=1 -ojson` which forces user to run full json once and then select an option to watch resources doing another full list call.
2. With kubectl we can only know the `api-resources` but I want the full resource patch to run the `get` command with `--raw` flag and custom options like `resourceVersion`, receive BOOKMARK events etc
3. Naive implementation of the App without leveraging the data binding provided by SDK which needs exploring MVVC GUI patterns and also a bit uncomforatable writing C.

## Demo

Based on how you see it, some of the functionality could just be a gimmick, so you have been warned!. Below is compressed to be within Github limit (38MB to 10MB) and could be blurry, drop an email if you want uncompressed screencast.

[kutes-demo.webm.mov](https://github.com/user-attachments/assets/3f30ccbd-8ee2-426f-8926-ac6fee4ab98f)

## Details

If you are still here, below are some of project details, maybe it could help someone else?

``` sh
> tree -I 'build*' -L2 -I compile_commands.json 
.
├── CMakeLists.txt                         # main entry for building kutes and has commented out instruction for building individual NAppGUI demos or experiments
├── LICENSE                                # may not be compatible with dependencies but yeah all the source is available, who is asking?
├── README.md
├── src                                    # the source directory
│   ├── alloc.c
│   ├── CMakeLists.txt
│   ├── const.c
│   ├── expr.c                             # boron integration
│   ├── hist.c                             # rax intergration
│   ├── kt.h
│   ├── kutes.c
│   ├── list.c
│   └── res
├── support
│   ├── CPM.cmake
│   ├── cross-mingwamd64-toolchain.cmake
│   ├── cross-osxaarch64-toolchain.cmake
│   ├── cross-osxamd64-toolchain.cmake
│   ├── exp                                # folders with runnable individual experiments
│   ├── kutes.drawio.svg
│   ├── Makefile
│   ├── manual
│   └── patches                            # patches on the packages under vendor/ dir
└── vendor                                 # all the vendor packages fetched by CPM
    ├── boron
    ├── nappgui_src
    ├── rax
    └── yyjson

11 directories, 18 files
```

You can look at individual CMake files which has good enough comments to see what they do and has targets in `support/Makefile` but YMMV. Due to NAppGUI using the platform specific GUI components the resulting binaries would be very small, as of now Kutes in release mode is <2MiB.

### Build for linux on linux
``` sh
dnf install cmake gtk3-devel ninja-build # refer nappgui for more instructions
make -C support/ linux64-c linux64-b EXTRA_ARGS="--config Release"
./build-linux64/Release/bin/kutes
```

### Build for windows on linux
``` sh
dnf install mingw64-gcc mingw64-g++
make -C support/ cross-mingwamd64-c cross-mingwamd64-b EXTRA_ARGS="--config Release"
wine ./build-cross-mingwamd64/Release/bin/kutes.exe
```

### Build for MacOS on linux
``` sh
# initial setup
podman run -v $PWD:/workspace:Z --rm -it \
ghcr.io/shepherdjerred/macos-cross-compiler:latest@sha256:a4ced303153cbbef65c7971fb742bad8423d33bc3ead276f11367b8e4ad580a2 \
/bin/bash # beware it's ~8 GiB
# commands in container [start]
apt install cmake ninja-build patch && rm -rf /var/lib/apt/lists/*
ln -sr /sdk /MacOSX13.1.sdk # referred from /sdk/SDKSettings.json
# commands in container [end]
podman commit <container-id> for-kutes # and reuse
```
For MacOS ARM and AMD
``` sh
podman run -v $PWD:/workspace:Z --rm -it localhost/for-kutes /bin/bash  
# commands in container [start]
make -C support/ cross-osxamd64-c cross-osxamd64-b EXTRA_ARGS="--config Release"
make -C support/ cross-osxaarch64-c cross-osxaarch64-b EXTRA_ARGS="--config Release"
# commands in container [end]
ls build-cross-osxa*/Release/bin/ # I don't own a Mac and not sure how it runs
```

Thanks for stopping by!
