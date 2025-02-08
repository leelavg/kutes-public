#!/usr/bin/boron -s
/*
  Inject styles into HTML <pre><code> blocks to colorize code examples.

  Pandoc supports syntax highlighting, but there are a number of hurdles
  to overcome.
    * The highlight styles can conflict with a user provided stylesheet.
      It messes with indentation and background color.
    * A KDE syntax theme (XML file) must be created.
    * An extra semantic mapping is needed between REPL (prompt, input) spans
      and language (keyword, comment, etc.) spans.
    * The source becomes messy with the fenced_code_blocks & attributes.
    * The additional style code is unnecessarily large.

  The UserManual.html size increases from each method are:
    instyle: 1869 bytes (0.7%) larger.
    Pandoc: 13450 bytes (5.0%) larger.
*/

usage: {{
    Usage: instyle [OPTIONS] <html-file>

    Options:
      -h            Print this help and quit.
      -o <file>     Write an output file instead printing to stdout.
}}

output:
file: none

forall args [
    switch first args [
        "-h" [print usage quit]
        "-o" [output: second ++ args]
       ;"-t" [theme: second ++ args]
        [file: first args]
    ]
]
ifn file [
    print usage
    quit/return 64
]

instyle: context [
    prompt: ")&gt;"

    st-prompt: {<i style="color: #A71">}
    st-input:  {<i style="color: #000">}
    close-i: "</i>"

    markup-code: func [code] [
        ifn find code prompt [return code]

        out: make string! mul 2 size? code
        append out {<pre class="repl">}
        code: skip code 5

        parse code [some[
            to prompt ppos: to ' ' input: to '^/' end-input: (
                append out reduce [
                    slice code ppos
                    st-prompt prompt close-i
                    st-input slice input end-input close-i
                ]
                code: end-input
            )
        ]]
        append out code
    ]

    set 'inject-style func [html] [
        out: reserve make string! 'utf8
                add 1024 size? html
        parse html [some[
            to "<pre><code>" code: to "</code>" end-code: (
                appair out slice html code
                           markup-code slice code end-code
                html: end-code
            )
        ]]
        append out html
    ]
]

html: inject-style read/text file
either output [
    write output html
][
    print html
]
