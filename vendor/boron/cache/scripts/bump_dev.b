old: 2,1,1
new: 2,96,0
files: [
    %Makefile               ["VER=$v"]
    %project.b              ["%boron $c"]
    %eval/boot.b            ["version: $c"]
    %include/boron.h [
        {BORON_VERSION_STR  "$v"}
        {BORON_VERSION      0x0$m0$i0$r}
    ]
    %include/urlan.h [
        {UR_VERSION_STR  "$v"}
        {UR_VERSION      0x0$m0$i0$r}
    ]
]
finish: [
    print "Now run eval/mkboot."
]
