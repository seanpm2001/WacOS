# Start of script
# The main configuration script for Sherman
# Note: I am not very experienced with M4, so this program may not work
# Sources: https://github.com/tsuna/boost.m4

m4_define([main], [m4_translit([

return _SHERMAN
break
wait 10
exit

])])

# Sherman method

m4_define([_SHERMAN], [m4_translit([
# serial 36
get method _setup1
], [#
], [])])

# Setup1

m4_define([_setup1], [m4_translit([
rm -rf /.github/
# There should be separate setup options for what gets removed and what doesn't. I can't do that right now due to a lack of experience, and not enough time.

])])

# File info
# File type: m4 source file (*.m4)
# File version: 1 (2021, Saturday, December 25th at 4:05 pm)
# Line count (including blank lines and compiler line): 37

# End of script
