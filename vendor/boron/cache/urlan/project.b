lib %urlan [
    cflags "-DURLAN_LIB"
    include_from [
        %../include
        %../support
    ]
    sources [
        %array.c
        %binary.c
        %block.c
        %context.c
        %coord.c
        %date.c
        %env.c      ; %atoms.c %datatypes.c
        %gc.c
        %hashmap.c
        %i_parse_blk.c
      ; %memtrack.c
        %parse_block.c
        %parse_string.c
        %path.c
        %serialize.c
        %string.c   ; %ucs2_case.c
        %tokenize.c
        %vector.c
        %../support/str.c
        %../support/mem_util.c
      ; %../support/quickSortIndex.c
        %../support/fpconv.c
    ]
    ;linux [libs [%m]]
]

exe %calc [
    ;linux [lflags "-Wl,-z,origin,-rpath,'$ORIGIN/'"]
    linux [libs [%m]]
    win32 [console]
    include_from %../include
    libs_from %. %urlan
    sources [%../examples/calculator.c]
]
