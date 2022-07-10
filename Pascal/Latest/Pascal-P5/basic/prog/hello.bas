1  rem
2  rem This program prints "Hello, world !" in a big frame
3  rem
10 clear: ascroll 0: curvis 0
20 for i = 1 to maxx%: print "*";: next i
30 cursor 1, maxy%
40 for i = 1 to maxx%: print "*";: next i
50 home
60 for i = 1 to maxy%: print "*";: cursor 1, i: next i
80 for i = 1 to maxy%: print "*";: cursor maxx%, i: print "*";: next i
90 cursor maxx%/2-len("hello, world !")/2, maxy%/2
100 print "Hello, world !"
110 home: a$ = inkey$
120 ascroll 1: curvis 1: clear