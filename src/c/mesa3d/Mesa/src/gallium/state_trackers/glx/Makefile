TOP = ../../../..
include $(TOP)/configs/current

# Commented the following lines: #ed #tmp - ed

SUBDIRS = # xlib


default: # subdirs


# subdirs:
#   @for dir in $(SUBDIRS) ; do \
#       if [ -d $$dir ] ; then \
#           (cd $$dir && $(MAKE)) || exit 1 ; \
#       fi \
#   done


clean:
	rm -f `find . -name \*.[oa]`
	rm -f `find . -name depend`


# Dummy install target
install:
