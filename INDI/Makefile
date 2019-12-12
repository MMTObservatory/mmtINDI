# Makefile for indiserver, libindic, drivers and other misc INDI tools.

# tell make we can turn .man files into .1 files
.SUFFIXES: .1 .man

# set compiler and linker flags
CC = g++
CXX = g++
CLDFLAGS = -g -pthread
INCPATHS = -I../libastro -I../liblilxml -I.
CFLAGS = -O2 -ffast-math -Wall $(CLDFLAGS) $(INCPATHS) -D_FILE_OFFSET_BITS=64
CXXFLAGS = $(CFLAGS)
LIBPATHS = -L../libastro -L../liblilxml -L.
LDFLAGS = $(CLDFLAGS) $(LIBPATHS)
LIBS = -lindic -lastro -llilxml -lz -lm

# set target directories
MANDIR = $(MMTAOHOME)/man/cat1
BINDIR = $(MMTAOHOME)/bin
LOGDIR = $(MMTAOHOME)/logs/IS
LOGFILE = $(LOGDIR)/indiserver.log


# collections: sample drivers; man pages; CL tools
SDRIVERS = inditime indisimbad indishell indistub
MANPAGES = evalINDI.1 getINDI.1 indidevapi.1 indiserver.1 setINDI.1
TOOLS = getINDI setINDI evalINDI


# files in libindic.a
LIBCSRC = indidrivermain.cpp indidriverbase.cpp eventloop.cpp configfile.cpp
LIBCOBJ = $(LIBCSRC:.cpp=.o)


# default is to build everything
all: pass2 pass3 pass4 pass5


# standard targets
pass2: $(MANPAGES)

# make these in pass3 because they are needed by other systems
pass3: indisimbadprops.o libindic.a

pass4: indiserver $(SDRIVERS)

pass5: $(TOOLS)

install: indiserver $(BINDIR) $(MANDIR) $(TOOLS) all 
	@ for f in indiserver $(SDRIVERS) $(TOOLS) $(FITSFILES) ; do \
	    if [ ! -f $(BINDIR)/$$f -o $$f -nt $(BINDIR)/$$f ] ; then \
		echo cp -f $$f $(BINDIR) ; \
		cp -f $$f $(BINDIR)  ; \
	    fi ; \
	done
	@ for f in $(MANPAGES) ; do \
	    if [ $$f -nt $(MANDIR)/$$f ] ; then \
		echo cp -f $$f $(MANDIR) ; \
		cp -f $$f $(MANDIR) ; \
	    fi; \
	done


# targets to build required full paths
$(BINDIR):
	mkdir -p $(BINDIR)

libindic.a: $(LIBCOBJ)
	ar r libindic.a $(LIBCOBJ)

$(MANDIR):
	mkdir -p $(MANDIR)

$(LOGDIR):
	mkdir -p $(LOGDIR)


# run indiserver and all sample drivers
runsamples: $(LOGDIR) indiserver $(SDRIVERS) $(SDRIVERS)
	indiserver -v -l $(LOGDIR) $(SDRIVERS) > $(LOGFILE) 2>&1 &




# build INDI server
indiserver: indiserver.o fq.o 
	$(CC) $(LDFLAGS) -o $@ indiserver.o fq.o -llilxml



# build each INDI driver process
inditime: inditime.o localrs.o location.o libindic.a
	$(CC) $(LDFLAGS) -o $@ inditime.o localrs.o location.o $(LIBS)

indisimbad: indisimbad.o indisimbadprops.o location.o localrs.o libindic.a
	$(CC) $(LDFLAGS) -o $@ indisimbad.o indisimbadprops.o location.o localrs.o $(LIBS)

indishell: indishell.o libindic.a
	$(CC) $(LDFLAGS) -o $@ indishell.o $(LIBS)

indistub: indistub.o libindic.a
	$(CC) $(LDFLAGS) -o $@ indistub.o $(LIBS)



# build each INDI tool
getINDI: getINDI.o connect_to.o
	$(CC) $(LDFLAGS) -o $@ getINDI.o connect_to.o -llilxml -lz

setINDI: setINDI.o connect_to.o
	$(CC) $(LDFLAGS) -o $@ setINDI.o connect_to.o -llilxml

evalINDI: evalINDI.o compiler.o connect_to.o
	$(CC) $(LDFLAGS) -o $@ evalINDI.o compiler.o connect_to.o -llilxml -lm

# build man pages
.man.1:
	nroff -man $< > $@

# remove all derived files
clobber:
	touch x.o
	rm -f *.o indiserver $(SDRIVERS) $(TOOLS) $(MANPAGES) libindic.a
