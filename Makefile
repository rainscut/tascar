#
# main targets:
#
BINFILES = tascar_cli tascar_gui tascar_sampler				\
tascar_osc_jack_transport tascar_oscmix tascar_jackio			\
tascar_osc_recorder tascar_ambwarping tascar_hoadisplay tascar_oscctl	\
tascar_tscupdate

RECEIVERS = omni nsp amb3h0v cardioid

TASCARMODS = system pos2osc sampler

#
#
#
RECEIVERMODS = $(patsubst %,tascarreceiver_%.so,$(RECEIVERS))

TASCARMODDLLS = $(patsubst %,tascar_%.so,$(TASCARMODS))

PREFIX = /usr/local

BINFILES += $(TEST_FILES)

ifeq "ok" "$(shell pkg-config gtkmm-3.0 && echo ok)"
GTKDEF = "-DGTKMM30"
GTKEXT = gtkmm-3.0
else
GTKDEF = "-DGTKMM24"
GTKEXT = gtkmm-2.4
endif

ifeq "ok" "$(shell test -e /usr/include/linuxtrack.h && echo  ok)"
LTRDEF = "-DLINUXTRACK"
LDLIBS += -llinuxtrack
endif

ifeq "$(DEBUG)" "yes"
CXXFLAGS += -g
else
CXXFLAGS += -O3 
endif

#BINFILES += `pkg-config gtkmm-3.0 && echo tascar_gui`

CXXFLAGS += $(GTKDEF) $(LTRDEF)

OBJECTS = coordinates.o dynamicobjects.o scene.o session.o receivermod.o	\
jackclient.o delayline.o speakerlayout.o multipan.o osc_helper.o	\
async_file.o errorhandling.o audiochunks.o acousticmodel.o multipan.o	\
multipan_amb3.o hoafilt.o xmlconfig.o osc_scene.o audioplayer.o		\
ringbuffer.o gammatone.o viewport.o sampler.o jackiowav.o

GUIOBJECTS = gui_elements.o

INSTBIN = $(patsubst %,$(PREFIX)/bin/%,$(BINFILES)) \
	$(patsubst %,$(PREFIX)/lib/%,$(RECEIVERMODS)) \
	$(patsubst %,$(PREFIX)/lib/%,$(TASCARMODDLLS))

#GTKMMBIN = tascar_gui

CXXFLAGS += -fPIC -Wall -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -fno-finite-math-only -L./
EXTERNALS = jack libxml++-2.6 liblo sndfile
#EXTERNALS = jack liblo sndfile

$(GUIOBJECTS): EXTERNALS += gtkmm-3.0

#tascar_gui tascar_renderer tascar_audioplayer: OBJECTS += /usr/lib/libxml++-2.6.a
#tascar_gui tascar_renderer tascar_audioplayer: CXXFLAGS += -I/usr/include/libxml++-2.6/

tascar_hoadisplay: EXTERNALS += $(GTKEXT)

tascar_gui tascar_pdf: LDLIBS += -ltascargui `pkg-config --libs $(EXTERNALS)`
tascar_gui tascar_pdf: EXTERNALS += $(GTKEXT)
#tascar_gui: gui_elements.o

LDLIBS += -ltascar -ldl

LDLIBS += `pkg-config --libs $(EXTERNALS)`
CXXFLAGS += `pkg-config --cflags $(EXTERNALS)`

#CXXFLAGS += -ggdb

all: lib
	mkdir -p build
	$(MAKE) -C build -f ../Makefile $(BINFILES)

lib:
	mkdir -p build
	$(MAKE) -C build -f ../Makefile libtascar.a libtascargui.a $(RECEIVERMODS) $(TASCARMODDLLS)

libtascar.a: $(OBJECTS)
	ar rcs $@ $^

libtascargui.a: $(GUIOBJECTS)
	ar rcs $@ $^

install:
	$(MAKE) -C build -f ../Makefile $(INSTBIN)

uninstall:
	rm -f $(INSTBIN)

clean:
	rm -Rf *~ src/*~ build doc/html

VPATH = ../src ../src/hoafilt

.PHONY : doc

doc:
	cd doc && sed -e 's/PROJECT.NUMBER.*=.*/&'`cat ../version`'/1' doxygen.cfg > .temp.cfg && doxygen .temp.cfg
	rm -Rf doc/.temp.cfg

include $(wildcard *.mk)

tascar_gui: libtascargui.a

$(BINFILES): libtascar.a

$(PREFIX)/bin/%: %
	cp $< $@

%: %.o
	$(CXX) $(CXXFLAGS) $(LDLIBS) $^ -o $@

%.o: %.cc
	$(CPP) $(CPPFLAGS) -MM -MF $(@:.o=.mk) $<
	$(CXX) $(CXXFLAGS) -c $< -o $@

#dist: clean doc
dist: clean
	$(MAKE) -C doc
	$(MAKE) DISTNAME=tascar-`cat version` bz2

disttest:
	$(MAKE) DISTNAME=tascar-`cat version` disttest2

disttest2:
	rm -Rf $(DISTNAME) && tar xjf $(DISTNAME).tar.bz2
	cd $(DISTNAME) && $(MAKE)

bz2:
	rm -Rf $(DISTNAME) $(DISTNAME).files
	(cat files; find doc/html -print) | sed 's/.*/$(DISTNAME)\/&/g' > $(DISTNAME).files
	ln -s . $(DISTNAME)
	tar cjf $(DISTNAME).tar.bz2 --no-recursion -T $(DISTNAME).files
	rm -Rf $(DISTNAME) $(DISTNAME).files


tascar_ambdecoder: LDLIBS += `pkg-config --libs gsl`

tascarreceiver_%.so: receivermod_%.cc
	$(CXX) -shared -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LDLIBS)

tascar_%.so: tascarmod_%.cc
	$(CXX) -shared -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LDLIBS)


# Local Variables:
# compile-command: "make"
# End:
