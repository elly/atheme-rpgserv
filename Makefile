MODULE = rpgserv

include ../../extra.mk
include ../../buildsys.mk
include ../../buildsys.module.mk

SRCS = main.c 

CPPFLAGS += -I../../include
LIBS += -L../../libathemecore -lathemecore ${LDFLAGS_RPATH}
