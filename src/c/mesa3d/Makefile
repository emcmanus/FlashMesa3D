libGL.swc: libGL.a
	FORCESYNC=1 gcc flash_demo.c -DFLASH -IMesa/include -LMesa/src/mesa/drivers/osmesa -lGL -swc -O5 -o libGL.swc

libGL.a:
	cd Mesa && make realclean && make flash && cd ..

default: libGL.swc

clean:
	cd Mesa; make realclean; cd ..
	rm libGL.swc
