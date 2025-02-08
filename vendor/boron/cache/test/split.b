white: charset " ^-^/"

print "---- not found"
probe split "a bit of time" ':'
probe split "heel-on-shoe" white
probe split #{CC00DEAD00BEEF} 0x01
probe split [blk | of time | blah] 'x

print "---- normal"
probe split "a bit of time" ' '
probe split "heel on^/shoe" white
probe split #{CC00DEAD00BEEF} 0x00
probe split [blk | of time | blah] '|

print "---- subsequent delim"
probe split "A,Bee,,,C" ','
probe split "  a  bit   of  time  " ' '
probe split "^/^-heel^/^-on   shoe^/^- " white
probe split #{0000CC0000DEAD000000BEEF0000} 0x00
probe split [+ + blk + + of time + + + blah + +] '+

print "---- keep"
probe split/keep "A,Bee,,,C" ','
probe split/keep "  a  bit   of  time  " ' '
probe split/keep "^/^-heel^/^-on   shoe^/^- " white
probe split/keep #{0000CC0000DEAD000000BEEF0000} 0x00
probe split/keep [+ + blk + + of time + + + blah + +] '+
