require 'mkmf'
$INCFLAGS += ' -I${IBASE}/include'
$LIBS += ' -L${IBASE}/lib -lamq_wireapi -lamq_common -lsmt -licl -lipr -lasl -lapr -laprutil -lpcre'

# ropenamq needs to be built with the same C defines that are used in building
# the client library.  Find out what defines are used.
io=open("|#{ENV['IBASE']}/bin/amq_client -v")
defines = io.readlines.grep(/^Compiled with:/).first.chomp.split(/\s+/).grep(/^-D/)
io.close()
$CFLAGS += " " + defines.join(" ")
create_makefile("rwire")


