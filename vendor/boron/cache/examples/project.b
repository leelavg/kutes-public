project: "examples"

default [
    include_from [%../include %../urlan]
    libs_from %.. %boron

    macx [
        libs [%m %z]
        universal
    ]
    unix [
        libs [%m %z %pthread]
    ]
    win32 [
        include_from %../win32
        console
    ]
]

exe %calculator [
    sources [%calculator.c]
]

exe %boron_mini [
    sources [%boron_mini.c]
]
